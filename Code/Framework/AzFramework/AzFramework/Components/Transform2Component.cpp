/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Visibility/EntityBoundsUnionBus.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/vector.h>

namespace AzFramework
{
    using AZ::Transform2::ChainState;

    namespace
    {
        // Two floats whose relative difference is below this are one float.
        constexpr float FloatChangedTolerance = 1e-6f;

        // A residual that moved by less than this has not changed for the systems that listen to it.
        constexpr float ResidualChangedTolerance = 1e-5f;

        // The debug check of a chain against its sources allows this fraction of the largest element.
        [[maybe_unused]] constexpr float CompositionTolerance = 1e-4f;

        // A stored view and the same view recomputed from the authored values are one view when they agree to this fraction.
        // One solve and one composition are a few rounding errors, and the harness f32 error of a chain at depth 8 is 1.3e-6.
        constexpr float StoredViewTolerance = 2e-6f;

        bool HasFloatChanged(float a, float b)
        {
            return AZ::GetAbs(a - b) > FloatChangedTolerance * AZ::GetMax(AZ::GetAbs(a), AZ::GetAbs(b));
        }

        // True when a chain recomputed from the authored values is the stored chain with rounding only.
        // Each column of the world is compared with its own size, so a tiny scale is not lost in a large translation.
        // The translation column also counts the translation of the parent: a child near the origin under a far parent
        // is a small difference of large numbers.
        bool IsStoredChain(const ChainState& stored, const ChainState& recomputed, const AZ::Vector3& parentTranslation)
        {
            if (!(stored.m_signs == recomputed.m_signs) || !stored.m_rotation.IsClose(recomputed.m_rotation, StoredViewTolerance))
            {
                return false;
            }

            for (int32_t column = 0; column < 4; ++column)
            {
                const AZ::Vector3 storedColumn = stored.m_world.GetColumn(column);
                const AZ::Vector3 recomputedColumn = recomputed.m_world.GetColumn(column);
                float largest = AZ::GetMax(storedColumn.GetAbs().GetMaxElement(), recomputedColumn.GetAbs().GetMaxElement());
                if (column == 3)
                {
                    largest = AZ::GetMax(largest, parentTranslation.GetAbs().GetMaxElement());
                }
                if ((storedColumn - recomputedColumn).GetAbs().GetMaxElement() > StoredViewTolerance * largest)
                {
                    return false;
                }
            }
            return true;
        }

        // +1 for every axis that is not negative, -1 for every axis that is.
        AZ::Vector3 SignOf(const AZ::Vector3& value)
        {
            return AZ::Vector3(
                value.GetX() < 0.0f ? -1.0f : 1.0f,
                value.GetY() < 0.0f ? -1.0f : 1.0f,
                value.GetZ() < 0.0f ? -1.0f : 1.0f);
        }

        // True when the entity has a Transform 2. Otherwise reports the error a script gets for a Transform 1 entity,
        // where action finishes the sentence "so its signed scale cannot be ...".
        bool RequireTransform2(AZ::EntityId entityId, [[maybe_unused]] const char* action)
        {
            const bool hasTransform2 = AZ::Transform2RequestBus::HasHandlers(entityId);
            AZ_Error("Transform2Component", hasTransform2,
                "Entity %s has no Transform 2, so its signed scale cannot be %s. Enable \"Deploy as Extended\" on its Transform.",
                entityId.ToString().c_str(), action);
            return hasTransform2;
        }

#if defined(AZ_DEBUG_BUILD)
        // True when every element of a is within tolerance of b, taken relative to the largest element of b (at least 1).
        bool IsCloseRelative(const AZ::Matrix3x4& a, const AZ::Matrix3x4& b, float tolerance)
        {
            float largest = 1.0f;
            for (int32_t row = 0; row < 3; ++row)
            {
                const AZ::Vector4 values = b.GetRow(row);
                for (int32_t column = 0; column < 4; ++column)
                {
                    largest = AZ::GetMax(largest, AZ::GetAbs(values.GetElement(column)));
                }
            }
            return a.IsClose(b, tolerance * largest);
        }
#endif
    } // namespace

    //////////////////////////////////////////////////////////////////////////
    // Reflection and services

    void Transform2Component::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            // The scale holder that was deployed beside Transform 1. Cached data that still holds it loads as nothing,
            // the way the NetBindable component of Transform 1 does, until it is exported again.
            serializeContext->ClassDeprecate("Transform2ScaleHolder", AZ::Uuid("{DF23B15E-0FCF-4745-8A2B-0BD3F1E55430}"));

            serializeContext->Class<Transform2Component, TransformComponent>()
                ->Version(1)
                ->Field("LocalScale", &Transform2Component::m_localScale)
                ;
        }

        // Scripts go through these checked methods instead of the request bus, which is not reflected: a bus call on an
        // entity without a Transform 2 finds no handler and silently does nothing, where a script needs the error.
        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            // The track view of Maestro finds the virtual properties of the transform through the class of the component.
            behaviorContext->Class<Transform2Component>("Transform2Component")->RequestBus("TransformBus");

            behaviorContext->Method("Transform2GetLocalScale", &Transform2Component::GetLocalScaleChecked,
                { { { "EntityId", "The entity to read." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Gets the signed local scale of an entity with a Transform 2. Reports an error on a Transform 1 entity. "
                    "TransformBus GetLocalScale returns the positive stand-in instead.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2SetLocalScale", &Transform2Component::SetLocalScaleChecked,
                { { { "EntityId", "The entity to change." }, { "Scale", "The signed local scale. A negative axis flips that axis." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Sets the signed local scale of an entity with a Transform 2. Reports an error on a Transform 1 entity.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2GetWorldScale", &Transform2Component::GetWorldScaleChecked,
                { { { "EntityId", "The entity to read." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Gets the signed world scale of an entity with a Transform 2: the scale it is drawn at, with every parent applied. "
                    "Reports an error on a Transform 1 entity.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2GetWorldMatrix", &Transform2Component::GetWorldMatrixChecked,
                { { { "EntityId", "The entity to read." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Gets the exact world matrix of an entity with a Transform 2, with its scale, flips and any shear in it. "
                    "Reports an error on a Transform 1 entity.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2IsExtended", &Transform2Component::IsExtended,
                { { { "EntityId", "The entity to ask about." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "True when the entity has a Transform 2. Reports no error on a Transform 1 entity, so a script can branch on it.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2FlipX", &Transform2Component::FlipXChecked,
                { { { "EntityId", "The entity to flip." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Negates the X scale of an entity with a Transform 2, which turns it around. "
                    "Reports an error on a Transform 1 entity.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2FlipY", &Transform2Component::FlipYChecked,
                { { { "EntityId", "The entity to flip." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Negates the Y scale of an entity with a Transform 2. Reports an error on a Transform 1 entity.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
            behaviorContext->Method("Transform2FlipZ", &Transform2Component::FlipZChecked,
                { { { "EntityId", "The entity to flip." } } })
                ->Attribute(AZ::Script::Attributes::ToolTip,
                    "Negates the Z scale of an entity with a Transform 2, which turns it upside down. "
                    "Reports an error on a Transform 1 entity.")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Entity")
                ->Attribute(AZ::Script::Attributes::Module, "entity")
                ;
        }
    }

    void Transform2Component::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("TransformService"));
        provided.push_back(AZ_CRC_CE("Transform2Service"));
    }

    void Transform2Component::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        // A component may provide and be incompatible with the same service when it is alone, as the editor transform is.
        // This turns a Transform 1 beside a Transform 2, which would take over the single handler of the bus, into a failed sort.
        incompatible.push_back(AZ_CRC_CE("TransformService"));
    }

    //////////////////////////////////////////////////////////////////////////
    // Construction

    Transform2Component::Transform2Component()
    {
        m_childLink.Attach(*this);
    }

    Transform2Component::Transform2Component(const AZ::Vector3& localScale)
        : Transform2Component()
    {
        m_localScale = localScale;

        // The Transform 1 float is the stand-in of the scale from construction on.
        const float standIn = AZ::Transform2::UniformStandIn(m_localScale);
        m_localTM.SetUniformScale(standIn);
        m_worldTM.SetUniformScale(standIn);
    }

    Transform2Component::~Transform2Component()
    {
        m_childLink.BusDisconnect();
        AZ::NonUniformScaleRequestBus::Handler::BusDisconnect();
        AZ::Transform2RequestBus::Handler::BusDisconnect();
    }

    //////////////////////////////////////////////////////////////////////////
    // Component lifecycle

    void Transform2Component::Init()
    {
        TransformComponent::Init();

        // Connect here, not in Activate, so that presence on the buses is already
        // valid when any other component on the entity activates.
        AZ::Transform2RequestBus::Handler::BusConnect(GetEntityId());

        // Until the first world is computed the view stands. A parent that is already there is read at OnEntityExists.
        SeedChainFromView();
        m_announcedResidual = AZ::Transform2::Residual(m_chain.m_worldScale);
        if (AZ::Transform2::IsNonUniformEffective(m_chain.m_worldScale))
        {
            ConnectResidualBus();
        }
    }

    void Transform2Component::Activate()
    {
        TransformComponent::Activate();

        m_isActive = true;
        m_announcedWorldScale = m_chain.m_worldScale;
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, GetEntityId(), m_chain.m_worldScale);
    }

    void Transform2Component::Deactivate()
    {
        AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, GetEntityId());

        m_isActive = false;
        TransformComponent::Deactivate();
    }

    bool Transform2Component::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        if (!TransformComponent::ReadInConfig(baseConfig))
        {
            return false;
        }

        // The world of a root stands, as it does in Transform 1 at its first world, and the local of a child does.
        if (!m_parentId.IsValid())
        {
            m_localTM.SetTranslation(m_worldTM.GetTranslation());
            m_localTM.SetRotation(m_worldTM.GetRotation());
        }

        // A plain configuration carries one float and no vector. The float folds into the vector by ratio, so a prefab
        // authored at -1,1,1 that is spawned at scale 2 is -2,2,2.
        FoldUniformScale((m_parentId.IsValid() ? m_localTM : m_worldTM).GetUniformScale());
        m_localTM.SetUniformScale(AZ::Transform2::UniformStandIn(m_localScale));
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Parent link
    //
    // SetParentT2 and ProcessParentT2 are the parent link of Transform 1 (SetParentImpl, ProcessParentEntity) with three
    // differences. They run no Transform 1 arithmetic. The children query is answered by m_childLink, because the handler of
    // Transform 1 is a private base. And the parent is computed once: the connect policy of the EntityBus runs
    // ProcessParentT2 when the parent exists, and the explicit call runs only when it did not.

    void Transform2Component::SetParent(AZ::EntityId id)
    {
        SetParentT2(id, true);
    }

    void Transform2Component::SetParentRelative(AZ::EntityId id)
    {
        SetParentT2(id, m_isStatic);
    }

    void Transform2Component::SetParentT2(AZ::EntityId parentId, bool keepWorld)
    {
        if (GetEntity() && parentId == GetEntityId())
        {
            AZ_Warning("Transform2Component", false, "An entity can not be set as its own parent.");
            return;
        }

#ifndef AZ_RELEASE_BUILD
        // Refused before anything connects. ProcessParentT2 can run inside the connect policy of the EntityBus below, and a
        // disconnect from in there frees the handler node that the policy goes on to use.
        for (AZ::EntityId ancestorId = parentId; ancestorId.IsValid();)
        {
            if (ancestorId == GetEntityId())
            {
                AZ_Error("Transform2Component", false, "Trying to create a circular dependency of parenting. Aborting set parent call.");
                parentId = AZ::EntityId();
                break;
            }

            auto handler = AZ::TransformBus::FindFirstHandler(ancestorId);
            ancestorId = handler ? handler->GetParentId() : AZ::EntityId();
        }
#endif

        const AZ::EntityId oldParent = m_parentId;
        if (oldParent.IsValid())
        {
            AZ::TransformNotificationBus::Handler::BusDisconnect();
            m_childLink.BusDisconnect();
            AZ::EntityBus::MultiHandler::BusDisconnect(oldParent);
            m_parentActive = false;
        }
        m_parentTM = nullptr;
        m_parent2 = nullptr;

        m_parentId = parentId;
        if (m_parentId.IsValid())
        {
            AZ::ComponentApplicationRequests* componentApplication = AZ::Interface<AZ::ComponentApplicationRequests>::Get();
            AZ::Entity* parentEntity = (componentApplication != nullptr) ? componentApplication->FindEntity(m_parentId) : nullptr;
            m_parentActive = parentEntity && (parentEntity->GetState() == AZ::Entity::State::Active);

            m_onNewParentKeepWorldTM = keepWorld;

            AZ::TransformNotificationBus::Handler::BusConnect(m_parentId);
            m_childLink.BusConnect(m_parentId);
            // Parent (De)Activate handles local entity parent state flag changes.
            AZ::EntityBus::MultiHandler::BusConnect(m_parentId);

            // The connect policy has processed the parent when it exists. If it did not, try now, and if the parent is
            // still not there, the view stands until OnEntityExists of the parent comes.
            if (!m_parentTM)
            {
                ProcessParentT2(m_parentId);
            }
            if (!m_parentTM)
            {
                SeedChainFromView();
            }
        }
        else
        {
            // Transform 1 fires its events here whether or not the view changed. For a root this is also its event at the first
            // world, which a component that connects in Init waits for (see the comment in TransformComponent::Init).
            if (keepWorld)
            {
                AdoptView(m_worldTM, FireViewEvent);
            }
            else
            {
                Refresh(FireViewEvent);
            }
        }

        AZ::TransformNotificationBus::Event(m_notificationBus, &AZ::TransformNotificationBus::Events::OnParentChanged, oldParent, parentId);
        m_parentChangedEvent.Signal(oldParent, parentId);

        if (oldParent.IsValid() && oldParent != parentId) // Don't send removal notification while activating.
        {
            AZ::TransformNotificationBus::Event(oldParent, &AZ::TransformNotificationBus::Events::OnChildRemoved, GetEntityId());
            auto oldParentTransform = AZ::TransformBus::FindFirstHandler(oldParent);
            if (oldParentTransform)
            {
                oldParentTransform->NotifyChildChangedEvent(AZ::ChildChangeType::Removed, GetEntityId());
            }
        }

        AZ::TransformNotificationBus::Event(parentId, &AZ::TransformNotificationBus::Events::OnChildAdded, GetEntityId());
        auto newParentTransform = AZ::TransformBus::FindFirstHandler(parentId);
        if (newParentTransform)
        {
            newParentTransform->NotifyChildChangedEvent(AZ::ChildChangeType::Added, GetEntityId());
        }
    }

    void Transform2Component::ProcessParentT2(const AZ::EntityId& parentEntityId)
    {
        AZ_Assert(parentEntityId == m_parentId, "We expect to receive notifications only from the current parent!");

#ifndef AZ_RELEASE_BUILD
        // The following is an error check for cyclic dependencies and can be omitted in shipping builds
        // Put variables in such a check in their own scope to avoid this kind of code leaking out of scope
        {
            AZ::EntityId parentId = m_parentId;

            while (parentId.IsValid())
            {
                if (parentId == GetEntityId())
                {
                    AZ_Error("Transform2Component", false, "Trying to create a circular dependency of parenting. Aborting set parent call.");
                    SetParent(AZ::EntityId());
                    return;
                }

                auto handler = AZ::TransformBus::FindFirstHandler(parentId);

                if (handler == nullptr)
                {
                    break;
                }

                parentId = handler->GetParentId();
            }
        }
#endif

        // If the parent entity exists to componentApplication, then this transform is ready to process runtime data and state.
        // Otherwise, EntityId references set earlier are good enough.
        AZ::ComponentApplicationRequests* componentApplication = AZ::Interface<AZ::ComponentApplicationRequests>::Get();
        AZ::Entity* parentEntity = (componentApplication != nullptr) ? componentApplication->FindEntity(parentEntityId) : nullptr;
        AZ::TransformInterface* parentTM = parentEntity ? parentEntity->GetTransform() : nullptr;
        if (!parentTM)
        {
            // The parent entity exists but its transform has not connected yet. OnEntityExists of the parent comes back here.
            return;
        }

        m_parentTM = parentTM;
        m_parent2 = azrtti_cast<Transform2Component*>(parentTM);

        AZ_Warning("Transform2Component", !m_isStatic || m_parentTM->IsStaticTransform(),
            "Entity '%s' %s has static transform, but parent has non-static transform. This may lead to unexpected movement.",
            GetEntity() ? GetEntity()->GetName().c_str() : "", GetEntityId().ToString().c_str());

        // Spawn and builder code writes to a Transform 2 that has no entity yet.
        if (m_entity)
        {
            m_entity->SetEffectiveActiveLayerByTypeIndex(GetParentActiveIndex(), parentEntity->IsEffectivelyActive());
            if (m_entity->GetState() == AZ::Entity::State::Init || m_entity->GetState() == AZ::Entity::State::Active)
            {
                m_entity->ApplyEffectiveActiveState();
            }
        }

        // ComputeLocalTM and ComputeWorldTM of Transform 1 fire their events whether or not the view changed. A reparent that
        // keeps the world changes the authored local and not the view, and a handler that caches the local needs the event.
        if (m_onNewParentKeepWorldTM)
        {
            AdoptView(m_worldTM, FireViewEvent);
        }
        else
        {
            Refresh(FireViewEvent);
        }
    }

    void Transform2Component::OnEntityExists(const AZ::EntityId& entityId)
    {
        if (entityId == m_parentId)
        {
            // My Parent popped into existence, OR, we called BusConnect(...parent...) since it has a connection policy that auto-calls it.
            ProcessParentT2(entityId);
        }
        else if (entityId == GetEntityId())
        {
            // This is self.PostInit, the first world. The rest of the components on the same entity are now initialized.
            const bool keepWorldTm = (m_parentActivationTransformMode == ParentActivationTransformMode::MaintainCurrentWorldTransform || !m_parentId.IsValid());
            SetParentT2(m_parentId, keepWorldTm);
        }
    }

    void Transform2Component::OnEntityDestruction(const AZ::EntityId& entityId)
    {
        if (entityId == m_parentId)
        {
            // The parent entity is destroyed but this one is not. As in Transform 1, unparent and keep the view the same,
            // so that entities don't mysteriously teleport to the origin when their parent is deleted.

            // Catch if the destruction is a false fire from runtime start.
            if (m_entity->GetState() != AZ::Entity::State::Init && m_entity->GetState() != AZ::Entity::State::Active)
            {
                return;
            }

            m_parentTM = nullptr;
            m_parent2 = nullptr;
            m_parentActive = false;
            SetParentT2(AZ::EntityId(), true);

            // The "null" entity is never inactive, so make sure that we update our active state based on this in case we
            // were inactive before.
            m_entity->SetEffectiveActiveLayerByTypeIndex(GetParentActiveIndex(), true);
            if (m_entity->GetState() == AZ::Entity::State::Init || m_entity->GetState() == AZ::Entity::State::Active)
            {
                m_entity->ApplyEffectiveActiveState();
            }
        }
        else
        {
            // This entity is destroyed: the notification of the parent is Transform 1's.
            TransformComponent::OnEntityDestruction(entityId);
        }
    }

    void Transform2Component::OnTransformChanged([[maybe_unused]] const AZ::Transform& parentLocalTM, [[maybe_unused]] const AZ::Transform& parentWorldTM)
    {
        // The arguments are the Transform 1 view of the parent. What a child needs is the parent state, which is read.
        FollowParent();
    }

    void Transform2Component::RefreshFromParent()
    {
        FollowParent();
    }

    // Called when the parent moved or flipped. Ignored until the parent state has been derived, as in Transform 1.
    void Transform2Component::FollowParent()
    {
        if (!m_parentTM)
        {
            return;
        }

        if (m_onParentChangedBehavior == AZ::OnParentChangedBehavior::Update)
        {
            Refresh();
        }
        else
        {
            // Keep the Transform 1 view and re-solve the authored values against the parent. The signs still come from the parent.
            AdoptView(m_worldTM, NoFlags);
        }
    }

    // Transform 1 tells the parent about a child that has no Transform 2: it composes with the Transform 1 view of the parent.
    // Warn once per parent.
    void Transform2Component::NotifyChildChangedEvent(AZ::ChildChangeType changeType, AZ::EntityId entityId)
    {
        TransformComponent::NotifyChildChangedEvent(changeType, entityId);

        if (changeType != AZ::ChildChangeType::Added || m_warnedAboutTransform1Child || AZ::Transform2RequestBus::HasHandlers(entityId))
        {
            return;
        }

        // A child that is not initialized yet may be a Transform 2 that has not connected. It adds itself again at its Init.
        AZ::ComponentApplicationRequests* componentApplication = AZ::Interface<AZ::ComponentApplicationRequests>::Get();
        const AZ::Entity* child = (componentApplication != nullptr) ? componentApplication->FindEntity(entityId) : nullptr;
        if (child)
        {
            m_warnedAboutTransform1Child = true;
            AZ_Warning("Transform2Component", false,
                "Entity '%s' is a Transform 1 under the Transform 2 parent '%s'. It sees only the best fit of its parent and does not "
                "follow its flip. To follow, it has to be a Transform 2 when it spawns: \"Deploy as Extended\" on it, or a Transform 2 "
                "parent in its prefab, and no Non-uniform Scale component on it.",
                child->GetName().c_str(), GetEntity() ? GetEntity()->GetName().c_str() : "");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Recompute

    // The parent state is read from the direct parent only. Nothing walks the chain above it.
    ChainState Transform2Component::GetParentChain() const
    {
        if (m_parent2)
        {
            return m_parent2->m_chain;
        }
        if (m_parentTM)
        {
            return AZ::Transform2::ChainFromTransform(m_parentTM->GetWorldTM());
        }
        return ChainState();
    }

    // The chain of an entity that has no parent to read yet: its Transform 1 view, with its own signs and axis ratios on top.
    void Transform2Component::SeedChainFromView()
    {
        const float standIn = AZ::Transform2::UniformStandIn(m_localScale);
        const float viewFloat = m_worldTM.GetUniformScale();

        AZ::Vector3 scale = m_localScale;
        if (HasFloatChanged(viewFloat, standIn))
        {
            scale *= viewFloat / standIn;
        }
        m_chain = AZ::Transform2::ComposeChain(ChainState(), m_worldTM.GetTranslation(), m_worldTM.GetRotation(), scale);
    }

    // The authored values to the chain, and the chain to the Transform 1 view. Nothing is published.
    void Transform2Component::Recompute()
    {
        m_isViewStored = false;
        m_localTM.SetUniformScale(AZ::Transform2::UniformStandIn(m_localScale));

        m_chain = AZ::Transform2::ComposeChain(GetParentChain(), m_localTM.GetTranslation(), m_localTM.GetRotation(), m_localScale);
        m_worldTM = AZ::Transform2::BestFit(m_chain);
    }

    Transform2Component::Before Transform2Component::Capture() const
    {
        return Before{ m_worldTM, m_chain };
    }

    // The authored values are the same, the parent state is not: recompute and tell what changed.
    void Transform2Component::Refresh(int flags)
    {
        const Before before = Capture();
        const bool wasStored = m_isViewStored;
        Recompute();

        // A view stored as a request (a character controller wrote it) is derived again from the authored values, which were
        // solved against the parent as it was. A parent that did not change gives the request back to within rounding, and that
        // is no change: the stored view stays, so a no-op write of the parent does not reach this entity.
        if (wasStored && IsStoredChain(before.m_chain, m_chain, GetParentChain().m_world.GetTranslation()))
        {
            m_worldTM = before.m_view;
            m_chain = before.m_chain;
            m_isViewStored = true;
        }
        Publish(before, flags);
    }

    // Keeps the Transform 1 view as given (position, rotation, magnitude) and solves the authored translation and rotation
    // against the current parent. The own signs are not rewritten, so the entity takes the flip of the parent it has now.
    void Transform2Component::AdoptView(const AZ::Transform view, int flags)
    {
        const Before before = Capture();
        const ChainState parent = GetParentChain();

        if (m_parentTM)
        {
            m_localTM.SetTranslation(AZ::Transform2::SolveLocalTranslation(parent, view.GetTranslation()));
            m_localTM.SetRotation(AZ::Transform2::SolveLocalRotation(parent, view.GetRotation()));
        }
        else
        {
            m_localTM.SetTranslation(view.GetTranslation());
            m_localTM.SetRotation(view.GetRotation());
        }

        FoldToWorldFloat(view.GetUniformScale());
        Recompute();
        StoreRequest(view);

        // The values solved against a parent that moved give the chain this entity had to within rounding (a DoNotUpdate child
        // under a rotating parent). That is no change: the chain and the view stay, so nothing reaches the children.
        if (IsStoredChain(before.m_chain, m_chain, parent.m_world.GetTranslation()))
        {
            m_worldTM = before.m_view;
            m_chain = before.m_chain;
        }
        Publish(before, flags | ExactRequest);
    }

    // The view is stored as asked: translation and rotation exactly, the float too when it is the one the chain reached.
    // A writer that feeds what it reads back into the transform each tick then reads back what it wrote.
    void Transform2Component::StoreRequest(const AZ::Transform& request)
    {
        m_worldTM.SetTranslation(request.GetTranslation());
        m_worldTM.SetRotation(request.GetRotation());
        if (!HasFloatChanged(m_worldTM.GetUniformScale(), request.GetUniformScale()))
        {
            m_worldTM.SetUniformScale(request.GetUniformScale());
        }

        m_chain.m_world.SetTranslation(request.GetTranslation());
        m_chain.m_rotation = request.GetRotation();
        m_isViewStored = true;
    }

    // Folds a Transform 1 float into the own scale by ratio, so the signs and axis ratios survive.
    // Zero and negative become the smallest scale. A vector with no magnitude has no signs or ratios to keep, and
    // multiplying it grows nothing, so the size is the value.
    void Transform2Component::FoldUniformScale(float requested)
    {
        if (!AZ::IsFiniteFloat(requested) || !HasFloatChanged(requested, AZ::Transform2::UniformStandIn(m_localScale)))
        {
            return;
        }

        const float held = AZ::GetClamp(requested, AZ::MinTransformScale, AZ::MaxTransformScale);
        if (m_localScale.GetAbs().GetMaxElement() == 0.0f)
        {
            m_localScale = AZ::Vector3(held);
            return;
        }

        m_localScale *= held / AZ::Transform2::UniformStandIn(m_localScale);
    }

    // Folds the own scale so that the Transform 1 float of the world, with the authored values as they are now, is worldFloat.
    void Transform2Component::FoldToWorldFloat(float worldFloat)
    {
        const ChainState composed = AZ::Transform2::ComposeChain(GetParentChain(), m_localTM.GetTranslation(), m_localTM.GetRotation(), m_localScale);
        const float current = AZ::Transform2::UniformStandIn(composed.m_worldScale);
        if (HasFloatChanged(current, worldFloat))
        {
            FoldUniformScale(AZ::Transform2::UniformStandIn(m_localScale) * (worldFloat / current));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Publish
    //
    // The order per entity is the one the listeners depend on, and all the data of this entity is final before anything fires:
    // 1. The Transform 1 events, if the view changed (or a write asked for them). Children that are Transform 2 recompute
    //    inside this, so none sees a stale parent.
    // 2. The non-uniform scale changed event, if the residual of the world scale changed.
    // 3. While active, OnTransform2WorldChanged and then OnTransform2ScaleChanged.
    // 4. If the exact world changed and the view did not (a pure flip), a refresh of each child. No listener of this entity
    //    gets a Transform 1 event from a flip, so a dynamic body is not teleported by one.

    void Transform2Component::Publish(const Before& before, int flags)
    {
        const bool viewChanged = (flags & FireViewEvent) != 0 || m_worldTM != before.m_view;
        const bool exactChanged = m_chain.m_world != before.m_chain.m_world;
        // Spawn and builder code writes before the entity exists, and GetEntityId warns about a component with no entity.
        const AZ::EntityId entityId = GetEntity() ? GetEntityId() : AZ::EntityId();

        if (viewChanged)
        {
            AZ::TransformNotificationBus::Event(m_notificationBus, &AZ::TransformNotificationBus::Events::OnTransformChanged, m_localTM, m_worldTM);
            m_transformChangedEvent.Signal(m_localTM, m_worldTM);
            UpdateBoundsUnion();
        }

        if (AZ::Transform2::IsNonUniformEffective(m_chain.m_worldScale))
        {
            ConnectResidualBus();
        }
        // The scale and the residual are compared with what was last announced, not with the state before this change.
        // Steps under the tolerance then add up until they announce, and a listener that caches never falls behind.
        // The residual event only comes with a scale change: a mesh overwrites its signed scale with the positive residual on
        // the event, and it is the OnTransform2ScaleChanged below that puts the flip back.
        const bool scaleChanged = AZ::Transform2::HasWorldScaleChanged(m_announcedWorldScale, m_chain.m_worldScale);
        const AZ::Vector3 residual = AZ::Transform2::Residual(m_chain.m_worldScale);
        if (scaleChanged && !residual.IsClose(m_announcedResidual, ResidualChangedTolerance))
        {
            m_announcedResidual = residual;
            m_residualChangedEvent.Signal(residual);
        }

        if (m_isActive)
        {
            if (viewChanged || (exactChanged && m_chain.m_sheared))
            {
                AZ::Transform2NotificationBus::Broadcast(
                    &AZ::Transform2Notifications::OnTransform2WorldChanged, entityId, m_worldTM, m_chain.m_worldScale);
            }
            if (scaleChanged)
            {
                m_announcedWorldScale = m_chain.m_worldScale;
                AZ::Transform2NotificationBus::Broadcast(
                    &AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, m_chain.m_worldScale);
            }
        }

        if (exactChanged && !viewChanged && GetEntity())
        {
            // A T1 child has no handler and needs nothing: the best fit of its parent did not move.
            for (const AZ::EntityId& child : GetChildren())
            {
                AZ::Transform2RequestBus::Event(child, &AZ::Transform2Requests::RefreshFromParent);
            }
            UpdateBoundsUnion();
        }

        CheckInvariants((flags & ExactRequest) != 0);
    }

    void Transform2Component::UpdateBoundsUnion()
    {
        AzFramework::IEntityBoundsUnion* boundsUnion = AZ::Interface<AzFramework::IEntityBoundsUnion>::Get();
        if (boundsUnion != nullptr && GetEntity())
        {
            boundsUnion->OnTransformUpdated(GetEntity());
        }
    }

    void Transform2Component::ConnectResidualBus()
    {
        // The bus has a single handler per entity. If another component already owns it, leave it alone.
        if (GetEntity() && !AZ::NonUniformScaleRequestBus::Handler::BusIsConnected() && !AZ::NonUniformScaleRequestBus::HasHandlers(GetEntityId()))
        {
            AZ::NonUniformScaleRequestBus::Handler::BusConnect(GetEntityId());
        }
    }

    // Debug builds only. A Transform 1 helper that ran on a Transform 2 breaks one of these, and no test would say which.
    void Transform2Component::CheckInvariants([[maybe_unused]] bool skipComposition) const
    {
#if defined(AZ_DEBUG_BUILD)
        AZ_Assert(m_localTM.GetUniformScale() == AZ::Transform2::UniformStandIn(m_localScale),
            "Transform2Component: the float of the local transform is not the stand-in of the local scale.");
        AZ_Assert(m_worldTM.GetTranslation() == m_chain.m_world.GetTranslation(),
            "Transform2Component: the translation of the view is not the translation of the exact world.");

        // A child that keeps its view, a request stored as asked, and a parent that is not there yet do not follow the sources.
        const bool waitingForParent = m_parentId.IsValid() && !m_parentTM;
        if (!skipComposition && !waitingForParent && m_onParentChangedBehavior == AZ::OnParentChangedBehavior::Update)
        {
            const ChainState expected = AZ::Transform2::ComposeChain(GetParentChain(), m_localTM.GetTranslation(), m_localTM.GetRotation(), m_localScale);
            AZ_Assert(IsCloseRelative(m_chain.m_world, expected.m_world, CompositionTolerance),
                "Transform2Component: the exact world is not the parent state composed with the authored values.");
        }
#endif
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes
    //
    // Every write solves the authored values and recomputes. The own scale is rewritten by nothing but a uniform write
    // (by ratio) and SetLocalScale, so the noise of a world write never reaches saved data.

    void Transform2Component::SetLocalTM(const AZ::Transform& tm)
    {
        if (!AreMoveRequestsAllowed())
        {
            return;
        }

        const Before before = Capture();

        m_localTM.SetTranslation(tm.GetTranslation());
        m_localTM.SetRotation(tm.GetRotation());
        FoldUniformScale(tm.GetUniformScale());

        Recompute();
        Publish(before, FireViewEvent);
    }

    void Transform2Component::SetWorldTM(const AZ::Transform& tm)
    {
        if (!AreMoveRequestsAllowed())
        {
            return;
        }

        // The request can be the view of this very transform (SetWorldTM(GetWorldTM())), which Recompute replaces.
        const AZ::Transform request = tm;
        const Before before = Capture();
        const bool hasParent = m_parentTM != nullptr;
        const ChainState parent = GetParentChain();

        // Each part is compared exactly: a tolerance would stall a slow rotation.
        if (request.GetTranslation() != m_worldTM.GetTranslation())
        {
            m_localTM.SetTranslation(hasParent ? AZ::Transform2::SolveLocalTranslation(parent, request.GetTranslation()) : request.GetTranslation());
        }
        if (request.GetRotation() != m_worldTM.GetRotation())
        {
            m_localTM.SetRotation(hasParent ? AZ::Transform2::SolveLocalRotation(parent, request.GetRotation()) : request.GetRotation());
        }
        if (HasFloatChanged(request.GetUniformScale(), m_worldTM.GetUniformScale()))
        {
            const float current = m_worldTM.GetUniformScale();
            FoldUniformScale(current > 0.0f ? AZ::Transform2::UniformStandIn(m_localScale) * (request.GetUniformScale() / current) : request.GetUniformScale());
        }

        Recompute();
        StoreRequest(request);
        Publish(before, FireViewEvent | ExactRequest);
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 requests

    AZ::Vector3 Transform2Component::GetLocalScale() const
    {
        return m_localScale;
    }

    // Limits:
    // - A magnitude change at runtime reaches the draw, but not the colliders, which keep the size they were built at
    //   (upstream issue #2263).
    void Transform2Component::SetLocalScale(const AZ::Vector3& scale)
    {
        if (!scale.IsFinite())
        {
            AZ_Warning("Transform2Component", false, "SetLocalScale ignored a non-finite scale for entity %s", GetEntity() ? GetEntity()->GetName().c_str() : "");
            return;
        }

        if (scale == m_localScale)
        {
            return;
        }

        const Before before = Capture();
        const float oldStandIn = AZ::Transform2::UniformStandIn(m_localScale);

        m_localScale = scale;

        // A static entity refuses a change of its size, as Transform 1 refuses the write. The signs and ratios land and the
        // size stays what it was held at. A zero vector takes that size.
        if (!AreMoveRequestsAllowed())
        {
            FoldUniformScale(oldStandIn);
        }

        Recompute();
        Publish(before, NoFlags);
    }

    AZ::Vector3 Transform2Component::GetWorldScale() const
    {
        return m_chain.m_worldScale;
    }

    AZ::Matrix3x4 Transform2Component::GetWorldMatrix() const
    {
        return m_chain.m_world;
    }

    bool Transform2Component::IsWorldSheared() const
    {
        return m_chain.m_sheared;
    }

    //////////////////////////////////////////////////////////////////////////
    // Non-uniform scale requests: the positive residual on top of the Transform 1 float of the world

    AZ::Vector3 Transform2Component::GetScale() const
    {
        return AZ::Transform2::Residual(m_chain.m_worldScale);
    }

    void Transform2Component::SetScale(const AZ::Vector3& scale)
    {
        // The residual multiplies the Transform 1 float of the world. The signs stay, and each axis of the own scale takes
        // the factor that its world axis needs. An axis with no world magnitude has no factor to take, and takes the size.
        const AZ::Vector3 worldMagnitude = m_chain.m_worldScale.GetAbs();
        const AZ::Vector3 target = AZ::Transform2::UniformStandIn(m_chain.m_worldScale) * scale.GetAbs();
        const AZ::Vector3 signs = SignOf(m_localScale);

        AZ::Vector3 newScale = m_localScale;
        for (int32_t axis = 0; axis < 3; ++axis)
        {
            const float world = worldMagnitude.GetElement(axis);
            newScale.SetElement(axis, world > 0.0f
                ? m_localScale.GetElement(axis) * (target.GetElement(axis) / world)
                : signs.GetElement(axis) * target.GetElement(axis));
        }
        SetLocalScale(newScale);
    }

    void Transform2Component::RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler& handler)
    {
        handler.Connect(m_residualChangedEvent);
    }

    //////////////////////////////////////////////////////////////////////////
    // Script entry points

    AZ::Vector3 Transform2Component::GetLocalScaleChecked(AZ::EntityId entityId)
    {
        if (!RequireTransform2(entityId, "read"))
        {
            return AZ::Vector3::CreateOne();
        }

        AZ::Vector3 scale = AZ::Vector3::CreateOne();
        AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetLocalScale);
        return scale;
    }

    void Transform2Component::SetLocalScaleChecked(AZ::EntityId entityId, const AZ::Vector3& scale)
    {
        if (!RequireTransform2(entityId, "set"))
        {
            return;
        }

        AZ::Transform2RequestBus::Event(entityId, &AZ::Transform2Requests::SetLocalScale, scale);
    }

    AZ::Vector3 Transform2Component::GetWorldScaleChecked(AZ::EntityId entityId)
    {
        if (!RequireTransform2(entityId, "read"))
        {
            return AZ::Vector3::CreateOne();
        }

        AZ::Vector3 scale = AZ::Vector3::CreateOne();
        AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetWorldScale);
        return scale;
    }

    AZ::Matrix3x4 Transform2Component::GetWorldMatrixChecked(AZ::EntityId entityId)
    {
        if (!RequireTransform2(entityId, "read"))
        {
            return AZ::Matrix3x4::CreateIdentity();
        }

        AZ::Matrix3x4 matrix = AZ::Matrix3x4::CreateIdentity();
        AZ::Transform2RequestBus::EventResult(matrix, entityId, &AZ::Transform2Requests::GetWorldMatrix);
        return matrix;
    }

    bool Transform2Component::IsExtended(AZ::EntityId entityId)
    {
        return AZ::Transform2RequestBus::HasHandlers(entityId);
    }

    void Transform2Component::FlipXChecked(AZ::EntityId entityId)
    {
        FlipChecked(entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));
    }

    void Transform2Component::FlipYChecked(AZ::EntityId entityId)
    {
        FlipChecked(entityId, AZ::Vector3(1.0f, -1.0f, 1.0f));
    }

    void Transform2Component::FlipZChecked(AZ::EntityId entityId)
    {
        FlipChecked(entityId, AZ::Vector3(1.0f, 1.0f, -1.0f));
    }

    void Transform2Component::FlipChecked(AZ::EntityId entityId, const AZ::Vector3& flip)
    {
        if (!RequireTransform2(entityId, "flipped"))
        {
            return;
        }

        AZ::Vector3 scale = AZ::Vector3::CreateOne();
        AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetLocalScale);
        AZ::Transform2RequestBus::Event(entityId, &AZ::Transform2Requests::SetLocalScale, scale * flip);
    }
} // namespace AzFramework
