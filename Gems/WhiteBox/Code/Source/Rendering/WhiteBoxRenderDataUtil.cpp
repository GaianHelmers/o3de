/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WhiteBoxRenderData.h"
#include "WhiteBoxRenderDataUtil.h"
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/TransformBus.h>

namespace WhiteBox
{
    AzFramework::VisibleGeometry BuildVisibleGeometryFromWhiteBoxRenderData(
        const AZ::EntityId& entityId, const WhiteBoxRenderData& renderData)
    {
        // Calculate the world transform for the entity which will be used to transform the white box geometry
        AZ::Transform transform = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(transform, entityId, &AZ::TransformBus::Events::GetWorldTM);

        AZ::Vector3 nonUniformScale = AZ::Vector3::CreateOne();
        AZ::NonUniformScaleRequestBus::EventResult(nonUniformScale, entityId, &AZ::NonUniformScaleRequests::GetScale);

        // Initialize the visible geometry structure by reserving memory for mesh data and setting its world transform
        const WhiteBoxFaces faceData = BuildCulledWhiteBoxFaces(renderData.m_faces);
        const size_t faceCount = faceData.size();
        const size_t vertCount = faceCount * 3;

        AzFramework::VisibleGeometry geometry;
        geometry.m_vertices.reserve(vertCount * 3);
        geometry.m_indices.reserve(vertCount);
        geometry.m_transform = AZ::Matrix4x4::CreateFromTransform(transform);
        geometry.m_transform *= AZ::Matrix4x4::CreateScale(nonUniformScale);

        // A White Box with a negative scale axis (Transform 2) is drawn mirrored about its entity, and the geometry of it is what is
        // drawn: the mesh keeps the positive scale and the signs go into the transform. The signs come from one Transform2RequestBus
        // lookup. A Transform 1 entity has no handler, so the lookup misses and every sign stays positive, with zero not a flip. This is a
        // runtime path, which is only reached when another system asks for the geometry, so that one lookup is all it costs.
        // It is the world scale, so the White Box on a child of a flipped entity is drawn mirrored as well.
        AZ::Vector3 worldScale = AZ::Vector3::CreateOne();
        AZ::Transform2RequestBus::EventResult(worldScale, entityId, &AZ::Transform2Requests::GetWorldScale);
        const AZ::Vector3 flipSigns = AZ::Vector3::CreateSelectCmpGreaterEqual(
            worldScale, AZ::Vector3::CreateZero(), AZ::Vector3::CreateOne(), -AZ::Vector3::CreateOne());
        geometry.m_transform.MultiplyByScale(flipSigns);
        geometry.m_transparent = false;

        // Copy white box render data vertices and indices into visible geometry structure. This is not as efficient as it could be. The
        // function that builds the white box data removes degenerate triangles but the format does not allow sharing vertices and edges.
        uint32_t vertIndex = 0;
        for (const auto& face : faceData)
        {
            geometry.m_vertices.push_back(face.m_v1.m_position.GetX());
            geometry.m_vertices.push_back(face.m_v1.m_position.GetY());
            geometry.m_vertices.push_back(face.m_v1.m_position.GetZ());
            geometry.m_indices.push_back(vertIndex++);

            geometry.m_vertices.push_back(face.m_v2.m_position.GetX());
            geometry.m_vertices.push_back(face.m_v2.m_position.GetY());
            geometry.m_vertices.push_back(face.m_v2.m_position.GetZ());
            geometry.m_indices.push_back(vertIndex++);

            geometry.m_vertices.push_back(face.m_v3.m_position.GetX());
            geometry.m_vertices.push_back(face.m_v3.m_position.GetY());
            geometry.m_vertices.push_back(face.m_v3.m_position.GetZ());
            geometry.m_indices.push_back(vertIndex++);
        }

        // An odd number of mirrored axes reverses the winding of every triangle, so the facing of each surface is kept by listing the
        // last two corners of the triangle the other way round. Two mirrored axes are a half turn, which keeps the winding.
        if (AZ::Transform2::IsMirrored(worldScale))
        {
            for (size_t index = 0; index + 2 < geometry.m_indices.size(); index += 3)
            {
                AZStd::swap(geometry.m_indices[index + 1], geometry.m_indices[index + 2]);
            }
        }

        return geometry;
    }
} // namespace WhiteBox
