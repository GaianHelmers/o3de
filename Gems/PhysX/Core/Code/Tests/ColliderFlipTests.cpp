/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PhysXTestFixtures.h"

#include <AzTest/AzTest.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/TransformBus.h>
#include <AZTestShared/Math/MathTestHelpers.h>

#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Physics/ColliderComponentBus.h>
#include <AzFramework/Physics/Configuration/RigidBodyConfiguration.h>
#include <AzFramework/Physics/Configuration/StaticRigidBodyConfiguration.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Physics/ShapeConfiguration.h>
#include <AzFramework/Physics/SimulatedBodies/RigidBody.h>
#include <AzFramework/Physics/SimulatedBodies/StaticRigidBody.h>
#include <AzFramework/Physics/SystemBus.h>

#include <PhysX/ColliderComponentBus.h>
#include <PhysX/ColliderShapeBus.h>
#include <PhysX/PhysXLocks.h>
#include <Source/BoxColliderComponent.h>
#include <Source/ForceRegion.h>
#include <Source/RigidBodyComponent.h>
#include <Source/Shape.h>
#include <Source/StaticRigidBodyComponent.h>
#include <Source/Transform2ColliderSupport.h>
#include <Tests/PhysXTestCommon.h>

namespace PhysX
{
    //! Counts how often the collider of an entity is announced as changed.
    class ColliderChangedCounter
        : public Physics::ColliderComponentEventBus::Handler
    {
    public:
        explicit ColliderChangedCounter(AZ::EntityId entityId)
        {
            Physics::ColliderComponentEventBus::Handler::BusConnect(entityId);
        }

        ~ColliderChangedCounter() override
        {
            Physics::ColliderComponentEventBus::Handler::BusDisconnect();
        }

        void OnColliderChanged() override
        {
            ++m_count;
        }

        int m_count = 0;
    };

    class ColliderFlipTest
        : public PhysXDefaultWorldTest
    {
    protected:
        static constexpr float Tolerance = 1e-3f;

        // A rotation with all three vector components set, so a wrong sign on any of them shows.
        static AZ::Quaternion GeneralRotation()
        {
            return AZ::Quaternion::CreateRotationZ(0.7f) * AZ::Quaternion::CreateRotationY(0.5f) * AZ::Quaternion::CreateRotationX(0.3f);
        }

        AZStd::shared_ptr<Physics::Shape> CreateBoxShape(const AZ::Vector3& position, const AZ::Quaternion& rotation)
        {
            Physics::ColliderConfiguration colliderConfig;
            colliderConfig.m_position = position;
            colliderConfig.m_rotation = rotation;
            const Physics::BoxShapeConfiguration shapeConfig(AZ::Vector3(1.0f, 2.0f, 3.0f));
            return AZ::Interface<Physics::System>::Get()->CreateShape(colliderConfig, shapeConfig);
        }

        AZStd::shared_ptr<Physics::Shape> CreateCapsuleShape(const AZ::Vector3& position, const AZ::Quaternion& rotation)
        {
            Physics::ColliderConfiguration colliderConfig;
            colliderConfig.m_position = position;
            colliderConfig.m_rotation = rotation;
            const Physics::CapsuleShapeConfiguration shapeConfig(2.0f, 0.5f);
            return AZ::Interface<Physics::System>::Get()->CreateShape(colliderConfig, shapeConfig);
        }

        AzPhysics::StaticRigidBody* AddStaticBody(const AZStd::shared_ptr<Physics::Shape>& shape, const AZ::Vector3& position)
        {
            AzPhysics::StaticRigidBodyConfiguration configuration;
            configuration.m_position = position;
            configuration.m_colliderAndShapeData = shape;
            auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
            AzPhysics::SimulatedBodyHandle handle = sceneInterface->AddSimulatedBody(m_testSceneHandle, &configuration);
            return azdynamic_cast<AzPhysics::StaticRigidBody*>(sceneInterface->GetSimulatedBodyFromHandle(m_testSceneHandle, handle));
        }

        // The pose reported by the shape must be the mirrored authored pose, whatever the pose of the first flip was.
        static void ExpectPose(
            const AZStd::shared_ptr<Physics::Shape>& shape, const AZ::Vector3& expectedPosition, const AZ::Quaternion& expectedRotation)
        {
            const AZStd::pair<AZ::Vector3, AZ::Quaternion> pose = shape->GetLocalPose();
            EXPECT_THAT(pose.first, UnitTest::IsCloseTolerance(expectedPosition, Tolerance));
            // A quaternion and its negative are the same rotation.
            EXPECT_NEAR(AZStd::abs(pose.second.Dot(expectedRotation)), 1.0f, Tolerance);
        }

        static physx::PxShape* GetPxShape(const AZStd::shared_ptr<Physics::Shape>& shape)
        {
            return static_cast<physx::PxShape*>(shape->GetNativePointer());
        }

        static physx::PxScene* GetPxScene(const physx::PxShape* pxShape)
        {
            return pxShape->getActor() ? pxShape->getActor()->getScene() : nullptr;
        }

        static physx::PxVec3 GetTriangleMeshScale(const AZStd::shared_ptr<Physics::Shape>& shape)
        {
            physx::PxShape* pxShape = GetPxShape(shape);
            PHYSX_SCENE_READ_LOCK(GetPxScene(pxShape));
            physx::PxTriangleMeshGeometry geometry;
            EXPECT_TRUE(pxShape->getTriangleMeshGeometry(geometry));
            return geometry.scale.scale;
        }

        static physx::PxConvexMesh* GetConvexMesh(const AZStd::shared_ptr<Physics::Shape>& shape)
        {
            physx::PxShape* pxShape = GetPxShape(shape);
            PHYSX_SCENE_READ_LOCK(GetPxScene(pxShape));
            physx::PxConvexMeshGeometry geometry;
            EXPECT_TRUE(pxShape->getConvexMeshGeometry(geometry));
            return geometry.convexMesh;
        }

        static void GetHeightRange(const AZStd::vector<AZ::Vector3>& vertices, float& minZ, float& maxZ)
        {
            minZ = vertices[0].GetZ();
            maxZ = vertices[0].GetZ();
            for (const AZ::Vector3& vertex : vertices)
            {
                minZ = AZStd::min(minZ, vertex.GetZ());
                maxZ = AZStd::max(maxZ, vertex.GetZ());
            }
        }

        AzPhysics::RigidBody* GetRigidBody(AZ::EntityId entityId)
        {
            AzPhysics::RigidBody* body = nullptr;
            Physics::RigidBodyRequestBus::EventResult(body, entityId, &Physics::RigidBodyRequests::GetRigidBody);
            return body;
        }

        // An entity with a box collider offset from its origin. It is left uninitialized, so the caller adds a body to it.
        EntityPtr CreateBoxColliderEntity(const AZ::Vector3& position, const AZ::Vector3& shapeOffset, bool isTrigger)
        {
            EntityPtr entity = AZStd::make_shared<AZ::Entity>("FlipBoxEntity");

            AZ::TransformConfig transformConfig;
            transformConfig.m_worldTransform = AZ::Transform::CreateTranslation(position);
            entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(transformConfig);

            auto colliderConfig = AZStd::make_shared<Physics::ColliderConfiguration>();
            colliderConfig->m_position = shapeOffset;
            colliderConfig->m_isTrigger = isTrigger;
            auto shapeConfig = AZStd::make_shared<Physics::BoxShapeConfiguration>();
            entity->CreateComponent<BoxColliderComponent>()->SetShapeConfigurationList({ AZStd::make_pair(colliderConfig, shapeConfig) });
            return entity;
        }

        void AddRigidBody(const EntityPtr& entity, bool startAsleep)
        {
            AzPhysics::RigidBodyConfiguration rigidBodyConfig;
            rigidBodyConfig.m_computeMass = false;
            rigidBodyConfig.m_startAsleep = startAsleep;
            entity->CreateComponent<RigidBodyComponent>(rigidBodyConfig, m_testSceneHandle);
        }

        static AZ::Aabb GetColliderAabb(AZ::EntityId entityId)
        {
            AZ::Aabb aabb = AZ::Aabb::CreateNull();
            ColliderShapeRequestBus::EventResult(aabb, entityId, &ColliderShapeRequests::GetColliderShapeAabb);
            return aabb;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Mirror math

    TEST_F(ColliderFlipTest, GetSigns_ResidualAndSign_KeepsOnlyTheSign)
    {
        // Magnitude plays no part: (-2, 1, 0.5) is a flip of X alone, whatever the stand-in and the residual are.
        EXPECT_THAT(Transform2Mirror::GetSigns(AZ::Vector3(-2.0f, 1.0f, 0.5f)), UnitTest::IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_THAT(Transform2Mirror::GetSigns(AZ::Vector3(3.0f, 0.5f, -0.25f)), UnitTest::IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f)));
        EXPECT_THAT(Transform2Mirror::GetSigns(AZ::Vector3(-1.0f, 2.0f, -1.0f)), UnitTest::IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
        // A collapsed axis is not a flip.
        EXPECT_THAT(Transform2Mirror::GetSigns(AZ::Vector3(0.0f, 1.0f, 1.0f)), UnitTest::IsClose(AZ::Vector3(1.0f, 1.0f, 1.0f)));
    }

    TEST_F(ColliderFlipTest, GetSignMask_NegativeAxes_SetsOneBitEach)
    {
        EXPECT_EQ(Transform2Mirror::GetSignMask(AZ::Vector3(1.0f, 1.0f, 1.0f)), 0u);
        EXPECT_EQ(Transform2Mirror::GetSignMask(AZ::Vector3(-1.0f, 1.0f, 1.0f)), 1u);
        EXPECT_EQ(Transform2Mirror::GetSignMask(AZ::Vector3(1.0f, -1.0f, 1.0f)), 2u);
        EXPECT_EQ(Transform2Mirror::GetSignMask(AZ::Vector3(1.0f, 1.0f, -1.0f)), 4u);
        EXPECT_EQ(Transform2Mirror::GetSignMask(AZ::Vector3(-1.0f, 1.0f, -1.0f)), 5u);
    }

    TEST_F(ColliderFlipTest, MirrorRotation_EverySignPattern_IsTheReflectionOfTheRotation)
    {
        // R' = M R M, so R' v must equal M R M v for any v. This checks the quaternion rule against the definition.
        const AZ::Quaternion rotation = GeneralRotation();
        const AZ::Vector3 vector(0.3f, -1.2f, 2.1f);

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 signs((mask & 1) ? -1.0f : 1.0f, (mask & 2) ? -1.0f : 1.0f, (mask & 4) ? -1.0f : 1.0f);
            const AZ::Vector3 expected = signs * rotation.TransformVector(signs * vector);
            const AZ::Vector3 actual = Transform2Mirror::MirrorRotation(signs, rotation).TransformVector(vector);
            EXPECT_THAT(actual, UnitTest::IsCloseTolerance(expected, Tolerance)) << "sign mask " << mask;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Primitives

    TEST_F(ColliderFlipTest, Primitive_FlipX_MirrorsOffsetAndRotation)
    {
        AZStd::shared_ptr<Physics::Shape> shape = CreateBoxShape(AZ::Vector3(1.0f, 2.0f, 3.0f), GeneralRotation());
        const AZStd::pair<AZ::Vector3, AZ::Quaternion> authored = shape->GetLocalPose();

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, 1.0f));

        // X flip: (qx, -qy, -qz, qw)
        ExpectPose(
            shape,
            AZ::Vector3(-authored.first.GetX(), authored.first.GetY(), authored.first.GetZ()),
            AZ::Quaternion(authored.second.GetX(), -authored.second.GetY(), -authored.second.GetZ(), authored.second.GetW()));
    }

    TEST_F(ColliderFlipTest, Primitive_FlipZ_MirrorsOffsetAndRotation)
    {
        AZStd::shared_ptr<Physics::Shape> shape = CreateBoxShape(AZ::Vector3(1.0f, 2.0f, 3.0f), GeneralRotation());
        const AZStd::pair<AZ::Vector3, AZ::Quaternion> authored = shape->GetLocalPose();

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(1.0f, 1.0f, -1.0f));

        // Z flip: (-qx, -qy, qz, qw)
        ExpectPose(
            shape,
            AZ::Vector3(authored.first.GetX(), authored.first.GetY(), -authored.first.GetZ()),
            AZ::Quaternion(-authored.second.GetX(), -authored.second.GetY(), authored.second.GetZ(), authored.second.GetW()));
    }

    TEST_F(ColliderFlipTest, Primitive_FlipXAndZ_MirrorsOffsetAndRotation)
    {
        AZStd::shared_ptr<Physics::Shape> shape = CreateBoxShape(AZ::Vector3(1.0f, 2.0f, 3.0f), GeneralRotation());
        const AZStd::pair<AZ::Vector3, AZ::Quaternion> authored = shape->GetLocalPose();

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, -1.0f));

        // Both flipped is a 180 degree turn about Y, which conjugates the rotation to (-qx, qy, -qz, qw).
        ExpectPose(
            shape,
            AZ::Vector3(-authored.first.GetX(), authored.first.GetY(), -authored.first.GetZ()),
            AZ::Quaternion(-authored.second.GetX(), authored.second.GetY(), -authored.second.GetZ(), authored.second.GetW()));
    }

    TEST_F(ColliderFlipTest, Capsule_FlipX_MirrorsOffsetAndRotationInTheFrameOfTheEntity)
    {
        // The PhysX shape adds a rotation to lay a capsule along Z and strips it on get. The flip works in the stripped frame.
        AZStd::shared_ptr<Physics::Shape> shape = CreateCapsuleShape(AZ::Vector3(1.0f, 2.0f, 3.0f), GeneralRotation());
        const AZStd::pair<AZ::Vector3, AZ::Quaternion> authored = shape->GetLocalPose();

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, 1.0f));

        ExpectPose(
            shape,
            AZ::Vector3(-authored.first.GetX(), authored.first.GetY(), authored.first.GetZ()),
            AZ::Quaternion(authored.second.GetX(), -authored.second.GetY(), -authored.second.GetZ(), authored.second.GetW()));
    }

    TEST_F(ColliderFlipTest, Primitive_FlipTwiceOrBack_ReturnsTheAuthoredPose)
    {
        AZStd::shared_ptr<Physics::Shape> shape = CreateBoxShape(AZ::Vector3(1.0f, 2.0f, 3.0f), GeneralRotation());
        const AZStd::pair<AZ::Vector3, AZ::Quaternion> authored = shape->GetLocalPose();

        Transform2ShapeFlip flip(shape);

        // Flipping the same way again derives from the authored pose, so it does not flip back.
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectPose(
            shape,
            AZ::Vector3(-authored.first.GetX(), authored.first.GetY(), authored.first.GetZ()),
            AZ::Quaternion(authored.second.GetX(), -authored.second.GetY(), -authored.second.GetZ(), authored.second.GetW()));

        // Flipping through the other states and back to no flip lands on the authored pose.
        flip.Apply(AZ::Vector3(1.0f, 1.0f, -1.0f));
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, -1.0f));
        flip.Apply(AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectPose(shape, authored.first, authored.second);
    }

    TEST_F(ColliderFlipTest, Primitive_Flip_KeepsTheGeometry)
    {
        AZStd::shared_ptr<Physics::Shape> shape = CreateBoxShape(AZ::Vector3(1.0f, 2.0f, 3.0f), GeneralRotation());
        const AZ::Aabb boundsBefore = shape->GetAabbLocal();

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, -1.0f));

        // A box is symmetric across its own axes, so the extents stay the same and only the position moves.
        const AZ::Aabb boundsAfter = shape->GetAabbLocal();
        EXPECT_THAT(boundsAfter.GetExtents(), UnitTest::IsCloseTolerance(boundsBefore.GetExtents(), Tolerance));
    }

    //////////////////////////////////////////////////////////////////////////
    // Bodies

    TEST_F(ColliderFlipTest, StaticBody_FlipX_MirrorsTheShapeAndLeavesTheBodyWhereItIs)
    {
        AZStd::shared_ptr<Physics::Shape> shape = CreateBoxShape(AZ::Vector3(2.0f, 0.0f, 1.0f), AZ::Quaternion::CreateIdentity());
        AzPhysics::StaticRigidBody* body = AddStaticBody(shape, AZ::Vector3(5.0f, 6.0f, 7.0f));
        ASSERT_NE(body, nullptr);
        const AZ::Vector3 positionBefore = body->GetPosition();
        const AZ::Quaternion orientationBefore = body->GetOrientation();

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, 1.0f));

        ExpectPose(shape, AZ::Vector3(-2.0f, 0.0f, 1.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_THAT(body->GetPosition(), UnitTest::IsCloseTolerance(positionBefore, Tolerance));
        EXPECT_THAT(body->GetOrientation(), UnitTest::IsCloseTolerance(orientationBefore, Tolerance));

        flip.Apply(AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectPose(shape, AZ::Vector3(2.0f, 0.0f, 1.0f), AZ::Quaternion::CreateIdentity());
    }

    TEST_F(ColliderFlipTest, Entity_FlipNotifications_MirrorCollidersAndMassWithoutMovingTheEntity)
    {
        EntityPtr entity = TestUtils::AddUnitTestObject<BoxColliderComponent>(m_testSceneHandle, AZ::Vector3(10.0f, 20.0f, 30.0f));
        const AZ::EntityId entityId = entity->GetId();

        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
        ColliderComponentRequestBus::EventResult(shapes, entityId, &ColliderComponentRequests::GetShapes);
        ASSERT_EQ(shapes.size(), 1u);

        // Author an offset after the body was made, so the mass properties still describe a centered collider.
        shapes[0]->SetLocalPose(AZ::Vector3(2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());

        AzPhysics::RigidBody* body = GetRigidBody(entityId);
        ASSERT_NE(body, nullptr);
        const AZ::Transform worldBefore = entity->GetTransform()->GetWorldTM();
        const AZ::Vector3 bodyPositionBefore = body->GetPosition();
        const AZ::Quaternion bodyOrientationBefore = body->GetOrientation();

        // A scale with both a residual and a sign: only the sign of X reaches the colliders.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-2.0f, 1.0f, 0.5f));

        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), -2.0f, Tolerance);

        // The flip never moves or turns the entity or its body.
        EXPECT_THAT(entity->GetTransform()->GetWorldTM().GetTranslation(), UnitTest::IsCloseTolerance(worldBefore.GetTranslation(), Tolerance));
        EXPECT_THAT(entity->GetTransform()->GetWorldTM().GetRotation(), UnitTest::IsCloseTolerance(worldBefore.GetRotation(), Tolerance));
        EXPECT_THAT(body->GetPosition(), UnitTest::IsCloseTolerance(bodyPositionBefore, Tolerance));
        EXPECT_THAT(body->GetOrientation(), UnitTest::IsCloseTolerance(bodyOrientationBefore, Tolerance));

        // A change of size without a change of sign leaves the flip as it is.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-4.0f, 1.0f, 0.5f));
        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());

        // Turning back around lands on the authored collider.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(2.0f, 1.0f, 0.5f));
        ExpectPose(shapes[0], AZ::Vector3(2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), 2.0f, Tolerance);

        // After deactivation all state is gone, so a late notification does nothing.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectPose(shapes[0], AZ::Vector3(2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
    }

    TEST_F(ColliderFlipTest, Entity_Deactivated_PutsTheAuthoredCollidersBack)
    {
        EntityPtr entity = TestUtils::AddUnitTestObject<BoxColliderComponent>(m_testSceneHandle, AZ::Vector3(0.0f, 0.0f, 10.0f));
        const AZ::EntityId entityId = entity->GetId();

        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
        ColliderComponentRequestBus::EventResult(shapes, entityId, &ColliderComponentRequests::GetShapes);
        ASSERT_EQ(shapes.size(), 1u);
        shapes[0]->SetLocalPose(AZ::Vector3(0.0f, 0.0f, 3.0f), AZ::Quaternion::CreateIdentity());

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
        ExpectPose(shapes[0], AZ::Vector3(0.0f, 0.0f, -3.0f), AZ::Quaternion::CreateIdentity());

        // An entity that lives on after leaving Transform 2 has its authored collider back.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
        ExpectPose(shapes[0], AZ::Vector3(0.0f, 0.0f, 3.0f), AZ::Quaternion::CreateIdentity());
    }

    TEST_F(ColliderFlipTest, Entity_Flip_MirrorsAnAuthoredCenterOfMassInsteadOfComputingOne)
    {
        EntityPtr entity = TestUtils::AddUnitTestObject<BoxColliderComponent>(m_testSceneHandle, AZ::Vector3(0.0f, 0.0f, 10.0f));
        const AZ::EntityId entityId = entity->GetId();

        // The body is configured with a center of mass of its own, off to the +X side of a box that is centered.
        AzPhysics::RigidBodyConfiguration& configuration = entity->FindComponent<RigidBodyComponent>()->GetConfiguration();
        configuration.m_computeCenterOfMass = false;
        configuration.m_centerOfMassOffset = AZ::Vector3(2.0f, 0.0f, 0.0f);

        AzPhysics::RigidBody* body = GetRigidBody(entityId);
        ASSERT_NE(body, nullptr);
        const float massBefore = body->GetMass();

        // Computing the center of mass again from the shapes would land on zero. The authored one mirrors instead.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), -2.0f, Tolerance);
        EXPECT_NEAR(body->GetMass(), massBefore, Tolerance);

        // Turning back around restores the authored value.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), 2.0f, Tolerance);
        EXPECT_NEAR(body->GetMass(), massBefore, Tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Bodies that start asleep

    TEST_F(ColliderFlipTest, Entity_StartAsleepBody_StaysAsleepThroughTheFlipAtActivationAndWakesOnALiveFlip)
    {
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(0.0f, 0.0f, 10.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), false);
        AddRigidBody(entity, true);
        entity->Init();
        entity->Activate();
        const AZ::EntityId entityId = entity->GetId();

        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
        ColliderComponentRequestBus::EventResult(shapes, entityId, &ColliderComponentRequests::GetShapes);
        ASSERT_EQ(shapes.size(), 1u);
        AzPhysics::RigidBody* body = GetRigidBody(entityId);
        ASSERT_NE(body, nullptr);
        ASSERT_FALSE(body->IsAwake());

        // The flip runs, and the body that PhysX put to sleep when it was added is left asleep.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_FALSE(body->IsAwake());

        // A flip during play moves a collider under a resting body, so the body wakes.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectPose(shapes[0], AZ::Vector3(2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_TRUE(body->IsAwake());
    }

    TEST_F(ColliderFlipTest, Entity_StartAsleepBodyMadeAfterTheFlip_StaysAsleep)
    {
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(0.0f, 0.0f, 10.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), false);
        AddRigidBody(entity, true);
        entity->Init();
        const AZ::EntityId entityId = entity->GetId();

        // Announced before the entity activates, so the colliders are flipped as the entity finishes activating. Whether the
        // body is made before or after that, it comes out flipped, and asleep.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Activate();

        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
        ColliderComponentRequestBus::EventResult(shapes, entityId, &ColliderComponentRequests::GetShapes);
        ASSERT_EQ(shapes.size(), 1u);
        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());

        AzPhysics::RigidBody* body = GetRigidBody(entityId);
        ASSERT_NE(body, nullptr);
        EXPECT_FALSE(body->IsAwake());
    }

    TEST_F(ColliderFlipTest, Entity_PhysicsDisabledAndEnabledAgain_KeepsTheFlippedColliderAndMass)
    {
        // Disabling and enabling the physics of a rigid body only switches its simulation. The body is not removed or made again,
        // so the shapes keep their capture and their flip, and the mass properties are mirrored again as the physics comes back.
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(0.0f, 0.0f, 10.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), false);
        AddRigidBody(entity, false);
        entity->Init();
        const AZ::EntityId entityId = entity->GetId();

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Activate();

        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
        ColliderComponentRequestBus::EventResult(shapes, entityId, &ColliderComponentRequests::GetShapes);
        ASSERT_EQ(shapes.size(), 1u);
        AzPhysics::RigidBody* body = GetRigidBody(entityId);
        ASSERT_NE(body, nullptr);
        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), -2.0f, Tolerance);

        Physics::RigidBodyRequestBus::Event(entityId, &Physics::RigidBodyRequests::DisablePhysics);
        Physics::RigidBodyRequestBus::Event(entityId, &Physics::RigidBodyRequests::EnablePhysics);
        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), -2.0f, Tolerance);

        // Turning back around still reaches the shape, so it was never forgotten.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectPose(shapes[0], AZ::Vector3(2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());
        EXPECT_NEAR(body->GetCenterOfMassLocal().GetX(), 2.0f, Tolerance);

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
    }

    //////////////////////////////////////////////////////////////////////////
    // Watchers of the collider

    TEST_F(ColliderFlipTest, Entity_Flip_RefreshesTheColliderAabbAndAnnouncesTheChangeOnce)
    {
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), false);
        AddRigidBody(entity, false);
        entity->Init();
        entity->Activate();
        const AZ::EntityId entityId = entity->GetId();

        // Reading the AABB fills its cache. The collider sits 2 to the +X side of the entity.
        EXPECT_NEAR(GetColliderAabb(entityId).GetCenter().GetX(), 12.0f, Tolerance);

        // A pure flip fires no transform event, yet the AABB moves to the -X side and the change is announced once.
        ColliderChangedCounter counter(entityId);
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        const AZ::Aabb flippedAabb = GetColliderAabb(entityId);
        EXPECT_THAT(flippedAabb.GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(8.0f, 20.0f, 30.0f), Tolerance));
        EXPECT_EQ(counter.m_count, 1);

        // A change of size without a change of sign flips nothing, so there is nothing to announce.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-4.0f, 1.0f, 1.0f));
        EXPECT_NEAR(GetColliderAabb(entityId).GetCenter().GetX(), 8.0f, Tolerance);
        EXPECT_EQ(counter.m_count, 1);

        // Turning back around moves the AABB back and announces it again.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_NEAR(GetColliderAabb(entityId).GetCenter().GetX(), 12.0f, Tolerance);
        EXPECT_EQ(counter.m_count, 2);
    }

    TEST_F(ColliderFlipTest, Entity_FlipWithoutARuntimeCollider_AnnouncesNoColliderChange)
    {
        // An editor entity has editor colliders only, and its rigid body component rebuilds its body whenever the collider is
        // announced as changed, which every flip would trigger. Nothing here answers the collider bus, as in the editor.
        EntityPtr entity = AZStd::make_shared<AZ::Entity>("FlipEntityWithoutACollider");
        entity->CreateComponent<AzFramework::TransformComponent>();
        entity->Init();
        entity->Activate();
        const AZ::EntityId entityId = entity->GetId();

        ColliderChangedCounter counter(entityId);
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_EQ(counter.m_count, 0);
    }

    TEST_F(ColliderFlipTest, ForceRegion_FlippedTrigger_PushesFromTheMirroredCenter)
    {
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), true);
        entity->CreateComponent<StaticRigidBodyComponent>();
        entity->Init();
        entity->Activate();
        const AZ::EntityId entityId = entity->GetId();

        // The force region component builds its region parameters when it activates, which is before the flip, and keeps them.
        // A region of the same class stands in for the component here, because the component does not hand its region out.
        ForceRegion region;
        region.Activate(entityId);
        region.AddAndActivateForce(AZStd::make_unique<ForcePoint>(100.0f));
        EXPECT_NEAR(region.GetRegionParams().m_aabb.GetCenter().GetX(), 12.0f, Tolerance);

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_NEAR(region.GetRegionParams().m_aabb.GetCenter().GetX(), 8.0f, Tolerance);

        // A point force pushes away from the center of the region. Seen from the origin of the entity, that is toward +X once
        // the region sits on the -X side. Unflipped it is toward -X.
        EntityParams entityParams;
        entityParams.m_id = entityId;
        entityParams.m_position = AZ::Vector3(10.0f, 20.0f, 30.0f);
        entityParams.m_velocity = AZ::Vector3::CreateZero();
        entityParams.m_aabb = AZ::Aabb::CreateNull();
        entityParams.m_mass = 1.0f;
        EXPECT_GT(region.CalculateNetForce(entityParams).GetX(), 0.0f);

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_LT(region.CalculateNetForce(entityParams).GetX(), 0.0f);

        region.Deactivate();
    }

    //////////////////////////////////////////////////////////////////////////
    // A child of a flipped entity
    //
    // A child hears the effective world scale sW: its own scale times the signs of every parent. The tests raise the notification a
    // real parent would raise for the child, with the values written out by hand. The child sits away from the parent, at (10, 20, 30),
    // and its box collider is offset by (2, 0, 1) from it, so a mirror about the wrong origin shows. A mirror about the origin of the
    // child puts the center of the box at (10 + 2 * sx, 20, 30 + 1 * sz), with sx and sz the signs of sW.

    TEST_F(ColliderFlipTest, Child_ParentFlip_MirrorsTheCollidersAboutTheOriginOfTheChild)
    {
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Vector3(2.0f, 0.0f, 1.0f), false);
        AddRigidBody(entity, false);
        entity->Init();
        entity->Activate();
        const AZ::EntityId entityId = entity->GetId();

        // The parent is not flipped and the child has a scale of 1.5: the collider is where it was authored.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(1.5f, 1.5f, 1.5f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(12.0f, 20.0f, 31.0f), Tolerance));

        // The parent flips on X. The scale of the child is still 1.5, and it hears (-1.5, 1.5, 1.5).
        ColliderChangedCounter counter(entityId);
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-1.5f, 1.5f, 1.5f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(8.0f, 20.0f, 31.0f), Tolerance));
        EXPECT_EQ(counter.m_count, 1);

        // The parent grows and stays flipped. The child hears a new size and the same signs, so nothing changes.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-3.0f, 3.0f, 3.0f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(8.0f, 20.0f, 31.0f), Tolerance));
        EXPECT_EQ(counter.m_count, 1);

        // The parent flips on Z as well.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(-3.0f, 3.0f, -3.0f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(8.0f, 20.0f, 29.0f), Tolerance));
        EXPECT_EQ(counter.m_count, 2);

        // The parent turns back around, and the collider is where it was authored.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.5f, 1.5f, 1.5f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(12.0f, 20.0f, 31.0f), Tolerance));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
    }

    TEST_F(ColliderFlipTest, Child_FlippedOnTheSameAxisAsItsParent_IsNotMirrored)
    {
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Vector3(2.0f, 0.0f, 1.0f), false);
        AddRigidBody(entity, false);
        entity->Init();
        entity->Activate();
        const AZ::EntityId entityId = entity->GetId();

        // The child is flipped on X by its own scale, (-1.5, 1.5, 1.5).
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, AZ::Vector3(-1.5f, 1.5f, 1.5f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(8.0f, 20.0f, 31.0f), Tolerance));

        // The parent flips on X too. The signs multiply, so the child hears (1.5, 1.5, 1.5) and the collider is the authored one.
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, AZ::Vector3(1.5f, 1.5f, 1.5f));
        EXPECT_THAT(GetColliderAabb(entityId).GetCenter(), UnitTest::IsCloseTolerance(AZ::Vector3(12.0f, 20.0f, 31.0f), Tolerance));

        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
    }

    //! Stands in for the Transform 2 of a child under a flipped parent: the scale of the child is positive and the world scale is not.
    class ChildTransform2Stand
        : public AZ::Transform2RequestBus::Handler
    {
    public:
        ChildTransform2Stand(AZ::EntityId entityId, const AZ::Vector3& localScale, const AZ::Vector3& worldScale)
            : m_localScale(localScale)
            , m_worldScale(worldScale)
        {
            AZ::Transform2RequestBus::Handler::BusConnect(entityId);
        }

        ~ChildTransform2Stand() override
        {
            AZ::Transform2RequestBus::Handler::BusDisconnect();
        }

        // AZ::Transform2RequestBus overrides...
        AZ::Vector3 GetLocalScale() const override
        {
            return m_localScale;
        }

        void SetLocalScale(const AZ::Vector3& scale) override
        {
            m_localScale = scale;
        }

        AZ::Vector3 GetWorldScale() const override
        {
            return m_worldScale;
        }

        AZ::Vector3 m_localScale;
        AZ::Vector3 m_worldScale;
    };

    TEST_F(ColliderFlipTest, GetEntitySigns_ChildOfAFlippedParent_ReadsTheWorldScale)
    {
        const AZ::EntityId entityId(4321);

        // No Transform 2 on the entity: nothing is flipped.
        EXPECT_THAT(Transform2Mirror::GetEntitySigns(entityId), UnitTest::IsClose(AZ::Vector3::CreateOne()));

        // The child has a scale of one and its parent is flipped on X and Z: the editor mirrors with the signs of the world scale.
        ChildTransform2Stand transform2(entityId, AZ::Vector3(1.0f, 1.0f, 1.0f), AZ::Vector3(-2.0f, 2.0f, -2.0f));
        const AZ::Vector3 signs = Transform2Mirror::GetEntitySigns(entityId);
        EXPECT_THAT(signs, UnitTest::IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));

        // The pivot is the origin of the child, not the origin of the world.
        const AZ::Transform world = AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, 20.0f, 30.0f));
        EXPECT_THAT(
            Transform2Mirror::MirrorWorldPoint(world, signs, AZ::Vector3(12.0f, 20.0f, 31.0f)),
            UnitTest::IsCloseTolerance(AZ::Vector3(8.0f, 20.0f, 29.0f), Tolerance));

        // Flipped on X by its own scale and by its parent, the child is not mirrored on X. The Z flip of the parent stays.
        transform2.m_localScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        transform2.m_worldScale = AZ::Vector3(2.0f, 2.0f, -2.0f);
        EXPECT_THAT(Transform2Mirror::GetEntitySigns(entityId), UnitTest::IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Meshes

    TEST_F(ColliderFlipTest, TriangleMesh_Flip_TakesTheSignInTheMeshScaleAndKeepsTheWinding)
    {
        auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
        AzPhysics::SimulatedBodyHandle handle =
            TestUtils::AddKinematicTriangleMeshCubeToScene(m_testSceneHandle, 1.0f, AzPhysics::MassComputeFlags::NONE);
        auto* body = azdynamic_cast<AzPhysics::RigidBody*>(sceneInterface->GetSimulatedBodyFromHandle(m_testSceneHandle, handle));
        ASSERT_NE(body, nullptr);
        AZStd::shared_ptr<Physics::Shape> shape = body->GetShape(0);
        ASSERT_NE(shape, nullptr);

        const physx::PxVec3 authoredScale = GetTriangleMeshScale(shape);
        AZStd::vector<AZ::Vector3> authoredVertices;
        AZStd::vector<AZ::u32> authoredIndices;
        shape->GetGeometry(authoredVertices, authoredIndices);
        ASSERT_FALSE(authoredIndices.empty());

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, 1.0f));

        const physx::PxVec3 flippedScale = GetTriangleMeshScale(shape);
        EXPECT_NEAR(flippedScale.x, -authoredScale.x, Tolerance);
        EXPECT_NEAR(flippedScale.y, authoredScale.y, Tolerance);
        EXPECT_NEAR(flippedScale.z, authoredScale.z, Tolerance);

        // The vertices come out mirrored in X. The determinant is negative, so two indices of every triangle are swapped
        // and the triangles keep the winding of the unmirrored mesh.
        AZStd::vector<AZ::Vector3> flippedVertices;
        AZStd::vector<AZ::u32> flippedIndices;
        shape->GetGeometry(flippedVertices, flippedIndices);
        ASSERT_EQ(flippedVertices.size(), authoredVertices.size());
        ASSERT_EQ(flippedIndices.size(), authoredIndices.size());
        for (size_t vertexIndex = 0; vertexIndex < authoredVertices.size(); ++vertexIndex)
        {
            const AZ::Vector3& authored = authoredVertices[vertexIndex];
            const AZ::Vector3 mirrored(-authored.GetX(), authored.GetY(), authored.GetZ());
            EXPECT_THAT(flippedVertices[vertexIndex], UnitTest::IsCloseTolerance(mirrored, Tolerance));
        }
        for (size_t index = 0; index + 2 < authoredIndices.size(); index += 3)
        {
            EXPECT_EQ(flippedIndices[index], authoredIndices[index]);
            EXPECT_EQ(flippedIndices[index + 1], authoredIndices[index + 2]);
            EXPECT_EQ(flippedIndices[index + 2], authoredIndices[index + 1]);
        }

        // X and Z together are a turn about Y. The determinant is positive again, so the indices are left alone.
        flip.Apply(AZ::Vector3(-1.0f, 1.0f, -1.0f));
        const physx::PxVec3 turnedScale = GetTriangleMeshScale(shape);
        EXPECT_NEAR(turnedScale.x, -authoredScale.x, Tolerance);
        EXPECT_NEAR(turnedScale.y, authoredScale.y, Tolerance);
        EXPECT_NEAR(turnedScale.z, -authoredScale.z, Tolerance);
        AZStd::vector<AZ::Vector3> turnedVertices;
        AZStd::vector<AZ::u32> turnedIndices;
        shape->GetGeometry(turnedVertices, turnedIndices);
        EXPECT_EQ(turnedIndices, authoredIndices);

        flip.Apply(AZ::Vector3(1.0f, 1.0f, 1.0f));
        const physx::PxVec3 restoredScale = GetTriangleMeshScale(shape);
        EXPECT_NEAR(restoredScale.x, authoredScale.x, Tolerance);
        EXPECT_NEAR(restoredScale.y, authoredScale.y, Tolerance);
        EXPECT_NEAR(restoredScale.z, authoredScale.z, Tolerance);
    }

    TEST_F(ColliderFlipTest, ConvexMesh_FlipZ_SwapsInAMirroredHullAndBackToTheAuthoredOne)
    {
        // A pyramid with its apex up. A Z flip puts the apex down, which a negative scale could not do for a convex mesh.
        AZStd::shared_ptr<Physics::Shape> shape = TestUtils::CreatePyramidShape(1.0f);
        ASSERT_NE(shape, nullptr);
        ASSERT_NE(AddStaticBody(shape, AZ::Vector3::CreateZero()), nullptr);

        physx::PxConvexMesh* authoredMesh = GetConvexMesh(shape);
        AZStd::vector<AZ::Vector3> vertices;
        AZStd::vector<AZ::u32> indices;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        shape->GetGeometry(vertices, indices);
        GetHeightRange(vertices, minZ, maxZ);
        EXPECT_NEAR(minZ, 0.0f, Tolerance);
        EXPECT_NEAR(maxZ, 1.0f, Tolerance);

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(1.0f, 1.0f, -1.0f));

        EXPECT_NE(GetConvexMesh(shape), authoredMesh);
        vertices.clear();
        indices.clear();
        shape->GetGeometry(vertices, indices);
        GetHeightRange(vertices, minZ, maxZ);
        EXPECT_NEAR(minZ, -1.0f, Tolerance);
        EXPECT_NEAR(maxZ, 0.0f, Tolerance);

        // Back to no flip uses the authored mesh again, not a third copy.
        flip.Apply(AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_EQ(GetConvexMesh(shape), authoredMesh);
        vertices.clear();
        indices.clear();
        shape->GetGeometry(vertices, indices);
        GetHeightRange(vertices, minZ, maxZ);
        EXPECT_NEAR(minZ, 0.0f, Tolerance);
        EXPECT_NEAR(maxZ, 1.0f, Tolerance);
    }

    TEST_F(ColliderFlipTest, ConvexMesh_FlipAgainTheSameWay_ReusesTheCookedHull)
    {
        AZStd::shared_ptr<Physics::Shape> shape = TestUtils::CreatePyramidShape(1.0f);
        ASSERT_NE(shape, nullptr);

        Transform2ShapeFlip flip(shape);
        flip.Apply(AZ::Vector3(1.0f, 1.0f, -1.0f));
        physx::PxConvexMesh* flippedMesh = GetConvexMesh(shape);

        flip.Apply(AZ::Vector3(1.0f, 1.0f, 1.0f));
        flip.Apply(AZ::Vector3(1.0f, 1.0f, -1.0f));
        EXPECT_EQ(GetConvexMesh(shape), flippedMesh);
    }

    //////////////////////////////////////////////////////////////////////////
    // The bodies of the default scene: the White Box collider
    //
    // The White Box collider makes its own body in the default scene, static or kinematic, in its Activate, and answers no collider
    // bus. It depends on the Transform2Service, so that is after the Transform 2 of its entity announced itself and before the
    // entity finished activating: inside the activation window of the listener, which these tests open by announcing an entity
    // that is not active yet. They make such a body by hand for the entity, at the moment the White Box makes it.
    //
    // The listener lives as long as the process, so a window that one test leaves open would stay open for every test after it.
    // The fixture retires every entity it announced, and checks that no window is left.

    class BodyFlipTest
        : public ColliderFlipTest
    {
    protected:
        void TearDown() override
        {
            for (const AZ::EntityId entityId : m_announced)
            {
                Retire(entityId);
            }
            m_announced.clear();
            EXPECT_EQ(Transform2ColliderSupport::GetOpenWindowCount(), 0u);
            EXPECT_FALSE(Transform2ColliderSupport::IsHearingBodies());

            ColliderFlipTest::TearDown();
        }

        static AZ::Vector3 ShapeOffset()
        {
            return AZ::Vector3(2.0f, 0.0f, 1.0f);
        }

        // An entity with a transform only. A test that activates it later announces it first, as a Transform 2 does.
        EntityPtr CreateTransformEntity(bool activate)
        {
            EntityPtr entity = AZStd::make_shared<AZ::Entity>("BodyFlipEntity");
            entity->CreateComponent<AzFramework::TransformComponent>();
            entity->Init();
            if (activate)
            {
                entity->Activate();
            }
            return entity;
        }

        // A cooked cube as a triangle mesh, offset from the entity, the way the White Box collider makes its shape.
        AZStd::shared_ptr<Physics::Shape> CreateTriangleMeshShape()
        {
            const VertexIndexData cubeMeshData = TestUtils::GenerateCubeMeshData(1.0f);
            AZStd::vector<AZ::u8> cookedData;
            bool cookingResult = false;
            Physics::SystemRequestBus::BroadcastResult(
                cookingResult,
                &Physics::SystemRequests::CookTriangleMeshToMemory,
                cubeMeshData.first.data(),
                static_cast<AZ::u32>(cubeMeshData.first.size()),
                cubeMeshData.second.data(),
                static_cast<AZ::u32>(cubeMeshData.second.size()),
                cookedData);
            EXPECT_TRUE(cookingResult);

            Physics::CookedMeshShapeConfiguration shapeConfig;
            shapeConfig.SetCookedMeshData(
                cookedData.data(), cookedData.size(), Physics::CookedMeshShapeConfiguration::MeshType::TriangleMesh);

            Physics::ColliderConfiguration colliderConfig;
            colliderConfig.m_position = ShapeOffset();
            return AZ::Interface<Physics::System>::Get()->CreateShape(colliderConfig, shapeConfig);
        }

        // WhiteBoxBodyType::Static
        AzPhysics::SimulatedBodyHandle AddStaticBodyFor(AZ::EntityId entityId, const AZStd::shared_ptr<Physics::Shape>& shape)
        {
            AzPhysics::StaticRigidBodyConfiguration configuration;
            configuration.m_entityId = entityId;
            configuration.m_colliderAndShapeData = shape;
            return AZ::Interface<AzPhysics::SceneInterface>::Get()->AddSimulatedBody(m_testSceneHandle, &configuration);
        }

        // WhiteBoxBodyType::Kinematic. The mass cannot be computed from a triangle mesh, so it is not.
        AzPhysics::SimulatedBodyHandle AddKinematicBodyFor(AZ::EntityId entityId, const AZStd::shared_ptr<Physics::Shape>& shape)
        {
            AzPhysics::RigidBodyConfiguration configuration;
            configuration.m_entityId = entityId;
            configuration.m_kinematic = true;
            configuration.m_computeCenterOfMass = false;
            configuration.m_computeMass = false;
            configuration.m_computeInertiaTensor = false;
            configuration.m_colliderAndShapeData = shape;
            return AZ::Interface<AzPhysics::SceneInterface>::Get()->AddSimulatedBody(m_testSceneHandle, &configuration);
        }

        void RemoveBody(AzPhysics::SimulatedBodyHandle& bodyHandle)
        {
            AZ::Interface<AzPhysics::SceneInterface>::Get()->RemoveSimulatedBody(m_testSceneHandle, bodyHandle);
        }

        // A mirrored triangle mesh shape has its offset mirrored and the sign of each flipped axis in its mesh scale.
        void ExpectMirrored(const AZStd::shared_ptr<Physics::Shape>& shape, const AZ::Vector3& signs)
        {
            ExpectPose(shape, ShapeOffset() * signs, AZ::Quaternion::CreateIdentity());
            const physx::PxVec3 meshScale = GetTriangleMeshScale(shape);
            EXPECT_NEAR(meshScale.x, signs.GetX(), Tolerance);
            EXPECT_NEAR(meshScale.y, signs.GetY(), Tolerance);
            EXPECT_NEAR(meshScale.z, signs.GetZ(), Tolerance);
        }

        // No window is open and the default scene is not heard.
        static void ExpectNoWindow()
        {
            EXPECT_EQ(Transform2ColliderSupport::GetOpenWindowCount(), 0u);
            EXPECT_FALSE(Transform2ColliderSupport::IsHearingBodies());
        }

        void Announce(AZ::EntityId entityId, const AZ::Vector3& scale)
        {
            m_announced.push_back(entityId);
            AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, scale);
        }

        void ChangeScale(AZ::EntityId entityId, const AZ::Vector3& scale)
        {
            AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, scale);
        }

        void Retire(AZ::EntityId entityId)
        {
            AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
        }

        AZStd::vector<AZ::EntityId> m_announced;
    };

    TEST_F(BodyFlipTest, StaticBodyAddedWhileTheEntityActivates_IsFlippedWhenTheEntityFinishes)
    {
        // The order of a real White Box entity: the Transform 2 announces first, the collider makes its body while the entity
        // is still activating, and only then does the entity finish.
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-2.0f, 1.0f, 0.5f));
        EXPECT_EQ(Transform2ColliderSupport::GetOpenWindowCount(), 1u);
        EXPECT_TRUE(Transform2ColliderSupport::IsHearingBodies());

        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddStaticBodyFor(entityId, shape);
        ASSERT_NE(bodyHandle, AzPhysics::InvalidSimulatedBodyHandle);
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        // Only the sign of X reaches the shape, whatever the size.
        entity->Activate();
        ExpectMirrored(shape, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectNoWindow();

        Retire(entityId);
        RemoveBody(bodyHandle);
    }

    TEST_F(BodyFlipTest, KinematicBodyAddedWhileTheEntityActivates_IsFlippedWhenTheEntityFinishes)
    {
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));

        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddKinematicBodyFor(entityId, shape);
        ASSERT_NE(bodyHandle, AzPhysics::InvalidSimulatedBodyHandle);
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        entity->Activate();
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, -1.0f));
        ExpectNoWindow();

        Retire(entityId);
        RemoveBody(bodyHandle);
    }

    TEST_F(BodyFlipTest, BodyOfAnotherEntityAddedInTheWindow_IsUntouched)
    {
        // A nested activation: while the flipped entity is still activating, the bodies of two other entities are added. One is a
        // Transform 1 entity, the other has no entity at all.
        EntityPtr flippedEntity = CreateTransformEntity(false);
        EntityPtr plainEntity = CreateTransformEntity(true);
        Announce(flippedEntity->GetId(), AZ::Vector3(-1.0f, 1.0f, -1.0f));

        AZStd::shared_ptr<Physics::Shape> plainShape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle plainHandle = AddStaticBodyFor(plainEntity->GetId(), plainShape);
        AZStd::shared_ptr<Physics::Shape> ownerlessShape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle ownerlessHandle = AddStaticBodyFor(AZ::EntityId(), ownerlessShape);

        AZStd::shared_ptr<Physics::Shape> flippedShape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle flippedHandle = AddStaticBodyFor(flippedEntity->GetId(), flippedShape);

        flippedEntity->Activate();
        ExpectMirrored(flippedShape, AZ::Vector3(-1.0f, 1.0f, -1.0f));

        // The later flips of the flipped entity do not reach the others either.
        ChangeScale(flippedEntity->GetId(), AZ::Vector3(1.0f, -1.0f, 1.0f));
        ExpectMirrored(plainShape, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectMirrored(ownerlessShape, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectMirrored(flippedShape, AZ::Vector3(1.0f, -1.0f, 1.0f));

        Retire(flippedEntity->GetId());
        RemoveBody(plainHandle);
        RemoveBody(ownerlessHandle);
        RemoveBody(flippedHandle);
    }

    TEST_F(BodyFlipTest, BodyAddedAfterTheWindowClosed_IsNotCaptured)
    {
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Activate();
        ExpectNoWindow();

        // The window is over, so the scene is not heard and the body is left as it was authored. Later flips do not find it.
        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddStaticBodyFor(entityId, shape);
        ASSERT_NE(bodyHandle, AzPhysics::InvalidSimulatedBodyHandle);
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        ChangeScale(entityId, AZ::Vector3(1.0f, -1.0f, 1.0f));
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        Retire(entityId);
        RemoveBody(bodyHandle);
    }

    TEST_F(BodyFlipTest, AnnouncementOnALiveEntity_OpensNoWindow)
    {
        EntityPtr entity = CreateTransformEntity(true);
        const AZ::EntityId entityId = entity->GetId();

        // The entity is active already, so connecting to it calls OnEntityActivated at once and the window it opened is closed.
        Announce(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectNoWindow();

        // Announced again, the entity is live and the window is not opened a second time. A body made now is not heard.
        Announce(entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
        ExpectNoWindow();

        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddStaticBodyFor(entityId, shape);
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        Retire(entityId);
        RemoveBody(bodyHandle);
    }

    TEST_F(BodyFlipTest, DeactivationBeforeTheEntityFinished_ClosesTheWindow)
    {
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_EQ(Transform2ColliderSupport::GetOpenWindowCount(), 1u);
        EXPECT_TRUE(Transform2ColliderSupport::IsHearingBodies());

        Retire(entityId);
        ExpectNoWindow();

        // The entity finishing afterwards finds nothing to close, and the count does not go below zero.
        entity->Activate();
        ExpectNoWindow();
    }

    TEST_F(BodyFlipTest, TwoEntitiesActivating_KeepTheSceneHeardUntilTheLastOneFinishes)
    {
        EntityPtr firstEntity = CreateTransformEntity(false);
        EntityPtr secondEntity = CreateTransformEntity(false);
        Announce(firstEntity->GetId(), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        Announce(secondEntity->GetId(), AZ::Vector3(1.0f, 1.0f, -1.0f));
        EXPECT_EQ(Transform2ColliderSupport::GetOpenWindowCount(), 2u);

        AZStd::shared_ptr<Physics::Shape> secondShape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle secondHandle = AddStaticBodyFor(secondEntity->GetId(), secondShape);

        firstEntity->Activate();
        EXPECT_EQ(Transform2ColliderSupport::GetOpenWindowCount(), 1u);
        EXPECT_TRUE(Transform2ColliderSupport::IsHearingBodies());
        ExpectMirrored(secondShape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        secondEntity->Activate();
        ExpectNoWindow();
        ExpectMirrored(secondShape, AZ::Vector3(1.0f, 1.0f, -1.0f));

        Retire(firstEntity->GetId());
        Retire(secondEntity->GetId());
        RemoveBody(secondHandle);
    }

    TEST_F(BodyFlipTest, LaterFlips_ReachTheCapturedShape)
    {
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));

        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddKinematicBodyFor(entityId, shape);
        ASSERT_NE(bodyHandle, AzPhysics::InvalidSimulatedBodyHandle);
        entity->Activate();
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        // The turn-around of a script, then the other axis, then both, then back. Each state derives from the authored shape.
        ChangeScale(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ExpectMirrored(shape, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ChangeScale(entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, -1.0f));
        ChangeScale(entityId, AZ::Vector3(-3.0f, 1.0f, -2.0f));
        ExpectMirrored(shape, AZ::Vector3(-1.0f, 1.0f, -1.0f));
        ChangeScale(entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        Retire(entityId);
        RemoveBody(bodyHandle);
    }

    TEST_F(BodyFlipTest, EntityDeactivated_PutsTheShapeBackAndLaterBodiesAreNotFlipped)
    {
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddStaticBodyFor(entityId, shape);
        entity->Activate();
        ExpectMirrored(shape, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        // An entity that lives on after leaving Transform 2 has its authored shape back.
        Retire(entityId);
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        // The listener knows the entity no more, so a body made now is left alone.
        AZStd::shared_ptr<Physics::Shape> laterShape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle laterHandle = AddStaticBodyFor(entityId, laterShape);
        ExpectMirrored(laterShape, AZ::Vector3(1.0f, 1.0f, 1.0f));

        RemoveBody(bodyHandle);
        RemoveBody(laterHandle);
    }

    TEST_F(BodyFlipTest, BodyRemoved_TheShapeThatOutlivesItKeepsItsCapture)
    {
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        AZStd::shared_ptr<Physics::Shape> shape = CreateTriangleMeshShape();
        AzPhysics::SimulatedBodyHandle bodyHandle = AddStaticBodyFor(entityId, shape);
        entity->Activate();
        ExpectMirrored(shape, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        // Removing a body is not heard. The shape of a collider component outlives its body here, as it does when its rigid body
        // is destroyed, and it stays flipped under the one capture it has. Leaving Transform 2 puts it back as it was authored.
        RemoveBody(bodyHandle);
        ExpectMirrored(shape, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        Retire(entityId);
        ExpectMirrored(shape, AZ::Vector3(1.0f, 1.0f, 1.0f));
    }

    TEST_F(BodyFlipTest, ColliderComponentBody_IsFlippedOnceWhetherTheComponentOrTheSceneFindsIt)
    {
        // A box collider with a static body: its shape is reached from the collider bus and from the body the scene adds. The
        // announcement comes first, so the scene sees the body before the entity finishes activating.
        EntityPtr entity = CreateBoxColliderEntity(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), false);
        entity->CreateComponent<StaticRigidBodyComponent>();
        entity->Init();
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Activate();
        ExpectNoWindow();

        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
        ColliderComponentRequestBus::EventResult(shapes, entityId, &ColliderComponentRequests::GetShapes);
        ASSERT_EQ(shapes.size(), 1u);

        // Flipped once. A shape captured twice would take the flipped pose for the authored one and flip back.
        ExpectPose(shapes[0], AZ::Vector3(-2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());

        ChangeScale(entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ExpectPose(shapes[0], AZ::Vector3(2.0f, 0.0f, 0.0f), AZ::Quaternion::CreateIdentity());

        Retire(entityId);
    }

    TEST_F(BodyFlipTest, ChildOfAFlippedParent_JointsReadTheSignsOfTheWorldScale)
    {
        // A joint reads the signs of its follower once, when it makes its native joint, and nothing re-poses it after. A follower that is
        // a child of a flipped entity has a scale of one of its own, and is announced with the world scale: here the parent is
        // flipped on X and Z, so (-1.5, 1.5, -1.5).
        EntityPtr entity = CreateTransformEntity(false);
        const AZ::EntityId entityId = entity->GetId();
        Announce(entityId, AZ::Vector3(-1.5f, 1.5f, -1.5f));
        entity->Activate();
        ExpectNoWindow();

        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));

        // The parent flips on Z back, then the child is flipped by its own scale on Y as well: (-1.5, -1.5, 1.5).
        ChangeScale(entityId, AZ::Vector3(-1.5f, -1.5f, 1.5f));
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(AZ::Vector3(-1.0f, -1.0f, 1.0f)));

        // The parent turns back around and the child hears a positive scale.
        ChangeScale(entityId, AZ::Vector3(1.5f, 1.5f, 1.5f));
        EXPECT_THAT(Transform2ColliderSupport::FindSigns(entityId), UnitTest::IsClose(AZ::Vector3::CreateOne()));

        Retire(entityId);
    }
} // namespace PhysX
