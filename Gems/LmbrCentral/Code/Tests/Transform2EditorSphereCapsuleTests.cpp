/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "LmbrCentralReflectionTest.h"
#include "Shape/CapsuleShapeComponent.h"
#include "Shape/EditorCapsuleShapeComponent.h"
#include "Shape/EditorSphereShapeComponent.h"
#include "Shape/SphereShapeComponent.h"
#include <AZTestShared/Math/MathTestHelpers.h>
#include <AZTestShared/Utils/Utils.h>
#include <AzFramework/UnitTest/TestDebugDisplayRequests.h>
#include <AzFramework/Visibility/BoundsBus.h>
#include <AzToolsFramework/API/ComponentEntitySelectionBus.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <AzToolsFramework/Manipulators/ShapeManipulatorRequestBus.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <EditorShapeTestUtils.h>

// The Editor Sphere and Capsule draw, select and are edited on the mirrored side of a flipped entity. The offset that is saved is the
// authored one all the way. The Box half of this is in EditorBoxShapeComponentTests.

namespace LmbrCentral
{
    namespace
    {
        // The mask for an X and a Z flip, which an entity with a scale of (-1, 1, -1) pushes to its shapes.
        constexpr AZ::u8 FlipMaskXZ = 5;
        const AZ::Vector3 FlipSignsXZ(-1.0f, 1.0f, -1.0f);

        // An Editor entity with one shape component on it.
        struct EditorShapeEntity
        {
            AZ::Entity* m_entity = nullptr;
            AZ::EntityId m_entityId;
            AZ::EntityComponentIdPair m_entityComponentIdPair;
        };

        // Records the matrices a shape pushes. A shape that draws a mesh pushes its offset as the last of them, and an Editor that has
        // no geometry system generates no mesh, so this is what tells where the capsule is drawn.
        class PushedMatrixRecorder
            : public AzFramework::DebugDisplayRequests
        {
        public:
            void PushMatrix(const AZ::Transform& tm) override
            {
                m_pushedMatrices.push_back(tm);
            }

            AZStd::vector<AZ::Transform> m_pushedMatrices;
        };

        // A ray that comes in from the side, so none of its components is zero, and passes through the target.
        bool SelectionRayHits(AZ::EntityId entityId, const AZ::Vector3& target)
        {
            const AZ::Vector3 src = target + AZ::Vector3(1.0f, 10.0f, 10.0f);
            const AZ::Vector3 dir = (target - src).GetNormalized();
            bool hit = false;
            float distance = 0.0f;
            AzToolsFramework::EditorComponentSelectionRequestsBus::EventResult(
                hit, entityId, &AzToolsFramework::EditorComponentSelectionRequests::EditorSelectionIntersectRayViewport,
                AzFramework::ViewportInfo{ 0 }, src, dir, distance);
            return hit;
        }

        // The handles are given the offset of the shape as drawn, and what they write is taken back as authored.
        void ExpectHandlesSitOnTheFlippedShape(const EditorShapeEntity& shape, const AZ::Vector3& authoredOffset)
        {
            AZ::Vector3 handleOffset = AZ::Vector3::CreateZero();
            AzToolsFramework::ShapeManipulatorRequestBus::EventResult(
                handleOffset, shape.m_entityComponentIdPair, &AzToolsFramework::ShapeManipulatorRequestBus::Events::GetTranslationOffset);
            EXPECT_THAT(handleOffset, UnitTest::IsClose(authoredOffset * FlipSignsXZ));

            // Writing back what was read stores the authored offset.
            AzToolsFramework::ShapeManipulatorRequestBus::Event(
                shape.m_entityComponentIdPair, &AzToolsFramework::ShapeManipulatorRequestBus::Events::SetTranslationOffset, handleOffset);
            ExpectTranslationOffset(shape.m_entityId, authoredOffset);

            // A handle dragged one unit along X on the drawn shape moves the authored offset one unit the other way.
            AzToolsFramework::ShapeManipulatorRequestBus::Event(
                shape.m_entityComponentIdPair, &AzToolsFramework::ShapeManipulatorRequestBus::Events::SetTranslationOffset,
                handleOffset + AZ::Vector3::CreateAxisX());
            ExpectTranslationOffset(shape.m_entityId, authoredOffset - AZ::Vector3::CreateAxisX());
        }

        // The game entity gets the authored shape, and the Transform 2 of the game entity flips it again.
        template<typename GameShapeComponent, typename ShapeConfig>
        void ExpectBuildGameEntityKeepsTheAuthoredOffset(const EditorShapeEntity& shape, const AZ::Vector3& authoredOffset)
        {
            auto* editorComponent = azrtti_cast<AzToolsFramework::Components::EditorComponentBase*>(
                shape.m_entity->FindComponent(shape.m_entityComponentIdPair.GetComponentId()));
            ASSERT_NE(editorComponent, nullptr);

            AZ::Entity gameEntity;
            editorComponent->BuildGameEntity(&gameEntity);

            auto* gameShape = gameEntity.FindComponent<GameShapeComponent>();
            ASSERT_NE(gameShape, nullptr);
            ShapeConfig gameConfiguration;
            gameShape->GetConfiguration(gameConfiguration);
            EXPECT_THAT(gameConfiguration.m_translationOffset, UnitTest::IsClose(authoredOffset));
        }
    } // namespace

    // A Sphere and a Capsule on two Editor entities, as the Editor builds them.
    class Transform2EditorSphereCapsuleFixture
        : public UnitTest::ToolsApplicationFixture<>
    {
    public:
        void SetUpEditorFixtureImpl() override
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);

            // EditorSphereShapeComponent has to be reflected in order for EditorBaseShapeComponent to be reflected
            m_editorSphereShapeComponentDescriptor.reset(EditorSphereShapeComponent::CreateDescriptor());
            m_editorCapsuleShapeComponentDescriptor.reset(EditorCapsuleShapeComponent::CreateDescriptor());

            ShapeComponentConfig::Reflect(serializeContext);
            SphereShape::Reflect(serializeContext);
            CapsuleShape::Reflect(serializeContext);
            m_editorSphereShapeComponentDescriptor->Reflect(serializeContext);
            m_editorCapsuleShapeComponentDescriptor->Reflect(serializeContext);

            m_sphere = CreateShapeEntity("SphereShapeComponentEntity", EditorSphereShapeComponentTypeId);
            m_capsule = CreateShapeEntity("CapsuleShapeComponentEntity", EditorCapsuleShapeComponentTypeId);
        }

        void TearDownEditorFixtureImpl() override
        {
            DestroyShapeEntity(m_sphere);
            DestroyShapeEntity(m_capsule);

            m_editorSphereShapeComponentDescriptor.reset();
            m_editorCapsuleShapeComponentDescriptor.reset();
        }

        // Sets the authored offset of the shape, then flips it for an entity with a scale of (-1, 1, -1).
        static void SetOffsetAndFlip(const EditorShapeEntity& shape, const AZ::Vector3& translationOffset)
        {
            ShapeComponentRequestsBus::Event(shape.m_entityId, &ShapeComponentRequests::SetTranslationOffset, translationOffset);
            ShapeComponentRequestsBus::Event(shape.m_entityId, &ShapeComponentRequests::SetFlipMask, FlipMaskXZ);
        }

        EditorShapeEntity m_sphere;
        EditorShapeEntity m_capsule;

    private:
        static EditorShapeEntity CreateShapeEntity(const char* name, const AZ::Uuid& componentType)
        {
            EditorShapeEntity shape;
            UnitTest::CreateDefaultEditorEntity(name, &shape.m_entity);
            shape.m_entityId = shape.m_entity->GetId();
            shape.m_entity->Deactivate();
            shape.m_entityComponentIdPair =
                AZ::EntityComponentIdPair(shape.m_entityId, shape.m_entity->CreateComponent(componentType)->GetId());
            shape.m_entity->Activate();
            return shape;
        }

        static void DestroyShapeEntity(EditorShapeEntity& shape)
        {
            AzToolsFramework::EditorEntityContextRequestBus::Broadcast(
                &AzToolsFramework::EditorEntityContextRequestBus::Events::DestroyEditorEntity, shape.m_entityId);
            shape.m_entity = nullptr;
            shape.m_entityId.SetInvalid();
        }

        AZStd::unique_ptr<AZ::ComponentDescriptor> m_editorSphereShapeComponentDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_editorCapsuleShapeComponentDescriptor;
    };

    //////////////////////////////////////////////////////////////////////////
    // Sphere

    TEST_F(Transform2EditorSphereCapsuleFixture, SphereHandlesSitOnTheFlippedSphereAndWriteTheAuthoredOffset)
    {
        const AZ::Vector3 authoredOffset(2.0f, 0.0f, 1.0f);
        SetOffsetAndFlip(m_sphere, authoredOffset);

        ExpectHandlesSitOnTheFlippedShape(m_sphere, authoredOffset);
    }

    TEST_F(Transform2EditorSphereCapsuleFixture, SphereWireframeIsDrawnOnTheFlippedSideAndTheOffsetStaysAuthored)
    {
        const AZ::Vector3 authoredOffset(3.0f, 0.0f, 0.0f);
        ShapeComponentRequestsBus::Event(m_sphere.m_entityId, &ShapeComponentRequests::SetTranslationOffset, authoredOffset);

        auto getDrawnCenter = [this]()
        {
            UnitTest::TestDebugDisplayRequests testDebugDisplayRequests;
            AzFramework::EntityDebugDisplayEventBus::Event(
                m_sphere.m_entityId, &AzFramework::EntityDebugDisplayEvents::DisplayEntityViewport, AzFramework::ViewportInfo{ 0 },
                testDebugDisplayRequests);
            return testDebugDisplayRequests.GetAabb().GetCenter();
        };

        // The wire sphere is only an approximation of a sphere, so the center of what it draws is checked with some slack.
        EXPECT_THAT(getDrawnCenter(), UnitTest::IsCloseTolerance(authoredOffset, 0.1f));

        ShapeComponentRequestsBus::Event(m_sphere.m_entityId, &ShapeComponentRequests::SetFlipMask, FlipMaskXZ);

        EXPECT_THAT(getDrawnCenter(), UnitTest::IsCloseTolerance(authoredOffset * FlipSignsXZ, 0.1f));
        ExpectTranslationOffset(m_sphere.m_entityId, authoredOffset);

        ShapeComponentRequestsBus::Event(m_sphere.m_entityId, &ShapeComponentRequests::SetFlipMask, static_cast<AZ::u8>(0));

        EXPECT_THAT(getDrawnCenter(), UnitTest::IsCloseTolerance(authoredOffset, 0.1f));
    }

    TEST_F(Transform2EditorSphereCapsuleFixture, SphereSelectionAndBoundsFollowTheFlip)
    {
        const AZ::Vector3 authoredOffset(3.0f, 0.0f, 0.0f);
        SphereShapeComponentRequestsBus::Event(m_sphere.m_entityId, &SphereShapeComponentRequests::SetRadius, 1.0f);
        ShapeComponentRequestsBus::Event(m_sphere.m_entityId, &ShapeComponentRequests::SetTranslationOffset, authoredOffset);

        EXPECT_TRUE(SelectionRayHits(m_sphere.m_entityId, authoredOffset));
        EXPECT_FALSE(SelectionRayHits(m_sphere.m_entityId, -authoredOffset));

        ShapeComponentRequestsBus::Event(m_sphere.m_entityId, &ShapeComponentRequests::SetFlipMask, FlipMaskXZ);

        // Click select goes to the drawn side, and not to where the sphere was.
        EXPECT_TRUE(SelectionRayHits(m_sphere.m_entityId, -authoredOffset));
        EXPECT_FALSE(SelectionRayHits(m_sphere.m_entityId, authoredOffset));

        // The selection box, Focus and the visibility bounds all come from these.
        AZ::Aabb worldBounds = AZ::Aabb::CreateNull();
        AzFramework::BoundsRequestBus::EventResult(
            worldBounds, m_sphere.m_entityId, &AzFramework::BoundsRequestBus::Events::GetWorldBounds);
        EXPECT_THAT(worldBounds.GetCenter(), UnitTest::IsClose(-authoredOffset));

        AZ::Aabb localBounds = AZ::Aabb::CreateNull();
        AzFramework::BoundsRequestBus::EventResult(
            localBounds, m_sphere.m_entityId, &AzFramework::BoundsRequestBus::Events::GetLocalBounds);
        EXPECT_THAT(localBounds.GetCenter(), UnitTest::IsClose(-authoredOffset));
    }

    TEST_F(Transform2EditorSphereCapsuleFixture, SphereBuildGameEntityKeepsTheAuthoredConfigurationUnderAFlip)
    {
        const AZ::Vector3 authoredOffset(2.0f, 0.0f, 1.0f);
        SetOffsetAndFlip(m_sphere, authoredOffset);

        ExpectBuildGameEntityKeepsTheAuthoredOffset<SphereShapeComponent, SphereShapeConfig>(m_sphere, authoredOffset);
    }

    //////////////////////////////////////////////////////////////////////////
    // Capsule

    TEST_F(Transform2EditorSphereCapsuleFixture, CapsuleHandlesSitOnTheFlippedCapsuleAndWriteTheAuthoredOffset)
    {
        const AZ::Vector3 authoredOffset(2.0f, 0.0f, 1.0f);
        SetOffsetAndFlip(m_capsule, authoredOffset);

        ExpectHandlesSitOnTheFlippedShape(m_capsule, authoredOffset);
    }

    TEST_F(Transform2EditorSphereCapsuleFixture, CapsuleWireframeIsDrawnOnTheFlippedSideAndTheOffsetStaysAuthored)
    {
        const AZ::Vector3 authoredOffset(3.0f, 0.0f, 1.0f);
        ShapeComponentRequestsBus::Event(m_capsule.m_entityId, &ShapeComponentRequests::SetTranslationOffset, authoredOffset);

        auto getDrawnOffset = [this]()
        {
            PushedMatrixRecorder recorder;
            AzFramework::EntityDebugDisplayEventBus::Event(
                m_capsule.m_entityId, &AzFramework::EntityDebugDisplayEvents::DisplayEntityViewport, AzFramework::ViewportInfo{ 0 },
                recorder);
            return recorder.m_pushedMatrices.empty() ? AZ::Vector3::CreateZero() : recorder.m_pushedMatrices.back().GetTranslation();
        };

        EXPECT_THAT(getDrawnOffset(), UnitTest::IsClose(authoredOffset));

        ShapeComponentRequestsBus::Event(m_capsule.m_entityId, &ShapeComponentRequests::SetFlipMask, FlipMaskXZ);

        EXPECT_THAT(getDrawnOffset(), UnitTest::IsClose(authoredOffset * FlipSignsXZ));
        ExpectTranslationOffset(m_capsule.m_entityId, authoredOffset);

        ShapeComponentRequestsBus::Event(m_capsule.m_entityId, &ShapeComponentRequests::SetFlipMask, static_cast<AZ::u8>(0));

        EXPECT_THAT(getDrawnOffset(), UnitTest::IsClose(authoredOffset));
    }

    TEST_F(Transform2EditorSphereCapsuleFixture, CapsuleSelectionAndBoundsFollowTheFlip)
    {
        const AZ::Vector3 authoredOffset(3.0f, 0.0f, 0.0f);
        CapsuleShapeComponentRequestsBus::Event(m_capsule.m_entityId, &CapsuleShapeComponentRequests::SetRadius, 1.0f);
        CapsuleShapeComponentRequestsBus::Event(m_capsule.m_entityId, &CapsuleShapeComponentRequests::SetHeight, 4.0f);
        ShapeComponentRequestsBus::Event(m_capsule.m_entityId, &ShapeComponentRequests::SetTranslationOffset, authoredOffset);

        EXPECT_TRUE(SelectionRayHits(m_capsule.m_entityId, authoredOffset));
        EXPECT_FALSE(SelectionRayHits(m_capsule.m_entityId, -authoredOffset));

        ShapeComponentRequestsBus::Event(m_capsule.m_entityId, &ShapeComponentRequests::SetFlipMask, FlipMaskXZ);

        // Click select goes to the drawn side, and not to where the capsule was.
        EXPECT_TRUE(SelectionRayHits(m_capsule.m_entityId, -authoredOffset));
        EXPECT_FALSE(SelectionRayHits(m_capsule.m_entityId, authoredOffset));

        // The selection box, Focus and the visibility bounds all come from these.
        AZ::Aabb worldBounds = AZ::Aabb::CreateNull();
        AzFramework::BoundsRequestBus::EventResult(
            worldBounds, m_capsule.m_entityId, &AzFramework::BoundsRequestBus::Events::GetWorldBounds);
        EXPECT_THAT(worldBounds.GetCenter(), UnitTest::IsClose(-authoredOffset));

        AZ::Aabb localBounds = AZ::Aabb::CreateNull();
        AzFramework::BoundsRequestBus::EventResult(
            localBounds, m_capsule.m_entityId, &AzFramework::BoundsRequestBus::Events::GetLocalBounds);
        EXPECT_THAT(localBounds.GetCenter(), UnitTest::IsClose(-authoredOffset));
    }

    TEST_F(Transform2EditorSphereCapsuleFixture, CapsuleBuildGameEntityKeepsTheAuthoredConfigurationUnderAFlip)
    {
        const AZ::Vector3 authoredOffset(2.0f, 0.0f, 1.0f);
        SetOffsetAndFlip(m_capsule, authoredOffset);

        ExpectBuildGameEntityKeepsTheAuthoredOffset<CapsuleShapeComponent, CapsuleShapeConfig>(m_capsule, authoredOffset);
    }
} // namespace LmbrCentral
