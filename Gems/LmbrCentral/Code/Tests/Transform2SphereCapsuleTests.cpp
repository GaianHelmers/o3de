/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/containers/vector.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/UnitTest/TestDebugDisplayRequests.h>
#include <AzTest/AzTest.h>
#include <Geometry/GeometrySystemComponent.h>
#include <Shape/CapsuleShapeComponent.h>
#include <Shape/SphereShapeComponent.h>
#include <ShapeTestUtils.h>

// A Sphere and a Capsule on an entity with a negative scale axis answer, draw and report on the mirrored side of the entity. The
// radius and the height are never negated, and the authored offset is never written. The Box half of this is in Transform2ShapeTests.

// In a namespace of its own: a unity build puts this file beside Transform2ShapeTests, whose file-local helpers share these names.
namespace UnitTest::Transform2SphereCapsule
{
    namespace
    {
        constexpr AZ::u8 FlipMaskX = 1;
        constexpr AZ::u8 FlipMaskY = 2;
        constexpr AZ::u8 FlipMaskZ = 4;

        // Counts the shape changes of an entity, which is how everything downstream of a shape hears of a flip.
        class ShapeChangedCounter
            : public LmbrCentral::ShapeComponentNotificationsBus::Handler
        {
        public:
            explicit ShapeChangedCounter(AZ::EntityId entityId)
            {
                LmbrCentral::ShapeComponentNotificationsBus::Handler::BusConnect(entityId);
            }

            ~ShapeChangedCounter()
            {
                LmbrCentral::ShapeComponentNotificationsBus::Handler::BusDisconnect();
            }

            void OnShapeChanged(LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons changeReason) override
            {
                if (changeReason == LmbrCentral::ShapeComponentNotifications::ShapeChangeReasons::ShapeChanged)
                {
                    ++m_count;
                }
            }

            int m_count = 0;
        };

        void PushFlipMask(AZ::EntityId entityId, AZ::u8 flipMask)
        {
            LmbrCentral::ShapeComponentRequestsBus::Event(entityId, &LmbrCentral::ShapeComponentRequests::SetFlipMask, flipMask);
        }

        void SetWorld(AZ::EntityId entityId, const AZ::Transform& world)
        {
            AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetWorldTM, world);
        }

        // Rotated, moved and scaled, so the mirror is checked in the local axes of the entity and not in the world's. The rotation is
        // about Z, so the local Z of the entity is the world Z and a capsule stands upright.
        AZ::Transform GetRotatedWorld()
        {
            return AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateRotationZ(0.6f), 1.5f);
        }

        // The box the debug draw of a shape fits into.
        AZ::Aabb GetDrawnBounds(AZ::EntityId entityId)
        {
            TestDebugDisplayRequests testDebugDisplayRequests;
            AzFramework::EntityDebugDisplayEventBus::Event(
                entityId, &AzFramework::EntityDebugDisplayEvents::DisplayEntityViewport, AzFramework::ViewportInfo{ 0 },
                testDebugDisplayRequests);
            return testDebugDisplayRequests.GetAabb();
        }
    } // namespace

    class Transform2SphereCapsuleTest
        : public LeakDetectionFixture
    {
    protected:
        void SetUp() override
        {
            LeakDetectionFixture::SetUp();
            m_serializeContext = AZStd::make_unique<AZ::SerializeContext>();

            RegisterDescriptor(AzFramework::TransformComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::SphereShapeComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::SphereShapeDebugDisplayComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::CapsuleShapeComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::CapsuleShapeDebugDisplayComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::GeometrySystemComponent::CreateDescriptor());
        }

        void TearDown() override
        {
            m_descriptors.clear();
            m_serializeContext.reset();
            LeakDetectionFixture::TearDown();
        }

        void RegisterDescriptor(AZ::ComponentDescriptor* descriptor)
        {
            descriptor->Reflect(m_serializeContext.get());
            m_descriptors.emplace_back(descriptor);
        }

        // A sphere shape on an entity with a Transform 1. The entity is initialized, and not activated.
        static void AddSphere(AZ::Entity& entity, float radius, const AZ::Vector3& translationOffset)
        {
            LmbrCentral::SphereShapeConfig config(radius);
            config.m_translationOffset = translationOffset;
            entity.CreateComponent<LmbrCentral::SphereShapeComponent>()->SetConfiguration(config);
            entity.CreateComponent<AzFramework::TransformComponent>();
            entity.Init();
        }

        // A capsule shape on an entity with a Transform 1. The entity is initialized, and not activated.
        static void AddCapsule(AZ::Entity& entity, float radius, float height, const AZ::Vector3& translationOffset)
        {
            LmbrCentral::CapsuleShapeConfig config;
            config.m_radius = radius;
            config.m_height = height;
            config.m_translationOffset = translationOffset;
            entity.CreateComponent<LmbrCentral::CapsuleShapeComponent>()->SetConfiguration(config);
            entity.CreateComponent<AzFramework::TransformComponent>();
            entity.Init();
        }

        // What the shape answers with has to move, and what it was authored with has to stay. The authored bounds are the bounds of the
        // shape as saved data sees them, the others are the bounds of the shape as drawn.
        static void ExpectOffsetAnswers(
            AZ::EntityId entityId, const AZ::Vector3& offset, AZ::u8 flipMask, const AZ::Transform& world, const AZ::Vector3& localExtents)
        {
            const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(flipMask);

            AZ::Vector3 authoredOffset = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                authoredOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetTranslationOffset);
            EXPECT_THAT(authoredOffset, IsClose(offset));

            AZ::Vector3 effectiveOffset = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                effectiveOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetEffectiveTranslationOffset);
            EXPECT_THAT(effectiveOffset, IsClose(signs * offset));

            AZ::Vector3 flipSigns = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
            EXPECT_THAT(flipSigns, IsClose(signs));

            // The bounds of the drawn shape are the mirrored bounds with positive extents, and the transform puts them on the mirrored
            // center.
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            AZ::Aabb localBounds = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndLocalBounds, transform, localBounds);
            EXPECT_THAT(localBounds.GetCenter(), IsClose(signs * offset));
            EXPECT_THAT(localBounds.GetExtents(), IsClose(localExtents));
            EXPECT_THAT(transform.TransformPoint(localBounds.GetCenter()), IsClose(world.TransformPoint(signs * offset)));

            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndAuthoredLocalBounds, transform, localBounds);
            EXPECT_THAT(localBounds.GetCenter(), IsClose(offset));
            EXPECT_THAT(localBounds.GetExtents(), IsClose(localExtents));
            EXPECT_THAT(transform.TransformPoint(localBounds.GetCenter()), IsClose(world.TransformPoint(offset)));
        }

        // A ray that runs along the world X towards the point from 10 units away.
        static bool IntersectRayAlongX(AZ::EntityId entityId, const AZ::Vector3& target, float& distance)
        {
            bool hit = false;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                hit, entityId, &LmbrCentral::ShapeComponentRequests::IntersectRay, target + AZ::Vector3(10.0f, 0.0f, 0.0f),
                -AZ::Vector3::CreateAxisX(), distance);
            return hit;
        }

        // The offset is (2,0,0), so a flip of X moves the shape and a flip of Y or Z does not. Only a shape that moved tells anyone.
        static void ExpectOnlyAFlipOfTheOffsetRaisesShapeChange(AZ::EntityId entityId)
        {
            ShapeChangedCounter counter(entityId);

            PushFlipMask(entityId, FlipMaskY | FlipMaskZ);
            EXPECT_EQ(counter.m_count, 0);

            PushFlipMask(entityId, FlipMaskX | FlipMaskY | FlipMaskZ);
            EXPECT_EQ(counter.m_count, 1);

            // The same mask again moves nothing, and taking it off moves the shape back.
            PushFlipMask(entityId, FlipMaskX | FlipMaskY | FlipMaskZ);
            EXPECT_EQ(counter.m_count, 1);

            PushFlipMask(entityId, 0);
            EXPECT_EQ(counter.m_count, 2);
        }

        // A shape at the center of its entity is where it was under any flip, so nothing downstream is told.
        static void ExpectNoShapeChangeWithoutOffset(AZ::EntityId entityId)
        {
            ShapeChangedCounter counter(entityId);

            PushFlipMask(entityId, FlipMaskX);
            PushFlipMask(entityId, FlipMaskX | FlipMaskY | FlipMaskZ);
            PushFlipMask(entityId, 0);

            EXPECT_EQ(counter.m_count, 0);
        }

        // Nobody pushes a flip after the entity activates again, so a flip that survived the activation would stay.
        static void ExpectDeactivatingResetsTheFlip(AZ::Entity& entity, const AZ::Vector3& offset)
        {
            const AZ::Transform world = GetRotatedWorld();
            const AZ::Vector3 authoredCenter = world.TransformPoint(offset);
            const AZ::Vector3 flippedCenter = world.TransformPoint(AZ::Vector3(-offset.GetX(), offset.GetY(), offset.GetZ()));

            entity.Activate();
            SetWorld(entity.GetId(), world);

            PushFlipMask(entity.GetId(), FlipMaskX);
            EXPECT_TRUE(IsPointInside(entity, flippedCenter));

            entity.Deactivate();
            entity.Activate();
            SetWorld(entity.GetId(), world);

            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));
        }

        // The offset is (2,0,1), so a flip of X, Z or both moves the sphere, and a flip of Y alone would not. The sphere has a radius of
        // 1, which the scale of the entity makes 1.5.
        void ExpectSphereFollowsFlip(AZ::u8 flipMask)
        {
            const float radius = 1.0f;
            const float worldRadius = radius * 1.5f; // the scale of the entity
            const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
            const AZ::Transform world = GetRotatedWorld();
            const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(flipMask);
            const AZ::Vector3 authoredCenter = world.TransformPoint(offset);
            const AZ::Vector3 flippedCenter = world.TransformPoint(signs * offset);

            AZ::Entity entity;
            AddSphere(entity, radius, offset);
            entity.Activate();
            SetWorld(entity.GetId(), world);
            const AZ::EntityId entityId = entity.GetId();

            // Nothing is flipped yet.
            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));

            PushFlipMask(entityId, flipMask);

            EXPECT_TRUE(IsPointInside(entity, flippedCenter));
            EXPECT_FALSE(IsPointInside(entity, authoredCenter));

            // A point half a unit above the surface of the sphere as drawn.
            float distanceSquared = 0.0f;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                distanceSquared, entityId, &LmbrCentral::ShapeComponentRequests::DistanceSquaredFromPoint,
                flippedCenter + AZ::Vector3(0.0f, 0.0f, worldRadius + 0.5f));
            EXPECT_NEAR(distanceSquared, 0.25f, 1e-3f);

            // A ray towards the center of the sphere as drawn hits the near side, and a ray towards where it was misses.
            float distance = 0.0f;
            EXPECT_TRUE(IntersectRayAlongX(entityId, flippedCenter, distance));
            EXPECT_NEAR(distance, 10.0f - worldRadius, 1e-3f);
            EXPECT_FALSE(IntersectRayAlongX(entityId, authoredCenter, distance));

            AZ::Aabb encompassingAabb = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                encompassingAabb, entityId, &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
            EXPECT_THAT(encompassingAabb.GetCenter(), IsClose(flippedCenter));
            EXPECT_THAT(encompassingAabb.GetExtents(), IsClose(AZ::Vector3(2.0f * worldRadius)));

            ExpectOffsetAnswers(entityId, offset, flipMask, world, AZ::Vector3(2.0f * radius));

            // The configuration the component saves is as authored.
            LmbrCentral::SphereShapeConfig configuration;
            LmbrCentral::SphereShapeComponentRequestsBus::EventResult(
                configuration, entityId, &LmbrCentral::SphereShapeComponentRequests::GetSphereConfiguration);
            EXPECT_THAT(configuration.m_translationOffset, IsClose(offset));
            EXPECT_FLOAT_EQ(configuration.m_radius, radius);

            // Taking the flip off restores the authored answers.
            PushFlipMask(entityId, 0);

            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));
            ExpectOffsetAnswers(entityId, offset, 0, world, AZ::Vector3(2.0f * radius));
        }

        // The capsule is upright in the world, 3 units tall with a radius of half a unit. Scaled by the entity it is 4.5 tall with a
        // radius of 0.75, and the centers of its end caps are 1.5 above and below its center.
        void ExpectCapsuleFollowsFlip(AZ::u8 flipMask)
        {
            const float radius = 0.5f;
            const float height = 3.0f;
            const float worldRadius = radius * 1.5f; // the scale of the entity
            const float worldHalfSegment = (height - 2.0f * radius) * 0.5f * 1.5f;
            const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
            const AZ::Transform world = GetRotatedWorld();
            const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(flipMask);
            const AZ::Vector3 authoredCenter = world.TransformPoint(offset);
            const AZ::Vector3 flippedCenter = world.TransformPoint(signs * offset);
            const AZ::Vector3 halfSegment(0.0f, 0.0f, worldHalfSegment);

            AZ::Entity entity;
            AddCapsule(entity, radius, height, offset);
            entity.Activate();
            SetWorld(entity.GetId(), world);
            const AZ::EntityId entityId = entity.GetId();

            // Nothing is flipped yet.
            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));

            PushFlipMask(entityId, flipMask);

            EXPECT_TRUE(IsPointInside(entity, flippedCenter));
            EXPECT_FALSE(IsPointInside(entity, authoredCenter));

            // The end points of the capsule are the same set about the mirrored center, with the base below and the top above.
            LmbrCentral::CapsuleInternalEndPoints endPoints{ AZ::Vector3::CreateZero(), AZ::Vector3::CreateZero() };
            LmbrCentral::CapsuleShapeComponentRequestsBus::EventResult(
                endPoints, entityId, &LmbrCentral::CapsuleShapeComponentRequests::GetCapsulePoints);
            EXPECT_THAT(endPoints.m_begin, IsClose(flippedCenter - halfSegment));
            EXPECT_THAT(endPoints.m_end, IsClose(flippedCenter + halfSegment));

            // A point beside the cylinder and a point above the cap, both as drawn.
            float distanceSquared = 0.0f;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                distanceSquared, entityId, &LmbrCentral::ShapeComponentRequests::DistanceSquaredFromPoint,
                flippedCenter + AZ::Vector3(3.0f, 0.0f, 0.0f));
            EXPECT_NEAR(distanceSquared, (3.0f - worldRadius) * (3.0f - worldRadius), 1e-3f);

            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                distanceSquared, entityId, &LmbrCentral::ShapeComponentRequests::DistanceSquaredFromPoint,
                flippedCenter + AZ::Vector3(0.0f, 0.0f, worldHalfSegment + worldRadius + 1.0f));
            EXPECT_NEAR(distanceSquared, 1.0f, 1e-3f);

            // A ray towards the center of the capsule as drawn hits its side, and a ray towards where it was misses.
            float distance = 0.0f;
            EXPECT_TRUE(IntersectRayAlongX(entityId, flippedCenter, distance));
            EXPECT_NEAR(distance, 10.0f - worldRadius, 1e-2f);
            EXPECT_FALSE(IntersectRayAlongX(entityId, authoredCenter, distance));

            AZ::Aabb encompassingAabb = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                encompassingAabb, entityId, &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
            EXPECT_THAT(encompassingAabb.GetCenter(), IsClose(flippedCenter));
            const float worldHalfHeight = worldHalfSegment + worldRadius;
            EXPECT_THAT(
                encompassingAabb.GetExtents(), IsClose(AZ::Vector3(2.0f * worldRadius, 2.0f * worldRadius, 2.0f * worldHalfHeight)));

            ExpectOffsetAnswers(entityId, offset, flipMask, world, AZ::Vector3(2.0f * radius, 2.0f * radius, height));

            // The configuration the component saves is as authored.
            LmbrCentral::CapsuleShapeConfig configuration;
            LmbrCentral::CapsuleShapeComponentRequestsBus::EventResult(
                configuration, entityId, &LmbrCentral::CapsuleShapeComponentRequests::GetCapsuleConfiguration);
            EXPECT_THAT(configuration.m_translationOffset, IsClose(offset));
            EXPECT_FLOAT_EQ(configuration.m_radius, radius);
            EXPECT_FLOAT_EQ(configuration.m_height, height);

            // Taking the flip off restores the authored answers.
            PushFlipMask(entityId, 0);

            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));
            ExpectOffsetAnswers(entityId, offset, 0, world, AZ::Vector3(2.0f * radius, 2.0f * radius, height));
        }

        AZStd::unique_ptr<AZ::SerializeContext> m_serializeContext;
        AZStd::vector<AZStd::unique_ptr<AZ::ComponentDescriptor>> m_descriptors;
    };

    //////////////////////////////////////////////////////////////////////////
    // Sphere queries

    TEST_F(Transform2SphereCapsuleTest, SphereFollowsAnXFlip)
    {
        ExpectSphereFollowsFlip(FlipMaskX);
    }

    TEST_F(Transform2SphereCapsuleTest, SphereFollowsAZFlip)
    {
        ExpectSphereFollowsFlip(FlipMaskZ);
    }

    TEST_F(Transform2SphereCapsuleTest, SphereFollowsAnXZFlip)
    {
        ExpectSphereFollowsFlip(FlipMaskX | FlipMaskZ);
    }

    TEST_F(Transform2SphereCapsuleTest, DeactivatingTheSphereResetsTheFlip)
    {
        AZ::Entity entity;
        AddSphere(entity, 1.0f, AZ::Vector3(2.0f, 0.0f, 1.0f));

        ExpectDeactivatingResetsTheFlip(entity, AZ::Vector3(2.0f, 0.0f, 1.0f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Sphere events

    TEST_F(Transform2SphereCapsuleTest, OnlyAFlipOfTheOffsetRaisesASphereShapeChange)
    {
        AZ::Entity entity;
        AddSphere(entity, 1.0f, AZ::Vector3(2.0f, 0.0f, 0.0f));
        entity.Activate();

        ExpectOnlyAFlipOfTheOffsetRaisesShapeChange(entity.GetId());
    }

    TEST_F(Transform2SphereCapsuleTest, FlipOfASphereWithoutOffsetRaisesNoShapeChange)
    {
        AZ::Entity entity;
        AddSphere(entity, 1.0f, AZ::Vector3::CreateZero());
        entity.Activate();

        ExpectNoShapeChangeWithoutOffset(entity.GetId());
    }

    //////////////////////////////////////////////////////////////////////////
    // Capsule queries

    TEST_F(Transform2SphereCapsuleTest, CapsuleFollowsAnXFlip)
    {
        ExpectCapsuleFollowsFlip(FlipMaskX);
    }

    TEST_F(Transform2SphereCapsuleTest, CapsuleFollowsAZFlip)
    {
        ExpectCapsuleFollowsFlip(FlipMaskZ);
    }

    TEST_F(Transform2SphereCapsuleTest, CapsuleFollowsAnXZFlip)
    {
        ExpectCapsuleFollowsFlip(FlipMaskX | FlipMaskZ);
    }

    // The axis of the capsule is its local Z. A flip of Z swaps the names of the base and the top, and the two end points are the
    // same set, so a capsule at the center of its entity reports the end points it reported before.
    TEST_F(Transform2SphereCapsuleTest, CapsuleEndPointsAreTheSameSetUnderAZFlip)
    {
        const AZ::Transform world = GetRotatedWorld();
        AZ::Entity entity;
        AddCapsule(entity, 0.5f, 3.0f, AZ::Vector3::CreateZero());
        entity.Activate();
        SetWorld(entity.GetId(), world);

        LmbrCentral::CapsuleInternalEndPoints authoredEndPoints{ AZ::Vector3::CreateZero(), AZ::Vector3::CreateZero() };
        LmbrCentral::CapsuleShapeComponentRequestsBus::EventResult(
            authoredEndPoints, entity.GetId(), &LmbrCentral::CapsuleShapeComponentRequests::GetCapsulePoints);

        PushFlipMask(entity.GetId(), FlipMaskZ);

        LmbrCentral::CapsuleInternalEndPoints flippedEndPoints{ AZ::Vector3::CreateZero(), AZ::Vector3::CreateZero() };
        LmbrCentral::CapsuleShapeComponentRequestsBus::EventResult(
            flippedEndPoints, entity.GetId(), &LmbrCentral::CapsuleShapeComponentRequests::GetCapsulePoints);

        EXPECT_THAT(flippedEndPoints.m_begin, IsClose(authoredEndPoints.m_begin));
        EXPECT_THAT(flippedEndPoints.m_end, IsClose(authoredEndPoints.m_end));
    }

    TEST_F(Transform2SphereCapsuleTest, DeactivatingTheCapsuleResetsTheFlip)
    {
        AZ::Entity entity;
        AddCapsule(entity, 0.5f, 3.0f, AZ::Vector3(2.0f, 0.0f, 1.0f));

        ExpectDeactivatingResetsTheFlip(entity, AZ::Vector3(2.0f, 0.0f, 1.0f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Capsule events

    TEST_F(Transform2SphereCapsuleTest, OnlyAFlipOfTheOffsetRaisesACapsuleShapeChange)
    {
        AZ::Entity entity;
        AddCapsule(entity, 0.5f, 3.0f, AZ::Vector3(2.0f, 0.0f, 0.0f));
        entity.Activate();

        ExpectOnlyAFlipOfTheOffsetRaisesShapeChange(entity.GetId());
    }

    TEST_F(Transform2SphereCapsuleTest, FlipOfACapsuleWithoutOffsetRaisesNoShapeChange)
    {
        AZ::Entity entity;
        AddCapsule(entity, 0.5f, 3.0f, AZ::Vector3::CreateZero());
        entity.Activate();

        ExpectNoShapeChangeWithoutOffset(entity.GetId());
    }

    // A capsule with an offset along its axis moves under a flip of Z, and tells.
    TEST_F(Transform2SphereCapsuleTest, ZFlipOfACapsuleWithAnOffsetAlongItsAxisRaisesOneShapeChange)
    {
        AZ::Entity entity;
        AddCapsule(entity, 0.5f, 3.0f, AZ::Vector3(0.0f, 0.0f, 2.0f));
        entity.Activate();
        ShapeChangedCounter counter(entity.GetId());

        PushFlipMask(entity.GetId(), FlipMaskZ);
        EXPECT_EQ(counter.m_count, 1);

        PushFlipMask(entity.GetId(), FlipMaskZ);
        EXPECT_EQ(counter.m_count, 1);
    }

    //////////////////////////////////////////////////////////////////////////
    // Game View debug draw

    TEST_F(Transform2SphereCapsuleTest, GameViewSphereDrawFollowsTheFlipAndKeepsItsConfiguration)
    {
        const AZ::Vector3 offset(3.0f, 0.0f, 0.0f);
        LmbrCentral::SphereShapeConfig config(1.0f);
        config.m_translationOffset = offset;

        AZ::Entity entity;
        entity.CreateComponent<LmbrCentral::SphereShapeComponent>()->SetConfiguration(config);
        auto* debugDisplay = entity.CreateComponent<LmbrCentral::SphereShapeDebugDisplayComponent>();
        debugDisplay->SetConfiguration(config);
        entity.CreateComponent<AzFramework::TransformComponent>();
        entity.Init();
        entity.Activate();

        // The wire sphere is only an approximation of a sphere, so the center of what it draws is checked with some slack.
        EXPECT_THAT(GetDrawnBounds(entity.GetId()).GetCenter(), IsCloseTolerance(offset, 0.1f));

        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_THAT(GetDrawnBounds(entity.GetId()).GetCenter(), IsCloseTolerance(-offset, 0.1f));

        // The configuration that is saved with the component still holds the authored offset.
        LmbrCentral::SphereShapeConfig savedConfig;
        debugDisplay->GetConfiguration(savedConfig);
        EXPECT_THAT(savedConfig.m_translationOffset, IsClose(offset));

        PushFlipMask(entity.GetId(), 0);
        EXPECT_THAT(GetDrawnBounds(entity.GetId()).GetCenter(), IsCloseTolerance(offset, 0.1f));
    }

    TEST_F(Transform2SphereCapsuleTest, GameViewCapsuleDrawFollowsTheFlipAndKeepsItsConfiguration)
    {
        // The capsule mesh is made by the geometry system.
        AZ::Entity systemEntity;
        systemEntity.CreateComponent<LmbrCentral::GeometrySystemComponent>();
        systemEntity.Init();
        systemEntity.Activate();

        const AZ::Vector3 offset(3.0f, 0.0f, 0.0f);
        LmbrCentral::CapsuleShapeConfig config;
        config.m_radius = 0.5f;
        config.m_height = 3.0f;
        config.m_translationOffset = offset;

        AZ::Entity entity;
        entity.CreateComponent<LmbrCentral::CapsuleShapeComponent>()->SetConfiguration(config);
        auto* debugDisplay = entity.CreateComponent<LmbrCentral::CapsuleShapeDebugDisplayComponent>();
        debugDisplay->SetConfiguration(config);
        entity.CreateComponent<AzFramework::TransformComponent>();
        entity.Init();
        entity.Activate();

        // The debug mesh is only an approximation of a capsule, so the center of what it draws is checked with some slack.
        EXPECT_THAT(GetDrawnBounds(entity.GetId()).GetCenter(), IsCloseTolerance(offset, 0.1f));

        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_THAT(GetDrawnBounds(entity.GetId()).GetCenter(), IsCloseTolerance(-offset, 0.1f));

        // The configuration that is saved with the component still holds the authored offset.
        LmbrCentral::CapsuleShapeConfig savedConfig;
        debugDisplay->GetConfiguration(savedConfig);
        EXPECT_THAT(savedConfig.m_translationOffset, IsClose(offset));

        PushFlipMask(entity.GetId(), 0);
        EXPECT_THAT(GetDrawnBounds(entity.GetId()).GetCenter(), IsCloseTolerance(offset, 0.1f));
    }
} // namespace UnitTest::Transform2SphereCapsule
