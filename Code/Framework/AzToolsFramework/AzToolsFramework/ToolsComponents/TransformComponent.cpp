/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "TransformComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/EntityUtils.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/IntersectSegment.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/Json/RegistrationContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/API/ApplicationAPI.h>
#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Visibility/EntityBoundsUnionBus.h>
#include <AzToolsFramework/API/EntityCompositionRequestBus.h>
#include <AzToolsFramework/Entity/EditorEntityTransformBus.h>
#include <AzToolsFramework/API/EntityPropertyEditorRequestsBus.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/ContainerEntity/ContainerEntityInterface.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/Entity/EditorEntityInfoBus.h>
#include <AzToolsFramework/Entity/ReadOnly/ReadOnlyEntityInterface.h>
#include <AzToolsFramework/Prefab/DocumentPropertyEditor/PrefabComponentAdapter.h>
#include <AzToolsFramework/Prefab/PrefabFocusPublicInterface.h>
#include <AzToolsFramework/ToolsComponents/TransformComponentBus.h>
#include <AzToolsFramework/ToolsComponents/TransformComponentSerializer.h>
#include <AzToolsFramework/ToolsComponents/EditorInspectorComponentBus.h>
#include <AzToolsFramework/ToolsComponents/EditorPendingCompositionBus.h>
#include <AzToolsFramework/UI/PropertyEditor/ComponentEditorHeader.hxx>
#include <AzToolsFramework/UI/PropertyEditor/PropertyEditorAPI.h>
#include <AzToolsFramework/ViewportSelection/EditorSelectionUtil.h>
#include <AzToolsFramework/Viewport/ViewportMessages.h>

#include <QMenu>
#include <QMessageBox>

namespace AzToolsFramework
{
    namespace Components
    {
        namespace Internal
        {
            const AZ::u32 ParentEntityCRC = AZ_CRC_CE("Parent Entity");

            // Wording of the header tag shown while this transform, or an ancestor of it, targets Transform 2.
            // The tooltip says what a flip reaches, what stays in world terms and the limits a user will hit. It is plain text,
            // which Qt does not wrap, so the lines are broken by hand. Revisit it when a system joins the flip. The tag also
            // shows on a child of an extended parent, which is why the first line names a parent. That child follows the flip
            // of its parent; only a child attached while the game runs has to ask for it.
            const char* const Transform2Tag = "Extended";
            const char* const Transform2TagToolTip =
                "Extended: this entity, or a parent of it, is deployed as Transform 2.\n"
                "It costs more memory and math than the optimized transform.\n"
                "A flip (a negative scale axis) mirrors meshes, colliders, actors,\n"
                "shapes, lights and probes, force regions, joints, decals and gradients.\n"
                "Gravity, character controllers, world space forces, sun and sky\n"
                "stay in world terms.\n"
                "Limits:\n"
                "- Baked probes need a rebake.\n"
                "- A joint is mirrored only when it is created. D6 joints do not flip.\n"
                "- A spot light gobo and a decal normal map do not mirror.\n"
                "- White Box edit handles stay on the authored side.\n"
                "- Particles, cloth and Track View scale keys do not flip.\n"
                "- A child attached while the game runs needs Deploy as Extended\n"
                "to follow the flip.\n"
                "To go back, set X, Y and Z to one positive value\n"
                "and clear Deploy as Extended.";

            // Two floats whose relative difference is below this are one float. The runtime Transform 2 uses the same value.
            const float FloatChangedTolerance = 1e-6f;

            // A residual that moved by less than this has not changed for the systems that listen to it. The runtime Transform 2
            // uses the same value.
            const float ResidualChangedTolerance = 1e-5f;

            // The entity whose activation is being cycled by a crossing between uniform and non-uniform scale, if any.
            static AZ::EntityId ReactivatingEntityId;

            // True when two floats differ by more than the tolerance, relative to the larger magnitude.
            static bool HasFloatChanged(float a, float b)
            {
                return AZ::GetAbs(a - b) > FloatChangedTolerance * AZ::GetMax(AZ::GetAbs(a), AZ::GetAbs(b));
            }

            // +1 for every axis that is not negative, -1 for every axis that is.
            static AZ::Vector3 SignOf(const AZ::Vector3& value)
            {
                return AZ::Vector3(
                    value.GetX() < 0.0f ? -1.0f : 1.0f,
                    value.GetY() < 0.0f ? -1.0f : 1.0f,
                    value.GetZ() < 0.0f ? -1.0f : 1.0f);
            }

            // Decompose a transform into euler angles in degrees, uniform scale, and translation.
            void DecomposeTransform(const AZ::Transform& transform, AZ::Vector3& translation, AZ::Vector3& rotation, float& scale)
            {
                scale = transform.GetUniformScale();
                translation = transform.GetTranslation();
                rotation = transform.GetRotation().GetEulerDegrees();
            }

            bool TransformComponentDataConverter(AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& classElement)
            {
                if (classElement.GetVersion() < 6)
                {
                    // In v6, "Slice Transform" became slice-relative.
                    const int sliceRelTransformIdx = classElement.FindElement(AZ_CRC_CE("Slice Transform"));
                    if (sliceRelTransformIdx >= 0)
                    {
                    // Convert slice-relative transform/root to standard parent-child relationship.
                    const int sliceRootIdx = classElement.FindElement(AZ_CRC_CE("Slice Root"));
                    const int parentIdx = classElement.FindElement(ParentEntityCRC);
                    const int editorTransformIdx = classElement.FindElement(AZ_CRC_CE("Transform Data"));
                    const int cachedTransformIdx = classElement.FindElement(AZ_CRC_CE("Cached World Transform"));

                    if (editorTransformIdx >= 0 && sliceRootIdx >= 0 && parentIdx >= 0)
                    {
                        auto& sliceTransformElement = classElement.GetSubElement(sliceRelTransformIdx);
                        auto& sliceRootElement = classElement.GetSubElement(sliceRootIdx);
                        auto& parentElement = classElement.GetSubElement(parentIdx);
                        auto& editorTransformElement = classElement.GetSubElement(editorTransformIdx);

                        AZ::Transform sliceRelTransform;
                        if (sliceTransformElement.GetData(sliceRelTransform))
                        {
                            // If the entity already has a parent assigned, we don't need to fix anything up.
                            // We only need to convert slice root to parent for non-child entities.
                            const int parentIdValueIdx = parentElement.FindElement(AZ_CRC_CE("id"));
                            AZ::u64 parentId = 0;
                            if (parentIdValueIdx >= 0)
                            {
                                parentElement.GetSubElement(parentIdValueIdx).GetData(parentId);
                            }

                            AZ::EntityId sliceRootId;
                            const int entityIdValueIdx = sliceRootElement.FindElement(AZ_CRC_CE("id"));

                            if (entityIdValueIdx < 0)
                            {
                                return false;
                            }

                            if (parentId == static_cast<AZ::u64>(AZ::EntityId()) && sliceRootElement.GetSubElement(entityIdValueIdx).GetData(sliceRootId))
                            {
                                // Upgrading the data itself is only relevant when a slice root was actually defined.
                                if (sliceRootId.IsValid())
                                {
                                    // Cached transforms weren't nullified in really old slices.
                                    if (cachedTransformIdx >= 0)
                                    {
                                        auto& cachedTransformElement = classElement.GetSubElement(cachedTransformIdx);
                                        cachedTransformElement.Convert<AZ::Transform>(context);
                                        cachedTransformElement.SetData(context, AZ::Transform::Identity());
                                    }

                                    // Our old slice root Id is now our parent Id.
                                    // Note - this could be ourself, but we can't know yet, so it gets fixed up by EditorEntityFixupComponent.
                                    parentElement.Convert<AZ::EntityId>(context);
                                    parentElement.SetData(context, sliceRootId);

                                    // Decompose the old slice-relative transform and set it as a our editor transform,
                                    // since the entity is now our parent.
                                    EditorTransform editorTransform;
                                    DecomposeTransform(sliceRelTransform, editorTransform.m_translate, editorTransform.m_rotate, editorTransform.m_uniformScale);
                                    editorTransform.m_authoredScale = AZ::Vector3(editorTransform.m_uniformScale);
                                    editorTransformElement.Convert<EditorTransform>(context);
                                    editorTransformElement.SetData(context, editorTransform);
                                }
                            }

                            // Finally, remove old fields.
                            classElement.RemoveElementByName(AZ_CRC_CE("Slice Transform"));
                            classElement.RemoveElementByName(AZ_CRC_CE("Slice Root"));
                            }
                        }
                        }
                    }

                if (classElement.GetVersion() < 7)
                {
                    // "IsStatic" added at v7.
                    // Old versions of TransformComponent are assumed to be non-static.
                    classElement.AddElementWithData(context, "IsStatic", false);
                }

                if (classElement.GetVersion() < 8)
                {
                    // "InterpolatePosition" added at v8.
                    // "InterpolateRotation" added at v8.
                    // Old versions of TransformComponent are assumed to be using linear interpolation.
                    classElement.AddElementWithData(context, "InterpolatePosition", AZ::InterpolationMode::NoInterpolation);
                    classElement.AddElementWithData(context, "InterpolateRotation", AZ::InterpolationMode::NoInterpolation);
                }

                // note the == on the following line.  Do not add to this block.  If you add an "InterpolateScale" back in, then
                // consider erasing this block.  The version was bumped from 8 to 9 to ensure this code runs.
                // if you add the field back in, then increment the version number again.
                if (classElement.GetVersion() == 8)
                {
                    // a field was temporarily added to this specific version, then was removed.
                    // However, some data may have been exported with this field present, so
                    // remove it if its found, but only in this version which the change was present in, so that
                    // future re-additions of it won't remove it (as long as they bump the version number.)
                    classElement.RemoveElementByName(AZ_CRC_CE("InterpolateScale"));
                }

                if (classElement.GetVersion() < 10)
                {
                    // The "Sync Enabled" flag is no longer needed.
                    classElement.RemoveElementByName(AZ_CRC_CE("Sync Enabled"));
                }

                return true;
            }

            bool EditorTransformDataConverter(AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& classElement)
            {
                if (classElement.GetVersion() < 3)
                {
                    // version 3 replaces vector scale with uniform scale but does not yet delete the legacy scale data
                    // in order to allow for migration
                    AZ::Vector3 vectorScale;
                    if (classElement.FindSubElementAndGetData<AZ::Vector3>(AZ_CRC_CE("Scale"), vectorScale))
                    {
                        const float uniformScale = vectorScale.GetMaxElement();
                        classElement.AddElementWithData(context, "UniformScale", uniformScale);
                    }
                }

                if (classElement.GetVersion() < 4)
                {
                    // version 4 adds the authored vector scale, which starts out as the uniform scale
                    float uniformScale = 1.0f;
                    classElement.FindSubElementAndGetData<float>(AZ_CRC_CE("UniformScale"), uniformScale);
                    classElement.AddElementWithData(context, "AuthoredScale", AZ::Vector3(uniformScale));
                }

                return true;
            }
        } // namespace Internal

        TransformComponent::TransformComponent()
            : m_isStatic(false)
            , m_parentActivationTransformMode(AZ::TransformConfig::ParentActivationTransformMode::MaintainOriginalRelativeTransform)
            , m_cachedWorldTransform(AZ::Transform::Identity())
            , m_suppressTransformChangedEvent(false)
            , m_interpolatePosition(AZ::InterpolationMode::NoInterpolation)
            , m_interpolateRotation(AZ::InterpolationMode::NoInterpolation)
            , m_focusModeInterface(AZ::Interface<AzToolsFramework::FocusModeInterface>::Get())
        {
        }

        TransformComponent::~TransformComponent()
        {
        }

        void TransformComponent::Init()
        {
            //Ensure that when we init that our previous/parent match
            m_previousParentEntityId = m_parentEntityId;
        }

        void TransformComponent::Activate()
        {
            EditorComponentBase::Activate();

            // Transform 2: the memo of the chain, and with it the world, is read afresh from the data at every activation,
            // because components may have come or gone since the last one.
            m_chainDirty = true;
            m_worldTransformDirty = true;

            TransformComponentMessages::Bus::Handler::BusConnect(GetEntityId());
            AZ::TransformBus::Handler::BusConnect(GetEntityId());
            AZ::SliceEntityHierarchyRequestBus::Handler::BusConnect(GetEntityId());

            // for drag + drop child entity from one parent to another, undo/redo
            if (m_parentEntityId.IsValid())
            {
                AZ::EntityBus::Handler::BusConnect(m_parentEntityId);

                if (m_parentEntityId != m_previousParentEntityId)
                {
                    ToolsApplicationNotificationBus::Broadcast(
                        &ToolsApplicationEvents::EntityParentChanged, GetEntityId(),
                        m_parentEntityId, m_previousParentEntityId);
                }

                m_previousParentEntityId = m_parentEntityId;
            }
            // it includes the process of create/delete entity
            else
            {
                CheckApplyCachedWorldTransform(AZ::Transform::Identity());
                UpdateCachedWorldTransform();
            }

            // Transform 2 services start last, so presence on the buses is valid before any dependent component activates.
            m_isActive = true;
            AZ_Warning(
                "TransformComponent", !(m_editorTransform.IsExtended() && HasLegacyNonUniformScaleComponent()),
                "Entity '%s' has an extended transform and a Non-uniform Scale component. The Non-uniform Scale component keeps "
                "the scale of the entity, and the extended scale is not applied until that component is removed. "
                "Press 'Convert to Transform Scale' on the Non-uniform Scale component to fold it into the Scale of this transform.",
                GetEntity()->GetName().c_str());
            UpdateResidualBus(ServesTransform2());
            UpdateTransform2State(GetWorldTM());
        }

        void TransformComponent::Deactivate()
        {
            DeactivateTransform2();

            AZ::TransformNotificationBus::Event(m_parentEntityId, &AZ::TransformNotificationBus::Events::OnChildRemoved, GetEntityId());
            if (auto* parentTransform = AZ::TransformBus::FindFirstHandler(m_parentEntityId))
            {
                parentTransform->NotifyChildChangedEvent(AZ::ChildChangeType::Removed, GetEntityId());
            }

            AZ::EntityBus::Handler::BusDisconnect();
            AZ::SliceEntityHierarchyRequestBus::Handler::BusDisconnect();
            AZ::TransformBus::Handler::BusDisconnect();
            TransformComponentMessages::Bus::Handler::BusDisconnect();
            AZ::TransformHierarchyInformationBus::Handler::BusDisconnect();
            AZ::TransformNotificationBus::MultiHandler::BusDisconnect();

            EditorComponentBase::Deactivate();
        }

        void TransformComponent::BindTransformChangedEventHandler(AZ::TransformChangedEvent::Handler& handler)
        {
            handler.Connect(m_transformChangedEvent);
        }

        void TransformComponent::BindParentChangedEventHandler(AZ::ParentChangedEvent::Handler& handler)
        {
            handler.Connect(m_parentChangedEvent);
        }

        void TransformComponent::BindChildChangedEventHandler(AZ::ChildChangedEvent::Handler& handler)
        {
            handler.Connect(m_childChangedEvent);
        }

        void TransformComponent::NotifyChildChangedEvent(AZ::ChildChangeType changeType, AZ::EntityId entityId)
        {
            m_childChangedEvent.Signal(changeType, entityId);
        }

        // This is called when our transform changes directly, or our parent's has changed.
        void TransformComponent::OnTransformChanged(const AZ::Transform& /*local*/, const AZ::Transform& world)
        {
            m_localTransformDirty = true;
            m_worldTransformDirty = true;

            // Transform 2: a transform that serves, or has an extended scale, reads its memo again. Any other transform has
            // nothing in this notification that could change its answer: a parent that changes whether it serves marks the memo
            // of its children dirty before it announces itself, and one that does so by activating again is found by the
            // children in OnEntityActivated. So a Transform 1 chain pays these two tests, and no lookup.
            if (m_serves || m_editorTransform.IsExtended())
            {
                m_chainDirty = true;
            }

            if (const AZ::Entity* entity = GetEntity())
            {
                SetDirty();

                // Transform 2: the world of a transform that serves is the best fit of its chain, not the parent world times
                // the local, so it announces itself with that world and the argument is ignored.
                if (ServesTransform2())
                {
                    OnTransformChangedExtended();
                    return;
                }

                // Update parent-relative transform.
                const auto& localTM = GetLocalTM();
                const auto worldTM = world * localTM;

                if (m_parentEntityId.IsValid())
                {
                    // If the parent entity is invalid, the world position of the entity shouldn't be cached as this can be later used in
                    // calculating the local translation of the entity when activated under a different parent.
                    UpdateCachedWorldTransform();
                }

                AZ::TransformNotificationBus::Event(
                    GetEntityId(), &TransformNotification::OnTransformChanged, localTM, worldTM);
                m_transformChangedEvent.Signal(localTM, worldTM);

                // After both Transform 1 signals, so components reacting to them have already run.
                UpdateTransform2State(worldTM);

                AzFramework::IEntityBoundsUnion* boundsUnion = AZ::Interface<AzFramework::IEntityBoundsUnion>::Get();
                if (boundsUnion != nullptr)
                {
                    boundsUnion->OnTransformUpdated(GetEntity());
                }
                // Fire a property changed notification for this component.  This will cascade to updating the UI.  It is not
                // necessary to notify the UI directly.
                if (const AZ::Component* component = entity->FindComponent<Components::TransformComponent>())
                {
                    PropertyEditorEntityChangeNotificationBus::Event(
                        GetEntityId(), &PropertyEditorEntityChangeNotifications::OnEntityComponentPropertyChanged, component->GetId());
                }
            }
        }

        void TransformComponent::OnTransformChanged()
        {
            if (!m_suppressTransformChangedEvent)
            {
                auto parent = GetParentTransformComponent();
                if (parent)
                {
                    OnTransformChanged(parent->GetLocalTM(), parent->GetWorldTM());
                }
                else
                {
                    OnTransformChanged(AZ::Transform::Identity(), AZ::Transform::Identity());
                }
            }
        }

        void TransformComponent::UpdateCachedWorldTransform()
        {
            const AZ::Transform& worldTransform = GetWorldTM();
            if (m_cachedWorldTransformParent != m_parentEntityId || !worldTransform.IsClose(m_cachedWorldTransform))
            {
                m_cachedWorldTransformParent = GetParentId();
                m_cachedWorldTransform = GetWorldTM();
                if (GetEntity())
                {
                    SetDirty();
                }
            }
        }

        void TransformComponent::ClearCachedWorldTransform()
        {
            m_cachedWorldTransform = AZ::Transform::Identity();
            m_cachedWorldTransformParent = AZ::EntityId();
        }

        void TransformComponent::CheckApplyCachedWorldTransform(const AZ::Transform& parentWorld)
        {
            // Transform 2: the cached world is a Transform 1 view, so a transform that serves keeps it as a world write does.
            // The cheap tests come first, so a Transform 1 only meets the memo when it has a cached world to apply.
            if (m_parentEntityId != m_cachedWorldTransformParent && !m_cachedWorldTransform.IsClose(AZ::Transform::Identity()) &&
                ServesTransform2())
            {
                SetWorldTM(m_cachedWorldTransform);
                return;
            }

            if (m_parentEntityId != m_cachedWorldTransformParent)
            {
                if (!m_cachedWorldTransform.IsClose(AZ::Transform::Identity()))
                {
                    SetLocalTM(parentWorld.GetInverse() * m_cachedWorldTransform);
                }
            }
        }

        AZ::Transform TransformComponent::GetLocalTranslationTM() const
        {
            return AZ::Transform::CreateTranslation(m_editorTransform.m_translate);
        }

        AZ::Transform TransformComponent::GetLocalRotationTM() const
        {
            return AZ::ConvertEulerDegreesToTransform(m_editorTransform.m_rotate);
        }

        AZ::Transform TransformComponent::GetLocalScaleTM() const
        {
            return AZ::Transform::CreateUniformScale(m_editorTransform.m_uniformScale);
        }

        const AZ::Transform& TransformComponent::GetLocalTM()
        {
            if (m_localTransformDirty)
            {
                m_localTransformCache = GetLocalTranslationTM() * GetLocalRotationTM() * GetLocalScaleTM();
                m_localTransformDirty = false;
            }

            return m_localTransformCache;
        }

        // given a local transform, update local transform.
        void TransformComponent::SetLocalTM(const AZ::Transform& finalTx)
        {
            AZ::Vector3 tx, rot;
            float uniformScale;
            Internal::DecomposeTransform(finalTx, tx, rot, uniformScale);

            m_editorTransform.m_translate = tx;
            m_editorTransform.m_rotate = rot;
            m_editorTransform.ApplyUniformScale(uniformScale);

            TransformChanged();
        }

        const EditorTransform& TransformComponent::GetLocalEditorTransform()
        {
            return m_editorTransform;
        }

        void TransformComponent::SetLocalEditorTransform(const EditorTransform& dest)
        {
            m_editorTransform = dest;
            m_editorTransform.m_uniformScale = m_editorTransform.ResolveUniformScale();

            TransformChanged();
        }

        bool TransformComponent::IsTransformLocked()
        {
            return m_editorTransform.m_locked;
        }

        const AZ::Transform& TransformComponent::GetWorldTM()
        {
            // Transform 2: the world of a transform that serves is the best fit of its chain, kept in the same cache.
            if (m_worldTransformDirty && ServesTransform2())
            {
                m_worldTransformCache = AZ::Transform2::BestFit(m_chain);
                m_worldTransformDirty = false;
                return m_worldTransformCache;
            }

            if (m_worldTransformDirty)
            {
                m_worldTransformCache = GetParentWorldTM() * GetLocalTM();
                m_worldTransformDirty = false;
            }

            return m_worldTransformCache;
        }

        void TransformComponent::SetWorldTM(const AZ::Transform& finalTx)
        {
            // Transform 2: a transform that serves solves its authored values against the parent chain.
            if (ServesTransform2())
            {
                SetWorldTMExtended(finalTx);
                return;
            }

            AZ::Transform parentGlobalToWorldInverse = GetParentWorldTM().GetInverse();
            SetLocalTM(parentGlobalToWorldInverse * finalTx);
        }

        void TransformComponent::GetLocalAndWorld(AZ::Transform& localTM, AZ::Transform& worldTM)
        {
            localTM = GetLocalTM();
            worldTM = GetWorldTM();
        }

        void TransformComponent::SetWorldTranslation(const AZ::Vector3& newPosition)
        {
            AZ::Transform newWorldTransform = GetWorldTM();
            newWorldTransform.SetTranslation(newPosition);
            SetWorldTM(newWorldTransform);
        }

        void TransformComponent::SetLocalTranslation(const AZ::Vector3& newPosition)
        {
            AZ::Transform newLocalTransform = GetLocalTM();
            newLocalTransform.SetTranslation(newPosition);
            SetLocalTM(newLocalTransform);
        }

        AZ::Vector3 TransformComponent::GetWorldTranslation()
        {
            return GetWorldTM().GetTranslation();
        }

        AZ::Vector3 TransformComponent::GetLocalTranslation()
        {
            return GetLocalTM().GetTranslation();
        }

        void TransformComponent::MoveEntity(const AZ::Vector3& offset)
        {
            const AZ::Vector3& worldPosition = GetWorldTM().GetTranslation();
            SetWorldTranslation(worldPosition + offset);
        }

        void TransformComponent::SetWorldX(float newX)
        {
            const AZ::Vector3& worldPosition = GetWorldTM().GetTranslation();
            SetWorldTranslation(AZ::Vector3(newX, worldPosition.GetY(), worldPosition.GetZ()));
        }

        void TransformComponent::SetWorldY(float newY)
        {
            const AZ::Vector3& worldPosition = GetWorldTM().GetTranslation();
            SetWorldTranslation(AZ::Vector3(worldPosition.GetX(), newY, worldPosition.GetZ()));
        }

        void TransformComponent::SetWorldZ(float newZ)
        {
            const AZ::Vector3& worldPosition = GetWorldTM().GetTranslation();
            SetWorldTranslation(AZ::Vector3(worldPosition.GetX(), worldPosition.GetY(), newZ));
        }

        float TransformComponent::GetWorldX()
        {
            return GetWorldTranslation().GetX();
        }

        float TransformComponent::GetWorldY()
        {
            return GetWorldTranslation().GetY();
        }

        float TransformComponent::GetWorldZ()
        {
            return GetWorldTranslation().GetZ();
        }

        void TransformComponent::SetLocalX(float x)
        {
            m_editorTransform.m_translate.SetX(x);
            TransformChanged();
        }

        void TransformComponent::SetLocalY(float y)
        {
            m_editorTransform.m_translate.SetY(y);
            TransformChanged();
        }

        void TransformComponent::SetLocalZ(float z)
        {
            m_editorTransform.m_translate.SetZ(z);
            TransformChanged();
        }

        float TransformComponent::GetLocalX()
        {
            return m_editorTransform.m_translate.GetX();
        }

        float TransformComponent::GetLocalY()
        {
            return m_editorTransform.m_translate.GetY();
        }

        float TransformComponent::GetLocalZ()
        {
            return m_editorTransform.m_translate.GetZ();
        }

        void TransformComponent::SetWorldRotationQuaternion(const AZ::Quaternion& quaternion)
        {
            AZ::Transform newWorldTransform = GetWorldTM();
            newWorldTransform.SetRotation(quaternion);
            SetWorldTM(newWorldTransform);
        }

        AZ::Vector3 TransformComponent::GetWorldRotation()
        {
            return GetWorldTM().GetRotation().GetEulerRadians();
        }

        AZ::Quaternion TransformComponent::GetWorldRotationQuaternion()
        {
            return GetWorldTM().GetRotation();
        }

        void TransformComponent::SetLocalRotation(const AZ::Vector3& eulerAnglesRadian)
        {
            m_editorTransform.m_rotate = AZ::Vector3RadToDeg(eulerAnglesRadian);
            TransformChanged();
        }

        void TransformComponent::SetLocalRotationQuaternion(const AZ::Quaternion& quaternion)
        {
            m_editorTransform.m_rotate = quaternion.GetEulerDegrees();
            TransformChanged();
        }

        void TransformComponent::RotateAroundLocalX(float eulerAngleRadian)
        {
            AZ::Transform localRotate = AZ::ConvertEulerDegreesToTransform(m_editorTransform.m_rotate);
            AZ::Vector3 xAxis = localRotate.GetBasisX();
            AZ::Quaternion xRotate = AZ::Quaternion::CreateFromAxisAngle(xAxis, eulerAngleRadian);
            AZ::Quaternion currentRotate = AZ::ConvertEulerDegreesToQuaternion(m_editorTransform.m_rotate);
            AZ::Quaternion newRotate = xRotate * currentRotate;
            newRotate.Normalize();
            m_editorTransform.m_rotate = newRotate.GetEulerDegrees();
            TransformChanged();
        }

        void TransformComponent::RotateAroundLocalY(float eulerAngleRadian)
        {
            AZ::Transform localRotate = AZ::ConvertEulerDegreesToTransform(m_editorTransform.m_rotate);
            AZ::Vector3 yAxis = localRotate.GetBasisY();
            AZ::Quaternion yRotate = AZ::Quaternion::CreateFromAxisAngle(yAxis, eulerAngleRadian);
            AZ::Quaternion currentRotate = AZ::ConvertEulerDegreesToQuaternion(m_editorTransform.m_rotate);
            AZ::Quaternion newRotate = yRotate * currentRotate;
            newRotate.Normalize();
            m_editorTransform.m_rotate = newRotate.GetEulerDegrees();

            TransformChanged();
        }

        void TransformComponent::RotateAroundLocalZ(float eulerAngleRadian)
        {
            AZ::Transform localRotate = AZ::ConvertEulerDegreesToTransform(m_editorTransform.m_rotate);
            AZ::Vector3 zAxis = localRotate.GetBasisZ();
            AZ::Quaternion zRotate = AZ::Quaternion::CreateFromAxisAngle(zAxis, eulerAngleRadian);
            AZ::Quaternion currentRotate = AZ::ConvertEulerDegreesToQuaternion(m_editorTransform.m_rotate);
            AZ::Quaternion newRotate = zRotate * currentRotate;
            newRotate.Normalize();
            m_editorTransform.m_rotate = newRotate.GetEulerDegrees();

            TransformChanged();
        }

        AZ::Vector3 TransformComponent::GetLocalRotation()
        {
            AZ::Vector3 result = AZ::Vector3DegToRad(m_editorTransform.m_rotate);
            return result;
        }

        AZ::Quaternion TransformComponent::GetLocalRotationQuaternion()
        {
            AZ::Quaternion result = AZ::ConvertEulerDegreesToQuaternion(m_editorTransform.m_rotate);
            return result;
        }

        AZ::Vector3 TransformComponent::GetLocalScale()
        {
            AZ_WarningOnce("TransformComponent", false, "GetLocalScale is deprecated, please use GetLocalUniformScale instead");
            return m_editorTransform.m_legacyScale;
        }

        void TransformComponent::SetLocalUniformScale(float scale)
        {
            m_editorTransform.ApplyUniformScale(scale);
            TransformChanged();
        }

        float TransformComponent::GetLocalUniformScale()
        {
            return m_editorTransform.m_uniformScale;
        }

        float TransformComponent::GetWorldUniformScale()
        {
            return GetWorldTM().GetUniformScale();
        }

        const AZ::Transform& TransformComponent::GetParentWorldTM() const
        {
            auto parent = GetParentTransformComponent();
            if (parent)
            {
                return parent->GetWorldTM();
            }
            return AZ::Transform::Identity();
        }

        void TransformComponent::SetParentImpl(AZ::EntityId parentId, bool relative)
        {
            // If the parent id to be set is the same as the current parent id
            // Or if the component belongs to an entity and the entity's id is the same as the id being set as parent
            if (parentId == m_parentEntityId || (GetEntity() && (GetEntityId() == parentId)))
            {
                return;
            }

            // Entity is not associated if we're just doing data preparation (slice construction).
            if (!GetEntity() || GetEntity()->GetState() == AZ::Entity::State::Constructed)
            {
                m_previousParentEntityId = m_parentEntityId = parentId;
                return;
            }

            // Prevent this from parenting to its own child. Check if this entity is in the new parent's hierarchy.
            auto potentialParentTransformComponent = GetTransformComponent(parentId);
            if (potentialParentTransformComponent && potentialParentTransformComponent->IsEntityInHierarchy(GetEntityId()))
            {
                return;
            }

            auto oldParentId = m_parentEntityId;

            bool canChangeParent = true;
            AZ::TransformNotificationBus::Event(
                GetEntityId(),
                &AZ::TransformNotificationBus::Events::CanParentChange,
                canChangeParent,
                oldParentId,
                parentId);

            if (!canChangeParent)
            {
                return;
            }

            // Transform 2: a reparent of a transform that serves now, or will serve under the new parent, reads its memo from
            // the new parent once that is in place. A world keeping one also keeps the Transform 1 view (position, rotation
            // and magnitude) instead of the parent world times the local: the lines below still run, and the authored values
            // are solved from this view once the new parent is in place. A reparent between two Transform 1 chains meets
            // nothing of this.
            const bool involvesTransform2 = ServesTransform2() || ServesTransform2Under(parentId);
            const bool keepsView = involvesTransform2 && !relative;
            const AZ::Transform viewToKeep = keepsView ? GetWorldTM() : AZ::Transform::CreateIdentity();
            const EditorTransform editorTransformBefore = m_editorTransform;

            // SetLocalTM calls below can confuse listeners, because transforms are mathematically
            // detached before the ParentChanged events are dispatched. Suppress TransformChanged()
            // until the transaction is complete.
            m_suppressTransformChangedEvent = true;

            if (m_parentEntityId.IsValid())
            {
                AZ::TransformHierarchyInformationBus::Handler::BusDisconnect();
                AZ::TransformNotificationBus::MultiHandler::BusDisconnect(m_parentEntityId);
                AZ::EntityBus::Handler::BusDisconnect(m_parentEntityId);

                if (!relative)
                {
                    SetLocalTM(GetParentWorldTM() * GetLocalTM());
                }

                TransformComponent* parentTransform = GetParentTransformComponent();
                if (parentTransform)
                {
                    auto& parentChildIds = GetParentTransformComponent()->m_childrenEntityIds;
                    parentChildIds.erase(AZStd::remove(parentChildIds.begin(), parentChildIds.end(), GetEntityId()), parentChildIds.end());
                }

                m_parentEntityId.SetInvalid();
                m_previousParentEntityId = m_parentEntityId;
            }

            if (parentId.IsValid())
            {
                AZ::TransformNotificationBus::MultiHandler::BusConnect(parentId);
                AZ::TransformHierarchyInformationBus::Handler::BusConnect(parentId);

                m_parentEntityId = parentId;
                m_previousParentEntityId = m_parentEntityId;

                if (!relative)
                {
                    AZ::Transform parentXform = GetParentWorldTM();
                    AZ::Transform inverseXform = parentXform.GetInverse();
                    SetLocalTM(inverseXform * GetLocalTM());
                }

                // Transform 2: the memo and the authored values are final before the connect below, which can apply the
                // cached world at once.
                if (involvesTransform2)
                {
                    SettleTransform2AfterReparent(keepsView, viewToKeep, editorTransformBefore);
                }

                // OnEntityActivated will trigger immediately if the parent is active
                AZ::EntityBus::Handler::BusConnect(m_parentEntityId);
            }

            // Transform 2: the same when the new parent is none.
            if (involvesTransform2 && !parentId.IsValid())
            {
                SettleTransform2AfterReparent(keepsView, viewToKeep, editorTransformBefore);
            }

            m_suppressTransformChangedEvent = false;

            // This is for Create Entity as child / Drag+drop parent update / add component
            AzToolsFramework::ToolsApplicationEvents::Bus::Broadcast(
                &AzToolsFramework::ToolsApplicationEvents::Bus::Events::EntityParentChanged, GetEntityId(), parentId, oldParentId);
            AZ::TransformNotificationBus::Event(
                GetEntityId(), &AZ::TransformNotificationBus::Events::OnParentChanged, oldParentId, parentId);
            m_parentChangedEvent.Signal(oldParentId, parentId);

            if (oldParentId.IsValid() && oldParentId != parentId)
            {
                AZ::TransformNotificationBus::Event(oldParentId, &AZ::TransformNotificationBus::Events::OnChildRemoved, GetEntityId());
                auto oldParentTransform = AZ::TransformBus::FindFirstHandler(oldParentId);
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

            TransformChanged();
        }

        void TransformComponent::SetParent(AZ::EntityId parentId)
        {
            SetParentImpl(parentId, false);
        }

        void TransformComponent::SetParentRelative(AZ::EntityId parentId)
        {
            SetParentImpl(parentId, true);
        }

        AZ::EntityId TransformComponent::GetParentId()
        {
            return m_parentEntityId;
        }

        EntityIdList TransformComponent::GetChildren()
        {
            EntityIdList children;
            AZ::TransformHierarchyInformationBus::Event(GetEntityId(), &AZ::TransformHierarchyInformation::GatherChildren, children);
            return children;
        }

        EntityIdList TransformComponent::GetAllDescendants()
        {
            EntityIdList descendants = GetChildren();
            for (size_t i = 0; i < descendants.size(); ++i)
            {
                AZ::TransformHierarchyInformationBus::Event(descendants[i], &AZ::TransformHierarchyInformation::GatherChildren, descendants);
            }
            return descendants;
        }

        AZStd::vector<AZ::EntityId> TransformComponent::GetEntityAndAllDescendants()
        {
            AZStd::vector<AZ::EntityId> descendants = { GetEntityId() };
            for (size_t i = 0; i < descendants.size(); ++i)
            {
                AZ::TransformHierarchyInformationBus::Event(descendants[i], &AZ::TransformHierarchyInformation::GatherChildren, descendants);
            }
            return descendants;
        }

        void TransformComponent::GatherChildren(EntityIdList& children)
        {
            children.push_back(GetEntityId());
        }

        bool TransformComponent::IsStaticTransform()
        {
            return m_isStatic;
        }

        void TransformComponent::SetIsStaticTransform(bool isStatic)
        {
            m_isStatic = isStatic;
        }

        TransformComponent* TransformComponent::GetParentTransformComponent() const
        {
            TransformComponent* component = GetTransformComponent(m_parentEntityId);

            // if our parent was cleared but we haven't gotten our change notify yet, we need to walk the previous parent if possible
            // to get the proper component for any necessary cleanup
            if (!component && !m_parentEntityId.IsValid() && m_previousParentEntityId.IsValid())
            {
                component = GetTransformComponent(m_previousParentEntityId);
            }

            return component;
        }

        TransformComponent* TransformComponent::GetTransformComponent(AZ::EntityId otherEntityId) const
        {
            if (!otherEntityId.IsValid())
            {
                return nullptr;
            }

            AZ::Entity* entity = AZ::Interface<AZ::ComponentApplicationRequests>::Get()->FindEntity(otherEntityId);
            if (!entity)
            {
                return nullptr;
            }

            return entity->FindComponent<TransformComponent>();
        }

        AZ::TransformInterface* TransformComponent::GetParent()
        {
            return GetParentTransformComponent();
        }

        void TransformComponent::OnEntityActivated(const AZ::EntityId& parentEntityId)
        {
            AZ::TransformNotificationBus::MultiHandler::BusConnect(parentEntityId);
            AZ::TransformHierarchyInformationBus::Handler::BusConnect(parentEntityId);

            // Our parent entity has just been activated.
            AZ_Assert(parentEntityId == m_parentEntityId,
                "Received Activation message for an entity other than our parent.");

            TransformComponent* parentTransform = GetParentTransformComponent();
            if (parentTransform)
            {
                // Prevent circular parent/child relationships potentially generated through slice data hierarchies.
                // This doesn't only occur through direct user assignment of parent (which is handled separately),
                // but can also occur through cascading of slicces, so we need to validate on activation as well.
                if (GetEntity() && parentTransform->IsEntityInHierarchy(GetEntityId()))
                {
                    AZ_Error("Transform Component", false,
                             "Slice data propagation for Entity %s [%llu] has resulted in circular parent/child relationships. "
                             "Parent assignment for this entity has been reset.",
                             GetEntity()->GetName().c_str(),
                             GetEntityId());

                    SetParent(AZ::EntityId());
                    return;
                }

                // Transform 2: the parent has come back, and it may serve differently from when this memo was read from it: the
                // old Non-uniform Scale component came or went while it was re-activated, which is no change of its data, or the
                // component is a new one. This transform is not told by a notification then, so it reads its memo again and
                // announces what changed. A Transform 1 chain pays two bool tests here. A transform that is still activating
                // reads its memo as part of that.
                const bool parentChangedServing = m_isActive && parentTransform->ServesTransform2() != m_parentServes;
                if (parentChangedServing)
                {
                    m_chainDirty = true;
                }

                bool suppressTransformChangedEvent = m_suppressTransformChangedEvent;
                // temporarily disable calling OnTransformChanged, because CheckApplyCachedWorldTransform is not guaranteed
                // to call it when m_cachedWorldTransform is identity. We send it manually later.
                m_suppressTransformChangedEvent = false;
                // When parent comes online, compute local TM from world TM.
                CheckApplyCachedWorldTransform(parentTransform->GetWorldTM());
                if (!m_initialized)
                {
                    m_initialized = true;
                    // If this is the first time this entity is being activated, manually compute OnTransformChanged
                    // this can occur when either the entity first created or undo/redo command is performed
                    OnTransformChanged(AZ::Transform::Identity(), parentTransform->GetWorldTM());
                }
                m_suppressTransformChangedEvent = suppressTransformChangedEvent;

                auto& parentChildIds = GetParentTransformComponent()->m_childrenEntityIds;
                if (parentChildIds.end() == AZStd::find(parentChildIds.begin(), parentChildIds.end(), GetEntityId()))
                {
                    parentChildIds.push_back(GetEntityId());
                }

                // Announced as any change of this transform is, to its listeners and to its own children.
                if (parentChangedServing)
                {
                    OnTransformChanged();
                }
                else if (m_isActive && m_serves)
                {
                    // The parent was created again by an undo, a redo, an override revert or a prefab reload, and this transform
                    // was not, because nothing in its own data changed. Its chain was read from the old parent, and the new one
                    // may differ in a way that moves no Transform 1 view of this entity: a flip of another axis, or an axis
                    // ratio under the same largest magnitude. The refresh reads the parent again and announces only what changed.
                    RefreshFromParent();
                }
            }

            UpdateCachedWorldTransform();
        }

        void TransformComponent::OnEntityDeactivated(const AZ::EntityId& parentEntityId)
        {
            AZ_Assert(parentEntityId == m_parentEntityId,
                "Received Deactivation message for an entity other than our parent.");

            AZ::TransformNotificationBus::MultiHandler::BusDisconnect(parentEntityId);
        }

        bool TransformComponent::IsEntityInHierarchy(AZ::EntityId entityId)
        {
            /// Begin 1.7 Release hack - #TODO - LMBR-37330
            if (GetParentId() == GetEntityId())
            {
                m_parentEntityId = m_previousParentEntityId;
            }
            /// End 1.7 Release hack

            auto parentTComp = this;
            while (parentTComp)
            {
                if (parentTComp->GetEntityId() == entityId)
                {
                    return true;
                }

                parentTComp = parentTComp->GetParentTransformComponent();
            }

            return false;
        }

        AZ::u32 TransformComponent::TransformChangedInspector()
        {
            m_editorTransform.m_uniformScale = m_editorTransform.ResolveUniformScale();

            if (TransformChanged())
            {
                AzToolsFramework::EditorTransformChangeNotificationBus::Broadcast(
                    &AzToolsFramework::EditorTransformChangeNotificationBus::Events::OnEntityTransformChanged,
                    EntityIdList{ GetEntityId() });
            }

            return AZ::Edit::PropertyRefreshLevels::None;
        }

        AZStd::string TransformComponent::GetHeaderTag() const
        {
            return ServesTransform2() ? AZStd::string(Internal::Transform2Tag) : AZStd::string();
        }

        bool TransformComponent::TransformChanged()
        {
            if (!m_suppressTransformChangedEvent)
            {
                if (auto parent = GetParentTransformComponent())
                {
                    OnTransformChanged(parent->GetLocalTM(), parent->GetWorldTM());
                }
                else
                {
                    OnTransformChanged(AZ::Transform::Identity(), AZ::Transform::Identity());
                }

                return true;
            }

            return false;
        }

        //////////////////////////////////////////////////////////////////////////
        // Extended (Transform 2) requests
        //
        // A transform that serves Transform 2 is a Transform 2 at deploy time: its own scale is extended, or a parent serves.
        // In the editor it additionally serves the Transform 2 request bus, and the non-uniform scale bus while its effective
        // world scale is non-uniform, so editor tools and scripts see the same buses the runtime Transform 2 serves.
        // A Transform 1 serves neither and raises nothing.

        AZ::Vector3 TransformComponent::GetLocalScale() const
        {
            return m_editorTransform.m_authoredScale;
        }

        AZ::Vector3 TransformComponent::GetWorldScale() const
        {
            // Without a chain the answers are those of the bus defaults, which only a transform that serves is reached by.
            return ServesTransform2() ? m_chain.m_worldScale : GetLocalScale();
        }

        AZ::Matrix3x4 TransformComponent::GetWorldMatrix() const
        {
            return ServesTransform2() ? m_chain.m_world : AZ::Matrix3x4::CreateIdentity();
        }

        bool TransformComponent::IsWorldSheared() const
        {
            return ServesTransform2() && m_chain.m_sheared;
        }

        void TransformComponent::RefreshFromParent()
        {
            // The chain is read from the parent again. An editor transform hears of every change of its parent on the transform
            // notification bus, so this is for a caller that changed the parent without that, and for the activation of a parent
            // that was created again. It announces only what changed: a refresh that finds the same chain publishes nothing.
            if (!ServesTransform2())
            {
                return;
            }

            const AZ::Transform2::ChainState before = m_chain;
            m_chainDirty = true;
            const bool changed = !ServesTransform2() || m_chain.m_world != before.m_world ||
                m_chain.m_rotation != before.m_rotation || m_chain.m_worldScale != before.m_worldScale;
            if (changed)
            {
                m_worldTransformDirty = true;
                OnTransformChanged();
            }
        }

        void TransformComponent::SetLocalScale(const AZ::Vector3& scale)
        {
            if (!scale.IsFinite())
            {
                AZ_Warning("TransformComponent", false, "SetLocalScale ignored a non-finite scale for entity %s", GetEntityId().ToString().c_str());
                return;
            }

            m_editorTransform.m_authoredScale = scale;
            m_editorTransform.m_uniformScale = m_editorTransform.ResolveUniformScale();
            TransformChanged();
        }

        AZ::Vector3 TransformComponent::GetScale() const
        {
            // The positive residual on top of the Transform 1 float of the world, so a child of a non-uniform parent serves it too.
            return AZ::Transform2::Residual(GetWorldScale());
        }

        void TransformComponent::SetScale(const AZ::Vector3& scale)
        {
            // The residual multiplies the Transform 1 float of the world. The signs stay, and each axis of the own scale takes
            // the factor that its world axis needs. An axis with no world magnitude has no factor to take, and takes the size.
            const AZ::Vector3 worldScale = GetWorldScale();
            const AZ::Vector3 worldMagnitude = worldScale.GetAbs();
            const AZ::Vector3 target = AZ::Transform2::UniformStandIn(worldScale) * scale.GetAbs();
            const AZ::Vector3& authoredScale = m_editorTransform.m_authoredScale;
            const AZ::Vector3 signs = Internal::SignOf(authoredScale);

            AZ::Vector3 newScale = authoredScale;
            for (int32_t axis = 0; axis < 3; ++axis)
            {
                const float world = worldMagnitude.GetElement(axis);
                newScale.SetElement(
                    axis,
                    world > 0.0f ? authoredScale.GetElement(axis) * (target.GetElement(axis) / world)
                                 : signs.GetElement(axis) * target.GetElement(axis));
            }
            SetLocalScale(newScale);
        }

        void TransformComponent::RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler& handler)
        {
            handler.Connect(m_residualChangedEvent);
        }

        //////////////////////////////////////////////////////////////////////////
        // Extended (Transform 2) state

        bool TransformComponent::HasLegacyNonUniformScaleComponent() const
        {
            // The old component activates after this one, so it is looked for on the entity rather than waited for.
            return GetEntity() && GetEntity()->FindComponent<EditorNonUniformScaleComponent>() != nullptr;
        }

        //////////////////////////////////////////////////////////////////////////
        // Extended (Transform 2) chain
        //
        // Each transform keeps a memo of whether it serves Transform 2 and of the chain it hands to its children. Both are
        // read from the authored data of this transform and of its direct parent only. So the answer is the same live and at
        // export, where entities are initialized and not active, and it does not depend on the order of activation. Nothing
        // here reads bus presence or the live flag of a parent: a child can activate before its parent, and a child is told of a
        // change before its parent has announced it. A parent that serves hands over its own memo, and so on up the chain.

        bool TransformComponent::ServesTransform2() const
        {
            if (m_chainDirty)
            {
                UpdateChainMemo();
            }
            return m_serves;
        }

        void TransformComponent::UpdateChainMemo() const
        {
            // A parent loop in bad data meets a memo that is being computed. GetChainParent reads it as no parent, so the
            // recursion ends.
            if (m_chainComputing)
            {
                return;
            }
            m_chainComputing = true;

            // The old Non-uniform Scale component owns the scale of its entity while it exists: it holds the single handler of
            // the non-uniform scale bus, and the systems would otherwise get its stretch and the extended scale at once, from the
            // own scale or from a parent. Content that has it keeps working as it always did, and acts on the best fit of its
            // parent. The extended scale takes over when the component is removed. The other tests come first, so a
            // Transform 1 chain never looks through its components.
            const bool servedBefore = m_serves;
            const TransformComponent* parent = GetChainParent();
            m_parentServes = parent && parent->ServesTransform2();
            m_serves = (m_parentServes || m_editorTransform.IsExtended()) && !HasLegacyNonUniformScaleComponent();
            if (m_serves)
            {
                m_chain = AZ::Transform2::ComposeChain(
                    GetParentChain(), m_editorTransform.m_translate, AZ::ConvertEulerDegreesToQuaternion(m_editorTransform.m_rotate),
                    m_editorTransform.m_authoredScale);
            }

            // The children read their memo from this one, and a child that neither serves nor has an extended scale does not
            // read it again on a notification. So a change of the answer marks each of them dirty, here, before anything is
            // announced. A child that changes its own answer does the same for its children when it reads its memo. The children
            // are asked on the bus, because a parent that an undo reloaded has no list of them: they attach again without
            // OnEntityActivated.
            if (m_serves != servedBefore && GetEntity())
            {
                AZStd::vector<AZ::EntityId> children;
                AZ::TransformHierarchyInformationBus::Event(
                    GetEntityId(), &AZ::TransformHierarchyInformationBus::Events::GatherChildren, children);
                for (const AZ::EntityId childId : children)
                {
                    if (TransformComponent* child = GetTransformComponent(childId))
                    {
                        child->m_chainDirty = true;
                    }
                }
            }

            m_chainComputing = false;
            m_chainDirty = false;
        }

        TransformComponent* TransformComponent::GetChainParent() const
        {
            TransformComponent* parent = GetParentTransformComponent();
            return (parent && !parent->m_chainComputing) ? parent : nullptr;
        }

        AZ::Transform2::ChainState TransformComponent::GetParentChain() const
        {
            TransformComponent* parent = GetChainParent();
            if (!parent)
            {
                return AZ::Transform2::ChainState();
            }
            return parent->ServesTransform2() ? parent->m_chain : AZ::Transform2::ChainFromTransform(parent->GetWorldTM());
        }

        bool TransformComponent::ServesTransform2Under(AZ::EntityId parentId) const
        {
            const TransformComponent* parent = GetTransformComponent(parentId);
            return ((parent && parent->ServesTransform2()) || m_editorTransform.IsExtended()) &&
                !HasLegacyNonUniformScaleComponent();
        }

        bool TransformComponent::HasNonUniformScale(const AZ::Entity& entity)
        {
            // The same answer the buses give an active entity: the old component owns the non-uniform scale bus when it is
            // there, and otherwise a transform that serves has it while the magnitudes of its effective world scale differ,
            // which includes a scale inherited from a parent.
            if (entity.FindComponent<EditorNonUniformScaleComponent>() != nullptr)
            {
                return true;
            }

            const TransformComponent* transform = entity.FindComponent<TransformComponent>();
            return transform && transform->ServesTransform2() && AZ::Transform2::IsNonUniformEffective(transform->m_chain.m_worldScale);
        }

        bool TransformComponent::HasExtendedOwnScale(const AZ::Entity& entity)
        {
            const TransformComponent* transform = entity.FindComponent<TransformComponent>();
            return transform && transform->m_editorTransform.IsExtended();
        }

        bool TransformComponent::UpdateResidualBus(bool serving)
        {
            // Only a transform that serves, with a non-uniform effective world scale, is served. A Transform 1 that serves
            // nothing leaves here at once.
            const bool serve = serving && AZ::Transform2::IsNonUniformEffective(m_chain.m_worldScale);
            const bool connected = AZ::NonUniformScaleRequestBus::Handler::BusIsConnected();

            if (serve == connected)
            {
                return false;
            }

            if (serve)
            {
                // The bus has a single handler. Any other owner is left alone.
                if (AZ::NonUniformScaleRequestBus::HasHandlers(GetEntityId()))
                {
                    return false;
                }
                AZ::NonUniformScaleRequestBus::Handler::BusConnect(GetEntityId());
            }
            else
            {
                AZ::NonUniformScaleRequestBus::Handler::BusDisconnect();
            }
            return true;
        }

        void TransformComponent::UpdateTransform2State(const AZ::Transform& world)
        {
            if (!m_isActive)
            {
                return;
            }

            // A Transform 1 has nothing to serve and nothing to announce. The memo answers in one test.
            // The answer comes from authored data, never from the live flag of the parent: a child is told of a change before
            // its parent has run this function.
            const bool serving = ServesTransform2();
            if (!serving && !m_transform2Live)
            {
                return;
            }

            // Systems see the effective signed world scale, sW, while this transform serves, and the plain world float once
            // it stops: a descendant whose parent turned Transform 2 off acts on its own again.
            const AZ::EntityId entityId = GetEntityId();
            const AZ::Vector3 scale = serving ? m_chain.m_worldScale : AZ::Vector3(world.GetUniformScale());
            const AZ::Vector3 residual = AZ::Transform2::Residual(scale);
            if (!m_transform2Live)
            {
                // Nothing was announced before the first announcement of a life, so there is no change to find in it.
                m_announcedScale = scale;
                m_announcedResidual = residual;
            }

            // The scale and the residual are compared with what was last announced, not with the state before this change, as
            // the runtime does. Steps under the tolerance then add up until they announce. Stopping always announces.
            const bool scaleChanged = !serving || AZ::Transform2::HasWorldScaleChanged(m_announcedScale, scale);

            // Consumers read the presence of the non-uniform scale bus when they activate, so one that is already
            // active only learns of a crossing from a re-activation. A flip alone never changes the presence.
            if (UpdateResidualBus(serving))
            {
                QueueEntityReactivation();
            }

            // Anyone bound to the residual event hears of every change, including the last one before the bus goes away. The
            // event only comes with a scale change: a mesh overwrites its signed scale with the positive residual on the event,
            // and it is the scale notification below that puts the flip back.
            if (scaleChanged && !residual.IsClose(m_announcedResidual, Internal::ResidualChangedTolerance))
            {
                m_announcedResidual = residual;
                m_residualChangedEvent.Signal(residual);
            }

            if (serving)
            {
                const bool firstAnnouncement = !m_transform2Live;
                if (firstAnnouncement)
                {
                    m_transform2Live = true;
                    AZ::Transform2RequestBus::Handler::BusConnect(entityId);
                    AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Activated, entityId, scale);
                }

                // The order of Transform2Notifications, as the runtime raises it: the world, then the scale. The world is raised with
                // every call here, because the editor transform announces every authored change on the Transform 1 side as well.
                AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2WorldChanged, entityId, world, scale);
                if (!firstAnnouncement && scaleChanged)
                {
                    m_announcedScale = scale;
                    AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, scale);
                }
            }
            else
            {
                // Stopped serving: the listeners get the plain scale, then let go of the entity.
                m_transform2Live = false;
                AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2ScaleChanged, entityId, scale);
                AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, entityId);
                AZ::Transform2RequestBus::Handler::BusDisconnect();
            }
        }

        void TransformComponent::DeactivateTransform2()
        {
            if (m_transform2Live)
            {
                m_transform2Live = false;
                AZ::Transform2NotificationBus::Broadcast(&AZ::Transform2Notifications::OnTransform2Deactivated, GetEntityId());
            }

            AZ::Transform2RequestBus::Handler::BusDisconnect();
            AZ::NonUniformScaleRequestBus::Handler::BusDisconnect();
            m_reactivationQueued = false;
            m_isActive = false;
        }

        void TransformComponent::QueueEntityReactivation()
        {
            // One request per entity until its cycle runs, which clears the flag. A parent that crosses queues each of its
            // descendants on its own, so a tick runs one cycle per descendant, each in its own queued function.
            if (m_reactivationQueued)
            {
                return;
            }
            m_reactivationQueued = true;

            // The edit that caused the crossing is still open here, for this entity and for a descendant alike, because the
            // descendant hears of the crossing inside the edit of its ancestor. The re-activation below joins that undo step.
            UndoSystem::URSequencePoint* undoBatch = nullptr;
            ToolsApplicationRequests::Bus::BroadcastResult(undoBatch, &ToolsApplicationRequests::GetCurrentUndoBatch);

            // By entity id on the next tick, never inside the property message that caused the crossing.
            AZ::TickBus::QueueFunction(
                [entityId = GetEntityId(), undoBatch]()
                {
                    AZ::Entity* entity = nullptr;
                    AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, entityId);
                    if (entity && entity->GetState() == AZ::Entity::State::Active)
                    {
                        // Colliders save values they derive at activation, such as HasNonUniformScale, the shape scale and the
                        // shape offset, and game mode and the Asset Processor build from that saved data. So every entity that
                        // cycles is captured again after the cycle, in the same undo step as the edit: one undo takes all of it
                        // back, and nothing is left to be written into the prefab by the next change of an ancestor, which marks
                        // every descendant dirty and compares it with the prefab. Only what changed is written: the transform of
                        // a descendant does not. Without an open edit there is nothing to join, and BuildGameEntity of the
                        // colliders asks HasNonUniformScale rather than trust the saved flag.
                        if (undoBatch)
                        {
                            UndoSystem::URSequencePoint* resumedBatch = nullptr;
                            ToolsApplicationRequests::Bus::BroadcastResult(
                                resumedBatch, &ToolsApplicationRequests::ResumeUndoBatch, undoBatch, "Modify Entity Property");
                        }

                        // The components stay as they are, only their activation cycles. The inspector is told through
                        // IsReactivatingForTransform2 to keep the tree it has, because rebuilding it would destroy the field the
                        // user is editing and drop its focus. The values and attributes are refreshed in place instead.
                        Internal::ReactivatingEntityId = entityId;
                        entity->Deactivate();
                        entity->Activate();
                        Internal::ReactivatingEntityId = AZ::EntityId();

                        if (undoBatch)
                        {
                            ToolsApplicationRequests::Bus::Broadcast(&ToolsApplicationRequests::AddDirtyEntity, entityId);
                            ToolsApplicationRequests::Bus::Broadcast(&ToolsApplicationRequests::EndUndoBatch);
                        }

                        ToolsApplicationNotificationBus::Broadcast(
                            &ToolsApplicationNotificationBus::Events::InvalidatePropertyDisplay, Refresh_AttributesAndValues);
                    }
                });
        }

        bool TransformComponent::IsReactivatingForTransform2(AZ::EntityId entityId)
        {
            return entityId.IsValid() && entityId == Internal::ReactivatingEntityId;
        }

        //////////////////////////////////////////////////////////////////////////
        // Extended (Transform 2) announcements and writes
        //
        // The Transform 1 world of a transform that serves is the best fit of its chain, and its authored values are solved
        // against the chain of its parent. The functions below are the early branches of the Transform 1 functions of the
        // same name, so the Transform 1 lines stay as they are for every transform that does not serve.

        void TransformComponent::OnTransformChangedExtended()
        {
            // The announcements of OnTransformChanged, in the same order, with the world of the chain.
            const AZ::Transform& localTM = GetLocalTM();
            const AZ::Transform worldTM = GetWorldTM();

            if (m_parentEntityId.IsValid())
            {
                // If the parent entity is invalid, the world position of the entity shouldn't be cached as this can be later used in
                // calculating the local translation of the entity when activated under a different parent.
                UpdateCachedWorldTransform();
            }

            AZ::TransformNotificationBus::Event(GetEntityId(), &TransformNotification::OnTransformChanged, localTM, worldTM);
            m_transformChangedEvent.Signal(localTM, worldTM);

            // After both Transform 1 signals, so components reacting to them have already run.
            UpdateTransform2State(worldTM);

            AzFramework::IEntityBoundsUnion* boundsUnion = AZ::Interface<AzFramework::IEntityBoundsUnion>::Get();
            if (boundsUnion != nullptr)
            {
                boundsUnion->OnTransformUpdated(GetEntity());
            }

            // Fire a property changed notification for this component, which updates the UI and the header tag.
            if (const AZ::Component* component = GetEntity()->FindComponent<Components::TransformComponent>())
            {
                PropertyEditorEntityChangeNotificationBus::Event(
                    GetEntityId(), &PropertyEditorEntityChangeNotifications::OnEntityComponentPropertyChanged, component->GetId());
            }
        }

        void TransformComponent::SetWorldTMExtended(const AZ::Transform& tm)
        {
            // A copy of the request, because it can be the cache of this very transform.
            const AZ::Transform requested = tm;
            const AZ::Transform current = GetWorldTM();
            const bool hasParent = GetChainParent() != nullptr;
            const AZ::Transform2::ChainState parent = GetParentChain();

            // Each part is compared exactly, because a tolerance would stall a slow rotation. A part that did not change is not
            // written. So a writer that reads the world, changes one part and writes it back leaves the other parts alone, the
            // scale included: a rotate drag never changes the authored scale, and a Transform 1 child never becomes extended by it.
            if (requested.GetTranslation() != current.GetTranslation())
            {
                m_editorTransform.m_translate =
                    hasParent ? AZ::Transform2::SolveLocalTranslation(parent, requested.GetTranslation()) : requested.GetTranslation();
            }

            if (requested.GetRotation() != current.GetRotation())
            {
                const AZ::Quaternion rotation = hasParent
                    ? AZ::Transform2::SolveLocalRotation(parent, requested.GetRotation()).GetNormalized()
                    : requested.GetRotation();
                m_editorTransform.m_rotate = rotation.GetEulerDegrees();
            }

            // The float of the request is the world float, so the own scale is written by ratio, which keeps its signs and axis ratios.
            if (Internal::HasFloatChanged(requested.GetUniformScale(), current.GetUniformScale()))
            {
                const float currentFloat = current.GetUniformScale();
                m_editorTransform.ApplyUniformScale(
                    currentFloat > 0.0f ? m_editorTransform.m_uniformScale * (requested.GetUniformScale() / currentFloat)
                                        : requested.GetUniformScale());
            }

            // The announcement goes out even when nothing was written, as it does for a Transform 1.
            TransformChanged();
        }

        void TransformComponent::SettleTransform2AfterReparent(bool keepView, const AZ::Transform& view, const EditorTransform& before)
        {
            // The parent id changed without a change notification, so the memo and the world are read again from here on.
            m_worldTransformDirty = true;
            m_chainDirty = true;
            if (!keepView)
            {
                // SetParentRelative keeps the authored local, and the notification that ends the reparent recomputes the rest.
                return;
            }

            // The Transform 1 lines of the reparent folded the scale as they would for a Transform 1. The scale held before is
            // the one to keep, and the signs in it are not rewritten: the entity takes the flip of its new parent.
            m_editorTransform.m_authoredScale = before.m_authoredScale;
            m_editorTransform.m_uniformScale = before.m_uniformScale;
            m_localTransformDirty = true;

            // The position and the rotation of the view are the request, solved against the parent that is in place now.
            const bool hasParent = GetChainParent() != nullptr;
            const AZ::Transform2::ChainState parent = GetParentChain();
            m_editorTransform.m_translate =
                hasParent ? AZ::Transform2::SolveLocalTranslation(parent, view.GetTranslation()) : view.GetTranslation();
            const AZ::Quaternion rotation = hasParent
                ? AZ::Transform2::SolveLocalRotation(parent, view.GetRotation()).GetNormalized()
                : view.GetRotation();
            m_editorTransform.m_rotate = rotation.GetEulerDegrees();

            // The magnitude of the view is kept by folding the own float, by ratio, unless the world float is already the one.
            const AZ::Transform2::ChainState composed = AZ::Transform2::ComposeChain(
                parent, m_editorTransform.m_translate, AZ::ConvertEulerDegreesToQuaternion(m_editorTransform.m_rotate),
                m_editorTransform.m_authoredScale);
            const float worldFloat = AZ::Transform2::UniformStandIn(composed.m_worldScale);
            if (Internal::HasFloatChanged(worldFloat, view.GetUniformScale()))
            {
                m_editorTransform.ApplyUniformScale(m_editorTransform.m_uniformScale * (view.GetUniformScale() / worldFloat));
            }
        }

        bool TransformComponent::ReparentChildOfRemovedEntity(
            const AZ::Entity& childEntity, AZ::TransformInterface& removedParent, AZ::TransformInterface& child,
            bool& warnedAboutShear)
        {
            AzFramework::Transform2Component* removedParent2 = azrtti_cast<AzFramework::Transform2Component*>(&removedParent);
            AzFramework::Transform2Component* child2 = azrtti_cast<AzFramework::Transform2Component*>(&child);
            if (!removedParent2 && !child2)
            {
                return false;
            }

            // Both locals are authored, so the chain starts from the identity: the removed entity is composed under what was its
            // parent, and the child under the removed entity. The parent of the removed entity is not read, the child takes its place.
            // The own scale of a Transform 1 is its float. The const type is the bus request, and not the deprecated Transform 1 getter.
            const AZ::Transform parentLocal = removedParent.GetLocalTM();
            const AZ::Transform childLocal = child.GetLocalTM();
            const AzFramework::Transform2Component* constParent2 = removedParent2;
            const AzFramework::Transform2Component* constChild2 = child2;
            const AZ::Vector3 parentScale = constParent2 ? constParent2->GetLocalScale() : AZ::Vector3(parentLocal.GetUniformScale());
            const AZ::Vector3 childScale = constChild2 ? constChild2->GetLocalScale() : AZ::Vector3(childLocal.GetUniformScale());

            const AZ::Transform2::ChainState parentChain = AZ::Transform2::ComposeChain(
                AZ::Transform2::ChainState(), parentLocal.GetTranslation(), parentLocal.GetRotation(), parentScale);
            const AZ::Transform2::ChainState childChain = AZ::Transform2::ComposeChain(
                parentChain, childLocal.GetTranslation(), childLocal.GetRotation(), childScale);

            if (childChain.m_sheared && !warnedAboutShear)
            {
                warnedAboutShear = true;
                AZ_Warning(
                    "TransformComponent", false,
                    "Entity '%s' lost the shear of its removed editor-only parent. A translation, a rotation and a scale cannot "
                    "express it, and the child keeps the scale of its axes. This is reported once for the first entity.",
                    childEntity.GetName().c_str());
            }

            // The new local of the child is the translation and rotation of its chain, with the world scale of the chain as its
            // own scale. The scale goes first, and the Transform 1 float of the local is the stand-in of that scale, so the write
            // of the local folds nothing.
            child.SetParent(removedParent.GetParentId());
            if (child2)
            {
                child2->SetLocalScale(childChain.m_worldScale);
            }

            AZ::Transform newLocal =
                AZ::Transform::CreateFromQuaternionAndTranslation(childChain.m_rotation, childChain.m_world.GetTranslation());
            newLocal.SetUniformScale(AZ::Transform2::UniformStandIn(childChain.m_worldScale));
            child.SetLocalTM(newLocal);
            return true;
        }

        //////////////////////////////////////////////////////////////////////////
        // Converting an old Non-uniform Scale component
        //
        // The Scale of this transform replaces the Non-uniform Scale component, which can no longer be added. Content that
        // still has the component keeps working, and its card carries a button that folds it into the Scale: the authored
        // scale multiplied per axis by the old scale, and the old component removed, inside one undo batch.
        // Each entity converts on its own, so a button pressed with several entities selected leaves one undo step per entity.

        void TransformComponent::QueueLegacyScaleConversion(AZ::EntityId entityId)
        {
            // The conversion removes the component whose button is still handling the press, so it waits for the next tick.
            // Nothing is captured but the id: the entity, its transform and the old component are looked up again then.
            AZ::TickBus::QueueFunction(
                [entityId]()
                {
                    AZ::Entity* entity = nullptr;
                    AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, entityId);
                    TransformComponent* transform = entity ? entity->FindComponent<TransformComponent>() : nullptr;
                    if (transform && entity->GetState() == AZ::Entity::State::Active)
                    {
                        transform->ConvertLegacyNonUniformScale();
                    }
                });
        }

        void TransformComponent::ConvertLegacyNonUniformScale()
        {
            EditorNonUniformScaleComponent* legacyComponent = GetEntity()->FindComponent<EditorNonUniformScaleComponent>();
            if (!legacyComponent)
            {
                return;
            }

            // The Scale row is read only on a locked transform, and so is the conversion. RemoveComponents skips an entity
            // the editor will not change without saying so, which would leave the new scale written beside the old component.
            bool editable = true;
            ToolsApplicationRequestBus::BroadcastResult(editable, &ToolsApplicationRequests::IsEntityEditable, GetEntityId());
            const AZ::Vector3 foldedScale = EditorTransform::FoldLegacyScale(m_editorTransform.m_authoredScale, legacyComponent->GetScale());
            if (m_editorTransform.m_locked || !editable || !foldedScale.IsFinite())
            {
                AZ_Warning(
                    "TransformComponent", false,
                    "Entity '%s' keeps its Non-uniform Scale component: its transform is locked, the entity cannot be edited, "
                    "or the folded scale is not finite.",
                    GetEntity()->GetName().c_str());
                return;
            }

            AzToolsFramework::ScopedUndoBatch undo("Convert to Transform Scale");

            // Written while the old component is still on the entity, so the entity only activates once, with the new scale
            // and without the old component. The write is the one the Scale row makes: the vector, then the same change
            // notification, which resolves the float, signals the change and tells the editor.
            m_editorTransform.m_authoredScale = foldedScale;
            TransformChangedInspector();

            // This deletes the old component, and re-activates the entity, which brings the Transform 2 services up.
            EntityCompositionRequestBus::Broadcast(&EntityCompositionRequests::RemoveComponents, AZ::Entity::ComponentArrayType{ legacyComponent });
        }

        // This is called when our transform changes static state.
        AZ::u32 TransformComponent::StaticChangedInspector()
        {
            InvalidatePropertyDisplay(AzToolsFramework::PropertyModificationRefreshLevel::Refresh_EntireTree);

            if (GetEntity())
            {
                SetDirty();
                AZ::TransformNotificationBus::Event(GetEntityId(), &AZ::TransformNotificationBus::Events::OnStaticChanged, m_isStatic);
            }

            return AZ::Edit::PropertyRefreshLevels::EntireTree;
        }

        void TransformComponent::ModifyEditorTransform(AZ::Vector3& vec, const AZ::Vector3& data, const AZ::Transform& parentInverse)
        {
            if (data.IsZero())
            {
                return;
            }

            auto delta = parentInverse.TransformPoint(data);

            vec += delta;

            TransformChanged();
        }

        void TransformComponent::TranslateBy(const AZ::Vector3& data)
        {
            auto parent = GetParentWorldTM();
            parent.SetTranslation(AZ::Vector3::CreateZero());
            parent.Invert();

            ModifyEditorTransform(m_editorTransform.m_translate, data, parent);
        }

        void TransformComponent::RotateBy(const AZ::Vector3& data)
        {
            auto parent = GetParentWorldTM();
            parent.SetTranslation(AZ::Vector3::CreateZero());
            parent.Invert();

            ModifyEditorTransform(m_editorTransform.m_rotate, data, parent);
        }

        AZ::EntityId TransformComponent::GetSliceEntityParentId()
        {
            return GetParentId();
        }

        EntityIdList TransformComponent::GetSliceEntityChildren()
        {
            return GetChildren();
        }

        void TransformComponent::BuildGameEntity(AZ::Entity* gameEntity)
        {
            // Transform 2: a transform that serves deploys as a Transform 2 instead of a Transform 1. Its own scale is extended,
            // or its parent serves. It carries the same configuration, and its own signed scale. A child that only inherits
            // passes the vector of its float (ReconcileLoadedScale), and takes the flip of its parent from the chain at
            // runtime. The world is the best fit of the chain. This reads the memo, which at export needs the parents
            // initialized, as the export pass of EditorInfoRemover does. With an old Non-uniform Scale component on the
            // entity the entity does not serve, whatever its scale or its parent is, as it does not in the editor: that
            // component deploys its own runtime component, and both would claim the single non-uniform scale handler.
            if (ServesTransform2())
            {
                AZ::TransformConfig extendedConfiguration;
                extendedConfiguration.m_parentId = m_parentEntityId;
                extendedConfiguration.m_worldTransform = GetWorldTM();
                extendedConfiguration.m_localTransform = GetLocalTM();
                extendedConfiguration.m_parentActivationTransformMode = m_parentActivationTransformMode;
                extendedConfiguration.m_isStatic = m_isStatic;
                extendedConfiguration.m_interpolatePosition = m_interpolatePosition;
                extendedConfiguration.m_interpolateRotation = m_interpolateRotation;

                gameEntity->CreateComponent<AzFramework::Transform2Component>(m_editorTransform.m_authoredScale)
                    ->SetConfiguration(extendedConfiguration);
                return;
            }

            AZ::TransformConfig configuration;
            configuration.m_parentId = m_parentEntityId;
            configuration.m_worldTransform = GetWorldTM();
            configuration.m_localTransform = GetLocalTM();
            configuration.m_parentActivationTransformMode = m_parentActivationTransformMode;
            configuration.m_isStatic = m_isStatic;
            configuration.m_interpolatePosition = m_interpolatePosition;
            configuration.m_interpolateRotation = m_interpolateRotation;

            gameEntity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(configuration);
        }

        void TransformComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
        {
            provided.push_back(AZ_CRC_CE("TransformService"));
        }

        void TransformComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
        {
            incompatible.push_back(AZ_CRC_CE("TransformService"));
        }

        void TransformComponent::PasteOverComponent(const TransformComponent* sourceComponent, TransformComponent* destinationComponent)
        {
            // The world transform only carries the positive stand-in float. A Transform 2 source also hands over its
            // signs and axis ratios, so the change event is held until both have landed and fires once.
            const EditorTransform& source = sourceComponent->m_editorTransform;
            const bool carriesVector = !source.IsTransform1Scale();
            const bool wasSuppressed = destinationComponent->m_suppressTransformChangedEvent;
            destinationComponent->m_suppressTransformChangedEvent = wasSuppressed || carriesVector;

            // When pasting from another transform component, just grab the world transform as that's the part the user is intuitively expecting to bring over
            destinationComponent->SetWorldTM(const_cast<TransformComponent*>(sourceComponent)->GetWorldTM());

            if (carriesVector)
            {
                // The world transform scaled the stand-in by the destination's parent, so scale the vector by the same ratio.
                EditorTransform& destination = destinationComponent->m_editorTransform;
                destination.m_authoredScale = source.m_authoredScale * (destination.m_uniformScale / source.ResolveUniformScale());
                destination.m_uniformScale = destination.ResolveUniformScale();

                destinationComponent->m_suppressTransformChangedEvent = wasSuppressed;
                destinationComponent->TransformChanged();
            }
        }

        void TransformComponent::Reflect(AZ::ReflectContext* context)
        {
            // reflect data for script, serialization, editing..
            if (AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serializeContext->Class<EditorTransform>()->
                    Field("Translate", &EditorTransform::m_translate)->
                    Field("Rotate", &EditorTransform::m_rotate)->
                    Field("Scale", &EditorTransform::m_legacyScale)->
                    Field("Locked", &EditorTransform::m_locked)->
                    Field("UniformScale", &EditorTransform::m_uniformScale)->
                    Field("AuthoredScale", &EditorTransform::m_authoredScale)->
                    Field("DeployExtended", &EditorTransform::m_deployExtended)->
                    // Version 5 changes no data. Cached spawnables are keyed on a fingerprint of the editor types and not of the
                    // runtime ones, so a transform that now deploys differently (a child of an extended parent) needs the bump
                    // to be exported again. Data older than 4 still runs its steps, and version 4 data needs none.
                    Version(5, &Internal::EditorTransformDataConverter);

                serializeContext->Class<Components::TransformComponent, EditorComponentBase>()->
                    Field("Parent Entity", &TransformComponent::m_parentEntityId)->
                    Field("Transform Data", &TransformComponent::m_editorTransform)->
                    Field("Cached World Transform", &TransformComponent::m_cachedWorldTransform)->
                    Field("Cached World Transform Parent", &TransformComponent::m_cachedWorldTransformParent)->
                    Field("Parent Activation Transform Mode", &TransformComponent::m_parentActivationTransformMode)->
                    Field("IsStatic", &TransformComponent::m_isStatic)->
                    Field("InterpolatePosition", &TransformComponent::m_interpolatePosition)->
                    Field("InterpolateRotation", &TransformComponent::m_interpolateRotation)->
                    Version(10, &Internal::TransformComponentDataConverter);

                if (AZ::EditContext* ptrEdit = serializeContext->GetEditContext())
                {
                    ptrEdit->Class<TransformComponent>("Transform", "Controls the placement of the entity in the world in 3d")->
                        ClassElement(AZ::Edit::ClassElements::EditorData, "")->
                            Attribute(AZ::Edit::Attributes::FixedComponentListIndex, 0)->
                            Attribute(AZ::Edit::Attributes::Icon, "Icons/Components/Transform.svg")->
                            Attribute(AZ::Edit::Attributes::ViewportIcon, "Icons/Components/Viewport/Transform.svg")->
                            Attribute(AZ::Edit::Attributes::HelpPageURL, "https://o3de.org/docs/user-guide/components/reference/transform/")->
                            Attribute(ComponentHeaderAttributes::Tag, &TransformComponent::GetHeaderTag)->
                            Attribute(ComponentHeaderAttributes::TagToolTip, Internal::Transform2TagToolTip)->
                            Attribute(AZ::Edit::Attributes::AutoExpand, true)->
                        DataElement(AZ::Edit::UIHandlers::Default, &TransformComponent::m_parentEntityId, "Parent entity", "Modify this using the Entity Outliner")->
                            Attribute(AZ::Edit::Attributes::SliceFlags, AZ::Edit::SliceFlags::DontGatherReference | AZ::Edit::SliceFlags::NotPushableOnSliceRoot)->
                            Attribute(AZ::Edit::Attributes::ShowClearButtonHandler, false)->
                            Attribute(AZ::Edit::Attributes::ShowPickButton, false)->
                            Attribute(AZ::Edit::Attributes::AllowDrop, false)->
                        DataElement(AZ::Edit::UIHandlers::Default, &TransformComponent::m_editorTransform, "Values", "")->
                            Attribute(AZ::Edit::Attributes::ChangeNotify, &TransformComponent::TransformChangedInspector)->
                            Attribute(AZ::Edit::Attributes::AutoExpand, true)->
                        DataElement(AZ::Edit::UIHandlers::ComboBox, &TransformComponent::m_parentActivationTransformMode,
                            "Parent activation", "Configures relative transform behavior when parent activates.")->
                            EnumAttribute(AZ::TransformConfig::ParentActivationTransformMode::MaintainOriginalRelativeTransform, "Original relative transform")->
                            EnumAttribute(AZ::TransformConfig::ParentActivationTransformMode::MaintainCurrentWorldTransform, "Current world transform")->
                        DataElement(AZ::Edit::UIHandlers::Default, &TransformComponent::m_isStatic ,"Static", "Static entities are highly optimized and cannot be moved during runtime.")->
                            Attribute(AZ::Edit::Attributes::ChangeNotify, &TransformComponent::StaticChangedInspector)->
                        DataElement(AZ::Edit::UIHandlers::Default, &TransformComponent::m_cachedWorldTransformParent, "Cached Parent Entity", "")->
                            Attribute(AZ::Edit::Attributes::SliceFlags, AZ::Edit::SliceFlags::DontGatherReference | AZ::Edit::SliceFlags::NotPushable)->
                            Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::Hide)->
                        DataElement(AZ::Edit::UIHandlers::Default, &TransformComponent::m_cachedWorldTransform, "Cached World Transform", "")->
                            Attribute(AZ::Edit::Attributes::SliceFlags, AZ::Edit::SliceFlags::NotPushable)->
                            Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::Hide);

                    ptrEdit->Class<EditorTransform>("Values", "XYZ PYR")->
                        DataElement(AZ::Edit::UIHandlers::Default, &EditorTransform::m_translate, "Translate", "Local Position (Relative to parent) in meters.")->
                            Attribute(AZ::Edit::Attributes::Step, 0.1f)->
                            Attribute(AZ::Edit::Attributes::Suffix, " m")->
                            Attribute(AZ::Edit::Attributes::SliceFlags, AZ::Edit::SliceFlags::NotPushableOnSliceRoot)->
                            Attribute(AZ::Edit::Attributes::ReadOnly, &EditorTransform::m_locked)->
                        DataElement(AZ::Edit::UIHandlers::Default, &EditorTransform::m_rotate, "Rotate", "Local Rotation (Relative to parent) in degrees.")->
                            Attribute(AZ::Edit::Attributes::Step, 1.0f)->
                            Attribute(AZ::Edit::Attributes::Suffix, " deg")->
                            Attribute(AZ::Edit::Attributes::ReadOnly, &EditorTransform::m_locked)->
                            Attribute(AZ::Edit::Attributes::SliceFlags, AZ::Edit::SliceFlags::NotPushableOnSliceRoot)->
                        DataElement(AZ::Edit::UIHandlers::Default, &EditorTransform::m_authoredScale, "Scale", "Local Scale")->
                            Attribute(AZ::Edit::Attributes::Step, 0.1f)->
                            Attribute(AZ::Edit::Attributes::ReadOnly, &EditorTransform::m_locked)->
                            // A Transform 1 scale is saved into UniformScale and AuthoredScale keeps its default, so a prefab
                            // override of it is patched under UniformScale. The label of this row must see that path too.
                            Attribute(AzToolsFramework::Prefab::PrefabOverrideAttributes::AdditionalOverrideField, "UniformScale")->
                        DataElement(AZ::Edit::UIHandlers::Default, &EditorTransform::m_deployExtended, "Deploy as Extended",
                            "Deploys this entity with an extended transform (Transform 2) even at scale 1, 1, 1, so a script can flip it while the game runs. "
                            "Costs more memory and math than the optimized transform.")->
                            Attribute(AZ::Edit::Attributes::ReadOnly, &EditorTransform::m_locked)
                        ;
                }
            }

            if (AZ::BehaviorContext* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
            {
                // string-name differs from class-name to avoid collisions with the other "TransformComponent" (AzFramework::TransformComponent).
                behaviorContext->Class<TransformComponent>("EditorTransformBus")->RequestBus("TransformBus");
            }

            AZ::JsonRegistrationContext* jsonRegistration = azrtti_cast<AZ::JsonRegistrationContext*>(context);
            if (jsonRegistration)
            {
                jsonRegistration->Serializer<JsonTransformComponentSerializer>()->HandlesType<TransformComponent>();
            }
        }

        void TransformComponent::AddContextMenuActions(QMenu* menu)
        {
            bool parentEntityIsReadOnly = false;

            // If the parent entity is marked as read-only, don't allow actions on this transform.
            if (auto readOnlyEntityPublicInterface = AZ::Interface<ReadOnlyEntityPublicInterface>::Get();
                readOnlyEntityPublicInterface->IsReadOnly(GetEntityId()))
            {
                parentEntityIsReadOnly = true;
            }

            if (menu)
            {
                if (!menu->actions().empty())
                {
                    menu->addSeparator();
                }

                QAction* resetAction = menu->addAction(QObject::tr("Reset transform values"), [this]()
                {
                    {
                        AzToolsFramework::ScopedUndoBatch undo("Reset transform values");
                        m_editorTransform.m_translate = AZ::Vector3::CreateZero();
                        m_editorTransform.m_legacyScale = AZ::Vector3::CreateOne();
                        m_editorTransform.m_uniformScale = 1.0f;
                        m_editorTransform.m_authoredScale = AZ::Vector3::CreateOne();
                        m_editorTransform.m_rotate = AZ::Vector3::CreateZero();
                        OnTransformChanged();
                        SetDirty();
                    }

                    InvalidatePropertyDisplay(AzToolsFramework::Refresh_Values);
                });
                resetAction->setEnabled(!m_editorTransform.m_locked && !parentEntityIsReadOnly);

                QString lockString = m_editorTransform.m_locked ? "Unlock transform values" : "Lock transform values";
                QAction* lockAction = menu->addAction(lockString, [this, lockString]()
                {
                    {
                        AzToolsFramework::ScopedUndoBatch undo(lockString.toUtf8().data());
                        m_editorTransform.m_locked = !m_editorTransform.m_locked;
                        SetDirty();
                    }
                    InvalidatePropertyDisplay(AzToolsFramework::Refresh_AttributesAndValues);
                });
                lockAction->setEnabled(!parentEntityIsReadOnly);
            }
        }
    }
} // namespace AzToolsFramework
