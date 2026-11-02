/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Memory/SystemAllocator.h>

namespace Multiplayer
{
    //! The Multiplayer side of Transform 2, owned by MultiplayerSystemComponent.
    //!
    //! Replication carries a rotation, a translation and one float, so the sign and the axis ratios of a Transform 2
    //! scale never reach the other side. A flip made at runtime would show on the machine that made it only, with no
    //! message. This reports every network entity that has a Transform 2, so that loss is not silent.
    //!
    //! Only Transform 2 entities raise the notification it handles, so a Transform 1 entity costs nothing.
    //!
    //! In the editor, an authored entity holds its Network Binding component inside a generic component wrapper, which is
    //! not a NetBindComponent, so authoring reports nothing. The report comes when the entity is built for game or play mode.
    class Transform2NetReporter
        : private AZ::Transform2NotificationBus::Handler
    {
    public:
        AZ_CLASS_ALLOCATOR(Transform2NetReporter, AZ::SystemAllocator);

        Transform2NetReporter();
        ~Transform2NetReporter();

    private:
        // Transform2NotificationBus::Handler overrides...
        void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override;
    };
} // namespace Multiplayer
