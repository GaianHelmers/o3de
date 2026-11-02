/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

// Transform 2 in a hierarchy, on real entities that are initialized and activated.
//
// The expected values come from here and not from the code under test: nodes with a translation, a rotation and a signed scale,
// multiplied with plain matrix products (Matrix3x4 of the engine), and from numbers worked out by hand where a test says so.
// A Transform 2 derives its world from the state of its direct parent only, so these tests build chains and trees and
// compare every entity with the product of its whole chain.

#include "FrameworkApplicationFixture.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/EntityUtils.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Debug/TraceMessageBus.h>
#include <AzCore/IO/ByteContainerStream.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Serialization/ObjectStream.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/UnitTest/UnitTest.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzTest/AzTest.h>

namespace UnitTest
{
    namespace
    {
        //////////////////////////////////////////////////////////////////////////
        // The reference: nodes and matrix products

        // One link of a chain as authored: a translation, a rotation and a signed scale, in the space of its parent.
        struct Node
        {
            AZ::Vector3 m_translation = AZ::Vector3::CreateZero();
            AZ::Quaternion m_rotation = AZ::Quaternion::CreateIdentity();
            AZ::Vector3 m_scale = AZ::Vector3::CreateOne();
        };

        AZ::Matrix3x4 LocalMatrix(const Node& node)
        {
            return AZ::Matrix3x4::CreateFromQuaternionAndTranslation(node.m_rotation, node.m_translation) *
                AZ::Matrix3x4::CreateScale(node.m_scale);
        }

        // The exact world of the last node of a chain, root first.
        AZ::Matrix3x4 ReferenceWorld(const AZStd::vector<Node>& chain)
        {
            AZ::Matrix3x4 world = AZ::Matrix3x4::CreateIdentity();
            for (const Node& node : chain)
            {
                world = world * LocalMatrix(node);
            }
            return world;
        }

        // The float that the Transform 1 view of an entity holds for its own scale.
        float StandIn(const AZ::Vector3& scale)
        {
            const float largest = scale.GetAbs().GetMaxElement();
            return largest == 0.0f ? AZ::MinTransformScale : largest;
        }

        // Tolerances are relative to the largest element of what is expected, and at least 1.
        float ToleranceFor(const AZ::Matrix3x4& expected, float relative = 1.0e-4f)
        {
            float largest = 1.0f;
            for (int32_t row = 0; row < 3; ++row)
            {
                for (int32_t column = 0; column < 4; ++column)
                {
                    largest = AZ::GetMax(largest, AZ::GetAbs(expected.GetRow(row).GetElement(column)));
                }
            }
            return relative * largest;
        }

        bool MatricesMatch(const AZ::Matrix3x4& actual, const AZ::Matrix3x4& expected, float relative = 1.0e-4f)
        {
            return actual.IsClose(expected, ToleranceFor(expected, relative));
        }

        // A quaternion and its negative are one rotation.
        bool QuaternionsMatch(const AZ::Quaternion& a, const AZ::Quaternion& b, float tolerance = 1.0e-4f)
        {
            return AZ::GetAbs(a.Dot(b)) > 1.0f - tolerance;
        }

        // A deterministic generator, so a failure repeats.
        class Random
        {
        public:
            explicit Random(AZ::u32 seed)
                : m_state(seed)
            {
            }

            AZ::u32 Next()
            {
                m_state ^= m_state << 13;
                m_state ^= m_state >> 17;
                m_state ^= m_state << 5;
                return m_state;
            }

            float Range(float low, float high)
            {
                return low + (high - low) * static_cast<float>(Next() >> 8) / 16777216.0f;
            }

            float Sign()
            {
                return (Next() & 1u) ? -1.0f : 1.0f;
            }

        private:
            AZ::u32 m_state;
        };

        Node RandomNode(Random& random, bool equalMagnitudes)
        {
            Node node;
            node.m_translation = AZ::Vector3(random.Range(-5.0f, 5.0f), random.Range(-5.0f, 5.0f), random.Range(-5.0f, 5.0f));
            const AZ::Vector3 axis(random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f) + 1.5f);
            node.m_rotation = AZ::Quaternion::CreateFromAxisAngle(axis.GetNormalized(), random.Range(-3.1f, 3.1f));
            if (equalMagnitudes)
            {
                const float magnitude = random.Range(0.5f, 2.0f);
                node.m_scale = magnitude * AZ::Vector3(random.Sign(), random.Sign(), random.Sign());
            }
            else
            {
                node.m_scale = AZ::Vector3(
                    random.Sign() * random.Range(0.5f, 2.0f), random.Sign() * random.Range(0.5f, 2.0f), random.Sign() * random.Range(0.5f, 2.0f));
            }
            return node;
        }

        //////////////////////////////////////////////////////////////////////////
        // Readers: through the buses, as the rest of the engine reads

        AZ::Transform WorldTMOf(AZ::EntityId id)
        {
            AZ::Transform tm = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(tm, id, &AZ::TransformBus::Events::GetWorldTM);
            return tm;
        }

        AZ::Transform LocalTMOf(AZ::EntityId id)
        {
            AZ::Transform tm = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(tm, id, &AZ::TransformBus::Events::GetLocalTM);
            return tm;
        }

        AZ::Vector3 WorldTranslationOf(AZ::EntityId id)
        {
            AZ::Vector3 translation = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(translation, id, &AZ::TransformBus::Events::GetWorldTranslation);
            return translation;
        }

        AZ::Quaternion WorldRotationOf(AZ::EntityId id)
        {
            AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(rotation, id, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            return rotation;
        }

        AZ::Matrix3x4 WorldMatrixOf(AZ::EntityId id)
        {
            AZ::Matrix3x4 matrix = AZ::Matrix3x4::CreateIdentity();
            AZ::Transform2RequestBus::EventResult(matrix, id, &AZ::Transform2Requests::GetWorldMatrix);
            return matrix;
        }

        AZ::Vector3 WorldScaleOf(AZ::EntityId id)
        {
            AZ::Vector3 scale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(scale, id, &AZ::Transform2Requests::GetWorldScale);
            return scale;
        }

        AZ::Vector3 LocalScaleOf(AZ::EntityId id)
        {
            AZ::Vector3 scale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(scale, id, &AZ::Transform2Requests::GetLocalScale);
            return scale;
        }

        bool IsShearedOf(AZ::EntityId id)
        {
            bool sheared = false;
            AZ::Transform2RequestBus::EventResult(sheared, id, &AZ::Transform2Requests::IsWorldSheared);
            return sheared;
        }

        // The positive residual of the world scale, which the non-uniform scale bus serves. Zero when the entity does not serve it.
        AZ::Vector3 ResidualOf(AZ::EntityId id)
        {
            AZ::Vector3 residual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleRequestBus::EventResult(residual, id, &AZ::NonUniformScaleRequests::GetScale);
            return residual;
        }

        AZ::EntityId ParentIdOf(AZ::EntityId id)
        {
            AZ::EntityId parentId = AZ::EntityId(1234);
            AZ::TransformBus::EventResult(parentId, id, &AZ::TransformBus::Events::GetParentId);
            return parentId;
        }

        AZStd::vector<AZ::EntityId> ChildrenOf(AZ::EntityId id)
        {
            AZStd::vector<AZ::EntityId> children;
            AZ::TransformBus::EventResult(children, id, &AZ::TransformBus::Events::GetChildren);
            return children;
        }

        AZ::Quaternion RandomRotation(Random& random)
        {
            const AZ::Vector3 axis(random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f) + 1.5f);
            return AZ::Quaternion::CreateFromAxisAngle(axis.GetNormalized(), random.Range(-3.1f, 3.1f));
        }

        AZ::Vector3 RandomTranslation(Random& random, float range)
        {
            return AZ::Vector3(random.Range(-range, range), random.Range(-range, range), random.Range(-range, range));
        }

        //////////////////////////////////////////////////////////////////////////
        // Listeners

        // Records the Transform 2 notifications of every entity apart.
        class Transform2Recorder
            : public AZ::Transform2NotificationBus::Handler
        {
        public:
            struct Counts
            {
                int m_activated = 0;
                int m_deactivated = 0;
                int m_scaleChanged = 0;
                int m_worldChanged = 0;
                AZ::Vector3 m_lastScale = AZ::Vector3::CreateZero();
                AZ::Vector3 m_activatedScale = AZ::Vector3::CreateZero(); //!< The scale that the last activation carried.
                AZ::Vector3 m_worldChangedScale = AZ::Vector3::CreateZero(); //!< The scale that the last world change carried.
                AZ::Transform m_worldChangedTM = AZ::Transform::CreateIdentity(); //!< The transform that the last world change carried.
            };

            Transform2Recorder()
            {
                AZ::Transform2NotificationBus::Handler::BusConnect();
            }

            ~Transform2Recorder() override
            {
                AZ::Transform2NotificationBus::Handler::BusDisconnect();
            }

            Counts GetCounts(AZ::EntityId id) const
            {
                const auto found = m_counts.find(id);
                return found != m_counts.end() ? found->second : Counts();
            }

            int GetTotalScaleChanged() const
            {
                int total = 0;
                for (const auto& entry : m_counts)
                {
                    total += entry.second.m_scaleChanged;
                }
                return total;
            }

            void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override
            {
                Counts& counts = m_counts[entityId];
                ++counts.m_activated;
                counts.m_lastScale = scale;
                counts.m_activatedScale = scale;
            }

            void OnTransform2Deactivated(AZ::EntityId entityId) override
            {
                ++m_counts[entityId].m_deactivated;
            }

            void OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale) override
            {
                Counts& counts = m_counts[entityId];
                ++counts.m_scaleChanged;
                counts.m_lastScale = scale;
            }

            void OnTransform2WorldChanged(AZ::EntityId entityId, const AZ::Transform& world, const AZ::Vector3& scale) override
            {
                Counts& counts = m_counts[entityId];
                ++counts.m_worldChanged;
                counts.m_worldChangedScale = scale;
                counts.m_worldChangedTM = world;
            }

        private:
            AZStd::unordered_map<AZ::EntityId, Counts> m_counts;
        };

        // Counts the Transform 1 events of one entity: what its own components hear on the TransformNotificationBus.
        class ViewEventCounter
            : public AZ::TransformNotificationBus::Handler
        {
        public:
            explicit ViewEventCounter(AZ::EntityId entityId)
            {
                AZ::TransformNotificationBus::Handler::BusConnect(entityId);
            }

            ~ViewEventCounter() override
            {
                AZ::TransformNotificationBus::Handler::BusDisconnect();
            }

            void OnTransformChanged([[maybe_unused]] const AZ::Transform& local, [[maybe_unused]] const AZ::Transform& world) override
            {
                ++m_count;
            }

            int m_count = 0;
        };

        // Counts the warnings that are raised while it lives.
        class WarningCounter
            : public AZ::Debug::TraceMessageBus::Handler
        {
        public:
            WarningCounter()
            {
                AZ::Debug::TraceMessageBus::Handler::BusConnect();
            }

            ~WarningCounter() override
            {
                AZ::Debug::TraceMessageBus::Handler::BusDisconnect();
            }

            bool OnPreWarning(
                [[maybe_unused]] const char* window, [[maybe_unused]] const char* fileName, [[maybe_unused]] int line,
                [[maybe_unused]] const char* func, [[maybe_unused]] const char* message) override
            {
                ++m_count;
                return true;
            }

            int m_count = 0;
        };

        // Counts the hierarchy events that one entity hears on the TransformNotificationBus.
        class HierarchyProbe
            : public AZ::TransformNotificationBus::Handler
        {
        public:
            explicit HierarchyProbe(AZ::EntityId entityId)
            {
                AZ::TransformNotificationBus::Handler::BusConnect(entityId);
            }

            ~HierarchyProbe() override
            {
                AZ::TransformNotificationBus::Handler::BusDisconnect();
            }

            void OnParentChanged(AZ::EntityId oldParent, AZ::EntityId newParent) override
            {
                ++m_parentChanged;
                m_oldParent = oldParent;
                m_newParent = newParent;
            }

            void OnChildAdded(AZ::EntityId child) override
            {
                ++m_childAdded;
                m_lastAdded = child;
            }

            void OnChildRemoved(AZ::EntityId child) override
            {
                ++m_childRemoved;
                m_lastRemoved = child;
            }

            int m_parentChanged = 0;
            int m_childAdded = 0;
            int m_childRemoved = 0;
            AZ::EntityId m_oldParent;
            AZ::EntityId m_newParent;
            AZ::EntityId m_lastAdded;
            AZ::EntityId m_lastRemoved;
        };

        // Records, in one sequence that every probe shares, each signal that one entity raises, and what the entity and its parent
        // report about themselves from inside the signal.
        class PublishProbe
            : public AZ::TransformNotificationBus::Handler
            , public AZ::Transform2NotificationBus::Handler
        {
        public:
            // What an entity says of itself through the buses.
            struct State
            {
                AZ::Transform m_view = AZ::Transform::CreateIdentity();
                AZ::Matrix3x4 m_world = AZ::Matrix3x4::CreateIdentity();
                AZ::Vector3 m_scale = AZ::Vector3::CreateZero();
                AZ::Vector3 m_residual = AZ::Vector3::CreateZero();
            };

            struct Signal
            {
                int m_count = 0;
                int m_at = 0; //!< Place of the last one in the shared sequence.
                State m_own; //!< What the entity reported from inside the last one.
                State m_parent; //!< What its parent reported from inside the last one.
            };

            PublishProbe(AZ::EntityId entityId, AZ::EntityId parentId, int& sequence)
                : m_entityId(entityId)
                , m_parentId(parentId)
                , m_sequence(sequence)
                , m_residualHandler([this](const AZ::Vector3&) { Record(m_residualEvent); })
            {
                AZ::TransformNotificationBus::Handler::BusConnect(entityId);
                AZ::Transform2NotificationBus::Handler::BusConnect();
                AZ::NonUniformScaleRequestBus::Event(entityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, m_residualHandler);
            }

            ~PublishProbe() override
            {
                AZ::TransformNotificationBus::Handler::BusDisconnect();
                AZ::Transform2NotificationBus::Handler::BusDisconnect();
            }

            static State Observe(AZ::EntityId entityId)
            {
                State state;
                state.m_view = WorldTMOf(entityId);
                state.m_world = WorldMatrixOf(entityId);
                state.m_scale = WorldScaleOf(entityId);
                state.m_residual = ResidualOf(entityId);
                return state;
            }

            // Bit for bit.
            static bool Same(const State& a, const State& b)
            {
                return a.m_view == b.m_view && a.m_world == b.m_world && a.m_scale == b.m_scale && a.m_residual == b.m_residual;
            }

            void Reset()
            {
                m_viewEvent = Signal();
                m_residualEvent = Signal();
                m_worldChanged = Signal();
                m_scaleChanged = Signal();
            }

            // TransformNotificationBus, at the address of the entity
            void OnTransformChanged([[maybe_unused]] const AZ::Transform& local, [[maybe_unused]] const AZ::Transform& world) override
            {
                Record(m_viewEvent);
            }

            // Transform2NotificationBus
            void OnTransform2WorldChanged(AZ::EntityId entityId, [[maybe_unused]] const AZ::Transform& world, [[maybe_unused]] const AZ::Vector3& scale) override
            {
                if (entityId == m_entityId)
                {
                    Record(m_worldChanged);
                }
            }

            void OnTransform2ScaleChanged(AZ::EntityId entityId, [[maybe_unused]] const AZ::Vector3& scale) override
            {
                if (entityId == m_entityId)
                {
                    Record(m_scaleChanged);
                }
            }

            Signal m_viewEvent; //!< The Transform 1 event.
            Signal m_residualEvent; //!< The non-uniform scale changed event.
            Signal m_worldChanged; //!< OnTransform2WorldChanged.
            Signal m_scaleChanged; //!< OnTransform2ScaleChanged.

        private:
            void Record(Signal& signal)
            {
                ++signal.m_count;
                signal.m_at = ++m_sequence;
                signal.m_own = Observe(m_entityId);
                if (m_parentId.IsValid())
                {
                    signal.m_parent = Observe(m_parentId);
                }
            }

            AZ::EntityId m_entityId;
            AZ::EntityId m_parentId;
            int& m_sequence;
            AZ::NonUniformScaleChangedEvent::Handler m_residualHandler;
        };

        // A component that already serves the non-uniform scale bus of an entity.
        class ForeignScaleHandler
            : public AZ::NonUniformScaleRequestBus::Handler
        {
        public:
            static constexpr float Marker = 7.0f;

            explicit ForeignScaleHandler(AZ::EntityId entityId)
            {
                AZ::NonUniformScaleRequestBus::Handler::BusConnect(entityId);
            }

            ~ForeignScaleHandler() override
            {
                AZ::NonUniformScaleRequestBus::Handler::BusDisconnect();
            }

            AZ::Vector3 GetScale() const override
            {
                return AZ::Vector3(Marker);
            }

            void SetScale(const AZ::Vector3&) override
            {
            }

            void RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler&) override
            {
            }
        };
    } // namespace

    //////////////////////////////////////////////////////////////////////////
    // Fixture

    class Transform2HierarchyTest
        : public FrameworkApplicationFixture
    {
    protected:
        void TearDown() override
        {
            // Children go before their parents.
            while (!m_entities.empty())
            {
                m_entities.pop_back();
            }
            FrameworkApplicationFixture::TearDown();
        }

        // Builds one Transform 2 entity as a spawn would: configured with its authored local transform and its parent,
        // not initialized yet.
        AZ::Entity* BuildEntity(const Node& node, AZ::EntityId parentId = AZ::EntityId())
        {
            AZ::Transform local = AZ::Transform::CreateFromQuaternionAndTranslation(node.m_rotation, node.m_translation);
            local.SetUniformScale(StandIn(node.m_scale));
            AZ::TransformConfig config;
            config.m_localTransform = local;
            config.m_worldTransform = local;
            config.m_parentId = parentId;
            return BuildEntityFromConfig(node.m_scale, config);
        }

        // As BuildEntity, with the configuration given. A spawn can hand a configuration whose local and world floats differ.
        AZ::Entity* BuildEntityFromConfig(const AZ::Vector3& scale, const AZ::TransformConfig& config)
        {
            auto entity = AZStd::make_unique<AZ::Entity>();
            entity->CreateComponent<AzFramework::Transform2Component>(scale)->SetConfiguration(config);

            m_entities.push_back(AZStd::move(entity));
            return m_entities.back().get();
        }

        // As BuildEntity, then initialized and activated. A parent has to be created before its children.
        AZ::Entity* CreateEntity(const Node& node, AZ::EntityId parentId = AZ::EntityId())
        {
            AZ::Entity* entity = BuildEntity(node, parentId);
            entity->Init();
            entity->Activate();
            return entity;
        }

        // An entity with the Transform 1 only.
        AZ::Entity* CreateTransform1Entity()
        {
            auto entity = AZStd::make_unique<AZ::Entity>();
            entity->CreateComponent<AzFramework::TransformComponent>();
            entity->Init();
            entity->Activate();
            m_entities.push_back(AZStd::move(entity));
            return m_entities.back().get();
        }

        void DestroyEntity(AZ::Entity* entity)
        {
            const auto found = AZStd::find_if(
                m_entities.begin(), m_entities.end(), [entity](const AZStd::unique_ptr<AZ::Entity>& owned) { return owned.get() == entity; });
            ASSERT_TRUE(found != m_entities.end());
            m_entities.erase(found);
        }

        // The world matrix of the entity is the product of its chain, and its translation is the true position.
        void ExpectWorld(AZ::EntityId id, const AZStd::vector<Node>& chain, const char* label) const
        {
            const AZ::Matrix3x4 expected = ReferenceWorld(chain);
            EXPECT_TRUE(MatricesMatch(WorldMatrixOf(id), expected)) << label;
            EXPECT_TRUE(WorldTranslationOf(id).IsClose(expected.GetTranslation(), ToleranceFor(expected))) << label;
        }

        // The Transform 1 view and the signed world scale together draw the exact world.
        void ExpectViewMatchesWorldScale(AZ::EntityId id, const AZStd::vector<Node>& chain, const char* label) const
        {
            const AZ::Transform view = WorldTMOf(id);
            const AZ::Matrix3x4 drawn = AZ::Matrix3x4::CreateFromQuaternionAndTranslation(view.GetRotation(), view.GetTranslation()) *
                AZ::Matrix3x4::CreateScale(WorldScaleOf(id));
            EXPECT_TRUE(MatricesMatch(drawn, ReferenceWorld(chain))) << label;
        }

        // The authored values that the TransformBus Local calls report.
        void ExpectAuthored(AZ::EntityId id, const Node& node, const char* label) const
        {
            const AZ::Transform local = LocalTMOf(id);
            EXPECT_TRUE(local.GetTranslation().IsClose(node.m_translation, 1.0e-4f)) << label;
            EXPECT_TRUE(QuaternionsMatch(local.GetRotation(), node.m_rotation)) << label;
            EXPECT_TRUE(LocalScaleOf(id).IsClose(node.m_scale, 1.0e-4f)) << label;
            EXPECT_NEAR(local.GetUniformScale(), StandIn(node.m_scale), 1.0e-4f) << label;
        }

        AZStd::vector<AZStd::unique_ptr<AZ::Entity>> m_entities;
    };

    //////////////////////////////////////////////////////////////////////////
    // A mirrored parent: the position, rotation and scale of a child

    TEST_F(Transform2HierarchyTest, ChildFarRight_UnderAFlippedParent_IsFarLeft)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);

        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());

        // 10 - 5 on x, worked out by hand.
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(5.0f, 2.0f, 3.0f), 1.0e-4f));
        float worldX = 0.0f;
        AZ::TransformBus::EventResult(worldX, childEntity->GetId(), &AZ::TransformBus::Events::GetWorldX);
        EXPECT_NEAR(worldX, 5.0f, 1.0e-4f);
        ExpectWorld(childEntity->GetId(), { parent, child }, "flipped parent");
        EXPECT_TRUE(WorldScaleOf(childEntity->GetId()).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));

        // The control: the same pair with a parent that is not flipped is far right.
        Node plainParent = parent;
        plainParent.m_scale = AZ::Vector3::CreateOne();
        AZ::Entity* plainParentEntity = CreateEntity(plainParent);
        AZ::Entity* plainChildEntity = CreateEntity(child, plainParentEntity->GetId());
        EXPECT_TRUE(WorldTranslationOf(plainChildEntity->GetId()).IsClose(AZ::Vector3(15.0f, 2.0f, 3.0f), 1.0e-4f));
    }

    TEST_F(Transform2HierarchyTest, LiveFlip_ReachesEveryDescendantThreeLevelsDown)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        Node a; // a child of the parent
        a.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);
        Node b; // a child of the parent that sits on the plane of the flip
        b.m_translation = AZ::Vector3(0.0f, 4.0f, 0.0f);
        Node g; // a child of a
        g.m_translation = AZ::Vector3(1.0f, 0.0f, 0.0f);
        Node h; // a child of g: three levels below the parent
        h.m_translation = AZ::Vector3(0.0f, 0.0f, 2.0f);

        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* aEntity = CreateEntity(a, parentEntity->GetId());
        AZ::Entity* bEntity = CreateEntity(b, parentEntity->GetId());
        AZ::Entity* gEntity = CreateEntity(g, aEntity->GetId());
        AZ::Entity* hEntity = CreateEntity(h, gEntity->GetId());
        const AZ::EntityId parentId = parentEntity->GetId();
        Transform2Recorder recorder;

        // Nothing is called on a descendant from here on.
        Node flipped = parent;
        flipped.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, flipped.m_scale);

        ExpectWorld(aEntity->GetId(), { flipped, a }, "a flipped");
        ExpectWorld(bEntity->GetId(), { flipped, b }, "b flipped");
        ExpectWorld(gEntity->GetId(), { flipped, a, g }, "g flipped");
        ExpectWorld(hEntity->GetId(), { flipped, a, g, h }, "h flipped");
        // The positions worked out by hand.
        EXPECT_TRUE(WorldTranslationOf(aEntity->GetId()).IsClose(AZ::Vector3(5.0f, 2.0f, 3.0f), 1.0e-4f));
        EXPECT_TRUE(WorldTranslationOf(bEntity->GetId()).IsClose(AZ::Vector3(10.0f, 6.0f, 3.0f), 1.0e-4f));
        EXPECT_TRUE(WorldTranslationOf(gEntity->GetId()).IsClose(AZ::Vector3(4.0f, 2.0f, 3.0f), 1.0e-4f));
        EXPECT_TRUE(WorldTranslationOf(hEntity->GetId()).IsClose(AZ::Vector3(4.0f, 2.0f, 5.0f), 1.0e-4f));

        // Every entity took the mask. The ones whose position moved heard a world change, the others did not.
        for (const AZ::Entity* entity : { parentEntity, aEntity, bEntity, gEntity, hEntity })
        {
            const Transform2Recorder::Counts counts = recorder.GetCounts(entity->GetId());
            EXPECT_EQ(counts.m_scaleChanged, 1);
            EXPECT_TRUE(counts.m_lastScale.IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));
        }
        EXPECT_EQ(recorder.GetCounts(parentId).m_worldChanged, 0);
        EXPECT_EQ(recorder.GetCounts(aEntity->GetId()).m_worldChanged, 1);
        EXPECT_EQ(recorder.GetCounts(bEntity->GetId()).m_worldChanged, 0);
        EXPECT_EQ(recorder.GetCounts(gEntity->GetId()).m_worldChanged, 1);
        EXPECT_EQ(recorder.GetCounts(hEntity->GetId()).m_worldChanged, 1);

        // Flipping back restores every entity.
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3::CreateOne());
        ExpectWorld(aEntity->GetId(), { parent, a }, "a restored");
        ExpectWorld(bEntity->GetId(), { parent, b }, "b restored");
        ExpectWorld(gEntity->GetId(), { parent, a, g }, "g restored");
        ExpectWorld(hEntity->GetId(), { parent, a, g, h }, "h restored");
        EXPECT_TRUE(WorldTranslationOf(hEntity->GetId()).IsClose(AZ::Vector3(16.0f, 2.0f, 5.0f), 1.0e-4f));
        EXPECT_TRUE(WorldScaleOf(hEntity->GetId()).IsClose(AZ::Vector3::CreateOne(), 1.0e-5f));
        EXPECT_EQ(recorder.GetCounts(hEntity->GetId()).m_scaleChanged, 2);

        // And a script's flip does the same.
        AzFramework::Transform2Component::FlipXChecked(parentId);
        ExpectWorld(hEntity->GetId(), { flipped, a, g, h }, "h flipped again");
        EXPECT_EQ(recorder.GetCounts(hEntity->GetId()).m_scaleChanged, 3);
    }

    TEST_F(Transform2HierarchyTest, ParentFlippedOnEachAxis_IdentityChildTakesTheRotationAndTheMask)
    {
        const AZ::Vector3 flips[] = { AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(1.0f, -1.0f, 1.0f), AZ::Vector3(1.0f, 1.0f, -1.0f) };
        for (const AZ::Vector3& flip : flips)
        {
            Node parent;
            parent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
            parent.m_rotation = AZ::Quaternion::CreateRotationZ(0.5f);
            parent.m_scale = flip;
            const Node child{}; // identity

            AZ::Entity* parentEntity = CreateEntity(parent);
            AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
            const AZ::EntityId childId = childEntity->GetId();

            EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(childId), parent.m_rotation));
            EXPECT_TRUE(WorldScaleOf(childId).IsClose(flip, 1.0e-5f));
            EXPECT_TRUE(MatricesMatch(WorldMatrixOf(childId), WorldMatrixOf(parentEntity->GetId())));
            ExpectWorld(childId, { parent, child }, "identity child");
            EXPECT_FALSE(IsShearedOf(childId));
        }
    }

    TEST_F(Transform2HierarchyTest, DoubleMirror_Cancels)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationY(0.4f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(2.0f, 0.0f, 0.0f);
        child.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);

        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId childId = childEntity->GetId();

        ExpectWorld(childId, { parent, child }, "double mirror");
        EXPECT_TRUE(WorldScaleOf(childId).IsClose(AZ::Vector3::CreateOne(), 1.0e-5f));
        EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(childId), parent.m_rotation));
        ExpectViewMatchesWorldScale(childId, { parent, child }, "double mirror");

        // The third mirror brings the flip back.
        Node grandchild;
        grandchild.m_scale = AZ::Vector3(1.0f, 1.0f, -1.0f);
        AZ::Entity* grandchildEntity = CreateEntity(grandchild, childId);
        ExpectWorld(grandchildEntity->GetId(), { parent, child, grandchild }, "triple mirror");
        EXPECT_TRUE(WorldScaleOf(grandchildEntity->GetId()).IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, RotatedChild_UnderAMirroredParent_IsConjugated)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);

        // A turn about z becomes the opposite turn. The columns of the world, worked out by hand:
        // the rotation takes x to y and y to -x, and the mirror then takes the x of each column away from it.
        Node turnedZ;
        turnedZ.m_rotation = AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi);
        AZ::Entity* zEntity = CreateEntity(turnedZ, parentEntity->GetId());
        const AZ::Matrix3x4 zWorld = WorldMatrixOf(zEntity->GetId());
        EXPECT_TRUE(zWorld.GetColumn(0).IsClose(AZ::Vector3(0.0f, 1.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(zWorld.GetColumn(1).IsClose(AZ::Vector3(1.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(zWorld.GetColumn(2).IsClose(AZ::Vector3(0.0f, 0.0f, 1.0f), 1.0e-4f));
        EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(zEntity->GetId()), AZ::Quaternion::CreateRotationZ(-AZ::Constants::HalfPi)));
        ExpectWorld(zEntity->GetId(), { parent, turnedZ }, "turn about z");

        // A turn about the mirrored axis is its own mirror image.
        Node turnedX;
        turnedX.m_rotation = AZ::Quaternion::CreateRotationX(0.7f);
        AZ::Entity* xEntity = CreateEntity(turnedX, parentEntity->GetId());
        EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(xEntity->GetId()), AZ::Quaternion::CreateRotationX(0.7f)));
        ExpectWorld(xEntity->GetId(), { parent, turnedX }, "turn about x");

        Node turnedY;
        turnedY.m_rotation = AZ::Quaternion::CreateRotationY(0.7f);
        AZ::Entity* yEntity = CreateEntity(turnedY, parentEntity->GetId());
        EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(yEntity->GetId()), AZ::Quaternion::CreateRotationY(-0.7f)));
        ExpectWorld(yEntity->GetId(), { parent, turnedY }, "turn about y");
    }

    TEST_F(Transform2HierarchyTest, NonUniformParent_RotatedChild_HasTheExactMatrixAndTheAuthoredRotation)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(2.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(1.0f, 2.0f, 0.0f);
        child.m_rotation = AZ::Quaternion::CreateRotationZ(AZ::Constants::QuarterPi);

        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId childId = childEntity->GetId();

        // The matrix worked out by hand: diag(2,1,1) times a turn of 45 degrees, and the child moved by (2,2,0).
        const AZ::Matrix3x4 world = WorldMatrixOf(childId);
        EXPECT_TRUE(world.GetColumn(0).IsClose(AZ::Vector3(1.41421356f, 0.70710678f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(world.GetColumn(1).IsClose(AZ::Vector3(-1.41421356f, 0.70710678f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(world.GetColumn(2).IsClose(AZ::Vector3(0.0f, 0.0f, 1.0f), 1.0e-4f));
        EXPECT_TRUE(world.GetTranslation().IsClose(AZ::Vector3(2.0f, 2.0f, 0.0f), 1.0e-4f));
        ExpectWorld(childId, { parent, child }, "non uniform parent");

        // The rotation is the authored product, and the shear stays in the matrix.
        EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(childId), child.m_rotation));
        EXPECT_TRUE(IsShearedOf(childId));
        EXPECT_FALSE(IsShearedOf(parentEntity->GetId()));
        EXPECT_TRUE(WorldScaleOf(parentEntity->GetId()).IsClose(AZ::Vector3(2.0f, 1.0f, 1.0f), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, RandomChains_MatchTheMatrixProduct)
    {
        Random random(20261101u);
        for (int pass = 0; pass < 2; ++pass)
        {
            const bool equalMagnitudes = (pass == 0);
            for (int trial = 0; trial < 30; ++trial)
            {
                const int depth = 1 + static_cast<int>(random.Next() % 6u);
                AZStd::vector<Node> chain;
                AZ::EntityId parentId;
                for (int level = 0; level < depth; ++level)
                {
                    chain.push_back(RandomNode(random, equalMagnitudes));
                    AZ::Entity* entity = CreateEntity(chain.back(), parentId);
                    parentId = entity->GetId();

                    ExpectWorld(parentId, chain, equalMagnitudes ? "equal magnitudes" : "unequal magnitudes");
                    if (equalMagnitudes)
                    {
                        // Without shear the view and the signed scale draw the exact world.
                        ExpectViewMatchesWorldScale(parentId, chain, "equal magnitudes");
                        EXPECT_FALSE(IsShearedOf(parentId));
                    }
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Every Local and World call of the TransformBus on a Transform 2 under a mirrored parent

    TEST_F(Transform2HierarchyTest, TransformBusCalls_UnderAMirroredParent_MatchTheReference)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);

        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId id = childEntity->GetId();

        Node expected = child;
        const auto verify = [&](const char* label)
        {
            ExpectAuthored(id, expected, label);
            ExpectWorld(id, { parent, expected }, label);
        };
        verify("created");

        // Local calls are the authored values, in the space of the flipped parent.
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(1.0f, 2.0f, 3.0f));
        expected.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        verify("SetLocalTranslation");

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalX, 5.0f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalY, 6.0f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalZ, 7.0f);
        expected.m_translation = AZ::Vector3(5.0f, 6.0f, 7.0f);
        verify("SetLocalX Y Z");
        float localX = 0.0f;
        float localY = 0.0f;
        float localZ = 0.0f;
        AZ::TransformBus::EventResult(localX, id, &AZ::TransformBus::Events::GetLocalX);
        AZ::TransformBus::EventResult(localY, id, &AZ::TransformBus::Events::GetLocalY);
        AZ::TransformBus::EventResult(localZ, id, &AZ::TransformBus::Events::GetLocalZ);
        EXPECT_EQ(localX, 5.0f);
        EXPECT_EQ(localY, 6.0f);
        EXPECT_EQ(localZ, 7.0f);
        AZ::Vector3 localTranslation = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(localTranslation, id, &AZ::TransformBus::Events::GetLocalTranslation);
        EXPECT_TRUE(localTranslation == AZ::Vector3(5.0f, 6.0f, 7.0f));

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalRotation, AZ::Vector3(0.0f, 0.0f, AZ::Constants::HalfPi));
        expected.m_rotation = AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi);
        verify("SetLocalRotation");

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalRotationQuaternion, AZ::Quaternion::CreateRotationX(0.5f));
        expected.m_rotation = AZ::Quaternion::CreateRotationX(0.5f);
        verify("SetLocalRotationQuaternion");
        AZ::Quaternion localRotation = AZ::Quaternion::CreateZero();
        AZ::TransformBus::EventResult(localRotation, id, &AZ::TransformBus::Events::GetLocalRotationQuaternion);
        EXPECT_TRUE(QuaternionsMatch(localRotation, expected.m_rotation));
        AZ::Vector3 localEuler = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(localEuler, id, &AZ::TransformBus::Events::GetLocalRotation);
        EXPECT_TRUE(localEuler.IsClose(AZ::Vector3(0.5f, 0.0f, 0.0f), 1.0e-4f));

        // A turn about an own axis, each from the identity.
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalRotationQuaternion, AZ::Quaternion::CreateIdentity());
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::RotateAroundLocalX, 0.3f);
        expected.m_rotation = AZ::Quaternion::CreateRotationX(0.3f);
        verify("RotateAroundLocalX");
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalRotationQuaternion, AZ::Quaternion::CreateIdentity());
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::RotateAroundLocalY, 0.4f);
        expected.m_rotation = AZ::Quaternion::CreateRotationY(0.4f);
        verify("RotateAroundLocalY");
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalRotationQuaternion, AZ::Quaternion::CreateIdentity());
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::RotateAroundLocalZ, 0.5f);
        expected.m_rotation = AZ::Quaternion::CreateRotationZ(0.5f);
        verify("RotateAroundLocalZ");

        AZ::TransformBus::Event(
            id, &AZ::TransformBus::Events::SetLocalTM,
            AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion::CreateRotationY(0.2f), AZ::Vector3(2.0f, 0.0f, 1.0f)));
        expected.m_translation = AZ::Vector3(2.0f, 0.0f, 1.0f);
        expected.m_rotation = AZ::Quaternion::CreateRotationY(0.2f);
        verify("SetLocalTM");

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalUniformScale, 2.0f);
        expected.m_scale = AZ::Vector3(2.0f);
        verify("SetLocalUniformScale");
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalUniformScale, 1.0f);
        expected.m_scale = AZ::Vector3::CreateOne();
        verify("SetLocalUniformScale back");

        // World calls land where they are asked. The authored translation is what the mirrored parent makes of it:
        // x is mirrored about the parent at 10, and y and z are offsets from it.
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalTM, AZ::Transform::CreateTranslation(AZ::Vector3(3.0f, 0.0f, 0.0f)));
        expected = child;
        verify("SetLocalTM reset");

        const auto authoredFor = [&parent](const AZ::Vector3& world)
        {
            return AZ::Vector3(
                parent.m_translation.GetX() - world.GetX(), world.GetY() - parent.m_translation.GetY(),
                world.GetZ() - parent.m_translation.GetZ());
        };

        AZ::Vector3 world(7.0f, 1.0f, 1.0f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTranslation, world);
        expected.m_translation = authoredFor(world);
        verify("SetWorldTranslation");
        EXPECT_TRUE(WorldTranslationOf(id) == world);

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::MoveEntity, AZ::Vector3(1.0f, 1.0f, 1.0f));
        world = AZ::Vector3(8.0f, 2.0f, 2.0f);
        expected.m_translation = authoredFor(world);
        verify("MoveEntity");
        EXPECT_TRUE(WorldTranslationOf(id) == world);

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldX, 4.0f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldY, 5.0f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldZ, 6.0f);
        world = AZ::Vector3(4.0f, 5.0f, 6.0f);
        expected.m_translation = authoredFor(world);
        verify("SetWorldX Y Z");
        float worldX = 0.0f;
        float worldY = 0.0f;
        float worldZ = 0.0f;
        AZ::TransformBus::EventResult(worldX, id, &AZ::TransformBus::Events::GetWorldX);
        AZ::TransformBus::EventResult(worldY, id, &AZ::TransformBus::Events::GetWorldY);
        AZ::TransformBus::EventResult(worldZ, id, &AZ::TransformBus::Events::GetWorldZ);
        EXPECT_EQ(worldX, 4.0f);
        EXPECT_EQ(worldY, 5.0f);
        EXPECT_EQ(worldZ, 6.0f);

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTM, AZ::Transform::CreateTranslation(AZ::Vector3(9.0f, 9.0f, 9.0f)));
        world = AZ::Vector3(9.0f, 9.0f, 9.0f);
        expected.m_translation = authoredFor(world);
        verify("SetWorldTM");

        // A world rotation is the rotation of the view, so the authored one is its mirror image.
        const AZ::Quaternion worldRotation = AZ::Quaternion::CreateRotationZ(0.7f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldRotationQuaternion, worldRotation);
        expected.m_rotation = AZ::Quaternion::CreateRotationZ(-0.7f);
        verify("SetWorldRotationQuaternion");
        EXPECT_TRUE(WorldTMOf(id).GetRotation() == worldRotation);
        EXPECT_TRUE(QuaternionsMatch(WorldRotationOf(id), worldRotation));

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldRotation, AZ::Vector3(0.0f, 0.0f, 0.4f));
        expected.m_rotation = AZ::Quaternion::CreateRotationZ(-0.4f);
        verify("SetWorldRotation");
        AZ::Vector3 worldEuler = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(worldEuler, id, &AZ::TransformBus::Events::GetWorldRotation);
        EXPECT_TRUE(worldEuler.IsClose(AZ::Vector3(0.0f, 0.0f, 0.4f), 1.0e-4f));

        // The world calls never touched the own scale.
        EXPECT_TRUE(LocalScaleOf(id) == AZ::Vector3::CreateOne());
    }

    //////////////////////////////////////////////////////////////////////////
    // Events

    TEST_F(Transform2HierarchyTest, PureFlip_OfAParentWithAChild_RaisesNoTransform1EventOnTheParent)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        Node child;
        child.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        ViewEventCounter parentEvents(parentEntity->GetId());
        ViewEventCounter childEvents(childEntity->GetId());
        Transform2Recorder recorder;

        AZ::Transform2RequestBus::Event(parentEntity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        // A dynamic body on the flipped entity is not teleported by a flip. Its child moved, so the child hears one.
        EXPECT_EQ(parentEvents.m_count, 0);
        EXPECT_EQ(childEvents.m_count, 1);
        EXPECT_EQ(recorder.GetCounts(parentEntity->GetId()).m_scaleChanged, 1);
        EXPECT_EQ(recorder.GetCounts(parentEntity->GetId()).m_worldChanged, 0);
    }

    TEST_F(Transform2HierarchyTest, PureFlip_OfAChildlessLeaf_RaisesScaleChangedOnly)
    {
        Node leaf;
        leaf.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        AZ::Entity* leafEntity = CreateEntity(leaf);
        ViewEventCounter leafEvents(leafEntity->GetId());
        Transform2Recorder recorder;

        AzFramework::Transform2Component::FlipZChecked(leafEntity->GetId());

        const Transform2Recorder::Counts counts = recorder.GetCounts(leafEntity->GetId());
        EXPECT_EQ(counts.m_scaleChanged, 1);
        EXPECT_EQ(counts.m_worldChanged, 0);
        EXPECT_EQ(leafEvents.m_count, 0);
        EXPECT_TRUE(counts.m_lastScale.IsClose(AZ::Vector3(1.0f, 1.0f, -1.0f), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, SetWorldTMOfTheWorldTM_IsBitExactAndStillNotifies)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationZ(0.3f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(3.0f, 0.1f, 1.0f / 3.0f);
        child.m_rotation = AZ::Quaternion::CreateRotationX(0.9f);
        child.m_scale = AZ::Vector3(1.0f, 2.0f, 1.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId id = childEntity->GetId();

        const AZ::Transform worldBefore = WorldTMOf(id);
        const AZ::Transform localBefore = LocalTMOf(id);
        const AZ::Vector3 scaleBefore = LocalScaleOf(id);
        ViewEventCounter events(id);

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTM, worldBefore);

        EXPECT_TRUE(WorldTMOf(id) == worldBefore);
        EXPECT_TRUE(LocalTMOf(id) == localBefore);
        EXPECT_TRUE(LocalScaleOf(id) == scaleBefore);
        EXPECT_EQ(events.m_count, 1); // as Transform 1: a write notifies even when nothing changed

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalTM, localBefore);

        EXPECT_TRUE(WorldTMOf(id) == worldBefore);
        EXPECT_TRUE(LocalTMOf(id) == localBefore);
        EXPECT_TRUE(LocalScaleOf(id) == scaleBefore);
        EXPECT_EQ(events.m_count, 2);
    }

    TEST_F(Transform2HierarchyTest, WorldTranslationWritten_ComesBackBitExact)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationY(0.3f);
        parent.m_scale = AZ::Vector3(-1.0f, 2.0f, 1.0f);
        const Node child{};
        AZ::Entity* rootEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, rootEntity->GetId());

        Random random(77u);
        for (const AZ::Entity* entity : { rootEntity, childEntity })
        {
            for (int i = 0; i < 100; ++i)
            {
                const AZ::Vector3 requested(random.Range(-500.0f, 500.0f), random.Range(-500.0f, 500.0f), random.Range(-500.0f, 500.0f));
                AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, requested);
                ASSERT_TRUE(WorldTranslationOf(entity->GetId()) == requested);
                ASSERT_TRUE(WorldTMOf(entity->GetId()).GetTranslation() == requested);
                // The exact world holds the request too, not a value that is a rounding away from it.
                ASSERT_TRUE(WorldMatrixOf(entity->GetId()).GetTranslation() == requested);
            }
        }
    }

    TEST_F(Transform2HierarchyTest, WorldTMWritten_ChildrenComposeWithTheRequestedRotationExactly)
    {
        // The rotation of the chain is what a child multiplies with its own. A parent written by a world call holds the request,
        // so a child with an identity local has the requested rotation bit for bit, where the solved authored values of a parent
        // under a mirrored grandparent are a rounding away from it.
        Node grandparent;
        grandparent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        grandparent.m_rotation = AZ::Quaternion::CreateRotationY(0.3f);
        grandparent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        const Node plain{};
        AZ::Entity* grandparentEntity = CreateEntity(grandparent);
        AZ::Entity* parentEntity = CreateEntity(plain, grandparentEntity->GetId());
        AZ::Entity* childEntity = CreateEntity(plain, parentEntity->GetId());

        Random random(515u);
        for (int i = 0; i < 20; ++i)
        {
            const AZ::Quaternion requested = RandomRotation(random);
            AZ::Transform view = WorldTMOf(parentEntity->GetId());
            view.SetRotation(requested);
            AZ::TransformBus::Event(parentEntity->GetId(), &AZ::TransformBus::Events::SetWorldTM, view);

            ASSERT_TRUE(WorldRotationOf(parentEntity->GetId()) == requested) << i;
            ASSERT_TRUE(WorldRotationOf(childEntity->GetId()) == requested) << i;
        }
    }

    TEST_F(Transform2HierarchyTest, ManyRotatingWorldWrites_LeaveTheOwnScaleBitIdenticalAndRaiseNoScaleChange)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(2.0f, 1.0f, 1.0f);
        child.m_scale = AZ::Vector3(-1.0f, 2.0f, 1.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId id = childEntity->GetId();

        const AZ::Vector3 scaleBefore = LocalScaleOf(id);
        const AZ::Vector3 positionBefore = WorldTranslationOf(id);
        EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(id));
        int residualChanges = 0;
        AZ::NonUniformScaleChangedEvent::Handler residualHandler([&residualChanges](const AZ::Vector3&) { ++residualChanges; });
        AZ::NonUniformScaleRequestBus::Event(id, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, residualHandler);
        Transform2Recorder recorder;

        // The write of a rigid body each step: the view, with the rotation replaced.
        const AZ::Vector3 axis = AZ::Vector3(0.3f, 0.5f, 0.8f).GetNormalized();
        for (int step = 1; step <= 100000; ++step)
        {
            AZ::Transform view = WorldTMOf(id);
            view.SetRotation(AZ::Quaternion::CreateFromAxisAngle(axis, static_cast<float>(step) * 0.0001f));
            AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTM, view);
        }

        EXPECT_TRUE(LocalScaleOf(id) == scaleBefore);
        EXPECT_TRUE(WorldTranslationOf(id) == positionBefore);
        EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, 0);
        EXPECT_EQ(residualChanges, 0);
        EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(id));
    }

    TEST_F(Transform2HierarchyTest, SlowScaleRamp_EachResidualEventComesWithAScaleChange)
    {
        // A mesh overwrites its signed scale with the positive residual on the event. Only the scale change that follows puts the flip back.
        Node node;
        node.m_scale = AZ::Vector3(-1.0f, 0.5f, 1.0f);
        const AZ::EntityId id = CreateEntity(node)->GetId();

        int residualChanges = 0;
        AZ::NonUniformScaleChangedEvent::Handler residualHandler([&residualChanges](const AZ::Vector3&) { ++residualChanges; });
        AZ::NonUniformScaleRequestBus::Event(id, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, residualHandler);
        Transform2Recorder recorder;

        AZ::Vector3 scale = node.m_scale;
        for (int step = 0; step < 100; ++step)
        {
            const int residualChangesBefore = residualChanges;
            const int scaleChangesBefore = recorder.GetCounts(id).m_scaleChanged;

            scale.SetY(scale.GetY() + 4.0e-6f);
            AZ::Transform2RequestBus::Event(id, &AZ::Transform2Requests::SetLocalScale, scale);

            if (residualChanges != residualChangesBefore)
            {
                EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, scaleChangesBefore + 1);
            }
        }

        EXPECT_GT(residualChanges, 0);
    }

    TEST_F(Transform2HierarchyTest, ReadModifyWriteRotation_OfAHundredthDegree_ReachesTenDegrees)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        const Node child{};
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        AZ::Entity* rootEntity = CreateEntity(child);

        const AZ::Quaternion step = AZ::Quaternion::CreateRotationZ(AZ::DegToRad(0.01f));
        for (const AZ::Entity* entity : { childEntity, rootEntity })
        {
            for (int i = 0; i < 1000; ++i)
            {
                // A slow turn must not stall: each small difference has to be taken.
                const AZ::Quaternion rotation = (step * WorldRotationOf(entity->GetId())).GetNormalized();
                AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion, rotation);
            }

            // The angle of the turn about z, from the quaternion: 2 atan2(|z|, |w|).
            const AZ::Quaternion turned = WorldRotationOf(entity->GetId());
            const float degrees = AZ::RadToDeg(2.0f * AZ::Atan2(AZ::GetAbs(turned.GetZ()), AZ::GetAbs(turned.GetW())));
            EXPECT_NEAR(degrees, 10.0f, 0.02f);
        }
    }

    TEST_F(Transform2HierarchyTest, RotatingARootWithFiftyUniformDescendants_RaisesNoScaleChange)
    {
        Node root;
        root.m_translation = AZ::Vector3(1.0f, 1.0f, 1.0f);
        AZ::Entity* rootEntity = CreateEntity(root);
        AZStd::vector<Node> children;
        AZStd::vector<AZ::EntityId> childIds;
        for (int i = 0; i < 50; ++i)
        {
            Node child;
            child.m_translation = AZ::Vector3(1.0f + 0.1f * static_cast<float>(i), 0.0f, 0.5f);
            children.push_back(child);
            childIds.push_back(CreateEntity(child, rootEntity->GetId())->GetId());
        }
        Transform2Recorder recorder;

        for (int step = 0; step < 100; ++step)
        {
            AZ::TransformBus::Event(rootEntity->GetId(), &AZ::TransformBus::Events::RotateAroundLocalZ, 0.02f);
        }

        EXPECT_EQ(recorder.GetTotalScaleChanged(), 0);
        // Every descendant followed the turn of 2 radians.
        Node turnedRoot = root;
        turnedRoot.m_rotation = AZ::Quaternion::CreateRotationZ(2.0f);
        for (size_t i : { size_t(0), size_t(25), size_t(49) })
        {
            ASSERT_TRUE(MatricesMatch(WorldMatrixOf(childIds[i]), ReferenceWorld({ turnedRoot, children[i] }), 1.0e-3f));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Scale arrives as a float

    TEST_F(Transform2HierarchyTest, PlainTransformConfig_OnAFlippedRoot_FoldsTheFloatIntoTheScale)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        entity->CreateComponent<AzFramework::Transform2Component>(AZ::Vector3(-1.0f, 1.0f, 1.0f))
            ->SetConfiguration(AZ::TransformConfig(AZ::Transform::CreateUniformScale(2.0f)));
        entity->Init();
        entity->Activate();

        EXPECT_TRUE(LocalScaleOf(entity->GetId()).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(WorldScaleOf(entity->GetId()).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(WorldMatrixOf(entity->GetId()).GetColumn(0).IsClose(AZ::Vector3(-2.0f, 0.0f, 0.0f), 1.0e-5f));
        m_entities.push_back(AZStd::move(entity));
    }

    TEST_F(Transform2HierarchyTest, UniformFloat_BelowTheSmallestClamp_StaysExactAndDrawsTheRightSize)
    {
        // A Deploy as Extended entity at a uniform scale that Transform 1 holds exactly.
        Node small;
        small.m_scale = AZ::Vector3(0.005f);
        Node child;
        child.m_translation = AZ::Vector3(100.0f, 0.0f, 0.0f);

        AZ::Entity* smallEntity = CreateEntity(small);
        AZ::Entity* childEntity = CreateEntity(child, smallEntity->GetId());
        const AZ::EntityId id = smallEntity->GetId();

        EXPECT_TRUE(LocalScaleOf(id) == AZ::Vector3(0.005f));
        float localFloat = 0.0f;
        float worldFloat = 0.0f;
        AZ::TransformBus::EventResult(localFloat, id, &AZ::TransformBus::Events::GetLocalUniformScale);
        AZ::TransformBus::EventResult(worldFloat, id, &AZ::TransformBus::Events::GetWorldUniformScale);
        EXPECT_EQ(localFloat, 0.005f);
        EXPECT_EQ(worldFloat, 0.005f);
        EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(id));

        // The drawn size: each column of the world is 0.005 long.
        const AZ::Matrix3x4 world = WorldMatrixOf(id);
        for (int32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(world.GetColumn(axis).GetLength(), 0.005f, 1.0e-8f);
        }
        ExpectViewMatchesWorldScale(id, { small }, "small uniform");

        // And a child of it is moved by that size: 100 times 0.005.
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(0.5f, 0.0f, 0.0f), 1.0e-5f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Serialize, clone, write before Init

    namespace
    {
        // What a spawn does to a prototype: save it, load it, clone it.
        AZStd::unique_ptr<AZ::Entity> SaveLoadAndClone(const AZ::Entity& source)
        {
            AZ::SerializeContext* context = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(context, &AZ::ComponentApplicationRequests::GetSerializeContext);
            EXPECT_NE(context, nullptr);

            AZStd::vector<char> buffer;
            AZ::IO::ByteContainerStream<AZStd::vector<char>> stream(&buffer);
            EXPECT_TRUE(AZ::Utils::SaveObjectToStream(stream, AZ::DataStream::ST_BINARY, &source, context));

            AZStd::unique_ptr<AZ::Entity> loaded(AZ::Utils::LoadObjectFromBuffer<AZ::Entity>(buffer.data(), buffer.size(), context));
            EXPECT_NE(loaded, nullptr);
            return AZStd::unique_ptr<AZ::Entity>(context->CloneObject(loaded.get()));
        }
    } // namespace

    TEST_F(Transform2HierarchyTest, SerializeCloneWriteBeforeInit_KeepsThePositionAndTheSigns)
    {
        const AZ::Vector3 scales[] = { AZ::Vector3(2.0f, 1.0f, 1.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f) };
        for (const AZ::Vector3& scale : scales)
        {
            AZ::Entity source;
            source.CreateComponent<AzFramework::Transform2Component>(scale);
            AZStd::unique_ptr<AZ::Entity> clone = SaveLoadAndClone(source);
            ASSERT_NE(clone, nullptr);
            auto* component = clone->FindComponent<AzFramework::Transform2Component>();
            ASSERT_NE(component, nullptr);

            // A spawn writes the world of the root before it is initialized.
            AZ::Transform world = AZ::Transform::CreateTranslation(AZ::Vector3(5.0f, 0.0f, 0.0f));
            world.SetUniformScale(StandIn(scale));
            component->SetWorldTM(world);
            clone->Init();
            clone->Activate();

            EXPECT_TRUE(WorldTranslationOf(clone->GetId()) == AZ::Vector3(5.0f, 0.0f, 0.0f));
            EXPECT_TRUE(LocalScaleOf(clone->GetId()).IsClose(scale, 1.0e-5f));
            EXPECT_TRUE(WorldScaleOf(clone->GetId()).IsClose(scale, 1.0e-5f));
            EXPECT_TRUE(WorldMatrixOf(clone->GetId()).GetTranslation().IsClose(AZ::Vector3(5.0f, 0.0f, 0.0f), 1.0e-5f));
            EXPECT_EQ(AZ::NonUniformScaleRequestBus::HasHandlers(clone->GetId()), scale.GetX() == 2.0f);

            m_entities.push_back(AZStd::move(clone));
        }
    }

    TEST_F(Transform2HierarchyTest, WriteBeforeInit_WithADifferentFloat_FoldsItByRatio)
    {
        auto entity = AZStd::make_unique<AZ::Entity>();
        auto* component = entity->CreateComponent<AzFramework::Transform2Component>(AZ::Vector3(2.0f, 1.0f, 1.0f));

        // A float of 1 where the entity holds 2: the size is halved, and the ratios stay.
        component->SetWorldTM(AZ::Transform::CreateTranslation(AZ::Vector3(5.0f, 0.0f, 0.0f)));
        entity->Init();
        entity->Activate();

        EXPECT_TRUE(LocalScaleOf(entity->GetId()).IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f), 1.0e-5f));
        EXPECT_TRUE(WorldTranslationOf(entity->GetId()) == AZ::Vector3(5.0f, 0.0f, 0.0f));
        m_entities.push_back(AZStd::move(entity));
    }

    //////////////////////////////////////////////////////////////////////////
    // Reparenting

    TEST_F(Transform2HierarchyTest, KeepWorldReparent_UnderAMirroredParent_KeepsTheViewAndTakesTheMask)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(7.0f, 0.0f, 0.0f);
        child.m_rotation = AZ::Quaternion::CreateRotationZ(0.5f);
        child.m_scale = AZ::Vector3(1.0f, 2.0f, 1.0f);

        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child);
        const AZ::EntityId id = childEntity->GetId();
        const AZ::Transform viewBefore = WorldTMOf(id);
        const AZ::Vector3 scaleBefore = LocalScaleOf(id);

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetParent, parentEntity->GetId());

        // The view, and the own scale, are what they were. The mask is the new parent's.
        EXPECT_TRUE(WorldTMOf(id) == viewBefore);
        EXPECT_TRUE(LocalScaleOf(id) == scaleBefore);
        EXPECT_TRUE(WorldScaleOf(id).IsClose(AZ::Vector3(-1.0f, 2.0f, 1.0f), 1.0e-5f));

        // The authored values were solved against the new parent: 7 is 3 from it, on the other side, and the turn is mirrored.
        Node solved = child;
        solved.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
        solved.m_rotation = AZ::Quaternion::CreateRotationZ(-0.5f);
        ExpectAuthored(id, solved, "keep world");
        ExpectWorld(id, { parent, solved }, "keep world");

        // Out of the parent again, keeping the view: the mask is the own one again.
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetParent, AZ::EntityId());
        EXPECT_TRUE(WorldTMOf(id) == viewBefore);
        EXPECT_TRUE(LocalScaleOf(id) == scaleBefore);
        EXPECT_TRUE(WorldScaleOf(id).IsClose(scaleBefore, 1.0e-5f));
        ExpectWorld(id, { child }, "unparented");

        // SetParentRelative keeps the authored local and recomputes the world.
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetParentRelative, parentEntity->GetId());
        ExpectAuthored(id, child, "relative");
        ExpectWorld(id, { parent, child }, "relative");
        EXPECT_TRUE(WorldTranslationOf(id).IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(WorldScaleOf(id).IsClose(AZ::Vector3(-1.0f, 2.0f, 1.0f), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, DoNotUpdateChild_KeepsItsViewWhenTheParentMovesAndFlips)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        Node child;
        child.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId parentId = parentEntity->GetId();
        const AZ::EntityId id = childEntity->GetId();
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetOnParentChangedBehavior, AZ::OnParentChangedBehavior::DoNotUpdate);

        const AZ::Transform viewBefore = WorldTMOf(id);
        ViewEventCounter events(id);
        Transform2Recorder recorder;

        // The parent moves by 3: the child stays at 15, and its local is re-solved to 2.
        AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(13.0f, 0.0f, 0.0f));
        EXPECT_TRUE(WorldTMOf(id) == viewBefore);
        EXPECT_EQ(events.m_count, 0);
        EXPECT_TRUE(LocalTMOf(id).GetTranslation().IsClose(AZ::Vector3(2.0f, 0.0f, 0.0f), 1.0e-4f));

        // The parent flips: the child stays, its local is re-solved to -2, and its mask follows the parent.
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_TRUE(WorldTMOf(id) == viewBefore);
        EXPECT_EQ(events.m_count, 0);
        EXPECT_TRUE(LocalTMOf(id).GetTranslation().IsClose(AZ::Vector3(-2.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(WorldScaleOf(id).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));
        EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, 1);
        EXPECT_TRUE(WorldTranslationOf(id) == AZ::Vector3(15.0f, 0.0f, 0.0f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Activation and destruction

    TEST_F(Transform2HierarchyTest, ParentDeactivateAndReactivate_ChildStillFollows)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        Node child;
        child.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId parentId = parentEntity->GetId();
        const AZ::EntityId childId = childEntity->GetId();
        Transform2Recorder recorder;

        parentEntity->Deactivate();

        // The children go down with the parent, and the buses stay for as long as the components live.
        EXPECT_TRUE(childEntity->GetState() == AZ::Entity::State::Init);
        EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(parentId));
        EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(childId));

        parentEntity->Activate();
        EXPECT_TRUE(childEntity->GetState() == AZ::Entity::State::Active);

        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        Node flipped = parent;
        flipped.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        ExpectWorld(childId, { flipped, child }, "after reactivation");

        for (const AZ::EntityId& id : { parentId, childId })
        {
            const Transform2Recorder::Counts counts = recorder.GetCounts(id);
            EXPECT_EQ(counts.m_deactivated, 1);
            EXPECT_EQ(counts.m_activated, 1);
            EXPECT_EQ(counts.m_scaleChanged, 1);
        }
    }

    TEST_F(Transform2HierarchyTest, ParentDestroyedWithALiveChild_ChildKeepsItsView)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
        const AZ::EntityId id = childEntity->GetId();
        const AZ::Transform viewBefore = WorldTMOf(id);
        EXPECT_TRUE(viewBefore.GetTranslation().IsClose(AZ::Vector3(7.0f, 2.0f, 3.0f), 1.0e-4f));

        DestroyEntity(parentEntity);

        EXPECT_TRUE(WorldTMOf(id) == viewBefore);
        AZ::EntityId parentId = AZ::EntityId(1234);
        AZ::TransformBus::EventResult(parentId, id, &AZ::TransformBus::Events::GetParentId);
        EXPECT_FALSE(parentId.IsValid());
        EXPECT_TRUE(childEntity->GetState() == AZ::Entity::State::Active);

        // It is a root now: the flip of the parent is gone, and it moves on its own.
        EXPECT_TRUE(WorldScaleOf(id).IsClose(AZ::Vector3::CreateOne(), 1.0e-5f));
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_TRUE(WorldTranslationOf(id) == AZ::Vector3(1.0f, 1.0f, 1.0f));
        EXPECT_TRUE(LocalTMOf(id).GetTranslation() == AZ::Vector3(1.0f, 1.0f, 1.0f));
    }

    TEST_F(Transform2HierarchyTest, ChildInitializedBeforeItsParent_FollowsWhenTheParentAppears)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);

        AZ::Entity* parentEntity = BuildEntity(parent);
        AZ::Entity* childEntity = BuildEntity(child, parentEntity->GetId());

        // The editor and slice paths can do this. The child holds on to its configured values until the parent exists.
        childEntity->Init();
        childEntity->Activate();
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(5.0f, 0.0f, 0.0f), 1.0e-4f));

        parentEntity->Init();
        parentEntity->Activate();

        ExpectWorld(childEntity->GetId(), { parent, child }, "parent appeared");
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(5.0f, 2.0f, 3.0f), 1.0e-4f));
        EXPECT_TRUE(WorldScaleOf(childEntity->GetId()).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));

        // And it follows from there.
        AZ::Transform2RequestBus::Event(parentEntity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3::CreateOne());
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(15.0f, 2.0f, 3.0f), 1.0e-4f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 1 and Transform 2 in one hierarchy

    TEST_F(Transform2HierarchyTest, Transform1ChildUnderATransform2_WarnsOnceAndSeesTheBestFit)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        AZ::Entity* parentEntity = CreateEntity(parent);

        AZ::Entity* first = CreateTransform1Entity();
        AZ::Entity* second = CreateTransform1Entity();
        AZ::TransformBus::Event(first->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(7.0f, 0.0f, 0.0f));

        WarningCounter warnings;
        AZ::TransformBus::Event(first->GetId(), &AZ::TransformBus::Events::SetParent, parentEntity->GetId());
        AZ::TransformBus::Event(second->GetId(), &AZ::TransformBus::Events::SetParent, parentEntity->GetId());
#if defined(AZ_ENABLE_TRACING)
        // Once per parent, whatever the number of Transform 1 children.
        EXPECT_EQ(warnings.m_count, 1);
#endif

        // It is a Transform 1: it composes with the best fit of the parent, so keep world kept its position.
        EXPECT_TRUE(WorldTranslationOf(first->GetId()).IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_EQ(first->FindComponent<AzFramework::Transform2Component>(), nullptr);
        EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(first->GetId()));

        // A flip does not move the best fit of the parent, so it does not move the Transform 1 child.
        AZ::Transform2RequestBus::Event(parentEntity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3::CreateOne());
        EXPECT_TRUE(WorldTranslationOf(first->GetId()).IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f), 1.0e-4f));
    }

    TEST_F(Transform2HierarchyTest, Transform2ChildUnderATransform1_IsExact)
    {
        AZ::Entity* parentEntity = CreateTransform1Entity();
        AZ::TransformBus::Event(parentEntity->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(10.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(parentEntity->GetId(), &AZ::TransformBus::Events::SetLocalUniformScale, 2.0f);

        Node child;
        child.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
        child.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        WarningCounter warnings;
        AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
#if defined(AZ_ENABLE_TRACING)
        EXPECT_EQ(warnings.m_count, 0);
#endif

        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        parent.m_scale = AZ::Vector3(2.0f);
        ExpectWorld(childEntity->GetId(), { parent, child }, "under a Transform 1");
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(16.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(WorldScaleOf(childEntity->GetId()).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));

        // The Transform 1 parent moves, and the child follows through the ordinary event.
        AZ::TransformBus::Event(parentEntity->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(20.0f, 0.0f, 0.0f));
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(26.0f, 0.0f, 0.0f), 1.0e-4f));
    }

    TEST_F(Transform2HierarchyTest, Transform1OnlySubtree_StaysTransform1)
    {
        Transform2Recorder recorder;
        AZ::Entity* root = CreateTransform1Entity();
        AZ::Entity* middle = CreateTransform1Entity();
        AZ::Entity* leaf = CreateTransform1Entity();
        AZ::TransformBus::Event(root->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(1.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(middle->GetId(), &AZ::TransformBus::Events::SetParentRelative, root->GetId());
        AZ::TransformBus::Event(middle->GetId(), &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(1.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(leaf->GetId(), &AZ::TransformBus::Events::SetParentRelative, middle->GetId());
        AZ::TransformBus::Event(leaf->GetId(), &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(1.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(root->GetId(), &AZ::TransformBus::Events::RotateAroundLocalZ, AZ::Constants::HalfPi);

        for (const AZ::Entity* entity : { root, middle, leaf })
        {
            EXPECT_NE(entity->FindComponent<AzFramework::TransformComponent>(), nullptr);
            EXPECT_EQ(entity->FindComponent<AzFramework::Transform2Component>(), nullptr);
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entity->GetId()));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entity->GetId()));
        }
        // The plain Transform 1 products: a quarter turn about z takes the offsets of 1 and 2 to y.
        EXPECT_TRUE(WorldTranslationOf(leaf->GetId()).IsClose(AZ::Vector3(1.0f, 2.0f, 0.0f), 1.0e-4f));
        // No Transform 2 event was raised.
        EXPECT_EQ(recorder.GetCounts(root->GetId()).m_activated, 0);
        EXPECT_EQ(recorder.GetCounts(middle->GetId()).m_scaleChanged, 0);
        EXPECT_EQ(recorder.GetCounts(leaf->GetId()).m_worldChanged, 0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Limits

    TEST_F(Transform2HierarchyTest, ZeroScale_StaysFinite)
    {
        const AZ::Vector3 scales[] = { AZ::Vector3(0.0f, 1.0f, 1.0f), AZ::Vector3(0.0f, 0.0f, 0.0f) };
        for (const AZ::Vector3& scale : scales)
        {
            Node parent;
            parent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
            parent.m_rotation = AZ::Quaternion::CreateRotationX(0.3f);
            parent.m_scale = scale;
            Node child;
            child.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
            child.m_rotation = AZ::Quaternion::CreateRotationY(0.6f);

            AZ::Entity* parentEntity = CreateEntity(parent);
            AZ::Entity* childEntity = CreateEntity(child, parentEntity->GetId());
            const AZ::EntityId childId = childEntity->GetId();

            // A write under the collapsed parent has no inverse to solve with, and has to stay finite too.
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(4.0f, 5.0f, 6.0f));
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldRotationQuaternion, AZ::Quaternion::CreateRotationZ(0.2f));

            for (const AZ::EntityId& id : { parentEntity->GetId(), childId })
            {
                EXPECT_TRUE(WorldTMOf(id).IsFinite());
                EXPECT_TRUE(LocalTMOf(id).IsFinite());
                EXPECT_TRUE(WorldMatrixOf(id).IsFinite());
                EXPECT_TRUE(WorldScaleOf(id).IsFinite());
                EXPECT_TRUE(WorldRotationOf(id).IsFinite());
                EXPECT_NEAR(WorldRotationOf(id).GetLength(), 1.0f, 1.0e-3f);
            }
        }

        // The smallest scale a Transform 1 float can hold is what the collapsed entity reports.
        Node collapsed;
        collapsed.m_scale = AZ::Vector3::CreateZero();
        AZ::Entity* collapsedEntity = CreateEntity(collapsed);
        float worldFloat = 0.0f;
        AZ::TransformBus::EventResult(worldFloat, collapsedEntity->GetId(), &AZ::TransformBus::Events::GetWorldUniformScale);
        EXPECT_EQ(worldFloat, AZ::MinTransformScale);
    }

    TEST_F(Transform2HierarchyTest, StaticParent_RefusesMovesAndItsChildStays)
    {
        auto parent = AZStd::make_unique<AZ::Entity>();
        AZ::TransformConfig config(AZ::Transform::CreateTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f)));
        config.m_isStatic = true;
        parent->CreateComponent<AzFramework::Transform2Component>(AZ::Vector3(-1.0f, 1.0f, 1.0f))->SetConfiguration(config);
        parent->Init();
        parent->Activate();
        const AZ::EntityId parentId = parent->GetId();
        m_entities.push_back(AZStd::move(parent));

        Node child;
        child.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
        AZ::Entity* childEntity = CreateEntity(child, parentId);
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f), 1.0e-4f));

        AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(50.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetLocalTM, AZ::Transform::CreateTranslation(AZ::Vector3(60.0f, 0.0f, 0.0f)));

        EXPECT_TRUE(WorldTranslationOf(parentId).IsClose(AZ::Vector3(10.0f, 0.0f, 0.0f), 1.0e-5f));
        EXPECT_TRUE(WorldTranslationOf(childEntity->GetId()).IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f), 1.0e-4f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Services, casts and buses

    TEST_F(Transform2HierarchyTest, Services_Transform1BesideATransform2_FailsTheDependencySort)
    {
        AZ::Entity both;
        auto* transform1 = both.CreateComponent<AzFramework::TransformComponent>();
        auto* transform2 = both.CreateComponent<AzFramework::Transform2Component>();
        AZ::Entity::ComponentArrayType pair{ transform1, transform2 };
        EXPECT_FALSE(AZ::Entity::DependencySort(pair).IsSuccess());
    }

    TEST_F(Transform2HierarchyTest, Services_Transform2Alone_SortsAndProvidesBothServices)
    {
        AZ::Entity alone;
        auto* transform2 = alone.CreateComponent<AzFramework::Transform2Component>();
        AZ::Entity::ComponentArrayType components{ transform2 };
        EXPECT_TRUE(AZ::Entity::DependencySort(components).IsSuccess());

        AZ::ComponentDescriptor* descriptor = nullptr;
        AZ::ComponentDescriptorBus::EventResult(
            descriptor, azrtti_typeid<AzFramework::Transform2Component>(), &AZ::ComponentDescriptorBus::Events::GetDescriptor);
        ASSERT_NE(descriptor, nullptr);
        AZ::ComponentDescriptor::DependencyArrayType provided;
        descriptor->GetProvidedServices(provided, nullptr);
        EXPECT_NE(AZStd::find(provided.begin(), provided.end(), AZ_CRC_CE("TransformService")), provided.end());
        EXPECT_NE(AZStd::find(provided.begin(), provided.end(), AZ_CRC_CE("Transform2Service")), provided.end());
        AZ::ComponentDescriptor::DependencyArrayType required;
        descriptor->GetRequiredServices(required, nullptr);
        EXPECT_TRUE(required.empty());
    }

    TEST_F(Transform2HierarchyTest, Casts_ToTransform1AndTheTransformInterface_Work)
    {
        AZ::Entity* entity = CreateEntity(Node());
        auto* component = entity->FindComponent<AzFramework::Transform2Component>();
        ASSERT_NE(component, nullptr);

        EXPECT_NE(azrtti_cast<AzFramework::TransformComponent*>(component), nullptr);
        EXPECT_NE(azrtti_cast<AZ::TransformInterface*>(component), nullptr);
        EXPECT_TRUE(azrtti_istypeof<AzFramework::TransformComponent>(component));

        // Entity::GetTransform and the derived lookup find it. The exact lookup of Transform 1 does not.
        ASSERT_NE(entity->GetTransform(), nullptr);
        EXPECT_NE(azrtti_cast<AzFramework::Transform2Component*>(entity->GetTransform()), nullptr);
        EXPECT_EQ(AZ::EntityUtils::FindFirstDerivedComponent<AZ::TransformInterface>(entity), component);
        EXPECT_EQ(AZ::EntityUtils::FindFirstDerivedComponent<AzFramework::TransformComponent>(entity), component);
        EXPECT_EQ(entity->FindComponent<AzFramework::TransformComponent>(), nullptr);
    }

    TEST_F(Transform2HierarchyTest, Buses_PresenceFollowsTheEffectiveScale)
    {
        // A uniform child of a non-uniform parent is non-uniform in the world, and serves the residual.
        Node stretched;
        stretched.m_scale = AZ::Vector3(2.0f, 1.0f, 1.0f);
        AZ::Entity* stretchedEntity = CreateEntity(stretched);
        AZ::Entity* inheritedNonUniform = CreateEntity(Node(), stretchedEntity->GetId());
        EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(inheritedNonUniform->GetId()));
        EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(inheritedNonUniform->GetId()));
        AZ::Vector3 residual = AZ::Vector3::CreateZero();
        AZ::NonUniformScaleRequestBus::EventResult(residual, inheritedNonUniform->GetId(), &AZ::NonUniformScaleRequests::GetScale);
        EXPECT_TRUE(residual.IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f), 1.0e-4f));

        // A uniform child of a flipped parent is uniform in the world, and a flip alone never serves the residual.
        Node flipped;
        flipped.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        AZ::Entity* flippedEntity = CreateEntity(flipped);
        AZ::Entity* inheritedFlip = CreateEntity(Node(), flippedEntity->GetId());
        EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(inheritedFlip->GetId()));
        EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(inheritedFlip->GetId()));

        // A parent that becomes non-uniform while the child lives gives the child the bus.
        AZ::Transform2RequestBus::Event(flippedEntity->GetId(), &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-3.0f, 1.0f, 1.0f));
        EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(inheritedFlip->GetId()));
    }

    //////////////////////////////////////////////////////////////////////////
    // Publish: the order of the signals, and what each one sees

    TEST_F(Transform2HierarchyTest, Publish_OfAScaleChange_RaisesTheSignalsInOrderAndEachSeesTheFinalState)
    {
        Node node;
        node.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        node.m_rotation = AZ::Quaternion::CreateRotationZ(0.5f);
        node.m_scale = AZ::Vector3(1.0f, 0.5f, 1.0f);
        const AZ::EntityId id = CreateEntity(node)->GetId();
        ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(id));
        int sequence = 0;
        PublishProbe probe(id, AZ::EntityId(), sequence);

        // The stand-in, the residual and the sign of x change together.
        Node scaled = node;
        scaled.m_scale = AZ::Vector3(-3.0f, 1.0f, 1.0f);
        AZ::Transform2RequestBus::Event(id, &AZ::Transform2Requests::SetLocalScale, scaled.m_scale);

        // The state after the write, against numbers worked out by hand: the float is 3 and the residual is 1, 1/3, 1/3.
        const PublishProbe::State after = PublishProbe::Observe(id);
        EXPECT_EQ(after.m_view.GetUniformScale(), 3.0f);
        EXPECT_TRUE(after.m_view.GetTranslation().IsClose(node.m_translation, 1.0e-5f));
        EXPECT_TRUE(after.m_scale.IsClose(AZ::Vector3(-3.0f, 1.0f, 1.0f), 1.0e-5f));
        EXPECT_TRUE(after.m_residual.IsClose(AZ::Vector3(1.0f, 1.0f / 3.0f, 1.0f / 3.0f), 1.0e-5f));
        EXPECT_TRUE(MatricesMatch(after.m_world, ReferenceWorld({ scaled })));

        // Each signal came once, in the order of the contract: the Transform 1 view, the residual, the world, the scale.
        EXPECT_EQ(probe.m_viewEvent.m_count, 1);
        EXPECT_EQ(probe.m_residualEvent.m_count, 1);
        EXPECT_EQ(probe.m_worldChanged.m_count, 1);
        EXPECT_EQ(probe.m_scaleChanged.m_count, 1);
        EXPECT_LT(probe.m_viewEvent.m_at, probe.m_residualEvent.m_at);
        EXPECT_LT(probe.m_residualEvent.m_at, probe.m_worldChanged.m_at);
        EXPECT_LT(probe.m_worldChanged.m_at, probe.m_scaleChanged.m_at);

        // Each of them saw the final state: all the data of the entity is in place before anything fires.
        EXPECT_TRUE(PublishProbe::Same(probe.m_viewEvent.m_own, after));
        EXPECT_TRUE(PublishProbe::Same(probe.m_residualEvent.m_own, after));
        EXPECT_TRUE(PublishProbe::Same(probe.m_worldChanged.m_own, after));
        EXPECT_TRUE(PublishProbe::Same(probe.m_scaleChanged.m_own, after));
    }

    TEST_F(Transform2HierarchyTest, Publish_OfAParentScaleChange_RunsTheChildInsideTheFirstSignalAndEachSeesTheFinalParent)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        parent.m_scale = AZ::Vector3(1.0f, 0.5f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(2.0f, 0.0f, 0.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();
        ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(parentId));
        ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
        int sequence = 0;
        PublishProbe parentProbe(parentId, AZ::EntityId(), sequence);
        PublishProbe childProbe(childId, parentId, sequence);

        Node scaledParent = parent;
        scaledParent.m_scale = AZ::Vector3(-3.0f, 1.0f, 1.0f);
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, scaledParent.m_scale);

        // By hand: the child sits at (1, 2, 3) + (-3 * 2, 0, 0), and takes the scale, the float and the residual of the parent.
        const PublishProbe::State parentAfter = PublishProbe::Observe(parentId);
        const PublishProbe::State childAfter = PublishProbe::Observe(childId);
        EXPECT_TRUE(childAfter.m_view.GetTranslation().IsClose(AZ::Vector3(-5.0f, 2.0f, 3.0f), 1.0e-4f));
        EXPECT_NEAR(childAfter.m_view.GetUniformScale(), 3.0f, 1.0e-5f);
        EXPECT_TRUE(childAfter.m_scale.IsClose(AZ::Vector3(-3.0f, 1.0f, 1.0f), 1.0e-5f));
        EXPECT_TRUE(childAfter.m_residual.IsClose(AZ::Vector3(1.0f, 1.0f / 3.0f, 1.0f / 3.0f), 1.0e-5f));
        EXPECT_TRUE(parentAfter.m_residual.IsClose(AZ::Vector3(1.0f, 1.0f / 3.0f, 1.0f / 3.0f), 1.0e-5f));
        EXPECT_TRUE(MatricesMatch(childAfter.m_world, ReferenceWorld({ scaledParent, child })));

        for (const PublishProbe* probe : { &parentProbe, &childProbe })
        {
            EXPECT_EQ(probe->m_viewEvent.m_count, 1);
            EXPECT_EQ(probe->m_residualEvent.m_count, 1);
            EXPECT_EQ(probe->m_worldChanged.m_count, 1);
            EXPECT_EQ(probe->m_scaleChanged.m_count, 1);
            EXPECT_LT(probe->m_viewEvent.m_at, probe->m_residualEvent.m_at);
            EXPECT_LT(probe->m_residualEvent.m_at, probe->m_worldChanged.m_at);
            EXPECT_LT(probe->m_worldChanged.m_at, probe->m_scaleChanged.m_at);
        }

        // The child recomputes inside the Transform 1 event of its parent, so it is over before the second signal of the parent.
        EXPECT_LT(childProbe.m_scaleChanged.m_at, parentProbe.m_residualEvent.m_at);

        // Whenever the parent or the child speaks, the parent is in its final state, and the child in its own.
        for (const PublishProbe::Signal* signal : { &parentProbe.m_viewEvent, &parentProbe.m_residualEvent, &parentProbe.m_worldChanged, &parentProbe.m_scaleChanged })
        {
            EXPECT_TRUE(PublishProbe::Same(signal->m_own, parentAfter));
        }
        for (const PublishProbe::Signal* signal : { &childProbe.m_viewEvent, &childProbe.m_residualEvent, &childProbe.m_worldChanged, &childProbe.m_scaleChanged })
        {
            EXPECT_TRUE(PublishProbe::Same(signal->m_own, childAfter));
            EXPECT_TRUE(PublishProbe::Same(signal->m_parent, parentAfter));
        }
    }

    TEST_F(Transform2HierarchyTest, Publish_OfAWorldWrite_ShowsTheRequestedViewToTheOwnTransform1Event)
    {
        // The view is stored as asked before anything fires, so a listener reads back what was written, and not a value that is a
        // rounding away from it.
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationY(0.3f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(3.0f, 0.1f, 1.0f / 3.0f);
        child.m_rotation = AZ::Quaternion::CreateRotationX(0.9f);
        child.m_scale = AZ::Vector3(1.0f, 2.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();
        int sequence = 0;
        PublishProbe probe(childId, AZ::EntityId(), sequence);

        Random random(4242u);
        for (int i = 0; i < 50; ++i)
        {
            probe.Reset();
            AZ::Transform requested = AZ::Transform::CreateFromQuaternionAndTranslation(RandomRotation(random), RandomTranslation(random, 20.0f));
            requested.SetUniformScale(WorldTMOf(childId).GetUniformScale());
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTM, requested);

            ASSERT_EQ(probe.m_viewEvent.m_count, 1) << i;
            ASSERT_TRUE(probe.m_viewEvent.m_own.m_view == requested) << i;
            ASSERT_TRUE(WorldTMOf(childId) == requested) << i;
        }
    }

    TEST_F(Transform2HierarchyTest, Descendant_OfAFlippedParent_IsToldTheWorldScaleAndNotItsOwn)
    {
        Transform2Recorder recorder; // before the entities, so that it hears their activations
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationZ(0.7f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);
        child.m_rotation = AZ::Quaternion::CreateRotationX(0.4f);
        AZ::Entity* parentEntity = CreateEntity(parent);
        const AZ::EntityId parentId = parentEntity->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();

        // The child has no flip of its own, and is announced with the flip of its parent.
        EXPECT_TRUE(LocalScaleOf(childId) == AZ::Vector3::CreateOne());
        EXPECT_EQ(recorder.GetCounts(childId).m_activated, 1);
        EXPECT_TRUE(recorder.GetCounts(childId).m_activatedScale.IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));

        // A change of the parent: the world change of the child carries the world scale and the Transform 1 view that it has now.
        Node grown = parent;
        grown.m_scale = AZ::Vector3(-2.0f, 2.0f, 2.0f);
        const int worldChangesBefore = recorder.GetCounts(childId).m_worldChanged;
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, grown.m_scale);

        const Transform2Recorder::Counts counts = recorder.GetCounts(childId);
        EXPECT_EQ(counts.m_worldChanged, worldChangesBefore + 1);
        EXPECT_TRUE(counts.m_worldChangedScale.IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_EQ(counts.m_worldChangedTM.GetUniformScale(), 2.0f);
        EXPECT_TRUE(counts.m_worldChangedTM == WorldTMOf(childId));
        ExpectWorld(childId, { grown, child }, "grown parent");

        // Down and up again with the parent: the activation carries the world scale of that moment.
        parentEntity->Deactivate();
        parentEntity->Activate();
        EXPECT_EQ(recorder.GetCounts(childId).m_activated, 2);
        EXPECT_TRUE(recorder.GetCounts(childId).m_activatedScale.IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(recorder.GetCounts(parentId).m_activatedScale.IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
    }

    //////////////////////////////////////////////////////////////////////////
    // A view that was stored as a request

    TEST_F(Transform2HierarchyTest, StoredView_SurvivesAWriteOfItsParentThatChangesNothing)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(3.0f, -2.0f, 4.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationY(0.3f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_scale = AZ::Vector3(1.0f, 2.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();

        // A character controller writes the child each tick, with values that the solve cannot give back to the last bit.
        Random random(2468u);
        for (int i = 0; i < 20; ++i)
        {
            AZ::Transform requested = AZ::Transform::CreateFromQuaternionAndTranslation(RandomRotation(random), RandomTranslation(random, 4.0f));
            requested.SetUniformScale(WorldTMOf(childId).GetUniformScale());
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTM, requested);
            ASSERT_TRUE(WorldTMOf(childId) == requested) << i;
            const AZ::Matrix3x4 matrixBefore = WorldMatrixOf(childId);
            ViewEventCounter events(childId);

            // The parent is written with what it holds: that is no change for the child, to the last bit.
            AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetWorldTM, WorldTMOf(parentId));
            AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetLocalTM, LocalTMOf(parentId));

            EXPECT_TRUE(WorldTMOf(childId) == requested) << i;
            EXPECT_TRUE(WorldMatrixOf(childId) == matrixBefore) << i;
            EXPECT_EQ(events.m_count, 0) << i;
        }
    }

    TEST_F(Transform2HierarchyTest, StoredView_UnderAParentFarFromTheOrigin_SurvivesAWriteOfItsParentThatChangesNothing)
    {
        // The child is near the origin and its parent is a thousand away: the translation of the child is a small difference of
        // large numbers, so the rounding of the solve is small against the parent and large against the child.
        Node parent;
        parent.m_translation = AZ::Vector3(1000.0f, 0.0f, 0.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationZ(0.3f);
        const Node child{};
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();

        Random random(1357u);
        for (int i = 0; i < 20; ++i)
        {
            AZ::Transform requested = AZ::Transform::CreateFromQuaternionAndTranslation(RandomRotation(random), RandomTranslation(random, 1.5f));
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTM, requested);
            ASSERT_TRUE(WorldTMOf(childId) == requested) << i;
            const AZ::Matrix3x4 matrixBefore = WorldMatrixOf(childId);
            ViewEventCounter events(childId);

            AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetWorldTM, WorldTMOf(parentId));

            EXPECT_TRUE(WorldTMOf(childId) == requested) << i;
            EXPECT_TRUE(WorldMatrixOf(childId) == matrixBefore) << i;
            EXPECT_EQ(events.m_count, 0) << i;
        }
    }

    TEST_F(Transform2HierarchyTest, StoredView_IsFollowedWhenItsParentReallyMoves)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(3.0f, -2.0f, 4.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationY(0.3f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        const Node child{};
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();

        // A parent that moves by a tenth of a thousandth moves the child by as much, and the child says so.
        const float distances[] = { 1.0e-4f, 1.0f };
        for (const float distance : distances)
        {
            AZ::Transform requested = AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion::CreateRotationX(0.6f), AZ::Vector3(1.0f, 2.0f, 3.0f));
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTM, requested);
            ViewEventCounter events(childId);

            const AZ::Vector3 parentMoved = WorldTranslationOf(parentId) + AZ::Vector3(distance, 0.0f, 0.0f);
            AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetWorldTranslation, parentMoved);

            EXPECT_EQ(events.m_count, 1) << distance;
            EXPECT_TRUE(WorldTranslationOf(childId).IsClose(requested.GetTranslation() + AZ::Vector3(distance, 0.0f, 0.0f), 1.0e-5f)) << distance;

            // Back to where it was, for the next distance.
            AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetWorldTranslation, parent.m_translation);
        }
    }

    TEST_F(Transform2HierarchyTest, DoNotUpdateParent_UnderARotatingGrandparent_SaysNothingToItsChild)
    {
        Node grandparent;
        grandparent.m_translation = AZ::Vector3(1.0f, 0.0f, 0.0f);
        grandparent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node parent;
        parent.m_translation = AZ::Vector3(3.0f, 1.0f, 2.0f);
        Node child;
        child.m_translation = AZ::Vector3(1.0f, 1.0f, 0.0f);
        child.m_rotation = AZ::Quaternion::CreateRotationZ(0.3f);
        const AZ::EntityId grandparentId = CreateEntity(grandparent)->GetId();
        const AZ::EntityId parentId = CreateEntity(parent, grandparentId)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();
        AZ::TransformBus::Event(parentId, &AZ::TransformBus::Events::SetOnParentChangedBehavior, AZ::OnParentChangedBehavior::DoNotUpdate);

        const AZ::Transform parentView = WorldTMOf(parentId);
        const AZ::Matrix3x4 parentMatrix = WorldMatrixOf(parentId);
        const AZ::Transform childView = WorldTMOf(childId);
        const AZ::Matrix3x4 childMatrix = WorldMatrixOf(childId);
        ViewEventCounter parentEvents(parentId);
        ViewEventCounter childEvents(childId);
        Transform2Recorder recorder;

        // The parent keeps its place in the world while the grandparent turns. The values it solves against the turned grandparent
        // are the old ones to within rounding, and that is no change: its child hears nothing, as the child of a Transform 1 would.
        for (int step = 0; step < 40; ++step)
        {
            AZ::TransformBus::Event(grandparentId, &AZ::TransformBus::Events::RotateAroundLocalZ, 0.1f + 0.013f * static_cast<float>(step));

            ASSERT_TRUE(WorldTMOf(parentId) == parentView) << step;
            ASSERT_TRUE(WorldMatrixOf(parentId) == parentMatrix) << step;
            ASSERT_TRUE(WorldTMOf(childId) == childView) << step;
            ASSERT_TRUE(WorldMatrixOf(childId) == childMatrix) << step;
            ASSERT_EQ(childEvents.m_count, 0) << step;
        }
        EXPECT_EQ(parentEvents.m_count, 0);
        EXPECT_EQ(recorder.GetCounts(parentId).m_worldChanged, 0);
        EXPECT_EQ(recorder.GetCounts(childId).m_worldChanged, 0);
    }

    TEST_F(Transform2HierarchyTest, SetWorldTM_OfTheViewItself_StaysBitExact)
    {
        // The request can be the reference that GetWorldTM returns, which the write itself replaces while it recomputes.
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 2.0f, 3.0f);
        parent.m_rotation = AZ::Quaternion::CreateRotationZ(0.3f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        Node child;
        child.m_scale = AZ::Vector3(1.0f, 2.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();
        AZ::TransformInterface* transform = AZ::TransformBus::FindFirstHandler(childId);
        ASSERT_NE(transform, nullptr);

        Random random(8642u);
        for (int i = 0; i < 20; ++i)
        {
            AZ::Transform requested = AZ::Transform::CreateFromQuaternionAndTranslation(RandomRotation(random), RandomTranslation(random, 20.0f));
            requested.SetUniformScale(WorldTMOf(childId).GetUniformScale());
            AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTM, requested);
            ASSERT_TRUE(WorldTMOf(childId) == requested) << i;
            const AZ::Transform localBefore = LocalTMOf(childId);
            const AZ::Matrix3x4 matrixBefore = WorldMatrixOf(childId);
            ViewEventCounter events(childId);

            transform->SetWorldTM(transform->GetWorldTM());

            EXPECT_TRUE(WorldTMOf(childId) == requested) << i;
            EXPECT_TRUE(LocalTMOf(childId) == localBefore) << i;
            EXPECT_TRUE(WorldMatrixOf(childId).GetTranslation() == requested.GetTranslation()) << i;
            EXPECT_EQ(events.m_count, 1) << i; // a write notifies even when nothing changed

            // The local write with the reference of the local transform is as safe.
            transform->SetLocalTM(transform->GetLocalTM());
            EXPECT_TRUE(LocalTMOf(childId) == localBefore) << i;
            EXPECT_EQ(events.m_count, 2) << i;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Reparenting between parents, and the events of the hierarchy

    TEST_F(Transform2HierarchyTest, Reparent_FromOneParentToAnother_MovesTheChildBetweenTheChildListsAndTellsEveryone)
    {
        Node a;
        a.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        Node b;
        b.m_translation = AZ::Vector3(0.0f, 20.0f, 0.0f);
        Node c;
        c.m_translation = AZ::Vector3(1.0f, 0.0f, 0.0f);
        const AZ::EntityId aId = CreateEntity(a)->GetId();
        const AZ::EntityId bId = CreateEntity(b)->GetId();
        AZ::Entity* cEntity = CreateEntity(c, aId);
        const AZ::EntityId cId = cEntity->GetId();
        HierarchyProbe aProbe(aId);
        HierarchyProbe bProbe(bId);
        HierarchyProbe cProbe(cId);
        Transform2Recorder recorder;
        const AZ::Transform viewBefore = WorldTMOf(cId);
        ASSERT_EQ(ChildrenOf(aId).size(), 1u);
        EXPECT_TRUE(ChildrenOf(bId).empty());

        AZ::TransformBus::Event(cId, &AZ::TransformBus::Events::SetParent, bId);

        // The child heard one change of parent, the old parent heard it go, the new one heard it come, and nobody else heard anything.
        EXPECT_EQ(ParentIdOf(cId), bId);
        EXPECT_EQ(cProbe.m_parentChanged, 1);
        EXPECT_EQ(cProbe.m_oldParent, aId);
        EXPECT_EQ(cProbe.m_newParent, bId);
        EXPECT_EQ(aProbe.m_childRemoved, 1);
        EXPECT_EQ(aProbe.m_lastRemoved, cId);
        EXPECT_EQ(aProbe.m_childAdded, 0);
        EXPECT_EQ(bProbe.m_childAdded, 1);
        EXPECT_EQ(bProbe.m_lastAdded, cId);
        EXPECT_EQ(bProbe.m_childRemoved, 0);
        EXPECT_TRUE(ChildrenOf(aId).empty());
        ASSERT_EQ(ChildrenOf(bId).size(), 1u);
        EXPECT_EQ(ChildrenOf(bId)[0], cId);

        // The view stayed (11, 0, 0), so the authored local is that minus the new parent at (0, 20, 0), worked out by hand.
        EXPECT_TRUE(WorldTMOf(cId) == viewBefore);
        EXPECT_TRUE(LocalTMOf(cId).GetTranslation().IsClose(AZ::Vector3(11.0f, -20.0f, 0.0f), 1.0e-4f));

        // It follows the new parent and not the old one: a flip of A leaves it where it is, a flip of B takes it to x = 0 - 11.
        AZ::Transform2RequestBus::Event(aId, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_TRUE(WorldTMOf(cId) == viewBefore);
        EXPECT_EQ(recorder.GetCounts(cId).m_scaleChanged, 0);
        AZ::Transform2RequestBus::Event(bId, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_TRUE(WorldTranslationOf(cId).IsClose(AZ::Vector3(-11.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_EQ(recorder.GetCounts(cId).m_scaleChanged, 1);

        // Out of every parent.
        AZ::TransformBus::Event(cId, &AZ::TransformBus::Events::SetParent, AZ::EntityId());
        EXPECT_FALSE(ParentIdOf(cId).IsValid());
        EXPECT_EQ(cProbe.m_parentChanged, 2);
        EXPECT_EQ(cProbe.m_oldParent, bId);
        EXPECT_FALSE(cProbe.m_newParent.IsValid());
        EXPECT_EQ(bProbe.m_childRemoved, 1);
        EXPECT_TRUE(ChildrenOf(bId).empty());

        // And a child that is destroyed tells its parent.
        AZ::TransformBus::Event(cId, &AZ::TransformBus::Events::SetParent, bId);
        EXPECT_EQ(bProbe.m_childAdded, 2);
        DestroyEntity(cEntity);
        EXPECT_EQ(bProbe.m_childRemoved, 2);
        EXPECT_EQ(bProbe.m_lastRemoved, cId);
        EXPECT_TRUE(ChildrenOf(bId).empty());
    }

    TEST_F(Transform2HierarchyTest, SetParent_ToItself_IsRefusedWithAWarning)
    {
        const AZ::EntityId id = CreateEntity(Node())->GetId();
        WarningCounter warnings;

        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetParent, id);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetParentRelative, id);

#if defined(AZ_ENABLE_TRACING)
        EXPECT_EQ(warnings.m_count, 2);
#endif
        EXPECT_FALSE(ParentIdOf(id).IsValid());
        EXPECT_TRUE(ChildrenOf(id).empty());
    }

#if !defined(AZ_RELEASE_BUILD)
    TEST_F(Transform2HierarchyTest, SetParent_UnderItsOwnChild_IsRefusedWithAnErrorAndBothStayUsable)
    {
        Node a;
        a.m_translation = AZ::Vector3(1.0f, 0.0f, 0.0f);
        Node b;
        b.m_translation = AZ::Vector3(0.0f, 2.0f, 0.0f);
        const AZ::EntityId aId = CreateEntity(a)->GetId();
        const AZ::EntityId bId = CreateEntity(b, aId)->GetId();

        AZ_TEST_START_TRACE_SUPPRESSION;
        AZ::TransformBus::Event(aId, &AZ::TransformBus::Events::SetParent, bId);
        AZ_TEST_STOP_TRACE_SUPPRESSION(1);

        // A did not take its own child as a parent, and B is still the child of A.
        EXPECT_FALSE(ParentIdOf(aId).IsValid());
        EXPECT_EQ(ParentIdOf(bId), aId);
        EXPECT_TRUE(ChildrenOf(bId).empty());

        // Writes stay finite, and B still follows A: (4, 0, 0) and the offset of (0, 2, 0).
        AZ::TransformBus::Event(aId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(4.0f, 0.0f, 0.0f));
        AZ::TransformBus::Event(bId, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(0.0f, 3.0f, 0.0f));
        EXPECT_TRUE(WorldTMOf(aId).IsFinite());
        EXPECT_TRUE(WorldTMOf(bId).IsFinite());
        EXPECT_TRUE(WorldTranslationOf(bId).IsClose(AZ::Vector3(4.0f, 3.0f, 0.0f), 1.0e-4f));
    }
#endif

    TEST_F(Transform2HierarchyTest, FirstWorldAndEveryReparent_RaiseTheTransform1EventEvenWhenTheViewDoesNotChange)
    {
        // A component that connects before Init waits for the event of the first world, of a root and of a child.
        Node placed;
        placed.m_translation = AZ::Vector3(5.0f, 0.0f, 0.0f);
        AZ::Entity* rootEntity = BuildEntity(placed);
        ViewEventCounter rootEvents(rootEntity->GetId());
        rootEntity->Init();
        rootEntity->Activate();
        EXPECT_GE(rootEvents.m_count, 1);

        AZ::Entity* childEntity = BuildEntity(placed, rootEntity->GetId());
        ViewEventCounter childEvents(childEntity->GetId());
        childEntity->Init();
        childEntity->Activate();
        EXPECT_GE(childEvents.m_count, 1);

        // Parents that change nothing: the same place, with no scale and no turn.
        const AZ::EntityId first = CreateEntity(Node())->GetId();
        const AZ::EntityId second = CreateEntity(Node())->GetId();
        const AZ::EntityId movedId = CreateEntity(placed)->GetId();
        const AZ::Transform view = WorldTMOf(movedId);
        ViewEventCounter events(movedId);

        // Each way of changing parents raises the event once, as Transform 1 does, whether the view changed or not.
        const auto expectOneEvent = [&](const char* label)
        {
            EXPECT_EQ(events.m_count, 1) << label;
            EXPECT_TRUE(WorldTMOf(movedId) == view) << label;
            events.m_count = 0;
        };
        AZ::TransformBus::Event(movedId, &AZ::TransformBus::Events::SetParent, first);
        expectOneEvent("keep world, to a parent");
        AZ::TransformBus::Event(movedId, &AZ::TransformBus::Events::SetParentRelative, second);
        expectOneEvent("relative, to a parent");
        AZ::TransformBus::Event(movedId, &AZ::TransformBus::Events::SetParentRelative, AZ::EntityId());
        expectOneEvent("relative, to none");
        AZ::TransformBus::Event(movedId, &AZ::TransformBus::Events::SetParent, first);
        expectOneEvent("keep world, to a parent again");
        AZ::TransformBus::Event(movedId, &AZ::TransformBus::Events::SetParent, AZ::EntityId());
        expectOneEvent("keep world, to none");
    }

    TEST_F(Transform2HierarchyTest, MaintainCurrentWorldTransform_KeepsTheWorldWhenTheParentIsFoundAtActivation)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();

        // The configuration holds a local of 5 and a world of 20: which one the entity ends at is the mode.
        AZ::TransformConfig config;
        config.m_parentId = parentId;
        config.m_localTransform = AZ::Transform::CreateTranslation(AZ::Vector3(5.0f, 0.0f, 0.0f));
        config.m_worldTransform = AZ::Transform::CreateTranslation(AZ::Vector3(20.0f, 0.0f, 0.0f));

        // The original relative transform: 10 - 5, with the mirror.
        AZ::Entity* relative = BuildEntityFromConfig(AZ::Vector3::CreateOne(), config);
        relative->Init();
        relative->Activate();
        EXPECT_TRUE(WorldTranslationOf(relative->GetId()).IsClose(AZ::Vector3(5.0f, 0.0f, 0.0f), 1.0e-4f));

        // The current world: 20, and the local that gets it from the mirrored parent is 10 - 20.
        config.m_parentActivationTransformMode = AZ::TransformConfig::ParentActivationTransformMode::MaintainCurrentWorldTransform;
        AZ::Entity* maintained = BuildEntityFromConfig(AZ::Vector3::CreateOne(), config);
        maintained->Init();
        maintained->Activate();
        EXPECT_TRUE(WorldTranslationOf(maintained->GetId()).IsClose(AZ::Vector3(20.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(LocalTMOf(maintained->GetId()).GetTranslation().IsClose(AZ::Vector3(-10.0f, 0.0f, 0.0f), 1.0e-4f));
    }

    TEST_F(Transform2HierarchyTest, ChildSetUnderAParentThatIsNotEffectivelyActive_GoesDownAndComesBackWithIt)
    {
        const Node placed{};
        AZ::Entity* parentEntity = BuildEntity(placed);
        parentEntity->SetRuntimeActiveByDefault(false);
        parentEntity->Init(); // initialized and not active, and not to be active yet
        ASSERT_FALSE(parentEntity->IsEffectivelyActive());
        AZ::Entity* childEntity = CreateEntity(placed);
        ASSERT_TRUE(childEntity->GetState() == AZ::Entity::State::Active);

        AZ::TransformBus::Event(childEntity->GetId(), &AZ::TransformBus::Events::SetParent, parentEntity->GetId());

        // Transform 1 puts the child into the state of its parent when it finds it.
        EXPECT_TRUE(childEntity->GetState() == AZ::Entity::State::Init);

        parentEntity->SetEntityActive(true);
        parentEntity->ApplyEffectiveActiveState();
        EXPECT_TRUE(parentEntity->GetState() == AZ::Entity::State::Active);
        EXPECT_TRUE(childEntity->GetState() == AZ::Entity::State::Active);
    }

    //////////////////////////////////////////////////////////////////////////
    // The float of a world write or a reparent, under a parent whose own float is not 1

    TEST_F(Transform2HierarchyTest, WorldFloatWritten_UnderAParentWithAFloat_FoldsIntoTheOwnScaleByTheRatio)
    {
        const AZ::Vector3 parentScales[] = { AZ::Vector3(2.0f), AZ::Vector3(-2.0f, 2.0f, 2.0f) };
        for (const AZ::Vector3& parentScale : parentScales)
        {
            Node parent;
            parent.m_scale = parentScale;
            Node child;
            child.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
            const AZ::EntityId parentId = CreateEntity(parent)->GetId();
            const AZ::EntityId id = CreateEntity(child, parentId)->GetId();
            ASSERT_EQ(WorldTMOf(id).GetUniformScale(), 2.0f);

            // A world float of 4 where the child has 2: the own scale doubles, 1 to 2, and the place stays.
            AZ::Transform view = WorldTMOf(id);
            view.SetUniformScale(4.0f);
            AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTM, view);

            Node folded = child;
            folded.m_scale = AZ::Vector3(2.0f);
            EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(2.0f), 1.0e-5f));
            EXPECT_EQ(WorldTMOf(id).GetUniformScale(), 4.0f);
            EXPECT_TRUE(WorldScaleOf(id).IsClose(parentScale * 2.0f, 1.0e-5f));
            ExpectWorld(id, { parent, folded }, "folded");
        }
    }

    TEST_F(Transform2HierarchyTest, KeepWorldReparent_UnderAParentWithAFloat_KeepsTheWorldFloat)
    {
        const AZ::Vector3 parentScales[] = { AZ::Vector3(4.0f), AZ::Vector3(-4.0f, 4.0f, 4.0f) };
        for (const AZ::Vector3& parentScale : parentScales)
        {
            Node parent;
            parent.m_scale = parentScale;
            Node child;
            child.m_translation = AZ::Vector3(3.0f, 0.0f, 0.0f);
            child.m_scale = AZ::Vector3(2.0f);
            const AZ::EntityId parentId = CreateEntity(parent)->GetId();
            const AZ::EntityId id = CreateEntity(child)->GetId();
            const AZ::Transform viewBefore = WorldTMOf(id);
            ASSERT_EQ(viewBefore.GetUniformScale(), 2.0f);

            AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetParent, parentId);

            // The view is what it was, float included, so the own scale is halved against the 4 of the parent: 2 * 4 / 8.
            EXPECT_TRUE(WorldTMOf(id) == viewBefore);
            EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(0.5f), 1.0e-5f));
            EXPECT_TRUE(WorldScaleOf(id).IsClose(parentScale * 0.5f, 1.0e-5f));
        }
    }

    TEST_F(Transform2HierarchyTest, UniformFloatWritten_FoldsAboveTheToleranceAndNotBelowIt)
    {
        Node node;
        node.m_scale = AZ::Vector3(-2.0f, 1.0f, 1.0f);
        const AZ::EntityId id = CreateEntity(node)->GetId();
        Transform2Recorder recorder;

        // A tenth of a thousandth is a write: the size follows, and the signs and ratios stay.
        const float larger = 2.0f * (1.0f + 1.0e-4f);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalUniformScale, larger);
        EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(-1.0f, 0.5f, 0.5f) * larger, 1.0e-5f));
        EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, 1);

        // Ten millionths is the rounding of a float that went through a computation: not a write.
        const float noisy = larger * (1.0f + 1.0e-7f);
        ASSERT_NE(noisy, larger);
        const AZ::Vector3 scaleBefore = LocalScaleOf(id);
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalUniformScale, noisy);
        EXPECT_TRUE(LocalScaleOf(id) == scaleBefore);
        EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, 1);

        // The same through the world transform.
        AZ::Transform view = WorldTMOf(id);
        view.SetUniformScale(view.GetUniformScale() * (1.0f + 1.0e-4f));
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTM, view);
        EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(-1.0f, 0.5f, 0.5f) * (larger * (1.0f + 1.0e-4f)), 1.0e-5f));
        EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, 2);

        const AZ::Vector3 scaleAfterWorldWrite = LocalScaleOf(id);
        view.SetUniformScale(view.GetUniformScale() * (1.0f + 1.0e-7f));
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTM, view);
        EXPECT_TRUE(LocalScaleOf(id) == scaleAfterWorldWrite);
        EXPECT_EQ(recorder.GetCounts(id).m_scaleChanged, 2);

        // And a read-modify-write of the local transform gives its own float back.
        AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetLocalTM, LocalTMOf(id));
        EXPECT_TRUE(LocalScaleOf(id) == scaleAfterWorldWrite);
    }

    //////////////////////////////////////////////////////////////////////////
    // Configuration of a child, and a child that has no parent yet

    TEST_F(Transform2HierarchyTest, ConfiguredChild_FoldsTheLocalFloatAndNotTheWorldFloat)
    {
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();

        // A prefab child authored at -1, 1, 1, spawned with a float of 2 against the parent, and 4 in the world it was saved in.
        AZ::TransformConfig config;
        config.m_parentId = parentId;
        config.m_localTransform = AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f));
        config.m_localTransform.SetUniformScale(2.0f);
        config.m_worldTransform = config.m_localTransform;
        config.m_worldTransform.SetUniformScale(4.0f);
        AZ::Entity* childEntity = BuildEntityFromConfig(AZ::Vector3(-1.0f, 1.0f, 1.0f), config);
        childEntity->Init();
        childEntity->Activate();

        // The float of the local is the one that folds: -1, 1, 1 times 2.
        Node child;
        child.m_translation = AZ::Vector3(1.0f, 0.0f, 0.0f);
        child.m_scale = AZ::Vector3(-2.0f, 2.0f, 2.0f);
        EXPECT_TRUE(LocalScaleOf(childEntity->GetId()).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(WorldScaleOf(childEntity->GetId()).IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f), 1.0e-5f));
        ExpectWorld(childEntity->GetId(), { parent, child }, "configured child");
    }

    TEST_F(Transform2HierarchyTest, ChildWithNoParentYet_AnnouncesTheFloatOfItsViewWithItsOwnSigns)
    {
        Transform2Recorder recorder;
        Node parent;
        parent.m_translation = AZ::Vector3(10.0f, 0.0f, 0.0f);
        AZ::Entity* parentEntity = BuildEntity(parent);

        // The child was saved in a world whose float is 4, and its own is 1.
        AZ::TransformConfig config;
        config.m_parentId = parentEntity->GetId();
        config.m_localTransform = AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f));
        config.m_worldTransform = config.m_localTransform;
        config.m_worldTransform.SetUniformScale(4.0f);
        AZ::Entity* childEntity = BuildEntityFromConfig(AZ::Vector3(-1.0f, 1.0f, 1.0f), config);
        const AZ::EntityId childId = childEntity->GetId();
        childEntity->Init();
        childEntity->Activate();

        // The parent is not there, so the view stands: 4 times the signs and ratios of the child.
        EXPECT_TRUE(WorldScaleOf(childId).IsClose(AZ::Vector3(-4.0f, 4.0f, 4.0f), 1.0e-5f));
        EXPECT_TRUE(recorder.GetCounts(childId).m_activatedScale.IsClose(AZ::Vector3(-4.0f, 4.0f, 4.0f), 1.0e-5f));

        // The parent arrives, and the child is its own again under it.
        parentEntity->Init();
        parentEntity->Activate();
        EXPECT_TRUE(WorldScaleOf(childId).IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));
        EXPECT_TRUE(WorldTranslationOf(childId).IsClose(AZ::Vector3(11.0f, 0.0f, 0.0f), 1.0e-4f));
    }

    //////////////////////////////////////////////////////////////////////////
    // A change of the exact world that is not a change of the Transform 1 view

    TEST_F(Transform2HierarchyTest, ParentAxisMagnitude_BelowTheLargestAxis_MovesItsChildrenAndRaisesNoTransform1Event)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(4.0f, 2.0f, 1.0f);
        Node child;
        child.m_translation = AZ::Vector3(0.0f, 1.0f, 0.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId childId = CreateEntity(child, parentId)->GetId();
        EXPECT_TRUE(WorldTranslationOf(childId).IsClose(AZ::Vector3(0.0f, 2.0f, 0.0f), 1.0e-5f)); // 1 * 2, by hand
        ViewEventCounter parentEvents(parentId);
        ViewEventCounter childEvents(childId);

        // The largest axis stays 4, so the float of the Transform 1 view does not change. The child still has to move: 1 * 1.
        Node squeezed = parent;
        squeezed.m_scale = AZ::Vector3(4.0f, 1.0f, 1.0f);
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, squeezed.m_scale);

        EXPECT_EQ(parentEvents.m_count, 0);
        EXPECT_EQ(childEvents.m_count, 1);
        EXPECT_TRUE(WorldTranslationOf(childId).IsClose(AZ::Vector3(0.0f, 1.0f, 0.0f), 1.0e-5f));
        ExpectWorld(childId, { squeezed, child }, "squeezed parent");
        EXPECT_TRUE(WorldScaleOf(childId).IsClose(AZ::Vector3(4.0f, 1.0f, 1.0f), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, ParentAxisMagnitude_BelowTheLargestAxis_TellsAShearedDescendantItsWorldChanged)
    {
        // The parent stays in place and so do its middle child and the grandchild, whose view does not change either: its place,
        // its turn and its largest column are the same. Only the shorter columns of its exact matrix change, and it is sheared.
        Node parent;
        parent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
        parent.m_scale = AZ::Vector3(4.0f, 2.0f, 1.0f);
        const Node middle{};
        Node grandchild;
        grandchild.m_rotation = AZ::Quaternion::CreateRotationX(AZ::Constants::QuarterPi);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId middleId = CreateEntity(middle, parentId)->GetId();
        const AZ::EntityId grandchildId = CreateEntity(grandchild, middleId)->GetId();
        ASSERT_TRUE(IsShearedOf(grandchildId));
        const AZ::Transform viewBefore = WorldTMOf(grandchildId);
        ViewEventCounter parentEvents(parentId);
        ViewEventCounter middleEvents(middleId);
        ViewEventCounter grandchildEvents(grandchildId);
        Transform2Recorder recorder;

        Node stretched = parent;
        stretched.m_scale = AZ::Vector3(4.0f, 3.0f, 1.0f);
        AZ::Transform2RequestBus::Event(parentId, &AZ::Transform2Requests::SetLocalScale, stretched.m_scale);

        // The view of every one of them is what it was, and the grandchild is told, as a sheared entity whose exact world changed.
        EXPECT_TRUE(WorldTMOf(grandchildId) == viewBefore);
        EXPECT_EQ(parentEvents.m_count, 0);
        EXPECT_EQ(middleEvents.m_count, 0);
        EXPECT_EQ(grandchildEvents.m_count, 0);
        EXPECT_EQ(recorder.GetCounts(grandchildId).m_worldChanged, 1);
        EXPECT_EQ(recorder.GetCounts(grandchildId).m_scaleChanged, 1);
        EXPECT_EQ(recorder.GetCounts(middleId).m_worldChanged, 0);

        // The columns of the grandchild, worked out by hand: diag(4,3,1) times a turn of 45 degrees about x.
        ASSERT_TRUE(IsShearedOf(grandchildId));
        const AZ::Matrix3x4 world = WorldMatrixOf(grandchildId);
        const float root = 0.70710678f;
        EXPECT_TRUE(world.GetColumn(0).IsClose(AZ::Vector3(4.0f, 0.0f, 0.0f), 1.0e-4f));
        EXPECT_TRUE(world.GetColumn(1).IsClose(AZ::Vector3(0.0f, 3.0f * root, root), 1.0e-4f));
        EXPECT_TRUE(world.GetColumn(2).IsClose(AZ::Vector3(0.0f, -3.0f * root, root), 1.0e-4f));
        ExpectWorld(grandchildId, { stretched, middle, grandchild }, "stretched parent");
    }

    //////////////////////////////////////////////////////////////////////////
    // The residual written through the NonUniformScaleRequestBus

    TEST_F(Transform2HierarchyTest, SetScale_OnAChildOfANonUniformParent_ReachesTheResidualThatWasAskedFor)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(2.0f, 1.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId id = CreateEntity(Node(), parentId)->GetId();
        ASSERT_TRUE(ResidualOf(id).IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f), 1.0e-5f));

        // A residual of 1,1,1 is a world scale with equal axes: the child makes up for the stretch of its parent, 1, 2, 2.
        AZ::NonUniformScaleRequestBus::Event(id, &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3::CreateOne());

        EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(1.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(ResidualOf(id).IsClose(AZ::Vector3::CreateOne(), 1.0e-5f));
        const AZ::Matrix3x4 world = WorldMatrixOf(id);
        EXPECT_TRUE(world.GetColumn(0).IsClose(AZ::Vector3(2.0f, 0.0f, 0.0f), 1.0e-5f));
        EXPECT_TRUE(world.GetColumn(1).IsClose(AZ::Vector3(0.0f, 2.0f, 0.0f), 1.0e-5f));
        EXPECT_TRUE(world.GetColumn(2).IsClose(AZ::Vector3(0.0f, 0.0f, 2.0f), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, SetScale_OnAChildOfAFlippedParent_KeepsTheSignsOfTheWorld)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(-2.0f, 1.0f, 1.0f);
        Node child;
        child.m_scale = AZ::Vector3(1.0f, -1.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();
        const AZ::EntityId id = CreateEntity(child, parentId)->GetId();
        ASSERT_TRUE(WorldScaleOf(id).IsClose(AZ::Vector3(-2.0f, -1.0f, 1.0f), 1.0e-5f));

        AZ::NonUniformScaleRequestBus::Event(id, &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3::CreateOne());

        // The axes of the world need 2, 2, 2: the own scale makes up what the parent leaves, 1, 2, 2, and keeps its minus on y.
        EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(1.0f, -2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(WorldScaleOf(id).IsClose(AZ::Vector3(-2.0f, -2.0f, 2.0f), 1.0e-5f));
        EXPECT_TRUE(ResidualOf(id).IsClose(AZ::Vector3::CreateOne(), 1.0e-5f));
    }

    TEST_F(Transform2HierarchyTest, SetScale_OnAnAxisWithNoWorldMagnitude_TakesTheSizeAndKeepsTheOtherSigns)
    {
        Node collapsed;
        collapsed.m_scale = AZ::Vector3(0.0f, -1.0f, 1.0f);
        const AZ::EntityId id = CreateEntity(collapsed)->GetId();
        ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(id));

        // The collapsed x has no factor to take, so it takes the size: 1 times the 2 that was asked for. y and z are as they were.
        AZ::NonUniformScaleRequestBus::Event(id, &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3(2.0f, 1.0f, 1.0f));

        EXPECT_TRUE(LocalScaleOf(id).IsFinite());
        EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(2.0f, -1.0f, 1.0f), 1.0e-5f));
        EXPECT_TRUE(ResidualOf(id).IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f), 1.0e-5f));
    }

    //////////////////////////////////////////////////////////////////////////
    // A parent so small that the inverse of Matrix3x4 would give up on it

    TEST_F(Transform2HierarchyTest, WorldTranslationWritten_UnderATinyParent_SolvesTheLocalWithTheTrueInverse)
    {
        const AZ::Vector3 parentScales[] = { AZ::Vector3(0.001f), AZ::Vector3(-0.001f, 0.001f, 0.001f) };
        for (const AZ::Vector3& parentScale : parentScales)
        {
            Node parent;
            parent.m_translation = AZ::Vector3(1.0f, 2.0f, 3.0f);
            parent.m_scale = parentScale;
            const AZ::EntityId parentId = CreateEntity(parent)->GetId();
            const AZ::EntityId id = CreateEntity(Node(), parentId)->GetId();

            const AZ::Vector3 request(5.0f, 4.0f, 6.0f);
            AZ::TransformBus::Event(id, &AZ::TransformBus::Events::SetWorldTranslation, request);
            EXPECT_TRUE(WorldTranslationOf(id) == request);

            // The local is the offset over the scale of the parent, worked out by hand: 4000, 2000, 3000, with x mirrored.
            const float mirror = parentScale.GetX() < 0.0f ? -1.0f : 1.0f;
            EXPECT_TRUE(LocalTMOf(id).GetTranslation().IsClose(AZ::Vector3(mirror * 4000.0f, 2000.0f, 3000.0f), 0.05f));

            // And it composes back: any recompute of the child lands where it was put.
            AZ::Transform2RequestBus::Event(id, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(2.0f));
            EXPECT_TRUE(WorldTranslationOf(id).IsClose(request, 1.0e-3f));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Warnings, and a bus that another component already serves

    TEST_F(Transform2HierarchyTest, Transform2ChildUnderATransform2_RaisesNoWarning)
    {
        Node parent;
        parent.m_scale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        const AZ::EntityId parentId = CreateEntity(parent)->GetId();

        WarningCounter warnings;
        CreateEntity(Node(), parentId);
        CreateEntity(Node(), parentId);
#if defined(AZ_ENABLE_TRACING)
        EXPECT_EQ(warnings.m_count, 0);
#endif
    }

    TEST_F(Transform2HierarchyTest, NonUniformEntity_WhoseScaleBusIsServedByAnotherComponent_LeavesItAlone)
    {
        Node stretched;
        stretched.m_scale = AZ::Vector3(2.0f, 1.0f, 1.0f);
        AZ::Entity* entity = BuildEntity(stretched);
        const AZ::EntityId id = entity->GetId();
        ForeignScaleHandler foreign(id);
        entity->Init();
        entity->Activate();

        // Every write that changes the scale tries to take the bus again.
        AZ::Transform2RequestBus::Event(id, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(3.0f, 1.0f, 1.0f));
        AZ::Transform2RequestBus::Event(id, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(-1.0f, 5.0f, 1.0f));

        EXPECT_TRUE(ResidualOf(id) == AZ::Vector3(ForeignScaleHandler::Marker));
        EXPECT_TRUE(LocalScaleOf(id).IsClose(AZ::Vector3(-1.0f, 5.0f, 1.0f), 1.0e-5f));
    }

    //////////////////////////////////////////////////////////////////////////
    // A component that has no entity, as spawn and builder code makes one

    TEST_F(Transform2HierarchyTest, ComponentWithNoEntity_TakesWritesAndKeepsItsValues)
    {
        WarningCounter warnings;
        AzFramework::Transform2Component component(AZ::Vector3(-1.0f, 1.0f, 1.0f));
        const AzFramework::Transform2Component& reader = component;

        // A world: the float of the transform is the stand-in of the scale, 1.
        component.SetWorldTM(AZ::Transform::CreateTranslation(AZ::Vector3(5.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(component.GetWorldTM().GetTranslation() == AZ::Vector3(5.0f, 0.0f, 0.0f));
        EXPECT_TRUE(component.GetLocalTM().GetTranslation() == AZ::Vector3(5.0f, 0.0f, 0.0f));
        EXPECT_TRUE(reader.GetWorldMatrix().GetTranslation() == AZ::Vector3(5.0f, 0.0f, 0.0f));
        EXPECT_TRUE(reader.GetWorldScale().IsClose(AZ::Vector3(-1.0f, 1.0f, 1.0f), 1.0e-5f));

        // A local.
        component.SetLocalTM(AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 7.0f, 0.0f)));
        EXPECT_TRUE(component.GetWorldTM().GetTranslation() == AZ::Vector3(0.0f, 7.0f, 0.0f));

        // A scale that makes it non-uniform, which would take the scale bus of an entity that it does not have.
        component.SetLocalScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
        EXPECT_TRUE(reader.GetLocalScale().IsClose(AZ::Vector3(2.0f, 1.0f, 1.0f), 1.0e-5f));
        EXPECT_TRUE(reader.GetWorldScale().IsClose(AZ::Vector3(2.0f, 1.0f, 1.0f), 1.0e-5f));
        EXPECT_EQ(component.GetLocalTM().GetUniformScale(), 2.0f);

        // A float that differs from the one it holds: the own scale takes the ratio, 2 to 4.
        AZ::Transform view = component.GetWorldTM();
        view.SetUniformScale(4.0f);
        component.SetWorldTM(view);
        EXPECT_TRUE(reader.GetLocalScale().IsClose(AZ::Vector3(4.0f, 2.0f, 2.0f), 1.0e-5f));
        EXPECT_EQ(component.GetWorldTM().GetUniformScale(), 4.0f);
        EXPECT_TRUE(component.GetWorldTM().GetTranslation() == AZ::Vector3(0.0f, 7.0f, 0.0f));

#if defined(AZ_ENABLE_TRACING)
        EXPECT_EQ(warnings.m_count, 0);
#endif
    }

    //////////////////////////////////////////////////////////////////////////
    // What is saved

    TEST_F(Transform2HierarchyTest, Serialized_KeepsTheSignedScaleUnderTheNameLocalScale)
    {
        AZ::Entity source;
        source.CreateComponent<AzFramework::Transform2Component>(AZ::Vector3(-2.0f, 1.0f, 1.0f));
        AZ::SerializeContext* context = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(context, &AZ::ComponentApplicationRequests::GetSerializeContext);
        ASSERT_NE(context, nullptr);

        AZStd::vector<char> buffer;
        AZ::IO::ByteContainerStream<AZStd::vector<char>> stream(&buffer);
        ASSERT_TRUE(AZ::Utils::SaveObjectToStream(stream, AZ::DataStream::ST_XML, &source, context));

        // The name is what saved data and prefabs find the field by.
        const AZStd::string text(buffer.begin(), buffer.end());
        EXPECT_NE(text.find("LocalScale"), AZStd::string::npos);
    }
} // namespace UnitTest
