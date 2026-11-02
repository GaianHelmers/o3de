/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "FrameworkApplicationFixture.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/UnitTest/UnitTest.h>
#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzTest/AzTest.h>

namespace UnitTest
{
    //////////////////////////////////////////////////////////////////////////
    // Scale helpers

    class Transform2ScaleHelpersTest
        : public LeakDetectionFixture
    {
    protected:
        // Scales that do not fit Transform 1: every sign state, zero, extremes and unequal axes.
        static AZStd::vector<AZ::Vector3> GetTransform2Scales()
        {
            return {
                AZ::Vector3(-1.0f, 1.0f, 1.0f),
                AZ::Vector3(1.0f, 1.0f, -1.0f),
                AZ::Vector3(-1.0f, 1.0f, -1.0f),
                AZ::Vector3(-1.0f, -1.0f, -1.0f),
                AZ::Vector3(-1.0f, -1.0f, 1.0f),
                AZ::Vector3(1.0f, -1.0f, 1.0f),
                AZ::Vector3(2.0f, 1.0f, 1.0f),
                AZ::Vector3(1.0f, 1.0f, 2.0f),
                AZ::Vector3(-3.0f, 0.5f, 2.0f),
                AZ::Vector3(0.0f, 1.0f, 1.0f),
                AZ::Vector3(0.0f, 0.0f, 0.0f),
                AZ::Vector3(1.0e-5f, -1.0e-5f, 1.0e-5f),
                AZ::Vector3(1.0e12f, 1.0f, 1.0f),
                AZ::Vector3(-1.0e12f, -1.0e12f, -1.0e12f),
            };
        }
    };

    TEST_F(Transform2ScaleHelpersTest, FitsTransform1_EqualPositiveAxes_ReturnsTrue)
    {
        EXPECT_TRUE(AZ::Transform2::FitsTransform1(AZ::Vector3(1.0f)));
        EXPECT_TRUE(AZ::Transform2::FitsTransform1(AZ::Vector3(2.5f)));
        EXPECT_TRUE(AZ::Transform2::FitsTransform1(AZ::Vector3(0.001f)));
        // Float noise between the axes is still Transform 1.
        EXPECT_TRUE(AZ::Transform2::FitsTransform1(AZ::Vector3(1.0f, 1.0000001192f, 1.0000001192f)));
    }

    TEST_F(Transform2ScaleHelpersTest, FitsTransform1_NegativeZeroOrUnequalAxes_ReturnsFalse)
    {
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(1.0f, 1.0f, -1.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(-1.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(0.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(2.0f, 1.0f, 1.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(1.0f, 2.0f, 1.0f)));
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(AZ::Vector3(1.0f, 1.0f, 2.0f)));
    }

    TEST_F(Transform2ScaleHelpersTest, UniformStandIn_Transform1_IsExactAndNeverClamped)
    {
        EXPECT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(1.0f)), 1.0f);
        EXPECT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(2.5f)), 2.5f);
        // Below MinTransformScale, which only Transform 2 is clamped to.
        EXPECT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(0.0001f)), 0.0001f);
    }

    TEST_F(Transform2ScaleHelpersTest, UniformStandIn_Transform2_IsLargestMagnitude)
    {
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(-2.0f, 1.0f, 1.0f)), 2.0f);
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(1.0f, 1.0f, -1.0f)), 1.0f);
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(-1.0f, -1.0f, -1.0f)), 1.0f);
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(-3.0f, 0.5f, 2.0f)), 3.0f);
    }

    TEST_F(Transform2ScaleHelpersTest, UniformStandIn_Transform2_NeverNegativeOrZero)
    {
        for (const AZ::Vector3& scale : GetTransform2Scales())
        {
            const float standIn = AZ::Transform2::UniformStandIn(scale);
            EXPECT_GT(standIn, 0.0f);
            EXPECT_GE(standIn, AZ::MinTransformScale);
            EXPECT_LE(standIn, AZ::MaxTransformScale);
        }
    }

    TEST_F(Transform2ScaleHelpersTest, UniformStandIn_CollapsedOrHugeTransform2_ClampsToLimits)
    {
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(0.0f)), AZ::MinTransformScale);
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(AZ::Vector3(1.0e12f, 1.0f, 1.0f)), AZ::MaxTransformScale);
    }

    TEST_F(Transform2ScaleHelpersTest, IsNonUniform_ComparesMagnitudesOnly)
    {
        EXPECT_FALSE(AZ::Transform2::IsNonUniform(AZ::Vector3(1.0f)));
        EXPECT_FALSE(AZ::Transform2::IsNonUniform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_FALSE(AZ::Transform2::IsNonUniform(AZ::Vector3(1.0f, 1.0f, -1.0f)));
        EXPECT_FALSE(AZ::Transform2::IsNonUniform(AZ::Vector3(-2.0f, 2.0f, -2.0f)));
        EXPECT_FALSE(AZ::Transform2::IsNonUniform(AZ::Vector3(-1.0f)));
        EXPECT_TRUE(AZ::Transform2::IsNonUniform(AZ::Vector3(2.0f, 1.0f, 1.0f)));
        EXPECT_TRUE(AZ::Transform2::IsNonUniform(AZ::Vector3(1.0f, 2.0f, 1.0f)));
        EXPECT_TRUE(AZ::Transform2::IsNonUniform(AZ::Vector3(1.0f, 1.0f, 2.0f)));
        EXPECT_TRUE(AZ::Transform2::IsNonUniform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
    }

    TEST_F(Transform2ScaleHelpersTest, IsMirrored_SingleAxisFlipsOfXAndZ_FollowHandedness)
    {
        // Facing and upwards are the two flips that matter: four states, of which two are mirrored.
        EXPECT_FALSE(AZ::Transform2::IsMirrored(AZ::Vector3(1.0f, 1.0f, 1.0f)));
        EXPECT_TRUE(AZ::Transform2::IsMirrored(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_TRUE(AZ::Transform2::IsMirrored(AZ::Vector3(1.0f, 1.0f, -1.0f)));
        // Both flipped is a 180 degree rotation about Y, so handedness is kept.
        EXPECT_FALSE(AZ::Transform2::IsMirrored(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
    }

    TEST_F(Transform2ScaleHelpersTest, IsMirrored_OddNumberOfNegativeAxes_ReturnsTrue)
    {
        EXPECT_TRUE(AZ::Transform2::IsMirrored(AZ::Vector3(-1.0f, -1.0f, -1.0f)));
        EXPECT_TRUE(AZ::Transform2::IsMirrored(AZ::Vector3(1.0f, -2.0f, 1.0f)));
        EXPECT_FALSE(AZ::Transform2::IsMirrored(AZ::Vector3(-1.0f, -1.0f, 1.0f)));
        EXPECT_FALSE(AZ::Transform2::IsMirrored(AZ::Vector3(1.0f, -1.0f, -1.0f)));
    }

    TEST_F(Transform2ScaleHelpersTest, IsMirrored_NegativeUniformScale_IsTransform2AndMirrored)
    {
        const AZ::Vector3 scale(-1.0f, -1.0f, -1.0f);
        EXPECT_FALSE(AZ::Transform2::FitsTransform1(scale));
        EXPECT_FALSE(AZ::Transform2::IsNonUniform(scale));
        EXPECT_TRUE(AZ::Transform2::IsMirrored(scale));
        EXPECT_FLOAT_EQ(AZ::Transform2::UniformStandIn(scale), 1.0f);
        EXPECT_TRUE(AZ::Transform2::RenderScale(scale).IsClose(AZ::Vector3(-1.0f, -1.0f, -1.0f)));
    }

    TEST_F(Transform2ScaleHelpersTest, Residual_AnyScale_IsAlwaysPositive)
    {
        for (const AZ::Vector3& scale : GetTransform2Scales())
        {
            const AZ::Vector3 residual = AZ::Transform2::Residual(scale);
            EXPECT_GT(residual.GetX(), 0.0f);
            EXPECT_GT(residual.GetY(), 0.0f);
            EXPECT_GT(residual.GetZ(), 0.0f);
        }
    }

    TEST_F(Transform2ScaleHelpersTest, Residual_NonUniformScale_IsMagnitudeOverStandIn)
    {
        EXPECT_TRUE(AZ::Transform2::Residual(AZ::Vector3(-2.0f, 1.0f, 1.0f)).IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f)));
        EXPECT_TRUE(AZ::Transform2::Residual(AZ::Vector3(1.0f, 4.0f, -2.0f)).IsClose(AZ::Vector3(0.25f, 1.0f, 0.5f)));
    }

    TEST_F(Transform2ScaleHelpersTest, Residual_FlipWithEqualMagnitudes_IsOne)
    {
        EXPECT_TRUE(AZ::Transform2::Residual(AZ::Vector3(-1.0f, 1.0f, 1.0f)).IsClose(AZ::Vector3::CreateOne()));
        EXPECT_TRUE(AZ::Transform2::Residual(AZ::Vector3(-2.0f, 2.0f, -2.0f)).IsClose(AZ::Vector3::CreateOne()));
    }

    TEST_F(Transform2ScaleHelpersTest, RenderScale_Transform2_KeepsSign)
    {
        EXPECT_TRUE(AZ::Transform2::RenderScale(AZ::Vector3(-1.0f, 1.0f, 1.0f)).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_TRUE(AZ::Transform2::RenderScale(AZ::Vector3(1.0f, 1.0f, -1.0f)).IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f)));
        EXPECT_TRUE(AZ::Transform2::RenderScale(AZ::Vector3(-1.0f, 1.0f, -1.0f)).IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
        EXPECT_TRUE(AZ::Transform2::RenderScale(AZ::Vector3(-2.0f, 1.0f, 1.0f)).IsClose(AZ::Vector3(-1.0f, 0.5f, 0.5f)));
        EXPECT_TRUE(AZ::Transform2::RenderScale(AZ::Vector3(-2.0f, -2.0f, -2.0f)).IsClose(AZ::Vector3(-1.0f, -1.0f, -1.0f)));
    }

    TEST_F(Transform2ScaleHelpersTest, RenderScale_Transform1_IsOne)
    {
        EXPECT_TRUE(AZ::Transform2::RenderScale(AZ::Vector3(3.0f)).IsClose(AZ::Vector3::CreateOne()));
    }

    TEST_F(Transform2ScaleHelpersTest, RenderScale_TimesStandIn_ReproducesScale)
    {
        const AZ::Vector3 scales[] = {
            AZ::Vector3(-3.0f, 1.0f, 2.0f),
            AZ::Vector3(1.0f, -2.0f, 0.5f),
            AZ::Vector3(-1.0f, -1.0f, -1.0f),
            AZ::Vector3(2.0f, 2.0f, -2.0f),
        };
        for (const AZ::Vector3& scale : scales)
        {
            const AZ::Vector3 rebuilt = AZ::Transform2::RenderScale(scale) * AZ::Transform2::UniformStandIn(scale);
            EXPECT_TRUE(rebuilt.IsClose(scale, 1.0e-4f));
        }
    }

    TEST_F(Transform2ScaleHelpersTest, RenderScale_AnyScale_KeepsHandedness)
    {
        for (const AZ::Vector3& scale : GetTransform2Scales())
        {
            EXPECT_EQ(AZ::Transform2::IsMirrored(scale), AZ::Transform2::IsMirrored(AZ::Transform2::RenderScale(scale)));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform2Component

    // Records what a system listener would see from an entity with a Transform 2.
    class Transform2Listener
        : public AZ::Transform2NotificationBus::Handler
    {
    public:
        Transform2Listener()
        {
            AZ::Transform2NotificationBus::Handler::BusConnect();
        }

        ~Transform2Listener() override
        {
            AZ::Transform2NotificationBus::Handler::BusDisconnect();
        }

        void OnTransform2Activated([[maybe_unused]] AZ::EntityId entityId, const AZ::Vector3& scale) override
        {
            ++m_activatedCount;
            m_lastScale = scale;
        }

        void OnTransform2Deactivated([[maybe_unused]] AZ::EntityId entityId) override
        {
            ++m_deactivatedCount;
        }

        void OnTransform2ScaleChanged([[maybe_unused]] AZ::EntityId entityId, const AZ::Vector3& scale) override
        {
            ++m_scaleChangedCount;
            m_lastScale = scale;
            m_scaleChangedAt = ++m_sequence;
        }

        void OnTransform2WorldChanged(
            [[maybe_unused]] AZ::EntityId entityId, const AZ::Transform& world, const AZ::Vector3& scale) override
        {
            ++m_worldChangedCount;
            m_lastWorldUniformScale = world.GetUniformScale();
            m_lastScale = scale;
            m_worldChangedAt = ++m_sequence;
        }

        int m_activatedCount = 0;
        int m_deactivatedCount = 0;
        int m_scaleChangedCount = 0;
        int m_worldChangedCount = 0;
        int m_scaleChangedAt = 0; //!< Position in the order that events arrived.
        int m_worldChangedAt = 0;
        int m_sequence = 0;
        float m_lastWorldUniformScale = 0.0f;
        AZ::Vector3 m_lastScale = AZ::Vector3::CreateZero();
    };

    class Transform2ComponentTest
        : public FrameworkApplicationFixture
    {
    protected:
        // Builds what deploys for a Transform 2: the one Transform2Component, which replaces the Transform 1 on the entity.
        static AZStd::unique_ptr<AZ::Entity> CreateEntity(const AZ::Vector3& scale)
        {
            auto entity = AZStd::make_unique<AZ::Entity>();
            entity->CreateComponent<AzFramework::Transform2Component>(scale);
            return entity;
        }

        static AZStd::unique_ptr<AZ::Entity> CreateActiveEntity(const AZ::Vector3& scale)
        {
            AZStd::unique_ptr<AZ::Entity> entity = CreateEntity(scale);
            entity->Init();
            entity->Activate();
            return entity;
        }

        // As CreateActiveEntity, but the transform is static, so it refuses to be moved once the entity is active.
        static AZStd::unique_ptr<AZ::Entity> CreateActiveStaticEntity(const AZ::Vector3& scale)
        {
            auto entity = AZStd::make_unique<AZ::Entity>();
            AZ::TransformConfig config(AZ::Transform::CreateUniformScale(AZ::Transform2::UniformStandIn(scale)));
            config.m_isStatic = true;
            entity->CreateComponent<AzFramework::Transform2Component>(scale)->SetConfiguration(config);
            entity->Init();
            entity->Activate();
            return entity;
        }

        static float GetTransform1Scale(const AZ::Entity& entity)
        {
            float scale = 0.0f;
            AZ::TransformBus::EventResult(scale, entity.GetId(), &AZ::TransformBus::Events::GetLocalUniformScale);
            return scale;
        }

        static AZ::Vector3 GetTransform2Scale(const AZ::Entity& entity)
        {
            AZ::Vector3 scale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(scale, entity.GetId(), &AZ::Transform2Requests::GetLocalScale);
            return scale;
        }
    };

    TEST_F(Transform2ComponentTest, Init_NonUniformScale_ServesBothBusesBeforeActivate)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateEntity(AZ::Vector3(2.0f, 1.0f, 1.0f));
        entity->Init();

        EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entity->GetId()));
        EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entity->GetId()));

        AZ::Vector3 residual = AZ::Vector3::CreateZero();
        AZ::NonUniformScaleRequestBus::EventResult(residual, entity->GetId(), &AZ::NonUniformScaleRequests::GetScale);
        EXPECT_TRUE(residual.IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f)));
    }

    TEST_F(Transform2ComponentTest, Init_EqualMagnitudeFlip_DoesNotServeNonUniformScale)
    {
        // Presence on the non-uniform scale bus means non-uniform to its consumers, so a flip must not connect it.
        AZStd::unique_ptr<AZ::Entity> entity = CreateEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Init();

        EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entity->GetId()));
        EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entity->GetId()));
    }

    TEST_F(Transform2ComponentTest, ActivateAndDeactivate_RaiseNotificationsOnce)
    {
        Transform2Listener listener;
        AZStd::unique_ptr<AZ::Entity> entity = CreateEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Init();
        entity->Activate();

        EXPECT_EQ(listener.m_activatedCount, 1);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_EQ(listener.m_deactivatedCount, 0);

        entity->Deactivate();

        EXPECT_EQ(listener.m_activatedCount, 1);
        EXPECT_EQ(listener.m_deactivatedCount, 1);
    }

    TEST_F(Transform2ComponentTest, InactiveEntity_RaisesNoWorldOrScaleNotification_AndActivationCarriesTheScaleOfThatMoment)
    {
        // The listener is there before the entity, as a system is. The first world is computed at Init, before anyone was told that
        // the entity exists, and it is a flip: a listener that is told would hold a flip for an entity that it has no state for.
        Transform2Listener listener;
        AZStd::unique_ptr<AZ::Entity> entity = CreateEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Init();
        EXPECT_EQ(listener.m_worldChangedCount, 0);
        EXPECT_EQ(listener.m_scaleChangedCount, 0);
        entity->Activate();
        EXPECT_EQ(listener.m_activatedCount, 1);
        EXPECT_EQ(listener.m_worldChangedCount, 0);
        EXPECT_EQ(listener.m_scaleChangedCount, 0);

        // Writes while down: a new flip, and a move.
        entity->Deactivate();
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(1.0f, -1.0f, 1.0f));
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(4.0f, 0.0f, 0.0f));
        EXPECT_EQ(listener.m_worldChangedCount, 0);
        EXPECT_EQ(listener.m_scaleChangedCount, 0);

        // It comes back with the scale it has now, and that is the one that later changes are compared with.
        entity->Activate();
        EXPECT_EQ(listener.m_activatedCount, 2);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(1.0f, -1.0f, 1.0f)));

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(1.0f, -1.0f, 1.0f));
        EXPECT_EQ(listener.m_scaleChangedCount, 0);

        // A move says that the world changed, and not that the scale did.
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(5.0f, 0.0f, 0.0f));
        EXPECT_EQ(listener.m_worldChangedCount, 1);
        EXPECT_EQ(listener.m_scaleChangedCount, 0);

        // A real flip says that the scale did, and only that.
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(1.0f, -1.0f, -1.0f));
        EXPECT_EQ(listener.m_scaleChangedCount, 1);
        EXPECT_EQ(listener.m_worldChangedCount, 1);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(1.0f, -1.0f, -1.0f)));
    }

    TEST_F(Transform2ComponentTest, GetLocalScale_AfterActivate_ReturnsSignedScale)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-1.0f, 1.0f, -1.0f));

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-1.0f, 1.0f, -1.0f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_PureFlip_LeavesTheTransform1ViewAlone)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(1.0f, 1.0f, 1.0f));
        Transform2Listener listener;

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
        // The view did not change, so no transform event fires and only the scale notification does.
        EXPECT_EQ(listener.m_worldChangedCount, 0);
        EXPECT_EQ(listener.m_scaleChangedCount, 1);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_FlipBackAndForth_KeepsTransform1Unchanged)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(1.0f, 1.0f, 1.0f));
        Transform2Listener listener;

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(1.0f, 1.0f, 1.0f));
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, -1.0f));

        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
        EXPECT_EQ(listener.m_worldChangedCount, 0);
        EXPECT_EQ(listener.m_scaleChangedCount, 3);
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_StandInChanges_WritesTransform1AndRaisesWorldBeforeScale)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        Transform2Listener listener;

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-2.0f, 1.0f, 1.0f));

        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 2.0f);
        EXPECT_EQ(listener.m_worldChangedCount, 1);
        EXPECT_FLOAT_EQ(listener.m_lastWorldUniformScale, 2.0f);
        EXPECT_EQ(listener.m_scaleChangedCount, 1);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
        // The Transform 1 events and the world notification come before the scale notification.
        EXPECT_LT(listener.m_worldChangedAt, listener.m_scaleChangedAt);
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_SameScale_RaisesNothing)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        Transform2Listener listener;

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_EQ(listener.m_scaleChangedCount, 0);
        EXPECT_EQ(listener.m_worldChangedCount, 0);
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_ResidualChanges_SignalsResidualEvent)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(2.0f, 1.0f, 1.0f));

        int signalCount = 0;
        AZ::Vector3 lastResidual = AZ::Vector3::CreateZero();
        AZ::NonUniformScaleChangedEvent::Handler handler(
            [&signalCount, &lastResidual](const AZ::Vector3& residual)
            {
                ++signalCount;
                lastResidual = residual;
            });
        AZ::NonUniformScaleRequestBus::Event(entity->GetId(), &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, handler);

        // Same axis ratios, so the residual is unchanged.
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(4.0f, 2.0f, 2.0f));
        EXPECT_EQ(signalCount, 0);

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(4.0f, 1.0f, 1.0f));
        EXPECT_EQ(signalCount, 1);
        EXPECT_TRUE(lastResidual.IsClose(AZ::Vector3(1.0f, 0.25f, 0.25f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 4.0f);
    }

    TEST_F(Transform2ComponentTest, SetScale_ThroughNonUniformScaleBus_KeepsSigns)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-2.0f, 1.0f, 1.0f));

        AZ::NonUniformScaleRequestBus::Event(entity->GetId(), &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3(1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 2.0f);
    }

    TEST_F(Transform2ComponentTest, SetLocalScaleChecked_EntityWithTransform2_SetsScale)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(1.0f, 1.0f, 1.0f));

        AzFramework::Transform2Component::SetLocalScaleChecked(entity->GetId(), AZ::Vector3(1.0f, 1.0f, -1.0f));

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f)));
        EXPECT_TRUE(AzFramework::Transform2Component::GetLocalScaleChecked(entity->GetId()).IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f)));
    }

    TEST_F(Transform2ComponentTest, SetLocalScaleChecked_EntityWithoutTransform2_ReportsError)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        entity->CreateComponent<AzFramework::TransformComponent>();
        entity->Init();
        entity->Activate();

        AZ_TEST_START_TRACE_SUPPRESSION;
        AzFramework::Transform2Component::SetLocalScaleChecked(entity->GetId(), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);

        // The Transform 1 entity is untouched.
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
    }

    TEST_F(Transform2ComponentTest, GetLocalScaleChecked_EntityWithoutTransform2_ReportsErrorAndReadsOne)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        entity->CreateComponent<AzFramework::TransformComponent>();
        entity->Init();
        entity->Activate();

        // A script that reads the signed scale of a Transform 1 entity is told, and not handed a 1 in silence.
        AZ::Vector3 scale = AZ::Vector3::CreateZero();
        AZ_TEST_START_TRACE_SUPPRESSION;
        scale = AzFramework::Transform2Component::GetLocalScaleChecked(entity->GetId());
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);

        EXPECT_TRUE(scale.IsClose(AZ::Vector3::CreateOne()));
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes made through the TransformBus

    TEST_F(Transform2ComponentTest, TransformBusScaleWrite_Magnitude_FoldsIntoSignedScaleKeepingSigns)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-2.0f, 2.0f, 2.0f));
        Transform2Listener listener;

        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 4.0f);

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-4.0f, 4.0f, 4.0f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 4.0f);
        // The world notification already carries the folded scale.
        EXPECT_EQ(listener.m_worldChangedCount, 1);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(-4.0f, 4.0f, 4.0f)));
        // The world scale moved by more than 1e-5 of its magnitude, which is a scale change whatever made it.
        EXPECT_EQ(listener.m_scaleChangedCount, 1);
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_AfterTransformBusWrite_ComparesWithTheFoldedScale)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-2.0f, 2.0f, 2.0f));
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 4.0f);
        Transform2Listener listener;

        // The size that is drawn: there is nothing to do.
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-4.0f, 4.0f, 4.0f));
        EXPECT_EQ(listener.m_scaleChangedCount, 0);
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 4.0f);

        // The authored size: it differs from what is drawn, so it is taken and the float follows.
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-2.0f, 2.0f, 2.0f));
        EXPECT_EQ(listener.m_scaleChangedCount, 1);
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 2.0f);
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f)));
    }

    TEST_F(Transform2ComponentTest, TransformBusScaleWrite_ZeroThenSize_KeepsSignsAndRatios)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-4.0f, 2.0f, 1.0f));

        // Hiding an entity with zero must not erase the signs and ratios, which a vector scaled by zero could never get back.
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 0.0f);
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-1.0f, 0.5f, 0.25f) * AZ::MinTransformScale, 1.0e-5f));

        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 3.0f);
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-3.0f, 1.5f, 0.75f), 1.0e-3f));
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_AfterTransformBusZero_RestoresTheSize)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-4.0f, 2.0f, 1.0f));
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 0.0f);

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-4.0f, 2.0f, 1.0f));

        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 4.0f);
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-4.0f, 2.0f, 1.0f)));
    }

    // A vector with no magnitude has no ratios to keep and multiplying it grows nothing, so the written size is the value.
    TEST_F(Transform2ComponentTest, TransformBusScaleWrite_FromDeployedZeroScale_TakesTheSize)
    {
        // An entity authored at zero deploys with its configuration baked at the smallest scale.
        auto entity = AZStd::make_unique<AZ::Entity>();
        AZ::TransformConfig config(AZ::Transform::CreateUniformScale(AZ::MinTransformScale));
        entity->CreateComponent<AzFramework::Transform2Component>(AZ::Vector3::CreateZero())->SetConfiguration(config);
        entity->Init();
        entity->Activate();
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3::CreateZero()));

        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 1.0f);

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(1.0f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
    }

    TEST_F(Transform2ComponentTest, TransformBusScaleWrite_AfterSetLocalScaleToZero_TakesTheSize)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-4.0f, 2.0f, 1.0f));
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3::CreateZero());
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3::CreateZero()));

        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 1.0f);

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(1.0f)));
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
    }

    TEST_F(Transform2ComponentTest, Activate_ScaleWrittenToTransform1Before_IsFoldedBeforeListenersAreTold)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        entity->Init();
        // A spawn scale arrives through the TransformBus after Init and before Activate.
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 3.0f);
        Transform2Listener listener;

        entity->Activate();

        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-3.0f, 3.0f, 3.0f)));
        EXPECT_EQ(listener.m_activatedCount, 1);
        EXPECT_TRUE(listener.m_lastScale.IsClose(AZ::Vector3(-3.0f, 3.0f, 3.0f)));
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_StaticEntity_KeepsTheHeldSize)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveStaticEntity(AZ::Vector3(-2.0f, 2.0f, 2.0f));

        // A static entity refuses to change its size, so the signs and ratios land and the size stays.
        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(4.0f, -4.0f, 4.0f));

        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 2.0f);
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(2.0f, -2.0f, 2.0f)));
    }

    TEST_F(Transform2ComponentTest, SetLocalScale_StaticEntityToZero_TakesTheHeldSize)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveStaticEntity(AZ::Vector3(-2.0f, 2.0f, 2.0f));

        AZ::Transform2RequestBus::Event(entity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3::CreateZero());

        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 2.0f);
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(2.0f)));
    }

    TEST_F(Transform2ComponentTest, StaticEntity_RefusesToMove)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveStaticEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));

        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(5.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(0.0f, 5.0f, 0.0f));

        AZ::Vector3 position = AZ::Vector3::CreateOne();
        AZ::TransformBus::EventResult(position, entity->GetId(), &AZ::TransformBus::Events::GetWorldTranslation);
        EXPECT_TRUE(position.IsZero());
    }

    //////////////////////////////////////////////////////////////////////////
    // Script surface

    TEST_F(Transform2ComponentTest, IsExtended_EntityWithTransform2_ReturnsTrue)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(AzFramework::Transform2Component::IsExtended(entity->GetId()));
    }

    TEST_F(Transform2ComponentTest, IsExtended_EntityWithoutTransform2_ReturnsFalseWithoutError)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        entity->CreateComponent<AzFramework::TransformComponent>();
        entity->Init();
        entity->Activate();

        AZ_TEST_START_TRACE_SUPPRESSION;
        EXPECT_FALSE(AzFramework::Transform2Component::IsExtended(entity->GetId()));
        AZ_TEST_STOP_TRACE_SUPPRESSION(0);
    }

    TEST_F(Transform2ComponentTest, FlipChecked_EachAxis_NegatesOnlyThatAxis)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(1.0f, 1.0f, 1.0f));

        AzFramework::Transform2Component::FlipXChecked(entity->GetId());
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f)));

        AzFramework::Transform2Component::FlipYChecked(entity->GetId());
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-1.0f, -1.0f, 1.0f)));

        AzFramework::Transform2Component::FlipZChecked(entity->GetId());
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-1.0f, -1.0f, -1.0f)));
    }

    TEST_F(Transform2ComponentTest, FlipXChecked_Twice_ReturnsToStartAndLeavesTheSizeAlone)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(2.0f, 3.0f, 4.0f));
        Transform2Listener listener;

        AzFramework::Transform2Component::FlipXChecked(entity->GetId());
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(-2.0f, 3.0f, 4.0f)));

        AzFramework::Transform2Component::FlipXChecked(entity->GetId());
        EXPECT_TRUE(GetTransform2Scale(*entity).IsClose(AZ::Vector3(2.0f, 3.0f, 4.0f)));

        // A flip keeps the stand-in, so the Transform 1 view does not change.
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 4.0f);
        EXPECT_EQ(listener.m_worldChangedCount, 0);
        EXPECT_EQ(listener.m_scaleChangedCount, 2);
    }

    TEST_F(Transform2ComponentTest, FlipChecked_EntityWithoutTransform2_ReportsErrorForEachAxis)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        entity->CreateComponent<AzFramework::TransformComponent>();
        entity->Init();
        entity->Activate();

        AZ_TEST_START_TRACE_SUPPRESSION;
        AzFramework::Transform2Component::FlipXChecked(entity->GetId());
        AzFramework::Transform2Component::FlipYChecked(entity->GetId());
        AzFramework::Transform2Component::FlipZChecked(entity->GetId());
        AZ_TEST_STOP_TRACE_SUPPRESSION(3);

        // The Transform 1 entity is untouched.
        EXPECT_FLOAT_EQ(GetTransform1Scale(*entity), 1.0f);
    }

    TEST_F(Transform2ComponentTest, GetWorldScaleChecked_EntityWithTransform2_ReturnsTheWorldScale)
    {
        AZStd::unique_ptr<AZ::Entity> entity = CreateActiveEntity(AZ::Vector3(-2.0f, 1.0f, 1.0f));

        EXPECT_TRUE(AzFramework::Transform2Component::GetWorldScaleChecked(entity->GetId()).IsClose(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
        // No parent, so the world matrix is the local one: the scale is in the columns.
        const AZ::Matrix3x4 world = AzFramework::Transform2Component::GetWorldMatrixChecked(entity->GetId());
        EXPECT_TRUE(world.GetColumn(0).IsClose(AZ::Vector3(-2.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(world.GetColumn(1).IsClose(AZ::Vector3(0.0f, 1.0f, 0.0f)));
    }

    TEST_F(Transform2ComponentTest, GetWorldScaleChecked_EntityWithoutTransform2_ReportsError)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        entity->CreateComponent<AzFramework::TransformComponent>();
        entity->Init();
        entity->Activate();

        AZ_TEST_START_TRACE_SUPPRESSION;
        AzFramework::Transform2Component::GetWorldScaleChecked(entity->GetId());
        AzFramework::Transform2Component::GetWorldMatrixChecked(entity->GetId());
        AZ_TEST_STOP_TRACE_SUPPRESSION(2);
    }

    TEST_F(Transform2ComponentTest, Reflect_BehaviorContext_HoldsTheCheckedScriptMethodsAndNoRawBus)
    {
        AZ::BehaviorContext* behaviorContext = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(behaviorContext, &AZ::ComponentApplicationRequests::GetBehaviorContext);
        ASSERT_NE(behaviorContext, nullptr);

        const char* const methodNames[] = {
            "Transform2GetLocalScale", "Transform2SetLocalScale", "Transform2GetWorldScale", "Transform2GetWorldMatrix",
            "Transform2IsExtended", "Transform2FlipX", "Transform2FlipY", "Transform2FlipZ",
        };
        for (const char* methodName : methodNames)
        {
            EXPECT_NE(behaviorContext->FindMethodByReflectedName(methodName), nullptr) << methodName;
        }

        // A raw bus call on a Transform 1 entity finds no handler and silently does nothing, so the buses stay unreflected.
        EXPECT_EQ(behaviorContext->FindEBusByReflectedName("Transform2RequestBus"), nullptr);
        EXPECT_EQ(behaviorContext->FindEBusByReflectedName("Transform2NotificationBus"), nullptr);

        // The tracks of the cinematic system find the virtual properties of the transform through the class of the component.
        EXPECT_NE(behaviorContext->m_classes.find("Transform2Component"), behaviorContext->m_classes.end());
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 1 footprint (T.10)

    // Prototype pin. 720 is what sizeof(AzFramework::TransformComponent) measured on the MSVC x64 build of the Transform2-2026
    // branch, with the component file unchanged, so any growth of the Transform 1 component fails here. The Transform 2 derives
    // from it and adds its own members, so only the Transform 1 itself is pinned.
    // AZ::Transform is already pinned at 32 bytes by a static_assert in Transform.cpp and by a test in AzToolsFramework.Tests.
    // Other compilers and standard libraries lay the component out differently, so the pin is only checked on MSVC.
#if defined(AZ_COMPILER_MSVC)
    TEST(Transform1FootprintTest, PrototypePin_TransformComponentIs720Bytes)
    {
        EXPECT_EQ(sizeof(AzFramework::TransformComponent), 720u);
    }
#endif
} // namespace UnitTest
