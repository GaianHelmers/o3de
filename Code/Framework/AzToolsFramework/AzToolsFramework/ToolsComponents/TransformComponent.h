/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/AzToolsFrameworkAPI.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/Transform2Math.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Slice/SliceBus.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <AzToolsFramework/API/ComponentEntitySelectionBus.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Commands/SelectionCommand.h>
#include <AzToolsFramework/ToolsComponents/EditorNonUniformScaleComponent.h>
#include <AzToolsFramework/FocusMode/FocusModeInterface.h>

#include "EditorComponentBase.h"
#include "TransformComponentBus.h"

namespace AzToolsFramework
{
    namespace Components
    {
        /// Manages transform data as separate vector fields for editing purposes.
        /// The TransformComponent is referenced by other components in the same entity, it is not an asset.
        class AZTF_API TransformComponent
            : public EditorComponentBase
            , public AZ::TransformBus::Handler
            , public AZ::SliceEntityHierarchyRequestBus::Handler
            , private TransformComponentMessages::Bus::Handler
            , private AZ::EntityBus::Handler
            , private AZ::TransformNotificationBus::MultiHandler
            , private AZ::TransformHierarchyInformationBus::Handler
            // Transform 2: appended after every upstream base, so the upstream bases keep their sub-object positions.
            , private AZ::Transform2RequestBus::Handler
            , private AZ::NonUniformScaleRequestBus::Handler
        {
            friend class JsonTransformComponentSerializer;

        public:
            friend class TransformComponentFactory;

            AZ_EDITOR_COMPONENT(TransformComponent, AZ::EditorTransformComponentTypeId, EditorComponentBase, AZ::SliceEntityHierarchyInterface, AZ::TransformInterface)

            TransformComponent();
            virtual ~TransformComponent();

            // AZ::Component
            void Init() override;
            void Activate() override;
            void Deactivate() override;

            static void PasteOverComponent(const TransformComponent* sourceComponent, TransformComponent* destinationComponent);

            //! True while a transform is cycling the activation of this entity because its scale crossed between uniform and
            //! non-uniform. The components do not change then, so the inspector keeps its tree instead of rebuilding it
            //! under the edit that caused the crossing.
            static bool IsReactivatingForTransform2(AZ::EntityId entityId);

            //! True when the entity has a non-uniform scale: the old Non-uniform Scale component, or a transform that serves
            //! Transform 2 whose effective world scale has unequal magnitudes, so an inherited scale counts as well. It reads
            //! the data of the entity and its parents and no bus, so it also answers for an entity that was only initialized.
            //! BuildGameEntity uses it instead of a flag saved by a component, because such a flag is only refreshed when
            //! its component activates.
            static bool HasNonUniformScale(const AZ::Entity& entity);

            //! True when the entity has a transform whose own scale is extended (or Deploy as Extended). It reads the data
            //! alone, so it also answers for an entity that was not initialized yet. The export uses it to decide whether the
            //! entities must be initialized before any is exported, because the parents of an inherited Transform 2 are
            //! found through the entities that are initialized.
            static bool HasExtendedOwnScale(const AZ::Entity& entity);

            //! Export: an editor-only entity is removed and its child takes its place under the parent of the removed entity.
            //! When the removed entity or the child is a Transform 2, the local of the child is composed exactly, so the
            //! flip of the removed entity stays on the child. Returns false, and writes nothing, when neither is a Transform 2.
            //! A shear cannot be written as a translation, rotation and scale, so it is lost, and warnedAboutShear makes the
            //! caller say so once.
            static bool ReparentChildOfRemovedEntity(
                const AZ::Entity& childEntity, AZ::TransformInterface& removedParent, AZ::TransformInterface& child,
                bool& warnedAboutShear);

            //! Queues, for the next tick, the conversion of the old Non-uniform Scale component of an entity into the Scale of
            //! its transform. The button on that component calls it. The conversion removes that component, so it cannot run
            //! inside the notification of the button press. The entity is looked up again when the tick comes, because it may
            //! be gone by then.
            static void QueueLegacyScaleConversion(AZ::EntityId entityId);

            // AZ::EntityBus
            void OnEntityActivated(const AZ::EntityId& parentEntityId) override;
            void OnEntityDeactivated(const AZ::EntityId& parentEntityId) override;

            // AZ::TransformBus
            void BindTransformChangedEventHandler(AZ::TransformChangedEvent::Handler& handler) override;
            void BindParentChangedEventHandler(AZ::ParentChangedEvent::Handler& handler) override;
            void BindChildChangedEventHandler(AZ::ChildChangedEvent::Handler& handler) override;
            void NotifyChildChangedEvent(AZ::ChildChangeType changeType, AZ::EntityId entityId) override;
            const AZ::Transform& GetLocalTM() override;
            void SetLocalTM(const AZ::Transform& tm) override;
            const AZ::Transform& GetWorldTM() override;
            void SetWorldTM(const AZ::Transform& tm) override;
            void GetLocalAndWorld(AZ::Transform& localTM, AZ::Transform& worldTM) override;

            // Translation modifiers
            void SetWorldTranslation(const AZ::Vector3& newPosition) override;
            void SetLocalTranslation(const AZ::Vector3& newPosition) override;

            AZ::Vector3 GetWorldTranslation() override;
            AZ::Vector3 GetLocalTranslation() override;

            void MoveEntity(const AZ::Vector3& offset) override;

            void SetWorldX(float newX) override;
            void SetWorldY(float newY) override;
            void SetWorldZ(float newZ) override;

            float GetWorldX() override;
            float GetWorldY() override;
            float GetWorldZ() override;

            void SetLocalX(float x) override;
            void SetLocalY(float y) override;
            void SetLocalZ(float z) override;

            float GetLocalX() override;
            float GetLocalY() override;
            float GetLocalZ() override;

            // Rotation modifiers
            void SetWorldRotationQuaternion(const AZ::Quaternion& quaternion) override;

            AZ::Vector3 GetWorldRotation() override;
            AZ::Quaternion GetWorldRotationQuaternion() override;

            void SetLocalRotation(const AZ::Vector3& eulerAnglesRadian) override;
            void SetLocalRotationQuaternion(const AZ::Quaternion& quaternion) override;

            void RotateAroundLocalX(float eulerAngleRadian) override;
            void RotateAroundLocalY(float eulerAngleRadian) override;
            void RotateAroundLocalZ(float eulerAngleRadian) override;

            AZ::Vector3 GetLocalRotation() override;
            AZ::Quaternion GetLocalRotationQuaternion() override;

            // Scale Modifiers
            AZ::Vector3 GetLocalScale() override;

            void SetLocalUniformScale(float scale) override;
            float GetLocalUniformScale() override;
            float GetWorldUniformScale() override;

            AZ::EntityId  GetParentId() override;
            AZ::TransformInterface* GetParent() override;
            void SetParent(AZ::EntityId parentId) override;
            void SetParentRelative(AZ::EntityId parentId) override;
            AZStd::vector<AZ::EntityId> GetChildren() override;
            AZStd::vector<AZ::EntityId> GetAllDescendants() override;
            AZStd::vector<AZ::EntityId> GetEntityAndAllDescendants() override;
            bool IsStaticTransform() override;
            void SetIsStaticTransform(bool isStatic) override;

            // TransformComponentMessages::Bus
            void TranslateBy(const AZ::Vector3&) override;
            void RotateBy(const AZ::Vector3&) override; // euler in degrees
            const EditorTransform& GetLocalEditorTransform() override;
            void SetLocalEditorTransform(const EditorTransform& dest) override;
            bool IsTransformLocked() override;

            // AZ::Transform2RequestBus, served only while this transform serves Transform 2. The world scale and the world
            // matrix are what the whole chain of parents makes of the entity (see Transform2Math.h).
            AZ::Vector3 GetLocalScale() const override;
            void SetLocalScale(const AZ::Vector3& scale) override;
            AZ::Vector3 GetWorldScale() const override;
            AZ::Matrix3x4 GetWorldMatrix() const override;
            bool IsWorldSheared() const override;
            void RefreshFromParent() override;

            // AZ::NonUniformScaleRequestBus, served only while this transform serves and its effective world scale is
            // non-uniform. It serves the positive residual of that world scale (the per-axis magnitude on top of the uniform
            // scale float) and declares no NonUniformScaleService.
            AZ::Vector3 GetScale() const override;
            void SetScale(const AZ::Vector3& scale) override;
            void RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler& handler) override;

            /// \return true if the entity is a root-level entity (has no transform parent).
            bool IsRootEntity() const { return !m_parentEntityId.IsValid(); }

            //callable is a lambda taking a AZ::EntityId and returning nothing, will be called with the id of each child
            //Also will be called for children of children, all the way down the hierarchy
            template<typename Callable>
            void ForEachChild(Callable callable)
            {
                for (auto childId : m_childrenEntityIds)
                {
                    callable(childId);
                    auto child = GetTransformComponent(childId);
                    if (child)
                    {
                        child->ForEachChild(callable);
                    }
                }
            }

            void BuildGameEntity(AZ::Entity* gameEntity) override;

            void UpdateCachedWorldTransform();
            void ClearCachedWorldTransform();

            // SliceEntityHierarchyRequestBus
            AZ::EntityId GetSliceEntityParentId() override;
            AZStd::vector<AZ::EntityId> GetSliceEntityChildren() override;

        private:
            // AZ::TransformNotificationBus - Connected to parent's ID
            void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override;

            void OnTransformChanged(); //convenience

            // TransformHierarchyInformationBus
            void GatherChildren(AZStd::vector<AZ::EntityId>& children) override;

            void AddContextMenuActions(QMenu* menu) override;

            static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
            static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
            static void Reflect(AZ::ReflectContext* context);

            AZ::u32 TransformChangedInspector();
            AZ::u32 StaticChangedInspector();

            //! The tag shown in the component header, empty unless this transform serves Transform 2: its own scale is
            //! extended, or a parent serves.
            AZStd::string GetHeaderTag() const;

            bool TransformChanged();

            //////////////////////////////////////////////////////////////////////////
            // Extended (Transform 2) state

            //! True when an old Non-uniform Scale component is on the entity.
            bool HasLegacyNonUniformScaleComponent() const;

            //! True when this transform runs the Transform 2 services: its own scale is extended or its parent serves, and no
            //! old Non-uniform Scale component owns the scale of the entity. Answered from the memo, which is read from the
            //! authored data of this transform and of its direct parent only. A Transform 1 chain pays one bool test, and
            //! a change notification reads the memo again only for a transform that serves or has an extended scale: a
            //! parent that changes whether it serves marks the memo of its children dirty itself.
            bool ServesTransform2() const;

            //! Computes the memo when it is dirty: whether this transform serves, and the chain it hands to its children.
            //! When whether it serves changed, the memo of every child is marked dirty.
            void UpdateChainMemo() const;

            //! The parent as the memo reads it: its editor transform, or nothing when it is missing or is being computed right
            //! now, which is a parent loop in bad data.
            TransformComponent* GetChainParent() const;

            //! What the parent hands to this entity: its chain when it serves, the Transform 1 world of a parent that does not,
            //! and the identity when there is no parent.
            AZ::Transform2::ChainState GetParentChain() const;

            //! True when this transform would serve under that parent: its own scale is extended or the parent serves, and no
            //! old Non-uniform Scale component is on the entity.
            bool ServesTransform2Under(AZ::EntityId parentId) const;

            //! The announcements of OnTransformChanged for a transform that serves, in the same order, with its own world.
            void OnTransformChangedExtended();

            //! SetWorldTM for a transform that serves: each part of the request that differs from the world is solved
            //! against the parent, and the own scale changes only by a uniform write.
            void SetWorldTMExtended(const AZ::Transform& tm);

            //! After the parent id changed in a reparent that involves a transform that serves: the memo and the world are read
            //! again. For a world keeping reparent it also solves the authored translation and rotation against the new parent
            //! so the Transform 1 view stays, and folds the own float so its magnitude stays. The own signs are not rewritten.
            //! The scale held before the reparent is restored first, because the Transform 1 lines of the reparent folded it.
            void SettleTransform2AfterReparent(bool keepView, const AZ::Transform& view, const EditorTransform& before);

            //! Brings the Transform 2 services in line with the chain after every transform change: the request bus,
            //! the notifications, and the queued re-activation when the non-uniform state crosses.
            void UpdateTransform2State(const AZ::Transform& world);

            //! Serves the residual on the non-uniform scale bus only while this transform serves and its effective world
            //! scale is non-uniform. Presence of a handler there means "non-uniform" to its consumers. Returns true when
            //! presence changed.
            bool UpdateResidualBus(bool serving);

            //! Leaves the Transform 2 services: raises Deactivated and disconnects the buses.
            void DeactivateTransform2();

            //! Queues one re-activation of the entity for the next tick, so consumers that read bus presence in
            //! their Activate re-evaluate it. The re-activation joins the undo step of the edit that caused the crossing, for the
            //! entity that was edited and for a descendant alike: components save values they derive at activation, and the
            //! next change of any ancestor marks every descendant dirty, so a descendant that was left out of the step would
            //! write those values into its prefab later, in a step of its own.
            void QueueEntityReactivation();

            //! Folds the old Non-uniform Scale component into the Scale of this transform and removes the component, in one
            //! undo step. The scale is written the way the Scale row writes it, so the tag, the save branch and the
            //! Transform 2 services follow. Does nothing when there is no such component, or when the transform is locked.
            void ConvertLegacyNonUniformScale();

            AZ::Transform GetLocalTranslationTM() const;
            AZ::Transform GetLocalRotationTM() const;
            AZ::Transform GetLocalScaleTM() const;

            TransformComponent* GetParentTransformComponent() const;
            TransformComponent* GetTransformComponent(AZ::EntityId otherEntityId) const;
            bool IsEntityInHierarchy(AZ::EntityId entityId);

            void SetParentImpl(AZ::EntityId parentId, bool relative);
            const AZ::Transform& GetParentWorldTM() const;

            void ModifyEditorTransform(AZ::Vector3& vec, const AZ::Vector3& data, const AZ::Transform& parentInverse);

            void CheckApplyCachedWorldTransform(const AZ::Transform& parentWorld);

            // Drives transform behavior when parent activates. See AZ::TransformConfig::ParentActivationTransformMode for details.
            AZ::TransformConfig::ParentActivationTransformMode m_parentActivationTransformMode;

            AZ::TransformChangedEvent m_transformChangedEvent; ///< Event used to signal when a transform changes.
            AZ::ParentChangedEvent    m_parentChangedEvent;    ///< Event used to signal when a transforms parent changes.
            AZ::ChildChangedEvent     m_childChangedEvent;     ///< Event used to signal when a transform has a child entity added or removed.
            AZ::NonUniformScaleChangedEvent m_residualChangedEvent; ///< Event used to signal when the served residual scale changes.

            EditorTransform m_editorTransform;

            // These are only used to hold onto the references returned by GetLocalTM and GetWorldTM
            AZ::Transform m_localTransformCache;
            AZ::Transform m_worldTransformCache;

            // Keeping a world transform along with a parent Id at the time of capture.
            // This is required for dealing with external changes to parent assignment (i.e. slice propagation).
            // A local transform alone isn't enough, since we may have serialized a parent-relative local transform,
            // but detached from the parent via propagation of the parent Id field. In such a case, we need to
            // know to not erroneously apply the local-space transform we serialized in a world-space capacity.
            AZ::Transform m_cachedWorldTransform;
            AZ::EntityId m_cachedWorldTransformParent;

            AZ::EntityId m_parentEntityId;
            AZ::EntityId m_previousParentEntityId;

            EntityIdList m_childrenEntityIds;

            bool m_suppressTransformChangedEvent;

            bool m_localTransformDirty = true;
            bool m_worldTransformDirty = true;
            bool m_isStatic = false;

            // Used to check whether entity was just created vs manually reactivated. Set true after OnEntityActivated is called the first time.
            bool m_initialized = false;

            // Extended (Transform 2) state. Editor only: it never ships, and a Transform 1 only ever tests m_isActive.
            bool m_isActive = false;            ///< Between the end of Activate and the start of Deactivate.
            bool m_transform2Live = false;      ///< Serving Transform2RequestBus, with Activated raised and no Deactivated yet.
            bool m_reactivationQueued = false;  ///< A re-activation of the entity is already waiting for the next tick.
            AZ::Vector3 m_announcedScale = AZ::Vector3::CreateOne(); ///< Effective world scale last announced on the Transform 2 notifications.
            AZ::Vector3 m_announcedResidual = AZ::Vector3::CreateOne(); ///< Residual of the world scale last announced on the residual event.

            // The memo of the chain. It is dirty at every activation, on a change of a transform that serves or has an extended
            // scale, when the parent changes whether it serves (the parent marks it), after a reparent that involves a
            // transform that serves, and while the parent cannot be found. It is computed on demand from the authored data of
            // this transform and of its direct parent. Mutable, because the const questions of the editor (the header tag, the
            // export) are answered from it.
            mutable AZ::Transform2::ChainState m_chain;  ///< What this transform hands to its children. Valid while m_serves.
            mutable bool m_serves = false;               ///< This transform serves Transform 2: its own scale, or its parent.
            mutable bool m_parentServes = false;         ///< The parent served when the memo was last computed.
            mutable bool m_chainDirty = true;            ///< The memo must be computed again.
            mutable bool m_chainComputing = false;       ///< The memo is being computed. A parent loop in bad data meets it again.

            FocusModeInterface* m_focusModeInterface = nullptr;

            // Deprecated
            AZ::InterpolationMode m_interpolatePosition;
            AZ::InterpolationMode m_interpolateRotation;
            bool m_netSyncEnabled = false;
        };
    }
} // namespace AzToolsFramework
