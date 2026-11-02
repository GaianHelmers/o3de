/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once
#include <AzCore/Component/Component.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/Transform2Math.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Vector3.h>
#include <AzFramework/AzFrameworkAPI.h>
#include <AzFramework/Components/TransformComponent.h>

namespace AzFramework
{
    //! The runtime transform of an entity whose scale does not fit Transform 1, and of every descendant of one.
    //! It replaces AzFramework::TransformComponent on those entities, and derives from it to reuse its data, buses, events,
    //! configuration, getters, read-modify-write setters and activation layer. Every virtual that reaches the arithmetic of
    //! Transform 1 is overridden here, so no arithmetic of Transform 1 ever runs on a Transform 2.
    //!
    //! What it stores:
    //! - m_localTM (Transform 1): the authored translation, the authored rotation, and the stand-in float of the own scale.
    //! - m_localScale (saved as "LocalScale"): the own signed scale. It is changed by a uniform write (by ratio) and SetLocalScale only.
    //! - m_worldTM (Transform 1): the Transform 1 view, the best fit of the chain. It is never negative.
    //! - m_chain: what this transform hands to its children, read by them from this transform only (see Transform2Math.h).
    //!
    //! Local calls on the TransformBus are authored values, world calls are the Transform 1 view.
    class AZF_API Transform2Component
        : public TransformComponent
        , public AZ::Transform2RequestBus::Handler
        , public AZ::NonUniformScaleRequestBus::Handler
    {
    public:
        AZ_COMPONENT(Transform2Component, "{CB58695A-74A8-49BD-BD91-F975736DF113}", TransformComponent);

        static void Reflect(AZ::ReflectContext* context);

        Transform2Component();
        explicit Transform2Component(const AZ::Vector3& localScale);
        ~Transform2Component() override;

        // AZ::Transform2Requests::Handler ...
        // The Transform 1 name GetLocalScale() is a different function: inside this class and its tests, call through the buses.
        AZ::Vector3 GetLocalScale() const override;
        void SetLocalScale(const AZ::Vector3& scale) override;
        AZ::Vector3 GetWorldScale() const override;
        AZ::Matrix3x4 GetWorldMatrix() const override;
        bool IsWorldSheared() const override;
        void RefreshFromParent() override;

        // AZ::NonUniformScaleRequests::Handler ...
        AZ::Vector3 GetScale() const override;
        void SetScale(const AZ::Vector3& scale) override;
        void RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler& handler) override;

        // AZ::TransformBus, the entry points that reach arithmetic
        void NotifyChildChangedEvent(AZ::ChildChangeType changeType, AZ::EntityId entityId) override;
        void SetLocalTM(const AZ::Transform& tm) override;
        void SetWorldTM(const AZ::Transform& tm) override;
        void SetParent(AZ::EntityId id) override;
        void SetParentRelative(AZ::EntityId id) override;

        //! Script entry points. They report a clear error when the entity has no Transform 2,
        //! where a plain bus call would silently do nothing. IsExtended is the question a script asks
        //! to avoid that error, so it reports none. An entity is extended from the Init of this component until the component
        //! is destroyed, active or not, because it stays on the request bus for that long.
        //! The local methods read and write the own scale of the entity. The world methods read what its whole chain makes of it.
        //! @{
        static AZ::Vector3 GetLocalScaleChecked(AZ::EntityId entityId);
        static void SetLocalScaleChecked(AZ::EntityId entityId, const AZ::Vector3& scale);
        static AZ::Vector3 GetWorldScaleChecked(AZ::EntityId entityId);
        static AZ::Matrix3x4 GetWorldMatrixChecked(AZ::EntityId entityId);
        static bool IsExtended(AZ::EntityId entityId);
        static void FlipXChecked(AZ::EntityId entityId);
        static void FlipYChecked(AZ::EntityId entityId);
        static void FlipZChecked(AZ::EntityId entityId);
        //! @}

    protected:
        // Brings the deprecated Transform 1 GetLocalScale() back into this scope, beside the const one above.
        using TransformComponent::GetLocalScale;

        // AZ::Component ...
        void Init() override;
        void Activate() override;
        void Deactivate() override;
        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override;

        // AZ::TransformNotificationBus, for the parent
        void OnTransformChanged(const AZ::Transform& parentLocalTM, const AZ::Transform& parentWorldTM) override;

        // AZ::EntityBus
        void OnEntityExists(const AZ::EntityId& entityId) override;
        void OnEntityDestruction(const AZ::EntityId& entityId) override;

    private:
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        //! Answers the children query of the parent this transform is attached to.
        //! The handler Transform 1 uses for it is a private base, so a Transform 2 owns one of its own.
        class ChildLink final
            : public AZ::TransformHierarchyInformationBus::Handler
        {
        public:
            void Attach(const AZ::Component& owner)
            {
                m_owner = &owner;
            }

            void GatherChildren(AZStd::vector<AZ::EntityId>& children) override
            {
                children.push_back(m_owner->GetEntityId());
            }

        private:
            const AZ::Component* m_owner = nullptr;
        };

        //! What Publish compares against: the state before a change.
        struct Before
        {
            AZ::Transform m_view;
            AZ::Transform2::ChainState m_chain;
        };

        enum PublishFlags
        {
            NoFlags = 0,
            FireViewEvent = 1,  //!< Fire the Transform 1 events even if the view did not change, as Transform 1 does on a write.
            ExactRequest = 2,   //!< The view was stored as asked, so it can differ from the chain by rounding.
        };

        //! Parent link
        //! @{
        void SetParentT2(AZ::EntityId parentId, bool keepWorld);
        void ProcessParentT2(const AZ::EntityId& parentEntityId);
        void FollowParent();
        //! @}

        //! Recompute
        //! @{
        AZ::Transform2::ChainState GetParentChain() const;
        void SeedChainFromView();
        void Recompute();
        void Refresh(int flags = NoFlags);
        void AdoptView(const AZ::Transform view, int flags);
        void StoreRequest(const AZ::Transform& request);
        void FoldUniformScale(float requested);
        void FoldToWorldFloat(float worldFloat);
        Before Capture() const;
        //! @}

        //! Publish
        //! @{
        void Publish(const Before& before, int flags);
        void UpdateBoundsUnion();
        void ConnectResidualBus();
        void CheckInvariants(bool skipComposition) const;
        //! @}

        //! Multiplies the current scale by flip (a sign per axis) for the checked script entry points.
        static void FlipChecked(AZ::EntityId entityId, const AZ::Vector3& flip);

        AZ::Vector3 m_localScale = AZ::Vector3::CreateOne(); //!< The own signed per-axis scale. Saved.
        AZ::Transform2::ChainState m_chain; //!< What this transform hands to its children. Runtime only.
        Transform2Component* m_parent2 = nullptr; //!< The parent when it is a Transform 2, else null.
        ChildLink m_childLink;
        AZ::NonUniformScaleChangedEvent m_residualChangedEvent; //!< Signaled when the residual of the world scale changes.
        AZ::Vector3 m_announcedWorldScale = AZ::Vector3::CreateOne(); //!< The world scale that Transform2Notifications last carried.
        AZ::Vector3 m_announcedResidual = AZ::Vector3::CreateOne(); //!< The residual that m_residualChangedEvent last carried.
        bool m_isActive = false;
        bool m_isViewStored = false; //!< The view is the one StoreRequest stored, and no recompute has replaced it since.
        bool m_warnedAboutTransform1Child = false;
    };
} // namespace AzFramework
