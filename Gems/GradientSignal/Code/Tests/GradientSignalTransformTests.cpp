/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */


#include <Tests/GradientSignalTestFixtures.h>

#include <AzTest/AzTest.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Memory/PoolAllocator.h>
#include <AzCore/Math/Vector2.h>
#include <AZTestShared/Math/MathTestHelpers.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzFramework/Components/TransformComponent.h>
#include <GradientSignal/Components/GradientTransformComponent.h>
#include <GradientSignal/Util.h>
#include <LmbrCentral/Shape/BoxShapeComponentBus.h>
#include <LmbrCentral/Shape/ReferenceShapeComponentBus.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>

namespace UnitTest
{
    struct GradientSignalTransformTestsFixture : public GradientSignalTest
    {
        // By default, we'll use a shape half extents of (5, 10, 20) for every test, and a world translation of (100, 200, 300).
        struct GradientTransformSetupData
        {
            GradientSignal::WrappingType m_wrappingType{ GradientSignal::WrappingType::None };
            AZ::Vector3 m_shapeHalfExtents{ 5.0f, 10.0f, 20.0f };
            AZ::Vector3 m_worldTranslation{ 100.0f, 200.0f, 300.0f };
            float m_frequencyZoom{ 1.0f };
        };

        struct GradientTransformTestData
        {
            AZ::Vector3 m_positionToTest;
            AZ::Vector3 m_expectedOutputUVW;
            bool m_expectedOutputRejectionResult;
        };

        static constexpr float UvEpsilon = GradientSignal::GradientTransform::UvEpsilon;

        void TestGradientTransform(const GradientTransformSetupData& setup, const GradientTransformTestData& test)
        {
            AZ::Aabb shapeBounds = AZ::Aabb::CreateCenterHalfExtents(AZ::Vector3::CreateZero(), setup.m_shapeHalfExtents);
            AZ::Matrix3x4 transform = AZ::Matrix3x4::CreateTranslation(setup.m_worldTranslation);
            float frequencyZoom = setup.m_frequencyZoom;
            GradientSignal::WrappingType wrappingType = setup.m_wrappingType;

            AZ::Vector3 outUVW;
            bool wasPointRejected;

            // Perform the query with a 3D gradient and verify that the results match expectations.
            GradientSignal::GradientTransform gradientTransform3d(shapeBounds, transform, true, frequencyZoom, wrappingType);
            gradientTransform3d.TransformPositionToUVW(test.m_positionToTest, outUVW, wasPointRejected);
            EXPECT_THAT(outUVW, IsClose(test.m_expectedOutputUVW));
            EXPECT_EQ(wasPointRejected, test.m_expectedOutputRejectionResult);

            // Perform the query with a 2D gradient and verify that the results match, but always returns a W value of 0.
            GradientSignal::GradientTransform gradientTransform2d(shapeBounds, transform, false, frequencyZoom, wrappingType);
            gradientTransform2d.TransformPositionToUVW(test.m_positionToTest, outUVW, wasPointRejected);
            EXPECT_THAT(outUVW, IsClose(AZ::Vector3(test.m_expectedOutputUVW.GetX(), test.m_expectedOutputUVW.GetY(), 0.0f)));
            EXPECT_EQ(wasPointRejected, test.m_expectedOutputRejectionResult);
        }

        //////////////////////////////////////////////////////////////////////////
        // Transform 2 flip
        //
        // A shape on a Transform 2 entity with a negative scale axis is flipped by the Transform 2 shape support, and the gradient
        // pattern mirrors with it. The tests flip the shape the same way the support does: the mask goes to the shape, then the flip
        // is announced to whoever listens, and the gradient updates on the next tick.

        static constexpr AZ::u8 FlipMaskX = 1;

        // The Reference shape component, whose header is private to LmbrCentral.
        static constexpr AZ::TypeId ReferenceShapeComponentTypeId{ "{EB9C6DC1-900F-4CE8-AA00-81361127063A}" };

        // The box of the flip tests is 4 x 6 x 8, so its half extents are (2, 3, 4).
        static constexpr float BoxWidth = 4.0f;
        static constexpr float BoxDepth = 6.0f;
        static constexpr float BoxHeight = 8.0f;

        // A rotated and moved entity, so the mirror has to happen in the local axes of the entity and not in the world's.
        static AZ::Transform GetRotatedWorld()
        {
            return AZ::Transform(AZ::Vector3(100.0f, 200.0f, 300.0f), AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f)), 1.0f);
        }

        static GradientSignal::GradientTransformConfig CreateConfig(
            GradientSignal::WrappingType wrappingType = GradientSignal::WrappingType::None,
            bool is3d = true,
            float frequencyZoom = 1.0f)
        {
            GradientSignal::GradientTransformConfig config;
            config.m_advancedMode = true; // Sampling in 3D is an advanced setting.
            config.m_is3d = is3d;
            config.m_wrappingType = wrappingType;
            config.m_frequencyZoom = frequencyZoom;
            return config;
        }

        // An entity with a box shape and the gradient transform that reads it. The entity is activated, and the gradient has settled.
        AZStd::unique_ptr<AZ::Entity> CreateBoxGradientEntity(
            const AZ::Transform& world, const AZ::Vector3& boxOffset, const GradientSignal::GradientTransformConfig& config)
        {
            auto entity = CreateEntity();

            LmbrCentral::BoxShapeConfig boxConfig(AZ::Vector3(BoxWidth, BoxDepth, BoxHeight));
            boxConfig.m_translationOffset = boxOffset;
            entity->CreateComponent(LmbrCentral::BoxShapeComponentTypeId)->SetConfiguration(boxConfig);
            entity->CreateComponent<AzFramework::TransformComponent>()->SetWorldTM(world);
            entity->CreateComponent<GradientSignal::GradientTransformComponent>(config);

            ActivateEntity(entity.get());

            // The activation of the entity reaches the dependency monitor of the gradient, which marks it dirty. Let the first tick
            // update it, so a test starts from a gradient that only changes when it is told to.
            Tick();
            return entity;
        }

        // An entity with only a box shape, to be referenced. The entity is activated.
        AZStd::unique_ptr<AZ::Entity> CreateBoxEntity(const AZ::Transform& world)
        {
            auto entity = CreateEntity();

            LmbrCentral::BoxShapeConfig boxConfig(AZ::Vector3(BoxWidth, BoxDepth, BoxHeight));
            entity->CreateComponent(LmbrCentral::BoxShapeComponentTypeId)->SetConfiguration(boxConfig);
            entity->CreateComponent<AzFramework::TransformComponent>()->SetWorldTM(world);

            ActivateEntity(entity.get());
            return entity;
        }

        static GradientSignal::GradientTransform GetGradientTransform(AZ::EntityId entityId)
        {
            GradientSignal::GradientTransform gradientTransform;
            GradientSignal::GradientTransformRequestBus::EventResult(
                gradientTransform, entityId, &GradientSignal::GradientTransformRequests::GetGradientTransform);
            return gradientTransform;
        }

        // The UVW a gradient transform gives a world position.
        static AZ::Vector3 GetUvw(
            const GradientSignal::GradientTransform& gradientTransform, const AZ::Vector3& position, bool& wasPointRejected)
        {
            AZ::Vector3 uvw = AZ::Vector3::CreateZero();
            gradientTransform.TransformPositionToUVW(position, uvw, wasPointRejected);
            return uvw;
        }

        // The mirror image of a world position about the origin of an entity, across the planes of the entity's local axes.
        static AZ::Vector3 MirrorAboutEntity(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Vector3& position)
        {
            return world.TransformPoint(signs * world.GetInverse().TransformPoint(position));
        }

        // The gradient updates on the tick, on the main thread.
        void Tick()
        {
            AZ::TickBus::Broadcast(&AZ::TickBus::Events::OnTick, 0.0f, AZ::ScriptTimePoint{});
        }

        void SetShapeFlipMask(AZ::EntityId shapeEntityId, AZ::u8 flipMask)
        {
            LmbrCentral::ShapeComponentRequestsBus::Event(shapeEntityId, &LmbrCentral::ShapeComponentRequests::SetFlipMask, flipMask);
        }

        void AnnounceShapeFlip(AZ::EntityId shapeEntityId)
        {
            LmbrCentral::ShapeComponentNotificationsBus::Event(
                shapeEntityId, &LmbrCentral::ShapeComponentNotifications::OnShapeFlipChanged);
        }

        // What the Transform 2 shape support does when the sign of an entity's scale changes.
        void FlipShape(AZ::EntityId shapeEntityId, AZ::u8 flipMask)
        {
            SetShapeFlipMask(shapeEntityId, flipMask);
            AnnounceShapeFlip(shapeEntityId);
            Tick();
        }
    };

    TEST_F(GradientSignalTransformTestsFixture, UnboundedWrappingReturnsTranslatedInput)
    {
        GradientTransformSetupData setup = { GradientSignal::WrappingType::None };
        GradientTransformTestData test = {
            // Input position to query
            { 0.0f, 0.0f, 0.0f },

            // Output: For no wrapping, the output is just the input position offset by the world translation.
            { -100.0f, -200.0f, -300.0f }, false
        };

        TestGradientTransform(setup, test);
    }

    TEST_F(GradientSignalTransformTestsFixture, ClampToEdgeReturnsValuesClampedToShapeBounds)
    {
        GradientTransformSetupData setup = { GradientSignal::WrappingType::ClampToEdge };
        GradientTransformTestData tests[] = {
            // Test:  Input point far below minimum shape bounds
            // Our input point is below the minimum of shape bounds, so the result should be the minimum corner of the shape.
            { { 0.0f, 0.0f, 0.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test:  Input point directly on minimum shape bounds
            // Our input point is directly on the minimum of shape bounds, so the result should be the minimum corner of the shape.
            { { 95.0f, 190.0f, 280.0f }, { -5.0f, -10.0f, -20.0f }, false },
            
            // Test:  Input point inside shape bounds
            // Our input point is inside the shape bounds, so the result is just input - translation.
            { { 101.0f, 202.0f, 303.0f }, { 1.0f, 2.0f, 3.0f }, false },

            // Test:  Input point directly on maximum shape bounds
            // On the maximum side, GradientTransform clamps to "max - epsilon" for consistency with other wrapping types, so our
            // expected results are the max shape corner - epsilon.
            { { 105.0f, 210.0f, 320.0f }, { 5.0f - UvEpsilon, 10.0f - UvEpsilon, 20.0f - UvEpsilon }, false },

            // Test:  Input point far above maximum shape bounds
            // On the maximum side, GradientTransform clamps to "max - epsilon" for consistency with other wrapping types, so our
            // expected results are the max shape corner - epsilon.
            { { 1000.0f, 1000.0f, 1000.0f }, { 5.0f - UvEpsilon, 10.0f - UvEpsilon, 20.0f - UvEpsilon }, false },
        };

        for (auto& test : tests)
        {
            TestGradientTransform(setup, test);
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, MirrorReturnsValuesMirroredBasedOnShapeBounds)
    {
        /* Here's how the results are expected to work for various inputs when using Mirror wrapping.
         * This assumes shape half extents of (5, 10, 20), and a center translation of (100, 200, 300):
         *       Inputs:                                 Outputs:
         *       ...                                     ...
         *       (75, 150, 200) - (85, 170, 240)         (-5, -10, -20) to (5, 10, 20)       // forward mirror
         *       (85, 170, 240) - (95, 190, 280)         (5, 10, 20) to (-5, -10, -20)       // back mirror
         *       (95, 190, 280) - (105, 210, 320)        (-5, -10, -20) to (5, 10, 20)       // starting point
         *       (105, 210, 320) - (115, 230, 360)       (5, 10, 20) to (-5, -10, -20)       // back mirror
         *       (115, 230, 360) - (125, 250, 400)       (-5, -10, -20) to (5, 10, 20)       // forward mirror
         *       ...                                     ...
         * When below the starting point, both forward and back mirrors will be adjusted by UvEpsilon except for points that fall on the
         * shape minimums.
         * When above the starting point, only back mirrors will be adjusted by UvEpsilon.
         */

        GradientTransformSetupData setup = { GradientSignal::WrappingType::Mirror };
        GradientTransformTestData tests[] = {
            // Test: Input exactly 2x below minimum bounds
            // When landing exactly on the 2x boundary, we return the minumum shape bounds. There is no adjustment by epsilon
            // on the minimum side of the bounds, even when we're in a mirror below the shape bounds.
            { { 75.0f, 150.0f, 200.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test: Input within 2nd mirror repeat below minimum bounds
            // The second mirror repeat should go forward in values, but will be adjusted by UvEpsilon since we're below the
            // minimum bounds.
            { { 84.0f, 168.0f, 237.0f }, { 4.0f - UvEpsilon, 8.0f - UvEpsilon, 17.0f - UvEpsilon }, false },

            // Test: Input exactly 1x below minimum bounds.
            // When landing exactly on the 1x boundary, we return the maximum shape bounds minus epsilon.
            { { 85.0f, 170.0f, 240.0f }, { 5.0f - UvEpsilon, 10.0f - UvEpsilon, 20.0f - UvEpsilon }, false },

            // Test: Input within 1st mirror repeat below minimum bounds
            // The first mirror repeat should go backwards in values, but will be adjusted by UvEpsilon since we're below the
            // minimum bounds.
            { { 94.0f, 188.0f, 277.0f }, { -4.0f - UvEpsilon, -8.0f - UvEpsilon, -17.0f - UvEpsilon }, false },

            // Test: Input inside shape bounds
            // The translated input position is (1, 2, 3) is inside the shape bounds, so we should just get the translated
            // position back as output.
            { { 101.0f, 202.0f, 303.0f }, { 1.0f, 2.0f, 3.0f }, false },

            // Test: Input within 1st mirror repeat above maximum bounds
            // The first mirror repeat should go backwards in values. We're above the maximum bounds, so the expected result
            // is (4, 8, 17) minus an epsilon.
            { { 106.0f, 212.0f, 323.0f }, { 4.0f - UvEpsilon, 8.0f - UvEpsilon, 17.0f - UvEpsilon }, false },

            // Test: Input exactly 2x above minimum bounds.
            // When landing exactly on the 2x boundary, we return the exact minimum value again.
            { { 115.0f, 230.0f, 360.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test: Input within 2nd mirror repeat above maximum bounds
            // The second mirror repeat should go forwards in values. We're above the maximum bounds, so the expected result
            // is (-4, -8, -17) with no epsilon.
            { { 116.0f, 232.0f, 363.0f }, { -4.0f, -8.0f, -17.0f }, false },

            // Test: Input exactly 2x above maximum bounds
            // When landing exactly on the 2x boundary, we return the maximum adjusted by the epsilon again.
            { { 125.0f, 250.0f, 400.0f }, { 5.0f - UvEpsilon, 10.0f - UvEpsilon, 20.0f - UvEpsilon }, false }
        };

        for (auto& test : tests)
        {
            TestGradientTransform(setup, test);
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, RepeatReturnsRepeatingValuesBasedOnShapeBounds)
    {
        /* Here's how the results are expected to work for various inputs when using Repeat wrapping.
         * This assumes shape half extents of (5, 10, 20), and a center translation of (100, 200, 300):
         *       Inputs:                                 Outputs:
         *       ...                                     ...
         *       (75, 150, 200) - (85, 170, 240)         (-5, -10, -20) to (5, 10, 20)
         *       (85, 170, 240) - (95, 190, 280)         (-5, -10, -20) to (5, 10, 20)
         *       (95, 190, 280) - (105, 210, 320)        (-5, -10, -20) to (5, 10, 20)       // starting point
         *       (105, 210, 320) - (115, 230, 360)       (-5, -10, -20) to (5, 10, 20)
         *       (115, 230, 360) - (125, 250, 400)       (-5, -10, -20) to (5, 10, 20)
         *       ...                                     ...
         * Every shape min/max boundary point below the starting point will have the max shape value.
         * Every shape min/max boundary point above the starting point with have the min shape value.
         */


        GradientTransformSetupData setup = { GradientSignal::WrappingType::Repeat };
        GradientTransformTestData tests[] = {
            // Test: 2x below minimum shape bounds
            // We're on a shape boundary below the minimum bounds, so it should return the maximum.
            { { 75.0f, 150.0f, 200.0f }, { 5.0f, 10.0f, 20.0f }, false },

            // Test: Input within 2nd repeat below minimum shape bounds
            // Every repeat should go forwards in values.
            { { 76.0f, 152.0f, 203.0f }, { -4.0f, -8.0f, -17.0f }, false },

            // Test: 1x below minimum shape bounds
            // We're on a shape boundary below the minimum bounds, so it should return the maximum.
            { { 85.0f, 170.0f, 240.0f }, { 5.0f, 10.0f, 20.0f }, false },

            // Test: Input within 1st repeat below minimum shape bounds
            // Every repeat should go forwards in values.
            { { 86.0f, 172.0f, 243.0f }, { -4.0f, -8.0f, -17.0f }, false },

            // Test: Input exactly on minimum shape bounds
            // This should return the actual minimum bounds.
            { { 95.0f, 190.0f, 280.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test: Input inside shape bounds
            // This should return the mapped value.
            { { 101.0f, 202.0f, 303.0f }, { 1.0f, 2.0f, 3.0f }, false },

            // Test: Input exactly on maximum shape bounds
            // We're on a shape boundary above the minimum bounds, so it should return the minimum.
            { { 105.0f, 210.0f, 320.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test: Input within 1st repeat above maximum shape bounds
            // Every repeat should go forwards in values.
            { { 106.0f, 212.0f, 323.0f }, { -4.0f, -8.0f, -17.0f }, false },

            // Test: 1x above maximum shape bounds
            // We're on a shape boundary above the minimum bounds, so it should return the minimum.
            { { 105.0f, 210.0f, 320.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test: Input within 2nd repeat above maximum shape bounds
            // Every repeat should go forwards in values.
            { { 106.0f, 212.0f, 323.0f }, { -4.0f, -8.0f, -17.0f }, false },
        };

        for (auto& test : tests)
        {
            TestGradientTransform(setup, test);
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, ClampToZeroReturnsClampedValuesBasedOnShapeBounds)
    {
        GradientTransformSetupData setup = { GradientSignal::WrappingType::ClampToZero };
        GradientTransformTestData tests[] = {
            // Test:  Input point far below minimum shape bounds
            // Our input point is below the minimum of shape bounds, so the result should be the minimum corner of the shape.
            // Points outside the shape bounds should return "true" for rejected.
            { { 0.0f, 0.0f, 0.0f }, { -5.0f, -10.0f, -20.0f }, true },

            // Test:  Input point directly on minimum shape bounds
            // Our input point is directly on the minimum of shape bounds, so the result should be the minimum corner of the shape.
            { { 95.0f, 190.0f, 280.0f }, { -5.0f, -10.0f, -20.0f }, false },

            // Test:  Input point inside shape bounds
            // Our input point is inside the shape bounds, so the result is just input - translation.
            { { 101.0f, 202.0f, 303.0f }, { 1.0f, 2.0f, 3.0f }, false },

            // Test:  Input point directly on maximum shape bounds
            // On the maximum side, GradientTransform clamps to "max - epsilon" for consistency with other wrapping types, so our
            // expected results are the max shape corner - epsilon.
            // Points outside the shape bounds (which includes the maximum edge of the shape bounds) should return "true" for rejected.
            { { 105.0f, 210.0f, 320.0f }, { 5.0f - UvEpsilon, 10.0f - UvEpsilon, 20.0f - UvEpsilon }, true },

            // Test:  Input point far above maximum shape bounds
            // On the maximum side, GradientTransform clamps to "max - epsilon" for consistency with other wrapping types, so our
            // expected results are the max shape corner - epsilon.
            // Points outside the shape bounds should return "true" for rejected.
            { { 1000.0f, 1000.0f, 1000.0f }, { 5.0f - UvEpsilon, 10.0f - UvEpsilon, 20.0f - UvEpsilon }, true },
        };

        for (auto& test : tests)
        {
            TestGradientTransform(setup, test);
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, ShapeBoundsForSavedDataDoNotDependOnATransform2Flip)
    {
        // A box shape with an offset, as a gradient transform would reference. The box is axis aligned, and so is its flip.
        auto shapeEntity = CreateEntity();
        LmbrCentral::BoxShapeConfig boxConfig(AZ::Vector3(4.0f));
        boxConfig.m_translationOffset = AZ::Vector3(5.0f, 0.0f, 1.0f);
        shapeEntity->CreateComponent(LmbrCentral::AxisAlignedBoxShapeComponentTypeId)->SetConfiguration(boxConfig);
        shapeEntity->CreateComponent<AzFramework::TransformComponent>();
        ActivateEntity(shapeEntity.get());

        AZ::Aabb authoredBounds = AZ::Aabb::CreateNull();
        AZ::Matrix3x4 authoredWorldToBounds = AZ::Matrix3x4::CreateIdentity();
        GradientSignal::GetObbParamsFromShape(shapeEntity->GetId(), authoredBounds, authoredWorldToBounds);
        EXPECT_THAT(authoredBounds.GetCenter(), IsClose(AZ::Vector3(5.0f, 0.0f, 1.0f)));

        // An X flip puts the box on the other side of its entity.
        LmbrCentral::ShapeComponentRequestsBus::Event(
            shapeEntity->GetId(), &LmbrCentral::ShapeComponentRequests::SetFlipMask, static_cast<AZ::u8>(1));

        AZ::Transform unusedTransform = AZ::Transform::CreateIdentity();
        AZ::Aabb drawnBounds = AZ::Aabb::CreateNull();
        LmbrCentral::ShapeComponentRequestsBus::Event(
            shapeEntity->GetId(), &LmbrCentral::ShapeComponentRequests::GetTransformAndLocalBounds, unusedTransform, drawnBounds);
        EXPECT_THAT(drawnBounds.GetCenter(), IsClose(AZ::Vector3(-5.0f, 0.0f, 1.0f)));

        // The gradient writes its bounds into saved data, which must not change with the flip.
        AZ::Aabb flippedBounds = AZ::Aabb::CreateNull();
        AZ::Matrix3x4 flippedWorldToBounds = AZ::Matrix3x4::CreateIdentity();
        GradientSignal::GetObbParamsFromShape(shapeEntity->GetId(), flippedBounds, flippedWorldToBounds);
        EXPECT_THAT(flippedBounds, IsClose(authoredBounds));
        EXPECT_THAT(flippedWorldToBounds, IsClose(authoredWorldToBounds));
    }

    //////////////////////////////////////////////////////////////////////////
    // The gradient pattern follows a Transform 2 flip

    TEST_F(GradientSignalTransformTestsFixture, GradientTransformInvertsAMirroredMatrix)
    {
        // A flip makes the determinant of the gradient's matrix negative. Sampling goes through the inverse, which has to be the full
        // inverse: a fast one that assumes a rotation would not undo a mirror.
        AZ::Matrix3x4 mirrored = AZ::Matrix3x4::CreateTranslation(AZ::Vector3(100.0f, 200.0f, 300.0f));
        mirrored.MultiplyByScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        ASSERT_LT(mirrored.GetDeterminant3x3(), 0.0f);

        const AZ::Aabb bounds = AZ::Aabb::CreateCenterHalfExtents(AZ::Vector3::CreateZero(), AZ::Vector3(5.0f, 10.0f, 20.0f));
        const GradientSignal::GradientTransform gradientTransform(bounds, mirrored, true, 1.0f, GradientSignal::WrappingType::None);

        const AZ::Vector3 localPoint(3.0f, 2.0f, 1.0f);
        bool wasPointRejected = false;
        EXPECT_THAT(GetUvw(gradientTransform, mirrored * localPoint, wasPointRejected), IsClose(localPoint));
    }

    TEST_F(GradientSignalTransformTestsFixture, ShapeWithoutAFlipGivesTheGradientTransformItAlwaysDid)
    {
        // Mask zero is what every Transform 1 entity has: the pattern is the shape's transform and bounds, with no mirror.
        const AZ::Transform world = GetRotatedWorld();
        auto entity = CreateBoxGradientEntity(world, AZ::Vector3::CreateZero(), CreateConfig());

        const AZ::Aabb expectedBounds =
            AZ::Aabb::CreateCenterHalfExtents(AZ::Vector3::CreateZero(), AZ::Vector3(BoxWidth, BoxDepth, BoxHeight) * 0.5f);
        const GradientSignal::GradientTransform expected(
            expectedBounds, AZ::Matrix3x4::CreateFromTransform(world), true, 1.0f, GradientSignal::WrappingType::None);
        const GradientSignal::GradientTransform actual = GetGradientTransform(entity->GetId());

        EXPECT_THAT(actual.GetBounds(), IsClose(expectedBounds));

        const AZ::Vector3 points[] = {
            world.TransformPoint(AZ::Vector3(3.0f, 2.0f, 1.0f)), world.TransformPoint(AZ::Vector3(-7.0f, 9.0f, -2.0f))
        };
        for (const AZ::Vector3& point : points)
        {
            bool actualRejected = false;
            bool expectedRejected = false;
            EXPECT_THAT(
                GetUvw(actual, point, actualRejected), IsCloseTolerance(GetUvw(expected, point, expectedRejected), 1.0e-3f));
            EXPECT_EQ(actualRejected, expectedRejected);
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, FlippedShapeMirrorsTheGradientPatternEvenWhenTheShapeDoesNotMove)
    {
        // The box sits on the origin of its entity, so flipping it moves nothing it answers with, and it raises no OnShapeChanged.
        // The pattern is authored in the local axes of the entity and has to mirror anyway.
        const float tolerance = 1.0e-3f;
        const AZ::Transform world = GetRotatedWorld();
        const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(FlipMaskX);
        auto entity = CreateBoxGradientEntity(world, AZ::Vector3::CreateZero(), CreateConfig());
        const AZ::EntityId entityId = entity->GetId();
        auto* component = entity->FindComponent<GradientSignal::GradientTransformComponent>();
        ASSERT_NE(component, nullptr);

        // A point that is not symmetric about the entity, and its mirror image across the local YZ plane of the entity.
        const AZ::Vector3 localPoint(3.0f, 2.0f, 1.0f);
        const AZ::Vector3 point = world.TransformPoint(localPoint);
        const AZ::Vector3 mirroredPoint = MirrorAboutEntity(world, signs, point);

        // Unflipped, the pattern is the authored one.
        const GradientSignal::GradientTransform authoredTransform = GetGradientTransform(entityId);
        bool rejected = false;
        const AZ::Vector3 authoredUvw = GetUvw(authoredTransform, point, rejected);
        const AZ::Vector3 authoredMirroredUvw = GetUvw(authoredTransform, mirroredPoint, rejected);
        EXPECT_THAT(authoredUvw, IsCloseTolerance(localPoint, tolerance));
        EXPECT_THAT(authoredMirroredUvw, IsCloseTolerance(AZ::Vector3(-3.0f, 2.0f, 1.0f), tolerance));

        GradientSignal::GradientTransformConfig authoredConfig;
        component->WriteOutConfig(&authoredConfig);

        // The mask reaches the shape, and nobody hears of it: the gradient has no reason to update.
        SetShapeFlipMask(entityId, FlipMaskX);
        Tick();
        EXPECT_TRUE(GetGradientTransform(entityId) == authoredTransform);

        // The announcement is what tells the gradient.
        AnnounceShapeFlip(entityId);
        Tick();
        const GradientSignal::GradientTransform flippedTransform = GetGradientTransform(entityId);
        EXPECT_FALSE(flippedTransform == authoredTransform);

        // The pattern is mirrored about the origin of the entity: each point gets the UVW its mirror image had.
        EXPECT_THAT(GetUvw(flippedTransform, point, rejected), IsCloseTolerance(authoredMirroredUvw, tolerance));
        EXPECT_THAT(GetUvw(flippedTransform, mirroredPoint, rejected), IsCloseTolerance(authoredUvw, tolerance));
        EXPECT_FALSE(GetUvw(flippedTransform, point, rejected).IsClose(authoredUvw, tolerance));

        // The bounds are the authored ones. Only the matrix carries the mirror.
        EXPECT_THAT(flippedTransform.GetBounds(), IsClose(authoredTransform.GetBounds()));

        // What gets saved does not depend on the flip.
        GradientSignal::GradientTransformConfig flippedConfig;
        component->WriteOutConfig(&flippedConfig);
        EXPECT_THAT(flippedConfig.m_scale, IsClose(authoredConfig.m_scale));
        EXPECT_THAT(flippedConfig.m_center, IsClose(authoredConfig.m_center));
        EXPECT_THAT(flippedConfig.m_bounds, IsClose(authoredConfig.m_bounds));
        EXPECT_TRUE(flippedConfig == authoredConfig);

        // Taking the flip off gives the authored pattern back.
        FlipShape(entityId, 0);
        const GradientSignal::GradientTransform restoredTransform = GetGradientTransform(entityId);
        EXPECT_THAT(GetUvw(restoredTransform, point, rejected), IsCloseTolerance(authoredUvw, tolerance));
        EXPECT_THAT(GetUvw(restoredTransform, mirroredPoint, rejected), IsCloseTolerance(authoredMirroredUvw, tolerance));
    }

    TEST_F(GradientSignalTransformTestsFixture, EveryFlipMirrorsTheGradientPatternAboutTheEntity)
    {
        const float tolerance = 1.0e-3f;
        const AZ::Transform world = GetRotatedWorld();
        const AZ::Vector3 localPoint(3.0f, -2.0f, 1.0f);
        const AZ::Vector3 point = world.TransformPoint(localPoint);

        const AZ::u8 flipMasks[] = { 1, 2, 4, 3, 5, 7 }; // X, Y, Z, XY, XZ and XYZ. XZ is a rotation and the others are mirrors.
        const bool is3dSettings[] = { true, false };

        for (const bool is3d : is3dSettings)
        {
            for (const AZ::u8 flipMask : flipMasks)
            {
                SCOPED_TRACE(::testing::Message() << "mask " << static_cast<int>(flipMask) << ", 3D " << is3d);

                auto entity = CreateBoxGradientEntity(world, AZ::Vector3::CreateZero(), CreateConfig(GradientSignal::WrappingType::None, is3d));
                const AZ::EntityId entityId = entity->GetId();
                const AZ::Vector3 mirroredPoint = MirrorAboutEntity(world, LmbrCentral::FlipMaskToSigns(flipMask), point);

                // The pattern at a point after the flip is the pattern at its mirror image before. A 2D gradient has no W, so a flip
                // across Z leaves it as it was.
                bool rejected = false;
                const GradientSignal::GradientTransform authoredTransform = GetGradientTransform(entityId);
                const AZ::Vector3 authoredMirroredUvw = GetUvw(authoredTransform, mirroredPoint, rejected);

                FlipShape(entityId, flipMask);

                const GradientSignal::GradientTransform flippedTransform = GetGradientTransform(entityId);
                EXPECT_THAT(GetUvw(flippedTransform, point, rejected), IsCloseTolerance(authoredMirroredUvw, tolerance));

                // The size of the pattern stays a magnitude: a flip makes the determinant negative, and the image gradient divides
                // by this to get the size of a pixel.
                EXPECT_GT(flippedTransform.GetScale().GetMinElement(), 0.0f);
            }
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, FlippedPatternMirrorsInEveryWrappingTypeAndFollowsFrequencyZoom)
    {
        const float tolerance = 1.0e-3f;
        const float frequencyZoom = 2.0f;
        const AZ::Transform world = GetRotatedWorld();
        const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(FlipMaskX);

        // Inside the bounds, just outside them, and far enough out to wrap or mirror several times. The bounds are (2, 3, 4) either side,
        // and none of the points sits on an edge of a repeat, where a rounding error would pick the other side.
        const AZ::Vector3 localPoints[] = { { 1.0f, 2.0f, 3.0f }, { 3.5f, -4.0f, 5.0f }, { 13.0f, -8.5f, 21.0f } };

        const GradientSignal::WrappingType wrappingTypes[] = {
            GradientSignal::WrappingType::None,
            GradientSignal::WrappingType::ClampToEdge,
            GradientSignal::WrappingType::ClampToZero,
            GradientSignal::WrappingType::Mirror,
            GradientSignal::WrappingType::Repeat,
        };
        const bool is3dSettings[] = { true, false };

        for (const bool is3d : is3dSettings)
        {
            for (const GradientSignal::WrappingType wrappingType : wrappingTypes)
            {
                SCOPED_TRACE(::testing::Message() << "wrapping type " << static_cast<int>(wrappingType) << ", 3D " << is3d);

                auto entity = CreateBoxGradientEntity(world, AZ::Vector3::CreateZero(), CreateConfig(wrappingType, is3d, frequencyZoom));
                const AZ::EntityId entityId = entity->GetId();
                const GradientSignal::GradientTransform authoredTransform = GetGradientTransform(entityId);

                FlipShape(entityId, FlipMaskX);
                const GradientSignal::GradientTransform flippedTransform = GetGradientTransform(entityId);

                // Wrapping, clamping and mirroring act on the local position, and the zoom on what they return. The mirror is in the
                // local position, so the result at a point is the authored result at its mirror image, rejection included.
                for (const AZ::Vector3& localPoint : localPoints)
                {
                    const AZ::Vector3 point = world.TransformPoint(localPoint);
                    const AZ::Vector3 mirroredPoint = MirrorAboutEntity(world, signs, point);

                    bool authoredRejected = false;
                    bool flippedRejected = false;
                    const AZ::Vector3 authoredMirroredUvw = GetUvw(authoredTransform, mirroredPoint, authoredRejected);
                    const AZ::Vector3 flippedUvw = GetUvw(flippedTransform, point, flippedRejected);

                    EXPECT_THAT(flippedUvw, IsCloseTolerance(authoredMirroredUvw, tolerance));
                    EXPECT_EQ(flippedRejected, authoredRejected);
                }
            }
        }
    }

    TEST_F(GradientSignalTransformTestsFixture, FlippedPatternPutsTheRegionOnTheDrawnSideOfTheShape)
    {
        // A box with an offset moves to the other side of its entity when it is flipped. Clamping to zero keeps the pattern inside
        // the region, so the region has to be where the box is drawn.
        const AZ::Transform world = GetRotatedWorld();
        const AZ::Vector3 offset(5.0f, 0.0f, 0.0f);
        auto entity = CreateBoxGradientEntity(world, offset, CreateConfig(GradientSignal::WrappingType::ClampToZero));
        const AZ::EntityId entityId = entity->GetId();

        const AZ::Vector3 authoredSide = world.TransformPoint(offset);
        const AZ::Vector3 drawnSide = world.TransformPoint(-offset);

        auto isInsideShape = [entityId](const AZ::Vector3& point)
        {
            bool isInside = false;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(isInside, entityId, &LmbrCentral::ShapeComponentRequests::IsPointInside, point);
            return isInside;
        };

        auto isRejected = [entityId](const AZ::Vector3& point)
        {
            bool wasPointRejected = false;
            GetUvw(GetGradientTransform(entityId), point, wasPointRejected);
            return wasPointRejected;
        };

        // Unflipped, the region is around the offset.
        EXPECT_TRUE(isInsideShape(authoredSide));
        EXPECT_FALSE(isRejected(authoredSide));
        EXPECT_FALSE(isInsideShape(drawnSide));
        EXPECT_TRUE(isRejected(drawnSide));

        // Flipped, the box is drawn on the other side, and the region goes with it.
        FlipShape(entityId, FlipMaskX);
        EXPECT_TRUE(isInsideShape(drawnSide));
        EXPECT_FALSE(isRejected(drawnSide));
        EXPECT_FALSE(isInsideShape(authoredSide));
        EXPECT_TRUE(isRejected(authoredSide));

        // Flipped back, the region is where it was.
        FlipShape(entityId, 0);
        EXPECT_FALSE(isRejected(authoredSide));
        EXPECT_TRUE(isRejected(drawnSide));
    }

    TEST_F(GradientSignalTransformTestsFixture, OriginTransformTypeIsAWorldTermAndDoesNotFlip)
    {
        GradientSignal::GradientTransformConfig config = CreateConfig();
        config.m_transformType = GradientSignal::TransformType::World_Origin;
        auto entity = CreateBoxGradientEntity(GetRotatedWorld(), AZ::Vector3::CreateZero(), config);
        const AZ::EntityId entityId = entity->GetId();

        const GradientSignal::GradientTransform authoredTransform = GetGradientTransform(entityId);
        FlipShape(entityId, FlipMaskX);

        EXPECT_TRUE(GetGradientTransform(entityId) == authoredTransform);
    }

    TEST_F(GradientSignalTransformTestsFixture, FlipOfAReferencedShapeReachesTheGradient)
    {
        const float tolerance = 1.0e-3f;
        const AZ::Transform world = GetRotatedWorld();
        const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(FlipMaskX);

        // The shape lives on another entity, which a Reference shape on the gradient's entity points at.
        auto shapeEntity = CreateBoxEntity(world);
        const AZ::EntityId shapeEntityId = shapeEntity->GetId();

        auto gradientEntity = CreateEntity();
        ASSERT_NE(gradientEntity->CreateComponent(ReferenceShapeComponentTypeId), nullptr);
        gradientEntity->CreateComponent<AzFramework::TransformComponent>()->SetWorldTM(world);
        gradientEntity->CreateComponent<GradientSignal::GradientTransformComponent>(CreateConfig());
        ActivateEntity(gradientEntity.get());
        const AZ::EntityId gradientEntityId = gradientEntity->GetId();

        LmbrCentral::ReferenceShapeRequestBus::Event(gradientEntityId, &LmbrCentral::ReferenceShapeRequests::SetShapeEntityId, shapeEntityId);
        Tick();

        const AZ::Vector3 localPoint(3.0f, 2.0f, 1.0f);
        const AZ::Vector3 point = world.TransformPoint(localPoint);
        const AZ::Vector3 mirroredPoint = MirrorAboutEntity(world, signs, point);

        bool rejected = false;
        const GradientSignal::GradientTransform authoredTransform = GetGradientTransform(gradientEntityId);
        const AZ::Vector3 authoredUvw = GetUvw(authoredTransform, point, rejected);
        const AZ::Vector3 authoredMirroredUvw = GetUvw(authoredTransform, mirroredPoint, rejected);
        EXPECT_THAT(authoredUvw, IsCloseTolerance(localPoint, tolerance));

        // The flip is on the target. The Reference shape passes on both the signs and the announcement.
        FlipShape(shapeEntityId, FlipMaskX);

        AZ::Vector3 referenceSigns = AZ::Vector3::CreateOne();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            referenceSigns, gradientEntityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
        EXPECT_THAT(referenceSigns, IsClose(signs));

        const GradientSignal::GradientTransform flippedTransform = GetGradientTransform(gradientEntityId);
        EXPECT_THAT(GetUvw(flippedTransform, point, rejected), IsCloseTolerance(authoredMirroredUvw, tolerance));
        EXPECT_THAT(GetUvw(flippedTransform, mirroredPoint, rejected), IsCloseTolerance(authoredUvw, tolerance));

        // The authored bounds come through the reference as well.
        EXPECT_THAT(flippedTransform.GetBounds(), IsClose(authoredTransform.GetBounds()));
    }

    TEST_F(GradientSignalTransformTestsFixture, GradientFollowsTheSignsOfTheShapeItTakesItsBoundsFrom)
    {
        // The gradient's own entity has a box, and an advanced setting points the gradient at the box of another entity. The bounds,
        // and so the flip, are the other entity's. The box on the gradient's own entity answers for itself and changes nothing.
        const AZ::Transform world = GetRotatedWorld();
        auto shapeEntity = CreateBoxEntity(world);
        const AZ::EntityId shapeEntityId = shapeEntity->GetId();

        GradientSignal::GradientTransformConfig config = CreateConfig();
        config.m_allowReference = true;
        config.m_shapeReference = shapeEntityId;
        auto gradientEntity = CreateBoxGradientEntity(world, AZ::Vector3::CreateZero(), config);
        const AZ::EntityId gradientEntityId = gradientEntity->GetId();

        const AZ::Vector3 point = world.TransformPoint(AZ::Vector3(3.0f, 2.0f, 1.0f));
        const GradientSignal::GradientTransform authoredTransform = GetGradientTransform(gradientEntityId);

        FlipShape(gradientEntityId, FlipMaskX);
        EXPECT_TRUE(GetGradientTransform(gradientEntityId) == authoredTransform);

        FlipShape(shapeEntityId, FlipMaskX);
        const GradientSignal::GradientTransform flippedTransform = GetGradientTransform(gradientEntityId);
        EXPECT_FALSE(flippedTransform == authoredTransform);

        bool rejected = false;
        const AZ::Vector3 mirroredPoint = MirrorAboutEntity(world, LmbrCentral::FlipMaskToSigns(FlipMaskX), point);
        EXPECT_THAT(
            GetUvw(flippedTransform, point, rejected), IsCloseTolerance(GetUvw(authoredTransform, mirroredPoint, rejected), 1.0e-3f));
    }
}


