/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/Transform2Math.h>

namespace AZ::Transform2
{
    //////////////////////////////////////////////////////////////////////////
    // Tolerances

    namespace
    {
        // World scales whose axes differ by less than this fraction of the largest are one uniform scale.
        constexpr float UniformScaleTolerance = 1e-4f;

        // A world scale that moved by more than this fraction of its magnitude has changed.
        constexpr float ScaleChangedTolerance = 1e-5f;

        // Two unit columns whose dot product is above this are not orthogonal.
        constexpr float ShearTolerance = 1e-4f;

        // A matrix whose unit columns have a determinant at or below this is relatively singular.
        // Harness check 7c: the f32 inverse error is about 1.2e-7 / determinant, so 1e-5 admits an error near 1e-2.
        constexpr float SingularDeterminant = 1e-5f;

        // A column shorter than this is a collapsed axis. Its reciprocal times the 1e5 an inverse of a
        // non-singular unit matrix can reach still fits a float.
        constexpr float CollapsedLength = 1e-32f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Column helpers

    namespace
    {
        // The sign of each axis, +1 or -1. Zero counts as positive.
        Vector3 SignsOf(const Vector3& v)
        {
            return Vector3(v.GetX() < 0.0f ? -1.0f : 1.0f, v.GetY() < 0.0f ? -1.0f : 1.0f, v.GetZ() < 0.0f ? -1.0f : 1.0f);
        }

        // The length of a vector, scaled by its largest element first so that the squares neither underflow nor overflow.
        float ScaledLength(const Vector3& v)
        {
            const float largest = v.GetAbs().GetMaxElement();
            return largest > 0.0f ? largest * (v / largest).GetLength() : 0.0f;
        }

        // The columns of a matrix as lengths and unit vectors. A collapsed column has a zero unit vector.
        struct ColumnSplit
        {
            Vector3 m_unit[3];
            float m_length[3];
        };

        ColumnSplit SplitColumns(const Matrix3x4& m)
        {
            ColumnSplit split;
            for (int32_t axis = 0; axis < 3; ++axis)
            {
                const Vector3 column = m.GetColumn(axis);
                split.m_length[axis] = ScaledLength(column);
                split.m_unit[axis] = split.m_length[axis] < CollapsedLength ? Vector3::CreateZero() : column / split.m_length[axis];
            }
            return split;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Building a chain

    ChainState ChainFromTransform(const Transform& t1World)
    {
        ChainState chain;
        chain.m_world = Matrix3x4::CreateFromTransform(t1World);
        chain.m_rotation = t1World.GetRotation();
        chain.m_worldScale = Vector3(t1World.GetUniformScale());
        return chain;
    }

    ChainState ComposeChain(const ChainState& parent, const Vector3& t, const Quaternion& q, const Vector3& s)
    {
        Matrix3x4 local = Matrix3x4::CreateFromQuaternionAndTranslation(q, t);
        local.MultiplyByScale(s);

        ChainState chain;
        chain.m_world = parent.m_world * local;
        chain.m_signs = parent.m_signs * SignsOf(s);
        chain.m_rotation = parent.m_rotation * ConjugateBySigns(q, parent.m_signs);

        if (parent.m_conformal)
        {
            // Arithmetic on the scales, so a root reports its own scale bit for bit
            // and a uniform positive chain reports the Transform 1 product.
            chain.m_worldScale = parent.m_signs * GetAbs(parent.m_worldScale.GetX()) * s;
            chain.m_conformal = !IsNonUniformEffective(s);
            chain.m_sheared = false;
        }
        else
        {
            const ColumnSplit split = SplitColumns(chain.m_world);
            chain.m_worldScale = chain.m_signs * Vector3(split.m_length[0], split.m_length[1], split.m_length[2]);
            chain.m_conformal = false;
            chain.m_sheared = GetAbs(split.m_unit[0].Dot(split.m_unit[1])) > ShearTolerance
                || GetAbs(split.m_unit[0].Dot(split.m_unit[2])) > ShearTolerance
                || GetAbs(split.m_unit[1].Dot(split.m_unit[2])) > ShearTolerance;
        }
        return chain;
    }

    Transform BestFit(const ChainState& chain)
    {
        Transform bestFit = Transform::CreateFromQuaternionAndTranslation(chain.m_rotation, chain.m_world.GetTranslation());
        bestFit.SetUniformScale(UniformStandIn(chain.m_worldScale));
        return bestFit;
    }

    Quaternion ConjugateBySigns(const Quaternion& q, const Vector3& signs)
    {
        const float determinant = signs.GetX() * signs.GetY() * signs.GetZ();
        return Quaternion(
            signs.GetX() * determinant * q.GetX(),
            signs.GetY() * determinant * q.GetY(),
            signs.GetZ() * determinant * q.GetZ(),
            q.GetW());
    }

    //////////////////////////////////////////////////////////////////////////
    // Solving authored values from a requested world

    Vector3 SolveLocalTranslation(const ChainState& parent, const Vector3& worldTranslation)
    {
        return InverseRelative(parent.m_world).TransformPoint(worldTranslation);
    }

    Quaternion SolveLocalRotation(const ChainState& parent, const Quaternion& worldRotation)
    {
        // The parent rotation can only be zero in a state that was not built by this file. Skip the inverse then.
        const bool invertible = parent.m_rotation.GetLengthSq() > 0.0f;
        const Quaternion relative = invertible ? parent.m_rotation.GetInverseFull() * worldRotation : worldRotation;
        return ConjugateBySigns(relative, parent.m_signs);
    }

    Matrix3x4 InverseRelative(const Matrix3x4& m)
    {
        // m = N * D, with N the unit columns and D the column lengths. The inverse is D^-1 * N^-1.
        const ColumnSplit split = SplitColumns(m);
        const Vector3& unit0 = split.m_unit[0];
        const Vector3& unit1 = split.m_unit[1];
        const Vector3& unit2 = split.m_unit[2];

        // The rows of N^-1 are the cross products of the other two columns, over the determinant of N.
        // That determinant is relative: N has unit columns, so it is 1 for a rotation and falls to 0 as two
        // columns become parallel or a column collapses.
        Vector3 rows[3];
        const float determinant = unit0.Dot(unit1.Cross(unit2));
        if (GetAbs(determinant) > SingularDeterminant)
        {
            rows[0] = unit1.Cross(unit2) / determinant;
            rows[1] = unit2.Cross(unit0) / determinant;
            rows[2] = unit0.Cross(unit1) / determinant;
        }
        else
        {
            // Relatively singular: take the transpose of N, which is exact when the columns are orthogonal.
            rows[0] = unit0;
            rows[1] = unit1;
            rows[2] = unit2;
        }

        Matrix3x4 inverse = Matrix3x4::CreateIdentity();
        const Vector3 translation = m.GetTranslation();
        for (int32_t axis = 0; axis < 3; ++axis)
        {
            const bool collapsed = split.m_length[axis] < CollapsedLength;
            const Vector3 row = collapsed ? Vector3::CreateZero() : rows[axis] / split.m_length[axis];
            inverse.SetRow(axis, row, -row.Dot(translation));
        }
        return inverse;
    }

    //////////////////////////////////////////////////////////////////////////
    // Scale tests

    bool IsNonUniformEffective(const Vector3& worldScale)
    {
        const Vector3 magnitude = worldScale.GetAbs();
        return magnitude.GetMaxElement() - magnitude.GetMinElement() > UniformScaleTolerance * magnitude.GetMaxElement();
    }

    bool HasWorldScaleChanged(const Vector3& before, const Vector3& after)
    {
        for (int32_t axis = 0; axis < 3; ++axis)
        {
            const float oldValue = before.GetElement(axis);
            const float newValue = after.GetElement(axis);
            if ((oldValue < 0.0f) != (newValue < 0.0f))
            {
                return true;
            }

            const float oldMagnitude = GetAbs(oldValue);
            const float newMagnitude = GetAbs(newValue);
            if (GetAbs(oldMagnitude - newMagnitude) > ScaleChangedTolerance * GetMax(oldMagnitude, newMagnitude))
            {
                return true;
            }
        }
        return false;
    }
} // namespace AZ::Transform2
