/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Interface/Interface.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <AzFramework/Visibility/EntityBoundsUnionBus.h>
#include <AzToolsFramework/API/ComponentEntitySelectionBus.h>
#include <AzToolsFramework/ToolsComponents/EditorNonUniformScaleComponent.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <AzToolsFramework/ViewportSelection/EditorSelectionUtil.h>
#include <AzToolsFramework/Manipulators/BoxManipulatorRequestBus.h>
#include <AZTestShared/Math/MathTestHelpers.h>
#include <EditorColliderComponent.h>
#include <EditorMeshColliderComponent.h>
#include <EditorShapeColliderComponent.h>
#include <EditorForceRegionComponent.h>
#include <EditorRigidBodyComponent.h>
#include <EditorStaticRigidBodyComponent.h>
#include <LmbrCentral/Shape/BoxShapeComponentBus.h>
#include <PhysX/ForceRegionComponentBus.h>
#include <PhysX/MathConversion.h>
#include <PhysX/PhysXLocks.h>
#include <PhysX/SystemComponentBus.h>
#include <BaseColliderComponent.h>
#include <RigidBodyComponent.h>
#include <RigidBodyStatic.h>
#include <ShapeColliderComponent.h>
#include <SphereColliderComponent.h>
#include <BoxColliderComponent.h>
#include <CapsuleColliderComponent.h>
#include <StaticRigidBodyComponent.h>
#include <Source/Transform2ColliderSupport.h>
#include <Tests/EditorTestUtilities.h>
#include <Tests/PhysXTestCommon.h>

namespace PhysXEditorTests
{
    TEST_F(PhysXEditorFixture, EditorColliderComponent_RigidBodyDependencySatisfied_EntityIsValid)
    {
        EntityPtr entity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        entity->CreateComponent<PhysX::EditorColliderComponent>();
        entity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();

        // the entity should be in a valid state because the component requirement is satisfied
        AZ::Entity::DependencySortOutcome sortOutcome = entity->EvaluateDependenciesGetDetails();
        EXPECT_TRUE(sortOutcome.IsSuccess());
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_RigidBodyDependencyMissing_EntityIsInvalid)
    {
        EntityPtr entity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        entity->CreateComponent<PhysX::EditorColliderComponent>();

        // the entity should not be in a valid state because the collider component requires a rigid body
        AZ::Entity::DependencySortOutcome sortOutcome = entity->EvaluateDependenciesGetDetails();
        EXPECT_FALSE(sortOutcome.IsSuccess());
        EXPECT_TRUE(sortOutcome.GetError().m_code == AZ::Entity::DependencySortResult::MissingRequiredService);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_MultipleColliderComponents_EntityIsValid)
    {
        EntityPtr entity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        entity->CreateComponent<PhysX::EditorColliderComponent>();
        entity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();

        // adding a second collider component should not make the entity invalid
        entity->CreateComponent<PhysX::EditorColliderComponent>();

        // the entity should be in a valid state because the component requirement is satisfied
        AZ::Entity::DependencySortOutcome sortOutcome = entity->EvaluateDependenciesGetDetails();
        EXPECT_TRUE(sortOutcome.IsSuccess());
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_WithOtherColliderComponents_EntityIsValid)
    {
        EntityPtr entity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        entity->CreateComponent<PhysX::EditorColliderComponent>();
        entity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();

        // the collider component should be compatible with multiple collider components
        entity->CreateComponent<PhysX::EditorMeshColliderComponent>();
        entity->CreateComponent<PhysX::EditorShapeColliderComponent>();
        entity->CreateComponent(LmbrCentral::EditorBoxShapeComponentTypeId);

        // the entity should be in a valid state because the component requirement is satisfied
        AZ::Entity::DependencySortOutcome sortOutcome = entity->EvaluateDependenciesGetDetails();
        EXPECT_TRUE(sortOutcome.IsSuccess());
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithBox_CorrectRuntimeComponents)
    {
        // create an editor entity with a collider component
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();

        // Set collider to be a box
        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Box);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        // check that the runtime entity has the expected components
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BoxColliderComponent>() != nullptr);
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::StaticRigidBodyComponent>() != nullptr);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithBox_CorrectRuntimeGeometry)
    {
        // create an editor entity with a collider component
        const AZ::Vector3 boxDimensions(2.0f, 3.0f, 4.0f);
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(boxDimensions);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        // since there was no editor rigid body component, the runtime entity should have a static rigid body
        const auto* staticBody = azdynamic_cast<PhysX::StaticRigidBody*>(gameEntity->FindComponent<PhysX::StaticRigidBodyComponent>()->GetSimulatedBody());
        const auto* pxRigidStatic = static_cast<const physx::PxRigidStatic*>(staticBody->GetNativePointer());

        PHYSX_SCENE_READ_LOCK(pxRigidStatic->getScene());

        // there should be a single shape on the rigid body and it should be a box
        EXPECT_EQ(pxRigidStatic->getNbShapes(), 1);
        if (pxRigidStatic->getNbShapes() > 0)
        {
            physx::PxShape* shape = nullptr;
            pxRigidStatic->getShapes(&shape, 1, 0);
            EXPECT_EQ(shape->getGeometryType(), physx::PxGeometryType::eBOX);
        }

        // the bounding box of the rigid body should reflect the dimensions of the box set above
        AZ::Aabb aabb = staticBody->GetAabb();
        EXPECT_TRUE(aabb.GetMax().IsClose(0.5f * boxDimensions));
        EXPECT_TRUE(aabb.GetMin().IsClose(-0.5f * boxDimensions));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_BoxPrimitiveColliderWithTranslationOffset_CorrectEditorStaticBodyGeometry)
    {
        const AZ::Vector3 boxDimensions(5.0f, 8.0f, 6.0f);
        const AZ::Transform transform(AZ::Vector3(2.0f, -6.0f, 5.0f), AZ::Quaternion(0.3f, -0.3f, 0.1f, 0.9f), 1.6f);
        const AZ::Vector3 translationOffset(-4.0f, 3.0f, -1.0f);
        const AZ::Vector3 nonUniformScale(2.0f, 2.5f, 0.5f);
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(boxDimensions, transform, translationOffset, AZ::Quaternion::CreateIdentity(), nonUniformScale);

        AZ::Aabb aabb = GetSimulatedBodyAabb(editorEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-25.488f, -10.16f, -11.448f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(1.136f, 18.32f, 16.584f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_BoxPrimitiveColliderWithTranslationOffset_CorrectEditorDynamicBodyGeometry)
    {
        const AZ::Vector3 boxDimensions(6.0f, 4.0f, 1.0f);
        const AZ::Transform transform(AZ::Vector3(-5.0f, -1.0f, 3.0f), AZ::Quaternion(0.7f, 0.5f, -0.1f, 0.5f), 1.2f);
        const AZ::Vector3 translationOffset(6.0f, -5.0f, -4.0f);
        const AZ::Vector3 nonUniformScale(1.5f, 1.5f, 4.0f);
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            boxDimensions, transform, translationOffset, AZ::Quaternion::CreateIdentity(), nonUniformScale, RigidBodyType::Dynamic);

        editorEntity->Deactivate();
        editorEntity->Activate();

        AZ::Aabb aabb = GetSimulatedBodyAabb(editorEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-20.264f, 15.68f, -6.864f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(-7.592f, 26.0f, 6.672f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_BoxPrimitiveColliderWithTranslationOffset_CorrectRuntimeStaticBodyGeometry)
    {
        const AZ::Vector3 boxDimensions(1.0f, 4.0f, 7.0f);
        const AZ::Transform transform(AZ::Vector3(7.0f, 2.0f, 4.0f), AZ::Quaternion(0.4f, -0.8f, 0.4f, 0.2f), 2.5f);
        const AZ::Vector3 translationOffset(6.0f, -1.0f, -2.0f);
        const AZ::Vector3 nonUniformScale(0.8f, 2.0f, 1.5f);
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            boxDimensions, transform, translationOffset, AZ::Quaternion::CreateIdentity(), nonUniformScale);
        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        AZ::Aabb aabb = GetSimulatedBodyAabb(gameEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-4.8f, -14.14f, 5.265f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(12.4f, 15.02f, 31.895f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_BoxPrimitiveColliderWithTranslationOffset_CorrectRuntimeDynamicBodyGeometry)
    {
        const AZ::Vector3 boxDimensions(4.0f, 2.0f, 7.0f);
        const AZ::Transform transform(AZ::Vector3(4.0f, 4.0f, 2.0f), AZ::Quaternion(0.1f, 0.3f, 0.9f, 0.3f), 0.8f);
        const AZ::Vector3 translationOffset(-2.0f, 7.0f, -1.0f);
        const AZ::Vector3 nonUniformScale(2.5f, 1.0f, 2.0f);
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            boxDimensions, transform, translationOffset, AZ::Quaternion::CreateIdentity(), nonUniformScale, RigidBodyType::Dynamic);
        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        AZ::Aabb aabb = GetSimulatedBodyAabb(gameEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-1.664f, -8.352f, -0.88f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(9.536f, 2.848f, 9.04f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithSphere_CorrectRuntimeComponents)
    {
        // create an editor entity with a collider component
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();

        // Set collider to be a sphere
        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Sphere);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        // check that the runtime entity has the expected components
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::SphereColliderComponent>() != nullptr);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithSphereAndTranslationOffset_CorrectEditorStaticBodyGeometry)
    {
        EntityPtr editorEntity = CreateSpherePrimitiveColliderEditorEntity(
            1.6f,
            AZ::Transform(AZ::Vector3(4.0f, 2.0f, -2.0f), AZ::Quaternion(-0.5f, -0.5f, 0.1f, 0.7f), 3.0f),
            AZ::Vector3(2.0f, 3.0f, -7.0f)
        );

        AZ::Aabb aabb = GetSimulatedBodyAabb(editorEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(22.12f, -7.24f, -10.4f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(31.72f, 2.36f, -0.8f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithSphereAndTranslationOffset_CorrectEditorDynamicBodyGeometry)
    {
        EntityPtr editorEntity = CreateSpherePrimitiveColliderEditorEntity(
            3.5f,
            AZ::Transform(AZ::Vector3(2.0f, -5.0f, -6.0f), AZ::Quaternion(0.7f, 0.1f, 0.7f, 0.1f), 0.4f),
            AZ::Vector3(1.0f, 3.0f, -1.0f),
            AZ::Quaternion::CreateIdentity(),
            AZStd::nullopt,
            RigidBodyType::Dynamic);

        editorEntity->Deactivate();
        editorEntity->Activate();

        AZ::Aabb aabb = GetSimulatedBodyAabb(editorEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(0.2f, -7.44f, -6.68f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(3.0f, -4.64f, -3.88f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithSphereAndTranslationOffset_CorrectRuntimeStaticBodyGeometry)
    {
        EntityPtr editorEntity = CreateSpherePrimitiveColliderEditorEntity(
            2.0f,
            AZ::Transform(AZ::Vector3(4.0f, 4.0f, -1.0f), AZ::Quaternion(0.8f, -0.2f, 0.4f, 0.4f), 2.4f),
            AZ::Vector3(5.0f, 6.0f, -8.0f)
        );

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        AZ::Aabb aabb = GetSimulatedBodyAabb(gameEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-12.032f, 5.92f, 17.624f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(-2.432f, 15.52f, 27.224f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithSphereAndTranslationOffset_CorrectRuntimeDynamicBodyGeometry)
    {
        EntityPtr editorEntity = CreateSpherePrimitiveColliderEditorEntity(
            0.4f,
            AZ::Transform(AZ::Vector3(2.0f, 2.0f, -5.0f), AZ::Quaternion(0.9f, 0.3f, 0.3f, 0.1f), 5.0f),
            AZ::Vector3(4.0f, 7.0f, 3.0f),
            AZ::Quaternion::CreateIdentity(),
            AZStd::nullopt,
            RigidBodyType::Dynamic);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        AZ::Aabb aabb = GetSimulatedBodyAabb(gameEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(38.6f, -16.0f, 3.2f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(42.6f, -12.0f, 7.2f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCapsule_CorrectRuntimeComponents)
    {
        // create an editor entity with a collider component
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();

        // Set collider to be a capsule
        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Capsule);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        // check that the runtime entity has the expected components
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::CapsuleColliderComponent>() != nullptr);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCapsuleAndTranslationOffset_CorrectEditorStaticBodyGeometry)
    {
        EntityPtr editorEntity = CreateCapsulePrimitiveColliderEditorEntity(
            2.0f,
            8.0f,
            AZ::Transform(AZ::Vector3(2.0f, 1.0f, -2.0f), AZ::Quaternion(-0.2f, -0.8f, -0.4f, 0.4f), 4.0f),
            AZ::Vector3(2.0f, 3.0f, 5.0f)
        );

        AZ::Aabb aabb = GetSimulatedBodyAabb(editorEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-16.56f, 9.8f, -7.92f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(7.12f, 38.6f, 13.84f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCapsuleAndTranslationOffset_CorrectEditorDynamicBodyGeometry)
    {
        EntityPtr editorEntity = CreateCapsulePrimitiveColliderEditorEntity(
            1.0f,
            5.0f,
            AZ::Transform(AZ::Vector3(7.0f, -9.0f, 2.0f), AZ::Quaternion(0.7f, 0.1f, 0.7f, 0.1f), 0.2f),
            AZ::Vector3(2.0f, 9.0f, -5.0f),
            AZ::Quaternion::CreateIdentity(),
            AZStd::nullopt,
            RigidBodyType::Dynamic);

        editorEntity->Deactivate();
        editorEntity->Activate();

        AZ::Aabb aabb = GetSimulatedBodyAabb(editorEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(5.5f, -10.816f, 2.688f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(6.5f, -10.416f, 3.088f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCapsuleAndTranslationOffset_CorrectRuntimeStaticBodyGeometry)
    {
        EntityPtr editorEntity = CreateCapsulePrimitiveColliderEditorEntity(
            2.0f,
            11.0f,
            AZ::Transform(AZ::Vector3(-4.0f, -3.0f, -1.0f), AZ::Quaternion(0.5f, -0.7f, -0.1f, 0.5f), 0.8f),
            AZ::Vector3(4.0f, 1.0f, -3.0f)
        );

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        AZ::Aabb aabb = GetSimulatedBodyAabb(gameEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-6.4f, -6.92f, -0.36f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(1.28f, -1.704f, 5.528f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCapsuleAndTranslationOffset_CorrectRuntimeDynamicBodyGeometry)
    {
        EntityPtr editorEntity = CreateCapsulePrimitiveColliderEditorEntity(
            0.4f,
            3.0f,
            AZ::Transform(AZ::Vector3(7.0f, 6.0f, -3.0f), AZ::Quaternion(-0.3f, -0.1f, -0.3f, 0.9f), 6.0f),
            AZ::Vector3(2.0f, -7.0f, 7.0f),
            AZ::Quaternion::CreateIdentity(),
            AZStd::nullopt,
            RigidBodyType::Dynamic);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        AZ::Aabb aabb = GetSimulatedBodyAabb(gameEntity->GetId());
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(AZ::Vector3(-11.0f, -7.8f, 47.4f)));
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(AZ::Vector3(-6.2f, 4.92f, 62.76f)));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderWithValidRadiusAndValidHeight_CorrectRuntimeGeometry)
    {
        const float validRadius = 1.0f;
        const float validHeight = 1.0f;
        EntityPtr editorEntity = CreateCylinderPrimitiveColliderEditorEntity(validRadius, validHeight);
        
        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());
        
        // since there was no editor rigid body component, the runtime entity should have a static rigid body
        const auto* staticBody = azdynamic_cast<PhysX::StaticRigidBody*>(gameEntity->FindComponent<PhysX::StaticRigidBodyComponent>()->GetSimulatedBody());
        const auto* pxRigidStatic = static_cast<const physx::PxRigidStatic*>(staticBody->GetNativePointer());

        PHYSX_SCENE_READ_LOCK(pxRigidStatic->getScene());
        
        // there should be a single shape on the rigid body and it should be a convex mesh
        EXPECT_EQ(pxRigidStatic->getNbShapes(), 1);
        if (pxRigidStatic->getNbShapes() > 0)
        {
            physx::PxShape* shape = nullptr;
            pxRigidStatic->getShapes(&shape, 1, 0);
            EXPECT_EQ(shape->getGeometryType(), physx::PxGeometryType::eCONVEXMESH);
        }
        
        // the bounding box of the rigid body should reflect the dimensions of the cylinder set above
        AZ::Aabb aabb = staticBody->GetAabb();
        
        // Check that the z positions of the bounding box match that of the cylinder
        EXPECT_THAT(aabb.GetMin().GetZ(), ::testing::FloatNear(-0.5f * validHeight, AZ::Constants::Tolerance));
        EXPECT_THAT(aabb.GetMax().GetZ(), ::testing::FloatNear(0.5f * validHeight, AZ::Constants::Tolerance));
        
        // check that the xy points are not outside the radius of the cylinder
        AZ::Vector2 vecMin(aabb.GetMin().GetX(), aabb.GetMin().GetY());
        AZ::Vector2 vecMax(aabb.GetMax().GetX(), aabb.GetMax().GetY());
        EXPECT_TRUE(AZ::GetAbs(vecMin.GetX()) <= validRadius);
        EXPECT_TRUE(AZ::GetAbs(vecMin.GetY()) <= validRadius);
        EXPECT_TRUE(AZ::GetAbs(vecMax.GetX()) <= validRadius);
        EXPECT_TRUE(AZ::GetAbs(vecMax.GetX()) <= validRadius);        
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderWithNullRadius_HandledGracefully)
    {
        ValidateInvalidEditorColliderComponentParams(0.f, 1.f);
    }
    
    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderWithNullHeight_HandledGracefully)
    {
        ValidateInvalidEditorColliderComponentParams(1.f, 0.f);
    }
    
    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderWithNullRadiusAndNullHeight_HandledGracefully)
    {
        ValidateInvalidEditorColliderComponentParams(0.f, 0.f);
    }
    
    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderWithNegativeRadiusAndNullHeight_HandledGracefully)
    {
        ValidateInvalidEditorColliderComponentParams(-1.f, 0.f);
    }
    
    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderWithNullRadiusAndNegativeHeight_HandledGracefully)
    {
        ValidateInvalidEditorColliderComponentParams(0.f, -1.f);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithCylinderSwitchingFromNullHeightToValidHeight_HandledGracefully)
    {
        // create an editor entity with a collider component
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();

        // Set collider to be a cylinder
        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Cylinder);

        const float validRadius = 1.0f;
        const float nullHeight = 0.0f;
        const float validHeight = 1.0f;

        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetCylinderRadius, validRadius);

        {
            UnitTest::ErrorHandler dimensionErrorHandler("SetCylinderHeight: height must be greater than zero.");

            PhysX::EditorPrimitiveColliderComponentRequestBus::Event(idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetCylinderHeight, nullHeight);

            EXPECT_EQ(dimensionErrorHandler.GetExpectedErrorCount(), 1);
        }

        {
            UnitTest::ErrorHandler dimensionErrorHandler("SetCylinderHeight: height must be greater than zero.");

            PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
                idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetCylinderHeight, validHeight);

            EXPECT_EQ(dimensionErrorHandler.GetExpectedErrorCount(), 0);
        }
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithBoxAndRigidBody_CorrectRuntimeComponents)
    {
        // create an editor entity with a collider component
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorRigidBodyComponent>();
        editorEntity->Activate();

        // Set collider to be a box
        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Box);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        // check that the runtime entity has the expected components
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BoxColliderComponent>() != nullptr);
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::RigidBodyComponent>() != nullptr);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithBoxAndRigidBody_CorrectRuntimeEntity)
    {
        // create an editor entity with a collider component
        const AZ::Vector3 boxDimensions(2.0f, 3.0f, 4.0f);
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            boxDimensions,
            AZ::Transform::CreateIdentity(),
            AZ::Vector3::CreateZero(),
            AZ::Quaternion::CreateIdentity(),
            AZStd::nullopt,
            RigidBodyType::Dynamic);

        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());

        // since there was an editor rigid body component, the runtime entity should have a dynamic rigid body
        const auto* rigidBody = gameEntity->FindComponent<PhysX::RigidBodyComponent>()->GetRigidBody();
        const auto* pxRigidDynamic = static_cast<const physx::PxRigidDynamic*>(rigidBody->GetNativePointer());

        PHYSX_SCENE_READ_LOCK(pxRigidDynamic->getScene());

        // there should be a single shape on the rigid body and it should be a box
        EXPECT_EQ(pxRigidDynamic->getNbShapes(), 1);
        if (pxRigidDynamic->getNbShapes() > 0)
        {
            physx::PxShape* shape = nullptr;
            pxRigidDynamic->getShapes(&shape, 1, 0);
            EXPECT_EQ(shape->getGeometryType(), physx::PxGeometryType::eBOX);
        }

        // the bounding box of the rigid body should reflect the dimensions of the box set above
        AZ::Aabb aabb = rigidBody->GetAabb();
        EXPECT_TRUE(aabb.GetMax().IsClose(0.5f * boxDimensions));
        EXPECT_TRUE(aabb.GetMin().IsClose(-0.5f * boxDimensions));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_TransformChanged_ColliderUpdated)
    {
        // create an editor entity with a collider component
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();
        
        // Set collider to be a box
        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Box);

        AZ::Vector3 boxDimensions = AZ::Vector3::CreateOne();
        PhysX::EditorPrimitiveColliderComponentRequestBus::EventResult(
            boxDimensions, idPair, &PhysX::EditorPrimitiveColliderComponentRequests::GetBoxDimensions);

        AZ::EntityId editorEntityId = editorEntity->GetId();

        // update the transform
        const float scale = 2.0f;
        AZ::TransformBus::Event(editorEntityId, &AZ::TransformInterface::SetLocalUniformScale, scale);
        const AZ::Vector3 translation(10.0f, 20.0f, 30.0f);
        AZ::TransformBus::Event(editorEntityId, &AZ::TransformInterface::SetWorldTranslation, translation);

        // make a game entity and check its bounding box is consistent with the changed transform
        EntityPtr gameEntity = CreateActiveGameEntityFromEditorEntity(editorEntity.get());
        const auto* staticBody = azdynamic_cast<PhysX::StaticRigidBody*>(gameEntity->FindComponent<PhysX::StaticRigidBodyComponent>()->GetSimulatedBody());
        AZ::Aabb aabb = staticBody->GetAabb();
        EXPECT_TRUE(aabb.GetMax().IsClose(translation + 0.5f * scale * boxDimensions));
        EXPECT_TRUE(aabb.GetMin().IsClose(translation - 0.5f * scale * boxDimensions));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_ColliderWithScaleSetToParentEntity_CorrectRuntimeScale)
    {
        // create an editor parent entity (empty, need transform component only)
        EntityPtr editorParentEntity = CreateInactiveEditorEntity("ParentEntity");
        editorParentEntity->Activate();

        // set some scale to parent entity
        const float parentScale = 2.0f;
        AZ::TransformBus::Event(editorParentEntity->GetId(), &AZ::TransformInterface::SetLocalUniformScale, parentScale);

        // create an editor child entity with a collider component
        EntityPtr editorChildEntity = CreateInactiveEditorEntity("ChildEntity");
        auto* colliderComponent = editorChildEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorChildEntity->CreateComponent<PhysX::EditorRigidBodyComponent>();
        editorChildEntity->Activate();

        // Set collider to be a box
        AZ::EntityComponentIdPair idPair(editorChildEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Box);

        // set some dimensions to child entity box component
        const AZ::Vector3 boxDimensions(2.0f, 3.0f, 4.0f);
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetBoxDimensions, boxDimensions);

        // set one entity as parent of another
        AZ::TransformBus::Event(editorChildEntity->GetId(),
            &AZ::TransformBus::Events::SetParentRelative,
            editorParentEntity->GetId());

        // build child game entity (parent will be built implicitly)
        EntityPtr gameChildEntity = CreateActiveGameEntityFromEditorEntity(editorChildEntity.get());

        // since there was an editor rigid body component, the runtime entity should have a dynamic rigid body
        const AzPhysics::RigidBody* rigidBody =
            gameChildEntity->FindComponent<PhysX::RigidBodyComponent>()->GetRigidBody();
        
        // the bounding box of the rigid body should reflect the dimensions of the box set above
        // and parent entity scale
        const AZ::Aabb aabb = rigidBody->GetAabb();
        EXPECT_THAT(aabb.GetMax(), UnitTest::IsClose(0.5f * boxDimensions * parentScale));
        EXPECT_THAT(aabb.GetMin(), UnitTest::IsClose(-0.5f * boxDimensions * parentScale));
    }

    // The flag a collider saves for a non-uniform scale is only refreshed when the collider activates, and a scale that crosses
    // between uniform and non-uniform re-activates the entity on the next tick, after the prefab data was taken. The game entity
    // must therefore follow the transform of the entity and not the saved flag.
    TEST_F(PhysXEditorFixture, EditorColliderComponent_NonUniformTransformWithSavedFlagFalse_BuildsScaledPrimitive)
    {
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();

        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Box);

        // The scale crosses to non-uniform, and the collider has not heard of it, so its saved flag is still false.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(2.0f, 1.0f, 1.0f));
        ASSERT_FALSE(colliderComponent->GetShapeConfiguration().m_hasNonUniformScale);

        EntityPtr gameEntity = CreateInactiveGameEntityFromEditorEntity(editorEntity.get());

        // A box cannot take a non-uniform scale, so it becomes a convex mesh in a base collider and no box collider.
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BoxColliderComponent>() == nullptr);
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BaseColliderComponent>() != nullptr);
        EXPECT_TRUE(colliderComponent->GetShapeConfiguration().m_hasNonUniformScale);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_UniformTransformWithSavedFlagTrue_BuildsBoxCollider)
    {
        EntityPtr editorEntity = CreateInactiveEditorEntity("ColliderComponentEditorEntity");
        auto* colliderComponent = editorEntity->CreateComponent<PhysX::EditorColliderComponent>();
        editorEntity->CreateComponent<PhysX::EditorStaticRigidBodyComponent>();
        editorEntity->Activate();

        AZ::EntityComponentIdPair idPair(editorEntity->GetId(), colliderComponent->GetId());
        PhysX::EditorPrimitiveColliderComponentRequestBus::Event(
            idPair, &PhysX::EditorPrimitiveColliderComponentRequests::SetShapeType, Physics::ShapeType::Box);

        // Cycle the activation of the entity at a non-uniform scale, as the queued re-activation does, so the collider saves true.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(2.0f, 1.0f, 1.0f));
        editorEntity->Deactivate();
        editorEntity->Activate();
        ASSERT_TRUE(colliderComponent->GetShapeConfiguration().m_hasNonUniformScale);

        // The scale crosses back to uniform, and the collider has not heard of it, so its saved flag is still true.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(1.0f));
        ASSERT_TRUE(colliderComponent->GetShapeConfiguration().m_hasNonUniformScale);

        EntityPtr gameEntity = CreateInactiveGameEntityFromEditorEntity(editorEntity.get());

        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BoxColliderComponent>() != nullptr);
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BaseColliderComponent>() == nullptr);
        EXPECT_FALSE(colliderComponent->GetShapeConfiguration().m_hasNonUniformScale);
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_NonUniformScaleComponent_BuildsScaledPrimitive)
    {
        // The old Non-uniform Scale component still counts, whatever its collider saved.
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            AZ::Vector3(2.0f, 3.0f, 4.0f),
            AZ::Transform::CreateIdentity(),
            AZ::Vector3::CreateZero(),
            AZ::Quaternion::CreateIdentity(),
            AZ::Vector3(2.0f, 1.0f, 1.0f));

        EntityPtr gameEntity = CreateInactiveGameEntityFromEditorEntity(editorEntity.get());

        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BoxColliderComponent>() == nullptr);
        EXPECT_TRUE(gameEntity->FindComponent<PhysX::BaseColliderComponent>() != nullptr);
    }

    //////////////////////////////////////////////////////////////////////////
    // Picking and bounds of a flipped entity
    //
    // The editor body of a collider stays on the authored side of a flipped entity, and its wireframe draws mirrored. The pick
    // and the selection bounds of the editor follow the wireframe, so the mirrored side selects and the authored side does not.

    // Whether the editor pick of an entity hits it along a ray, and where. The pick runs the selection bounds first and then
    // the ray test of every component of the entity, as a click in the viewport does.
    static bool PickEntityAlongRay(AZ::EntityId entityId, const AZ::Vector3& origin, const AZ::Vector3& direction, float& distance)
    {
        return AzToolsFramework::PickEntity(entityId, origin, direction, distance, 0);
    }

    // A ray that runs along Y at the given X, from five units before the Y of the entity.
    static bool PickEntityAlongY(AZ::EntityId entityId, float x, float& distance)
    {
        return PickEntityAlongRay(entityId, AZ::Vector3(x, -5.0f, 0.0f), AZ::Vector3::CreateAxisY(), distance);
    }

    static AZ::Vector3 GetSelectionBoundsCenter(AZ::EntityId entityId)
    {
        return AzToolsFramework::CalculateEditorEntitySelectionBounds(entityId, AzFramework::ViewportInfo{ 0 }).GetCenter();
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_FlippedTransform_PickAndSelectionBoundsFollowTheMirroredSide)
    {
        // A box one unit wide with its centre offset to X = 1. Flipped in X, it is drawn with its centre at X = -1.
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            AZ::Vector3(1.0f, 1.0f, 1.0f), AZ::Transform::CreateIdentity(), AZ::Vector3(1.0f, 0.0f, 0.0f));
        const AZ::EntityId entityId = editorEntity->GetId();
        float distance = 0.0f;

        EXPECT_TRUE(PickEntityAlongY(entityId, 1.0f, distance));
        EXPECT_NEAR(distance, 4.5f, 1e-3f);
        EXPECT_FALSE(PickEntityAlongY(entityId, -1.0f, distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-3f));

        // The flip fires no transform event, so the body is not rebuilt. The pick maps onto it, and the distance is the one
        // along the ray that came in.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(PickEntityAlongY(entityId, -1.0f, distance));
        EXPECT_NEAR(distance, 4.5f, 1e-3f);
        EXPECT_FALSE(PickEntityAlongY(entityId, 1.0f, distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(-1.0f, 0.0f, 0.0f), 1e-3f));

        // Back at one, the authored side picks again.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(PickEntityAlongY(entityId, 1.0f, distance));
        EXPECT_FALSE(PickEntityAlongY(entityId, -1.0f, distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-3f));
    }

    TEST_F(PhysXEditorFixture, EditorColliderComponent_FlippedTransformAndRotatedEntity_PickAndSelectionBoundsMirrorAboutTheLocalPlane)
    {
        // A quarter turn about Z puts local X on world Y, so the box offset to local X = 1 sits at world Y = 1.
        // Flipping local X draws it at world Y = -1.
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            AZ::Vector3(1.0f, 1.0f, 1.0f),
            AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi), AZ::Vector3::CreateZero()),
            AZ::Vector3(1.0f, 0.0f, 0.0f));
        const AZ::EntityId entityId = editorEntity->GetId();
        float distance = 0.0f;

        EXPECT_TRUE(PickEntityAlongRay(entityId, AZ::Vector3(-5.0f, 1.0f, 0.0f), AZ::Vector3::CreateAxisX(), distance));

        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(PickEntityAlongRay(entityId, AZ::Vector3(-5.0f, -1.0f, 0.0f), AZ::Vector3::CreateAxisX(), distance));
        EXPECT_NEAR(distance, 4.5f, 1e-3f);
        EXPECT_FALSE(PickEntityAlongRay(entityId, AZ::Vector3(-5.0f, 1.0f, 0.0f), AZ::Vector3::CreateAxisX(), distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(0.0f, -1.0f, 0.0f), 1e-3f));
    }

    TEST_F(PhysXEditorFixture, EditorRigidBodyComponent_FlippedTransform_PickAndSelectionBoundsFollowTheMirroredSide)
    {
        // The same with a dynamic rigid body, whose editor body is the one that answers.
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(
            AZ::Vector3(1.0f, 1.0f, 1.0f),
            AZ::Transform::CreateIdentity(),
            AZ::Vector3(1.0f, 0.0f, 0.0f),
            AZ::Quaternion::CreateIdentity(),
            AZStd::nullopt,
            RigidBodyType::Dynamic);
        const AZ::EntityId entityId = editorEntity->GetId();
        float distance = 0.0f;

        EXPECT_TRUE(PickEntityAlongY(entityId, 1.0f, distance));
        EXPECT_FALSE(PickEntityAlongY(entityId, -1.0f, distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-3f));

        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(PickEntityAlongY(entityId, -1.0f, distance));
        EXPECT_NEAR(distance, 4.5f, 1e-3f);
        EXPECT_FALSE(PickEntityAlongY(entityId, 1.0f, distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(-1.0f, 0.0f, 0.0f), 1e-3f));

        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(PickEntityAlongY(entityId, 1.0f, distance));
        EXPECT_FALSE(PickEntityAlongY(entityId, -1.0f, distance));
        EXPECT_THAT(GetSelectionBoundsCenter(entityId), UnitTest::IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-3f));
    }

    // Stands in for the visibility bounds system of the test application while it lives, and counts the refreshes that are
    // asked for one entity.
    class CountingBoundsUnion
        : public AzFramework::IEntityBoundsUnion
    {
    public:
        explicit CountingBoundsUnion(AZ::EntityId entityId)
            : m_entityId(entityId)
            , m_runningSystem(AZ::Interface<AzFramework::IEntityBoundsUnion>::Get())
        {
            if (m_runningSystem)
            {
                AZ::Interface<AzFramework::IEntityBoundsUnion>::Unregister(m_runningSystem);
            }
            AZ::Interface<AzFramework::IEntityBoundsUnion>::Register(this);
        }

        virtual ~CountingBoundsUnion()
        {
            AZ::Interface<AzFramework::IEntityBoundsUnion>::Unregister(this);
            if (m_runningSystem)
            {
                AZ::Interface<AzFramework::IEntityBoundsUnion>::Register(m_runningSystem);
            }
        }

        // AzFramework::IEntityBoundsUnion overrides...
        void RefreshEntityLocalBoundsUnion(AZ::EntityId entityId) override
        {
            if (entityId == m_entityId)
            {
                ++m_refreshCount;
            }
        }

        AZ::Aabb GetEntityLocalBoundsUnion([[maybe_unused]] AZ::EntityId entityId) const override
        {
            return AZ::Aabb::CreateNull();
        }

        AZ::Aabb GetEntityWorldBoundsUnion([[maybe_unused]] AZ::EntityId entityId) const override
        {
            return AZ::Aabb::CreateNull();
        }

        void ProcessEntityBoundsUnionRequests() override
        {
        }

        void OnTransformUpdated([[maybe_unused]] AZ::Entity* entity) override
        {
        }

        AZ::EntityId m_entityId;
        AzFramework::IEntityBoundsUnion* m_runningSystem = nullptr;
        int m_refreshCount = 0;
    };

    TEST_F(PhysXEditorFixture, EditorColliderComponent_FlipSignsChange_RefreshesTheVisibilityBoundsOncePerChange)
    {
        EntityPtr editorEntity = CreateBoxPrimitiveColliderEditorEntity(AZ::Vector3(1.0f, 1.0f, 1.0f));
        CountingBoundsUnion boundsUnion(editorEntity->GetId());

        // A pure flip fires no transform event, so the collider bounds of the entity would stay on the authored side.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_EQ(boundsUnion.m_refreshCount, 1);

        // The same signs at another size are not a change of the flip.
        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(-2.0f, 2.0f, 2.0f));
        EXPECT_EQ(boundsUnion.m_refreshCount, 1);

        SetEditorTransformScale(editorEntity.get(), AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_EQ(boundsUnion.m_refreshCount, 2);
    }

    //////////////////////////////////////////////////////////////////////////
    // The mirror helpers

    namespace
    {
        AZ::Vector3 SignsFromMask(AZ::u32 mask)
        {
            return AZ::Vector3((mask & 1) ? -1.0f : 1.0f, (mask & 2) ? -1.0f : 1.0f, (mask & 4) ? -1.0f : 1.0f);
        }
    } // namespace

    TEST(Transform2MirrorTest, MirrorWorldPoint_UnrotatedEntity_NegatesTheFlippedAxesAboutThePosition)
    {
        const AZ::Transform world = AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, 20.0f, 30.0f));
        const AZ::Vector3 point(11.0f, 22.0f, 33.0f);

        EXPECT_THAT(
            PhysX::Transform2Mirror::MirrorWorldPoint(world, SignsFromMask(1), point),
            UnitTest::IsCloseTolerance(AZ::Vector3(9.0f, 22.0f, 33.0f), 1e-4f));
        EXPECT_THAT(
            PhysX::Transform2Mirror::MirrorWorldPoint(world, SignsFromMask(5), point),
            UnitTest::IsCloseTolerance(AZ::Vector3(9.0f, 22.0f, 27.0f), 1e-4f));

        // No flip returns the point as it is.
        EXPECT_THAT(PhysX::Transform2Mirror::MirrorWorldPoint(world, SignsFromMask(0), point), UnitTest::IsCloseTolerance(point, 1e-6f));
    }

    TEST(Transform2MirrorTest, MirrorWorldPoint_RotatedAndScaledEntity_MirrorsAboutThePlaneOfTheFlippedLocalAxis)
    {
        // A quarter turn about Z puts local X on world Y, so flipping X mirrors world Y about the position. The uniform
        // scale of the entity cancels out of the mirror.
        AZ::Transform world = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi), AZ::Vector3(1.0f, 2.0f, 3.0f));
        world.SetUniformScale(3.0f);

        EXPECT_THAT(
            PhysX::Transform2Mirror::MirrorWorldPoint(world, SignsFromMask(1), AZ::Vector3(5.0f, 7.0f, 9.0f)),
            UnitTest::IsCloseTolerance(AZ::Vector3(5.0f, -3.0f, 9.0f), 1e-4f));
        EXPECT_THAT(
            PhysX::Transform2Mirror::MirrorWorldDirection(world, SignsFromMask(1), AZ::Vector3(0.0f, 1.0f, 0.0f)),
            UnitTest::IsCloseTolerance(AZ::Vector3(0.0f, -1.0f, 0.0f), 1e-4f));
    }

    TEST(Transform2MirrorTest, MirrorWorld_EverySignPattern_IsTheEntityMirrorThatKeepsDistances)
    {
        AZ::Transform world = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(0.7f) * AZ::Quaternion::CreateRotationY(0.5f) * AZ::Quaternion::CreateRotationX(0.3f),
            AZ::Vector3(1.0f, -2.0f, 3.0f));
        world.SetUniformScale(2.0f);

        const AZ::Vector3 pointA(4.0f, 5.0f, -6.0f);
        const AZ::Vector3 pointB(-2.0f, 0.5f, 7.0f);
        const AZ::Vector3 direction = AZ::Vector3(0.3f, -0.5f, 0.8f).GetNormalized();

        for (AZ::u32 mask = 1; mask < 8; ++mask)
        {
            const AZ::Vector3 signs = SignsFromMask(mask);
            const AZ::Vector3 mirroredA = PhysX::Transform2Mirror::MirrorWorldPoint(world, signs, pointA);
            const AZ::Vector3 mirroredB = PhysX::Transform2Mirror::MirrorWorldPoint(world, signs, pointB);

            // W * diag(signs) * W^-1, written out.
            EXPECT_THAT(mirroredA, UnitTest::IsCloseTolerance(world.TransformPoint(signs * world.GetInverse().TransformPoint(pointA)), 1e-3f));

            // An isometry: distances between points and from the entity are kept, so a ray keeps its distances.
            EXPECT_NEAR(mirroredA.GetDistance(mirroredB), pointA.GetDistance(pointB), 1e-3f);
            EXPECT_NEAR(mirroredA.GetDistance(world.GetTranslation()), pointA.GetDistance(world.GetTranslation()), 1e-3f);

            // Mirroring twice returns where it started.
            EXPECT_THAT(PhysX::Transform2Mirror::MirrorWorldPoint(world, signs, mirroredA), UnitTest::IsCloseTolerance(pointA, 1e-3f));

            const AZ::Vector3 mirroredDirection = PhysX::Transform2Mirror::MirrorWorldDirection(world, signs, direction);
            EXPECT_NEAR(mirroredDirection.GetLength(), 1.0f, 1e-4f);
            EXPECT_THAT(
                PhysX::Transform2Mirror::MirrorWorldDirection(world, signs, mirroredDirection), UnitTest::IsCloseTolerance(direction, 1e-4f));
        }
    }

    TEST(Transform2MirrorTest, MirrorWorldAabb_UnrotatedEntity_ReflectsTheBoxAboutThePosition)
    {
        const AZ::Transform world = AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f));
        const AZ::Aabb box = AZ::Aabb::CreateFromMinMax(AZ::Vector3(11.0f, -1.0f, 2.0f), AZ::Vector3(13.0f, 1.0f, 3.0f));

        // X runs from 1 to 3 away from the position, so it runs from -3 to -1 once mirrored.
        const AZ::Aabb flippedX = PhysX::Transform2Mirror::MirrorWorldAabb(world, SignsFromMask(1), box);
        EXPECT_THAT(flippedX.GetMin(), UnitTest::IsCloseTolerance(AZ::Vector3(7.0f, -1.0f, 2.0f), 1e-4f));
        EXPECT_THAT(flippedX.GetMax(), UnitTest::IsCloseTolerance(AZ::Vector3(9.0f, 1.0f, 3.0f), 1e-4f));

        const AZ::Aabb flippedXZ = PhysX::Transform2Mirror::MirrorWorldAabb(world, SignsFromMask(5), box);
        EXPECT_THAT(flippedXZ.GetMin(), UnitTest::IsCloseTolerance(AZ::Vector3(7.0f, -1.0f, -3.0f), 1e-4f));
        EXPECT_THAT(flippedXZ.GetMax(), UnitTest::IsCloseTolerance(AZ::Vector3(9.0f, 1.0f, -2.0f), 1e-4f));

        // No flip returns the box as it is, and a null box stays null.
        EXPECT_TRUE(PhysX::Transform2Mirror::MirrorWorldAabb(world, SignsFromMask(0), box).IsClose(box, 1e-6f));
        EXPECT_FALSE(PhysX::Transform2Mirror::MirrorWorldAabb(world, SignsFromMask(1), AZ::Aabb::CreateNull()).IsValid());
    }

    TEST(Transform2MirrorTest, MirrorWorldAabb_RotatedEntity_ReflectsAQuarterTurnExactlyAndContainsTheMirroredCorners)
    {
        // A quarter turn about Z puts local X on world Y, so flipping X turns Y into -Y.
        const AZ::Transform quarterTurn =
            AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi), AZ::Vector3::CreateZero());
        const AZ::Aabb box = AZ::Aabb::CreateFromMinMax(AZ::Vector3(1.0f, 4.0f, 0.0f), AZ::Vector3(3.0f, 6.0f, 1.0f));

        const AZ::Aabb mirrored = PhysX::Transform2Mirror::MirrorWorldAabb(quarterTurn, SignsFromMask(1), box);
        EXPECT_THAT(mirrored.GetMin(), UnitTest::IsCloseTolerance(AZ::Vector3(1.0f, -6.0f, 0.0f), 1e-4f));
        EXPECT_THAT(mirrored.GetMax(), UnitTest::IsCloseTolerance(AZ::Vector3(3.0f, -4.0f, 1.0f), 1e-4f));

        // At any other angle the box is the box of the mirrored corners, so it holds every one of them.
        const AZ::Transform world = AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateRotationZ(0.7f) * AZ::Quaternion::CreateRotationY(0.5f), AZ::Vector3(1.0f, -2.0f, 3.0f));
        const AZ::Vector3 signs = SignsFromMask(3);
        const AZ::Aabb mirroredBox = PhysX::Transform2Mirror::MirrorWorldAabb(world, signs, box).GetExpanded(AZ::Vector3(1e-3f));
        for (AZ::u32 corner = 0; corner < 8; ++corner)
        {
            const AZ::Vector3 point(
                (corner & 1) ? box.GetMax().GetX() : box.GetMin().GetX(),
                (corner & 2) ? box.GetMax().GetY() : box.GetMin().GetY(),
                (corner & 4) ? box.GetMax().GetZ() : box.GetMin().GetZ());
            EXPECT_TRUE(mirroredBox.Contains(PhysX::Transform2Mirror::MirrorWorldPoint(world, signs, point)));
        }
    }
} // namespace PhysXEditorTests
