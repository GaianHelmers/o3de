/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Transform2ShapeSupport.h"

namespace LmbrCentral
{
    //////////////////////////////////////////////////////////////////////////
    // Lifetime

    Transform2ShapeSupport::Transform2ShapeSupport()
    {
        AZ::Transform2NotificationBus::Handler::BusConnect();
    }

    Transform2ShapeSupport::~Transform2ShapeSupport()
    {
        AZ::Transform2NotificationBus::Handler::BusDisconnect();
        AZ::EntityBus::MultiHandler::BusDisconnect();
        m_entities.clear();
    }

    //////////////////////////////////////////////////////////////////////////
    // Notifications of a Transform 2 entity

    void Transform2ShapeSupport::OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale)
    {
        EntityState& state = m_entities[entityId];
        state.m_flipMask = FlipMaskFromScale(scale);

        if (state.m_entityActive)
        {
            // Announced again on an entity that is already live, so its shapes have to see the flip.
            Push(entityId, state);
        }
        else if (!AZ::EntityBus::MultiHandler::BusIsConnectedId(entityId))
        {
            // The shapes do not exist yet while the entity activates. Wait for the entity to finish. If it is already
            // active, connecting calls OnEntityActivated at once.
            AZ::EntityBus::MultiHandler::BusConnect(entityId);
        }
    }

    void Transform2ShapeSupport::OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        found->second.m_flipMask = FlipMaskFromScale(scale);
        if (found->second.m_entityActive)
        {
            Push(entityId, found->second);
        }
    }

    void Transform2ShapeSupport::OnTransform2Deactivated(AZ::EntityId entityId)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        // An entity that lives on after leaving Transform 2 gets its authored shapes back. When the entity is going away
        // its shapes have already dropped the flip, and nothing answers the push.
        if (found->second.m_entityActive)
        {
            found->second.m_flipMask = 0;
            Push(entityId, found->second);
        }

        AZ::EntityBus::MultiHandler::BusDisconnect(entityId);

        // The push reaches code that is not ours, which may add entities and invalidate the iterator, so erase by key.
        m_entities.erase(entityId);
    }

    //////////////////////////////////////////////////////////////////////////
    // Notifications of the entity

    void Transform2ShapeSupport::OnEntityActivated(const AZ::EntityId& entityId)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        found->second.m_entityActive = true;
        Push(entityId, found->second);
    }

    void Transform2ShapeSupport::OnEntityDeactivated(const AZ::EntityId& entityId)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        // Raised before the components deactivate, and every shape drops its flip when it does. The entity may come back with
        // the same scale and no Transform 2 notification in between, so what was pushed is forgotten here.
        found->second.m_entityActive = false;
        found->second.m_pushedMask = 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Pushing the mask

    void Transform2ShapeSupport::Push(AZ::EntityId entityId, EntityState& state)
    {
        // A squash animation with an unchanged sign costs a map find and no bus dispatch.
        const AZ::u8 flipMask = state.m_flipMask;
        if (flipMask == state.m_pushedMask)
        {
            return;
        }

        // The pushed mask is set before anything is sent, so nothing that answers the shape's event can push again and loop.
        state.m_pushedMask = flipMask;
        ShapeComponentRequestsBus::Event(entityId, &ShapeComponentRequests::SetFlipMask, flipMask);

        // A shape raises OnShapeChanged only when its own answer moved, and the flip of a shape on the entity's origin moves nothing.
        // Data authored in the local axes, such as a gradient pattern, mirrors with the flip anyway, so it is told here. This is
        // only reached when the mask changed, and only a Transform 2 entity gets here.
        ShapeComponentNotificationsBus::Event(entityId, &ShapeComponentNotifications::OnShapeFlipChanged);
    }
} // namespace LmbrCentral
