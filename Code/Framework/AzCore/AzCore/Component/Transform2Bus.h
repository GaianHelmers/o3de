/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/** @file
 * Header file for the Transform 2 contract: the scale helpers that define where Transform 1 ends,
 * the request bus of an entity with a Transform 2, and the notifications Transform 2 entities raise.
 * The chain math that produces the values these carry is in Transform2Math.h.
 * Nothing here changes AZ::Transform or AZ::TransformBus. Transform 1 entities never raise or handle any of it.
 */

#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Transform.h>

namespace AZ
{
    //////////////////////////////////////////////////////////////////////////
    // Scale helpers: the single definition of the Transform 1 rule
    //
    // AZ::Transform carries one positive float. A scale fits it when all axes are equal and positive: that is a
    // Transform 1 and it keeps its exact value. Any other scale is a Transform 2. The float then only holds a safe
    // stand-in for the systems that read AZ::Transform, and the sign and axis ratios live in the Transform 2.

    namespace Transform2
    {
        //! True when the scale fits Transform 1: all axes equal (within float noise) and positive.
        inline bool FitsTransform1(const Vector3& scale)
        {
            const float x = scale.GetX();
            const float y = scale.GetY();
            const float z = scale.GetZ();
            return x > 0.0f && y > 0.0f && z > 0.0f && IsCloseMag(x, y) && IsCloseMag(x, z);
        }

        //! The float AZ::Transform carries. Transform 1 keeps its exact value, never clamped.
        //! Transform 2 uses its largest magnitude clamped to MinTransformScale..MaxTransformScale,
        //! so it is never negative or zero, because no system reading AZ::Transform survives that.
        inline float UniformStandIn(const Vector3& scale)
        {
            return FitsTransform1(scale)
                ? scale.GetX()
                : GetClamp(scale.GetAbs().GetMaxElement(), MinTransformScale, MaxTransformScale);
        }

        //! True when the axis magnitudes differ, regardless of sign.
        inline bool IsNonUniform(const Vector3& scale)
        {
            const Vector3 magnitude = scale.GetAbs();
            return !IsCloseMag(magnitude.GetX(), magnitude.GetY()) || !IsCloseMag(magnitude.GetX(), magnitude.GetZ());
        }

        //! True when an odd number of axes are negative, which reverses handedness.
        //! Two negative axes are a 180 degree rotation, so they are not mirrored.
        inline bool IsMirrored(const Vector3& scale)
        {
            const int negativeAxes = (scale.GetX() < 0.0f ? 1 : 0) + (scale.GetY() < 0.0f ? 1 : 0) + (scale.GetZ() < 0.0f ? 1 : 0);
            return (negativeAxes & 1) != 0;
        }

        //! The positive per-axis scale left over once the stand-in is applied: |scale| / stand-in.
        //! This is what the non-uniform scale bus serves. A collapsed axis is held just above zero, at the
        //! smallest scale PhysX accepts on a convex mesh, because existing consumers reject or divide by zero.
        inline Vector3 Residual(const Vector3& scale)
        {
            constexpr float MinResidual = 1e-6f;
            const Vector3 residual = scale.GetAbs() / UniformStandIn(scale);
            return residual.GetMax(Vector3(MinResidual));
        }

        //! The signed per-axis scale to apply on top of the stand-in: scale / stand-in.
        //! Carries the sign that the stand-in and the residual cannot, with the same floor as Residual.
        inline Vector3 RenderScale(const Vector3& scale)
        {
            const Vector3 magnitude = Residual(scale);
            return Vector3(
                scale.GetX() < 0.0f ? -magnitude.GetX() : magnitude.GetX(),
                scale.GetY() < 0.0f ? -magnitude.GetY() : magnitude.GetY(),
                scale.GetZ() < 0.0f ? -magnitude.GetZ() : magnitude.GetZ());
        }
    } // namespace Transform2

    //////////////////////////////////////////////////////////////////////////
    // Requests: addressed by entity, one handler. No handler means the entity is a Transform 1.

    //! Requests for the signed per-axis scale of an entity that has a Transform 2.
    //! The local scale is the entity's own authored scale. The world scale and the world matrix are what its
    //! whole chain of parents makes of it, as defined in Transform2Math.h.
    class Transform2Requests
        : public AZ::ComponentBus
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Single;

        virtual ~Transform2Requests() = default;

        //! Gets the signed local scale.
        virtual Vector3 GetLocalScale() const = 0;

        //! Sets the signed local scale. When the stand-in changes it is written to the entity's
        //! Transform 1 float. A pure sign flip writes nothing to Transform 1, so no transform event fires.
        virtual void SetLocalScale(const Vector3& scale) = 0;

        //! Gets the effective signed world scale, sW: the signed scale the entity is drawn at, with every parent applied.
        //! The default returns the local scale, which is right for an entity with no parent that changes it.
        virtual Vector3 GetWorldScale() const { return GetLocalScale(); }

        //! Gets the exact world matrix, W = Wparent * T R S, with the scale, the flips and any shear in it.
        //! The default is the identity.
        virtual Matrix3x4 GetWorldMatrix() const { return Matrix3x4::CreateIdentity(); }

        //! True when the columns of the exact world matrix are not orthogonal, so no rotation and scale can express it.
        //! The Transform 1 view of such an entity is a best fit. The default is false.
        virtual bool IsWorldSheared() const { return false; }

        //! Recomputes the entity from the state of its parent and raises what changed.
        //! A parent calls this on its children when its exact world changed and its Transform 1 view did not,
        //! which is a pure flip. The default does nothing.
        virtual void RefreshFromParent() {}
    };

    using Transform2RequestBus = AZCORE_API AZ::EBus<Transform2Requests>;

    //////////////////////////////////////////////////////////////////////////
    // Notifications: one address, one handler per system. Only Transform 2 entities raise these,
    // so a project with no Transform 2 entity never pays for them.

    //! Notifications raised by entities that have a Transform 2.
    //! A system listens here once and keeps its own state per entity. It must not assume that the
    //! entity's other components exist yet at OnTransform2Activated.
    //!
    //! The scale every notification carries is the effective signed world scale, sW: the signed scale the entity
    //! is drawn at with every parent applied, as defined in Transform2Math.h. From it a listener reads
    //! - Transform2::UniformStandIn(sW), the float of the Transform 1 view of the entity,
    //! - Transform2::Residual(sW), the positive non-uniform answer, and
    //! - the sign of each axis, the flip mask.
    //! For an entity with no parent that changes its scale, and under positive uniform Transform 1 parents,
    //! sW is the entity's own signed scale, so a listener computes what it computes without parents.
    //!
    //! For one entity, one change raises these in this order:
    //! 1. The Transform 1 events, only if the Transform 1 view changed: TransformNotificationBus, then the transform
    //!    changed event. Children that are Transform 2 entities recompute inside this, so none sees a stale parent.
    //! 2. The non-uniform scale changed event, if the residual of sW changed.
    //! 3. OnTransform2WorldChanged, if the Transform 1 view changed, or if the exact world of a sheared entity changed.
    //! 4. OnTransform2ScaleChanged, if sW changed, as Transform2::HasWorldScaleChanged defines it.
    //! 5. RefreshFromParent on each child, if the exact world changed and the Transform 1 view did not (a pure flip).
    //! Steps 3 and 4 are raised while the entity is active. A recompute that changes nothing raises nothing.
    //! The editor transform keeps this order, and raises steps 1 and 3 with every authored change, a pure flip included.
    //! The handlers of one address run last connected first, so a listener must not assume that the
    //! other components of the entity ran before it.
    class Transform2Notifications
        : public AZ::EBusTraits
    {
    public:
        virtual ~Transform2Notifications() = default;

        //! Signals that a Transform 2 entity activated.
        //! @param entityId The entity.
        //! @param scale Its effective signed world scale, sW.
        virtual void OnTransform2Activated([[maybe_unused]] EntityId entityId, [[maybe_unused]] const Vector3& scale) {}

        //! Signals that a Transform 2 entity is deactivating.
        //! @param entityId The entity.
        virtual void OnTransform2Deactivated([[maybe_unused]] EntityId entityId) {}

        //! Signals that the effective signed world scale changed: an axis changed sign, or the scale moved by
        //! more than 1e-5 of its magnitude (Transform2::HasWorldScaleChanged).
        //! Fires after the Transform 1 events and the world notification, if the Transform 1 view changed too.
        //! @param entityId The entity.
        //! @param scale Its new effective signed world scale, sW.
        virtual void OnTransform2ScaleChanged([[maybe_unused]] EntityId entityId, [[maybe_unused]] const Vector3& scale) {}

        //! Signals that the Transform 1 world transform changed, or that the exact world of a sheared entity changed.
        //! Raised after the Transform 1 events, so components that react to TransformNotificationBus have already run.
        //! @param entityId The entity.
        //! @param world The new Transform 1 world transform, the best fit of the chain.
        //! Its scale is the positive stand-in, Transform2::UniformStandIn(scale).
        //! @param scale The entity's effective signed world scale, sW.
        virtual void OnTransform2WorldChanged(
            [[maybe_unused]] EntityId entityId, [[maybe_unused]] const Transform& world, [[maybe_unused]] const Vector3& scale) {}
    };

    using Transform2NotificationBus = AZ::EBus<Transform2Notifications>;
} // namespace AZ

AZ_DECLARE_EBUS_MULTI_ADDRESS(AZCORE_API, AZ::Transform2Requests);
AZ_DECLARE_EBUS_SINGLE_ADDRESS(AZCORE_API, AZ::Transform2Notifications);
