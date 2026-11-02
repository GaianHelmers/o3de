/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <MCore/Source/AttributeFloat.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphNode.h>
#include <EMotionFX/Source/AnimGraphMotionNode.h>
#include <EMotionFX/Source/AnimGraphStateMachine.h>
#include <EMotionFX/Source/AnimGraphParameterCondition.h>
#include <EMotionFX/Source/AnimGraphStateTransition.h>
#include <EMotionFX/Source/BlendTree.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Source/Importer/Importer.h>
#include <EMotionFX/Source/Motion.h>
#include <EMotionFX/Source/MotionInstance.h>
#include <EMotionFX/Source/MotionSet.h>
#include <EMotionFX/Source/Node.h>
#include <EMotionFX/Source/Skeleton.h>
#include <EMotionFX/Source/TransformData.h>
#include <EMotionFX/Source/MotionEventTable.h>
#include <EMotionFX/Source/MotionEventTrack.h>
#include <EMotionFX/Source/TwoStringEventData.h>
#include <EMotionFX/Source/Parameter/BoolParameter.h>
#include <EMotionFX/Source/Parameter/ParameterFactory.h>
#include <EMotionFX/Source/MotionData/NonUniformMotionData.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Math/Matrix3x3.h>
#include <AzCore/std/containers/array.h>
#include <Integration/System/Transform2ActorSupport.h>
#include <Tests/JackGraphFixture.h>
#include <Tests/Matchers.h>
#include <Tests/TestAssetCode/TestMotionAssets.h>


namespace EMotionFX
{
    struct MotionExtractionTestsData
    {
        std::vector<float> m_durationMultipliers;
        std::vector<AZ::u32> m_numOfLoops;
    };

    std::vector<MotionExtractionTestsData> motionExtractionTestData
    {
        {
            {0.001f, 0.01f, 1.0f},
            {1000, 100, 1}
        }
    };

    class MotionExtractionFixtureBase
        : public JackGraphFixture
    {
    public:
        virtual void ConstructGraph() override
        {
            JackGraphFixture::ConstructGraph();
            m_jackSkeleton = m_actor->GetSkeleton();
            m_actorInstance->SetMotionExtractionEnabled(true);
            m_actor->AutoSetMotionExtractionNode();

            m_rootNode = m_jackSkeleton->FindNodeAndIndexByName("jack_root", m_jackRootIndex);
            m_hipNode = m_jackSkeleton->FindNodeAndIndexByName("Bip01__pelvis", m_jackHipIndex);
            m_jackPose = m_actorInstance->GetTransformData()->GetCurrentPose();
            AddMotionEntry(TestMotionAssets::GetJackWalkForward(), "jack_walk_forward_aim_zup");

            /*
            +------------+       +---------+
            |m_motionNode+------>+finalNode|
            +------------+       +---------+
            */
            m_motionNode = aznew AnimGraphMotionNode();
            BlendTreeFinalNode* finalNode = aznew BlendTreeFinalNode();
            m_motionNode->AddMotionId("jack_walk_forward_aim_zup");
            m_motionNode->SetLoop(true);
            m_motionNode->SetMotionExtraction(true);
            
            m_blendTree = aznew BlendTree();
            m_blendTree->AddChildNode(m_motionNode);
            m_blendTree->AddChildNode(finalNode);
            m_animGraph->GetRootStateMachine()->AddChildNode(m_blendTree);
            m_animGraph->GetRootStateMachine()->SetEntryState(m_blendTree);
            finalNode->AddConnection(m_motionNode, AnimGraphMotionNode::OUTPUTPORT_POSE, BlendTreeFinalNode::INPUTPORT_POSE);
        }

        void AddMotionEntry(Motion* motion, const AZStd::string& motionId)
        {
            m_motion = motion;
            EMotionFX::MotionSet::MotionEntry* newMotionEntry = aznew EMotionFX::MotionSet::MotionEntry();
            newMotionEntry->SetMotion(m_motion);
            m_motionSet->AddMotionEntry(newMotionEntry);
            m_motionSet->SetMotionEntryId(newMotionEntry, motionId);
        }

        AZ::Vector3 ExtractLastFramePos()
        {
            Node* motionExtractionNode = m_actor->GetMotionExtractionNode();
            if (!motionExtractionNode)
            {
                return AZ::Vector3::CreateZero();
            }

            auto motionData = azdynamic_cast<NonUniformMotionData*>(m_motion->GetMotionData());
            auto result = motionData->FindJointIndexByNameId(motionExtractionNode->GetID());
            if (!result.IsSuccess())
            {
                return AZ::Vector3::CreateZero();
            }

            const size_t motionJointIndex = result.GetValue();
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            if (motionData->IsJointPositionAnimated(motionJointIndex))
            {
                const size_t sampleIndex = motionData->GetNumJointPositionSamples(motionJointIndex) - 1;
                position = motionData->GetJointPositionSample(motionJointIndex, sampleIndex).m_value;
            }

            return position;
        }

    protected:
        size_t m_jackRootIndex = InvalidIndex;
        size_t m_jackHipIndex = InvalidIndex;
        AnimGraphMotionNode* m_motionNode = nullptr;
        BlendTree* m_blendTree = nullptr;
        Motion* m_motion = nullptr;
        Node* m_rootNode = nullptr;
        Node* m_hipNode = nullptr;
        Pose* m_jackPose = nullptr;
        Skeleton* m_jackSkeleton = nullptr;
    };
    

    class MotionExtractionFixture
        : public MotionExtractionFixtureBase
        , public ::testing::WithParamInterface<testing::tuple<bool, MotionExtractionTestsData>>
    {
    public:
        void ConstructGraph() override
        {
            MotionExtractionFixtureBase::ConstructGraph();
            m_reverse = testing::get<0>(GetParam());
            m_param = testing::get<1>(GetParam());
        }

    protected:
        MotionExtractionTestsData m_param;
        bool m_reverse = false;
    };

    class SyncMotionExtractionFixture
        : public MotionExtractionFixture
    {
    public:
        void ConstructGraph() override
        {
            JackGraphFixture::ConstructGraph();
            m_jackSkeleton = m_actor->GetSkeleton();
            m_actorInstance->SetMotionExtractionEnabled(true);
            m_actor->AutoSetMotionExtractionNode();

            m_jackPose = m_actorInstance->GetTransformData()->GetCurrentPose();

            Motion* motion = TestMotionAssets::GetJackWalkForward();
            AddMotionEntry(motion, "jack_walk_forward_aim_zup");

            /*
                +-------------+        +-------------+
                |m_motionNode1|---o--->+m_motionNode2|
                +-------------+        +-------------+

                Where o = parameter condition, checking if the parameter "Trigger" is set to a value of 1.
            */
            m_motionNode1 = aznew AnimGraphMotionNode();
            m_motionNode1->AddMotionId("jack_walk_forward_aim_zup");

            m_motionNode2 = aznew AnimGraphMotionNode();
            m_motionNode2->AddMotionId("jack_walk_forward_aim_zup");

            m_triggerParameter = static_cast<BoolParameter*>(ParameterFactory::Create(azrtti_typeid<BoolParameter>()));
            m_triggerParameter->SetName("Trigger");
            m_triggerParameter->SetDefaultValue(false);
            m_animGraph->AddParameter(m_triggerParameter);

            m_motion->GetEventTable()->GetSyncTrack()->AddEvent(0.3f, AZStd::make_shared<TwoStringEventData>("SyncA"));
            m_motion->GetEventTable()->GetSyncTrack()->AddEvent(0.6f, AZStd::make_shared<TwoStringEventData>("SyncB"));

            AnimGraphParameterCondition* paramCondition = aznew AnimGraphParameterCondition("Trigger", 1.0f, AnimGraphParameterCondition::EFunction::FUNCTION_EQUAL);
            AnimGraphStateTransition* transition = aznew AnimGraphStateTransition(m_motionNode1, m_motionNode2, {paramCondition}, 0.1f);
            transition->SetSyncMode(AnimGraphObject::ESyncMode::SYNCMODE_CLIPBASED);
            transition->SetExtractionMode(AnimGraphObject::EExtractionMode::EXTRACTIONMODE_TARGETONLY);
            transition->SetEventFilterMode(AnimGraphObject::EEventMode::EVENTMODE_FOLLOWERONLY);
            m_animGraph->GetRootStateMachine()->AddTransition(transition);
            
            m_animGraph->GetRootStateMachine()->AddChildNode(m_motionNode1);
            m_animGraph->GetRootStateMachine()->AddChildNode(m_motionNode2);
            m_animGraph->GetRootStateMachine()->SetEntryState(m_motionNode1);
        }

    protected:
        AnimGraphMotionNode* m_motionNode1 = nullptr;
        AnimGraphMotionNode* m_motionNode2 = nullptr;
        BoolParameter* m_triggerParameter = nullptr;
    };


#ifndef EMFX_SCALE_DISABLED
    TEST_F(MotionExtractionFixtureBase, ScaleTest)
    {
        const float scale = 2.0f;
        m_actorInstance->SetLocalSpaceScale(AZ::Vector3(scale, scale, scale));

        ASSERT_TRUE(m_motionNode->GetIsMotionExtraction()) << "Motion node should use motion extraction effect.";
        ASSERT_NE(m_actor->GetMotionExtractionNode(), nullptr) << "Actor's motion extraction node should not be nullptr.";

        // Move the character forward in 30 steps.
        // Make it so it exactly ends at the end of the motion.
        // The amount we move should be scaled up with the actor instance scale.
        const float expectedY = ExtractLastFramePos().GetY() * scale;
        const float duration = m_motion->GetDuration();
        const AZ::u32 numSteps = 30;
        const float stepSize = duration / static_cast<float>(numSteps);
        for (AZ::u32 i = 0; i < numSteps; ++i)
        {
            GetEMotionFX().Update(stepSize);
        }

        // Make sure we also really end where we expect.
        // Motion extraction will introduce some small inaccuracies, so we can't use AZ::g_fltEps here, but need a slightly larger value in our AZ::IsClose().
        const float yPos = m_actorInstance->GetWorldSpaceTransform().m_position.GetY();
        EXPECT_TRUE(AZ::IsClose(yPos, expectedY, 0.01f));
    }
#endif

    TEST_P(MotionExtractionFixture, ReverseRotationMotionExtractionOutputsCorrectDelta)
    {
        // Test motion extraction with reverse effect on and off, rotation to 90 degrees left and right
        m_motionNode->FindMotionInstance(m_animGraphInstance)->SetMotionExtractionEnabled(true);
        m_motionNode->SetReverse(m_reverse);
        GetEMotionFX().Update(0.0f);
        EXPECT_TRUE(m_motionNode->GetIsMotionExtraction()) << "Motion node should use motion extraction effect.";
        EXPECT_NE(m_actor->GetMotionExtractionNode(), nullptr) << "Actor's motion extraction node should not be nullptr.";

        // The expected delta used is the distance of the jack walk forward motion will move in 1 complete duration
        const float expectedDelta = ExtractLastFramePos().GetY();
        for (size_t paramIndex = 0; paramIndex < m_param.m_durationMultipliers.size(); paramIndex++)
        {
            // Test motion extraction under different durations/time deltas
            const float motionDuration = 1.066f * m_param.m_durationMultipliers[paramIndex];
            const float originalPositionY = m_actorInstance->GetWorldSpaceTransform().m_position.GetY();
            for (AZ::u32 i = 0; i < m_param.m_numOfLoops[paramIndex]; i++)
            {
                GetEMotionFX().Update(motionDuration);
            }
            const float updatedPositionY = m_actorInstance->GetWorldSpaceTransform().m_position.GetY();
            const float actualDeltaY = AZ::GetAbs(updatedPositionY - originalPositionY);
            EXPECT_TRUE(AZ::GetAbs(actualDeltaY - expectedDelta) < 0.002f)
                << "The absolute difference between actual delta and expected delta of Y-axis should be less than 0.002f.";
        }

        // Test motion extraction with rotation
        const AZ::Quaternion actorRotation(0.0f, 0.0f, -1.0f, 1.0f);
        m_actorInstance->SetLocalSpaceRotation(actorRotation.GetNormalized());
        GetEMotionFX().Update(0.0f);
        for (size_t paramIndex = 0; paramIndex < m_param.m_durationMultipliers.size(); paramIndex++)
        {
            const float motionDuration = 1.066f * m_param.m_durationMultipliers[paramIndex];
            const float originalPositionX = m_actorInstance->GetWorldSpaceTransform().m_position.GetX();
            for (AZ::u32 i = 0; i < m_param.m_numOfLoops[paramIndex]; i++)
            {
                GetEMotionFX().Update(motionDuration);
            }
            const float updatedPositionX = m_actorInstance->GetWorldSpaceTransform().m_position.GetX();
            const float actualDeltaX = AZ::GetAbs(updatedPositionX - originalPositionX);
            EXPECT_TRUE(AZ::GetAbs(actualDeltaX - expectedDelta) < 0.002f)
                << "The absolute difference between actual delta and expected delta of X-axis should be less than 0.002f.";
        }
    }

    TEST_P(MotionExtractionFixture, DiagonalRotationMotionExtractionOutputsCorrectDelta)
    {
        // Test motion extraction with diagonal rotation
        m_motionNode->FindMotionInstance(m_animGraphInstance)->SetMotionExtractionEnabled(true);
        GetEMotionFX().Update(0.0f);

        const float expectedDeltaX = 1.30162f;
        const float expectedDeltaY = 0.97622f;

        // Use m_reverse to decide rotating diagonally to the left(0.5) or right(-0.5)
        const AZ::Quaternion diagonalRotation = m_reverse ? AZ::Quaternion(0.0f, 0.0f, 0.5f, 1.0f) : AZ::Quaternion(0.0f, 0.0f, -0.5f, 1.0f);
        m_actorInstance->SetLocalSpaceRotation(diagonalRotation.GetNormalized());
        GetEMotionFX().Update(0.0f);
        for (size_t paramIndex = 0; paramIndex < m_param.m_durationMultipliers.size(); paramIndex++)
        {
            const float originalPositionX = m_actorInstance->GetWorldSpaceTransform().m_position.GetX();
            const float originalPositionY = m_actorInstance->GetWorldSpaceTransform().m_position.GetY();
            const float motionDuration = 1.066f * m_param.m_durationMultipliers[paramIndex];
            for (AZ::u32 i = 0; i < m_param.m_numOfLoops[paramIndex]; i++)
            {
                GetEMotionFX().Update(motionDuration);
            }
            const float updatedPositionX = m_actorInstance->GetWorldSpaceTransform().m_position.GetX();
            const float updatedPositionY = m_actorInstance->GetWorldSpaceTransform().m_position.GetY();
            const float actualDeltaX = AZ::GetAbs(updatedPositionX - originalPositionX);
            const float actualDeltaY = AZ::GetAbs(updatedPositionY - originalPositionY);
            EXPECT_NEAR(actualDeltaX, expectedDeltaX, 0.001f)
                << "Diagonal Rotation: The absolute difference between actual delta and expected delta of X-axis should be less than 0.001f.";
            EXPECT_NEAR(actualDeltaY, expectedDeltaY, 0.001f)
                << "Diagonal Rotation: The absolute difference between actual delta and expected delta of Y-axis should be less than 0.001f.";
        }
    }

    TEST_F(SyncMotionExtractionFixture, VerifyFirstFrameSync)
    {
        ASSERT_NE(m_motionNode1->FindMotionInstance(m_animGraphInstance), nullptr);
        ASSERT_NE(m_motionNode2->FindMotionInstance(m_animGraphInstance), nullptr);
        GetEMotionFX().Update(0.0f);

        // Make sure we're out of sync first.
        m_motionNode1->SetCurrentPlayTimeNormalized(m_animGraphInstance, 0.75f);
        m_motionNode2->SetCurrentPlayTimeNormalized(m_animGraphInstance, 0.2f);
        auto* param = m_animGraphInstance->GetParameterValueChecked<MCore::AttributeBool>(0); 
        param->SetValue(true); // Trigger the transition into motion 2.

        // Update one frame, which is the first frame during the synced transition.
        // We currently expect the motion extraction delta to be zero here. This is in order to prevent possible teleports which can happen.
        // This is because the presync time value of the second motion node is from the unsynced playback.
        // When we improve our syncing system we can handle this differently and we won't expect a zero trajectory delta anymore.
        GetEMotionFX().Update(0.15f);
        EXPECT_FLOAT_EQ(m_actorInstance->GetTrajectoryDeltaTransform().m_position.GetLength(), 0.0f);
        EXPECT_FLOAT_EQ(m_motionNode1->GetCurrentPlayTime(m_animGraphInstance), m_motionNode2->GetCurrentPlayTime(m_animGraphInstance));
        EXPECT_EQ(m_animGraphInstance->GetEventBuffer().GetNumEvents(), 0);

        // The second frame should be as normal.
        GetEMotionFX().Update(0.15f);
        EXPECT_GT(m_actorInstance->GetTrajectoryDeltaTransform().m_position.GetLength(), 0.0f);
        EXPECT_LE(m_actorInstance->GetTrajectoryDeltaTransform().m_position.GetLength(), 0.3f);
        EXPECT_FLOAT_EQ(m_motionNode1->GetCurrentPlayTime(m_animGraphInstance), m_motionNode2->GetCurrentPlayTime(m_animGraphInstance));
        EXPECT_EQ(m_animGraphInstance->GetEventBuffer().GetNumEvents(), 0);
    }
    
    INSTANTIATE_TEST_SUITE_P(MotionExtraction_OutputTests,
        MotionExtractionFixture,
        ::testing::Combine(
            ::testing::Bool(),
            ::testing::ValuesIn(motionExtractionTestData)
        )
    );

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 root motion correction
    //
    // EMotionFX moves the character as if it was not flipped. The correction has to leave it where the flipped model
    // would have walked: the delta scaled per axis in the actor's own frame, then rotated into the world.

    // What the character must end up with for a delta given in the actor's own frame.
    static AZ::Vector3 Transform2ExpectedDelta(const AZ::Quaternion& actorRotation, const AZ::Vector3& modelDelta, const AZ::Vector3& renderScale)
    {
        return actorRotation.TransformVector(modelDelta * renderScale);
    }

    TEST(Transform2RootMotionTests, XFlipSendsRightToLeftOnUnrotatedActor)
    {
        // Running right and slightly forward.
        const AZ::Vector3 delta(1.0f, 2.0f, 0.0f);
        const AZ::Vector3 correction = Integration::CalcRootMotionCorrection(delta, AZ::Quaternion::CreateIdentity(), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_THAT(delta + correction, IsClose(AZ::Vector3(-1.0f, 2.0f, 0.0f)));
    }

    TEST(Transform2RootMotionTests, ZFlipMirrorsVerticalDeltaOnUnrotatedActor)
    {
        const AZ::Vector3 delta(1.0f, 2.0f, 3.0f);
        const AZ::Vector3 correction = Integration::CalcRootMotionCorrection(delta, AZ::Quaternion::CreateIdentity(), AZ::Vector3(1.0f, 1.0f, -1.0f));

        EXPECT_THAT(delta + correction, IsClose(AZ::Vector3(1.0f, 2.0f, -3.0f)));
    }

    TEST(Transform2RootMotionTests, CombinedFlipMirrorsBothAxesOnUnrotatedActor)
    {
        const AZ::Vector3 delta(1.0f, 2.0f, 3.0f);
        const AZ::Vector3 correction = Integration::CalcRootMotionCorrection(delta, AZ::Quaternion::CreateIdentity(), AZ::Vector3(-1.0f, 1.0f, -1.0f));

        EXPECT_THAT(delta + correction, IsClose(AZ::Vector3(-1.0f, 2.0f, -3.0f)));
    }

    TEST(Transform2RootMotionTests, XFlipSendsRightToLeftOnRotatedActor)
    {
        // A 90 degree yaw turns the actor's right (model X) into world Y, and its forward (model Y) into world -X.
        const AZ::Quaternion yaw = AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi);
        const AZ::Vector3 renderScale(-1.0f, 1.0f, 1.0f);

        const AZ::Vector3 runningRight = yaw.TransformVector(AZ::Vector3(1.0f, 0.0f, 0.0f));
        const AZ::Vector3 runningForward = yaw.TransformVector(AZ::Vector3(0.0f, 1.0f, 0.0f));

        // Right becomes left. A flip in world axes would have left it alone, because it runs along world Y.
        EXPECT_THAT(runningRight + Integration::CalcRootMotionCorrection(runningRight, yaw, renderScale), IsClose(-runningRight));

        // Forward is on the unflipped axis, so it must not change.
        EXPECT_THAT(runningForward + Integration::CalcRootMotionCorrection(runningForward, yaw, renderScale), IsClose(runningForward));
    }

    TEST(Transform2RootMotionTests, FlipsFollowTheActorFrameOnAnyRotation)
    {
        const AZ::Quaternion rotations[] =
        {
            AZ::Quaternion::CreateIdentity(),
            AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi),
            AZ::Quaternion::CreateRotationZ(0.5f) * AZ::Quaternion::CreateRotationX(0.3f),
            AZ::Quaternion::CreateRotationY(2.5f) * AZ::Quaternion::CreateRotationZ(-1.2f),
        };
        const AZ::Vector3 renderScales[] =
        {
            AZ::Vector3(-1.0f, 1.0f, 1.0f),
            AZ::Vector3(1.0f, 1.0f, -1.0f),
            AZ::Vector3(-1.0f, 1.0f, -1.0f),
            AZ::Vector3(1.0f, 0.5f, 1.0f),
        };
        const AZ::Vector3 modelDelta(3.0f, 4.0f, 5.0f);

        for (const AZ::Quaternion& rotation : rotations)
        {
            for (const AZ::Vector3& renderScale : renderScales)
            {
                SCOPED_TRACE(testing::Message() << "rotation (" << rotation.GetX() << ", " << rotation.GetY() << ", "
                    << rotation.GetZ() << ", " << rotation.GetW() << "), render scale (" << renderScale.GetX() << ", "
                    << renderScale.GetY() << ", " << renderScale.GetZ() << ")");

                // EMotionFX applied the unflipped delta: the model delta rotated into the world.
                const AZ::Vector3 appliedDelta = rotation.TransformVector(modelDelta);
                const AZ::Vector3 correction = Integration::CalcRootMotionCorrection(appliedDelta, rotation, renderScale);

                EXPECT_THAT(appliedDelta + correction, IsClose(Transform2ExpectedDelta(rotation, modelDelta, renderScale)));
            }
        }
    }

    TEST(Transform2RootMotionTests, NoFlipNeedsNoCorrection)
    {
        const AZ::Quaternion rotation = AZ::Quaternion::CreateRotationZ(0.7f);
        const AZ::Vector3 delta = rotation.TransformVector(AZ::Vector3(3.0f, 4.0f, 5.0f));

        const AZ::Vector3 correction = Integration::CalcRootMotionCorrection(delta, rotation, AZ::Vector3::CreateOne());

        EXPECT_THAT(correction, IsClose(AZ::Vector3::CreateZero()));
    }

    TEST(Transform2RootMotionTests, RootMotionFollowsNonUniformScaleThroughTheStandIn)
    {
        // The entity scale is (-2, 1, 2). The actor is scaled by the stand-in, 2, so EMotionFX moved it by 2 * the model
        // delta. The render scale carries what is left: (-1, 0.5, 1). The character has to end up at scale * model delta.
        const AZ::Vector3 scale(-2.0f, 1.0f, 2.0f);
        const float standIn = AZ::Transform2::UniformStandIn(scale);
        const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);

        const AZ::Quaternion rotation = AZ::Quaternion::CreateRotationZ(0.5f);
        const AZ::Vector3 modelDelta(3.0f, 4.0f, 5.0f);
        const AZ::Vector3 appliedDelta = rotation.TransformVector(modelDelta) * standIn;

        const AZ::Vector3 correction = Integration::CalcRootMotionCorrection(appliedDelta, rotation, renderScale);

        EXPECT_THAT(appliedDelta + correction, IsClose(Transform2ExpectedDelta(rotation, modelDelta, scale)));
    }

    TEST(Transform2RootMotionTests, MirroredActorTurnsTheOtherWay)
    {
        const AZ::Quaternion yaw = AZ::Quaternion::CreateRotationZ(0.5f);
        const AZ::Quaternion oppositeYaw = AZ::Quaternion::CreateRotationZ(-0.5f);
        const AZ::Quaternion pitch = AZ::Quaternion::CreateRotationX(0.5f);
        const AZ::Quaternion oppositePitch = AZ::Quaternion::CreateRotationX(-0.5f);

        // A flip of a horizontal axis mirrors in a vertical plane, which reverses a yaw.
        EXPECT_TRUE(Integration::CalcRootMotionRotation(yaw, AZ::Vector3(-1.0f, 1.0f, 1.0f)).IsClose(oppositeYaw));
        EXPECT_TRUE(Integration::CalcRootMotionRotation(yaw, AZ::Vector3(1.0f, -1.0f, 1.0f)).IsClose(oppositeYaw));

        // Upside down mirrors in the horizontal plane, which keeps a yaw and reverses a pitch.
        EXPECT_TRUE(Integration::CalcRootMotionRotation(yaw, AZ::Vector3(1.0f, 1.0f, -1.0f)).IsClose(yaw));
        EXPECT_TRUE(Integration::CalcRootMotionRotation(pitch, AZ::Vector3(1.0f, 1.0f, -1.0f)).IsClose(oppositePitch));

        // Two flips are a 180 degree turn about the third axis, not a mirror, so a turn about that axis stays as it is.
        EXPECT_TRUE(Integration::CalcRootMotionRotation(yaw, AZ::Vector3(-1.0f, -1.0f, 1.0f)).IsClose(yaw));

        // A positive scale changes nothing, whatever its size. Only the signs matter.
        EXPECT_TRUE(Integration::CalcRootMotionRotation(yaw, AZ::Vector3(0.5f, 2.0f, 1.0f)).IsClose(yaw));
    }

    // Steps a curved path the way the frames do: EMotionFX rotates the delta by the current actor rotation and multiplies the
    // turn on, and the correction follows. The flipped twin must trace the mirror image of the original, in the frame the entity
    // was authored in. This is what a character with a yaw in its motion needs in order to run left when its original runs right.
    TEST(Transform2RootMotionTests, CurvedPathIsTheMirrorImageOfTheOriginal)
    {
        const AZ::Quaternion authoredRotations[] =
        {
            AZ::Quaternion::CreateIdentity(),
            AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi),
            AZ::Quaternion::CreateRotationZ(-1.8f),
        };
        const AZ::Vector3 renderScales[] =
        {
            AZ::Vector3(-1.0f, 1.0f, 1.0f),
            AZ::Vector3(1.0f, -1.0f, 1.0f),
        };
        const AZ::Vector3 stepDelta(0.3f, 1.0f, 0.0f);
        const AZ::Quaternion stepTurn = AZ::Quaternion::CreateRotationZ(0.2f);

        for (const AZ::Quaternion& authored : authoredRotations)
        {
            for (const AZ::Vector3& renderScale : renderScales)
            {
                AZ::Vector3 original = AZ::Vector3::CreateZero();
                AZ::Vector3 twin = AZ::Vector3::CreateZero();
                AZ::Quaternion originalRotation = authored;
                AZ::Quaternion twinRotation = authored;

                for (int step = 0; step < 40; ++step)
                {
                    original += originalRotation.TransformVector(stepDelta);
                    originalRotation = (originalRotation * stepTurn).GetNormalized();

                    const AZ::Vector3 applied = twinRotation.TransformVector(stepDelta);
                    twin += applied + Integration::CalcRootMotionCorrection(applied, twinRotation, renderScale);
                    twinRotation = (twinRotation * Integration::CalcRootMotionRotation(stepTurn, renderScale)).GetNormalized();
                }

                // The mirror image, in the frame the entity was authored in.
                const AZ::Vector3 mirrored = authored.TransformVector(authored.GetConjugate().TransformVector(original) * renderScale);
                EXPECT_THAT(twin, IsClose(mirrored));
            }
        }
    }

    // Walks the Jack actor facing along the world X axis, so a flip along the model's forward axis (Y) only works when
    // the correction uses the actor rotation. The expected value is built from the rotation set by the test.
    TEST_F(MotionExtractionFixtureBase, Transform2RootMotionCorrectionUsesTheActorRotationOfTheTick)
    {
        const AZ::Quaternion actorRotation = AZ::Quaternion::CreateRotationZ(-AZ::Constants::HalfPi);
        m_actorInstance->SetLocalSpaceRotation(actorRotation);
        GetEMotionFX().Update(0.0f);

        // Skip the first step, which carries the pop from the bind pose to the first frame of the motion.
        GetEMotionFX().Update(0.1f);

        const AZ::Vector3 positionBefore = m_actorInstance->GetLocalSpaceTransform().m_position;
        GetEMotionFX().Update(0.1f);
        const AZ::Vector3 appliedDelta = m_actorInstance->GetLocalSpaceTransform().m_position - positionBefore;
        ASSERT_GT(appliedDelta.GetLength(), 0.001f) << "The walk motion should move the actor instance.";

        const AZ::Vector3 renderScale(1.0f, -1.0f, 1.0f);
        const AZ::Vector3 correction = Integration::CalcActorRootMotionCorrection(*m_actorInstance, renderScale);

        const AZ::Vector3 modelDelta = actorRotation.GetConjugate().TransformVector(appliedDelta);
        EXPECT_THAT(appliedDelta + correction, IsClose(Transform2ExpectedDelta(actorRotation, modelDelta, renderScale)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 pose frame mapping
    //
    // The actor instance is posed at the stand-in scale, and the renderer draws the pose with the render scale on top. A model
    // space vertex v sits at world * v in the pose and at world * (renderScale * v) when drawn. The Editor picks against the
    // pose, so a point on the drawn actor has to map to the pose point of the same vertex.

    // One of the eight combinations of flipped axes. The sign of each axis is what counts, not whether the scale mirrors:
    // (-1, 1, -1) is a rotation, but the geometry still sits on the other side.
    static AZ::Vector3 Transform2SignPattern(int pattern)
    {
        return AZ::Vector3((pattern & 1) ? -1.0f : 1.0f, (pattern & 2) ? -1.0f : 1.0f, (pattern & 4) ? -1.0f : 1.0f);
    }

    // The Transform 1 world of an entity: a position away from the origin, a rotation and the stand-in scale.
    static AZ::Transform Transform2PoseWorld(const AZ::Quaternion& rotation, float standIn)
    {
        AZ::Transform world = AZ::Transform::CreateFromQuaternionAndTranslation(rotation, AZ::Vector3(3.0f, -2.0f, 5.0f));
        world.SetUniformScale(standIn);
        return world;
    }

    static AZStd::array<AZ::Quaternion, 4> Transform2PoseRotations()
    {
        return {{
            AZ::Quaternion::CreateIdentity(),
            AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi),
            AZ::Quaternion::CreateRotationZ(0.5f) * AZ::Quaternion::CreateRotationX(0.3f),
            AZ::Quaternion::CreateRotationY(2.5f) * AZ::Quaternion::CreateRotationZ(-1.2f),
        }};
    }

    TEST(Transform2PoseFrameTests, MappingAgreesWithTheDrawnDefinitionOnEverySignPattern)
    {
        const AZ::Vector3 modelVertices[] =
        {
            AZ::Vector3(1.0f, 2.0f, 3.0f),
            AZ::Vector3(-4.0f, 0.5f, 2.0f),
            AZ::Vector3::CreateZero(),
        };

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                SCOPED_TRACE(testing::Message() << "sign pattern " << pattern);
                const AZ::Vector3 renderScale = Transform2SignPattern(pattern);
                const AZ::Transform world = Transform2PoseWorld(rotation, 2.0f);

                for (const AZ::Vector3& vertex : modelVertices)
                {
                    const AZ::Vector3 pose = world.TransformPoint(vertex);
                    const AZ::Vector3 drawn = world.TransformPoint(vertex * renderScale);

                    EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, drawn), IsClose(pose));
                    EXPECT_THAT(Integration::MapPointFromPoseFrame(world, renderScale, pose), IsClose(drawn));
                }
            }
        }
    }

    TEST(Transform2PoseFrameTests, AFlipNeverMovesTheEntityOrigin)
    {
        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                SCOPED_TRACE(testing::Message() << "sign pattern " << pattern);
                const AZ::Vector3 renderScale = Transform2SignPattern(pattern);
                const AZ::Transform world = Transform2PoseWorld(rotation, 2.0f);

                EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, world.GetTranslation()), IsClose(world.GetTranslation()));
                EXPECT_THAT(Integration::MapPointFromPoseFrame(world, renderScale, world.GetTranslation()), IsClose(world.GetTranslation()));
            }
        }
    }

    TEST(Transform2PoseFrameTests, UpsideDownActorIsPickedWhereItIsDrawn)
    {
        // Upside down: the head is 2 above the entity in the pose and 2 below it when drawn. The entity stays where it is.
        const AZ::Vector3 scale(1.0f, 1.0f, -1.0f);
        const AZ::Transform world = Transform2PoseWorld(AZ::Quaternion::CreateIdentity(), AZ::Transform2::UniformStandIn(scale));
        const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);

        const AZ::Vector3 poseHead(3.0f, -2.0f, 7.0f);
        const AZ::Vector3 drawnHead(3.0f, -2.0f, 3.0f);

        // A click on the drawn head reaches the head of the pose.
        EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, drawnHead), IsClose(poseHead));

        // A click where the unflipped head would be reaches the feet side of the pose, which is not the head.
        EXPECT_FALSE(Integration::MapPointIntoPoseFrame(world, renderScale, poseHead).IsClose(poseHead));
        EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, poseHead), IsClose(drawnHead));
    }

    TEST(Transform2PoseFrameTests, TwoOneOneActorIsPickedAsDrawnNotAsTwoTwoTwo)
    {
        // The stand-in is 2, so the pose is drawn at (2, 2, 2) and the render scale squeezes it to (2, 1, 1).
        const AZ::Vector3 scale(2.0f, 1.0f, 1.0f);
        const AZ::Transform world = Transform2PoseWorld(AZ::Quaternion::CreateIdentity(), AZ::Transform2::UniformStandIn(scale));
        const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);
        ASSERT_THAT(renderScale, IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f)));

        // The head is 1 above the entity in model space: 2 above it in the pose, and 1 above it when drawn.
        const AZ::Vector3 poseHead(3.0f, -2.0f, 7.0f);
        const AZ::Vector3 drawnHead(3.0f, -2.0f, 6.0f);

        EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, drawnHead), IsClose(poseHead));

        // One unit above the drawn head is where a (2, 2, 2) pick would still hit the head. It must miss now.
        const AZ::Vector3 aboveDrawnHead(3.0f, -2.0f, 7.0f);
        EXPECT_FALSE(Integration::MapPointIntoPoseFrame(world, renderScale, aboveDrawnHead).IsClose(poseHead));
    }

    TEST(Transform2PoseFrameTests, NonUniformMappingIsNotAnIsometry)
    {
        // Two points 1 apart along the entity's own Y axis when drawn are 2 apart in the pose, because Y is squeezed to a half.
        const AZ::Vector3 scale(2.0f, 1.0f, 1.0f);
        const AZ::Transform world = Transform2PoseWorld(AZ::Quaternion::CreateRotationZ(0.7f), AZ::Transform2::UniformStandIn(scale));
        const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);

        const AZ::Vector3 drawnFrom = world.TransformPoint(AZ::Vector3::CreateZero());
        const AZ::Vector3 drawnTo = world.TransformPoint(AZ::Vector3(0.0f, 1.0f, 0.0f) * renderScale);
        ASSERT_NEAR((drawnTo - drawnFrom).GetLength(), 1.0f, 0.001f);

        const AZ::Vector3 poseFrom = Integration::MapPointIntoPoseFrame(world, renderScale, drawnFrom);
        const AZ::Vector3 poseTo = Integration::MapPointIntoPoseFrame(world, renderScale, drawnTo);
        EXPECT_NEAR((poseTo - poseFrom).GetLength(), 2.0f, 0.001f);

        // Mapping a pose hit back gives the drawn distance, which is what the pick has to report.
        const AZ::Vector3 hit = Integration::MapPointFromPoseFrame(world, renderScale, poseTo);
        EXPECT_NEAR((hit - drawnFrom).GetLength(), 1.0f, 0.001f);
    }

    TEST(Transform2PoseFrameTests, MappingRoundTrips)
    {
        const AZ::Vector3 scales[] =
        {
            AZ::Vector3(-2.0f, 1.0f, 3.0f),
            AZ::Vector3(2.0f, 1.0f, 1.0f),
            AZ::Vector3(1.0f, 1.0f, -1.0f),
            AZ::Vector3(-1.0f, -1.0f, -1.0f),
            AZ::Vector3(0.25f, -4.0f, 1.0f),
        };
        const AZ::Vector3 points[] = { AZ::Vector3(1.0f, 2.0f, 3.0f), AZ::Vector3(-7.0f, 4.0f, 0.5f) };

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            for (const AZ::Vector3& scale : scales)
            {
                SCOPED_TRACE(testing::Message() << "scale (" << scale.GetX() << ", " << scale.GetY() << ", " << scale.GetZ() << ")");
                const AZ::Transform world = Transform2PoseWorld(rotation, AZ::Transform2::UniformStandIn(scale));
                const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);

                for (const AZ::Vector3& point : points)
                {
                    const AZ::Vector3 intoPose = Integration::MapPointIntoPoseFrame(world, renderScale, point);
                    EXPECT_THAT(Integration::MapPointFromPoseFrame(world, renderScale, intoPose), IsClose(point));

                    const AZ::Vector3 fromPose = Integration::MapPointFromPoseFrame(world, renderScale, point);
                    EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, fromPose), IsClose(point));
                }
            }
        }
    }

    TEST(Transform2PoseFrameTests, ScaleOfOneIsTheIdentity)
    {
        // A Transform 1 scale of any size has a render scale of exactly one, so nothing is mapped.
        const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(AZ::Vector3(3.0f));
        ASSERT_THAT(renderScale, IsClose(AZ::Vector3::CreateOne()));

        const AZ::Vector3 point(1.0f, -2.0f, 3.0f);
        const AZ::Aabb bounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-1.0f, 2.0f, -3.0f), AZ::Vector3(4.0f, 5.0f, 6.0f));

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            const AZ::Transform world = Transform2PoseWorld(rotation, 3.0f);

            EXPECT_THAT(Integration::MapPointIntoPoseFrame(world, renderScale, point), IsClose(point));
            EXPECT_THAT(Integration::MapPointFromPoseFrame(world, renderScale, point), IsClose(point));
            EXPECT_THAT(Integration::MapAabbFromPoseFrame(world, renderScale, bounds), IsClose(bounds));
        }
    }

    TEST(Transform2PoseFrameTests, FlippedBoundsAreTheBoxMirroredAboutTheEntity)
    {
        const AZ::Transform world = Transform2PoseWorld(AZ::Quaternion::CreateIdentity(), 1.0f);
        const AZ::Vector3 origin = world.GetTranslation();
        const AZ::Vector3 nearOffset(1.0f, 2.0f, 3.0f);
        const AZ::Vector3 farOffset(4.0f, 5.0f, 6.0f);
        const AZ::Aabb poseBounds = AZ::Aabb::CreateFromMinMax(origin + nearOffset, origin + farOffset);

        for (int pattern = 0; pattern < 8; ++pattern)
        {
            SCOPED_TRACE(testing::Message() << "sign pattern " << pattern);
            const AZ::Vector3 renderScale = Transform2SignPattern(pattern);

            // A flipped axis puts the far side of the box on the near side of the entity, and the near side on the far side.
            AZ::Vector3 expectedMin = origin + nearOffset;
            AZ::Vector3 expectedMax = origin + farOffset;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (renderScale.GetElement(axis) < 0.0f)
                {
                    expectedMin.SetElement(axis, origin.GetElement(axis) - farOffset.GetElement(axis));
                    expectedMax.SetElement(axis, origin.GetElement(axis) - nearOffset.GetElement(axis));
                }
            }

            EXPECT_THAT(Integration::MapAabbFromPoseFrame(world, renderScale, poseBounds), IsClose(AZ::Aabb::CreateFromMinMax(expectedMin, expectedMax)));
        }
    }

    TEST(Transform2PoseFrameTests, BoundsContainTheDrawnCornersOfARotatedEntity)
    {
        const AZ::Vector3 scales[] =
        {
            AZ::Vector3(2.0f, 1.0f, 1.0f),
            AZ::Vector3(-2.0f, 1.0f, 1.0f),
            AZ::Vector3(1.0f, 1.0f, -1.0f),
            AZ::Vector3(1.0f, -3.0f, 2.0f),
        };
        const AZ::Aabb poseBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-1.0f, -2.0f, 0.5f), AZ::Vector3(3.0f, 1.0f, 4.0f));

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            for (const AZ::Vector3& scale : scales)
            {
                SCOPED_TRACE(testing::Message() << "scale (" << scale.GetX() << ", " << scale.GetY() << ", " << scale.GetZ() << ")");
                const AZ::Transform world = Transform2PoseWorld(rotation, AZ::Transform2::UniformStandIn(scale));
                const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);

                // Draw each of the eight corners by the definition, and box them.
                AZ::Aabb expected = AZ::Aabb::CreateNull();
                for (int corner = 0; corner < 8; ++corner)
                {
                    const AZ::Vector3 poseCorner(
                        (corner & 1) ? poseBounds.GetMax().GetX() : poseBounds.GetMin().GetX(),
                        (corner & 2) ? poseBounds.GetMax().GetY() : poseBounds.GetMin().GetY(),
                        (corner & 4) ? poseBounds.GetMax().GetZ() : poseBounds.GetMin().GetZ());
                    const AZ::Vector3 modelCorner = world.GetInverse().TransformPoint(poseCorner);
                    expected.AddPoint(world.TransformPoint(modelCorner * renderScale));
                }

                EXPECT_THAT(Integration::MapAabbFromPoseFrame(world, renderScale, poseBounds), IsClose(expected));
            }
        }
    }

    TEST(Transform2PoseFrameTests, NullBoundsStayNull)
    {
        const AZ::Transform world = Transform2PoseWorld(AZ::Quaternion::CreateRotationZ(0.4f), 2.0f);

        EXPECT_FALSE(Integration::MapAabbFromPoseFrame(world, AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Aabb::CreateNull()).IsValid());
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 joints of a drawn actor
    //
    // A script, a bone attachment or an audio emitter that asks for a joint of a flipped actor wants it where it is drawn:
    // the model space joint with its position scaled per axis and its rotation conjugated by the signs, placed by the world.

    // A model space joint with a rotation about all three axes, so a rotation that is mapped wrongly cannot match by chance.
    static AZ::Transform Transform2ModelSpaceJoint(const AZ::Vector3& position)
    {
        const AZ::Quaternion rotation =
            (AZ::Quaternion::CreateRotationZ(0.3f) * AZ::Quaternion::CreateRotationX(0.8f) * AZ::Quaternion::CreateRotationY(-0.4f)).GetNormalized();
        return AZ::Transform::CreateFromQuaternionAndTranslation(rotation, position);
    }

    TEST(Transform2JointTests, XFlipReflectsAJointThroughTheFlipPlane)
    {
        // The entity is away from the origin and turned 30 degrees, so a reflection in world axes would use the wrong plane.
        const AZ::Quaternion entityRotation = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f));
        const AZ::Transform world = AZ::Transform::CreateFromQuaternionAndTranslation(entityRotation, AZ::Vector3(3.0f, 2.0f, 0.0f));
        const AZ::Vector3 renderScale(-1.0f, 1.0f, 1.0f);

        const AZ::Transform modelJoint = Transform2ModelSpaceJoint(AZ::Vector3(0.4f, 0.1f, 1.2f));
        const AZ::Transform unflippedRead = world * modelJoint;
        const AZ::Transform drawn = Integration::MapJointFromPoseFrame(world, renderScale, unflippedRead);

        // The position is the unflipped one reflected through the plane that holds the origin of the entity and has the X axis
        // of the entity for its normal.
        const AZ::Vector3 normal = entityRotation.TransformVector(AZ::Vector3::CreateAxisX());
        const AZ::Vector3 fromOrigin = unflippedRead.GetTranslation() - world.GetTranslation();
        const AZ::Vector3 reflected = unflippedRead.GetTranslation() - normal * (2.0f * fromOrigin.Dot(normal));
        EXPECT_THAT(drawn.GetTranslation(), IsClose(reflected));
        EXPECT_FALSE(drawn.GetTranslation().IsClose(unflippedRead.GetTranslation(), 0.1f)) << "The hand must move to the other side.";

        // The rotation is the entity rotation times S * q * S^-1 of the model space joint. Built from matrices, so it does not
        // lean on the quaternion trick that the function uses.
        const AZ::Matrix3x3 entity = AZ::Matrix3x3::CreateFromQuaternion(entityRotation);
        const AZ::Matrix3x3 flip = AZ::Matrix3x3::CreateDiagonal(renderScale);
        const AZ::Matrix3x3 expectedRotation = entity * flip * AZ::Matrix3x3::CreateFromQuaternion(modelJoint.GetRotation()) * flip;
        EXPECT_TRUE(AZ::Matrix3x3::CreateFromQuaternion(drawn.GetRotation()).IsClose(expectedRotation, 0.001f));
    }

    TEST(Transform2JointTests, ARenderScaleOfOneLeavesAJointAlone)
    {
        const AZ::Transform modelJoint = Transform2ModelSpaceJoint(AZ::Vector3(0.4f, 0.1f, 1.2f));

        const AZ::Transform modelSpaceDrawn = Integration::MapModelSpaceJointFromPoseFrame(modelJoint, AZ::Vector3::CreateOne());
        EXPECT_THAT(modelSpaceDrawn, IsClose(modelJoint));

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            const AZ::Transform world = Transform2PoseWorld(rotation, 2.0f);
            const AZ::Transform read = world * modelJoint;
            const AZ::Transform drawn = Integration::MapJointFromPoseFrame(world, AZ::Vector3::CreateOne(), read);

            EXPECT_THAT(drawn.GetTranslation(), IsClose(read.GetTranslation()));
            EXPECT_TRUE(AZ::Matrix3x3::CreateFromQuaternion(drawn.GetRotation()).IsClose(AZ::Matrix3x3::CreateFromQuaternion(read.GetRotation()), 0.001f));
            EXPECT_NEAR(drawn.GetUniformScale(), read.GetUniformScale(), 0.001f);
        }
    }

    TEST(Transform2JointTests, AnItemMirroredAboutTheJointLandsOnTheDrawnVertex)
    {
        // A vertex skinned rigidly to the joint is drawn at world * (renderScale * its model space position). An item that
        // is attached to the drawn joint, with its own geometry mirrored the same way, has to put that vertex in the same place.
        // This is what keeps a sword in the hand of a flipped character once the mesh of the sword is flipped as well.
        const AZ::Transform modelJoint = Transform2ModelSpaceJoint(AZ::Vector3(0.4f, 0.1f, 1.2f));
        const AZ::Vector3 offsets[] = { AZ::Vector3(0.2f, -0.3f, 0.5f), AZ::Vector3(-1.0f, 0.4f, 0.1f) };

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                SCOPED_TRACE(testing::Message() << "sign pattern " << pattern);
                const AZ::Vector3 renderScale = Transform2SignPattern(pattern);
                const AZ::Transform world = Transform2PoseWorld(rotation, 2.0f);
                const AZ::Transform drawnJoint = Integration::MapJointFromPoseFrame(world, renderScale, world * modelJoint);

                for (const AZ::Vector3& offset : offsets)
                {
                    const AZ::Vector3 drawnVertex = world.TransformPoint(modelJoint.TransformPoint(offset) * renderScale);
                    EXPECT_THAT(drawnJoint.TransformPoint(offset * renderScale), IsClose(drawnVertex));
                }
            }
        }
    }

    TEST(Transform2JointTests, ModelSpaceJointScalesItsPositionAndConjugatesItsRotationBySign)
    {
        // The position follows the whole scale, the rotation only its signs. The scale of the joint is kept.
        const AZ::Vector3 renderScale(-1.0f, 0.5f, 2.0f);
        AZ::Transform modelJoint = Transform2ModelSpaceJoint(AZ::Vector3(0.4f, 0.1f, 1.2f));
        modelJoint.SetUniformScale(1.5f);

        const AZ::Transform drawn = Integration::MapModelSpaceJointFromPoseFrame(modelJoint, renderScale);

        EXPECT_THAT(drawn.GetTranslation(), IsClose(AZ::Vector3(-0.4f, 0.05f, 2.4f)));
        EXPECT_NEAR(drawn.GetUniformScale(), 1.5f, 0.0001f);

        const AZ::Matrix3x3 flip = AZ::Matrix3x3::CreateDiagonal(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        const AZ::Matrix3x3 expectedRotation = flip * AZ::Matrix3x3::CreateFromQuaternion(modelJoint.GetRotation()) * flip;
        EXPECT_TRUE(AZ::Matrix3x3::CreateFromQuaternion(drawn.GetRotation()).IsClose(expectedRotation, 0.001f));
    }

    TEST(Transform2JointTests, SignMaskKeepsOnlyTheSignOfEachAxis)
    {
        for (int pattern = 0; pattern < 8; ++pattern)
        {
            SCOPED_TRACE(testing::Message() << "sign pattern " << pattern);
            const AZ::Vector3 signs = Transform2SignPattern(pattern);

            // Bit 0 is X, bit 1 is Y and bit 2 is Z, the same numbering as the pattern.
            const AZ::u8 mask = Integration::CalcRenderSignMask(signs);
            EXPECT_EQ(mask, static_cast<AZ::u8>(pattern));
            EXPECT_THAT(Integration::CalcRenderSignScale(mask), IsClose(signs));

            // The size of the scale does not matter, only its sign.
            EXPECT_EQ(Integration::CalcRenderSignMask(signs * AZ::Vector3(0.25f, 4.0f, 2.0f)), mask);
        }

        // A non-uniform scale with no flip leaves nothing to keep, so such an actor keeps the joints of its uniform stand-in.
        const AZ::Vector3 nonUniform = AZ::Transform2::RenderScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
        EXPECT_EQ(Integration::CalcRenderSignMask(nonUniform), static_cast<AZ::u8>(0));
        EXPECT_THAT(Integration::CalcRenderSignScale(0), IsClose(AZ::Vector3::CreateOne()));
    }

    TEST(Transform2PoseFrameTests, SignMaskBoundsAgreeWithTheFullRenderScaleOnAFlip)
    {
        // AtomActorInstance keeps the sign mask and the Editor component looks up the whole scale. The bounds bus unions the two
        // answers, so for a flip they have to be the same box or Focus would cover both sides.
        const AZ::Aabb poseBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-1.0f, -2.0f, 0.5f), AZ::Vector3(3.0f, 1.0f, 4.0f));

        for (const AZ::Quaternion& rotation : Transform2PoseRotations())
        {
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                SCOPED_TRACE(testing::Message() << "sign pattern " << pattern);
                const AZ::Vector3 renderScale = Transform2SignPattern(pattern);
                const AZ::Transform world = Transform2PoseWorld(rotation, 2.0f);
                const AZ::Vector3 fromMask = Integration::CalcRenderSignScale(Integration::CalcRenderSignMask(renderScale));

                EXPECT_THAT(Integration::MapAabbFromPoseFrame(world, fromMask, poseBounds), IsClose(Integration::MapAabbFromPoseFrame(world, renderScale, poseBounds)));
            }
        }
    }

    TEST(Transform2PoseFrameTests, LocalBoundsMirrorAboutTheEntityOrigin)
    {
        // A local box is in the frame of the entity already, so an identity world mirrors it about the origin of the entity.
        const AZ::Aabb localBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(1.0f, 2.0f, 3.0f), AZ::Vector3(4.0f, 5.0f, 6.0f));

        const AZ::Aabb mirrored = Integration::MapAabbFromPoseFrame(AZ::Transform::CreateIdentity(), AZ::Vector3(-1.0f, 1.0f, -1.0f), localBounds);

        EXPECT_THAT(mirrored, IsClose(AZ::Aabb::CreateFromMinMax(AZ::Vector3(-4.0f, 2.0f, -6.0f), AZ::Vector3(-1.0f, 5.0f, -3.0f))));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 on a child of a flipped parent
    //
    // The actor sits on a child of the flipped entity, which is the usual character rig. Its notifications and its scale read carry
    // the effective world scale sW, and the actor instance turns by the rotation of the Transform 1 view of the child.
    // The parent turns 0.4 about Z. The child is authored with a turn of 0.5 about Z. The exact world of the child is Rp * D * Rq * S,
    // with D the signs of the parent and S the scale of the child, written out below as matrices. The Transform 1 view of the child turns by
    // Rp * (D * Rq * D): the mirror turns the authored 0.5 into -0.5, so the view turns by 0.4 - 0.5 = -0.1 about Z.
    // The view rotation and sW together have to give what the exact world gives.

    static constexpr float Transform2ParentYaw = 0.4f;
    static constexpr float Transform2ChildYaw = 0.5f;
    static constexpr float Transform2ChildViewYaw = -0.1f;

    // The linear part of the exact world of the child: Rp * D * Rq * S.
    static AZ::Matrix3x3 Transform2ExactLinear(const AZ::Vector3& parentScale, const AZ::Vector3& childScale)
    {
        return AZ::Matrix3x3::CreateFromQuaternion(AZ::Quaternion::CreateRotationZ(Transform2ParentYaw))
            * AZ::Matrix3x3::CreateDiagonal(parentScale)
            * AZ::Matrix3x3::CreateFromQuaternion(AZ::Quaternion::CreateRotationZ(Transform2ChildYaw))
            * AZ::Matrix3x3::CreateDiagonal(childScale);
    }

    TEST(Transform2ChildTests, RootMotionOfAChildOfAFlippedParentFollowsTheExactWorld)
    {
        // The parent is flipped on X and the child has a scale of one, so the child is told sW = (-1, 1, 1).
        const AZ::Quaternion viewRotation = AZ::Quaternion::CreateRotationZ(Transform2ChildViewYaw);
        const AZ::Vector3 worldScale(-1.0f, 1.0f, 1.0f);
        const AZ::Matrix3x3 exact = Transform2ExactLinear(AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3::CreateOne());

        const AZ::Vector3 modelDeltas[] = { AZ::Vector3(3.0f, 4.0f, 5.0f), AZ::Vector3(1.0f, 0.0f, 0.0f), AZ::Vector3(0.0f, 1.0f, 0.0f) };
        for (const AZ::Vector3& modelDelta : modelDeltas)
        {
            // EMotionFX moved the actor along its own rotation, with no knowledge of the flip.
            const AZ::Vector3 appliedDelta = viewRotation.TransformVector(modelDelta);
            const AZ::Vector3 correction =
                Integration::CalcRootMotionCorrection(appliedDelta, viewRotation, AZ::Transform2::RenderScale(worldScale));

            EXPECT_THAT(appliedDelta + correction, IsClose(exact * modelDelta));
        }

        // Without the correction the actor runs on the authored side, which is not where the exact world puts it.
        const AZ::Vector3 uncorrected = viewRotation.TransformVector(AZ::Vector3(3.0f, 4.0f, 5.0f));
        EXPECT_FALSE(uncorrected.IsClose(exact * AZ::Vector3(3.0f, 4.0f, 5.0f), 0.1f));
    }

    TEST(Transform2ChildTests, RootMotionOfAChildOfAMagnifiedFlippedParentFollowsTheExactWorld)
    {
        // The parent is flipped on X with magnitude 2, so sW = (-2, 2, 2), the stand-in is 2 and the render scale is (-1, 1, 1).
        // EMotionFX multiplies the delta of the actor by the stand-in scale of the actor.
        const AZ::Quaternion viewRotation = AZ::Quaternion::CreateRotationZ(Transform2ChildViewYaw);
        const AZ::Vector3 worldScale(-2.0f, 2.0f, 2.0f);
        const AZ::Matrix3x3 exact = Transform2ExactLinear(worldScale, AZ::Vector3::CreateOne());

        EXPECT_NEAR(AZ::Transform2::UniformStandIn(worldScale), 2.0f, 0.0001f);

        const AZ::Vector3 modelDelta(3.0f, 4.0f, 5.0f);
        const AZ::Vector3 appliedDelta = viewRotation.TransformVector(modelDelta) * AZ::Transform2::UniformStandIn(worldScale);
        const AZ::Vector3 correction =
            Integration::CalcRootMotionCorrection(appliedDelta, viewRotation, AZ::Transform2::RenderScale(worldScale));

        EXPECT_THAT(appliedDelta + correction, IsClose(exact * modelDelta));
    }

    TEST(Transform2ChildTests, RootMotionWhereTheChildAndItsParentAreFlippedOnTheSameAxisNeedsNoCorrection)
    {
        // The signs multiply, so sW = (1, 1, 1). The child is a half turn in the exact world and not a mirror.
        const AZ::Quaternion viewRotation = AZ::Quaternion::CreateRotationZ(Transform2ChildViewYaw);
        const AZ::Vector3 worldScale = AZ::Vector3::CreateOne();
        const AZ::Matrix3x3 exact = Transform2ExactLinear(AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        const AZ::Vector3 modelDelta(3.0f, 4.0f, 5.0f);
        const AZ::Vector3 appliedDelta = viewRotation.TransformVector(modelDelta);
        const AZ::Vector3 correction =
            Integration::CalcRootMotionCorrection(appliedDelta, viewRotation, AZ::Transform2::RenderScale(worldScale));

        EXPECT_THAT(correction, IsClose(AZ::Vector3::CreateZero()));
        EXPECT_THAT(appliedDelta, IsClose(exact * modelDelta));
    }

    TEST(Transform2ChildTests, ATurnOfAChildOfAFlippedParentIsMirroredAndStaysOnTheExactWorld)
    {
        // The child turns by 0.2 about Z in its authored frame, so its exact world becomes Rp * D * Rq(0.5 + 0.2). The view
        // rotation has to turn the other way, by -0.2, to keep D in front of the same exact matrix.
        const AZ::Quaternion viewRotation = AZ::Quaternion::CreateRotationZ(Transform2ChildViewYaw);
        const AZ::Vector3 worldScale(-1.0f, 1.0f, 1.0f);
        const AZ::Quaternion turn = AZ::Quaternion::CreateRotationZ(0.2f);

        const AZ::Quaternion turnedView = viewRotation * Integration::CalcRootMotionRotation(turn, AZ::Transform2::RenderScale(worldScale));

        EXPECT_TRUE(turnedView.IsClose(AZ::Quaternion::CreateRotationZ(Transform2ChildViewYaw - 0.2f)));

        // Seen the way the renderer draws it, the view rotation followed by the signs.
        const AZ::Matrix3x3 drawn = AZ::Matrix3x3::CreateFromQuaternion(turnedView) * AZ::Matrix3x3::CreateDiagonal(worldScale);
        const AZ::Matrix3x3 exact =
            AZ::Matrix3x3::CreateFromQuaternion(AZ::Quaternion::CreateRotationZ(Transform2ParentYaw))
            * AZ::Matrix3x3::CreateDiagonal(AZ::Vector3(-1.0f, 1.0f, 1.0f))
            * AZ::Matrix3x3::CreateFromQuaternion(AZ::Quaternion::CreateRotationZ(Transform2ChildYaw + 0.2f));
        EXPECT_TRUE(drawn.IsClose(exact, 0.001f));
    }

    TEST(Transform2ChildTests, JointOfAChildOfAFlippedParentIsDrawnWhereTheExactWorldPutsIt)
    {
        // The script read of a joint maps the pose joint by the world scale. Under a flipped parent the local scale of the child is
        // one, which would leave the joint on the authored side.
        const AZ::Vector3 viewTranslation(3.0f, -2.0f, 5.0f);
        const AZ::Transform world = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(Transform2ChildViewYaw), viewTranslation);
        const AZ::Vector3 worldScale(-1.0f, 1.0f, 1.0f);
        const AZ::Matrix3x3 exact = Transform2ExactLinear(AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3::CreateOne());

        const AZ::Transform modelJoint = Transform2ModelSpaceJoint(AZ::Vector3(0.4f, 0.1f, 1.2f));
        const AZ::Transform drawn = Integration::MapJointFromPoseFrame(world, AZ::Transform2::RenderScale(worldScale), world * modelJoint);

        EXPECT_THAT(drawn.GetTranslation(), IsClose(viewTranslation + exact * modelJoint.GetTranslation()));

        // The rotation of the drawn joint is the exact world times the model space rotation times the signs.
        const AZ::Matrix3x3 expectedRotation = exact
            * AZ::Matrix3x3::CreateFromQuaternion(modelJoint.GetRotation())
            * AZ::Matrix3x3::CreateDiagonal(worldScale);
        EXPECT_TRUE(AZ::Matrix3x3::CreateFromQuaternion(drawn.GetRotation()).IsClose(expectedRotation, 0.001f));

        // Read with the local scale of the child, which is one, the joint stays where the pose has it.
        const AZ::Transform read = world * modelJoint;
        const AZ::Transform unflipped = Integration::MapJointFromPoseFrame(world, AZ::Vector3::CreateOne(), read);
        EXPECT_THAT(unflipped.GetTranslation(), IsClose(read.GetTranslation()));
        EXPECT_FALSE(unflipped.GetTranslation().IsClose(drawn.GetTranslation(), 0.1f));
    }

} // end namespace EMotionFX
