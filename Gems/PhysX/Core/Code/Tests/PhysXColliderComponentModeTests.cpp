/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <TestColliderComponent.h>
#include <TestMeshColliderComponent.h>

#include <AzManipulatorTestFramework/IndirectManipulatorViewportInteraction.h>
#include <AzManipulatorTestFramework/AzManipulatorTestFrameworkTestHelpers.h>
#include <AzManipulatorTestFramework/AzManipulatorTestFrameworkUtils.h>
#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <AzToolsFramework/ViewportSelection/EditorInteractionSystemViewportSelectionRequestBus.h>
#include <AzToolsFramework/ViewportSelection/EditorDefaultSelection.h>
#include <AzToolsFramework/ViewportSelection/EditorSelectionUtil.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/Viewport/ViewportSettings.h>
#include <AzToolsFramework/ViewportUi/ViewportUiManager.h>
#include <AzToolsFramework/ToolsComponents/EditorNonUniformScaleComponent.h>
#include <EditorColliderComponent.h>
#include <EditorRigidBodyComponent.h>
#include <EditorStaticRigidBodyComponent.h>
#include <Source/Transform2ColliderSupport.h>
#include <Tests/EditorTestUtilities.h>

namespace UnitTest
{
    class PhysXColliderComponentModeTest
        : public ToolsApplicationFixture<false>
    {
    protected:
        AZ::ComponentId m_colliderComponentId;

        AZ::Entity* CreateEntityWithTestColliderComponent()
        {
            AZ::Entity* entity = nullptr;
            AZ::EntityId entityId = CreateDefaultEditorEntity("ComponentModeEntity", &entity);

            entity->Deactivate();

            // Add placeholder component which implements component mode.
            auto colliderComponent = entity->CreateComponent<TestColliderComponent>();

            m_colliderComponentId = colliderComponent->GetId();

            entity->Activate();

            AzToolsFramework::SelectEntity(entityId);

            return entity;
        }

        // Needed to support ViewportUi request calls.
        ViewportManagerWrapper m_viewportManagerWrapper;

        void SetUpEditorFixtureImpl() override
        {
            m_viewportManagerWrapper.Create();
        }
        void TearDownEditorFixtureImpl() override
        {
            m_viewportManagerWrapper.Destroy();
        }
    };

    TEST_F(PhysXColliderComponentModeTest, MouseWheelUpShouldSetNextMode)
    {
        // Given there is a collider component in component mode.
        CreateEntityWithTestColliderComponent();
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // When the mouse wheel is scrolled while holding ctrl
        AzToolsFramework::ViewportInteraction::MouseInteractionEvent
            interactionEvent(AzToolsFramework::ViewportInteraction::MouseInteraction(), 1.0f);
        interactionEvent.m_mouseEvent = AzToolsFramework::ViewportInteraction::MouseEvent::Wheel;
        interactionEvent.m_mouseInteraction.m_keyboardModifiers.m_keyModifiers =
            static_cast<AZ::u32>(AzToolsFramework::ViewportInteraction::KeyboardModifier::Ctrl);

        using MouseInteractionResult = AzToolsFramework::ViewportInteraction::MouseInteractionResult;
        MouseInteractionResult handled = MouseInteractionResult::None;
        AzToolsFramework::EditorInteractionSystemViewportSelectionRequestBus::BroadcastResult(handled,
            &AzToolsFramework::ViewportInteraction::InternalMouseViewportRequests::InternalHandleAllMouseInteractions,
            interactionEvent);

        // Then the component mode is cycled.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(handled, MouseInteractionResult::Viewport);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Dimensions, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, MouseWheelDownShouldSetPreviousMode)
    {
        // Given there is a collider component in component mode.
        CreateEntityWithTestColliderComponent();
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // When the mouse wheel is scrolled while holding ctrl
        AzToolsFramework::ViewportInteraction::MouseInteractionEvent
            interactionEvent(AzToolsFramework::ViewportInteraction::MouseInteraction(), -1.0f);
        interactionEvent.m_mouseEvent = AzToolsFramework::ViewportInteraction::MouseEvent::Wheel;
        interactionEvent.m_mouseInteraction.m_keyboardModifiers.m_keyModifiers =
            static_cast<AZ::u32>(AzToolsFramework::ViewportInteraction::KeyboardModifier::Ctrl);

        using MouseInteractionResult = AzToolsFramework::ViewportInteraction::MouseInteractionResult;
        MouseInteractionResult handled = MouseInteractionResult::None;
        AzToolsFramework::EditorInteractionSystemViewportSelectionRequestBus::BroadcastResult(handled,
            &AzToolsFramework::ViewportInteraction::InternalMouseViewportRequests::InternalHandleAllMouseInteractions,
            interactionEvent);

        // Then the component mode is cycled.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(handled, MouseInteractionResult::Viewport);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Rotation, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKey1ShouldSetOffsetMode)
    {
        // Given there is a collider component in component mode.
        CreateEntityWithTestColliderComponent();
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // When the '1' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_1);

        // Then the component mode is set to Offset.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKey2ShouldSetRotationMode)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        AzToolsFramework::SelectEntity(colliderEntity->GetId());
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // When the '2' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_2);

        // Then the component mode is set to Rotation.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Rotation, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKey3ShouldSetSizeMode)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        AzToolsFramework::SelectEntity(colliderEntity->GetId());
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // When the '3' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_3);

        // Then the component mode is set to Size.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Dimensions, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKeyRShouldResetSphereRadius)
    {
        // Given there is a sphere collider in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        float radius = 5.0f;
        colliderEntity->FindComponent<TestColliderComponent>()->SetShapeType(Physics::ShapeType::Sphere);
        colliderEntity->FindComponent<TestColliderComponent>()->SetSphereRadius(radius);

        AzToolsFramework::SelectEntity(colliderEntity->GetId());
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequestBus::Broadcast(&PhysX::ColliderComponentModeRequests::SetCurrentMode,
            PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // When the 'R' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_R);

        // The the sphere radius should be reset.
        radius = colliderEntity->FindComponent<TestColliderComponent>()->GetSphereRadius();
        EXPECT_FLOAT_EQ(0.5f, radius);
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKeyRShouldResetCapsuleSize)
    {
        // Given there is a capsule collider in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        float height = 10.0f;
        float radius = 2.5f;
        colliderEntity->FindComponent<TestColliderComponent>()->SetShapeType(Physics::ShapeType::Capsule);
        colliderEntity->FindComponent<TestColliderComponent>()->SetCapsuleHeight(height);
        colliderEntity->FindComponent<TestColliderComponent>()->SetCapsuleRadius(radius);

        AzToolsFramework::SelectEntity(colliderEntity->GetId());
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequestBus::Broadcast(&PhysX::ColliderComponentModeRequests::SetCurrentMode,
            PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // When the 'R' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_R);

        // Then the capsule size should be reset.
        height = colliderEntity->FindComponent<TestColliderComponent>()->GetCapsuleHeight();
        radius = colliderEntity->FindComponent<TestColliderComponent>()->GetCapsuleRadius();
        EXPECT_FLOAT_EQ(1.0f, height);
        EXPECT_FLOAT_EQ(0.25f, radius);
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKeyRShouldResetOffset)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        AZ::Vector3 offset(5.0f, 6.0f, 7.0f);
        colliderEntity->FindComponent<TestColliderComponent>()->SetColliderOffset(offset);
        AzToolsFramework::SelectEntity(colliderEntity->GetId());
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequestBus::Broadcast(&PhysX::ColliderComponentModeRequests::SetCurrentMode,
            PhysX::ColliderComponentModeRequests::SubMode::Offset);

        // When the 'R' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_R);

        // Then the collider offset should be reset.
        offset = colliderEntity->FindComponent<TestColliderComponent>()->GetColliderOffset();
        EXPECT_THAT(offset, IsClose(AZ::Vector3::CreateZero()));
    }

    TEST_F(PhysXColliderComponentModeTest, PressingKeyRShouldResetRotation)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        AZ::Quaternion rotation = AZ::Quaternion::CreateFromAxisAngle(AZ::Vector3::CreateAxisZ(), 45.0f);
        colliderEntity->FindComponent<TestColliderComponent>()->SetColliderRotation(rotation);
        AzToolsFramework::SelectEntity(colliderEntity->GetId());
        EnterComponentMode<TestColliderComponent>();

        PhysX::ColliderComponentModeRequestBus::Broadcast(&PhysX::ColliderComponentModeRequests::SetCurrentMode,
            PhysX::ColliderComponentModeRequests::SubMode::Rotation);

        // When the 'R' key is pressed
        QTest::keyPress(&m_editorActions.m_componentModeWidget, Qt::Key_R);

        // Then the collider rotation should be reset.
        rotation = colliderEntity->FindComponent<TestColliderComponent>()->GetColliderRotation();
        EXPECT_THAT(rotation, IsClose(AZ::Quaternion::CreateIdentity()));
    }

    TEST_F(PhysXColliderComponentModeTest, ClickingOffsetButtonShouldSetOffsetMode)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        EnterComponentMode<TestColliderComponent>();

        // Given
        // Check preconditions
        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // Get the cluster and button Ids
        AzToolsFramework::ViewportUi::ClusterId modeSelectionClusterId;
        AzToolsFramework::ViewportUi::ButtonId offsetModeButtonId;

        AZ::EntityId entityId = colliderEntity->GetId();

        PhysX::ColliderComponentModeUiRequestBus::EventResult(
            modeSelectionClusterId, AZ::EntityComponentIdPair(entityId, m_colliderComponentId),
            &PhysX::ColliderComponentModeUiRequests::GetClusterId);
        PhysX::ColliderComponentModeUiRequestBus::EventResult(
            offsetModeButtonId, AZ::EntityComponentIdPair(entityId, m_colliderComponentId),
            &PhysX::ColliderComponentModeUiRequests::GetOffsetButtonId);

        // When
        // Trigger the button
        m_viewportManagerWrapper.GetViewportManager()->PressButton(modeSelectionClusterId, offsetModeButtonId);

        // Then
        // Then the component mode is set to Offset.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, ClickingRotationButtonShouldSetRotationMode)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        EnterComponentMode<TestColliderComponent>();

        // Given
        // Check preconditions
        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // Get the cluster and button Ids
        AzToolsFramework::ViewportUi::ClusterId modeSelectionClusterId;
        AzToolsFramework::ViewportUi::ButtonId offsetModeButtonId;

        AZ::EntityId entityId = colliderEntity->GetId();

        PhysX::ColliderComponentModeUiRequestBus::EventResult(
            modeSelectionClusterId, AZ::EntityComponentIdPair(entityId, m_colliderComponentId),
            &PhysX::ColliderComponentModeUiRequests::GetClusterId);
        PhysX::ColliderComponentModeUiRequestBus::EventResult(
            offsetModeButtonId, AZ::EntityComponentIdPair(entityId, m_colliderComponentId),
            &PhysX::ColliderComponentModeUiRequests::GetRotationButtonId);

        // When
        // Trigger the button
        m_viewportManagerWrapper.GetViewportManager()->PressButton(modeSelectionClusterId, offsetModeButtonId);

        // Then
        // Then the component mode is set to Offset.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Rotation, subMode);
    }

    TEST_F(PhysXColliderComponentModeTest, ClickingDimensionsButtonShouldSetDimensionsMode)
    {
        // Given there is a collider component in component mode.
        auto colliderEntity = CreateEntityWithTestColliderComponent();
        EnterComponentMode<TestColliderComponent>();

        // Given
        // Check preconditions
        PhysX::ColliderComponentModeRequests::SubMode subMode = PhysX::ColliderComponentModeRequests::SubMode::NumModes;
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Offset, subMode);

        // Get the cluster and button Ids
        AzToolsFramework::ViewportUi::ClusterId modeSelectionClusterId;
        AzToolsFramework::ViewportUi::ButtonId offsetModeButtonId;

        AZ::EntityId entityId = colliderEntity->GetId();

        PhysX::ColliderComponentModeUiRequestBus::EventResult(
            modeSelectionClusterId, AZ::EntityComponentIdPair(entityId, m_colliderComponentId),
            &PhysX::ColliderComponentModeUiRequests::GetClusterId);
        PhysX::ColliderComponentModeUiRequestBus::EventResult(
            offsetModeButtonId, AZ::EntityComponentIdPair(entityId, m_colliderComponentId),
            &PhysX::ColliderComponentModeUiRequests::GetDimensionsButtonId);

        // When
        // Trigger the button
        m_viewportManagerWrapper.GetViewportManager()->PressButton(modeSelectionClusterId, offsetModeButtonId);

        // Then
        // Then the component mode is set to Offset.
        PhysX::ColliderComponentModeRequestBus::BroadcastResult(subMode, &PhysX::ColliderComponentModeRequests::GetCurrentMode);
        EXPECT_EQ(PhysX::ColliderComponentModeRequests::SubMode::Dimensions, subMode);
    }

    class PhysXEditorColliderComponentFixture : public UnitTest::ToolsApplicationFixture<false>
    {
    public:
        void SetUpEditorFixtureImpl() override;
        void TearDownEditorFixtureImpl() override;
        void SetupTransform(const AZ::Quaternion& rotation, const AZ::Vector3& translation, float uniformScale);
        void SetupCollider(
            const Physics::ShapeConfiguration& shapeConfiguration,
            const AZ::Quaternion& colliderRotation,
            const AZ::Vector3& colliderOffset);
        void SetupNonUniformScale(const AZ::Vector3& nonUniformScale);
        void EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode subMode);

        AZ::Entity* m_entity = nullptr;
        AZ::EntityComponentIdPair m_idPair;
    };

    void PhysXEditorColliderComponentFixture::SetUpEditorFixtureImpl()
    {
        AZ::SerializeContext* serializeContext = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);

        UnitTest::CreateDefaultEditorEntity("EditorColliderComponentEntity", &m_entity);
    }

    void PhysXEditorColliderComponentFixture::TearDownEditorFixtureImpl()
    {
        AzToolsFramework::EditorEntityContextRequestBus::Broadcast(
            &AzToolsFramework::EditorEntityContextRequestBus::Events::DestroyEditorEntity, m_entity->GetId());
        m_entity = nullptr;
    }

    void PhysXEditorColliderComponentFixture::SetupTransform(
        const AZ::Quaternion& rotation, const AZ::Vector3& translation, float uniformScale)
    {
        const AZ::Transform transform = AZ::Transform::CreateFromQuaternionAndTranslation(rotation, translation);
        AZ::TransformBus::Event(m_entity->GetId(), &AZ::TransformBus::Events::SetWorldTM, transform);
        AZ::TransformBus::Event(m_entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, uniformScale);
    }

    void PhysXEditorColliderComponentFixture::SetupCollider(
        const Physics::ShapeConfiguration& shapeConfiguration, const AZ::Quaternion& colliderRotation, const AZ::Vector3& colliderOffset)
    {
        m_entity->Deactivate();
        auto* colliderComponent =
            m_entity->CreateComponent<PhysX::EditorColliderComponent>(Physics::ColliderConfiguration(), shapeConfiguration);
        m_entity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        m_entity->Activate();
        m_idPair = AZ::EntityComponentIdPair(m_entity->GetId(), colliderComponent->GetId());
        PhysX::EditorColliderComponentRequestBus::Event(
            m_idPair, &PhysX::EditorColliderComponentRequests::SetColliderOffset, colliderOffset);
        PhysX::EditorColliderComponentRequestBus::Event(
            m_idPair, &PhysX::EditorColliderComponentRequests::SetColliderRotation, colliderRotation);
    }

    void PhysXEditorColliderComponentFixture::SetupNonUniformScale(const AZ::Vector3& nonUniformScale)
    {
        m_entity->Deactivate();
        m_entity->CreateComponent(AzToolsFramework::Components::EditorNonUniformScaleComponent::RTTI_Type());
        m_entity->Activate();
        AZ::NonUniformScaleRequestBus::Event(m_entity->GetId(), &AZ::NonUniformScaleRequests::SetScale, nonUniformScale);
    }

    void PhysXEditorColliderComponentFixture::EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode subMode)
    {
        AzToolsFramework::SelectEntity(m_entity->GetId());
        EnterComponentMode<PhysX::EditorColliderComponent>();
        PhysX::ColliderComponentModeRequestBus::Broadcast(&PhysX::ColliderComponentModeRequests::SetCurrentMode, subMode);
    }

    using PhysXEditorColliderComponentManipulatorFixture =
        UnitTest::IndirectCallManipulatorViewportInteractionFixtureMixin<PhysXEditorColliderComponentFixture>;

    // use a reasonably large tolerance because manipulator precision is limited by viewport resolution
    static const float ManipulatorTolerance = 0.01f;

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, OffsetManipulatorsCorrectlyLocatedRelativeToCollider)
    {
        const AZ::Vector3 boxDimensions(2.0f, 3.0f, 1.5f);
        const AZ::Quaternion boxRotation(0.1f, 0.1f, 0.7f, 0.7f);
        const AZ::Vector3 boxOffset(3.0f, 1.0f, 2.0f);
        SetupCollider(Physics::BoxShapeConfiguration(boxDimensions), boxRotation, boxOffset);
        const AZ::Quaternion entityRotation(0.8f, 0.2f, 0.4f, 0.4f);
        const AZ::Vector3 entityTranslation(2.0f, -3.0f, 0.5f);
        const float uniformScale = 2.0f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Offset);

        // the expected position of the central point of the collider based on the combination of entity transform and collider offset
        const AZ::Vector3 expectedColliderPosition(8.8f, -2.28f, 3.54f);

        // the expected world space direction of the collider offset x-axis based on the entity transform
        const AZ::Vector3 expectedXAxis(0.6f, 0.64f, 0.48f);

        // position the camera to look down at the collider from above
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(-AZ::Constants::HalfPi), expectedColliderPosition + AZ::Vector3::CreateAxisZ(10.0f)));

        // position in world space, slightly moved along the x-axis in order to grab the x translation manipulator
        const AZ::Vector3 worldStart = expectedColliderPosition + 0.5f * expectedXAxis;

        // position in world space to move to
        const AZ::Vector3 worldEnd = worldStart + 2.0f * expectedXAxis;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        AZ::Vector3 newColliderOffset = AZ::Vector3::CreateZero();
        PhysX::EditorColliderComponentRequestBus::EventResult(
            newColliderOffset, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderOffset);

        EXPECT_THAT(newColliderOffset, IsCloseTolerance(AZ::Vector3(4.0f, 1.0f, 2.0f), ManipulatorTolerance));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, OffsetManipulatorsCorrectlyLocatedRelativeToColliderWithNonUniformScale)
    {
        const float capsuleRadius = 0.5f;
        const float capsuleHeight = 2.0f;
        const AZ::Quaternion capsuleRotation(0.2f, -0.4f, 0.8f, 0.4f);
        const AZ::Vector3 capsuleOffset(-2.0f, 3.0f, -1.0f);
        SetupCollider(Physics::CapsuleShapeConfiguration(capsuleHeight, capsuleRadius), capsuleRotation, capsuleOffset);
        const AZ::Quaternion entityRotation(-0.1f, 0.7f, -0.7f, 0.1f);
        const AZ::Vector3 entityTranslation(-1.0f, 1.0f, -2.5f);
        const float uniformScale = 1.5f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(2.0f, 0.5f, 1.5f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Offset);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(4.13f, 4.84f, -4.75f);

        // the expected world space direction of the collider offset z-axis based on the entity transform
        const AZ::Vector3 expectedZAxis(0.28f, -0.96f, 0.0f);

        // position the camera to look at the collider from underneath
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(AZ::Constants::HalfPi), expectedColliderPosition - AZ::Vector3::CreateAxisZ(10.0f)));

        // position in world space, slightly moved along the z-axis in order to grab the z translation manipulator
        // need to go in the negative z direction because the camera angle causes the manipulator to flip
        const AZ::Vector3 worldStart = expectedColliderPosition - 0.5f * expectedZAxis;

        // position in world space to move to
        const AZ::Vector3 worldEnd = worldStart - 2.25f * expectedZAxis;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        AZ::Vector3 newColliderOffset = AZ::Vector3::CreateZero();
        PhysX::EditorColliderComponentRequestBus::EventResult(
            newColliderOffset, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderOffset);

        EXPECT_THAT(newColliderOffset, IsCloseTolerance(AZ::Vector3(-2.0f, 3.0f, -2.0f), ManipulatorTolerance));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, BoxColliderScaleManipulatorsSymmetricalEditingWithNonUniformScale)
    {
        const AZ::Vector3 boxDimensions(2.0f, 2.0f, 3.0f);
        const AZ::Quaternion boxRotation(0.7f, 0.7f, -0.1f, 0.1f);
        const AZ::Vector3 boxOffset(0.5f, 1.5f, 2.0f);
        SetupCollider(Physics::BoxShapeConfiguration(boxDimensions), boxRotation, boxOffset);
        const AZ::Quaternion entityRotation(0.2f, 0.4f, -0.4f, 0.8f);
        const AZ::Vector3 entityTranslation(2.0f, -3.0f, -2.0f);
        const float uniformScale = 0.5f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(3.0f, 1.5f, 2.5f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(4.37f, -4.285f, -1.1f);

        // the expected position of the y scale manipulator relative to the central point of the collider, based on collider
        // rotation, entity rotation and scale, and non-uniform scale
        const AZ::Vector3 scaleManipulatorYDelta(0.54f, -0.72f, -1.2f);

        // position the camera to look at the collider along the x-y diagonal
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationZ(-AZ::Constants::QuarterPi), expectedColliderPosition - AZ::Vector3(2.0f, 2.0f, 0.0f)));

        const AZ::Vector3 worldStart = expectedColliderPosition + scaleManipulatorYDelta;
        const AZ::Vector3 worldEnd = worldStart + 0.1f * scaleManipulatorYDelta;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd, AzToolsFramework::DefaultSymmetricalEditingModifier);

        AZ::Vector3 newBoxDimensions = AZ::Vector3::CreateZero();
        AzToolsFramework::BoxManipulatorRequestBus::EventResult(
            newBoxDimensions, m_idPair, &AzToolsFramework::BoxManipulatorRequests::GetDimensions);

        EXPECT_THAT(newBoxDimensions, IsCloseTolerance(AZ::Vector3(2.0f, 2.2f, 3.0f), ManipulatorTolerance));

        // the offset should not have changed, because the editing was symmetrical
        AZ::Vector3 newBoxOffset = AZ::Vector3::CreateZero();
        AzToolsFramework::ShapeManipulatorRequestBus::EventResult(
            newBoxOffset, m_idPair, &AzToolsFramework::ShapeManipulatorRequests::GetTranslationOffset);

        EXPECT_THAT(newBoxOffset, IsCloseTolerance(boxOffset, ManipulatorTolerance));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, BoxColliderScaleManipulatorsAsymmetricalEditingWithNonUniformScale)
    {
        const AZ::Vector3 boxDimensions(4.0f, 5.0f, 2.0f);
        const AZ::Quaternion boxRotation(0.3f, -0.3f, -0.1f, 0.9f);
        const AZ::Vector3 boxOffset(1.0f, -4.0f, -3.0f);
        SetupCollider(Physics::BoxShapeConfiguration(boxDimensions), boxRotation, boxOffset);
        const AZ::Quaternion entityRotation(0.5f, -0.1f, 0.7f, 0.5f);
        const AZ::Vector3 entityTranslation(-2.0f, -2.0f, 5.0f);
        const float uniformScale = 3.0f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(0.5f, 1.5f, 2.5f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(-1.1f, 21.94f, -11.08f);

        // the expected position of the -z scale manipulator relative to the central point of the collider, based on collider
        // rotation, entity rotation and scale, and non-uniform scale
        const AZ::Vector3 scaleManipulatorMinusZDelta(-4.608f, 2.5752f, -0.8064f);

        // position the camera to look at the collider along the x-y diagonal
        const AZ::Vector3 worldStart = expectedColliderPosition + scaleManipulatorMinusZDelta;
        const AZ::Vector3 worldEnd = worldStart + 0.5f * scaleManipulatorMinusZDelta;

        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationZ(3.0f * AZ::Constants::QuarterPi), worldStart + AZ::Vector3(5.0f, 5.0f, 0.0f)));

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        AZ::Vector3 newBoxDimensions = AZ::Vector3::CreateZero();
        AzToolsFramework::BoxManipulatorRequestBus::EventResult(
            newBoxDimensions, m_idPair, &AzToolsFramework::BoxManipulatorRequests::GetDimensions);

        EXPECT_THAT(newBoxDimensions, IsCloseTolerance(AZ::Vector3(4.0f, 5.0f, 2.5f), ManipulatorTolerance));

        // the offset should have changed, because the editing was asymmetrical
        AZ::Vector3 newBoxOffset = AZ::Vector3::CreateZero();
        AzToolsFramework::ShapeManipulatorRequestBus::EventResult(
            newBoxOffset, m_idPair, &AzToolsFramework::ShapeManipulatorRequests::GetTranslationOffset);

        // the offset should have moved 0.25 units (half the change in the z dimension)
        // along the -z axis, tranformed by the local rotation of the box
        const AZ::Vector3 rotatedMinusZAxis(0.6f, 0.48f, -0.64f);
        EXPECT_THAT(newBoxOffset, IsCloseTolerance(boxOffset + 0.25f * rotatedMinusZAxis, ManipulatorTolerance));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, SphereColliderScaleManipulatorsCorrectlyLocatedRelativeToColliderWithNonUniformScale)
    {
        const float sphereRadius = 1.0f;
        const AZ::Quaternion sphereRotation(-0.1f, 0.7f, -0.7f, 0.1f);
        const AZ::Vector3 sphereOffset(-2.0f, 1.0f, -3.0f);
        SetupCollider(Physics::SphereShapeConfiguration(sphereRadius), sphereRotation, sphereOffset);
        const AZ::Quaternion entityRotation(-0.4f, -0.2f, 0.4f, 0.8f);
        const AZ::Vector3 entityTranslation(-1.0f, -3.0f, 3.0f);
        const float uniformScale = 1.5f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(1.5f, 0.5f, 2.0f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(1.7f, -10.65f, -3.0f);

        // position the camera to look at the collider along the y-axis
        AzFramework::SetCameraTransform(
            m_cameraState, AZ::Transform::CreateTranslation(expectedColliderPosition - AZ::Vector3(0.0f, 5.0f, 0.0f)));

        // the expected position of the scale manipulator relative to the central point of the collider, based on collider
        // rotation, entity scale, non-uniform scale and camera state
        const AZ::Vector3 scaleManipulatorDelta(2.2008f, -0.78993f, -1.75965f);

        const AZ::Vector3 worldStart = expectedColliderPosition + scaleManipulatorDelta;
        const AZ::Vector3 worldEnd = worldStart - 0.1f * scaleManipulatorDelta;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        float newSphereRadius = 0.0f;
        PhysX::EditorPrimitiveColliderComponentRequestBus::EventResult(
            newSphereRadius, m_idPair, &PhysX::EditorPrimitiveColliderComponentRequests::GetSphereRadius);

        EXPECT_NEAR(newSphereRadius, 0.9f, ManipulatorTolerance);
    }

    TEST_F(
        PhysXEditorColliderComponentManipulatorFixture,
        CapsuleColliderSymmetricalScaleManipulatorsCorrectlyLocatedRelativeToColliderWithNonUniformScale)
    {
        const float capsuleRadius = 0.2f;
        const float capsuleHeight = 1.0f;
        const AZ::Quaternion capsuleRotation(-0.2f, -0.8f, -0.4f, 0.4f);
        const AZ::Vector3 capsuleOffset(1.0f, -2.0f, 1.0f);
        SetupCollider(Physics::CapsuleShapeConfiguration(capsuleHeight, capsuleRadius), capsuleRotation, capsuleOffset);
        const AZ::Quaternion entityRotation(0.7f, -0.1f, -0.1f, 0.7f);
        const AZ::Vector3 entityTranslation(-2.0f, 1.0f, -3.0f);
        const float uniformScale = 2.0f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(1.0f, 0.5f, 1.5f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(-0.92f, -2.44f, -5.0f);

        // the expected position of the height manipulator relative to the central point of the collider, based on collider
        // rotation, entity scale and non-uniform scale
        const AZ::Vector3 heightManipulatorDelta(-0.3096f, 0.6528f, 0.4f);

        // position the camera to look at the collider along the y-z diagonal
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(-AZ::Constants::QuarterPi), expectedColliderPosition + AZ::Vector3(0.0f, -1.0f, 1.0f)));

        const AZ::Vector3 worldStart = expectedColliderPosition + heightManipulatorDelta;
        const AZ::Vector3 worldEnd = worldStart + 0.2f * heightManipulatorDelta;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd, AzToolsFramework::DefaultSymmetricalEditingModifier);

        float newCapsuleHeight = 0.0f;
        PhysX::EditorPrimitiveColliderComponentRequestBus::EventResult(
            newCapsuleHeight, m_idPair, &PhysX::EditorPrimitiveColliderComponentRequests::GetCapsuleHeight);

        EXPECT_NEAR(newCapsuleHeight, 1.2f, ManipulatorTolerance);
    }

    TEST_F(
        PhysXEditorColliderComponentManipulatorFixture,
        CapsuleColliderAsymmetricalScaleManipulatorsCorrectlyLocatedRelativeToColliderWithNonUniformScale)
    {
        const float capsuleRadius = 0.2f;
        const float capsuleHeight = 1.0f;
        const AZ::Quaternion capsuleRotation(-0.2f, -0.8f, -0.4f, 0.4f);
        const AZ::Vector3 capsuleOffset(1.0f, -2.0f, 1.0f);
        SetupCollider(Physics::CapsuleShapeConfiguration(capsuleHeight, capsuleRadius), capsuleRotation, capsuleOffset);
        const AZ::Quaternion entityRotation(0.7f, -0.1f, -0.1f, 0.7f);
        const AZ::Vector3 entityTranslation(-2.0f, 1.0f, -3.0f);
        const float uniformScale = 2.0f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(1.0f, 0.5f, 1.5f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(-0.92f, -2.44f, -5.0f);

        // the expected position of the height manipulator relative to the central point of the collider, based on collider
        // rotation, entity scale and non-uniform scale
        const AZ::Vector3 heightManipulatorDelta(-0.3096f, 0.6528f, 0.4f);

        // position the camera to look at the collider along the y-z diagonal
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(-AZ::Constants::QuarterPi), expectedColliderPosition + AZ::Vector3(0.0f, -1.0f, 1.0f)));

        const AZ::Vector3 worldStart = expectedColliderPosition + heightManipulatorDelta;
        const AZ::Vector3 worldEnd = worldStart + 0.2f * heightManipulatorDelta;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        float newCapsuleHeight = 0.0f;
        PhysX::EditorPrimitiveColliderComponentRequestBus::EventResult(
            newCapsuleHeight, m_idPair, &PhysX::EditorPrimitiveColliderComponentRequests::GetCapsuleHeight);

        EXPECT_NEAR(newCapsuleHeight, 1.1f, ManipulatorTolerance);
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, ColliderRotationManipulatorsCorrectlyLocatedRelativeToColliderWithNonUniformScale)
    {
        const float capsuleRadius = 1.2f;
        const float capsuleHeight = 4.0f;
        const AZ::Quaternion capsuleRotation(0.7f, 0.7f, -0.1f, 0.1f);
        const AZ::Vector3 capsuleOffset(-2.0f, -2.0f, 1.0f);
        SetupCollider(Physics::CapsuleShapeConfiguration(capsuleHeight, capsuleRadius), capsuleRotation, capsuleOffset);
        const AZ::Quaternion entityRotation(0.8f, -0.4f, -0.4f, 0.2f);
        const AZ::Vector3 entityTranslation(1.0f, -1.5f, 2.0f);
        const float uniformScale = 1.5f;
        SetupTransform(entityRotation, entityTranslation, uniformScale);
        const AZ::Vector3 nonUniformScale(1.5f, 1.5f, 2.0f);
        SetupNonUniformScale(nonUniformScale);
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Rotation);

        // the expected position of the central point of the collider based on the combination of entity transform, collider offset and
        // non-uniform scale
        const AZ::Vector3 expectedColliderPosition(-0.86f, 4.8f, -0.52f);

        // the y and z axes of the collider's frame in world space, used to locate points on the x rotation manipulator arc to interact with
        const AZ::Vector3 yDirection(0.36f, -0.8f, -0.48f);
        const AZ::Vector3 zDirection(0.9024f, 0.168f, 0.3968f);

        // position the camera to look at the collider along the world y axis
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateTranslation(expectedColliderPosition - AZ::Vector3(0.0f, 10.0f, 0.0f)));

        const float screenToWorldMultiplier = AzToolsFramework::CalculateScreenToWorldMultiplier(expectedColliderPosition, m_cameraState);
        const float manipulatorViewRadius = 2.0f;
        const AZ::Vector3 worldStart = expectedColliderPosition + screenToWorldMultiplier * manipulatorViewRadius * yDirection;
        const AZ::Vector3 worldEnd = expectedColliderPosition + screenToWorldMultiplier * manipulatorViewRadius * zDirection;

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        AZ::Quaternion newColliderRotation = AZ::Quaternion::CreateIdentity();
        PhysX::EditorColliderComponentRequestBus::EventResult(
            newColliderRotation, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderRotation);

        EXPECT_THAT(newColliderRotation, testing::Not(IsCloseTolerance(capsuleRotation, ManipulatorTolerance)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Flipped entity
    //
    // A flip never moves or turns the entity, and the wireframe of a flipped collider is drawn with its offset and rotation
    // mirrored. The handles of the component mode sit on the wireframe, with their axes still oriented to the entity. The editor
    // collider bus keeps the stored values, and every handle mirrors at its edge: on read when it takes its place, and on write
    // when it stores what it did.

    TEST_F(PhysXEditorColliderComponentFixture, ShapeManipulatorRequests_FlippedEntity_ReturnAndTakeMirroredValuesAndTheStoredValuesStayRaw)
    {
        const AZ::Quaternion colliderRotation = AZ::Quaternion::CreateFromEulerAnglesDegrees(AZ::Vector3(20.0f, 30.0f, 60.0f));
        const AZ::Vector3 colliderOffset(1.0f, 2.0f, 3.0f);
        SetupCollider(Physics::BoxShapeConfiguration(AZ::Vector3(2.0f, 3.0f, 1.5f)), colliderRotation, colliderOffset);

        const auto getHandleOffset = [this]()
        {
            AZ::Vector3 handleOffset = AZ::Vector3::CreateZero();
            AzToolsFramework::ShapeManipulatorRequestBus::EventResult(
                handleOffset, m_idPair, &AzToolsFramework::ShapeManipulatorRequests::GetTranslationOffset);
            return handleOffset;
        };
        const auto getHandleRotation = [this]()
        {
            AZ::Quaternion handleRotation = AZ::Quaternion::CreateIdentity();
            AzToolsFramework::ShapeManipulatorRequestBus::EventResult(
                handleRotation, m_idPair, &AzToolsFramework::ShapeManipulatorRequests::GetRotationOffset);
            return handleRotation;
        };
        const auto getStoredOffset = [this]()
        {
            AZ::Vector3 storedOffset = AZ::Vector3::CreateZero();
            PhysX::EditorColliderComponentRequestBus::EventResult(
                storedOffset, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderOffset);
            return storedOffset;
        };

        // not flipped: the handles take the stored values, as they always did
        EXPECT_THAT(getHandleOffset(), IsCloseTolerance(colliderOffset, 1e-5f));
        EXPECT_THAT(getHandleRotation(), IsCloseTolerance(colliderRotation, 1e-5f));

        // flipped in X: the offset and the rotation mirror, and a rotation keeps its x component and negates y and z
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_THAT(getHandleOffset(), IsCloseTolerance(AZ::Vector3(-1.0f, 2.0f, 3.0f), 1e-5f));
        EXPECT_THAT(
            getHandleRotation(),
            IsCloseTolerance(
                AZ::Quaternion(colliderRotation.GetX(), -colliderRotation.GetY(), -colliderRotation.GetZ(), colliderRotation.GetW()),
                1e-5f));

        // what a handle sets is mirrored back before it is stored, and the stored rotation is left alone
        AzToolsFramework::ShapeManipulatorRequestBus::Event(
            m_idPair, &AzToolsFramework::ShapeManipulatorRequests::SetTranslationOffset, AZ::Vector3(-0.5f, 2.0f, 3.0f));
        EXPECT_THAT(getStoredOffset(), IsCloseTolerance(AZ::Vector3(0.5f, 2.0f, 3.0f), 1e-5f));
        AZ::Quaternion storedRotation = AZ::Quaternion::CreateIdentity();
        PhysX::EditorColliderComponentRequestBus::EventResult(
            storedRotation, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderRotation);
        EXPECT_THAT(storedRotation, IsCloseTolerance(colliderRotation, 1e-5f));

        // flipped in Z: a rotation keeps its z component and negates x and y
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3(1.0f, 1.0f, -1.0f));
        EXPECT_THAT(getHandleOffset(), IsCloseTolerance(AZ::Vector3(0.5f, 2.0f, -3.0f), 1e-5f));
        EXPECT_THAT(
            getHandleRotation(),
            IsCloseTolerance(
                AZ::Quaternion(-colliderRotation.GetX(), -colliderRotation.GetY(), colliderRotation.GetZ(), colliderRotation.GetW()),
                1e-5f));

        // back to unflipped: the handles take the stored values again
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3::CreateOne());
        EXPECT_THAT(getHandleOffset(), IsCloseTolerance(AZ::Vector3(0.5f, 2.0f, 3.0f), 1e-5f));
        EXPECT_THAT(getHandleRotation(), IsCloseTolerance(colliderRotation, 1e-5f));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, OffsetManipulatorOnAFlippedEntitySitsOnTheMirroredColliderAndStoresTheUnmirroredOffset)
    {
        SetupCollider(
            Physics::BoxShapeConfiguration(AZ::Vector3(2.0f, 3.0f, 1.5f)), AZ::Quaternion::CreateIdentity(), AZ::Vector3(1.0f, 0.0f, 0.0f));
        const AZ::Quaternion entityRotation = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f));
        const AZ::Vector3 entityTranslation(2.0f, -3.0f, 0.5f);
        SetupTransform(entityRotation, entityTranslation, 1.0f);
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Offset);

        // the stored offset is +1 in x, and the collider is drawn at -1: world * (-1, 0, 0)
        AZ::Transform world = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(world, m_entity->GetId(), &AZ::TransformBus::Events::GetWorldTM);
        const AZ::Vector3 expectedColliderPosition = world.TransformPoint(AZ::Vector3(-1.0f, 0.0f, 0.0f));

        // the axes of the gizmo stay oriented to the entity
        const AZ::Vector3 xAxis = entityRotation.TransformVector(AZ::Vector3::CreateAxisX());

        // position the camera to look down at the collider from above
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(-AZ::Constants::HalfPi), expectedColliderPosition + AZ::Vector3::CreateAxisZ(10.0f)));

        // grab the x translation manipulator of the mirrored collider, which only exists there, and move it along the mouse
        const AZ::Vector3 worldStart = expectedColliderPosition + 0.5f * xAxis;
        const AZ::Vector3 worldEnd = worldStart + 0.5f * xAxis;
        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        // the mirrored offset went from -1 to -0.5, so the stored offset went from +1 to +0.5
        AZ::Vector3 storedOffset = AZ::Vector3::CreateZero();
        PhysX::EditorColliderComponentRequestBus::EventResult(
            storedOffset, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderOffset);
        EXPECT_THAT(storedOffset, IsCloseTolerance(AZ::Vector3(0.5f, 0.0f, 0.0f), 2.0f * ManipulatorTolerance));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, OffsetManipulatorFollowsAFlipMadeWhileTheModeIsOpen)
    {
        SetupCollider(
            Physics::BoxShapeConfiguration(AZ::Vector3(2.0f, 3.0f, 1.5f)), AZ::Quaternion::CreateIdentity(), AZ::Vector3(1.0f, 0.0f, 0.0f));
        const AZ::Quaternion entityRotation = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f));
        const AZ::Vector3 entityTranslation(2.0f, -3.0f, 0.5f);
        SetupTransform(entityRotation, entityTranslation, 1.0f);

        // the mode opens on an entity that is not flipped, then the entity is flipped. A pure flip fires no transform event, so
        // the handles only move because the mode listens for the flip.
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Offset);
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        // the collider is drawn at -1, and the gizmo is there
        AZ::Transform world = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(world, m_entity->GetId(), &AZ::TransformBus::Events::GetWorldTM);
        const AZ::Vector3 expectedColliderPosition = world.TransformPoint(AZ::Vector3(-1.0f, 0.0f, 0.0f));
        const AZ::Vector3 xAxis = entityRotation.TransformVector(AZ::Vector3::CreateAxisX());

        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(-AZ::Constants::HalfPi), expectedColliderPosition + AZ::Vector3::CreateAxisZ(10.0f)));

        const AZ::Vector3 worldStart = expectedColliderPosition + 0.5f * xAxis;
        const AZ::Vector3 worldEnd = worldStart + 0.5f * xAxis;
        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        // the mirrored offset went from -1 to -0.5, so the stored offset went from +1 to +0.5, and did not jump to the handle
        AZ::Vector3 storedOffset = AZ::Vector3::CreateZero();
        PhysX::EditorColliderComponentRequestBus::EventResult(
            storedOffset, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderOffset);
        EXPECT_THAT(storedOffset, IsCloseTolerance(AZ::Vector3(0.5f, 0.0f, 0.0f), 2.0f * ManipulatorTolerance));
    }

    TEST_F(PhysXEditorColliderComponentManipulatorFixture, RotationManipulatorOnAFlippedEntityMatchesTheMirroredTwin)
    {
        const AZ::Quaternion colliderRotation = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(60.0f));
        const AZ::Vector3 colliderOffset(1.0f, 0.0f, 0.0f);
        SetupCollider(Physics::BoxShapeConfiguration(AZ::Vector3(2.0f, 3.0f, 1.5f)), colliderRotation, colliderOffset);
        const AZ::Quaternion entityRotation = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f));
        const AZ::Vector3 entityTranslation(2.0f, -3.0f, 0.5f);
        SetupTransform(entityRotation, entityTranslation, 1.0f);
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EnterColliderSubMode(PhysX::ColliderComponentModeRequests::SubMode::Rotation);

        // the collider is drawn at -1 in x, and turned the other way about z
        AZ::Transform world = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(world, m_entity->GetId(), &AZ::TransformBus::Events::GetWorldTM);
        const AZ::Vector3 mirroredOffset(-1.0f, 0.0f, 0.0f);
        const AZ::Quaternion mirroredRotation = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(-60.0f));
        const AZ::Vector3 ringCentre = world.TransformPoint(mirroredOffset);
        const AZ::Quaternion ringFrame = world.GetRotation() * mirroredRotation;

        // position the camera to look down at the collider from above, so the ring about z faces it
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationX(-AZ::Constants::HalfPi), ringCentre + AZ::Vector3::CreateAxisZ(10.0f)));

        // two points on that ring, away from where the other rings cross it
        const float screenToWorldMultiplier = AzToolsFramework::CalculateScreenToWorldMultiplier(ringCentre, m_cameraState);
        const float manipulatorViewRadius = 2.0f;
        const float ringRadius = screenToWorldMultiplier * manipulatorViewRadius;
        const AZ::Vector3 worldStart = ringCentre + ringRadius * ringFrame.TransformVector(AZ::Vector3(0.7071f, 0.7071f, 0.0f));
        const AZ::Vector3 worldEnd = ringCentre + ringRadius * ringFrame.TransformVector(AZ::Vector3(-0.7071f, 0.7071f, 0.0f));

        const auto getStoredRotation = [this]()
        {
            AZ::Quaternion storedRotation = AZ::Quaternion::CreateIdentity();
            PhysX::EditorColliderComponentRequestBus::EventResult(
                storedRotation, m_idPair, &PhysX::EditorColliderComponentRequests::GetColliderRotation);
            return storedRotation;
        };

        // drag the ring of the flipped entity
        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);
        const AZ::Quaternion flippedResult = getStoredRotation();

        // the twin: the same collider on an entity that is not flipped, stored where the flipped one is drawn. The rings are
        // in the same place, so the same drag turns them the same way.
        PhysXEditorTests::SetEditorTransformScale(m_entity, AZ::Vector3::CreateOne());
        PhysX::EditorColliderComponentRequestBus::Event(
            m_idPair, &PhysX::EditorColliderComponentRequests::SetColliderOffset, mirroredOffset);
        PhysX::EditorColliderComponentRequestBus::Event(
            m_idPair, &PhysX::EditorColliderComponentRequests::SetColliderRotation, mirroredRotation);
        AzToolsFramework::ComponentModeFramework::ComponentModeSystemRequestBus::Broadcast(
            &AzToolsFramework::ComponentModeFramework::ComponentModeSystemRequests::Refresh, m_idPair);
        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);
        const AZ::Quaternion twinResult = getStoredRotation();

        // the drag took hold on the flipped entity, which fails if its rings are not where the collider is drawn
        EXPECT_THAT(flippedResult, testing::Not(IsCloseTolerance(colliderRotation, ManipulatorTolerance)));

        // what the flipped entity stored is the mirror of what its twin stored
        const AZ::Vector3 flippedX(-1.0f, 1.0f, 1.0f);
        EXPECT_THAT(flippedResult, IsCloseTolerance(PhysX::Transform2Mirror::MirrorRotation(flippedX, twinResult), ManipulatorTolerance));
    }

    class PhysXFlippedMeshColliderComponentModeTest
        : public PhysXColliderComponentModeTest
    {
    protected:
        AZ::Entity* CreateEntityWithTestMeshColliderComponent()
        {
            AZ::Entity* entity = nullptr;
            AZ::EntityId entityId = CreateDefaultEditorEntity("ComponentModeEntity", &entity);

            entity->Deactivate();
            entity->CreateComponent<TestMeshColliderComponent>();
            entity->Activate();

            AzToolsFramework::SelectEntity(entityId);

            return entity;
        }
    };

    using PhysXFlippedMeshColliderComponentModeManipulatorTest =
        UnitTest::IndirectCallManipulatorViewportInteractionFixtureMixin<PhysXFlippedMeshColliderComponentModeTest>;

    TEST_F(PhysXFlippedMeshColliderComponentModeManipulatorTest, AssetScaleManipulatorsOnAFlippedEntitySitOnTheMirroredCollider)
    {
        auto colliderEntity = CreateEntityWithTestMeshColliderComponent();
        auto* meshCollider = colliderEntity->FindComponent<TestMeshColliderComponent>();
        meshCollider->SetAssetScale(AZ::Vector3::CreateOne());
        meshCollider->SetColliderOffset(AZ::Vector3(1.0f, 0.0f, 0.0f));
        PhysXEditorTests::SetEditorTransformScale(colliderEntity, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EnterComponentMode<TestMeshColliderComponent>();
        PhysX::ColliderComponentModeRequestBus::Broadcast(
            &PhysX::ColliderComponentModeRequests::SetCurrentMode, PhysX::ColliderComponentModeRequests::SubMode::Dimensions);

        // the collider is drawn at -1 in x, so that is where the handles are. The view is the one of
        // AssetScaleManipulatorsScaleInCorrectDirection, moved with the collider.
        const AZ::Vector3 colliderPosition(-1.0f, 0.0f, 0.0f);
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationZ(-AZ::Constants::QuarterPi), colliderPosition + AZ::Vector3(-5.0f, -5.0f, 0.0f)));

        // select a point in world space slightly displaced from the mirrored collider in the negative x direction
        // in order to grab the X manipulator
        const float x = 0.1f;
        const float xDelta = 0.1f;
        const AZ::Vector3 worldStart = colliderPosition + AZ::Vector3(-x, 0.0f, 0.0f);
        const AZ::Vector3 worldEnd = colliderPosition + AZ::Vector3(-(x + xDelta), 0.0f, 0.0f);

        DragMouse(m_cameraState, m_actionDispatcher.get(), worldStart, worldEnd);

        const auto worldToScreenMultiplier = 1.0f / AzToolsFramework::CalculateScreenToWorldMultiplier(worldStart, m_cameraState);
        const AZ::Vector3 assetScale = meshCollider->GetAssetScale();
        // need quite a large tolerance because using screen co-ordinates limits precision
        const float tolerance = 0.01f;
        EXPECT_THAT(assetScale.GetX(), ::testing::FloatNear(1.0f + xDelta * worldToScreenMultiplier, tolerance));
        EXPECT_THAT(assetScale.GetY(), ::testing::FloatNear(1.0f, tolerance));
        EXPECT_THAT(assetScale.GetZ(), ::testing::FloatNear(1.0f, tolerance));
    }

    class ColliderPickingFixture : public PhysXEditorColliderComponentManipulatorFixture
    {
    public:
        void SetUpEditorFixtureImpl() override;
        //! Clicks at a given position and returns the entities that are selected.
        AzToolsFramework::EntityIdList ClickAndGetSelectedEntities(AzFramework::ScreenPoint screenPoint);

        static inline const float uniformScale = 1.0f;
        static inline const AZ::Quaternion ShapeRotation = AZ::Quaternion(0, 0, 0, 1);
        static inline const AZ::Quaternion EntityRotation = AZ::Quaternion(0, 0, 0, 1);
        static inline const AZ::Vector3 ShapeOffset = AZ::Vector3(0, 0, 0);
        static inline const AZ::Vector3 EntityTranslation = AZ::Vector3(5.0f, 15.0f, 10.0f);
    };

    void ColliderPickingFixture::SetUpEditorFixtureImpl()
    {
        PhysXEditorColliderComponentManipulatorFixture::SetUpEditorFixtureImpl();

        m_cameraState.m_viewportSize = AzFramework::ScreenSize(1920, 1080);
        AzFramework::SetCameraTransform(
            m_cameraState,
            AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateFromEulerAnglesDegrees(AZ::Vector3(0.0f, 0.0f, 90.0f)), AZ::Vector3(20.0f, 15.0f, 10.0f)));

        m_actionDispatcher->CameraState(m_cameraState);
    }

    AzToolsFramework::EntityIdList ColliderPickingFixture::ClickAndGetSelectedEntities(AzFramework::ScreenPoint screenPoint)
    {
        // click the entity in the viewport
        m_actionDispatcher->SetStickySelect(true)
            ->CameraState(m_cameraState)
            ->MousePosition(screenPoint)
            ->MouseLButtonDown()
            ->MouseLButtonUp();

        AzToolsFramework::EntityIdList selectedEntities;
        AzToolsFramework::ToolsApplicationRequestBus::BroadcastResult(
            selectedEntities, &AzToolsFramework::ToolsApplicationRequestBus::Events::GetSelectedEntities);
        return selectedEntities;
    }

    TEST_F(ColliderPickingFixture, ColliderPickingWithBoxShape)
    {
        // Given the setup conditions
        const AZ::Vector3 boxDimensions(5.0f, 5.0f, 5.0f);
        SetupCollider(Physics::BoxShapeConfiguration(boxDimensions), ShapeRotation, ShapeOffset);
        SetupTransform(EntityRotation, EntityTranslation, uniformScale);

        // When a user clicks just outside the collider it should not be selected
        auto clickPos1 = AzFramework::WorldToScreen(AZ::Vector3(7.5f, 12.4f, 10.0f), m_cameraState);
        auto selectedEntities = ClickAndGetSelectedEntities(clickPos1);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(0));

        // Then when a user clicks inside the collider it should be selected
        auto clickPos2 = AzFramework::WorldToScreen(AZ::Vector3(7.5f, 12.6f, 10.0f), m_cameraState);
        selectedEntities = ClickAndGetSelectedEntities(clickPos2);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(1));
        EXPECT_THAT(selectedEntities.front(), ::testing::Eq(m_entity->GetId()));
    }

    TEST_F(ColliderPickingFixture, ColliderPickingWithBoxShapeAndRigidBodyComponent)
    {
        // Given the setup conditions
        const AZ::Vector3 boxDimensions(5.0f, 5.0f, 5.0f);
        SetupTransform(EntityRotation, EntityTranslation, uniformScale);

        // The collider should be selectable with a collider and rigid body component
        m_entity->Deactivate();
        m_entity->CreateComponent<PhysX::EditorColliderComponent>(
            Physics::ColliderConfiguration(), Physics::BoxShapeConfiguration(boxDimensions));
        m_entity->CreateComponent<PhysX::EditorRigidBodyComponent>();
        m_entity->Activate();

        // When a user clicks just outside the collider it should not be picked
        auto clickPos1 = AzFramework::WorldToScreen(AZ::Vector3(7.5f, 12.4f, 10.0f), m_cameraState);
        auto selectedEntities = ClickAndGetSelectedEntities(clickPos1);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(0));

        // Then when a user clicks inside the collider it should be selected
        auto clickPos2 = AzFramework::WorldToScreen(AZ::Vector3(7.5f, 12.6f, 10.0f), m_cameraState);
        selectedEntities = ClickAndGetSelectedEntities(clickPos2);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(1));
        EXPECT_THAT(selectedEntities.front(), ::testing::Eq(m_entity->GetId()));
    }

    TEST_F(ColliderPickingFixture, ColliderPickingWithSphereShape)
    {
        // Given the setup conditions
        SetupCollider(Physics::SphereShapeConfiguration(2.5f), ShapeRotation, ShapeOffset);
        SetupTransform(EntityRotation, EntityTranslation, uniformScale);

        // When a user clicks just outside the collider it should not be picked
        auto clickPos1 = AzFramework::WorldToScreen(AZ::Vector3(5.0f, 12.4f, 10.0f), m_cameraState);
        auto selectedEntities = ClickAndGetSelectedEntities(clickPos1);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(0));

        // Then when a user clicks inside the collider it should be selected
        auto clickPos2 = AzFramework::WorldToScreen(AZ::Vector3(5.0f, 12.6f, 10.0f), m_cameraState);
        selectedEntities = ClickAndGetSelectedEntities(clickPos2);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(1));
        EXPECT_THAT(selectedEntities.front(), ::testing::Eq(m_entity->GetId()));
    }

    TEST_F(ColliderPickingFixture, ColliderPickingWithCapsuleShape)
    {
        // Given the setup conditions
        SetupCollider(Physics::CapsuleShapeConfiguration(5.0f, 2.5f), ShapeRotation, ShapeOffset);
        SetupTransform(EntityRotation, EntityTranslation, uniformScale);

        // When a user clicks just outside the collider it should not be picked
        auto clickPos1 = AzFramework::WorldToScreen(AZ::Vector3(5.0f, 12.4f, 10.0f), m_cameraState);
        auto selectedEntities = ClickAndGetSelectedEntities(clickPos1);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(0));

        // Then when a user clicks inside the collider it should be selected
        auto clickPos2 = AzFramework::WorldToScreen(AZ::Vector3(5.0f, 12.6f, 10.0f), m_cameraState);
        selectedEntities = ClickAndGetSelectedEntities(clickPos2);

        EXPECT_THAT(selectedEntities.size(), testing::Eq(1));
        EXPECT_THAT(selectedEntities.front(), ::testing::Eq(m_entity->GetId()));
    }
} // namespace UnitTest
