/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzTest/AzTest.h>

#include <BoxColliderComponent.h>
#include <ForceRegionComponent.h>
#include <RigidBodyComponent.h>
#include <StaticRigidBodyComponent.h>

#include <PhysX/ForceRegionComponentBus.h>

#include <LmbrCentral/Shape/SplineComponentBus.h>

#include <AzCore/Component/Entity.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Transform.h>

#include <AZTestShared/Math/MathTestHelpers.h>

#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Physics/ColliderComponentBus.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Physics/Shape.h>
#include <AzFramework/Physics/ShapeConfiguration.h>
#include <AzFramework/Physics/SystemBus.h>
#include <AzFramework/Physics/PhysicsSystem.h>
#include <AzFramework/Physics/Collision/CollisionEvents.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBody.h>

#include <Source/ForceRegion.h>
#include <Tests/PhysXTestCommon.h>

namespace PhysX
{
    enum ForceType : AZ::u8
    {
        WorldSpaceForce
        , LocalSpaceForce
        , PointForce
        , SplineFollowForce
        , SimpleDragForce
        , LinearDampingForce
    };

    class PhysXForceRegionTest
        : public::testing::Test
        , protected Physics::DefaultWorldBus::Handler
    {
        void SetUp() override
        {
            if (auto* physicsSystem = AZ::Interface<AzPhysics::SystemInterface>::Get())
            {
                AzPhysics::SceneConfiguration sceneConfiguration = physicsSystem->GetDefaultSceneConfiguration();
                sceneConfiguration.m_sceneName = AzPhysics::DefaultPhysicsSceneName;
                m_testSceneHandle = physicsSystem->AddScene(sceneConfiguration);
                m_defaultScene = physicsSystem->GetScene(m_testSceneHandle);
            }

            Physics::DefaultWorldBus::Handler::BusConnect();
        }

        void TearDown() override
        {
            Physics::DefaultWorldBus::Handler::BusDisconnect();

            //Cleanup the test scene
            m_defaultScene = nullptr;
            if (auto* physicsSystem = AZ::Interface<AzPhysics::SystemInterface>::Get())
            {
                physicsSystem->RemoveScene(m_testSceneHandle);
            }
            m_testSceneHandle = AzPhysics::InvalidSceneHandle;
            TestUtils::ResetPhysXSystem();
        }

        // DefaultWorldBus
        AzPhysics::SceneHandle GetDefaultSceneHandle() const override
        {
            return m_testSceneHandle;
        }

        AzPhysics::Scene* m_defaultScene = nullptr;
        AzPhysics::SceneHandle m_testSceneHandle = AzPhysics::InvalidSceneHandle;

    public:
        AzPhysics::Scene* GetTestScene() const
        {
            return m_defaultScene;
        }
        AzPhysics::SceneHandle GetTestSceneHandle() const
        {
            return m_testSceneHandle;
        }
    };

    AZStd::unique_ptr<AZ::Entity> AddTestRigidBodyCollider(AZ::Vector3& position
        , ForceType forceType
        , AzPhysics::SceneHandle sceneHandle
        , const char* name = "TestObjectEntity")
    {
        AZStd::unique_ptr<AZ::Entity> entity(aznew AZ::Entity(name));

        AZ::TransformConfig transformConfig;
        if (forceType == PointForce)
        {
            position.SetX(0.05f);
        }
        transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(position);
        entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

        auto colliderConfiguration = AZStd::make_shared<Physics::ColliderConfiguration>();
        auto boxShapeConfiguration = AZStd::make_shared<Physics::BoxShapeConfiguration>();
        auto boxColliderComponent = entity->CreateComponent<BoxColliderComponent>();
        boxColliderComponent->SetShapeConfigurationList({ AZStd::make_pair(colliderConfiguration, boxShapeConfiguration) });

        AzPhysics::RigidBodyConfiguration rigidBodyConfig;
        rigidBodyConfig.m_computeMass = false;
        entity->CreateComponent<PhysX::RigidBodyComponent>(rigidBodyConfig, sceneHandle);

        entity->Init();
        entity->Activate();

        return entity;
    }

    namespace DefaultForceRegionParams
    {
        const AZ::Vector3 ForceDirection(0.0f, 0.0f, 1.0f);
        const AZ::Vector3 RotationY(.0f, 90.0f, .0f);
        const float ForceMagnitude = 100.0f;
        const float DampingRatio = 0.0f;
        const float Frequency = 1.0f;
        const float TargetSpeed = 1.0f;
        const float LookAhead = 0.0f;
        const float DragCoefficient = 1.0f;
        const float VolumeDensity = 5.0f;
        const float Damping = 10.0f;
    }

    template<typename ColliderType>
    AZStd::unique_ptr<AZ::Entity> AddForceRegion(const AZ::Vector3& position, ForceType forceType)
    {
        AZStd::unique_ptr<AZ::Entity> forceRegionEntity(aznew AZ::Entity("ForceRegion"));

        AZ::TransformConfig transformConfig;
        transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(position);
        forceRegionEntity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

        auto colliderConfiguration = AZStd::make_shared<Physics::ColliderConfiguration>();
        colliderConfiguration->m_isTrigger = true;
        auto shapeConfiguration = AZStd::make_shared<typename ColliderType::Configuration>();

        auto colliderComponent = forceRegionEntity->CreateComponent<ColliderType>();
        colliderComponent->SetShapeConfigurationList({ AZStd::make_pair(colliderConfiguration, shapeConfiguration) });

        // We need StaticRigidBodyComponent to get shapes from collider component added to PhysX world
        forceRegionEntity->CreateComponent<StaticRigidBodyComponent>();

        forceRegionEntity->CreateComponent<ForceRegionComponent>();

        if (forceType == SplineFollowForce)
        {
            forceRegionEntity->CreateComponent(AZ::TypeId("{F0905297-1E24-4044-BFDA-BDE3583F1E57}"));//SplineComponent
        }

        forceRegionEntity->Init();
        forceRegionEntity->Activate();

        if (forceType == WorldSpaceForce)
        {
            PhysX::ForceRegionRequestBus::Event(forceRegionEntity->GetId()
                , &PhysX::ForceRegionRequests::AddForceWorldSpace
                , DefaultForceRegionParams::ForceDirection
                , DefaultForceRegionParams::ForceMagnitude);
        }
        else if (forceType == LocalSpaceForce)
        {
            PhysX::ForceRegionRequestBus::Event(forceRegionEntity->GetId()
                , &PhysX::ForceRegionRequests::AddForceLocalSpace
                , DefaultForceRegionParams::ForceDirection
                , DefaultForceRegionParams::ForceMagnitude);
            AZ::TransformBus::Event(forceRegionEntity->GetId()
                , &AZ::TransformBus::Events::SetLocalRotation
                , DefaultForceRegionParams::RotationY);
        }
        else if (forceType == PointForce)
        {
            PhysX::ForceRegionRequestBus::Event(forceRegionEntity->GetId()
                , &PhysX::ForceRegionRequests::AddForcePoint
                , DefaultForceRegionParams::ForceMagnitude);
        }
        else if (forceType == SplineFollowForce)
        {
            PhysX::ForceRegionRequestBus::Event(forceRegionEntity->GetId()
                , &PhysX::ForceRegionRequests::AddForceSplineFollow
                , DefaultForceRegionParams::DampingRatio
                , DefaultForceRegionParams::Frequency
                , DefaultForceRegionParams::TargetSpeed
                , DefaultForceRegionParams::LookAhead
            );

            const AZStd::vector<AZ::Vector3> vertices =
            {
                AZ::Vector3(0.0f, 0.0f, 12.5f),
                AZ::Vector3(0.25f, 0.25f, 12.0f),
                AZ::Vector3(0.5f, 0.5f, 12.0f)
            };

            LmbrCentral::SplineComponentRequestBus::Event(forceRegionEntity->GetId()
                , &LmbrCentral::SplineComponentRequestBus::Events::SetVertices, vertices);
        }
        else if (forceType == SimpleDragForce)
        {
            PhysX::ForceRegionRequestBus::Event(forceRegionEntity->GetId()
                , &PhysX::ForceRegionRequests::AddForceSimpleDrag
                , DefaultForceRegionParams::DragCoefficient
                , DefaultForceRegionParams::VolumeDensity);
        }
        else if (forceType == LinearDampingForce)
        {
            PhysX::ForceRegionRequestBus::Event(forceRegionEntity->GetId()
                , &PhysX::ForceRegionRequests::AddForceLinearDamping
                , DefaultForceRegionParams::Damping);
        }

        return forceRegionEntity;
    }

    template<typename ColliderType>
    AZ::Vector3 TestForceVolume(AzPhysics::SceneHandle sceneHandle, ForceType forceType)
    {
        AZ::Vector3 velocity = AZ::Vector3::CreateZero();

        AZ::Vector3 position(0.0f, 0.0f, 16.0f);
        auto rigidBodyCollider = AddTestRigidBodyCollider(position, forceType, sceneHandle, "TestBox" );
        auto forceRegion = AddForceRegion<ColliderType>(AZ::Vector3(0.0f, 0.0f, 12.0f), forceType);

        //Run simulation for a while - bounces box once on force volume
        constexpr const float deltaTime = 1.0f / 180.0f;
        TestUtils::UpdateScene(sceneHandle, deltaTime, 240);

        Physics::RigidBodyRequestBus::EventResult(velocity
            , rigidBodyCollider->GetId()
            , &Physics::RigidBodyRequestBus::Events::GetLinearVelocity);

        return velocity;
    }

    template<typename ColliderType>
    void TestAppliesSameMagnitude(AzPhysics::SceneHandle sceneHandle, ForceType forceType)
    {
        struct ForceRegionMagnitudeChecker
            : public ForceRegionNotificationBus::Handler
        {
            ForceRegionMagnitudeChecker()  { ForceRegionNotificationBus::Handler::BusConnect(); }
            ~ForceRegionMagnitudeChecker() { ForceRegionNotificationBus::Handler::BusDisconnect(); }

            void OnCalculateNetForce(AZ::EntityId, AZ::EntityId, const AZ::Vector3&, float netForceMagnitude)
            {
                // This callback can potentially be called in every frame, so just only catch the first failure to avoid spamming
                if (!m_failed)
                {
                    const float forceRegionMaxError = 0.05f; // Force region uses fast approximation for length calculations, hence the error
                    const bool result = AZ::IsClose(netForceMagnitude, DefaultForceRegionParams::ForceMagnitude, forceRegionMaxError);
                    if (!result)
                    {
                        m_failed = true;
                    }
                    EXPECT_TRUE(result);
                }
            }

            bool m_failed = false;
        };
        ForceRegionMagnitudeChecker magnitudeChecker;
        TestForceVolume<ColliderType>(sceneHandle, forceType);
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_WorldSpaceForce_EntityVelocityZPositive)
    {
        AZ::Vector3 entityVelocity = TestForceVolume<BoxColliderComponent>(GetTestSceneHandle(), WorldSpaceForce);
        // World space force direction: AZ::Vector3(0.0f, 0.0f, 1.0f)
        EXPECT_GT(entityVelocity.GetZ(), 0.0f); // World space force causes box to bounce upwards
        EXPECT_NEAR(entityVelocity.GetX(), 0.0f, AZ::Constants::FloatEpsilon);
        EXPECT_NEAR(entityVelocity.GetY(), 0.0f, AZ::Constants::FloatEpsilon);
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_LocalSpaceForce_EntityVelocityZPositive)
    {
        AZ::Vector3 entityVelocity = TestForceVolume<BoxColliderComponent>(GetTestSceneHandle(), LocalSpaceForce);
        // Local space force direction: AZ::Vector3(0.0f, 0.0f, 1.0f)
        // Force region was rotated about Y-axis by 90 deg
        EXPECT_GT(entityVelocity.GetX(), 0.0f); // Falling body should be moving in positive X direction since force region is rotated.
        EXPECT_NEAR(entityVelocity.GetY(), 0.0f, AZ::Constants::FloatEpsilon);
        EXPECT_LT(entityVelocity.GetZ(), 0.0f); // Gravity
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_PointForce_EntityVelocityZPositive)
    {
        // Falling body was positioned at AZ::Vector3(0.05f, 0.0f, 16.0f)
        // Force region was positioned at AZ::Vector3(0.0f, 0.0f, 12.0f)
        // PointForce causes box to bounce upwards and to the right.
        AZ::Vector3 entityVelocity = TestForceVolume<BoxColliderComponent>(GetTestSceneHandle(), PointForce);
        EXPECT_GT(entityVelocity.GetX(), 0.0f);
        EXPECT_NEAR(entityVelocity.GetY(), 0.0f, AZ::Constants::FloatEpsilon);
        EXPECT_GT(entityVelocity.GetZ(), 0.0f);
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_SplineFollowForce_EntityVelocitySpecificValue)
    {
        AZ::Vector3 entityVelocity = TestForceVolume<BoxColliderComponent>(GetTestSceneHandle(), SplineFollowForce);
        // Follow spline direction towards positive X and Y.
        EXPECT_GT(entityVelocity.GetX(), 0.0f);
        EXPECT_GT(entityVelocity.GetY(), 0.0f);
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_SimpleDragForce_EntityVelocitySpecificValue)
    {
        AZ::Vector3 entityVelocity = TestForceVolume<BoxColliderComponent>(GetTestSceneHandle(), SimpleDragForce);
        EXPECT_GT(entityVelocity.GetZ(), -12.66f); // Falling velocity should be slower than free fall velocity, which is -12.65 (+-0.01).
        EXPECT_NEAR(entityVelocity.GetX(), 0.0f, AZ::Constants::FloatEpsilon); // Dragging should not change original direction.
        EXPECT_NEAR(entityVelocity.GetY(), 0.0f, AZ::Constants::FloatEpsilon); // Dragging should not change original direction.
    }

    class PhysXForceRegionTestParameterized
        : public ::testing::WithParamInterface<::testing::tuple<float, float>>
        , public PhysXForceRegionTest
    {
    };

    TEST_P(PhysXForceRegionTestParameterized, ForceRegion_SimpleDragForce_NoOscillation)
    {
        auto [density, mass] = GetParam();

        auto sceneHandle = GetTestSceneHandle();
        AZ::Vector3 velocity = AZ::Vector3::CreateZero();

        AZ::Vector3 position(0.0f, 0.0f, 16.0f);
        auto dynamicRigidBody = AddTestRigidBodyCollider(position, SimpleDragForce, sceneHandle, "TestBox");
        auto forceRegion = AddForceRegion<BoxColliderComponent>(AZ::Vector3(0.0f, 0.0f, 12.0f), SimpleDragForce);

        ForceSimpleDragRequestBus::Event(forceRegion->GetId(), &ForceSimpleDragRequestBus::Events::SetDensity, density);
        Physics::RigidBodyRequestBus::Event(dynamicRigidBody->GetId(), &Physics::RigidBodyRequests::SetMass, mass);

        constexpr const float deltaTime = 1.0f / 180.0f;
        float maxVelocityZ = 0.0f;
        for (int timeStep = 0; timeStep < 300; timeStep++)
        {
            TestUtils::UpdateScene(sceneHandle, deltaTime, 1);
            Physics::RigidBodyRequestBus::EventResult(
                velocity, dynamicRigidBody->GetId(), &Physics::RigidBodyRequestBus::Events::GetLinearVelocity);

            maxVelocityZ = AZStd::GetMax(maxVelocityZ, velocity.GetZ());
        }

        // the velocity should never be upward if there is no oscillation
        EXPECT_LE(maxVelocityZ, 0.0f);
    }

    INSTANTIATE_TEST_SUITE_P(
        PhysXForceRegion,
        PhysXForceRegionTestParameterized,
        ::testing::Combine(::testing::Values(1.0f, 1e2f, 1e4f, 1e6f), ::testing::Values(1e-3f, 1e-2f, 1e-1f, 1.0f)));

    TEST_F(PhysXForceRegionTest, ForceRegion_LinearDampingForce_EntityVelocitySpecificValue)
    {
        AZ::Vector3 entityVelocity = TestForceVolume<BoxColliderComponent>(GetTestSceneHandle(), LinearDampingForce);
        EXPECT_GT(entityVelocity.GetZ(), -12.66f); // Falling velocity should be slower than free fall velocity, which is -12.65 (+-0.01).
        EXPECT_NEAR(entityVelocity.GetX(), 0.0f, AZ::Constants::FloatEpsilon); // Damping should not change original direction.
        EXPECT_NEAR(entityVelocity.GetY(), 0.0f, AZ::Constants::FloatEpsilon); // Damping should not change original direction.
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_PointForce_AppliesSameMagnitude)
    {
        TestAppliesSameMagnitude<BoxColliderComponent>(GetTestSceneHandle(), PointForce);
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_WorldSpaceForce_AppliesSameMagnitude)
    {
        TestAppliesSameMagnitude<BoxColliderComponent>(GetTestSceneHandle(), WorldSpaceForce);
    }

    TEST_F(PhysXForceRegionTest, ForceRegion_LocalSpaceForce_AppliesSameMagnitude)
    {
        TestAppliesSameMagnitude<BoxColliderComponent>(GetTestSceneHandle(), LocalSpaceForce);
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2: a flipped force region
    //
    // A flip mirrors the local space forces and the spline of the region across the local planes named by the mask, and
    // never touches what is authored. The tests drive the region the way the Transform 2 listener does, through
    // ForceRegionRequestBus::SetFlipMask, and the listener itself through the Transform 2 notifications.

    namespace
    {
        constexpr float FlipTolerance = 1e-3f;
        constexpr AZ::u8 FlipX = 1;
        constexpr AZ::u8 FlipY = 2;
        constexpr AZ::u8 FlipZ = 4;

        // A rotation with all three vector components set, so a wrong sign on any of them shows.
        AZ::Quaternion GeneralRotation()
        {
            return AZ::Quaternion::CreateRotationZ(0.7f) * AZ::Quaternion::CreateRotationY(0.5f) * AZ::Quaternion::CreateRotationX(0.3f);
        }

        // An entity with a Transform 1 at the pose, and a Spline component when asked for. A ForceRegion object stands in for
        // the force region component on it, because the component does not hand its region out.
        AZStd::unique_ptr<AZ::Entity> CreateFlipRegionEntity(const AZ::Transform& world, bool withSpline)
        {
            AZStd::unique_ptr<AZ::Entity> entity(aznew AZ::Entity("FlipRegionEntity"));

            AZ::TransformConfig transformConfig;
            transformConfig.m_worldTransform = world;
            entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

            if (withSpline)
            {
                AZ::Component* splineComponent = entity->CreateComponent(AZ::TypeId("{F0905297-1E24-4044-BFDA-BDE3583F1E57}")); // SplineComponent
                EXPECT_NE(splineComponent, nullptr);
            }

            entity->Init();
            entity->Activate();
            return entity;
        }

        // An entity with the force region component and a trigger, left inactive so the test chooses when it activates.
        AZStd::unique_ptr<AZ::Entity> CreateForceRegionComponentEntity()
        {
            AZStd::unique_ptr<AZ::Entity> entity(aznew AZ::Entity("FlipForceRegionEntity"));

            AZ::TransformConfig transformConfig;
            transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, 12.0f));
            entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

            auto colliderConfiguration = AZStd::make_shared<Physics::ColliderConfiguration>();
            colliderConfiguration->m_isTrigger = true;
            auto shapeConfiguration = AZStd::make_shared<BoxColliderComponent::Configuration>();
            entity->CreateComponent<BoxColliderComponent>()->SetShapeConfigurationList({ AZStd::make_pair(colliderConfiguration, shapeConfiguration) });
            entity->CreateComponent<StaticRigidBodyComponent>();
            entity->CreateComponent<ForceRegionComponent>();

            entity->Init();
            return entity;
        }

        // A body in the region. The mass is 2, so a force that forgets to scale by it shows.
        EntityParams CreateBody(const AZ::Vector3& position, const AZ::Vector3& velocity)
        {
            EntityParams body;
            body.m_id.SetInvalid();
            body.m_position = position;
            body.m_velocity = velocity;
            body.m_aabb = AZ::Aabb::CreateCenterRadius(position, 0.5f);
            body.m_mass = 2.0f;
            return body;
        }

        void AddLocalSpaceForce(AZ::EntityId entityId, const AZ::Vector3& direction, float magnitude)
        {
            ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::AddForceLocalSpace, direction, magnitude);
        }

        void SetFlipMask(AZ::EntityId entityId, AZ::u8 flipMask)
        {
            ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::SetFlipMask, flipMask);
        }

        // What GetDirection reports for the local space force of the entity, which is the direction as drawn.
        AZ::Vector3 GetLocalDirection(AZ::EntityId entityId)
        {
            AZ::Vector3 direction = AZ::Vector3::CreateZero();
            ForceLocalSpaceRequestBus::EventResult(direction, entityId, &ForceLocalSpaceRequestBus::Events::GetDirection);
            return direction;
        }

        // Multiplying by minus one is exact, so what comes back from a flip is the very same float.
        void ExpectExactlyEqual(const AZ::Vector3& actual, const AZ::Vector3& expected)
        {
            EXPECT_EQ(actual.GetX(), expected.GetX());
            EXPECT_EQ(actual.GetY(), expected.GetY());
            EXPECT_EQ(actual.GetZ(), expected.GetZ());
        }
    } // namespace

    //////////////////////////////////////////////////////////////////////////
    // The footprint of the flip mask

#if defined(AZ_COMPILER_MSVC)
    // The mask sits in the 4 bytes of padding after m_scale, which the MSVC x64 build of the Transform2-2026 branch measured before
    // the mask was added. Other compilers lay the struct out differently, so the pin is only checked on MSVC.
    TEST(ForceRegionFlipFootprintTest, PrototypePin_RegionParamsKeepsItsSize)
    {
        EXPECT_EQ(sizeof(RegionParams), 112u);
    }
#endif

    //////////////////////////////////////////////////////////////////////////
    // Local space forces

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_LocalSpaceForceMaskX_PushesAlongTheMirroredDirection)
    {
        const AZ::Quaternion rotation = GeneralRotation();
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateFromQuaternionAndTranslation(rotation, AZ::Vector3(10.0f, 20.0f, 30.0f)), false);
        const AZ::EntityId entityId = entity->GetId();

        ForceRegion region;
        region.Activate(entityId);
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        const EntityParams body = CreateBody(AZ::Vector3(10.0f, 20.0f, 35.0f), AZ::Vector3::CreateZero());

        // Authored: the local +X of the region, in the world of the rotation, times magnitude and mass.
        EXPECT_THAT(
            region.CalculateNetForce(body),
            UnitTest::IsCloseTolerance(rotation.TransformVector(AZ::Vector3(1.0f, 0.0f, 0.0f)) * 20.0f, FlipTolerance));

        // Flipped across X it is the local -X. The rotation of the region is the T1 rotation, unchanged by the flip.
        SetFlipMask(entityId, FlipX);
        EXPECT_THAT(
            region.CalculateNetForce(body),
            UnitTest::IsCloseTolerance(rotation.TransformVector(AZ::Vector3(-1.0f, 0.0f, 0.0f)) * 20.0f, FlipTolerance));

        region.Deactivate();
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_TwoLocalSpaceForces_KeepTheirOwnDirections)
    {
        const AZ::Quaternion rotation = GeneralRotation();
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateFromQuaternionAndTranslation(rotation, AZ::Vector3(10.0f, 20.0f, 30.0f)), false);
        const AZ::EntityId entityId = entity->GetId();

        ForceRegion region;
        region.Activate(entityId);
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        AddLocalSpaceForce(entityId, AZ::Vector3(0.0f, 0.0f, 1.0f), 5.0f);
        const EntityParams body = CreateBody(AZ::Vector3(10.0f, 20.0f, 35.0f), AZ::Vector3::CreateZero());

        // Both forces are reached by the same bus address. A push of one value through that bus would have turned both of them
        // the same way, which is -15 along X here. Each force mirrors its own direction instead: +X flips, +Z does not.
        SetFlipMask(entityId, FlipX);
        EXPECT_THAT(
            region.CalculateNetForce(body),
            UnitTest::IsCloseTolerance(rotation.TransformVector(AZ::Vector3(-10.0f, 0.0f, 5.0f)) * 2.0f, FlipTolerance));

        // Z flips the second one and leaves the first as authored.
        SetFlipMask(entityId, FlipZ);
        EXPECT_THAT(
            region.CalculateNetForce(body),
            UnitTest::IsCloseTolerance(rotation.TransformVector(AZ::Vector3(10.0f, 0.0f, -5.0f)) * 2.0f, FlipTolerance));

        region.Deactivate();
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_FlipThenUnflip_ReturnsTheExactAuthoredDirection)
    {
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateIdentity(), false);
        const AZ::EntityId entityId = entity->GetId();

        ForceRegion region;
        region.Activate(entityId);
        const AZ::Vector3 authored(0.3f, -0.7f, 0.2f);
        AddLocalSpaceForce(entityId, authored, 10.0f);

        // A script that reads the direction of a flipped region sees the direction as drawn.
        SetFlipMask(entityId, FlipX);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-0.3f, -0.7f, 0.2f));

        // From one flip to another, the axes that changed are mirrored: X back, Z over.
        SetFlipMask(entityId, FlipZ);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(0.3f, -0.7f, -0.2f));

        SetFlipMask(entityId, FlipX | FlipY | FlipZ);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-0.3f, 0.7f, -0.2f));

        // Back to zero, and what was authored comes back bit for bit.
        SetFlipMask(entityId, 0);
        ExpectExactlyEqual(GetLocalDirection(entityId), authored);
        EXPECT_EQ(region.GetRegionParams().m_flipMask, 0);

        region.Deactivate();
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_ForceAddedToAFlippedRegion_ComesInAsDrawn)
    {
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateIdentity(), false);
        const AZ::EntityId entityId = entity->GetId();

        ForceRegion region;
        region.Activate(entityId);
        SetFlipMask(entityId, FlipX);

        // Every force of a flipped region is held as drawn, or turning back would leave this one mirrored.
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));

        SetFlipMask(entityId, 0);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(1.0f, 0.0f, 0.0f));

        region.Deactivate();
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_MaskZeroAndWorldTerms_LeaveTheForcesAsTheyWere)
    {
        const AZ::Quaternion rotation = GeneralRotation();
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateFromQuaternionAndTranslation(rotation, AZ::Vector3(10.0f, 20.0f, 30.0f)), false);
        const AZ::EntityId entityId = entity->GetId();

        ForceRegion region;
        region.Activate(entityId);
        ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::AddForceWorldSpace, AZ::Vector3(0.0f, 0.0f, 1.0f), 100.0f);
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::AddForceSimpleDrag, 1.0f, 5.0f);
        ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::AddForceLinearDamping, 10.0f);
        const EntityParams body = CreateBody(AZ::Vector3(10.0f, 20.0f, 35.0f), AZ::Vector3(1.0f, -2.0f, 0.5f));

        // A mask of zero on a region that was never flipped changes nothing, to the bit.
        const AZ::Vector3 authored = region.CalculateNetForce(body);
        SetFlipMask(entityId, 0);
        ExpectExactlyEqual(region.CalculateNetForce(body), authored);

        // A flip changes the local space force and nothing else: world space force, drag and damping are world terms.
        SetFlipMask(entityId, FlipX | FlipY | FlipZ);
        EXPECT_THAT(
            region.CalculateNetForce(body),
            UnitTest::IsCloseTolerance(authored + rotation.TransformVector(AZ::Vector3(-2.0f, 0.0f, 0.0f)) * 20.0f, FlipTolerance));

        region.Deactivate();
    }

    //////////////////////////////////////////////////////////////////////////
    // Spline follow

    class PhysXForceRegionFlipSplineTest
        : public ::testing::WithParamInterface<LmbrCentral::SplineType>
        , public PhysXForceRegionTest
    {
    };

    TEST_P(PhysXForceRegionFlipSplineTest, ForceRegionFlip_SplineFollowMaskZ_IsTheMirrorImageOfTheAuthoredRegion)
    {
        const AZ::Vector3 entityPosition(10.0f, 20.0f, 30.0f);
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateTranslation(entityPosition), true);
        const AZ::EntityId entityId = entity->GetId();

        // Six vertices, so that all three kinds of spline have a segment to follow.
        const AZStd::vector<AZ::Vector3> vertices =
        {
            AZ::Vector3(0.0f, 0.0f, 0.0f), AZ::Vector3(2.0f, 1.0f, 1.0f), AZ::Vector3(4.0f, 0.0f, 2.0f),
            AZ::Vector3(6.0f, -1.0f, 1.0f), AZ::Vector3(8.0f, 0.0f, 0.0f), AZ::Vector3(10.0f, 1.0f, -1.0f)
        };
        LmbrCentral::SplineComponentRequestBus::Event(entityId, &LmbrCentral::SplineComponentRequestBus::Events::ChangeSplineType, GetParam());
        LmbrCentral::SplineComponentRequestBus::Event(entityId, &LmbrCentral::SplineComponentRequestBus::Events::SetVertices, vertices);
        AZ::SplinePtr authoredSpline;
        LmbrCentral::SplineComponentRequestBus::EventResult(authoredSpline, entityId, &LmbrCentral::SplineComponentRequestBus::Events::GetSpline);
        ASSERT_TRUE(authoredSpline != nullptr);

        ForceRegion region;
        region.Activate(entityId);
        ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::AddForceSplineFollow, 1.0f, 1.0f, 2.0f, 0.0f);

        // The mirror about the Z plane of the entity: a body at p with velocity v in the flipped region feels M F(M p, M v),
        // with F the force of the authored region and M the mirror. The position, the target and the tangent all follow.
        const AZ::Vector3 mirrorSigns(1.0f, 1.0f, -1.0f);
        const AZ::Vector3 localPosition(4.2f, -0.5f, 2.5f);
        const AZ::Vector3 velocity(0.5f, 0.25f, -1.0f);
        const EntityParams body = CreateBody(entityPosition + localPosition, velocity);
        const EntityParams mirroredBody = CreateBody(entityPosition + localPosition * mirrorSigns, velocity * mirrorSigns);

        const AZ::Vector3 authoredForce = region.CalculateNetForce(body);
        const AZ::Vector3 authoredForceAtMirror = region.CalculateNetForce(mirroredBody);

        SetFlipMask(entityId, FlipZ);
        const AZ::Vector3 flippedForce = region.CalculateNetForce(body);
        EXPECT_THAT(flippedForce, UnitTest::IsCloseTolerance(authoredForceAtMirror * mirrorSigns, 1e-2f));
        EXPECT_GT((flippedForce - authoredForce).GetLength(), 1.0f); // the flip made a difference, so the check above is not empty

        // The region follows a clone of its own type with mirrored vertices. The spline of the Spline component is as it was.
        const AZ::SplinePtr followedSpline = region.GetRegionParams().m_spline;
        ASSERT_TRUE(followedSpline != nullptr);
        EXPECT_NE(followedSpline.get(), authoredSpline.get());
        EXPECT_TRUE(followedSpline->RTTI_GetType() == authoredSpline->RTTI_GetType());
        ASSERT_EQ(followedSpline->GetVertexCount(), vertices.size());
        ASSERT_EQ(authoredSpline->GetVertexCount(), vertices.size());
        for (size_t index = 0; index < vertices.size(); ++index)
        {
            EXPECT_THAT(followedSpline->GetVertex(index), UnitTest::IsCloseTolerance(vertices[index] * mirrorSigns, 1e-6f));
            EXPECT_THAT(authoredSpline->GetVertex(index), UnitTest::IsCloseTolerance(vertices[index], 1e-6f));
        }

        // Turning back takes the authored spline again, not a second clone.
        SetFlipMask(entityId, 0);
        EXPECT_EQ(region.GetRegionParams().m_spline.get(), authoredSpline.get());
        ExpectExactlyEqual(region.CalculateNetForce(body), authoredForce);

        region.Deactivate();
    }

    INSTANTIATE_TEST_SUITE_P(
        PhysXForceRegionFlip,
        PhysXForceRegionFlipSplineTest,
        ::testing::Values(LmbrCentral::SplineType::LINEAR, LmbrCentral::SplineType::BEZIER, LmbrCentral::SplineType::CATMULL_ROM));

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_SplineChangedWhileFlipped_IsFollowedMirrored)
    {
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateIdentity(), true);
        const AZ::EntityId entityId = entity->GetId();
        const AZStd::vector<AZ::Vector3> vertices = { AZ::Vector3(0.0f, 0.0f, 1.0f), AZ::Vector3(4.0f, 0.0f, 1.0f) };
        LmbrCentral::SplineComponentRequestBus::Event(entityId, &LmbrCentral::SplineComponentRequestBus::Events::SetVertices, vertices);

        ForceRegion region;
        region.Activate(entityId);
        SetFlipMask(entityId, FlipX | FlipZ);

        // The vertices of the shared spline change while the region is flipped. The notification makes the clone again.
        const AZStd::vector<AZ::Vector3> movedVertices = { AZ::Vector3(1.0f, 2.0f, 3.0f), AZ::Vector3(5.0f, 6.0f, 7.0f), AZ::Vector3(9.0f, 1.0f, 2.0f) };
        LmbrCentral::SplineComponentRequestBus::Event(entityId, &LmbrCentral::SplineComponentRequestBus::Events::SetVertices, movedVertices);

        AZ::SplinePtr authoredSpline;
        LmbrCentral::SplineComponentRequestBus::EventResult(authoredSpline, entityId, &LmbrCentral::SplineComponentRequestBus::Events::GetSpline);
        const AZ::SplinePtr followedSpline = region.GetRegionParams().m_spline;
        ASSERT_TRUE(followedSpline != nullptr);
        ASSERT_TRUE(authoredSpline != nullptr);
        ASSERT_EQ(followedSpline->GetVertexCount(), movedVertices.size());
        ASSERT_EQ(authoredSpline->GetVertexCount(), movedVertices.size());
        for (size_t index = 0; index < movedVertices.size(); ++index)
        {
            EXPECT_THAT(followedSpline->GetVertex(index), UnitTest::IsCloseTolerance(movedVertices[index] * AZ::Vector3(-1.0f, 1.0f, -1.0f), 1e-6f));
            EXPECT_THAT(authoredSpline->GetVertex(index), UnitTest::IsCloseTolerance(movedVertices[index], 1e-6f));
        }

        region.Deactivate();
    }

    //////////////////////////////////////////////////////////////////////////
    // The region's life cycle

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_ColliderRebuild_KeepsTheMirror)
    {
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateIdentity(), true);
        const AZ::EntityId entityId = entity->GetId();
        const AZStd::vector<AZ::Vector3> vertices = { AZ::Vector3(0.0f, 0.0f, 1.0f), AZ::Vector3(4.0f, 2.0f, 1.0f) };
        LmbrCentral::SplineComponentRequestBus::Event(entityId, &LmbrCentral::SplineComponentRequestBus::Events::SetVertices, vertices);

        ForceRegion region;
        region.Activate(entityId);
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        SetFlipMask(entityId, FlipX);
        const AZ::SplinePtr followedBefore = region.GetRegionParams().m_spline;

        // A collider that changes makes the region build its parameters again, from the authored spline and with no flip.
        Physics::ColliderComponentEventBus::Event(entityId, &Physics::ColliderComponentEvents::OnColliderChanged);

        const RegionParams rebuilt = region.GetRegionParams();
        EXPECT_EQ(rebuilt.m_flipMask, FlipX);
        ASSERT_TRUE(rebuilt.m_spline != nullptr);
        EXPECT_EQ(rebuilt.m_spline.get(), followedBefore.get());
        EXPECT_THAT(rebuilt.m_spline->GetVertex(1), UnitTest::IsCloseTolerance(AZ::Vector3(-4.0f, 2.0f, 1.0f), 1e-6f));

        // The forces were not rebuilt, so they are still as drawn, and turning back is still exact.
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));
        SetFlipMask(entityId, 0);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(1.0f, 0.0f, 0.0f));

        region.Deactivate();
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_DeactivateThenActivate_RestoresTheAuthoredValuesAndANewPushMirrorsAgain)
    {
        const AZ::Quaternion rotation = GeneralRotation();
        auto entity = CreateFlipRegionEntity(AZ::Transform::CreateFromQuaternionAndTranslation(rotation, AZ::Vector3(10.0f, 20.0f, 30.0f)), false);
        const AZ::EntityId entityId = entity->GetId();

        ForceRegion region;
        region.Activate(entityId);
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        const EntityParams body = CreateBody(AZ::Vector3(10.0f, 20.0f, 35.0f), AZ::Vector3::CreateZero());
        const AZ::Vector3 authored = region.CalculateNetForce(body);

        SetFlipMask(entityId, FlipX);
        EXPECT_THAT(region.CalculateNetForce(body), UnitTest::IsCloseTolerance(authored * -1.0f, FlipTolerance));

        // The forces live on in the region, so deactivating it undoes the mirror, and the mask is gone with it.
        region.Deactivate();
        EXPECT_EQ(region.GetRegionParams().m_flipMask, 0);
        ExpectExactlyEqual(region.CalculateNetForce(body), authored);

        // Activated again it is as authored, and the next push mirrors it once more, neither twice nor not at all.
        region.Activate(entityId);
        ExpectExactlyEqual(region.CalculateNetForce(body), authored);
        SetFlipMask(entityId, FlipX);
        EXPECT_THAT(region.CalculateNetForce(body), UnitTest::IsCloseTolerance(authored * -1.0f, FlipTolerance));

        region.Deactivate();
    }

    //////////////////////////////////////////////////////////////////////////
    // The Transform 2 listener, with the force region component

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_TransformAnnouncedBeforeTheRegionActivates_FlipsTheRegion)
    {
        auto entity = CreateForceRegionComponentEntity();
        const AZ::EntityId entityId = entity->GetId();

        // The Transform 2 announces itself while the entity activates, and the region may activate after it. The listener
        // waits until the entity is done, and then pushes.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Activate();

        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_ScaleChanges_FlipAndUnflipTheRegion)
    {
        auto entity = CreateForceRegionComponentEntity();
        const AZ::EntityId entityId = entity->GetId();
        entity->Activate();
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));

        // A change of size without a change of sign flips nothing again.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-3.0f, 1.0f, 1.0f));
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(1.0f, 0.0f, 0.0f));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(1.0f, 0.0f, 0.0f)); // +X is symmetric to a Z flip

        // An entity that lives on after leaving Transform 2 gets its authored forces back, with no scale change before it.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(1.0f, 0.0f, 0.0f));
    }

    TEST_F(PhysXForceRegionTest, ForceRegionFlip_EntityActivatesAgainUnderTheSameMask_FlipsTheRegionAgain)
    {
        auto entity = CreateForceRegionComponentEntity();
        const AZ::EntityId entityId = entity->GetId();
        entity->Activate();
        AddLocalSpaceForce(entityId, AZ::Vector3(1.0f, 0.0f, 0.0f), 10.0f);

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));

        // The region undoes its mirror when it deactivates, and nothing tells the listener, which still holds the same mask for
        // the entity. Its change test alone would then leave the region unflipped after the entity comes back.
        entity->Deactivate();
        entity->Activate();
        ExpectExactlyEqual(GetLocalDirection(entityId), AZ::Vector3(-1.0f, 0.0f, 0.0f));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
    }
}

