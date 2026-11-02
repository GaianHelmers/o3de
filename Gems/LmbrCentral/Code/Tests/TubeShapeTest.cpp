/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzTest/AzTest.h>

#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/UnitTest/TestDebugDisplayRequests.h>
#include <AzCore/Component/ComponentApplication.h>
#include <AzCore/Math/Quaternion.h>
#include <Shape/SplineComponent.h>
#include <Shape/TubeShapeComponent.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <ShapeThreadsafeTest.h>

namespace UnitTest
{
    class TubeShapeTest
        : public LeakDetectionFixture
    {
        AZStd::unique_ptr<AZ::SerializeContext> m_serializeContext;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_transformComponentDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_splineComponentDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_tubeShapeComponentDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_tubeShapeDebugDisplayComponentDescriptor;

    public:
        void SetUp() override
        {
            LeakDetectionFixture::SetUp();
            m_serializeContext = AZStd::make_unique<AZ::SerializeContext>();

            m_transformComponentDescriptor = AZStd::unique_ptr<AZ::ComponentDescriptor>(AzFramework::TransformComponent::CreateDescriptor());
            m_transformComponentDescriptor->Reflect(&(*m_serializeContext));
            m_tubeShapeComponentDescriptor = AZStd::unique_ptr<AZ::ComponentDescriptor>(LmbrCentral::TubeShapeComponent::CreateDescriptor());
            m_tubeShapeComponentDescriptor->Reflect(&(*m_serializeContext));
            m_splineComponentDescriptor = AZStd::unique_ptr<AZ::ComponentDescriptor>(LmbrCentral::SplineComponent::CreateDescriptor());
            m_splineComponentDescriptor->Reflect(&(*m_serializeContext));
            m_tubeShapeDebugDisplayComponentDescriptor =
                AZStd::unique_ptr<AZ::ComponentDescriptor>(LmbrCentral::TubeShapeDebugDisplayComponent::CreateDescriptor());
            m_tubeShapeDebugDisplayComponentDescriptor->Reflect(&(*m_serializeContext));
        }

        void TearDown() override
        {
            m_tubeShapeDebugDisplayComponentDescriptor.reset();
            m_transformComponentDescriptor.reset();
            m_tubeShapeComponentDescriptor.reset();
            m_splineComponentDescriptor.reset();
            m_serializeContext.reset(); 
            LeakDetectionFixture::TearDown();
        }
    };

    void CreateTube(const AZ::Transform& transform, const float radius, AZ::Entity& entity)
    {
        entity.CreateComponent<AzFramework::TransformComponent>();
        entity.CreateComponent<LmbrCentral::SplineComponent>();
        entity.CreateComponent<LmbrCentral::TubeShapeComponent>();

        entity.Init();
        entity.Activate();

        AZ::TransformBus::Event(entity.GetId(), &AZ::TransformBus::Events::SetWorldTM, transform);

        LmbrCentral::SplineComponentRequestBus::Event(
            entity.GetId(), &LmbrCentral::SplineComponentRequests::SetVertices,
            AZStd::vector<AZ::Vector3>
            {
                AZ::Vector3(-3.0f, 0.0f, 0.0f), AZ::Vector3(-1.0f, 0.0f, 0.0f),
                AZ::Vector3(1.0f, 0.0f, 0.0f), AZ::Vector3(3.0f, 0.0f, 0.0f)
            }
        );

        LmbrCentral::TubeShapeComponentRequestsBus::Event(entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetRadius, radius);
    }

    TEST_F(TubeShapeTest, GetRayIntersectTubeSuccess1)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateIdentity(),
            1.0f,
            entity);

        bool rayHit = false;
        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            rayHit, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IntersectRay,
            AZ::Vector3(0.0f, -3.0f, 0.0f), AZ::Vector3(0.0f, 1.0f, 0.0f), distance);

        EXPECT_TRUE(rayHit);
        EXPECT_NEAR(distance, 2.0f, 1e-2f);
    }

    // firing at end of tube
    TEST_F(TubeShapeTest, GetRayIntersectTubeSuccess2)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateIdentity(),
            1.0f,
            entity);

        bool rayHit = false;
        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            rayHit, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IntersectRay,
            AZ::Vector3(6.0f, 0.0f, 0.0f), AZ::Vector3(-1.0f, 0.0f, 0.0f), distance);

        EXPECT_TRUE(rayHit);
        EXPECT_NEAR(distance, 2.0f, 1e-2f);
    }

    // firing at beginning of tube
    TEST_F(TubeShapeTest, GetRayIntersectTubeSuccess3)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateIdentity(),
            1.0f,
            entity);

        bool rayHit = false;
        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            rayHit, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IntersectRay,
            AZ::Vector3(-6.0f, 0.0f, 0.0f), AZ::Vector3(1.0f, 0.0f, 0.0f), distance);

        EXPECT_TRUE(rayHit);
        EXPECT_NEAR(distance, 2.0f, 1e-2f);
    }

    // transformed and scaled
    TEST_F(TubeShapeTest, GetRayIntersectTubeSuccess4)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3(-40.0f, 6.0f, 1.0f)) *
            AZ::Transform::CreateUniformScale(2.5f),
            1.0f,
            entity);

        // set variable radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 2.0f);

        bool rayHit = false;
        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            rayHit, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IntersectRay,
            AZ::Vector3(-17.0f, 6.0f, 1.0f), AZ::Vector3(-1.0f, 0.0f, 0.0f), distance);

        EXPECT_TRUE(rayHit);
        EXPECT_NEAR(distance, 8.0f, 1e-2f);
    }

    // above tube
    TEST_F(TubeShapeTest, GetRayIntersectTubeFailure)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateIdentity(),
            1.0f,
            entity);

        bool rayHit = false;
        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            rayHit, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IntersectRay,
            AZ::Vector3(0.0f, 2.0f, 2.0f), AZ::Vector3(0.0f, 1.0f, 0.0f), distance);

        EXPECT_FALSE(rayHit);
    }

    TEST_F(TubeShapeTest, GetAabb1)
    {
        AZ::Entity entity;
        CreateTube(AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateIdentity(), AZ::Vector3(0.0f, -10.0f, 0.0f)),
            1.0f, entity);

        AZ::Aabb aabb;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            aabb, entity.GetId(), &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);

        EXPECT_THAT(aabb.GetMin(), IsClose(AZ::Vector3(-4.0f, -11.0f, -1.0f)));
        EXPECT_THAT(aabb.GetMax(), IsClose(AZ::Vector3(4.0f, -9.0f, 1.0f)));
    }

    TEST_F(TubeShapeTest, GetAabb2)
    {
        AZ::Entity entity;
        CreateTube(AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateFromAxisAngle(AZ::Vector3::CreateAxisX(), AZ::Constants::QuarterPi) *
            AZ::Quaternion::CreateFromAxisAngle(AZ::Vector3::CreateAxisY(), AZ::Constants::QuarterPi), AZ::Vector3(-10.0f, -10.0f, 0.0f)),
            2.0f, entity);

        AZ::Aabb aabb;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            aabb, entity.GetId(), &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);

        EXPECT_THAT(aabb.GetMin(), IsClose(AZ::Vector3(-14.1213f, -13.5f, -3.5f)));
        EXPECT_THAT(aabb.GetMax(), IsClose(AZ::Vector3(-5.8786f, -6.5f, 3.5f)));
    }

    TEST_F(TubeShapeTest, GetAabb3)
    {
        AZ::Entity entity;
        CreateTube(AZ::Transform::CreateFromQuaternionAndTranslation(
            AZ::Quaternion::CreateFromAxisAngle(AZ::Vector3::CreateAxisX(), AZ::Constants::QuarterPi) *
            AZ::Quaternion::CreateFromAxisAngle(AZ::Vector3::CreateAxisY(), AZ::Constants::QuarterPi), AZ::Vector3(-10.0f, -10.0f, 0.0f)),
            2.0f, entity);

        // set variable radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);

        AZ::Aabb aabb;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            aabb, entity.GetId(), &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);

        EXPECT_THAT(aabb.GetMin(), IsClose(AZ::Vector3(-15.1213f, -14.5f, -4.5f)));
        EXPECT_THAT(aabb.GetMax(), IsClose(AZ::Vector3(-4.87867f, -5.5f, 4.5f)));
    }

    // variable radius and scale
    TEST_F(TubeShapeTest, GetAabb4)
    {
        AZ::Entity entity;
        CreateTube(AZ::Transform::CreateUniformScale(2.0f), 1.0f, entity);

        // set variable radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 3.0f);

        AZ::Aabb aabb;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            aabb, entity.GetId(), &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);

        EXPECT_THAT(aabb.GetMin(), IsClose(AZ::Vector3(-14.0f, -8.0f, -8.0f)));
        EXPECT_THAT(aabb.GetMax(), IsClose(AZ::Vector3(14.0f, 8.0f, 8.0f)));
    }

    // variable radius and scale
    TEST_F(TubeShapeTest, IsPointInsideSuccess1)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3(37.0f, 36.0f, 32.0f)) *
            AZ::Transform::CreateUniformScale(2.0f), 1.5f, entity);

        // set variable radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 3.0f);

        bool inside;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            inside, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IsPointInside, AZ::Vector3(43.6f, 35.8f, 37.86f));

        EXPECT_TRUE(inside);
    }

    // variable radius and scale
    TEST_F(TubeShapeTest, IsPointInsideSuccess2)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3(37.0f, 36.0f, 32.0f)) *
            AZ::Transform::CreateRotationZ(AZ::Constants::QuarterPi) *
            AZ::Transform::CreateRotationY(AZ::Constants::QuarterPi) *
            AZ::Transform::CreateUniformScale(1.5f), 1.5f, entity);

        // set variable radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 1, 0.2f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 2, 0.5f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 2.0f);

        bool inside;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            inside, entity.GetId(), &LmbrCentral::ShapeComponentRequests::IsPointInside, AZ::Vector3(37.6f, 36.76f, 34.0f));

        EXPECT_TRUE(inside);
    }

    // distance scaled - along length
    TEST_F(TubeShapeTest, DistanceFromPoint1)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3(37.0f, 36.0f, 39.0f)) *
            AZ::Transform::CreateUniformScale(2.0f), 1.5f, entity);

        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 1, 0.2f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 2, 0.5f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 2.0f);

        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            distance, entity.GetId(), &LmbrCentral::ShapeComponentRequests::DistanceFromPoint, AZ::Vector3(53.0f, 36.0f, 39.0f));

        EXPECT_NEAR(distance, 3.0f, 1e-2f);
    }

    // distance scaled - along length
    TEST_F(TubeShapeTest, DistanceFromPoint2)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3(37.0f, 36.0f, 39.0f)) *
            AZ::Transform::CreateUniformScale(2.0f), 1.5f, entity);

        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 1, 0.2f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 2, 0.5f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 2.0f);

        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            distance, entity.GetId(), &LmbrCentral::ShapeComponentRequests::DistanceFromPoint, AZ::Vector3(39.0f, 41.0f, 39.0f));

        EXPECT_NEAR(distance, 1.0f, 1e-2f);
    }

    // distance scaled - along length
    TEST_F(TubeShapeTest, DistanceFromPointInsideTubeIsZero)
    {
        AZ::Entity entity;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3(37.0f, 36.0f, 39.0f)) *
            AZ::Transform::CreateUniformScale(2.0f), 1.5f, entity);

        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, 1.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 1, 0.2f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 2, 0.5f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, 2.0f);

        // The 3rd vertex located at (43, 36, 39) has a radius of 2 * (1.5 + 2), so a point that's 5 up on the y axis should
        // still be located inside the tube and have a distance of 0.
        float distance;
        LmbrCentral::ShapeComponentRequestsBus::EventResult(
            distance, entity.GetId(), &LmbrCentral::ShapeComponentRequests::DistanceFromPoint, AZ::Vector3(43.0f, 41.0f, 39.0f));

        EXPECT_NEAR(distance, 0.0f, 1e-2f);
    }

    TEST_F(TubeShapeTest, RadiiCannotBeNegativeFromVariableChange)
    {
        AZ::Entity entity;
        const float baseRadius = 1.0f;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3::CreateZero()), baseRadius, entity);

        // setting variable radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, -2.0f);

        float totalRadius = 0.0f;
        LmbrCentral::TubeShapeComponentRequestsBus::EventResult(
            totalRadius, entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::GetTotalRadius,
            AZ::SplineAddress());

        float variableRadius = 0.0f;
        LmbrCentral::TubeShapeComponentRequestsBus::EventResult(
            variableRadius, entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::GetVariableRadius,
            0);

        using ::testing::FloatEq;

        EXPECT_THAT(totalRadius, FloatEq(0.0f));
        EXPECT_THAT(variableRadius, FloatEq(-1.0f));
    }

    TEST_F(TubeShapeTest, RadiiCannotBeNegativeFromBaseChange)
    {
        AZ::Entity entity;
        const float baseRadius = 5.0f;
        CreateTube(
            AZ::Transform::CreateTranslation(AZ::Vector3::CreateZero()), baseRadius, entity);

        // setting variable radii
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, -2.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 1, -3.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 2, -4.0f);
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, -0.5f);

        // set base radius
        LmbrCentral::TubeShapeComponentRequestsBus::Event(
            entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetRadius, 1.0f);

        // expected (clamped) values
        AZStd::pair<float, float> totalAndVariableRadii[] = {
            { 0.0f, -1.0f }, { 0.0f, -1.0f }, { 0.0f, -1.0f }, { 0.5f, -0.5f } };

        using ::testing::FloatEq;

        // verify all expected values for each vertex
        for (size_t vertIndex = 0; vertIndex < 4; ++vertIndex)
        {
            const auto radiis = totalAndVariableRadii[vertIndex];

            float totalRadius = 0.0f;
            LmbrCentral::TubeShapeComponentRequestsBus::EventResult(
                totalRadius, entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::GetTotalRadius,
                AZ::SplineAddress(vertIndex));

            float variableRadius = 0.0f;
            LmbrCentral::TubeShapeComponentRequestsBus::EventResult(
                variableRadius, entity.GetId(), &LmbrCentral::TubeShapeComponentRequestsBus::Events::GetVariableRadius,
                static_cast<int>(vertIndex));

            EXPECT_THAT(totalRadius, FloatEq(radiis.first));
            EXPECT_THAT(variableRadius, FloatEq(radiis.second));
        }
    }

    TEST_F(TubeShapeTest, ShapeHasThreadsafeGetSetCalls)
    {
        // Verify that setting values from one thread and querying values from multiple other threads in parallel produces
        // correct, consistent results.

        // Create our sphere centered at 0 with half our height as the radius.
        AZ::Entity entity;
        CreateTube(AZ::Transform::CreateTranslation(AZ::Vector3::CreateZero()), ShapeThreadsafeTest::ShapeHeight / 2.0f, entity);

        // Define the function for setting unimportant dimensions on the shape while queries take place.
        auto setDimensionFn = [](AZ::EntityId shapeEntityId, float minDimension, uint32_t dimensionVariance, float height)
        {
            // Set the radius back to the same value. This should have no effect.
            LmbrCentral::TubeShapeComponentRequestsBus::Event(
                shapeEntityId, &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetRadius, height / 2.0f);

            // Set the end radii to random values. As long as we don't set the middle radii that describe the portion of the tube
            // that intersects the origin, changing these should have no effect on our test point distance.

            float radius = minDimension + aznumeric_cast<float>(rand() % dimensionVariance);
            LmbrCentral::TubeShapeComponentRequestsBus::Event(
                shapeEntityId, &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 0, radius);

            radius = minDimension + aznumeric_cast<float>(rand() % dimensionVariance);
            LmbrCentral::TubeShapeComponentRequestsBus::Event(
                shapeEntityId, &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 3, radius);
        };

        // Run the test, which will run multiple queries in parallel with each other and with the dimension-setting function.
        // The number of iterations is arbitrary - it's set high enough to catch most failures, but low enough to keep the test
        // time to a minimum.
        const int numIterations = 30000;
        ShapeThreadsafeTest::TestShapeGetSetCallsAreThreadsafe(entity, numIterations, setDimensionFn);
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 flip
    //
    // A flipped tube answers on the mirrored side of its entity. The spline it follows is the one SplineComponent saves and others
    // hold, so it is never written: a query mirrors the point or the ray, the bounds mirror their ends, and the drawn mesh mirrors
    // its points.

    namespace
    {
        constexpr AZ::u8 TubeFlipMaskX = 1;
        constexpr AZ::u8 TubeFlipMaskZ = 4;

        // Counts the shape changes of an entity, which is how everything downstream of a flip hears of it.
        class TubeShapeChangedCounter
            : public LmbrCentral::ShapeComponentNotificationsBus::Handler
        {
        public:
            explicit TubeShapeChangedCounter(AZ::EntityId entityId)
            {
                LmbrCentral::ShapeComponentNotificationsBus::Handler::BusConnect(entityId);
            }

            ~TubeShapeChangedCounter()
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

        void PushTubeFlipMask(AZ::EntityId entityId, AZ::u8 flipMask)
        {
            LmbrCentral::ShapeComponentRequestsBus::Event(entityId, &LmbrCentral::ShapeComponentRequests::SetFlipMask, flipMask);
        }

        // The spline lies in the plane z = 3, runs from x = 2 to x = 8 and bends in y. No axis mirror of it is the spline itself,
        // so a mirrored answer cannot be taken for an authored one.
        AZStd::vector<AZ::Vector3> GetFlippableSplineVertices()
        {
            return AZStd::vector<AZ::Vector3>{
                AZ::Vector3(2.0f, 0.0f, 3.0f), AZ::Vector3(4.0f, 1.0f, 3.0f), AZ::Vector3(6.0f, -1.0f, 3.0f), AZ::Vector3(8.0f, 0.0f, 3.0f)
            };
        }

        // A tube of radius 1 along that spline.
        void CreateFlippableTube(const AZ::Transform& world, AZ::Entity& entity)
        {
            CreateTube(world, 1.0f, entity);
            LmbrCentral::SplineComponentRequestBus::Event(
                entity.GetId(), &LmbrCentral::SplineComponentRequests::SetVertices, GetFlippableSplineVertices());
        }

        bool IsInsideTube(AZ::EntityId entityId, const AZ::Vector3& point)
        {
            bool inside = false;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(inside, entityId, &LmbrCentral::ShapeComponentRequests::IsPointInside, point);
            return inside;
        }

        float DistanceFromTube(AZ::EntityId entityId, const AZ::Vector3& point)
        {
            float distance = 0.0f;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                distance, entityId, &LmbrCentral::ShapeComponentRequests::DistanceFromPoint, point);
            return distance;
        }

        bool RayHitsTube(AZ::EntityId entityId, const AZ::Vector3& src, const AZ::Vector3& dir, float& distance)
        {
            bool hit = false;
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                hit, entityId, &LmbrCentral::ShapeComponentRequests::IntersectRay, src, dir, distance);
            return hit;
        }

        AZ::Aabb GetTubeLocalBounds(AZ::EntityId entityId, AZ::Transform& transform)
        {
            AZ::Aabb bounds = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndLocalBounds, transform, bounds);
            return bounds;
        }

        AZ::Aabb GetTubeAuthoredLocalBounds(AZ::EntityId entityId, AZ::Transform& transform)
        {
            AZ::Aabb bounds = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::Event(
                entityId, &LmbrCentral::ShapeComponentRequests::GetTransformAndAuthoredLocalBounds, transform, bounds);
            return bounds;
        }

        // The tube as authored and the same tube flipped have the same spline and radii, so any difference in what they answer at a
        // point or along a ray, one mirrored about the entity and one not, is the flip. They answer alike at mirrored places.
        void ExpectTubeQueriesFollowFlip(AZ::u8 flipMask, const AZ::Transform& world)
        {
            const AZ::Vector3 signs = LmbrCentral::FlipMaskToSigns(flipMask);

            AZ::Entity authored;
            CreateFlippableTube(world, authored);
            AZ::Entity flipped;
            CreateFlippableTube(world, flipped);
            PushTubeFlipMask(flipped.GetId(), flipMask);

            // Points in the local space of the entity: on the spline, around it, and far from it.
            const AZ::Vector3 localPoints[] = {
                AZ::Vector3(5.0f, 0.0f, 3.0f), AZ::Vector3(3.0f, 0.4f, 3.5f), AZ::Vector3(7.0f, -0.5f, 2.2f),
                AZ::Vector3(5.0f, 1.8f, 3.0f), AZ::Vector3(9.5f, 0.0f, 3.0f), AZ::Vector3(-5.0f, 0.0f, -3.0f)
            };
            for (const AZ::Vector3& localPoint : localPoints)
            {
                const AZ::Vector3 authoredPoint = world.TransformPoint(localPoint);
                const AZ::Vector3 flippedPoint = world.TransformPoint(localPoint * signs);

                EXPECT_EQ(IsInsideTube(flipped.GetId(), flippedPoint), IsInsideTube(authored.GetId(), authoredPoint));
                EXPECT_NEAR(DistanceFromTube(flipped.GetId(), flippedPoint), DistanceFromTube(authored.GetId(), authoredPoint), 1e-3f);
            }

            // The point on the spline is in the tube where it is drawn and not where it was authored.
            const AZ::Vector3 onSpline(5.0f, 0.0f, 3.0f);
            EXPECT_TRUE(IsInsideTube(authored.GetId(), world.TransformPoint(onSpline)));
            EXPECT_TRUE(IsInsideTube(flipped.GetId(), world.TransformPoint(onSpline * signs)));
            EXPECT_FALSE(IsInsideTube(flipped.GetId(), world.TransformPoint(onSpline)));
            EXPECT_NEAR(DistanceFromTube(flipped.GetId(), world.TransformPoint(onSpline * signs)), 0.0f, 1e-3f);
            EXPECT_GT(DistanceFromTube(flipped.GetId(), world.TransformPoint(onSpline)), 1.0f);

            // A ray from the side that crosses the spline. The mirror is an isometry, so the flipped tube is hit at the same distance
            // by the ray mirrored about the entity.
            const AZ::Vector3 localOrigin(5.0f, -6.0f, 3.2f);
            const AZ::Vector3 localDirection = AZ::Vector3(0.1f, 1.0f, -0.05f).GetNormalized();
            const AZ::Quaternion rotation = world.GetRotation();

            float authoredDistance = 0.0f;
            const bool authoredHit = RayHitsTube(
                authored.GetId(), world.TransformPoint(localOrigin), rotation.TransformVector(localDirection), authoredDistance);
            float flippedDistance = 0.0f;
            const bool flippedHit = RayHitsTube(
                flipped.GetId(), world.TransformPoint(localOrigin * signs), rotation.TransformVector(localDirection * signs),
                flippedDistance);
            EXPECT_TRUE(authoredHit);
            EXPECT_TRUE(flippedHit);
            EXPECT_NEAR(flippedDistance, authoredDistance, 1e-3f);
        }

        // The local bounds of the flipped tube are the authored ones with [min, max] turned into [-max, -min] on the flipped axes.
        // The encompassing box is those bounds in the world, on an entity that is moved and scaled and not rotated.
        void ExpectTubeBoundsFollowFlip(AZ::u8 flipMask, const AZ::Vector3& expectedMin, const AZ::Vector3& expectedMax)
        {
            const AZ::Vector3 translation(10.0f, 20.0f, 30.0f);
            const float scale = 2.0f;
            const AZ::Transform world(translation, AZ::Quaternion::CreateIdentity(), scale);

            AZ::Entity entity;
            CreateFlippableTube(world, entity);
            const AZ::EntityId entityId = entity.GetId();

            // The spline is x 2 to 8, y -1 to 1 and z 3, and the radius is 1.
            const AZ::Aabb authoredBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(1.0f, -2.0f, 2.0f), AZ::Vector3(9.0f, 2.0f, 4.0f));
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            EXPECT_THAT(GetTubeLocalBounds(entityId, transform), IsClose(authoredBounds));

            PushTubeFlipMask(entityId, flipMask);

            const AZ::Aabb expectedBounds = AZ::Aabb::CreateFromMinMax(expectedMin, expectedMax);
            EXPECT_THAT(GetTubeLocalBounds(entityId, transform), IsClose(expectedBounds));
            EXPECT_THAT(transform.GetTranslation(), IsClose(translation));

            // The bounds that are saved stay the authored ones.
            EXPECT_THAT(GetTubeAuthoredLocalBounds(entityId, transform), IsClose(authoredBounds));
            EXPECT_THAT(transform.GetTranslation(), IsClose(translation));

            AZ::Aabb encompassingAabb = AZ::Aabb::CreateNull();
            LmbrCentral::ShapeComponentRequestsBus::EventResult(
                encompassingAabb, entityId, &LmbrCentral::ShapeComponentRequests::GetEncompassingAabb);
            EXPECT_THAT(
                encompassingAabb, IsClose(AZ::Aabb::CreateFromMinMax(expectedMin * scale + translation, expectedMax * scale + translation)));

            // Taking the flip off restores the authored answers.
            PushTubeFlipMask(entityId, 0);
            EXPECT_THAT(GetTubeLocalBounds(entityId, transform), IsClose(authoredBounds));
        }
    } // namespace

    TEST_F(TubeShapeTest, QueriesFollowAnXFlip)
    {
        ExpectTubeQueriesFollowFlip(
            TubeFlipMaskX, AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateRotationZ(0.6f), 1.5f));
    }

    TEST_F(TubeShapeTest, QueriesFollowAZFlip)
    {
        ExpectTubeQueriesFollowFlip(
            TubeFlipMaskZ, AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateRotationZ(0.6f), 1.5f));
    }

    TEST_F(TubeShapeTest, QueriesFollowAnXZFlip)
    {
        ExpectTubeQueriesFollowFlip(
            TubeFlipMaskX | TubeFlipMaskZ, AZ::Transform(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateRotationZ(0.6f), 1.5f));
    }

    TEST_F(TubeShapeTest, BoundsFollowAnXFlip)
    {
        ExpectTubeBoundsFollowFlip(TubeFlipMaskX, AZ::Vector3(-9.0f, -2.0f, 2.0f), AZ::Vector3(-1.0f, 2.0f, 4.0f));
    }

    TEST_F(TubeShapeTest, BoundsFollowAZFlip)
    {
        ExpectTubeBoundsFollowFlip(TubeFlipMaskZ, AZ::Vector3(1.0f, -2.0f, -4.0f), AZ::Vector3(9.0f, 2.0f, -2.0f));
    }

    TEST_F(TubeShapeTest, BoundsFollowAnXZFlip)
    {
        ExpectTubeBoundsFollowFlip(TubeFlipMaskX | TubeFlipMaskZ, AZ::Vector3(-9.0f, -2.0f, -4.0f), AZ::Vector3(-1.0f, 2.0f, -2.0f));
    }

    // The ray is hit where the tube is drawn, and the distance is the one of the authored tube from the mirrored side.
    TEST_F(TubeShapeTest, FlippedTubeIsHitByARayAtTheDrawnDistance)
    {
        AZ::Entity entity;
        CreateFlippableTube(AZ::Transform::CreateIdentity(), entity);
        PushTubeFlipMask(entity.GetId(), TubeFlipMaskX);

        // The drawn spline runs from x = -2 to x = -8, and crosses y = 0 at x = -5. A ray along +y from y = -6 reaches it after 6,
        // and the radius is 1.
        float distance = 0.0f;
        EXPECT_TRUE(RayHitsTube(entity.GetId(), AZ::Vector3(-5.0f, -6.0f, 3.0f), AZ::Vector3(0.0f, 1.0f, 0.0f), distance));
        EXPECT_NEAR(distance, 5.0f, 1e-2f);

        // Nothing is where the tube was authored.
        EXPECT_FALSE(RayHitsTube(entity.GetId(), AZ::Vector3(5.0f, -6.0f, 3.0f), AZ::Vector3(0.0f, 1.0f, 0.0f), distance));
    }

    // The spline is the one SplineComponent saves. A flip leaves its vertices, and the radii, as they were authored.
    TEST_F(TubeShapeTest, FlipLeavesTheSplineAndTheRadiiAsAuthored)
    {
        AZ::Entity entity;
        CreateFlippableTube(AZ::Transform::CreateIdentity(), entity);
        const AZ::EntityId entityId = entity.GetId();
        LmbrCentral::TubeShapeComponentRequestsBus::Event(entityId, &LmbrCentral::TubeShapeComponentRequestsBus::Events::SetVariableRadius, 1, 0.5f);

        PushTubeFlipMask(entityId, TubeFlipMaskX | TubeFlipMaskZ);

        AZ::SplinePtr spline;
        LmbrCentral::SplineComponentRequestBus::EventResult(spline, entityId, &LmbrCentral::SplineComponentRequests::GetSpline);
        ASSERT_NE(spline.get(), nullptr);
        const AZStd::vector<AZ::Vector3> authoredVertices = GetFlippableSplineVertices();
        ASSERT_EQ(spline->GetVertices().size(), authoredVertices.size());
        for (size_t vertexIndex = 0; vertexIndex < authoredVertices.size(); ++vertexIndex)
        {
            EXPECT_THAT(spline->GetVertices()[vertexIndex], IsClose(authoredVertices[vertexIndex]));
        }

        float radius = 0.0f;
        LmbrCentral::TubeShapeComponentRequestsBus::EventResult(radius, entityId, &LmbrCentral::TubeShapeComponentRequestsBus::Events::GetRadius);
        EXPECT_NEAR(radius, 1.0f, 1e-6f);
        float variableRadius = 0.0f;
        LmbrCentral::TubeShapeComponentRequestsBus::EventResult(
            variableRadius, entityId, &LmbrCentral::TubeShapeComponentRequestsBus::Events::GetVariableRadius, 1);
        EXPECT_NEAR(variableRadius, 0.5f, 1e-6f);

        AZ::Vector3 flipSigns = AZ::Vector3::CreateOne();
        LmbrCentral::ShapeComponentRequestsBus::EventResult(flipSigns, entityId, &LmbrCentral::ShapeComponentRequests::GetFlipSigns);
        EXPECT_THAT(flipSigns, IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
    }

    TEST_F(TubeShapeTest, FlipRaisesAShapeChangeOnlyWhenTheMaskChanges)
    {
        AZ::Entity entity;
        CreateFlippableTube(AZ::Transform::CreateIdentity(), entity);
        TubeShapeChangedCounter counter(entity.GetId());

        PushTubeFlipMask(entity.GetId(), TubeFlipMaskX);
        EXPECT_EQ(counter.m_count, 1);

        // The same mask again moves nothing, and taking it off moves the tube back.
        PushTubeFlipMask(entity.GetId(), TubeFlipMaskX);
        EXPECT_EQ(counter.m_count, 1);

        PushTubeFlipMask(entity.GetId(), 0);
        EXPECT_EQ(counter.m_count, 2);
    }

    TEST_F(TubeShapeTest, DeactivatingTheShapeResetsTheFlip)
    {
        AZ::Entity entity;
        CreateFlippableTube(AZ::Transform::CreateIdentity(), entity);
        PushTubeFlipMask(entity.GetId(), TubeFlipMaskX);
        EXPECT_TRUE(IsInsideTube(entity.GetId(), AZ::Vector3(-5.0f, 0.0f, 3.0f)));

        // Nobody pushes anything after this activation, so a mirror that survived it would stay.
        entity.Deactivate();
        entity.Activate();

        EXPECT_TRUE(IsInsideTube(entity.GetId(), AZ::Vector3(5.0f, 0.0f, 3.0f)));
        EXPECT_FALSE(IsInsideTube(entity.GetId(), AZ::Vector3(-5.0f, 0.0f, 3.0f)));
    }

    // Game View draws the mesh of the tube where the tube is. The mesh is the authored one with its points mirrored.
    TEST_F(TubeShapeTest, GameViewMeshFollowsTheFlip)
    {
        LmbrCentral::TubeShapeMeshConfig meshConfig;
        AZ::Entity entity;
        entity.CreateComponent<AzFramework::TransformComponent>();
        entity.CreateComponent<LmbrCentral::SplineComponent>();
        entity.CreateComponent<LmbrCentral::TubeShapeComponent>();
        entity.CreateComponent<LmbrCentral::TubeShapeDebugDisplayComponent>(meshConfig);
        entity.Init();
        entity.Activate();
        LmbrCentral::SplineComponentRequestBus::Event(
            entity.GetId(), &LmbrCentral::SplineComponentRequests::SetVertices, GetFlippableSplineVertices());

        auto getDrawnBounds = [&entity]()
        {
            UnitTest::TestDebugDisplayRequests testDebugDisplayRequests;
            AzFramework::EntityDebugDisplayEventBus::Event(
                entity.GetId(), &AzFramework::EntityDebugDisplayEvents::DisplayEntityViewport, AzFramework::ViewportInfo{ 0 },
                testDebugDisplayRequests);
            const AZStd::vector<AZ::Vector3>& points = testDebugDisplayRequests.GetPoints();
            return points.empty() ? AZ::Aabb::CreateNull() : AZ::Aabb::CreatePoints(points.data(), points.size());
        };

        const AZ::Aabb authoredBounds = getDrawnBounds();
        ASSERT_TRUE(authoredBounds.IsValid());

        PushTubeFlipMask(entity.GetId(), TubeFlipMaskX);

        // [min, max] becomes [-max, -min] in x, and y and z stay.
        const AZ::Aabb expectedBounds = AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(-authoredBounds.GetMax().GetX(), authoredBounds.GetMin().GetY(), authoredBounds.GetMin().GetZ()),
            AZ::Vector3(-authoredBounds.GetMin().GetX(), authoredBounds.GetMax().GetY(), authoredBounds.GetMax().GetZ()));
        EXPECT_THAT(getDrawnBounds(), IsClose(expectedBounds));

        PushTubeFlipMask(entity.GetId(), 0);
        EXPECT_THAT(getDrawnBounds(), IsClose(authoredBounds));
    }

    TEST_F(TubeShapeTest, MeshPointsMirrorAndTheIndicesStay)
    {
        LmbrCentral::ShapeMesh mesh;
        mesh.m_vertexBuffer = { AZ::Vector3(1.0f, 2.0f, 3.0f), AZ::Vector3(-4.0f, 5.0f, -6.0f) };
        mesh.m_lineBuffer = { AZ::Vector3(7.0f, 8.0f, 9.0f), AZ::Vector3(0.0f, -1.0f, 2.0f) };
        mesh.m_indexBuffer = { 0, 1, 0 };

        LmbrCentral::MirrorTubeMeshPoints(LmbrCentral::FlipMaskToSigns(TubeFlipMaskX | TubeFlipMaskZ), mesh);

        EXPECT_THAT(mesh.m_vertexBuffer[0], IsClose(AZ::Vector3(-1.0f, 2.0f, -3.0f)));
        EXPECT_THAT(mesh.m_vertexBuffer[1], IsClose(AZ::Vector3(4.0f, 5.0f, 6.0f)));
        EXPECT_THAT(mesh.m_lineBuffer[0], IsClose(AZ::Vector3(-7.0f, 8.0f, -9.0f)));
        EXPECT_THAT(mesh.m_lineBuffer[1], IsClose(AZ::Vector3(0.0f, -1.0f, -2.0f)));
        EXPECT_EQ(mesh.m_indexBuffer.size(), 3u);
        EXPECT_EQ(mesh.m_indexBuffer[1], 1u);

        // A mesh that is not flipped is left alone.
        LmbrCentral::MirrorTubeMeshPoints(AZ::Vector3::CreateOne(), mesh);
        EXPECT_THAT(mesh.m_vertexBuffer[0], IsClose(AZ::Vector3(-1.0f, 2.0f, -3.0f)));
    }

    // The mirror is an isometry and its own inverse, and it fixes the origin of the entity.
    TEST_F(TubeShapeTest, RayMirroredAboutAnEntityIsAnIsometryAndItsOwnInverse)
    {
        const AZ::Transform world(AZ::Vector3(10.0f, 20.0f, 30.0f), AZ::Quaternion::CreateRotationZ(0.6f), 1.5f);
        const AZ::Vector3 flipSigns = LmbrCentral::FlipMaskToSigns(TubeFlipMaskX | TubeFlipMaskZ);
        const AZ::Vector3 origin(4.0f, -2.0f, 7.0f);
        const AZ::Vector3 direction = AZ::Vector3(1.0f, 2.0f, 3.0f).GetNormalized();

        AZ::Vector3 mirroredOrigin = origin;
        AZ::Vector3 mirroredDirection = direction;
        LmbrCentral::MirrorRayAboutEntity(world, flipSigns, mirroredOrigin, mirroredDirection);

        // Distances do not change: the direction keeps its length and the origin keeps its distance from the entity.
        EXPECT_NEAR(mirroredDirection.GetLength(), 1.0f, 1e-4f);
        EXPECT_NEAR((mirroredOrigin - world.GetTranslation()).GetLength(), (origin - world.GetTranslation()).GetLength(), 1e-3f);
        EXPECT_FALSE(mirroredOrigin.IsClose(origin));

        // Doing it twice gives the ray back.
        LmbrCentral::MirrorRayAboutEntity(world, flipSigns, mirroredOrigin, mirroredDirection);
        EXPECT_THAT(mirroredOrigin, IsClose(origin));
        EXPECT_THAT(mirroredDirection, IsClose(direction));

        // The origin of the entity is where the mirror stands.
        AZ::Vector3 entityOrigin = world.GetTranslation();
        AZ::Vector3 unusedDirection = direction;
        LmbrCentral::MirrorRayAboutEntity(world, flipSigns, entityOrigin, unusedDirection);
        EXPECT_THAT(entityOrigin, IsClose(world.GetTranslation()));

        // A ray that is not flipped is left alone.
        AZ::Vector3 sameOrigin = origin;
        AZ::Vector3 sameDirection = direction;
        LmbrCentral::MirrorRayAboutEntity(world, AZ::Vector3::CreateOne(), sameOrigin, sameDirection);
        EXPECT_THAT(sameOrigin, IsClose(origin));
        EXPECT_THAT(sameDirection, IsClose(direction));
    }
} // namespace UnitTest
