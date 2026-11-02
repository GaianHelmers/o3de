/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/unordered_map.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>

namespace LmbrCentral
{
    //////////////////////////////////////////////////////////////////////////
    // Transform 2 shape support
    //
    // The system listener that pushes the flip of a Transform 2 entity to its shapes. It is owned by the LmbrCentral system
    // component, so the Editor and the game run the same code, and only Transform 2 entities ever raise what it listens to.
    // A Transform 1 entity has no handler, no bus connection and no state here.
    //
    // The Transform 2 activates before the shapes of its entity, so the mask is only pushed once the whole entity is active.
    // Each push is followed by ShapeComponentNotifications::OnShapeFlipChanged, for whatever mirrors with the flip without the
    // shape's own answer moving (a gradient pattern).
    class Transform2ShapeSupport
        : public AZ::Transform2NotificationBus::Handler
        , public AZ::EntityBus::MultiHandler
    {
    public:
        AZ_CLASS_ALLOCATOR(Transform2ShapeSupport, AZ::SystemAllocator);

        Transform2ShapeSupport();
        ~Transform2ShapeSupport() override;

    private:
        //! What is known about one Transform 2 entity.
        struct EntityState
        {
            AZ::u8 m_flipMask = 0; //!< The flip of the entity's effective signed world scale.
            //! The mask last sent to the shapes of the entity. A shape drops its flip when it deactivates, so this is forgotten
            //! with it (OnEntityDeactivated). A stale value here would take the push after a re-activation for a repeat, and
            //! the shape would stay unflipped.
            AZ::u8 m_pushedMask = 0;
            bool m_entityActive = false; //!< True once the entity finished activating, so its shapes exist.
        };

        // AZ::Transform2NotificationBus::Handler overrides...
        void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override;
        void OnTransform2Deactivated(AZ::EntityId entityId) override;
        void OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale) override;

        // AZ::EntityBus::MultiHandler overrides...
        void OnEntityActivated(const AZ::EntityId& entityId) override;
        void OnEntityDeactivated(const AZ::EntityId& entityId) override;

        //! Sends the mask to the shapes of the entity and announces the flip, unless they already have it.
        void Push(AZ::EntityId entityId, EntityState& state);

        AZStd::unordered_map<AZ::EntityId, EntityState> m_entities;
    };
} // namespace LmbrCentral
