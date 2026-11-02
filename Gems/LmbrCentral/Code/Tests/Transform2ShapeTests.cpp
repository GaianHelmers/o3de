/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Random.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/containers/vector.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/UnitTest/TestDebugDisplayRequests.h>
#include <AzTest/AzTest.h>
#include <LmbrCentral/Dependency/DependencyMonitor.h>
#include <LmbrCentral/Shape/MockShapes.h>
#include <Shape/AxisAlignedBoxShapeComponent.h>
#include <Shape/BoxShape.h>
#include <Shape/BoxShapeComponent.h>
#include <Shape/CapsuleShape.h>
#include <Shape/CapsuleShapeComponent.h>
#include <Shape/CylinderShape.h>
#include <Shape/DiskShapeComponent.h>
#include <Shape/PolygonPrismShape.h>
#include <Shape/QuadShapeComponent.h>
#include <Shape/SphereShape.h>
#include <Shape/SphereShapeComponent.h>
#include <Shape/Transform2ShapeSupport.h>
#include <Shape/TubeShape.h>
#include <ShapeTestUtils.h>

namespace UnitTest
{
    namespace
    {
        constexpr AZ::u8 FlipMaskX = 1;
        constexpr AZ::u8 FlipMaskY = 2;
        constexpr AZ::u8 FlipMaskZ = 4;

        // Counts the shape changes of an entity, which is how everything downstream of a shape hears of a flip. The announcement
        // of the flip itself is counted apart, with the signs the shape answered with when it arrived.
        class ShapeChangedCounter
            : public LmbrCentral::ShapeComponentNotificationsBus::Handler
        {
        public:
            explicit ShapeChangedCounter(AZ::EntityId entityId)
                : m_entityId(entityId)
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

            void OnShapeFlipChanged() override
            {
                ++m_flipCount;

                // Whatever mirrors with the flip reads the signs of the shape now, so the shape has to hold them by this event.
                LmbrCentral::ShapeComponentRequestsBus::EventResult(
                    m_signsAtFlip, m_entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
            }

            AZ::EntityId m_entityId;
            int m_count = 0;
            int m_flipCount = 0;
            AZ::Vector3 m_signsAtFlip = AZ::Vector3::CreateZero();
        };

        // A shape that only records the flip masks it is sent, so the listener can be checked without a real shape.
        class FlipMaskRecorder
            : public LmbrCentral::ShapeComponentRequestsBus::Handler
        {
        public:
            explicit FlipMaskRecorder(AZ::EntityId entityId)
            {
                LmbrCentral::ShapeComponentRequestsBus::Handler::BusConnect(entityId);
            }

            ~FlipMaskRecorder()
            {
                LmbrCentral::ShapeComponentRequestsBus::Handler::BusDisconnect();
            }

            AZ::Crc32 GetShapeType() const override
            {
                return AZ_CRC_CE("FlipMaskRecorder");
            }

            AZ::Aabb GetEncompassingAabb() const override
            {
                return AZ::Aabb::CreateNull();
            }

            void GetTransformAndLocalBounds(AZ::Transform& transform, AZ::Aabb& bounds) const override
            {
                transform = AZ::Transform::CreateIdentity();
                bounds = AZ::Aabb::CreateNull();
            }

            bool IsPointInside([[maybe_unused]] const AZ::Vector3& point) const override
            {
                return false;
            }

            float DistanceSquaredFromPoint([[maybe_unused]] const AZ::Vector3& point) const override
            {
                return 0.0f;
            }

            void SetFlipMask(AZ::u8 flipMask) override
            {
                m_flipMasks.push_back(flipMask);
            }

            AZStd::vector<AZ::u8> m_flipMasks;
        };

        // The listener is told by the Transform 2 of an entity, so the tests tell it the same way.
        void TellTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale)
        {
            AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, scale);
        }

        void TellTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale)
        {
            AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, scale);
        }

        void TellTransform2Deactivated(AZ::EntityId entityId)
        {
            AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
        }

        // An entity announces that it finished activating, and that it is about to deactivate, to whoever listens at its address.
        void TellEntityActivated(AZ::EntityId entityId)
        {
            AZ::EntityBus::Event(entityId, &AZ::EntityBus::Events::OnEntityActivated, entityId);
        }

        void TellEntityDeactivated(AZ::EntityId entityId)
        {
            AZ::EntityBus::Event(entityId, &AZ::EntityBus::Events::OnEntityDeactivated, entityId);
        }

        void PushFlipMask(AZ::EntityId entityId, AZ::u8 flipMask)
        {
            LmbrCentral::ShapeComponentRequestsBus::Event(entityId, &LmbrCentral::ShapeComponentRequests::SetFlipMask, flipMask);
        }
    } // namespace

    class Transform2ShapeTest
        : public LeakDetectionFixture
    {
    protected:
        void SetUp() override
        {
            LeakDetectionFixture::SetUp();
            m_serializeContext = AZStd::make_unique<AZ::SerializeContext>();

            RegisterDescriptor(AzFramework::TransformComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::BoxShapeComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::BoxShapeDebugDisplayComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::AxisAlignedBoxShapeComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::AxisAlignedBoxShapeDebugDisplayComponent::CreateDescriptor());
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

        // A box shape on an entity with a Transform 1. The entity is initialized, and not activated.
        template<typename BoxComponent>
        void AddBox(AZ::Entity& entity, const AZ::Vector3& dimensions, const AZ::Vector3& translationOffset)
        {
            LmbrCentral::BoxShapeConfig config(dimensions);
            config.m_translationOffset = translationOffset;
            entity.CreateComponent<BoxComponent>()->SetConfiguration(config);
            entity.CreateComponent<AzFramework::TransformComponent>();
            entity.Init();
        }

        // Rotated, moved and scaled, so the mirror is checked in the local axes of the entity and not in the world's.
        static AZ::Transform GetRotatedWorld()
        {
            return AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateRotationZ(0.6f), 1.5f);
        }

        // The axis aligned box ignores the rotation of its entity.
        static AZ::Transform GetUnrotatedWorld()
        {
            return AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateIdentity(), 1.5f);
        }

        static void SetWorld(AZ::EntityId entityId, const AZ::Transform& world)
        {
            AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetWorldTM, world);
        }

        // Every query of a box with an offset of (2,0,1) has to answer on the mirrored side while the flip is on, and on the authored
        // side again once it is off. The raw offset has to stay what was authored all the way.
        template<typename BoxComponent>
        void ExpectBoxFollowsFlip(AZ::u8 flipMask, const AZ::Transform& world)
        {
            const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
            const AZ::Vector3 dimensions(1.0f, 2.0f, 1.0f);
            const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(flipMask);
            const AZ::Vector3 authoredCenter = world.TransformPoint(offset);
            const AZ::Vector3 flippedCenter = world.TransformPoint(signs * offset);

            AZ::Entity entity;
            AddBox<BoxComponent>(entity, dimensions, offset);
            entity.Activate();
            SetWorld(entity.GetId(), world);
            const AZ::EntityId entityId = entity.GetId();

            // Nothing is flipped yet.
            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));

            PushFlipMask(entityId, flipMask);

            EXPECT_TRUE(IsPointInside(entity, flippedCenter));
            EXPECT_FALSE(IsPointInside(entity, authoredCenter));

            AZ::Aabb encompassingAabb = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                encompassingAabb, entityId, &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
            EXPECT_THAT(encompassingAabb.GetCenter(), IsClose(flippedCenter));

            // The local bounds are the mirrored min and max with positive extents, so the transform and the bounds are the box as drawn.
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            AZ::Aabb localBounds = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndLocalBounds, transform, localBounds);
            EXPECT_THAT(localBounds.GetCenter(), IsClose(signs * offset));
            EXPECT_THAT(localBounds.GetExtents(), IsClose(dimensions));
            EXPECT_THAT(transform.TransformPoint(localBounds.GetCenter()), IsClose(flippedCenter));

            // Random points land in the box as drawn.
            const AZ::Transform inverseTransform = transform.GetInverse();
            bool allRandomPointsInBounds = true;
            for (int i = 0; i < 200; ++i)
            {
                AZ::Vector3 point = AZ::Vector3::CreateZero();
                LmbrCentral::ShapeComponentRequestsBus::EventResult(
                    point, entityId, &LmbrCentral::ShapeComponentRequests::GenerateRandomPointInside, AZ::RandomDistributionType::UniformReal);
                allRandomPointsInBounds = allRandomPointsInBounds && localBounds.Contains(inverseTransform.TransformPoint(point));
            }
            EXPECT_TRUE(allRandomPointsInBounds);

            // The authored data is never written: the raw getter and the authored bounds still say where the box was put.
            AZ::Vector3 authoredOffset = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                authoredOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetTranslationOffset);
            EXPECT_THAT(authoredOffset, IsClose(offset));

            AZ::Vector3 effectiveOffset = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                effectiveOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetEffectiveTranslationOffset);
            EXPECT_THAT(effectiveOffset, IsClose(signs * offset));

            AZ::Vector3 flipSigns = AZ::Vector3::CreateOne();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
            EXPECT_THAT(flipSigns, IsClose(signs));

            AZ::Transform authoredTransform = AZ::Transform::CreateIdentity();
            AZ::Aabb authoredBounds = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndAuthoredLocalBounds, authoredTransform, authoredBounds);
            EXPECT_THAT(authoredBounds.GetCenter(), IsClose(offset));
            EXPECT_THAT(authoredTransform.TransformPoint(authoredBounds.GetCenter()), IsClose(authoredCenter));

            // Taking the flip off restores the authored answers.
            PushFlipMask(entityId, 0);

            EXPECT_TRUE(IsPointInside(entity, authoredCenter));
            EXPECT_FALSE(IsPointInside(entity, flippedCenter));
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndLocalBounds, transform, localBounds);
            EXPECT_THAT(localBounds.GetCenter(), IsClose(offset));

            AZ::Vector3 unflippedSigns = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                unflippedSigns, entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
            EXPECT_THAT(unflippedSigns, IsClose(AZ::Vector3::CreateOne()));
        }

        AZStd::unique_ptr<AZ::SerializeContext> m_serializeContext;
        AZStd::vector<AZStd::unique_ptr<AZ::ComponentDescriptor>> m_descriptors;
    };

    //////////////////////////////////////////////////////////////////////////
    // Footprint
    //
    // The flip mask lives in the padding of IntersectionTestDataCache, so a shape that already had a cache does not grow. The sizes
    // are what the MSVC x64 build of the Transform2-2026 branch measured before the mask was added. TubeShape has no cache and
    // keeps its size too. The Game View debug display components hold no flip state and read the shape when they draw, so they keep
    // their native size as well: the sizes of the same classes at HEAD, measured on the same build. Other compilers lay these classes
    // out differently, so the pins are only checked on MSVC.

#if defined(AZ_COMPILER_MSVC)
    TEST(Transform2ShapeFootprintTest, PrototypePin_ShapesKeepTheirSize)
    {
        EXPECT_EQ(sizeof(LmbrCentral::BoxShape), 480u);
        EXPECT_EQ(sizeof(LmbrCentral::SphereShape), 304u);
        EXPECT_EQ(sizeof(LmbrCentral::CapsuleShape), 336u);
        EXPECT_EQ(sizeof(LmbrCentral::CylinderShape), 304u);
        EXPECT_EQ(sizeof(LmbrCentral::PolygonPrismShape), 400u);
        EXPECT_EQ(sizeof(LmbrCentral::TubeShape), 400u);

        // The Game View debug display components, at their native size.
        EXPECT_EQ(sizeof(LmbrCentral::BoxShapeDebugDisplayComponent), 288u);
        EXPECT_EQ(sizeof(LmbrCentral::AxisAlignedBoxShapeDebugDisplayComponent), 272u);
        EXPECT_EQ(sizeof(LmbrCentral::CapsuleShapeDebugDisplayComponent), 368u);
        EXPECT_EQ(sizeof(LmbrCentral::SphereShapeDebugDisplayComponent), 272u);
        EXPECT_EQ(sizeof(LmbrCentral::QuadShapeDebugDisplayComponent), 272u);
    }
#endif

    //////////////////////////////////////////////////////////////////////////
    // The flip mask helpers

    TEST(Transform2ShapeMaskTest, MaskHasABitForEveryNegativeAxis)
    {
        EXPECT_EQ(LmbrCentral::FlipMaskFromScale(AZ::Vector3(1.0f, 1.0f, 1.0f)), 0);
        EXPECT_EQ(LmbrCentral::FlipMaskFromScale(AZ::Vector3(-1.0f, 1.0f, 1.0f)), FlipMaskX);
        EXPECT_EQ(LmbrCentral::FlipMaskFromScale(AZ::Vector3(1.0f, -2.0f, 1.0f)), FlipMaskY);
        EXPECT_EQ(LmbrCentral::FlipMaskFromScale(AZ::Vector3(1.0f, 1.0f, -0.5f)), FlipMaskZ);
        EXPECT_EQ(LmbrCentral::FlipMaskFromScale(AZ::Vector3(-1.0f, -1.0f, -1.0f)), FlipMaskX | FlipMaskY | FlipMaskZ);
    }

    TEST(Transform2ShapeMaskTest, ZeroIsNotAFlip)
    {
        EXPECT_EQ(LmbrCentral::FlipMaskFromScale(AZ::Vector3(0.0f, 0.0f, 0.0f)), 0);
    }

    TEST(Transform2ShapeMaskTest, SignsAreMinusOneOnTheFlippedAxes)
    {
        EXPECT_THAT(LmbrCentral::FlipMaskToSigns(0), IsClose(AZ::Vector3(1.0f, 1.0f, 1.0f)));
        EXPECT_THAT(LmbrCentral::FlipMaskToSigns(FlipMaskX | FlipMaskZ), IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
        EXPECT_THAT(LmbrCentral::FlipMaskToSigns(FlipMaskY), IsClose(AZ::Vector3(1.0f, -1.0f, 1.0f)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Box queries

    TEST_F(Transform2ShapeTest, BoxFollowsAnXFlip)
    {
        ExpectBoxFollowsFlip<LmbrCentral::BoxShapeComponent>(FlipMaskX, GetRotatedWorld());
    }

    TEST_F(Transform2ShapeTest, BoxFollowsAZFlip)
    {
        ExpectBoxFollowsFlip<LmbrCentral::BoxShapeComponent>(FlipMaskZ, GetRotatedWorld());
    }

    TEST_F(Transform2ShapeTest, BoxFollowsAnXZFlip)
    {
        ExpectBoxFollowsFlip<LmbrCentral::BoxShapeComponent>(FlipMaskX | FlipMaskZ, GetRotatedWorld());
    }

    // The offset of an axis aligned box is in its own rotation-free frame, and so is the flip.
    TEST_F(Transform2ShapeTest, AxisAlignedBoxFollowsAnXFlip)
    {
        ExpectBoxFollowsFlip<LmbrCentral::AxisAlignedBoxShapeComponent>(FlipMaskX, GetUnrotatedWorld());
    }

    TEST_F(Transform2ShapeTest, AxisAlignedBoxFollowsAnXZFlip)
    {
        ExpectBoxFollowsFlip<LmbrCentral::AxisAlignedBoxShapeComponent>(FlipMaskX | FlipMaskZ, GetUnrotatedWorld());
    }

    TEST_F(Transform2ShapeTest, DeactivatingTheShapeResetsTheFlip)
    {
        const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
        const AZ::Transform world = GetRotatedWorld();

        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), offset);
        entity.Activate();
        SetWorld(entity.GetId(), world);

        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));

        // Nobody pushes anything after this activation, so a mirror that survived it would stay.
        entity.Deactivate();
        entity.Activate();
        SetWorld(entity.GetId(), world);

        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(offset)));
        EXPECT_FALSE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));
    }

    //////////////////////////////////////////////////////////////////////////
    // Box events

    TEST_F(Transform2ShapeTest, FlipOfAnAxisWithoutOffsetRaisesNoShapeChange)
    {
        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), AZ::Vector3(2.0f, 0.0f, 0.0f));
        entity.Activate();
        ShapeChangedCounter counter(entity.GetId());

        PushFlipMask(entity.GetId(), FlipMaskY);
        PushFlipMask(entity.GetId(), FlipMaskY | FlipMaskZ);

        EXPECT_EQ(counter.m_count, 0);
    }

    TEST_F(Transform2ShapeTest, FlipOfTheAxisOfTheOffsetRaisesOneShapeChange)
    {
        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), AZ::Vector3(2.0f, 0.0f, 0.0f));
        entity.Activate();
        ShapeChangedCounter counter(entity.GetId());

        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_EQ(counter.m_count, 1);

        // The same mask again moves nothing, and taking it off moves the box back.
        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_EQ(counter.m_count, 1);

        PushFlipMask(entity.GetId(), 0);
        EXPECT_EQ(counter.m_count, 2);
    }

    TEST_F(Transform2ShapeTest, FlipOfAZeroOffsetRaisesNoShapeChange)
    {
        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), AZ::Vector3::CreateZero());
        entity.Activate();
        ShapeChangedCounter counter(entity.GetId());

        PushFlipMask(entity.GetId(), FlipMaskX);
        PushFlipMask(entity.GetId(), FlipMaskX | FlipMaskY | FlipMaskZ);
        PushFlipMask(entity.GetId(), 0);

        EXPECT_EQ(counter.m_count, 0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Shapes that do not flip

    TEST_F(Transform2ShapeTest, ShapeThatDoesNotFlipKeepsTheDefaultAnswers)
    {
        MockShape mockShape;
        const AZ::EntityId entityId = mockShape.m_entity.GetId();
        mockShape.m_localBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-1.0f, -2.0f, -3.0f), AZ::Vector3(4.0f, 5.0f, 6.0f));
        mockShape.m_localTransform = AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));

        // A mock written before the flip existed ignores it, and answers as authored.
        PushFlipMask(entityId, FlipMaskX | FlipMaskZ);

        AZ::Vector3 flipSigns = AZ::Vector3::CreateZero();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
        EXPECT_THAT(flipSigns, IsClose(AZ::Vector3::CreateOne()));

        AZ::Vector3 effectiveOffset = AZ::Vector3::CreateOne();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            effectiveOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetEffectiveTranslationOffset);
        EXPECT_THAT(effectiveOffset, IsClose(AZ::Vector3::CreateZero()));

        // The authored bounds are the bounds, so a shape that never flips needs no override.
        AZ::Transform transform = AZ::Transform::CreateIdentity();
        AZ::Aabb bounds = AZ::Aabb::CreateNull();
        LmbrCentral::ShapeComponentRequestsBus::Event(
            entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndAuthoredLocalBounds, transform, bounds);
        EXPECT_THAT(bounds, IsClose(mockShape.m_localBounds));
        EXPECT_THAT(transform, IsClose(mockShape.m_localTransform));
    }

    //////////////////////////////////////////////////////////////////////////
    // Game View debug draw

    // A box or an axis aligned box with its Game View debug display. The display draws the box where the shape answers, which is the
    // mirrored side while the entity is flipped, and the configuration it holds for saving is never written.
    template<typename ShapeComponent, typename DebugDisplayComponent>
    void ExpectGameViewDebugDrawFollowsFlip()
    {
        const AZ::Vector3 offset(3.0f, 0.0f, 0.0f);
        LmbrCentral::BoxShapeConfig config(AZ::Vector3(2.0f, 2.0f, 2.0f));
        config.m_translationOffset = offset;

        AZ::Entity entity;
        entity.CreateComponent<ShapeComponent>()->SetConfiguration(config);
        DebugDisplayComponent* debugDisplay = entity.CreateComponent<DebugDisplayComponent>();
        debugDisplay->SetConfiguration(config);
        entity.CreateComponent<AzFramework::TransformComponent>();
        entity.Init();
        entity.Activate();

        auto getDrawnBounds = [&entity]()
        {
            UnitTest::TestDebugDisplayRequests testDebugDisplayRequests;
            AzFramework::EntityDebugDisplayEventBus::Event(
                entity.GetId(), &AzFramework::EntityDebugDisplayEvents::DisplayEntityViewport, AzFramework::ViewportInfo{ 0 },
                testDebugDisplayRequests);
            const AZStd::vector<AZ::Vector3>& points = testDebugDisplayRequests.GetPoints();
            return points.empty() ? AZ::Aabb::CreateNull() : AZ::Aabb::CreatePoints(points.data(), points.size());
        };

        EXPECT_THAT(getDrawnBounds(), IsClose(AZ::Aabb::CreateFromMinMax(AZ::Vector3(2.0f, -1.0f, -1.0f), AZ::Vector3(4.0f, 1.0f, 1.0f))));

        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_THAT(getDrawnBounds(), IsClose(AZ::Aabb::CreateFromMinMax(AZ::Vector3(-4.0f, -1.0f, -1.0f), AZ::Vector3(-2.0f, 1.0f, 1.0f))));

        // The configuration that is saved with the component still holds the authored offset.
        LmbrCentral::BoxShapeConfig savedConfig;
        debugDisplay->GetConfiguration(savedConfig);
        EXPECT_THAT(savedConfig.m_translationOffset, IsClose(offset));

        PushFlipMask(entity.GetId(), 0);
        EXPECT_THAT(getDrawnBounds(), IsClose(AZ::Aabb::CreateFromMinMax(AZ::Vector3(2.0f, -1.0f, -1.0f), AZ::Vector3(4.0f, 1.0f, 1.0f))));
    }

    TEST_F(Transform2ShapeTest, GameViewDebugDrawFollowsTheFlipAndKeepsItsConfiguration)
    {
        ExpectGameViewDebugDrawFollowsFlip<LmbrCentral::BoxShapeComponent, LmbrCentral::BoxShapeDebugDisplayComponent>();
    }

    TEST_F(Transform2ShapeTest, AxisAlignedBoxGameViewDebugDrawFollowsTheFlipAndKeepsItsConfiguration)
    {
        ExpectGameViewDebugDrawFollowsFlip<LmbrCentral::AxisAlignedBoxShapeComponent, LmbrCentral::AxisAlignedBoxShapeDebugDisplayComponent>();
    }

    //////////////////////////////////////////////////////////////////////////
    // Disk and Quad facing
    //
    // A disk and a quad cover the same region on both sides, so a flip can only change the way they face, and only a Z flip does that.
    // The area lights read the facing and nothing else, so they follow it with no change of their own.

    class Transform2FacingShapeTest
        : public Transform2ShapeTest
    {
    protected:
        void SetUp() override
        {
            Transform2ShapeTest::SetUp();
            RegisterDescriptor(LmbrCentral::DiskShapeComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::QuadShapeComponent::CreateDescriptor());
            RegisterDescriptor(LmbrCentral::QuadShapeDebugDisplayComponent::CreateDescriptor());
        }

        // Tilted about two axes, so the facing is checked in the local axes of the entity and not in the world's.
        static AZ::Transform GetTiltedWorld()
        {
            const AZ::Quaternion rotation = (AZ::Quaternion::CreateRotationX(0.7f) * AZ::Quaternion::CreateRotationZ(0.4f)).GetNormalized();
            return AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), rotation, 1.5f);
        }

        // A local axis of the entity in the world, as authored.
        static AZ::Vector3 GetAuthoredAxis(const AZ::Vector3& localAxis)
        {
            return GetTiltedWorld().GetRotation().TransformVector(localAxis);
        }

        // A disk of radius 2 or a quad of 4 by 2 on an activated entity with a Transform 1, in the tilted world.
        template<typename ShapeComponent>
        void AddShape(AZ::Entity& entity)
        {
            entity.CreateComponent<ShapeComponent>();
            entity.CreateComponent<AzFramework::TransformComponent>();
            entity.Init();
            entity.Activate();
            SetWorld(entity.GetId(), GetTiltedWorld());
        }

        void AddDisk(AZ::Entity& entity)
        {
            AddShape<LmbrCentral::DiskShapeComponent>(entity);
            LmbrCentral::DiskShapeComponentRequestBus::Event(entity.GetId(), &LmbrCentral::DiskShapeComponentRequests::SetRadius, 2.0f);
        }

        void AddQuad(AZ::Entity& entity)
        {
            AddShape<LmbrCentral::QuadShapeComponent>(entity);
            LmbrCentral::QuadShapeComponentRequestBus::Event(entity.GetId(), &LmbrCentral::QuadShapeComponentRequests::SetQuadWidth, 4.0f);
            LmbrCentral::QuadShapeComponentRequestBus::Event(entity.GetId(), &LmbrCentral::QuadShapeComponentRequests::SetQuadHeight, 2.0f);
        }

        static AZ::Vector3 ReadDiskNormal(AZ::EntityId entityId)
        {
            AZ::Vector3 normal = AZ::Vector3::CreateZero();
            LmbrCentral::DiskShapeComponentRequestBus::EventResult(normal, entityId, &LmbrCentral::DiskShapeComponentRequests::GetNormal);
            return normal;
        }

        static AZ::Quaternion ReadQuadOrientation(AZ::EntityId entityId)
        {
            AZ::Quaternion orientation = AZ::Quaternion::CreateIdentity();
            LmbrCentral::QuadShapeComponentRequestBus::EventResult(
                orientation, entityId, &LmbrCentral::QuadShapeComponentRequests::GetQuadOrientation);
            return orientation;
        }

        static AZ::Vector3 ReadFlipSigns(AZ::EntityId entityId)
        {
            AZ::Vector3 flipSigns = AZ::Vector3::CreateZero();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
            return flipSigns;
        }

        // Everything a disk or a quad answers about the region it covers, which is all of it except the way it faces.
        struct RegionAnswers
        {
            AZ::Aabb m_aabb = AZ::Aabb::CreateNull();
            AZ::Aabb m_localBounds = AZ::Aabb::CreateNull();
            AZ::Transform m_transform = AZ::Transform::CreateIdentity();
            float m_distanceSquared = -1.0f;
            bool m_inside = true;
            bool m_hit = false;
            float m_hitDistance = -1.0f;
        };

        static RegionAnswers ReadRegionAnswers(
            AZ::EntityId entityId, const AZ::Vector3& point, const AZ::Vector3& rayOrigin, const AZ::Vector3& rayDirection)
        {
            RegionAnswers answers;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(answers.m_aabb, entityId, &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndLocalBounds, answers.m_transform, answers.m_localBounds);
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                answers.m_distanceSquared, entityId, &LmbrCentral::ShapeComponentRequests::DistanceSquaredFromPoint, point);
            LmbrCentral::ShapeComponentRequestsBus::EventResult(answers.m_inside, entityId, &LmbrCentral::ShapeComponentRequests::IsPointInside, point);
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                answers.m_hit, entityId, &LmbrCentral::ShapeComponentRequests::IntersectRay, rayOrigin, rayDirection, answers.m_hitDistance);
            return answers;
        }

        // The region of the shape as authored has to be what every flip mask answers, whichever way the mask leaves the shape facing.
        // The point is off the shape and the ray comes from its front, 4 away from where it hits.
        void ExpectRegionIsTheSameForEveryMask(
            AZ::EntityId entityId, const AZ::Vector3& point, const AZ::Vector3& rayOrigin, const AZ::Vector3& rayDirection)
        {
            PushFlipMask(entityId, 0);
            const RegionAnswers authored = ReadRegionAnswers(entityId, point, rayOrigin, rayDirection);
            EXPECT_FALSE(authored.m_inside);
            EXPECT_TRUE(authored.m_hit);
            EXPECT_NEAR(authored.m_hitDistance, 4.0f, 1e-3f);

            for (AZ::u8 flipMask = 0; flipMask < 8; ++flipMask)
            {
                SCOPED_TRACE(testing::Message() << "flip mask " << static_cast<int>(flipMask));
                PushFlipMask(entityId, flipMask);
                const RegionAnswers flipped = ReadRegionAnswers(entityId, point, rayOrigin, rayDirection);

                EXPECT_THAT(flipped.m_aabb, IsClose(authored.m_aabb));
                EXPECT_THAT(flipped.m_localBounds, IsClose(authored.m_localBounds));
                EXPECT_THAT(flipped.m_transform, IsClose(authored.m_transform));
                EXPECT_NEAR(flipped.m_distanceSquared, authored.m_distanceSquared, 1e-3f);
                EXPECT_EQ(flipped.m_inside, authored.m_inside);
                EXPECT_EQ(flipped.m_hit, authored.m_hit);
                EXPECT_NEAR(flipped.m_hitDistance, authored.m_hitDistance, 1e-3f);

                // These shapes have no offset for a flip to move, and a light asks for it on every move of its entity.
                AZ::Vector3 effectiveOffset = AZ::Vector3::CreateOne();
                LmbrCentral::ShapeComponentRequestsBus::EventResult(
                    effectiveOffset, entityId, &LmbrCentral::ShapeComponentRequests::GetEffectiveTranslationOffset);
                EXPECT_THAT(effectiveOffset, IsClose(AZ::Vector3::CreateZero()));
            }
        }

        // Only a Z flip tells anyone: X and Y flips leave the shape facing the way it did, whatever else is flipped with them.
        void ExpectOnlyTheZBitRaisesAShapeChange(AZ::EntityId entityId)
        {
            ShapeChangedCounter counter(entityId);

            PushFlipMask(entityId, FlipMaskX);
            PushFlipMask(entityId, FlipMaskX | FlipMaskY);
            PushFlipMask(entityId, FlipMaskY);
            EXPECT_EQ(counter.m_count, 0);

            // Z turns the shape over once. More X or Y flips on top of it, and the same mask again, change nothing.
            PushFlipMask(entityId, FlipMaskZ);
            EXPECT_EQ(counter.m_count, 1);
            PushFlipMask(entityId, FlipMaskZ | FlipMaskX);
            PushFlipMask(entityId, FlipMaskZ | FlipMaskX | FlipMaskY);
            PushFlipMask(entityId, FlipMaskZ | FlipMaskX | FlipMaskY);
            EXPECT_EQ(counter.m_count, 1);

            // Taking the Z off turns it back, and the rest is quiet again.
            PushFlipMask(entityId, FlipMaskX | FlipMaskY);
            EXPECT_EQ(counter.m_count, 2);
            PushFlipMask(entityId, 0);
            EXPECT_EQ(counter.m_count, 2);
        }
    };

    TEST_F(Transform2FacingShapeTest, DiskNormalFollowsTheZFlip)
    {
        AZ::Entity entity;
        AddDisk(entity);
        const AZ::Vector3 authoredNormal = GetAuthoredAxis(AZ::Vector3::CreateAxisZ());
        EXPECT_THAT(ReadDiskNormal(entity.GetId()), IsClose(authoredNormal));

        // Every mask in turn, so each one is also reached from masks that left the disk facing the other way.
        for (AZ::u8 flipMask = 0; flipMask < 8; ++flipMask)
        {
            SCOPED_TRACE(testing::Message() << "flip mask " << static_cast<int>(flipMask));
            PushFlipMask(entity.GetId(), flipMask);

            const float facing = (flipMask & FlipMaskZ) ? -1.0f : 1.0f;
            EXPECT_THAT(ReadDiskNormal(entity.GetId()), IsClose(authoredNormal * facing));
            EXPECT_THAT(ReadFlipSigns(entity.GetId()), IsClose(LmbrCentral::FlipMaskToSigns(flipMask)));
        }
    }

    TEST_F(Transform2FacingShapeTest, QuadOrientationFollowsTheZFlip)
    {
        AZ::Entity entity;
        AddQuad(entity);
        const AZ::Vector3 authoredLeft = GetAuthoredAxis(AZ::Vector3::CreateAxisX());
        const AZ::Vector3 authoredUp = GetAuthoredAxis(AZ::Vector3::CreateAxisY());
        const AZ::Vector3 authoredNormal = GetAuthoredAxis(AZ::Vector3::CreateAxisZ());

        for (AZ::u8 flipMask = 0; flipMask < 8; ++flipMask)
        {
            SCOPED_TRACE(testing::Message() << "flip mask " << static_cast<int>(flipMask));
            PushFlipMask(entity.GetId(), flipMask);

            // A quad that was turned over keeps its left, and its up and its normal point the other way. It is a rotation, so the
            // light that reads it is not mirrored.
            const float facing = (flipMask & FlipMaskZ) ? -1.0f : 1.0f;
            const AZ::Quaternion orientation = ReadQuadOrientation(entity.GetId());
            EXPECT_THAT(orientation.TransformVector(AZ::Vector3::CreateAxisX()), IsClose(authoredLeft));
            EXPECT_THAT(orientation.TransformVector(AZ::Vector3::CreateAxisY()), IsClose(authoredUp * facing));
            EXPECT_THAT(orientation.TransformVector(AZ::Vector3::CreateAxisZ()), IsClose(authoredNormal * facing));
            EXPECT_THAT(ReadFlipSigns(entity.GetId()), IsClose(LmbrCentral::FlipMaskToSigns(flipMask)));
        }
    }

    TEST_F(Transform2FacingShapeTest, OnlyAZFlipOfADiskRaisesAShapeChange)
    {
        AZ::Entity entity;
        AddDisk(entity);
        ExpectOnlyTheZBitRaisesAShapeChange(entity.GetId());
    }

    TEST_F(Transform2FacingShapeTest, OnlyAZFlipOfAQuadRaisesAShapeChange)
    {
        AZ::Entity entity;
        AddQuad(entity);
        ExpectOnlyTheZBitRaisesAShapeChange(entity.GetId());
    }

    TEST_F(Transform2FacingShapeTest, DiskRegionQueriesAreTheSameForEveryMask)
    {
        AZ::Entity entity;
        AddDisk(entity);
        const AZ::Transform world = GetTiltedWorld();
        const AZ::Vector3 normal = GetAuthoredAxis(AZ::Vector3::CreateAxisZ());
        const AZ::Vector3 pointOnTheDisk = world.TransformPoint(AZ::Vector3(0.5f, 0.25f, 0.0f));

        ExpectRegionIsTheSameForEveryMask(
            entity.GetId(), world.TransformPoint(AZ::Vector3(3.0f, 0.0f, 1.0f)), pointOnTheDisk + normal * 4.0f, -normal);
    }

    TEST_F(Transform2FacingShapeTest, QuadRegionQueriesAreTheSameForEveryMask)
    {
        AZ::Entity entity;
        AddQuad(entity);
        const AZ::Transform world = GetTiltedWorld();
        const AZ::Vector3 normal = GetAuthoredAxis(AZ::Vector3::CreateAxisZ());
        const AZ::Vector3 pointOnTheQuad = world.TransformPoint(AZ::Vector3(0.5f, 0.25f, 0.0f));

        ExpectRegionIsTheSameForEveryMask(
            entity.GetId(), world.TransformPoint(AZ::Vector3(3.0f, 0.0f, 1.0f)), pointOnTheQuad + normal * 4.0f, -normal);
    }

    TEST_F(Transform2FacingShapeTest, DeactivatingTheDiskResetsTheFlip)
    {
        AZ::Entity entity;
        AddDisk(entity);
        const AZ::Vector3 authoredNormal = GetAuthoredAxis(AZ::Vector3::CreateAxisZ());

        PushFlipMask(entity.GetId(), FlipMaskZ);
        EXPECT_THAT(ReadDiskNormal(entity.GetId()), IsClose(-authoredNormal));

        // Nobody pushes anything after this activation, so a flip that survived it would stay.
        entity.Deactivate();
        entity.Activate();
        SetWorld(entity.GetId(), GetTiltedWorld());

        EXPECT_THAT(ReadDiskNormal(entity.GetId()), IsClose(authoredNormal));
        EXPECT_THAT(ReadFlipSigns(entity.GetId()), IsClose(AZ::Vector3::CreateOne()));
    }

    TEST_F(Transform2FacingShapeTest, DeactivatingTheQuadResetsTheFlip)
    {
        AZ::Entity entity;
        AddQuad(entity);
        const AZ::Vector3 authoredNormal = GetAuthoredAxis(AZ::Vector3::CreateAxisZ());

        PushFlipMask(entity.GetId(), FlipMaskZ);
        EXPECT_THAT(ReadQuadOrientation(entity.GetId()).TransformVector(AZ::Vector3::CreateAxisZ()), IsClose(-authoredNormal));

        entity.Deactivate();
        entity.Activate();
        SetWorld(entity.GetId(), GetTiltedWorld());

        EXPECT_THAT(ReadQuadOrientation(entity.GetId()).TransformVector(AZ::Vector3::CreateAxisZ()), IsClose(authoredNormal));
        EXPECT_THAT(ReadFlipSigns(entity.GetId()), IsClose(AZ::Vector3::CreateOne()));
    }

    // The facing line of the quad display is the last thing drawn: it runs from the center to the side the quad faces.
    TEST_F(Transform2FacingShapeTest, QuadFacingLineIsDrawnOnTheFacingSide)
    {
        const LmbrCentral::QuadShapeConfig config(3.0f, 4.0f);
        auto getFacingLineEnd = [&config](float facing)
        {
            TestDebugDisplayRequests testDebugDisplayRequests;
            LmbrCentral::DrawQuadShape(config.GetDrawParams(), config, testDebugDisplayRequests, AZ::Vector3::CreateOne(), facing);
            return testDebugDisplayRequests.GetPoints().back();
        };

        EXPECT_THAT(getFacingLineEnd(1.0f), IsClose(AZ::Vector3(0.0f, 0.0f, 0.5f)));
        EXPECT_THAT(getFacingLineEnd(-1.0f), IsClose(AZ::Vector3(0.0f, 0.0f, -0.5f)));

        // A caller that knows nothing of the flip draws the authored side.
        TestDebugDisplayRequests testDebugDisplayRequests;
        LmbrCentral::DrawQuadShape(config.GetDrawParams(), config, testDebugDisplayRequests);
        EXPECT_THAT(testDebugDisplayRequests.GetPoints().back(), IsClose(AZ::Vector3(0.0f, 0.0f, 0.5f)));
    }

    // The Game View display of a quad draws the facing line to the side the quad faces now. Only a Z flip turns it over.
    TEST_F(Transform2FacingShapeTest, QuadGameViewFacingLineFollowsTheZFlip)
    {
        const LmbrCentral::QuadShapeConfig config(3.0f, 4.0f);

        AZ::Entity entity;
        entity.CreateComponent<LmbrCentral::QuadShapeComponent>()->SetConfiguration(config);
        entity.CreateComponent<LmbrCentral::QuadShapeDebugDisplayComponent>()->SetConfiguration(config);
        entity.CreateComponent<AzFramework::TransformComponent>();
        entity.Init();
        entity.Activate();

        auto getFacingLineEnd = [&entity]()
        {
            TestDebugDisplayRequests testDebugDisplayRequests;
            AzFramework::EntityDebugDisplayEventBus::Event(
                entity.GetId(), &AzFramework::EntityDebugDisplayEvents::DisplayEntityViewport, AzFramework::ViewportInfo{ 0 },
                testDebugDisplayRequests);
            return testDebugDisplayRequests.GetPoints().back();
        };

        EXPECT_THAT(getFacingLineEnd(), IsClose(AZ::Vector3(0.0f, 0.0f, 0.5f)));

        PushFlipMask(entity.GetId(), FlipMaskZ);
        EXPECT_THAT(getFacingLineEnd(), IsClose(AZ::Vector3(0.0f, 0.0f, -0.5f)));

        PushFlipMask(entity.GetId(), FlipMaskX);
        EXPECT_THAT(getFacingLineEnd(), IsClose(AZ::Vector3(0.0f, 0.0f, 0.5f)));

        PushFlipMask(entity.GetId(), 0);
        EXPECT_THAT(getFacingLineEnd(), IsClose(AZ::Vector3(0.0f, 0.0f, 0.5f)));
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener

    TEST_F(Transform2ShapeTest, ListenerPushesWhenTheEntityFinishesActivatingAfterTheTransform2)
    {
        const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
        const AZ::Transform world = GetRotatedWorld();
        LmbrCentral::Transform2ShapeSupport support;

        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), offset);

        // The Transform 2 activates first, while the shape does not exist. The flip is held until the entity is active.
        TellTransform2Activated(entity.GetId(), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity.Activate();
        SetWorld(entity.GetId(), world);

        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));
        EXPECT_FALSE(IsPointInside(entity, world.TransformPoint(offset)));
    }

    TEST_F(Transform2ShapeTest, ListenerPushesAgainWhenTheEntityActivatesAgain)
    {
        const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
        const AZ::Transform world = GetRotatedWorld();
        LmbrCentral::Transform2ShapeSupport support;

        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), offset);
        TellTransform2Activated(entity.GetId(), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity.Activate();

        // The shape drops its flip when it deactivates, and no Transform 2 notification comes in between. What the listener
        // pushed has to be forgotten with it, or the push after this activation would be taken for a repeat.
        entity.Deactivate();
        entity.Activate();
        SetWorld(entity.GetId(), world);

        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));
        EXPECT_FALSE(IsPointInside(entity, world.TransformPoint(offset)));
    }

    TEST_F(Transform2ShapeTest, ListenerPushesNothingWhileTheScaleHasNoNegativeAxis)
    {
        const AZ::EntityId entityId(1234);
        FlipMaskRecorder recorder(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entityId, AZ::Vector3(2.0f, 1.0f, 1.0f));
        TellEntityActivated(entityId);
        TellTransform2ScaleChanged(entityId, AZ::Vector3(3.0f, 0.5f, 1.0f));

        EXPECT_TRUE(recorder.m_flipMasks.empty());
    }

    TEST_F(Transform2ShapeTest, ListenerPushesOnlyWhenTheMaskChanges)
    {
        const AZ::EntityId entityId(1234);
        FlipMaskRecorder recorder(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellEntityActivated(entityId);
        ASSERT_EQ(recorder.m_flipMasks.size(), 1u);
        EXPECT_EQ(recorder.m_flipMasks.back(), FlipMaskX);

        // A squash animation changes the size and keeps the sign.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-2.0f, 0.5f, 3.0f));
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-0.1f, 1.0f, 1.0f));
        EXPECT_EQ(recorder.m_flipMasks.size(), 1u);

        // A turn-around is a push.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        ASSERT_EQ(recorder.m_flipMasks.size(), 2u);
        EXPECT_EQ(recorder.m_flipMasks.back(), 0);
    }

    TEST_F(Transform2ShapeTest, ListenerHoldsTheMaskUntilTheEntityIsActive)
    {
        const AZ::EntityId entityId(1234);
        FlipMaskRecorder recorder(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        // The scale changes while the shapes do not exist yet, and only the last one is pushed.
        TellTransform2Activated(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, -1.0f));
        EXPECT_TRUE(recorder.m_flipMasks.empty());

        TellEntityActivated(entityId);
        ASSERT_EQ(recorder.m_flipMasks.size(), 1u);
        EXPECT_EQ(recorder.m_flipMasks.back(), FlipMaskX | FlipMaskZ);
    }

    TEST_F(Transform2ShapeTest, ListenerPushesAgainAfterAReactivationWithoutDeactivatedNotification)
    {
        const AZ::EntityId entityId(1234);
        FlipMaskRecorder recorder(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellEntityActivated(entityId);
        ASSERT_EQ(recorder.m_flipMasks.size(), 1u);

        // The entity deactivates and activates again. Every shape dropped its flip, so the same mask has to be pushed again.
        TellEntityDeactivated(entityId);
        TellEntityActivated(entityId);

        ASSERT_EQ(recorder.m_flipMasks.size(), 2u);
        EXPECT_EQ(recorder.m_flipMasks.back(), FlipMaskX);
    }

    TEST_F(Transform2ShapeTest, ListenerPushesAgainAfterAReactivationWithDeactivatedNotification)
    {
        const AZ::EntityId entityId(1234);
        FlipMaskRecorder recorder(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellEntityActivated(entityId);

        // The entity is going away: its shapes are gone already, so nothing is pushed on the way out.
        TellEntityDeactivated(entityId);
        TellTransform2Deactivated(entityId);
        ASSERT_EQ(recorder.m_flipMasks.size(), 1u);

        TellTransform2Activated(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellEntityActivated(entityId);

        ASSERT_EQ(recorder.m_flipMasks.size(), 2u);
        EXPECT_EQ(recorder.m_flipMasks.back(), FlipMaskX);
    }

    TEST_F(Transform2ShapeTest, ListenerPushesZeroWhenALiveEntityLeavesTransform2)
    {
        const AZ::EntityId entityId(1234);
        FlipMaskRecorder recorder(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
        TellEntityActivated(entityId);
        ASSERT_EQ(recorder.m_flipMasks.size(), 1u);

        TellTransform2Deactivated(entityId);

        ASSERT_EQ(recorder.m_flipMasks.size(), 2u);
        EXPECT_EQ(recorder.m_flipMasks.back(), 0);

        // The entity is forgotten: a late notification pushes nothing.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_EQ(recorder.m_flipMasks.size(), 2u);
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener, for a child of a flipped entity
    //
    // A child hears the effective world scale sW: its own scale times the signs of every parent. The tests tell the listener what a
    // parent would raise for the child, with the values written out by hand. The world of the child is the rotated one with a uniform
    // scale of 1.5, so sW has the magnitude 1.5 on every axis and the sign of each axis that an odd number of the chain flips.

    TEST_F(Transform2ShapeTest, ListenerMirrorsAChildOfAFlippedParentAboutTheOriginOfTheChild)
    {
        const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
        const AZ::Transform world = GetRotatedWorld();
        const AZ::Vector3 authoredCenter = world.TransformPoint(offset);
        LmbrCentral::Transform2ShapeSupport support;

        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), offset);

        // The parent is not flipped yet, and the child has a scale of one: the box is where it was authored.
        TellTransform2Activated(entity.GetId(), AZ::Vector3(1.5f, 1.5f, 1.5f));
        entity.Activate();
        SetWorld(entity.GetId(), world);
        EXPECT_TRUE(IsPointInside(entity, authoredCenter));

        // The parent flips on X. The child still has a scale of one, and hears (-1.5, 1.5, 1.5).
        TellTransform2ScaleChanged(entity.GetId(), AZ::Vector3(-1.5f, 1.5f, 1.5f));

        // The box is mirrored in the axes of the child, about the origin of the child. A mirror about the plane of the parent would
        // have put it at the negated world X of the authored center.
        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));
        EXPECT_FALSE(IsPointInside(entity, authoredCenter));
        EXPECT_FALSE(IsPointInside(entity, AZ::Vector3(-authoredCenter.GetX(), authoredCenter.GetY(), authoredCenter.GetZ())));

        // The parent flips on Z as well, and the child hears X and Z.
        TellTransform2ScaleChanged(entity.GetId(), AZ::Vector3(-1.5f, 1.5f, -1.5f));
        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, -1.0f))));
        EXPECT_FALSE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));

        // The parent turns back around, and the box is where it was authored.
        TellTransform2ScaleChanged(entity.GetId(), AZ::Vector3(1.5f, 1.5f, 1.5f));
        EXPECT_TRUE(IsPointInside(entity, authoredCenter));
    }

    TEST_F(Transform2ShapeTest, ListenerCancelsTheFlipOfAChildThatItsParentFlipsOnTheSameAxis)
    {
        const AZ::Vector3 offset(2.0f, 0.0f, 1.0f);
        const AZ::Transform world = GetRotatedWorld();
        LmbrCentral::Transform2ShapeSupport support;

        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), offset);

        // The child is flipped on X by its own scale: (-1.5, 1.5, 1.5).
        TellTransform2Activated(entity.GetId(), AZ::Vector3(-1.5f, 1.5f, 1.5f));
        entity.Activate();
        SetWorld(entity.GetId(), world);
        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));

        // The parent flips on X too. The signs multiply, so the child hears (1.5, 1.5, 1.5) and is not mirrored at all.
        TellTransform2ScaleChanged(entity.GetId(), AZ::Vector3(1.5f, 1.5f, 1.5f));
        EXPECT_TRUE(IsPointInside(entity, world.TransformPoint(offset)));
        EXPECT_FALSE(IsPointInside(entity, world.TransformPoint(AZ::Vector3(-2.0f, 0.0f, 1.0f))));
    }

    TEST_F(Transform2ShapeTest, ListenerPushesTheMaskOfAChildOncePerFlipOfItsParent)
    {
        const AZ::EntityId childId(1234);
        FlipMaskRecorder recorder(childId);
        ShapeChangedCounter counter(childId);
        LmbrCentral::Transform2ShapeSupport support;

        // The parent is not flipped: the child is announced with a positive scale, so nothing is pushed.
        TellTransform2Activated(childId, AZ::Vector3(2.0f, 2.0f, 2.0f));
        TellEntityActivated(childId);
        EXPECT_TRUE(recorder.m_flipMasks.empty());
        EXPECT_EQ(counter.m_flipCount, 0);

        // The parent flips on X.
        TellTransform2ScaleChanged(childId, AZ::Vector3(-2.0f, 2.0f, 2.0f));
        ASSERT_EQ(recorder.m_flipMasks.size(), 1u);
        EXPECT_EQ(recorder.m_flipMasks.back(), FlipMaskX);
        EXPECT_EQ(counter.m_flipCount, 1);

        // The parent grows and stays flipped: the child hears a new size and the same signs.
        TellTransform2ScaleChanged(childId, AZ::Vector3(-3.0f, 3.0f, 3.0f));
        EXPECT_EQ(recorder.m_flipMasks.size(), 1u);
        EXPECT_EQ(counter.m_flipCount, 1);

        // The parent flips on Z as well, and then turns back around.
        TellTransform2ScaleChanged(childId, AZ::Vector3(-3.0f, 3.0f, -3.0f));
        ASSERT_EQ(recorder.m_flipMasks.size(), 2u);
        EXPECT_EQ(recorder.m_flipMasks.back(), FlipMaskX | FlipMaskZ);
        TellTransform2ScaleChanged(childId, AZ::Vector3(3.0f, 3.0f, 3.0f));
        ASSERT_EQ(recorder.m_flipMasks.size(), 3u);
        EXPECT_EQ(recorder.m_flipMasks.back(), 0);
        EXPECT_EQ(counter.m_flipCount, 3);
    }

    //////////////////////////////////////////////////////////////////////////
    // The flip event
    //
    // Each push of a mask is followed by OnShapeFlipChanged, for whatever mirrors with the flip without the answer of the shape
    // moving. The event goes out once per real change of the mask, and never for a Transform 1 entity.

    TEST_F(Transform2ShapeTest, ListenerRaisesOneFlipChangedPerRealMaskChange)
    {
        const AZ::EntityId entityId(1234);
        ShapeChangedCounter counter(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        // A scale with no negative axis is no flip, however it moves.
        TellTransform2Activated(entityId, AZ::Vector3(2.0f, 1.0f, 1.0f));
        TellEntityActivated(entityId);
        TellTransform2ScaleChanged(entityId, AZ::Vector3(3.0f, 0.5f, 1.0f));
        EXPECT_EQ(counter.m_flipCount, 0);

        // The first flip is one event.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_EQ(counter.m_flipCount, 1);

        // The same sign again is no change: a squash animation, and an entity announced again with the sign it already has.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-2.0f, 0.5f, 3.0f));
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-0.1f, 1.0f, 1.0f));
        TellTransform2Activated(entityId, AZ::Vector3(-4.0f, 1.0f, 1.0f));
        EXPECT_EQ(counter.m_flipCount, 1);

        // Every real change is one event, in either direction.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, -1.0f));
        EXPECT_EQ(counter.m_flipCount, 2);
        TellTransform2ScaleChanged(entityId, AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_EQ(counter.m_flipCount, 3);
    }

    TEST_F(Transform2ShapeTest, ListenerRaisesFlipChangedWhenALiveEntityLeavesTransform2)
    {
        const AZ::EntityId entityId(1234);
        ShapeChangedCounter counter(entityId);
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
        TellEntityActivated(entityId);
        ASSERT_EQ(counter.m_flipCount, 1);

        // The shapes get their authored state back, and whatever mirrored with the flip is told that it is gone.
        TellTransform2Deactivated(entityId);
        EXPECT_EQ(counter.m_flipCount, 2);

        // The entity is forgotten: a late notification raises nothing.
        TellTransform2ScaleChanged(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_EQ(counter.m_flipCount, 2);
    }

    // A box on the origin of its entity answers the same flipped, so nothing but the flip event tells anyone downstream.
    TEST_F(Transform2ShapeTest, FlipOfAZeroOffsetBoxRaisesFlipChangedAndNoShapeChange)
    {
        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), AZ::Vector3::CreateZero());
        entity.Activate();
        ShapeChangedCounter counter(entity.GetId());
        LmbrCentral::Transform2ShapeSupport support;

        TellTransform2Activated(entity.GetId(), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        TellEntityActivated(entity.GetId());

        EXPECT_EQ(counter.m_flipCount, 1);
        EXPECT_EQ(counter.m_count, 0);

        // The box already held the flip when the event reached its listeners.
        EXPECT_THAT(counter.m_signsAtFlip, IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
    }

    // The gradient transform hears of its shape through the dependency monitor, and the monitor takes the flip event as a change
    // of the shape. The box is on the origin of its entity, so the monitor has nothing else to hear.
    TEST_F(Transform2ShapeTest, DependencyMonitorTakesAFlipAsAChangeOfTheShape)
    {
        AZ::Entity entity;
        AddBox<LmbrCentral::BoxShapeComponent>(entity, AZ::Vector3(1.0f, 2.0f, 1.0f), AZ::Vector3::CreateZero());
        entity.Activate();
        LmbrCentral::Transform2ShapeSupport support;
        TellTransform2Activated(entity.GetId(), AZ::Vector3::CreateOne());
        TellEntityActivated(entity.GetId());

        AZStd::vector<AZ::EntityId> changedEntityIds;
        LmbrCentral::DependencyMonitor monitor;
        monitor.ConnectOwner(AZ::EntityId(5678));
        monitor.SetEntityNotificationFunction(
            [&changedEntityIds](const AZ::EntityId&, const AZ::EntityId& dependentId, const AZ::Aabb&)
            {
                changedEntityIds.push_back(dependentId);
            });
        monitor.ConnectDependency(entity.GetId());

        TellTransform2ScaleChanged(entity.GetId(), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        ASSERT_EQ(changedEntityIds.size(), 1u);
        EXPECT_EQ(changedEntityIds.front(), entity.GetId());
    }
} // namespace UnitTest
