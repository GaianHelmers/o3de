/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <Source/Shape/EditorSphereShapeComponent.h>
#include <Source/Shape/EditorSplineComponent.h>
#include <Source/Shape/EditorTubeShapeComponent.h>
#include <Source/Shape/EditorTubeShapeComponentMode.h>
#include <Source/Shape/SplineComponent.h>
#include <Source/Shape/TubeShapeComponent.h>

namespace AZ
{
    void PrintTo(const AZ::SplineAddress& splineAddress, std::ostream* os)
    {
        *os << "SplineAddress { segmentIndex: " << splineAddress.m_segmentIndex << ", segmentFraction: " << splineAddress.m_segmentFraction
            << " }";
    }
} // namespace AZ

namespace UnitTest
{
    class EditorTubeShapeFixture
        : public LeakDetectionFixture
        , public ::testing::WithParamInterface<bool>
    {
    };

    // test both open and closed versions of the spline
    INSTANTIATE_TEST_SUITE_P(GenerateTubeManipulatorStates, EditorTubeShapeFixture, ::testing::Values(true, false));

    TEST_P(EditorTubeShapeFixture, GenerateTubeManipulatorStates_returns_no_TubeManipulatorStates_when_spline_is_empty)
    {
        using ::testing::Eq;

        // given (an empty spline)
        AZ::BezierSpline spline;
        spline.SetClosed(GetParam());

        // when (tube manipulator states are attempted to be created)
        const auto tubeManipulatorStates = LmbrCentral::GenerateTubeManipulatorStates(spline);

        // then (none are returned)
        EXPECT_THAT(tubeManipulatorStates.empty(), Eq(true));
    }

    TEST_P(EditorTubeShapeFixture, GenerateTubeManipulatorStates_returns_one_TubeManipulatorStates_when_spline_has_one_vertex)
    {
        using ::testing::Eq;

        // given (an empty spline)
        AZ::BezierSpline spline;
        spline.SetClosed(GetParam());
        spline.m_vertexContainer.AddVertex(AZ::Vector3::CreateZero());

        // when (tube manipulator states are attempted to be created)
        const auto tubeManipulatorStates = LmbrCentral::GenerateTubeManipulatorStates(spline);

        // then (one is returned)
        EXPECT_THAT(tubeManipulatorStates.size(), Eq(1));
        EXPECT_THAT(tubeManipulatorStates[0].m_splineAddress, Eq(AZ::SplineAddress(0, 0.0f)));
        EXPECT_THAT(tubeManipulatorStates[0].m_vertIndex, Eq(0));
    }

    TEST_P(EditorTubeShapeFixture, GenerateTubeManipulatorStates_returns_two_TubeManipulatorStates_when_spline_has_two_vertices)
    {
        using ::testing::Eq;

        // given (an empty spline)
        AZ::BezierSpline spline;
        spline.SetClosed(GetParam());

        spline.m_vertexContainer.AddVertex(AZ::Vector3::CreateZero());
        spline.m_vertexContainer.AddVertex(AZ::Vector3::CreateAxisX(1.0f));

        // when (tube manipulator states are attempted to be created)
        const auto tubeManipulatorStates = LmbrCentral::GenerateTubeManipulatorStates(spline);

        // then (two are returned)
        EXPECT_THAT(tubeManipulatorStates.size(), Eq(2));

        EXPECT_THAT(tubeManipulatorStates[0].m_splineAddress, Eq(AZ::SplineAddress(0, 0.0f)));
        EXPECT_THAT(tubeManipulatorStates[0].m_vertIndex, Eq(0));

        EXPECT_THAT(tubeManipulatorStates[1].m_splineAddress, Eq(AZ::SplineAddress(0, 1.0f)));
        EXPECT_THAT(tubeManipulatorStates[1].m_vertIndex, Eq(1));
    }

    TEST_P(EditorTubeShapeFixture, GenerateTubeManipulatorStates_returns_three_TubeManipulatorStates_when_spline_has_three_vertices)
    {
        using ::testing::Eq;

        // given (an empty spline)
        AZ::BezierSpline spline;
        spline.SetClosed(GetParam());

        spline.m_vertexContainer.AddVertex(AZ::Vector3::CreateAxisX(-1.0f));
        spline.m_vertexContainer.AddVertex(AZ::Vector3::CreateZero());
        spline.m_vertexContainer.AddVertex(AZ::Vector3::CreateAxisX(1.0f));

        // when (tube manipulator states are attempted to be created)
        const auto tubeManipulatorStates = LmbrCentral::GenerateTubeManipulatorStates(spline);

        // then (three are returned)
        EXPECT_THAT(tubeManipulatorStates.size(), Eq(3));

        EXPECT_THAT(tubeManipulatorStates[0].m_splineAddress, Eq(AZ::SplineAddress(0, 0.0f)));
        EXPECT_THAT(tubeManipulatorStates[0].m_vertIndex, Eq(0));

        EXPECT_THAT(tubeManipulatorStates[1].m_splineAddress, Eq(AZ::SplineAddress(1, 0.0f)));
        EXPECT_THAT(tubeManipulatorStates[1].m_vertIndex, Eq(1));

        EXPECT_THAT(tubeManipulatorStates[2].m_splineAddress, Eq(AZ::SplineAddress(1, 1.0f)));
        EXPECT_THAT(tubeManipulatorStates[2].m_vertIndex, Eq(2));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 flip
    //
    // Building the game entity deactivates and activates the shape of a live editor tube, and a shape drops its flip when it
    // deactivates. Nothing pushes the flip again to a shape its entity did not deactivate, so the build has to keep it.

    namespace
    {
        // The mask for an X and a Z flip, which an entity with a scale of (-1, 1, -1) pushes to its shapes.
        constexpr AZ::u8 TubeFlipMaskXZ = 5;
    } // namespace

    class EditorTubeShapeFlipFixture
        : public ToolsApplicationFixture<>
    {
    public:
        void SetUpEditorFixtureImpl() override
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);

            // The sphere reflects EditorBaseShapeComponent, the spline reflects the data of a spline, and the tube component reflects
            // the tube and its radius attribute.
            m_descriptors.emplace_back(LmbrCentral::EditorSphereShapeComponent::CreateDescriptor());
            m_descriptors.emplace_back(LmbrCentral::SplineComponent::CreateDescriptor());
            m_descriptors.emplace_back(LmbrCentral::TubeShapeComponent::CreateDescriptor());
            m_descriptors.emplace_back(LmbrCentral::EditorSplineComponent::CreateDescriptor());
            m_descriptors.emplace_back(LmbrCentral::EditorTubeShapeComponent::CreateDescriptor());

            LmbrCentral::ShapeComponentConfig::Reflect(serializeContext);
            LmbrCentral::TubeShapeMeshConfig::Reflect(serializeContext);
            for (auto& descriptor : m_descriptors)
            {
                descriptor->Reflect(serializeContext);
            }

            CreateDefaultEditorEntity("TubeShapeComponentEntity", &m_entity);
            m_entityId = m_entity->GetId();
            m_entity->Deactivate();
            m_entity->CreateComponent(azrtti_typeid<LmbrCentral::EditorSplineComponent>());
            m_entity->CreateComponent(LmbrCentral::EditorTubeShapeComponentTypeId);
            m_entity->Activate();
        }

        void TearDownEditorFixtureImpl() override
        {
            AzToolsFramework::EditorEntityContextRequestBus::Broadcast(
                &AzToolsFramework::EditorEntityContextRequestBus::Events::DestroyEditorEntity, m_entityId);
            m_entity = nullptr;
            m_entityId.SetInvalid();

            m_descriptors.clear();
        }

        AZStd::vector<AZStd::unique_ptr<AZ::ComponentDescriptor>> m_descriptors;
        AZ::Entity* m_entity = nullptr;
        AZ::EntityId m_entityId;
    };

    TEST_F(EditorTubeShapeFlipFixture, BuildGameEntityKeepsTheFlipOfTheEditorTube)
    {
        LmbrCentral::ShapeComponentRequestsBus::Event(m_entityId, &LmbrCentral::ShapeComponentRequests::SetFlipMask, TubeFlipMaskXZ);

        AZ::Vector3 flipSigns = AZ::Vector3::CreateZero();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, m_entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
        EXPECT_THAT(flipSigns, IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));

        auto* editorTube = m_entity->FindComponent<LmbrCentral::EditorTubeShapeComponent>();
        ASSERT_NE(editorTube, nullptr);

        // The game entity gets the authored tube, and the Transform 2 of the game entity flips it again.
        AZ::Entity gameEntity;
        editorTube->BuildGameEntity(&gameEntity);
        EXPECT_NE(gameEntity.FindComponent<LmbrCentral::TubeShapeComponent>(), nullptr);

        // The editor tube was deactivated and activated by the build, and it is still where it is drawn.
        flipSigns = AZ::Vector3::CreateZero();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, m_entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
        EXPECT_THAT(flipSigns, IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
    }
} // namespace UnitTest
