/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzTest/AzTest.h>

#include <Tests/PhysXGenericTestFixture.h>
#include <Tests/PhysXTestCommon.h>

#include <BoxColliderComponent.h>
#include <RigidBodyComponent.h>
#include <JointComponent.h>
#include <BallJointComponent.h>
#include <D6JointComponent.h>
#include <FixedJointComponent.h>
#include <HingeJointComponent.h>
#include <PrismaticJointComponent.h>
#include <Source/Transform2ColliderSupport.h>
#include <PhysX/Joint/Configuration/PhysXJointConfiguration.h>
#include <PhysX/Joint/PhysXJointRequestsBus.h>
#include <PhysX/MathConversion.h>
#include <PhysX/PhysXLocks.h>
#include <PxPhysicsAPI.h>

#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Matrix3x3.h>
#include <AzCore/std/math.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Physics/ShapeConfiguration.h>
#include <AzFramework/Physics/SystemBus.h>
#include <AzFramework/Physics/Configuration/RigidBodyConfiguration.h>
#include <AzFramework/Physics/PhysicsSystem.h>

namespace PhysX
{
    using PhysXJointsTest = PhysX::GenericPhysicsInterfaceTest;

    template<typename JointComponentType>
    AZStd::unique_ptr<AZ::Entity> AddBodyColliderEntity( AzPhysics::SceneHandle sceneHandle,
        const AZ::Vector3& position, 
        const AZ::Vector3& initialLinearVelocity,
        AZStd::shared_ptr<JointComponentConfiguration> jointConfig = nullptr,
        AZStd::shared_ptr<JointGenericProperties> jointGenericProperties = nullptr,
        AZStd::shared_ptr<JointLimitProperties> jointLimitProperties = nullptr)
    {
        const char* entityName = "testEntity";
        auto entity = AZStd::make_unique<AZ::Entity>(entityName);

        AZ::TransformConfig transformConfig;
        transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(position);
        entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

        auto colliderConfiguration = AZStd::make_shared<Physics::ColliderConfiguration>();
        auto boxShapeConfiguration = AZStd::make_shared<Physics::BoxShapeConfiguration>();
        auto boxColliderComponent = entity->CreateComponent<BoxColliderComponent>();
        boxColliderComponent->SetShapeConfigurationList({ AZStd::make_pair(colliderConfiguration, boxShapeConfiguration) });

        AzPhysics::RigidBodyConfiguration rigidBodyConfig;
        rigidBodyConfig.m_initialLinearVelocity = initialLinearVelocity;
        rigidBodyConfig.m_gravityEnabled = false;

        // Make lead body very heavy
        if (!jointConfig)
        {
            rigidBodyConfig.m_mass = 9999.0f;
        }
        entity->CreateComponent<PhysX::RigidBodyComponent>(rigidBodyConfig, sceneHandle);

        if (jointConfig)
        {
            jointConfig->m_followerEntity = entity->GetId();

            JointGenericProperties defaultJointGenericProperties;
            JointLimitProperties defaultJointLimitProperties;
            entity->CreateComponent<JointComponentType>(
                    *jointConfig,
                    (jointGenericProperties)? *jointGenericProperties : defaultJointGenericProperties,
                    (jointLimitProperties)? *jointLimitProperties : defaultJointLimitProperties);
        }

        entity->Init();
        entity->Activate();

        return entity;
    }

    AZ::Vector3 RunJointTest(AzPhysics::Scene* scene, AZ::EntityId followerEntityId)
    {
        AZ::Vector3 followerEndPosition(0.0f, 0.0f, 0.0f);

        //Run simulation for a while - bounces box once on force volume
        const float deltaTime = AzPhysics::SystemConfiguration::DefaultFixedTimestep;
        const AZ::u32 numSteps = 240;
        PhysX::TestUtils::UpdateScene(scene, deltaTime, numSteps);
        
        AZ::TransformBus::EventResult(followerEndPosition,
            followerEntityId, 
            &AZ::TransformBus::Events::GetWorldTranslation);

        return followerEndPosition;
    }

    TEST_F(PhysXJointsTest, Joints_FixedJoint_BodiesAreConstrainedAndMoveTogether)
    {
        // Place lead on the right of follower, tie them together with fixed joint and send the lead moving to the right.
        // The follower should be pulled along if the fixed joint works.

        const AZ::Vector3 followerPosition(-1.0f, 0.0f, 0.0f);
        const AZ::Vector3 followerInitialLinearVelocity(0.0f, 0.0f, 0.0f);

        const AZ::Vector3 leadPosition(1.0f, 0.0f, 0.0f);
        const AZ::Vector3 leadInitialLinearVelocity(10.0f, 0.0f, 0.0f);

        const AZ::Transform jointLocalTransform = AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f));          

        // Templated joint component type is irrelevant since joint component is not created for this invocation.
        auto leadEntity = AddBodyColliderEntity<JointComponent>(m_testSceneHandle,
            leadPosition,
            leadInitialLinearVelocity);

        auto jointConfig = AZStd::make_shared<JointComponentConfiguration>();
        jointConfig->m_leadEntity = leadEntity->GetId();
        jointConfig->m_localTransformFromFollower = jointLocalTransform;

        auto followerEntity = AddBodyColliderEntity<FixedJointComponent>(m_testSceneHandle,
            followerPosition,
            followerInitialLinearVelocity, 
            jointConfig);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, followerEntity->GetId());

        EXPECT_GT(followerEndPosition.GetX(), followerPosition.GetX());
    }

    TEST_F(PhysXJointsTest, Joint_HingeJoint_FollowerSwingsAroundLead)
    {
        // Place lead on the right of follower, tie them together with hinge joint and send the follower moving up.
        // The follower should swing around the lead.

        const AZ::Vector3 followerPosition(-1.0f, 0.0f, 0.0f);
        const AZ::Vector3 followerInitialLinearVelocity(0.0f, 0.0f, 10.0f);

        const AZ::Vector3 leadPosition(1.0f, 0.0f, 0.0f);
        const AZ::Vector3 leadInitialLinearVelocity(0.0f, 0.0f, 0.0f);

        const AZ::Vector3 jointLocalPosition(1.0f, 0.0f, 0.0f);
        const AZ::Quaternion jointLocalRotation = AZ::Quaternion::CreateRotationZ(90.0f);
        const AZ::Transform jointLocalTransform = AZ::Transform::CreateFromQuaternionAndTranslation(
            jointLocalRotation, 
            jointLocalPosition);

        // Templated joint component type is irrelevant since joint component is not created for this invocation.
        auto leadEntity = AddBodyColliderEntity<JointComponent>(m_testSceneHandle,
            leadPosition, 
            leadInitialLinearVelocity);

        auto jointConfig = AZStd::make_shared<JointComponentConfiguration>();
        jointConfig->m_leadEntity = leadEntity->GetId();
        jointConfig->m_localTransformFromFollower = jointLocalTransform;

        auto jointLimits = AZStd::make_shared <JointLimitProperties>();
        jointLimits->m_isLimited = false;

        auto followerEntity = AddBodyColliderEntity<HingeJointComponent>(m_testSceneHandle,
            followerPosition,
            followerInitialLinearVelocity,
            jointConfig,
            nullptr,
            jointLimits);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, followerEntity->GetId());

        EXPECT_GT(followerEndPosition.GetX(), followerPosition.GetX());
        EXPECT_GT(abs(followerEndPosition.GetZ()), FLT_EPSILON);
    }

    TEST_F(PhysXJointsTest, Joint_BallJoint_FollowerSwingsUpAboutLead)
    {
        // Place lead on top of follower, tie them together with ball joint and send the follower moving sideways in the X and Y directions.
        // The follower should swing up about lead.

        const AZ::Vector3 followerPosition(0.0f, 0.0f, -1.0f);
        const AZ::Vector3 followerInitialLinearVelocity(10.0f, 10.0f, 0.0f);

        const AZ::Vector3 leadPosition(0.0f, 0.0f, 1.0f);
        const AZ::Vector3 leadInitialLinearVelocity(0.0f, 0.0f, 0.0f);

        const AZ::Vector3 jointLocalPosition(0.0f, 0.0f, 2.0f);
        const AZ::Quaternion jointLocalRotation = AZ::Quaternion::CreateRotationY(90.0f);
        const AZ::Transform jointLocalTransform = AZ::Transform::CreateFromQuaternionAndTranslation(
            jointLocalRotation,
            jointLocalPosition);

        // Templated joint component type is irrelevant since joint component is not created for this invocation.
        auto leadEntity = AddBodyColliderEntity<JointComponent>(m_testSceneHandle,
            leadPosition,
            leadInitialLinearVelocity);

        auto jointConfig = AZStd::make_shared<JointComponentConfiguration>();
        jointConfig->m_leadEntity = leadEntity->GetId();
        jointConfig->m_localTransformFromFollower = jointLocalTransform;

        auto jointLimits = AZStd::make_shared <JointLimitProperties>();
        jointLimits->m_isLimited = false;

        auto followerEntity = AddBodyColliderEntity<BallJointComponent>(m_testSceneHandle,
            followerPosition,
            followerInitialLinearVelocity,
            jointConfig,
            nullptr,
            jointLimits);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, followerEntity->GetId());

        EXPECT_GT(followerEndPosition.GetZ(), followerPosition.GetZ());
    }

    TEST_F(PhysXJointsTest, Joint_BallJoint_GlobalConstraint)
    {
        // Place an entity in the world with a rigid body, physx collider, and a ball joint components.
        // Do not set a lead entity on the ball joint component.
        // Set entity's initial velocity to 10 in the X and Y directions on the rigid body component.
        // The entity should swing up on the global constraint.

        const AZ::Vector3 followerPosition(0.0f, 0.0f, -1.0f);
        const AZ::Vector3 followerInitialLinearVelocity(10.0f, 10.0f, 0.0f);

        const AZ::Vector3 jointLocalPosition(0.0f, 0.0f, 2.0f);
        const AZ::Quaternion jointLocalRotation = AZ::Quaternion::CreateRotationY(90.0f);
        const AZ::Transform jointLocalTransform = AZ::Transform::CreateFromQuaternionAndTranslation(jointLocalRotation, jointLocalPosition);

        //we want a global constraint, so leave the lead entity unset.
        auto jointConfig = AZStd::make_shared<JointComponentConfiguration>();
        jointConfig->m_localTransformFromFollower = jointLocalTransform;

        auto jointLimits = AZStd::make_shared<JointLimitProperties>();
        jointLimits->m_isLimited = false;

        auto followerEntity = AddBodyColliderEntity<BallJointComponent>(
            m_testSceneHandle, followerPosition, followerInitialLinearVelocity, jointConfig, nullptr, jointLimits);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, followerEntity->GetId());

        EXPECT_GT(followerEndPosition.GetZ(), followerPosition.GetZ());
    }

    TEST_F(PhysXJointsTest, Joint_HingeJoint_GlobalConstraint)
    {
        // Place an entity in the world with a rigid body, physx collider, and a hinge joint components.
        // Do not set a lead entity on the hinge joint component.
        // Set entity's initial velocity to 10 in the X and Y directions on the rigid body component.
        // The entity should swing up on the global constraint.

        const AZ::Vector3 followerPosition(0.0f, 0.0f, -1.0f);
        const AZ::Vector3 followerInitialLinearVelocity(10.0f, 10.0f, 0.0f);

        const AZ::Vector3 jointLocalPosition(0.0f, 0.0f, 2.0f);
        const AZ::Quaternion jointLocalRotation = AZ::Quaternion::CreateFromEulerAnglesDegrees(AZ::Vector3(0.0f, 180.0f, 90.0f));
        const AZ::Transform jointLocalTransform = AZ::Transform::CreateFromQuaternionAndTranslation(jointLocalRotation, jointLocalPosition);

        // do not set the lead entity as that makes this a global constraint
        auto jointConfig = AZStd::make_shared<JointComponentConfiguration>();
        jointConfig->m_localTransformFromFollower = jointLocalTransform;

        auto jointLimits = AZStd::make_shared<JointLimitProperties>();
        jointLimits->m_isLimited = false;

        auto followerEntity = AddBodyColliderEntity<HingeJointComponent>(
            m_testSceneHandle, followerPosition, followerInitialLinearVelocity, jointConfig, nullptr, jointLimits);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, followerEntity->GetId());

        EXPECT_GT(followerEndPosition.GetZ(), followerPosition.GetZ());
    }

    // D6 drive tests
    //
    // The follower is a 1 DOF slider: linear X is free, everything else locked, so the drive on the X axis
    // is the only thing that can move it. Gravity is off and the lead is heavy and stationary, which makes
    // the follower's X displacement a direct readout of what the drive did.

    namespace D6DriveTest
    {
        const AZ::Vector3 LeadPosition(5.0f, 0.0f, 0.0f);
        const AZ::Vector3 FollowerPosition(-5.0f, 0.0f, 0.0f);
        const AZ::Vector3 JointLocalPosition(5.0f, 0.0f, 0.0f);
        const float LargeForceLimit = 1.0e6f;
    }

    AZStd::unique_ptr<AZ::Entity> AddD6FollowerEntity(
        AzPhysics::SceneHandle sceneHandle,
        const AZ::Vector3& position,
        const AZ::Vector3& initialLinearVelocity,
        AZStd::shared_ptr<JointComponentConfiguration> jointConfig,
        const D6JointComponentConfiguration& d6Config)
    {
        auto entity = AZStd::make_unique<AZ::Entity>("d6TestEntity");

        AZ::TransformConfig transformConfig;
        transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(position);
        entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

        auto colliderConfiguration = AZStd::make_shared<Physics::ColliderConfiguration>();
        auto boxShapeConfiguration = AZStd::make_shared<Physics::BoxShapeConfiguration>();
        auto* boxColliderComponent = entity->CreateComponent<BoxColliderComponent>();
        boxColliderComponent->SetShapeConfigurationList({ AZStd::make_pair(colliderConfiguration, boxShapeConfiguration) });

        AzPhysics::RigidBodyConfiguration rigidBodyConfig;
        rigidBodyConfig.m_initialLinearVelocity = initialLinearVelocity;
        rigidBodyConfig.m_gravityEnabled = false;
        entity->CreateComponent<PhysX::RigidBodyComponent>(rigidBodyConfig, sceneHandle);

        jointConfig->m_followerEntity = entity->GetId();
        const JointGenericProperties genericProperties;
        entity->CreateComponent<D6JointComponent>(*jointConfig, genericProperties, d6Config);

        entity->Init();
        entity->Activate();

        return entity;
    }

    D6JointComponentConfiguration MakeLinearXDriveConfig(
        D6JointDriveType driveType, float stiffness, float damping, float forceLimit)
    {
        D6JointComponentConfiguration d6Config;
        d6Config.m_motionX = D6JointAxis::Free;
        d6Config.m_driveX.m_driveType = driveType;
        d6Config.m_driveX.m_stiffness = stiffness;
        d6Config.m_driveX.m_damping = damping;
        d6Config.m_driveX.m_forceLimit = forceLimit;
        return d6Config;
    }

    struct D6DriveTestBodies
    {
        AZStd::unique_ptr<AZ::Entity> m_lead;
        AZStd::unique_ptr<AZ::Entity> m_follower;
    };

    //! yOffset separates independent lead/follower pairs sharing one scene.
    D6DriveTestBodies CreateD6DriveTestBodies(
        AzPhysics::SceneHandle sceneHandle,
        const D6JointComponentConfiguration& d6Config,
        const AZ::Vector3& followerInitialLinearVelocity = AZ::Vector3::CreateZero(),
        float yOffset = 0.0f)
    {
        const AZ::Vector3 offset(0.0f, yOffset, 0.0f);

        auto lead = AddBodyColliderEntity<JointComponent>(
            sceneHandle, D6DriveTest::LeadPosition + offset, AZ::Vector3::CreateZero());

        auto jointConfig = AZStd::make_shared<JointComponentConfiguration>();
        jointConfig->m_leadEntity = lead->GetId();
        jointConfig->m_localTransformFromFollower = AZ::Transform::CreateTranslation(D6DriveTest::JointLocalPosition);

        auto follower = AddD6FollowerEntity(
            sceneHandle, D6DriveTest::FollowerPosition + offset, followerInitialLinearVelocity, jointConfig, d6Config);

        return { AZStd::move(lead), AZStd::move(follower) };
    }

    AZ::EntityComponentIdPair GetD6JointId(const AZ::Entity& followerEntity)
    {
        const auto* jointComponent = followerEntity.FindComponent<D6JointComponent>();
        EXPECT_NE(jointComponent, nullptr);
        return AZ::EntityComponentIdPair(
            followerEntity.GetId(), jointComponent ? jointComponent->GetId() : AZ::InvalidComponentId);
    }

    void SetDriveVelocityX(const AZ::Entity& followerEntity, float velocityX)
    {
        PhysX::JointRequestBus::Event(
            GetD6JointId(followerEntity),
            &PhysX::JointRequests::SetVelocityGeneral,
            AZ::Vector3(velocityX, 0.0f, 0.0f),
            AZ::Vector3::CreateZero());
    }

    TEST_F(PhysXJointsTest, Joint_D6Joint_VelocityDriveMovesFollowerAlongFreeAxis)
    {
        auto bodies = CreateD6DriveTestBodies(
            m_testSceneHandle, MakeLinearXDriveConfig(D6JointDriveType::Velocity, 0.0f, 1000.0f, D6DriveTest::LargeForceLimit));

        SetDriveVelocityX(*bodies.m_follower, 0.5f);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, bodies.m_follower->GetId());

        const float displacement = followerEndPosition.GetX() - D6DriveTest::FollowerPosition.GetX();
        EXPECT_GT(AZStd::abs(displacement), 0.5f);
    }

    TEST_F(PhysXJointsTest, Joint_D6Joint_WithoutDriveFollowerStaysAtRest)
    {
        // same free axis, but no drive to act on it
        auto bodies = CreateD6DriveTestBodies(
            m_testSceneHandle, MakeLinearXDriveConfig(D6JointDriveType::None, 0.0f, 1000.0f, D6DriveTest::LargeForceLimit));

        SetDriveVelocityX(*bodies.m_follower, 0.5f);

        const AZ::Vector3 followerEndPosition = RunJointTest(m_defaultScene, bodies.m_follower->GetId());

        const float displacement = followerEndPosition.GetX() - D6DriveTest::FollowerPosition.GetX();
        EXPECT_NEAR(displacement, 0.0f, 0.01f);
    }

    TEST_F(PhysXJointsTest, Joint_D6Joint_VelocityDriveReversesWithSign)
    {
        const D6JointComponentConfiguration d6Config =
            MakeLinearXDriveConfig(D6JointDriveType::Velocity, 0.0f, 1000.0f, D6DriveTest::LargeForceLimit);

        auto forwardBodies = CreateD6DriveTestBodies(m_testSceneHandle, d6Config, AZ::Vector3::CreateZero(), 0.0f);
        auto reverseBodies = CreateD6DriveTestBodies(m_testSceneHandle, d6Config, AZ::Vector3::CreateZero(), 10.0f);

        SetDriveVelocityX(*forwardBodies.m_follower, 0.5f);
        SetDriveVelocityX(*reverseBodies.m_follower, -0.5f);

        const AZ::Vector3 forwardEndPosition = RunJointTest(m_defaultScene, forwardBodies.m_follower->GetId());

        AZ::Vector3 reverseEndPosition = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(
            reverseEndPosition, reverseBodies.m_follower->GetId(), &AZ::TransformBus::Events::GetWorldTranslation);

        const float forwardDisplacement = forwardEndPosition.GetX() - D6DriveTest::FollowerPosition.GetX();
        const float reverseDisplacement = reverseEndPosition.GetX() - D6DriveTest::FollowerPosition.GetX();

        EXPECT_LT(forwardDisplacement * reverseDisplacement, 0.0f);
    }

    // Both followers are launched away from the joint; the drives are identical apart from the force limit,
    // so the one allowed to exert force must end up measurably less far along than the one that cannot.
    TEST_F(PhysXJointsTest, Joint_D6Joint_PositionDriveResistsInitialVelocityUnlessForceLimited)
    {
        const AZ::Vector3 followerInitialLinearVelocity(-2.0f, 0.0f, 0.0f);

        auto drivenBodies = CreateD6DriveTestBodies(
            m_testSceneHandle,
            MakeLinearXDriveConfig(D6JointDriveType::Position, 10000.0f, 1000.0f, D6DriveTest::LargeForceLimit),
            followerInitialLinearVelocity,
            0.0f);
        auto forceLimitedBodies = CreateD6DriveTestBodies(
            m_testSceneHandle,
            MakeLinearXDriveConfig(D6JointDriveType::Position, 10000.0f, 1000.0f, 0.0001f),
            followerInitialLinearVelocity,
            10.0f);

        const AZ::Vector3 drivenEndPosition = RunJointTest(m_defaultScene, drivenBodies.m_follower->GetId());

        AZ::Vector3 forceLimitedEndPosition = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(
            forceLimitedEndPosition, forceLimitedBodies.m_follower->GetId(), &AZ::TransformBus::Events::GetWorldTranslation);

        const float drivenDisplacement = drivenEndPosition.GetX() - D6DriveTest::FollowerPosition.GetX();
        const float forceLimitedDisplacement = forceLimitedEndPosition.GetX() - D6DriveTest::FollowerPosition.GetX();

        EXPECT_LT(forceLimitedDisplacement, -1.0f);
        EXPECT_GT(drivenDisplacement - forceLimitedDisplacement, 1.0f);
    }

    TEST_F(PhysXJointsTest, Joint_D6Joint_GetVelocityGeneralReturnsCommandedDriveVelocity)
    {
        auto bodies = CreateD6DriveTestBodies(
            m_testSceneHandle, MakeLinearXDriveConfig(D6JointDriveType::Velocity, 0.0f, 1000.0f, D6DriveTest::LargeForceLimit));

        const AZ::Vector3 commandedLinear(0.5f, 0.0f, 0.0f);
        const AZ::Vector3 commandedAngular(0.0f, 0.0f, 0.25f);

        PhysX::JointRequestBus::Event(
            GetD6JointId(*bodies.m_follower),
            &PhysX::JointRequests::SetVelocityGeneral,
            commandedLinear,
            commandedAngular);

        AZStd::pair<AZ::Vector3, AZ::Vector3> readBack;
        PhysX::JointRequestBus::EventResult(
            readBack, GetD6JointId(*bodies.m_follower), &PhysX::JointRequests::GetVelocityGeneral);

        EXPECT_NEAR(readBack.first.GetX(), commandedLinear.GetX(), 1e-3f);
        EXPECT_NEAR(readBack.first.GetY(), commandedLinear.GetY(), 1e-3f);
        EXPECT_NEAR(readBack.first.GetZ(), commandedLinear.GetZ(), 1e-3f);
        EXPECT_NEAR(readBack.second.GetX(), commandedAngular.GetX(), 1e-3f);
        EXPECT_NEAR(readBack.second.GetY(), commandedAngular.GetY(), 1e-3f);
        EXPECT_NEAR(readBack.second.GetZ(), commandedAngular.GetZ(), 1e-3f);
    }

    template<class JointConfigurationType>
    class PhysXJointsApiTest : public PhysX::GenericPhysicsInterfaceTest
    {
    public:
        
        void SetUp() override
        {
            PhysX::GenericPhysicsInterfaceTest::SetUp();

            if (auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get())
            {
                AzPhysics::RigidBodyConfiguration parentConfiguration;
                AzPhysics::RigidBodyConfiguration childConfiguration;

                auto colliderConfig = AZStd::make_shared<Physics::ColliderConfiguration>();
                auto shapeConfiguration = AZStd::make_shared<Physics::BoxShapeConfiguration>(AZ::Vector3(1.0f, 1.0f, 1.0f));

                parentConfiguration.m_colliderAndShapeData = AzPhysics::ShapeColliderPair(colliderConfig, shapeConfiguration);
                childConfiguration.m_colliderAndShapeData = AzPhysics::ShapeColliderPair(colliderConfig, shapeConfiguration);
                
                // Put the child body a bit to the lower side of X to avoid it colliding with parent
                childConfiguration.m_position.SetX(childConfiguration.m_position.GetX() - 2.0f);
                m_childInitialPos = childConfiguration.m_position;
                parentConfiguration.m_initialLinearVelocity.SetX(10.0f);

                m_parentBodyHandle = sceneInterface->AddSimulatedBody(m_testSceneHandle, &parentConfiguration);
                m_childBodyHandle = sceneInterface->AddSimulatedBody(m_testSceneHandle, &childConfiguration);
            }
        }

        void TearDown() override
        {
            if (auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get())
            {
                sceneInterface->RemoveSimulatedBody(m_testSceneHandle, m_parentBodyHandle);
                sceneInterface->RemoveSimulatedBody(m_testSceneHandle, m_childBodyHandle);    
            }

            PhysX::GenericPhysicsInterfaceTest::TearDown();
        }

        AzPhysics::SimulatedBodyHandle m_parentBodyHandle = AzPhysics::InvalidJointHandle;
        AzPhysics::SimulatedBodyHandle m_childBodyHandle = AzPhysics::InvalidJointHandle;
        AZ::Vector3 m_childInitialPos;
    };

    using JointTypes = testing::Types<
        D6JointLimitConfiguration, 
        FixedJointConfiguration, 
        BallJointConfiguration, 
        HingeJointConfiguration>;
    TYPED_TEST_SUITE(PhysXJointsApiTest, JointTypes);

    TYPED_TEST(PhysXJointsApiTest, Joint_ChildFollowsParent)
    {
        TypeParam jointConfiguration;
        AzPhysics::JointHandle jointHandle = AzPhysics::InvalidJointHandle;

        if (auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get())
        {
            jointHandle = sceneInterface->AddJoint(this->m_testSceneHandle, &jointConfiguration, this->m_parentBodyHandle, this->m_childBodyHandle);
        }

        EXPECT_NE(jointHandle, AzPhysics::InvalidJointHandle);

        // run physics to trigger the the move of parent body
        TestUtils::UpdateScene(this->m_testSceneHandle, AzPhysics::SystemConfiguration::DefaultFixedTimestep, 1);

        AZ::Vector3 childCurrentPos;

        if (auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get())
        {
            auto* childBody = sceneInterface->GetSimulatedBodyFromHandle(this->m_testSceneHandle, this->m_childBodyHandle);
            childCurrentPos = childBody->GetPosition();
        }

        EXPECT_GT(childCurrentPos.GetX(), this->m_childInitialPos.GetX());
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 flip (D.42)
    //
    // A joint frame is authored in the local space of the follower, so a flipped follower mirrors it: the frame (p, R) becomes
    // (M p, M R M). A hinge or a prismatic joint whose motion axis the mirror reverses also turns the frame half a turn, so its
    // angle, limits and travel stay as authored. The math tests check that on frames. The motion tests announce the follower as a
    // Transform 2 entity before it activates, as its Transform 2 component does while the entity activates, so the listener of the
    // PhysX system knows its signs when the rigid body makes its body and enables the joint. Each of them compares a flipped joint
    // with the unflipped joint it is the mirror image of, and the unflipped joint has to move, or the comparison says nothing.

    class PhysXJointFlipTest
        : public PhysX::GenericPhysicsInterfaceTest
    {
    protected:
        static constexpr float Tolerance = 1e-4f;

        // A rotation with all three vector components set, so a wrong sign on any of them shows.
        static AZ::Quaternion GeneralRotation()
        {
            return AZ::Quaternion::CreateRotationZ(0.7f) * AZ::Quaternion::CreateRotationY(0.5f) * AZ::Quaternion::CreateRotationX(0.3f);
        }

        static AZ::Transform GeneralFrame()
        {
            return AZ::Transform::CreateFromQuaternionAndTranslation(GeneralRotation(), AZ::Vector3(0.3f, -1.2f, 2.1f));
        }

        // A quaternion and its negative are the same rotation, so rotations are compared as matrices.
        static AZ::Matrix3x3 ToMatrix(const AZ::Quaternion& rotation)
        {
            return AZ::Matrix3x3::CreateFromQuaternion(rotation);
        }

        // A box with the rigid body of a follower and the joint that the caller adds. The caller's function creates the joint
        // component on the entity it is given. When flipScale names a flip, the entity is announced as a Transform 2 entity
        // before it activates. Nothing collides with it, and its body is made as the entity finishes activating.
        template<typename AddJointFunction>
        AZStd::unique_ptr<AZ::Entity> AddFollower(
            const AZ::Vector3& position,
            const AZ::Vector3& initialLinearVelocity,
            bool gravityEnabled,
            const AZ::Vector3& flipScale,
            AddJointFunction addJoint)
        {
            auto entity = AZStd::make_unique<AZ::Entity>("JointFlipFollower");

            AZ::TransformConfig transformConfig;
            transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(position);
            entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

            auto colliderConfiguration = AZStd::make_shared<Physics::ColliderConfiguration>();
            auto boxShapeConfiguration = AZStd::make_shared<Physics::BoxShapeConfiguration>();
            entity->CreateComponent<BoxColliderComponent>()->SetShapeConfigurationList(
                { AZStd::make_pair(colliderConfiguration, boxShapeConfiguration) });

            AzPhysics::RigidBodyConfiguration rigidBodyConfig;
            rigidBodyConfig.m_initialLinearVelocity = initialLinearVelocity;
            rigidBodyConfig.m_gravityEnabled = gravityEnabled;
            entity->CreateComponent<PhysX::RigidBodyComponent>(rigidBodyConfig, m_testSceneHandle);

            addJoint(*entity);
            entity->Init();

            if (Transform2Mirror::GetSignMask(flipScale) != 0)
            {
                AZ::Transform2NotificationBus::Broadcast(
                    &AZ::Transform2Notifications::OnTransform2Activated, entity->GetId(), flipScale);
            }
            entity->Activate();
            return entity;
        }

        // The Transform 2 component of a real entity deactivates with it, which is what drops the listener's record.
        static void RemoveFollower(AZStd::unique_ptr<AZ::Entity>& entity, const AZ::Vector3& flipScale)
        {
            if (Transform2Mirror::GetSignMask(flipScale) != 0)
            {
                AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entity->GetId());
            }
            entity.reset();
        }

        static AZ::Vector3 GetPosition(const AZ::Entity& entity)
        {
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(position, entity.GetId(), &AZ::TransformBus::Events::GetWorldTranslation);
            return position;
        }

        // The local frame of one side of the native joint that the entity's rigid body is in, read from the PhysX constraint of the
        // body. This is what the joint was made with, which a motion test cannot tell for a joint that holds still. The lead side
        // of a joint is actor 0 and the follower side is actor 1.
        static AZ::Transform GetNativeJointFrame(AZ::Entity& entity, physx::PxJointActorIndex::Enum actorIndex)
        {
            const AzPhysics::RigidBody* rigidBody = entity.FindComponent<PhysX::RigidBodyComponent>()->GetRigidBody();
            const auto* actor = static_cast<const physx::PxRigidActor*>(rigidBody->GetNativePointer());
            PHYSX_SCENE_READ_LOCK(actor->getScene());

            physx::PxConstraint* constraint = nullptr;
            const physx::PxU32 constraintCount = actor->getConstraints(&constraint, 1);
            EXPECT_EQ(constraintCount, 1u);
            if (constraint == nullptr)
            {
                return AZ::Transform::CreateIdentity();
            }

            physx::PxU32 typeId = 0;
            const auto* joint = static_cast<const physx::PxJoint*>(constraint->getExternalReference(typeId));
            EXPECT_EQ(typeId, static_cast<physx::PxU32>(physx::PxConstraintExtIDs::eJOINT));
            if (joint == nullptr || typeId != static_cast<physx::PxU32>(physx::PxConstraintExtIDs::eJOINT))
            {
                return AZ::Transform::CreateIdentity();
            }
            return PxMathConvert(joint->getLocalPose(actorIndex));
        }

        void Step(AZ::u32 steps)
        {
            PhysX::TestUtils::UpdateScene(m_defaultScene, AzPhysics::SystemConfiguration::DefaultFixedTimestep, steps);
        }

        // A hinge pinned to the world, with its frame at framePosition in the local space of the follower. The follower is
        // launched and swings about the frame. Returns the angle the hinge reports, in the authored sign.
        float RunHingeSwing(const AZ::Vector3& flipScale, const AZ::Vector3& framePosition, const AZ::Vector3& launch)
        {
            AZStd::unique_ptr<AZ::Entity> follower = AddFollower(
                AZ::Vector3(0.0f, 0.0f, 10.0f), launch, false, flipScale,
                [&framePosition](AZ::Entity& entity)
                {
                    JointComponentConfiguration jointConfig;
                    jointConfig.m_followerEntity = entity.GetId();
                    jointConfig.m_localTransformFromFollower = AZ::Transform::CreateTranslation(framePosition);

                    JointLimitProperties limits;
                    limits.m_isLimited = false;
                    entity.CreateComponent<HingeJointComponent>(jointConfig, JointGenericProperties(), limits, JointMotorProperties());
                });

            Step(12);

            float angle = 0.0f;
            PhysX::JointRequestBus::EventResult(
                angle,
                AZ::EntityComponentIdPair(follower->GetId(), follower->FindComponent<HingeJointComponent>()->GetId()),
                &PhysX::JointRequests::GetPosition);

            RemoveFollower(follower, flipScale);
            return angle;
        }

        // A ball joint pinned to the world, with gravity swinging the follower about the frame. Returns where the follower ends.
        AZ::Vector3 RunBallSwing(const AZ::Vector3& flipScale, const AZ::Vector3& framePosition)
        {
            AZStd::unique_ptr<AZ::Entity> follower = AddFollower(
                AZ::Vector3(0.0f, 0.0f, 10.0f), AZ::Vector3::CreateZero(), true, flipScale,
                [&framePosition](AZ::Entity& entity)
                {
                    JointComponentConfiguration jointConfig;
                    jointConfig.m_followerEntity = entity.GetId();
                    jointConfig.m_localTransformFromFollower = AZ::Transform::CreateTranslation(framePosition);

                    JointLimitProperties limits;
                    limits.m_isLimited = false;
                    entity.CreateComponent<BallJointComponent>(jointConfig, JointGenericProperties(), limits);
                });

            Step(30);

            const AZ::Vector3 position = GetPosition(*follower);
            RemoveFollower(follower, flipScale);
            return position;
        }

        // A prismatic joint pinned to the world with its travel limited to 0 to 1 along the X axis of its frame, which the follower
        // is launched along. Returns how far the follower moved along the world X axis.
        float RunSlide(const AZ::Vector3& flipScale, const AZ::Vector3& framePosition, const AZ::Vector3& launch)
        {
            const AZ::Vector3 start(0.0f, 0.0f, 10.0f);
            AZStd::unique_ptr<AZ::Entity> follower = AddFollower(
                start, launch, false, flipScale,
                [&framePosition](AZ::Entity& entity)
                {
                    JointComponentConfiguration jointConfig;
                    jointConfig.m_followerEntity = entity.GetId();
                    jointConfig.m_localTransformFromFollower = AZ::Transform::CreateTranslation(framePosition);

                    const JointLimitProperties limits(true, false, 20.0f, 1.0f, 0.0f, 100.0f, 0.1f);
                    entity.CreateComponent<PrismaticJointComponent>(
                        jointConfig, JointGenericProperties(), limits, JointMotorProperties());
                });

            Step(60);

            const float travel = GetPosition(*follower).GetX() - start.GetX();
            RemoveFollower(follower, flipScale);
            return travel;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // The frame

    TEST_F(PhysXJointFlipTest, MirrorJointFrame_NoFlip_ReturnsTheFrameAsItIs)
    {
        const AZ::Transform frame = GeneralFrame();

        // A half turn is only asked for when a sign is negative, and with none it still changes nothing.
        for (const bool halfTurn : { false, true })
        {
            const AZ::Transform mirrored = Transform2Mirror::MirrorJointFrame(AZ::Vector3::CreateOne(), frame, halfTurn);
            EXPECT_EQ(mirrored.GetTranslation(), frame.GetTranslation());
            EXPECT_EQ(mirrored.GetRotation(), frame.GetRotation());

            const AZ::Transform authored = Transform2Mirror::UnmirrorJointFrame(AZ::Vector3::CreateOne(), frame, halfTurn);
            EXPECT_EQ(authored.GetTranslation(), frame.GetTranslation());
            EXPECT_EQ(authored.GetRotation(), frame.GetRotation());
        }
    }

    TEST_F(PhysXJointFlipTest, MirrorJointFrame_EverySignPattern_ConjugatesTheRotationAndKeepsItProper)
    {
        const AZ::Transform frame = GeneralFrame();
        const AZ::Matrix3x3 authoredRotation = ToMatrix(frame.GetRotation());

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 signs = Transform2Mirror::GetMaskSigns(mask);
            const AZ::Transform mirrored = Transform2Mirror::MirrorJointFrame(signs, frame, false);
            const AZ::Matrix3x3 mirroredRotation = ToMatrix(mirrored.GetRotation());

            // (p, R) becomes (M p, M R M), with M = diag(signs).
            const AZ::Matrix3x3 diagonal = AZ::Matrix3x3::CreateDiagonal(signs);
            EXPECT_THAT(mirrored.GetTranslation(), UnitTest::IsCloseTolerance(signs * frame.GetTranslation(), Tolerance))
                << "sign mask " << mask;
            EXPECT_TRUE(mirroredRotation.IsClose(diagonal * authoredRotation * diagonal, Tolerance)) << "sign mask " << mask;

            // A mirrored frame is still a frame: a proper rotation, not a reflection.
            EXPECT_NEAR(mirroredRotation.GetDeterminant(), 1.0f, 1e-3f) << "sign mask " << mask;
            EXPECT_TRUE(mirroredRotation.IsOrthogonal(1e-3f)) << "sign mask " << mask;
        }
    }

    TEST_F(PhysXJointFlipTest, MirrorJointFrame_HalfTurn_ReversesTheXAndZAxesOfTheMirroredFrame)
    {
        const AZ::Transform frame = GeneralFrame();

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 signs = Transform2Mirror::GetMaskSigns(mask);
            const AZ::Transform mirrored = Transform2Mirror::MirrorJointFrame(signs, frame, false);
            const AZ::Transform turned = Transform2Mirror::MirrorJointFrame(signs, frame, true);

            // The turn is about the frame's own Y axis, so the position and Y stay and X and Z reverse.
            EXPECT_EQ(turned.GetTranslation(), mirrored.GetTranslation()) << "sign mask " << mask;
            EXPECT_THAT(turned.GetBasisX(), UnitTest::IsCloseTolerance(-mirrored.GetBasisX(), Tolerance)) << "sign mask " << mask;
            EXPECT_THAT(turned.GetBasisY(), UnitTest::IsCloseTolerance(mirrored.GetBasisY(), Tolerance)) << "sign mask " << mask;
            EXPECT_THAT(turned.GetBasisZ(), UnitTest::IsCloseTolerance(-mirrored.GetBasisZ(), Tolerance)) << "sign mask " << mask;
            EXPECT_NEAR(ToMatrix(turned.GetRotation()).GetDeterminant(), 1.0f, 1e-3f) << "sign mask " << mask;
        }
    }

    TEST_F(PhysXJointFlipTest, MirrorJointFrame_FrameWithAScale_KeepsTheScale)
    {
        AZ::Transform frame = GeneralFrame();
        frame.SetUniformScale(2.0f);

        const AZ::Transform mirrored = Transform2Mirror::MirrorJointFrame(AZ::Vector3(-1.0f, 1.0f, 1.0f), frame, true);
        EXPECT_NEAR(mirrored.GetUniformScale(), 2.0f, Tolerance);
    }

    TEST_F(PhysXJointFlipTest, UnmirrorJointFrame_TakesTheMirroredFrameBack)
    {
        // What a manipulator writes is taken back to the authored frame, with or without the half turn.
        const AZ::Transform frame = GeneralFrame();

        for (AZ::u32 mask = 0; mask < 8; ++mask)
        {
            for (const bool halfTurn : { false, true })
            {
                const AZ::Vector3 signs = Transform2Mirror::GetMaskSigns(mask);
                const AZ::Transform back = Transform2Mirror::UnmirrorJointFrame(
                    signs, Transform2Mirror::MirrorJointFrame(signs, frame, halfTurn), halfTurn);

                EXPECT_EQ(back.GetTranslation(), frame.GetTranslation()) << "sign mask " << mask << " half turn " << halfTurn;
                EXPECT_NEAR(AZStd::abs(back.GetRotation().Dot(frame.GetRotation())), 1.0f, Tolerance)
                    << "sign mask " << mask << " half turn " << halfTurn;
            }
        }
    }

    TEST_F(PhysXJointFlipTest, ReversesAxis_EverySignPattern_FollowsTheSignsOfTheAxis)
    {
        for (AZ::u32 mask = 0; mask < 8; ++mask)
        {
            const AZ::Vector3 signs = Transform2Mirror::GetMaskSigns(mask);

            // A hinge turns about X, which the mirror reverses when the signs of Y and Z differ. A prismatic joint slides along X,
            // which it reverses when X itself is flipped.
            const bool yFlipped = (mask & 2u) != 0;
            const bool zFlipped = (mask & 4u) != 0;
            EXPECT_EQ(Transform2Mirror::ReversesHingeAxis(signs), yFlipped != zFlipped) << "sign mask " << mask;
            EXPECT_EQ(Transform2Mirror::ReversesSlideAxis(signs), (mask & 1u) != 0) << "sign mask " << mask;
        }
    }

    TEST_F(PhysXJointFlipTest, Hinge_MirrorImageOfATurn_ReadsTheAuthoredAngleThroughTheHalfTurn)
    {
        const AZ::Transform frame = GeneralFrame();
        const AZ::Matrix3x3 authoredRotation = ToMatrix(frame.GetRotation());
        const float angle = 0.6f;

        // The follower turns by the angle about the X axis of the authored frame: Q = Rf Rx(angle) Rf^T, in its own space.
        const AZ::Matrix3x3 turn = authoredRotation * AZ::Matrix3x3::CreateRotationX(angle) * authoredRotation.GetTranspose();

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 signs = Transform2Mirror::GetMaskSigns(mask);
            const AZ::Matrix3x3 diagonal = AZ::Matrix3x3::CreateDiagonal(signs);

            // The mirror image of the motion is M Q M. The joint reads it as the turn seen from its own frame: Rf'^T Q' Rf'.
            const AZ::Matrix3x3 mirroredTurn = diagonal * turn * diagonal;
            const bool halfTurn = Transform2Mirror::ReversesHingeAxis(signs);
            const AZ::Matrix3x3 flippedRotation = ToMatrix(Transform2Mirror::MirrorJointFrame(signs, frame, halfTurn).GetRotation());
            const AZ::Matrix3x3 read = flippedRotation.GetTranspose() * mirroredTurn * flippedRotation;
            EXPECT_TRUE(read.IsClose(AZ::Matrix3x3::CreateRotationX(angle), 1e-3f)) << "sign mask " << mask;

            // Without the half turn the mirror would have reversed the angle, so the turn is needed exactly when sy * sz < 0.
            if (halfTurn)
            {
                const AZ::Matrix3x3 unturnedRotation = ToMatrix(Transform2Mirror::MirrorJointFrame(signs, frame, false).GetRotation());
                const AZ::Matrix3x3 readWithoutTurn = unturnedRotation.GetTranspose() * mirroredTurn * unturnedRotation;
                EXPECT_TRUE(readWithoutTurn.IsClose(AZ::Matrix3x3::CreateRotationX(-angle), 1e-3f)) << "sign mask " << mask;
            }
        }
    }

    TEST_F(PhysXJointFlipTest, Prismatic_MirrorImageOfASlide_ReadsTheAuthoredTravelThroughTheHalfTurn)
    {
        const AZ::Transform frame = GeneralFrame();
        const float travel = 0.8f;

        // The follower slides by the travel along the X axis of the authored frame.
        const AZ::Vector3 slide = ToMatrix(frame.GetRotation()) * AZ::Vector3(travel, 0.0f, 0.0f);

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 signs = Transform2Mirror::GetMaskSigns(mask);

            // The mirror image of the slide is M d. The joint reads it along its own X axis.
            const AZ::Vector3 mirroredSlide = signs * slide;
            const bool halfTurn = Transform2Mirror::ReversesSlideAxis(signs);
            const AZ::Transform flipped = Transform2Mirror::MirrorJointFrame(signs, frame, halfTurn);
            EXPECT_NEAR(mirroredSlide.Dot(flipped.GetBasisX()), travel, Tolerance) << "sign mask " << mask;

            // Without the half turn the travel would read reversed when X is flipped.
            if (halfTurn)
            {
                const AZ::Transform unturned = Transform2Mirror::MirrorJointFrame(signs, frame, false);
                EXPECT_NEAR(mirroredSlide.Dot(unturned.GetBasisX()), -travel, Tolerance) << "sign mask " << mask;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener

    TEST_F(PhysXJointFlipTest, FindSigns_ListenerKnowsTheEntity_AnswersItsSignsAndNoFlipOtherwise)
    {
        AZ::Entity entity("FindSignsEntity");
        AZ::Entity otherEntity("FindSignsOtherEntity");
        const AZ::EntityId entityId = entity.GetId();
        const AZ::Vector3 noFlip = AZ::Vector3::CreateOne();

        // The listener knows no Transform 2 entity yet.
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(noFlip));

        // The signs follow the scale as soon as the Transform 2 of the entity activates, which is before the entity is active.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-2.0f, 1.0f, 0.5f));
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));

        // An entity the listener does not know is not flipped, even while it holds another one.
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(otherEntity.GetId()), UnitTest::IsClose(noFlip));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, -3.0f, -1.0f));
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(AZ::Vector3(1.0f, -1.0f, -1.0f)));

        // Zero is not a flip.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(0.0f, 1.0f, 1.0f));
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(noFlip));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(noFlip));
    }

    //////////////////////////////////////////////////////////////////////////
    // The joints

    TEST_F(PhysXJointFlipTest, HingeJoint_FlippedFollower_SwingsAsTheMirrorImageAndReadsTheAuthoredAngle)
    {
        // The frame sits below the follower and a little to the side and above, with the hinge axis along the local X axis. The
        // follower is launched along Z and swings about the frame. The mirror image of the scene launches it along M * launch.
        const AZ::Vector3 framePosition(0.3f, -1.0f, 0.5f);
        const AZ::Vector3 launch(0.0f, 0.0f, 6.0f);

        const float authoredAngle = RunHingeSwing(AZ::Vector3::CreateOne(), framePosition, launch);
        EXPECT_GT(AZStd::abs(authoredAngle), 0.2f);

        // Every pattern: the hinge reads the authored angle, with the half turn where the mirror reverses its axis (Y, Z and
        // the patterns that flip exactly one of them), and none where it does not (X alone, Y and Z together).
        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 flipScale = Transform2Mirror::GetMaskSigns(mask);
            const float flippedAngle = RunHingeSwing(flipScale, framePosition, flipScale * launch);
            EXPECT_NEAR(flippedAngle, authoredAngle, 0.1f) << "sign mask " << mask;
        }
    }

    TEST_F(PhysXJointFlipTest, BallJoint_FlippedFollower_SwingsAboutTheMirroredFrame)
    {
        // The frame sits level with the follower, to the side and forward, so gravity swings the follower down about it. Gravity
        // is not authored in the space of the follower and does not flip, so only the patterns that leave Z alone are mirror images.
        const AZ::Vector3 framePosition(1.0f, 0.5f, 0.0f);
        const AZ::Vector3 start(0.0f, 0.0f, 10.0f);

        const AZ::Vector3 authoredMove = RunBallSwing(AZ::Vector3::CreateOne(), framePosition) - start;
        EXPECT_GT(AZStd::abs(authoredMove.GetZ()), 0.1f);

        for (const AZ::u32 mask : { 1u, 2u, 3u })
        {
            const AZ::Vector3 flipScale = Transform2Mirror::GetMaskSigns(mask);
            const AZ::Vector3 flippedMove = RunBallSwing(flipScale, framePosition) - start;
            EXPECT_THAT(flippedMove, UnitTest::IsCloseTolerance(flipScale * authoredMove, 0.05f)) << "sign mask " << mask;
        }
    }

    TEST_F(PhysXJointFlipTest, PrismaticJoint_FlippedFollower_SlidesAsTheMirrorImage)
    {
        // The travel is limited to 0 to 1 along the X axis of the frame, and the follower is launched along that axis. When the mirror
        // reverses the axis, the half turn keeps the limits as authored, and the follower travels the mirrored way as far as before.
        const AZ::Vector3 framePosition(0.3f, 0.2f, -0.4f);
        const AZ::Vector3 launch(5.0f, 0.0f, 0.0f);

        const float authoredTravel = RunSlide(AZ::Vector3::CreateOne(), framePosition, launch);
        EXPECT_GT(authoredTravel, 0.5f);

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 flipScale = Transform2Mirror::GetMaskSigns(mask);
            const float flippedTravel = RunSlide(flipScale, framePosition, flipScale * launch);
            EXPECT_NEAR(flippedTravel, flipScale.GetX() * authoredTravel, 0.05f) << "sign mask " << mask;
        }
    }

    TEST_F(PhysXJointFlipTest, FixedJoint_FlippedFollower_HoldsTheMirroredFramesAndKeepsTheBodiesWelded)
    {
        // The frame is between the bodies, and mirrored it is on the far side of the follower. The lead frame derives from the
        // same world frame, so the bodies start in agreement and stay at the distance they started at as the lead is pushed along.
        // That alone holds with the authored frame too, so the frames the native joint was made with are read and checked as well.
        const AZ::Vector3 followerPosition(0.0f, 0.0f, 10.0f);
        const AZ::Vector3 leadPosition(2.0f, 0.0f, 10.0f);
        const AZ::Vector3 flipScale(-1.0f, 1.0f, 1.0f);

        AZStd::unique_ptr<AZ::Entity> lead = AddBodyColliderEntity<JointComponent>(
            m_testSceneHandle, leadPosition, AZ::Vector3(10.0f, 0.0f, 0.0f));

        AZStd::unique_ptr<AZ::Entity> follower = AddFollower(
            followerPosition, AZ::Vector3::CreateZero(), false, flipScale,
            [&lead](AZ::Entity& entity)
            {
                JointComponentConfiguration jointConfig;
                jointConfig.m_leadEntity = lead->GetId();
                jointConfig.m_followerEntity = entity.GetId();
                jointConfig.m_localTransformFromFollower = AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f));
                entity.CreateComponent<FixedJointComponent>(jointConfig, JointGenericProperties());
            });

        // The authored frame is (1, 0, 0) in the follower. The follower holds it mirrored at (-1, 0, 0), which is (-1, 0, 10) in the
        // world, and the lead holds that same point at (-3, 0, 0) in its own space.
        Step(1);
        EXPECT_THAT(
            GetNativeJointFrame(*follower, physx::PxJointActorIndex::eACTOR1).GetTranslation(),
            UnitTest::IsCloseTolerance(AZ::Vector3(-1.0f, 0.0f, 0.0f), Tolerance));
        EXPECT_THAT(
            GetNativeJointFrame(*lead, physx::PxJointActorIndex::eACTOR0).GetTranslation(),
            UnitTest::IsCloseTolerance(AZ::Vector3(-3.0f, 0.0f, 0.0f), Tolerance));

        Step(120);

        const float separation = GetPosition(*lead).GetX() - GetPosition(*follower).GetX();
        EXPECT_NEAR(separation, 2.0f, 0.1f);
        EXPECT_GT(GetPosition(*follower).GetX(), 1.0f);

        RemoveFollower(follower, flipScale);
    }
}
