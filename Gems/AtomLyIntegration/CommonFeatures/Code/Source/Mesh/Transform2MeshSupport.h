/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>

namespace AZ
{
    namespace Render
    {
        //! The mesh side of Transform 2, owned by AtomLyIntegrationCommonFeaturesSystemComponent.
        //!
        //! The mesh components only ever see the positive Transform 1 scale. This listens to the entities that have a Transform 2
        //! and gives their mesh the sign back: the signed render scale goes into the mesh transform, and the mesh is marked mirrored
        //! when the handedness is reversed. When a Transform 2 entity leaves Transform 2 the mesh is restored.
        //!
        //! A world that is sheared (the columns of its matrix are not orthogonal, which a non-uniform parent gives a rotated child)
        //! is the one thing the transform and the signed scale cannot say. The entity then answers its exact world matrix on
        //! the Transform2RequestBus, and the mesh is drawn with it through MeshFeatureProcessorInterface::SetExactWorld. The transform
        //! stays the best fit, which is what ray tracing keeps (a stated limit). When the shear ends, or the entity leaves Transform 2,
        //! the same call lowers the exact path again. A world with a collapsed axis takes the exact path too, because the normal matrix
        //! of the best fit is an inverse, which a determinant that small defeats. Every other world, including a mirror, is drawn from
        //! the best fit and the render scale.
        //!
        //! The motion vector history of a mesh is ended when the flip it holds changes on any axis, and only then: it covers a flip of
        //! two axes at once, and a flip that reaches a child through its parent, where the world scale of the child changes sign.
        //!
        //! The signed scale is also handed to whoever answers on the MeshHandleStateRequestBus, so the bounds, the selection box and
        //! the ray tests of a mesh component sit on the drawn side. A flip fires no Transform 1 event, so when the scale changes the
        //! bounds of the entity are asked to be calculated again.
        //!
        //! The readers that cache what they took from the mesh (the surface data of a mesh, the vegetation mesh blocker) read the
        //! signed scale through MeshComponentRequests::GetRenderScale. A pure flip changes neither the Transform 1 world nor the
        //! positive non-uniform scale, so nothing else tells them. When the flip the mesh holds really changes, which includes the
        //! push back to no flip when a live entity leaves Transform 2, the listener raises
        //! MeshComponentNotificationBus::OnRenderScaleChanged for the entity, and they read again. Both of them handle that bus
        //! already, so a Transform 1 entity gains no handler or connection for it.
        //!
        //! A producer may announce its mesh before it has set the transform on it, as the White Box does when it rebuilds, and
        //! would then overwrite what was pushed on the announcement. So that push is made once more on the next tick.
        //!
        //! The same listener gives the area light and the decal of a Transform 2 entity the signed scale, through the new
        //! SetRenderScale on their request buses. A light without a shape (the simple spot) and a decal keep what they are given,
        //! and send it again with every later refresh. A move writes the unflipped transform first, so the listener pushes again
        //! after each move, as it does for the mesh. The shape lights take the flip from their shape and ignore it.
        //!
        //! It keeps state, and connects to the mesh handle, area light and decal notifications, for Transform 2 entities only, so a
        //! Transform 1 entity costs nothing. It works the same in the editor and in game, and it covers every entity that answers
        //! on the MeshHandleStateRequestBus (mesh components and actors), the AreaLightRequestBus and the DecalRequestBus.
        class Transform2MeshSupport
            : private AZ::Transform2NotificationBus::Handler
            , private AZ::TickBus::Handler
        {
        public:
            AZ_CLASS_ALLOCATOR(Transform2MeshSupport, AZ::SystemAllocator);

            Transform2MeshSupport();
            ~Transform2MeshSupport();

        private:
            // Transform2NotificationBus::Handler overrides...
            void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override;
            void OnTransform2Deactivated(AZ::EntityId entityId) override;
            void OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale) override;
            void OnTransform2WorldChanged(AZ::EntityId entityId, const AZ::Transform& world, const AZ::Vector3& scale) override;

            // AZ::TickBus::Handler overrides...
            void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
            int GetTickOrder() override;

            // The Transform 2 scale and the mesh handle notifications of one entity. Defined in the cpp.
            class EntityMesh;

            //! Asks for the mesh of an entity to be pushed again on the next tick, before the frame is drawn. The tick bus is
            //! connected only while there is something waiting.
            void QueueRefresh(AZ::EntityId entityId);

            AZStd::unordered_map<AZ::EntityId, AZStd::unique_ptr<EntityMesh>> m_entityMeshes;
            AZStd::vector<AZ::EntityId> m_pendingRefresh;
        };
    } // namespace Render
} // namespace AZ
