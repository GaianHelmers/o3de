/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Frustum.h>
#include <AzCore/Math/Hemisphere.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Obb.h>
#include <AzCore/Math/ShapeIntersection.h>
#include <AzCore/Math/Sphere.h>
#include <AzCore/Name/NameDictionary.h>
#include <AzCore/std/containers/variant.h>
#include <AzCore/std/limits.h>

#include <Atom/RPI.Public/Culling.h>
#include <Atom/RPI.Public/Scene.h>

namespace AZ::Render::MeshCommon
{

    inline static AZ::Name MeshMovedName = AZ::Name::FromStringLiteral("MeshMoved", AZ::Interface<AZ::NameDictionary>::Get());

    // The DrawListTag name for drawing to MeshMotionVector pass
    inline static AZ::Name MotionDrawListTagName = AZ::Name::FromStringLiteral("motion", AZ::Interface<AZ::NameDictionary>::Get());

    //! Scales a local space Aabb by a per-axis scale that may have negative axes.
    //! A negative axis turns the box inside out (Aabb::MultiplyByScale asserts on that), so the scaled
    //! corners are put back in order instead. With a positive scale the result is the same as MultiplyByScale.
    inline Aabb MultiplyAabbBySignedScale(const Aabb& aabb, const Vector3& scale)
    {
        const Vector3 cornerA = aabb.GetMin() * scale;
        const Vector3 cornerB = aabb.GetMax() * scale;
        return Aabb::CreateFromMinMax(cornerA.GetMin(cornerB), cornerA.GetMax(cornerB));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2: the exact world of a mesh
    //
    // A mesh with an exact world matrix is drawn with a matrix that may mirror an axis and may shear, which the rigid
    // transform and per-axis scale that the transform service reads back cannot say. These two functions are the parts
    // that need no scene, so they are the parts that are tested without one.

    //! The matrix that takes a normal from object space to world space, for the normal buffer of the transform service.
    //! It is the inverse transpose of the 3x3 part of the object to world matrix, up to a positive factor, which is all
    //! the shaders need because they normalize every normal they transform with it. The inverse transpose is the cofactor
    //! matrix divided by the determinant, and the cofactor matrix is still defined where the inverse is not. So a collapsed
    //! axis or a stack of small scales gives a usable matrix, where Matrix3x4::GetInverseFull gives the identity and a warning.
    //! The result is scaled so that its largest element is one, and it has no translation.
    //! The identity is returned for a matrix that has no normal at all: all zero, or with every basis vector on one line.
    inline Matrix3x4 CreateNormalMatrix(const Matrix3x4& objectToWorld)
    {
        // Dividing the basis by its largest element first keeps the cross products in range at huge and at tiny scales.
        const Vector3 absX = objectToWorld.GetBasisX().GetAbs();
        const Vector3 absY = objectToWorld.GetBasisY().GetAbs();
        const Vector3 absZ = objectToWorld.GetBasisZ().GetAbs();
        const float largest = GetMax(GetMax(absX.GetMaxElement(), absY.GetMaxElement()), absZ.GetMaxElement());
        if (!(largest > 0.0f))
        {
            return Matrix3x4::CreateIdentity();
        }

        const float inverseLargest = 1.0f / largest;
        const Vector3 basisX = objectToWorld.GetBasisX() * inverseLargest;
        const Vector3 basisY = objectToWorld.GetBasisY() * inverseLargest;
        const Vector3 basisZ = objectToWorld.GetBasisZ() * inverseLargest;

        // Column i of the cofactor matrix is the cross product of the other two basis vectors.
        const Vector3 normalX = basisY.Cross(basisZ);
        const Vector3 normalY = basisZ.Cross(basisX);
        const Vector3 normalZ = basisX.Cross(basisY);

        const float largestCofactor =
            GetMax(GetMax(normalX.GetAbs().GetMaxElement(), normalY.GetAbs().GetMaxElement()), normalZ.GetAbs().GetMaxElement());
        if (!(largestCofactor >= AZStd::numeric_limits<float>::min()))
        {
            return Matrix3x4::CreateIdentity();
        }

        // The cofactor matrix is the determinant times the inverse transpose, so a mirror (a negative determinant) turns
        // it around. The determinant is the dot product of a basis vector with the cross product of the other two. Zero counts as positive.
        const float sign = basisX.Dot(normalX) < 0.0f ? -1.0f : 1.0f;
        const float scale = sign / largestCofactor;
        return Matrix3x4::CreateFromColumns(normalX * scale, normalY * scale, normalZ * scale, Vector3::CreateZero());
    }

    //! The culling volumes of a mesh drawn with an exact object to world matrix.
    struct ExactCullBounds
    {
        Aabb m_aabb;                        //!< The world box of the local box. Exact for a mirror and for a shear.
        Sphere m_sphere;                    //!< The sphere around m_aabb, as the rigid transform path builds it.
        Obb m_obb;                          //!< m_aabb as a box. Looser than a rotated one, and never wrong.
        float m_lodSelectionRadius = 0.0f;  //!< Half the longest side of the local box scaled by the lengths of the basis vectors.
    };

    //! Builds the culling volumes of a local box under an exact matrix. The Obb is made from the world box, because
    //! Aabb::GetTransformedObb(Matrix3x4) takes a rotation out of the matrix, which only exists when the basis is orthogonal,
    //! and a mirror gives it negative half lengths. A wrong Obb makes visible objects fail the frustum test.
    inline ExactCullBounds CalculateExactCullBounds(const Aabb& localAabb, const Matrix3x4& objectToWorld)
    {
        const Aabb worldAabb = localAabb.GetTransformedAabb(objectToWorld);

        Vector3 center;
        float radius;
        worldAabb.GetAsSphere(center, radius);

        // The lengths of the basis vectors are the scale per axis, without the sign that a mirror gives all three at once.
        const Vector3 axisLengths(
            objectToWorld.GetBasisX().GetLength(), objectToWorld.GetBasisY().GetLength(), objectToWorld.GetBasisZ().GetLength());

        return { worldAabb, Sphere(center, radius), Obb::CreateFromAabb(worldAabb), 0.5f * (localAabb.GetExtents() * axisLengths).GetMaxElement() };
    }

    template <typename BoundsType>
    void MarkMeshesForBounds(AZ::RPI::Scene* scene, const BoundsType& bounds, AZ::RPI::Cullable::FlagType flag)
    {
        AzFramework::IVisibilityScene* visScene = scene->GetVisibilityScene();

        visScene->Enumerate(bounds, [flag, &boundsRef = bounds](const AzFramework::IVisibilityScene::NodeData& nodeData)
            {
                bool nodeIsContainedInBounds = ShapeIntersection::Contains(boundsRef, nodeData.m_bounds);
                for (auto* visibleEntry : nodeData.m_entries)
                {
                    if (visibleEntry->m_typeFlags & AzFramework::VisibilityEntry::TYPE_RPI_Cullable ||
                        visibleEntry->m_typeFlags & AzFramework::VisibilityEntry::TYPE_RPI_VisibleObjectList)
                    {
                        RPI::Cullable* cullable = static_cast<RPI::Cullable*>(visibleEntry->m_userData);

                        if (nodeIsContainedInBounds || ShapeIntersection::Overlaps(boundsRef, cullable->m_cullData.m_boundingSphere))
                        {
                            // This flag is cleared by the mesh feature processor each frame in OnEndPrepareRender()
                            cullable->m_shaderOptionFlags.fetch_or(flag);
                        }
                    }
                }
            }
        );
    }

    using BoundsVariant = AZStd::variant<AZStd::monostate, Sphere, Hemisphere, Frustum, Aabb, Capsule>;

    template <typename BoundsType>
    struct EmptyFilter
    {
        constexpr bool operator()(const BoundsType&) const { return true; }
    };

    template <typename BoundsType, class Filter = EmptyFilter<BoundsType>>
    void MarkMeshesWithFlag(AZ::RPI::Scene* scene, AZStd::span<const BoundsType> boundsCollection, AZ::RPI::Cullable::FlagType flag, Filter filter = {})
    {
        for (const BoundsType& bounds : boundsCollection)
        {
            if (filter(bounds))
            {
                MarkMeshesForBounds(scene, bounds, flag);
            }
        }
    }

    template <class Filter = EmptyFilter<BoundsVariant>>
    void MarkMeshesWithFlag(AZ::RPI::Scene* scene, AZStd::span<const BoundsVariant> boundsCollection, AZ::RPI::Cullable::FlagType flag, Filter filter = {})
    {
        for (const BoundsVariant& bounds : boundsCollection)
        {
            if (filter(bounds))
            {
                if (AZStd::holds_alternative<Sphere>(bounds))
                {
                    MarkMeshesForBounds(scene, AZStd::get<Sphere>(bounds), flag);
                }
                else if (AZStd::holds_alternative<Hemisphere>(bounds))
                {
                    MarkMeshesForBounds(scene, AZStd::get<Hemisphere>(bounds), flag);
                }
                else if (AZStd::holds_alternative<Frustum>(bounds))
                {
                    MarkMeshesForBounds(scene, AZStd::get<Frustum>(bounds), flag);
                }
                else if (AZStd::holds_alternative<Aabb>(bounds))
                {
                    MarkMeshesForBounds(scene, AZStd::get<Aabb>(bounds), flag);
                }
                else if (AZStd::holds_alternative<Capsule>(bounds))
                {
                    MarkMeshesForBounds(scene, AZStd::get<Capsule>(bounds), flag);
                }
            }
        }
    }

}
