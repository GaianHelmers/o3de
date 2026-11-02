/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Transform2NetReporter.h>

#include <Multiplayer/Components/NetBindComponent.h>

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/Interface/Interface.h>

namespace Multiplayer
{
    //////////////////////////////////////////////////////////////////////////
    // Transform2NetReporter

    Transform2NetReporter::Transform2NetReporter()
    {
        AZ::Transform2NotificationBus::Handler::BusConnect();
    }

    Transform2NetReporter::~Transform2NetReporter()
    {
        AZ::Transform2NotificationBus::Handler::BusDisconnect();
    }

    void Transform2NetReporter::OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale)
    {
        // The scale is the effective world scale, and every descendant of an extended entity is a Transform 2. One that is
        // uniform and positive replicates through the Transform 1 float as it always did, so there is nothing to report.
        if (AZ::Transform2::FitsTransform1(scale))
        {
            return;
        }

        // The other components of the entity may not be active yet, so ask what the entity holds, not what is running.
        // A component the entity holds is found from Init on.
        AZ::ComponentApplicationRequests* application = AZ::Interface<AZ::ComponentApplicationRequests>::Get();
        const AZ::Entity* entity = application ? application->FindEntity(entityId) : nullptr;
        if (entity && entity->FindComponent<NetBindComponent>())
        {
            AZ_Error("Multiplayer", false,
                "Network entity '%s' (%s) has a Transform 2 scale (%.3f, %.3f, %.3f). A Transform 2 scale does not replicate: "
                "only rotation, translation and one uniform scale do, so a flip or non-uniform scale is not seen by the other "
                "side. Use a uniform positive scale on network entities.",
                entity->GetName().c_str(), entityId.ToString().c_str(), scale.GetX(), scale.GetY(), scale.GetZ());
        }
    }
} // namespace Multiplayer
