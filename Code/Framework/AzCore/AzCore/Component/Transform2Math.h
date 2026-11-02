/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/** @file
 * The chain math of Transform 2: how a transform with a signed, per-axis scale hands its world to its children.
 * Pure functions, shared by the runtime component and the editor so both agree by construction.
 * Every function is total: finite input gives finite output, never NaN, including a zero axis,
 * an all-zero scale and a column scale down to 1e-30.
 * Nothing here changes AZ::Transform or AZ::TransformBus.
 */

#pragma once

#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>

namespace AZ::Transform2
{
    //////////////////////////////////////////////////////////////////////////
    // Why the chain products hold
    //
    // Every transform in a chain has a local scale s that may be negative and may differ per axis. Its exact world is
    // the plain matrix product W = Wparent * T(t) R(q) S(s). That product is what m_world holds, and it is always right.
    // Nothing below factors a matrix, so there is no rotation to fit, no drift and no NaN on a collapsed axis.
    //
    // Write the signs of a parent as a sign matrix D (entries +1 or -1). A parent whose columns are orthogonal with
    // equal lengths is Rp * D * up: a rotation, the signs, and one positive number. Put a child under it:
    //
    //     W = Rp D up Rq S = Rp (D Rq D) (up D S)
    //
    // because a sign matrix squared is the identity, so the D that was pushed through Rq is cancelled by a second one.
    // A rotation conjugated by a sign matrix is again a rotation, with the same angle and a mirrored axis: that is
    // ConjugateBySigns. So the new rotation is the parent's rotation times the conjugated local rotation, the new
    // signs are the parent's signs times the signs of s, and the new signed scale is up * parent signs * s.
    //
    // What follows from it:
    // - A child with an identity local under a parent flipped on any axis has that parent's rotation and that
    //   parent's sign mask, the same as if it were flipped itself.
    // - Nothing jumps by a half turn when an ancestor flips. The mask is tracked, never read back from the matrix.
    // - A double mirror cancels, because signs multiply per axis.
    // - On a parent with unequal axis lengths a rotated child shears the world. The rotation then stays the authored
    //   product (the rule Unity uses for `rotation`, plus the conjugation Unity lacks) and the shear stays in m_world,
    //   which is exact, so only the flags and the column lengths describe it.

    //////////////////////////////////////////////////////////////////////////
    // Chain state

    //! What a transform hands to its children. Default constructed, it is the state of no parent: an identity.
    struct ChainState
    {
        //! The exact world, W = Wparent * T(t) R(q) S(s).
        Matrix3x4 m_world = Matrix3x4::CreateIdentity();

        //! The rotation of the Transform 1 view. Not normalized, as the Transform 1 composition is not.
        Quaternion m_rotation = Quaternion::CreateIdentity();

        //! The product of the sign of every scale down the chain, +1 or -1 per axis. Zero counts as positive.
        Vector3 m_signs = Vector3::CreateOne();

        //! The effective signed world scale, sW.
        Vector3 m_worldScale = Vector3::CreateOne();

        //! True when the columns of m_world are orthogonal with equal lengths.
        bool m_conformal = true;

        //! True when the columns of m_world are not orthogonal.
        bool m_sheared = false;
    };

    //////////////////////////////////////////////////////////////////////////
    // Building a chain

    //! The state a Transform 1 parent hands to its children, or an identity state for no parent.
    AZCORE_API ChainState ChainFromTransform(const Transform& t1World);

    //! The state of a child with authored translation t, rotation q and signed scale s under a parent state.
    //! The parent is the only input read: nothing walks the chain above it.
    AZCORE_API ChainState ComposeChain(const ChainState& parent, const Vector3& t, const Quaternion& q, const Vector3& s);

    //! The Transform 1 view of a chain: the translation of the world, the chain rotation and
    //! UniformStandIn of the world scale. UniformStandIn is unclamped where the scale fits Transform 1,
    //! so (this float, RenderScale of the world scale) multiplies back to the world whenever the chain is not sheared.
    AZCORE_API Transform BestFit(const ChainState& chain);

    //! Conjugates a rotation by a sign matrix: (s.x*d*q.x, s.y*d*q.y, s.z*d*q.z, q.w) with d = s.x*s.y*s.z.
    //! It is its own inverse. For (-1,1,1) it negates y and z.
    AZCORE_API Quaternion ConjugateBySigns(const Quaternion& q, const Vector3& signs);

    //////////////////////////////////////////////////////////////////////////
    // Solving authored values from a requested world

    //! The authored local translation that puts a child of the parent at the requested world translation.
    AZCORE_API Vector3 SolveLocalTranslation(const ChainState& parent, const Vector3& worldTranslation);

    //! The authored local rotation that gives a child of the parent the requested Transform 1 view rotation.
    AZCORE_API Quaternion SolveLocalRotation(const ChainState& parent, const Quaternion& worldRotation);

    //! The inverse of an affine matrix that survives tiny scales, mirrors and shear. Never the identity as a fallback
    //! and never NaN. The singular test is relative to the column lengths, unlike Matrix3x4::GetInverseFull, whose
    //! absolute test returns the identity below a uniform scale of about 0.0049.
    //! A relatively singular matrix gets the best available inverse, in which a collapsed axis inverts to 0.
    AZCORE_API Matrix3x4 InverseRelative(const Matrix3x4& m);

    //////////////////////////////////////////////////////////////////////////
    // Scale tests

    //! True when the axis magnitudes of a world scale differ by more than 1e-4 of the largest, regardless of sign.
    AZCORE_API bool IsNonUniformEffective(const Vector3& worldScale);

    //! True when a world scale changed sign on any axis, or moved by more than 1e-5 of its magnitude.
    AZCORE_API bool HasWorldScaleChanged(const Vector3& before, const Vector3& after);
} // namespace AZ::Transform2
