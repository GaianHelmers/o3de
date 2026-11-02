/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Math/Matrix3x3.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <AZTestShared/Math/MathTestHelpers.h>

#include <Editor/Source/ComponentModes/Joints/JointsComponentModeCommon.h>
#include <EditorColliderComponent.h>
#include <EditorHingeJointComponent.h>
#include <EditorPrismaticJointComponent.h>
#include <EditorRigidBodyComponent.h>
#include <PhysX/EditorJointBus.h>
#include <Source/Transform2ColliderSupport.h>
#include <Tests/EditorTestUtilities.h>

namespace PhysXEditorTests
{
    //////////////////////////////////////////////////////////////////////////
    // Transform 2 flip (D.42)
    //
    // The editor joint bus is what the joint component mode reads to place its handles and writes to store what they move. A flipped
    // follower hands out the frame as it is drawn, and a write is taken back to the frame that is authored. The authored frame
    // lives in the configuration, which the inspector shows, so these tests read it by turning the flip off again.

    class EditorJointFlipTest
        : public PhysXEditorFixture
    {
    protected:
        static constexpr float Tolerance = 1e-3f;

        // A rotation can be written as more than one set of Euler angles, so rotations are compared as matrices.
        static AZ::Matrix3x3 ToMatrix(const AZ::Vector3& eulerDegrees)
        {
            return AZ::Matrix3x3::CreateFromQuaternion(AZ::Quaternion::CreateFromEulerAnglesDegrees(eulerDegrees));
        }

        template<typename JointComponentType>
        EntityPtr CreateJointEntity(AZ::EntityComponentIdPair& idPair)
        {
            EntityPtr editorEntity = CreateInactiveEditorEntity("EditorJointFlipEntity");
            editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
            editorEntity->CreateComponent<PhysX::EditorRigidBodyComponent>();
            auto* joint = editorEntity->CreateComponent<JointComponentType>();
            editorEntity->Activate();
            idPair = AZ::EntityComponentIdPair(editorEntity->GetId(), joint->GetId());
            return editorEntity;
        }

        static AZ::Vector3 GetVector3(const AZ::EntityComponentIdPair& idPair, const AZStd::string_view& parameterName)
        {
            AZ::Vector3 value = AZ::Vector3::CreateZero();
            PhysX::EditorJointRequestBus::EventResult(value, idPair, &PhysX::EditorJointRequests::GetVector3Value, parameterName);
            return value;
        }

        static void SetVector3(const AZ::EntityComponentIdPair& idPair, const AZStd::string_view& parameterName, const AZ::Vector3& value)
        {
            PhysX::EditorJointRequestBus::Event(idPair, &PhysX::EditorJointRequests::SetVector3Value, parameterName, value);
        }

        static AZ::Transform GetFrame(const AZ::EntityComponentIdPair& idPair)
        {
            AZ::Transform frame = AZ::Transform::CreateIdentity();
            PhysX::EditorJointRequestBus::EventResult(
                frame, idPair, &PhysX::EditorJointRequests::GetTransformValue,
                PhysX::JointsComponentModeCommon::ParameterNames::Transform);
            return frame;
        }
    };

    TEST_F(EditorJointFlipTest, EditorHingeJointComponent_NoFlip_BusReadsAndWritesTheConfigurationAsItIs)
    {
        AZ::EntityComponentIdPair idPair;
        EntityPtr editorEntity = CreateJointEntity<PhysX::EditorHingeJointComponent>(idPair);

        const AZ::Vector3 position(0.3f, -1.0f, 0.5f);
        const AZ::Vector3 rotation(10.0f, 20.0f, 30.0f);
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position, position);
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation, rotation);

        EXPECT_EQ(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position), position);
        EXPECT_EQ(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation), rotation);
        EXPECT_THAT(GetFrame(idPair).GetTranslation(), UnitTest::IsCloseTolerance(position, Tolerance));
    }

    TEST_F(EditorJointFlipTest, EditorHingeJointComponent_FlippedFollower_BusHandsOutTheDrawnFrameAndStoresTheAuthoredOne)
    {
        AZ::EntityComponentIdPair idPair;
        EntityPtr editorEntity = CreateJointEntity<PhysX::EditorHingeJointComponent>(idPair);

        const AZ::Vector3 position(0.3f, -1.0f, 0.5f);
        const AZ::Vector3 rotation(10.0f, 20.0f, 30.0f);
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position, position);
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation, rotation);

        // Flipping Y reverses the axis of a hinge, so the drawn frame is the mirror image with the half turn.
        const AZ::Vector3 flipScale(1.0f, -1.0f, 1.0f);
        const AZ::Vector3 signs = PhysX::Transform2Mirror::GetSigns(flipScale);
        const AZ::Transform authored = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateFromEulerAnglesDegrees(rotation), position);
        const AZ::Transform drawn = PhysX::Transform2Mirror::MirrorJointFrame(signs, authored, true);
        SetEditorTransformScale(editorEntity.get(), flipScale);

        EXPECT_THAT(
            GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position),
            UnitTest::IsCloseTolerance(drawn.GetTranslation(), Tolerance));
        EXPECT_TRUE(ToMatrix(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation))
                        .IsClose(AZ::Matrix3x3::CreateFromQuaternion(drawn.GetRotation()), Tolerance));
        EXPECT_THAT(GetFrame(idPair).GetTranslation(), UnitTest::IsCloseTolerance(drawn.GetTranslation(), Tolerance));
        EXPECT_TRUE(AZ::Matrix3x3::CreateFromQuaternion(GetFrame(idPair).GetRotation())
                        .IsClose(AZ::Matrix3x3::CreateFromQuaternion(drawn.GetRotation()), Tolerance));

        // A manipulator writes back what it read, and the authored frame is not touched. The same rotation has more than one set
        // of Euler angles, so a write of what was read must leave the stored ones as they were.
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position,
            GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position));
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation,
            GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation));

        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_THAT(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position), UnitTest::IsCloseTolerance(position, Tolerance));
        EXPECT_EQ(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation), rotation);
    }

    TEST_F(EditorJointFlipTest, EditorHingeJointComponent_FlippedFollower_ManipulatorWriteStoresTheFrameThatDrawsThere)
    {
        AZ::EntityComponentIdPair idPair;
        EntityPtr editorEntity = CreateJointEntity<PhysX::EditorHingeJointComponent>(idPair);

        const AZ::Vector3 flipScale(1.0f, -1.0f, 1.0f);
        const AZ::Vector3 signs = PhysX::Transform2Mirror::GetSigns(flipScale);
        SetEditorTransformScale(editorEntity.get(), flipScale);

        // The handle is dragged to a drawn position and turned to a drawn rotation.
        const AZ::Vector3 drawnPosition(-0.5f, 0.75f, 2.0f);
        const AZ::Vector3 drawnRotation(15.0f, -40.0f, 65.0f);
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position, drawnPosition);
        SetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation, drawnRotation);

        // It draws where it was dragged.
        EXPECT_THAT(
            GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position), UnitTest::IsCloseTolerance(drawnPosition, Tolerance));
        EXPECT_TRUE(ToMatrix(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation)).IsClose(ToMatrix(drawnRotation), Tolerance));

        // What is stored is the authored frame that mirrors to the drawn one.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(1.0f, 1.0f, 1.0f));
        const AZ::Transform storedFrame = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateFromEulerAnglesDegrees(GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Rotation)),
            GetVector3(idPair, PhysX::JointsComponentModeCommon::ParameterNames::Position));
        const AZ::Transform storedAsDrawn = PhysX::Transform2Mirror::MirrorJointFrame(signs, storedFrame, true);
        EXPECT_THAT(storedAsDrawn.GetTranslation(), UnitTest::IsCloseTolerance(drawnPosition, Tolerance));
        EXPECT_TRUE(AZ::Matrix3x3::CreateFromQuaternion(storedAsDrawn.GetRotation()).IsClose(ToMatrix(drawnRotation), Tolerance));
    }

    TEST_F(EditorJointFlipTest, EditorJointComponents_FlippedFollower_HalfTurnOnlyWhereTheMirrorReversesTheMotionAxis)
    {
        // With an identity frame the mirror leaves the frame alone, so the drawn X axis is the half turn made visible: it points
        // along +X, or along -X when the joint type reverses its axis under the flip.
        struct FlipCase
        {
            AZ::Vector3 m_flipScale;
            float m_hingeAxisX;
            float m_prismaticAxisX;
        };
        const FlipCase flipCases[] = {
            { AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0f, -1.0f },  // X: a slide reverses, a turn about X does not
            { AZ::Vector3(1.0f, -1.0f, 1.0f), -1.0f, 1.0f },  // Y: a turn reverses, a slide does not
            { AZ::Vector3(1.0f, 1.0f, -1.0f), -1.0f, 1.0f },  // Z: the same as Y
            { AZ::Vector3(1.0f, -1.0f, -1.0f), 1.0f, 1.0f },  // Y and Z together do not reverse either
        };

        for (const FlipCase& flipCase : flipCases)
        {
            AZ::EntityComponentIdPair hingeIdPair;
            EntityPtr hingeEntity = CreateJointEntity<PhysX::EditorHingeJointComponent>(hingeIdPair);
            SetEditorTransformScale(hingeEntity.get(), flipCase.m_flipScale);
            EXPECT_NEAR(GetFrame(hingeIdPair).GetBasisX().GetX(), flipCase.m_hingeAxisX, Tolerance);

            AZ::EntityComponentIdPair prismaticIdPair;
            EntityPtr prismaticEntity = CreateJointEntity<PhysX::EditorPrismaticJointComponent>(prismaticIdPair);
            SetEditorTransformScale(prismaticEntity.get(), flipCase.m_flipScale);
            EXPECT_NEAR(GetFrame(prismaticIdPair).GetBasisX().GetX(), flipCase.m_prismaticAxisX, Tolerance);
        }
    }
} // namespace PhysXEditorTests
