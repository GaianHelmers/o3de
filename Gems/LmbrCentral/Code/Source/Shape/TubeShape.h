/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/TransformBus.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/parallel/shared_mutex.h>
#include <Cry_Math.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>
#include <LmbrCentral/Shape/TubeShapeComponentBus.h>
#include <LmbrCentral/Shape/SplineComponentBus.h>
#include <LmbrCentral/Shape/SplineAttribute.h>

#include "ShapeGeometryUtil.h"

namespace LmbrCentral
{
    class TubeShape
        : public ShapeComponentRequestsBus::Handler
        , public TubeShapeComponentRequestsBus::Handler
        , private SplineComponentNotificationBus::Handler
        , private AZ::TransformNotificationBus::Handler
    {
    public:
        AZ_RTTI(TubeShape, "{8DF865F3-D155-402D-AF64-9342CE9E9E60}")
        AZ_CLASS_ALLOCATOR(TubeShape, AZ::SystemAllocator)

        TubeShape() = default;
        TubeShape(const TubeShape& rhs);
        static void Reflect(AZ::SerializeContext& context);

        void Activate(AZ::EntityId entityId);
        void Deactivate();

        // ShapeComponentRequestsBus
        AZ::Crc32 GetShapeType() const override { return AZ_CRC_CE("Tube"); }
        AZ::Aabb GetEncompassingAabb() const override;
        void GetTransformAndLocalBounds(AZ::Transform& transform, AZ::Aabb& bounds) const override;
        bool IsPointInside(const AZ::Vector3& point) const  override;
        float DistanceFromPoint(const AZ::Vector3& point) const override;
        float DistanceSquaredFromPoint(const AZ::Vector3& point) const override;
        bool IntersectRay(const AZ::Vector3& src, const AZ::Vector3& dir, float& distance) const override;

        // ShapeComponentRequestsBus, Transform 2 flip. The spline is the one SplineComponent saves, so it stays authored and
        // the queries are mirrored instead.
        void SetFlipMask(AZ::u8 flipMask) override;
        AZ::Vector3 GetFlipSigns() const override;
        void GetTransformAndAuthoredLocalBounds(AZ::Transform& transform, AZ::Aabb& bounds) const override;

        // TubeShapeComponentRequestsBus
        void SetRadius(float radius) override;
        float GetRadius() const override;
        float GetVariableRadius(int vertIndex) const override;
        void SetVariableRadius(int vertIndex, float radius) override;
        void SetAllVariableRadii(float radius) override;
        float GetTotalRadius(const AZ::SplineAddress& address) const override;
        const SplineAttribute<float>& GetRadiusAttribute() const override;

        AZ::SplinePtr GetSpline();
        AZ::ConstSplinePtr GetConstSpline() const;
        const AZ::Transform& GetCurrentTransform() const { return m_currentTransform; }

        /// The flip mask of the entity. Deactivate drops it, so a Deactivate and Activate pair keeps it with this and RestoreFlipMask.
        AZ::u8 GetFlipMask() const { return m_flipMask; }
        /// Puts a flip back after a Deactivate and Activate pair. Nobody saw the flip go, so this raises no shape change.
        void RestoreFlipMask(AZ::u8 flipMask);

    private:
        // TransformNotificationBus
        void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override;

        // SplineComponentNotificationBus
        void OnSplineChanged() override;

        // Helpers to ensure that a negative radius cannot be set
        void BaseRadiusChanged();
        void VariableRadiusChanged(size_t vertIndex);
        void ValidateAllVariableRadii();
        void ValidateVariableRadius(size_t vertIndex);

        // Transform 2 flip: a local vector or local bounds as the flipped tube sees them. The caller holds the lock. A tube that is
        // not flipped pays this one byte test.
        AZ::Vector3 Flip(const AZ::Vector3& localVector) const
        {
            return m_flipMask ? localVector * FlipMaskToSigns(m_flipMask) : localVector;
        }
        AZ::Aabb Flip(const AZ::Aabb& localBounds) const;

        mutable AZ::SplinePtr m_spline; ///< Spline pointer.
        SplineAttribute<float> m_variableRadius; ///< Variable radius defined at each spline point.
        AZ::u8 m_flipMask = 0; ///< Transform 2 flip of the entity (X=1, Y=2, Z=4). Declared here to sit in padding, so the size is unchanged.
        AZ::Transform m_currentTransform; ///< Caches the current World transform.
        AZ::EntityId m_entityId; ///< The Id of the entity the shape is attached to.
        float m_radius = 1.f; ///< Radius of the tube.
        mutable AZStd::shared_mutex m_mutex; ///< Mutex to allow multiple readers but single writer for efficient thread safety
    };

    /// Generates a Tube mesh with filled surface and outlines.
    void GenerateTubeMesh(
        const AZ::SplinePtr& spline, const SplineAttribute<float>& variableRadius,
        float radius, AZ::u32 capSegments, AZ::u32 sides, AZStd::vector<AZ::Vector3>& vertexBufferOut,
        AZStd::vector<AZ::u32>& indexBufferOut, AZStd::vector<AZ::Vector3>& lineBufferOut);

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 flip helpers

    /// Mirrors the points of a generated tube mesh: the vertices and the lines. The indices stay, only the points follow a flip.
    /// The mesh is built from the authored spline and its authored normals, so the mirrored points are the mirrored authored tube.
    void MirrorTubeMeshPoints(const AZ::Vector3& flipSigns, ShapeMesh& tubeShapeMesh);

    /// Mirrors a world space ray about an entity with W * diag(flipSigns) * W^-1. That map is an isometry and its own inverse, so
    /// a distance along the ray is unchanged, and the mirrored ray against the authored spline is the ray against the mirrored one.
    void MirrorRayAboutEntity(
        const AZ::Transform& worldFromLocal, const AZ::Vector3& flipSigns, AZ::Vector3& rayOrigin, AZ::Vector3& rayDirection);

    /// Configuration for how TubeShape debug drawing should appear (tesselation parameters etc).
    struct TubeShapeMeshConfig
    {
        AZ_CLASS_ALLOCATOR(TubeShapeMeshConfig, AZ::SystemAllocator)
        AZ_RTTI(TubeShapeMeshConfig, "{90791900-060F-4F0B-9552-D6E67572B317}")

        TubeShapeMeshConfig() = default;
        virtual ~TubeShapeMeshConfig() = default;

        static void Reflect(AZ::ReflectContext* context);

        AZ::u32 m_endSegments = 9; ///< The number of endcap segments displayed in the editor.
        AZ::u32 m_sides = 32; ///< The number of sides of the tube displayed in the editor.

        ShapeComponentConfig m_shapeComponentConfig;
    };
} // namespace LmbrCentral
