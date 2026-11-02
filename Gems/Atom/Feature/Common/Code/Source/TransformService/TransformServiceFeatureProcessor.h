/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Atom/Feature/TransformService/TransformServiceFeatureProcessorInterface.h>
#include <Atom/RPI.Public/FeatureProcessor.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/std/containers/unordered_map.h>

namespace AZ
{
    namespace Render
    {
        //! This feature processor handles static and dynamic non-skinned meshes.
        class TransformServiceFeatureProcessor final
            : public TransformServiceFeatureProcessorInterface
        {
        public:
            AZ_CLASS_ALLOCATOR(TransformServiceFeatureProcessor, AZ::SystemAllocator)

            AZ_RTTI(AZ::Render::TransformServiceFeatureProcessor, "{D8A2C353-2850-42F8-AA21-3979CBECBF80}", AZ::Render::TransformServiceFeatureProcessorInterface);

            static void Reflect(AZ::ReflectContext* context);

            TransformServiceFeatureProcessor() = default;
            virtual ~TransformServiceFeatureProcessor() = default;

            // FeatureProcessor overrides ...
            //! Creates pools, buffers, and buffer views
            void Activate() override;
            //! Releases GPU resources.
            void Deactivate() override;

            // RPI::SceneNotificationBus overrides ...
            void OnBeginPrepareRender() override;
            void OnEndPrepareRender() override;

            // TransformServiceFeatureProcessorInterface overrides ...
            ObjectId ReserveObjectId() override;
            void ReleaseObjectId(ObjectId& id) override;
            void SetTransformForId(ObjectId id, const AZ::Transform& transform,
                const AZ::Vector3& nonUniformScale = AZ::Vector3::CreateOne()) override;
            AZ::Transform GetTransformForId(ObjectId id) const override;
            AZ::Vector3 GetNonUniformScaleForId(ObjectId id) const override;

            // Transform 2 support, concrete class only
            //! Makes the previous-frame matrix of an object equal to its current one, so the next frame draws no motion for it.
            //! For a change that is not movement, such as a mirror flip. Call after SetTransformForId so the current entry is the new matrix.
            void ResetHistoryForId(ObjectId id);

            //! Writes an exact object to world matrix, with a normal matrix made from its cofactors (MeshCommon::CreateNormalMatrix).
            //! The matrix may mirror and may shear. It replaces what SetTransformForId stored, and GetTransformForId and
            //! GetNonUniformScaleForId read it back as a rotation and a scale, which they cannot do for a shear.
            //! A mesh that has an exact matrix reads it with GetMatrixForId.
            void SetMatrixForId(ObjectId id, const AZ::Matrix3x4& objectToWorld);
            //! The exact object to world matrix last written for an id, by SetTransformForId or by SetMatrixForId.
            AZ::Matrix3x4 GetMatrixForId(ObjectId id) const;

            //! The best fit of an object that has an exact matrix: the rigid transform and the per-axis scale that SetTransformForId
            //! was given last, read back the way GetTransformForId and GetNonUniformScaleForId read an entry. Ray tracing keeps it,
            //! because for a shear those two getters cannot read the exact matrix as a rotation and a scale.
            //! SetMatrixForId takes it from the entry it replaces, so SetTransformForId has to run before it for every move.
            //! Returns false, and leaves the outputs as they were, for an object without an exact matrix.
            bool GetBestFitForId(ObjectId id, AZ::Transform& transform, AZ::Vector3& nonUniformScale) const;
            //! Drops the best fit of an object that leaves the exact path. ReleaseObjectId drops it too.
            void ClearBestFitForId(ObjectId id);

        private:

            // Holds both regular 4x3 transforms and 3x3 normal transforms with padding at the end of each float3.
            union Float4x3
            {
                float m_transform[12] = { 0.0f };
                uint32_t m_nextFreeSlot;
            };

            // Flag value for when the buffers have no empty spaces.
            static const uint32_t NoAvailableTransformIndices = std::numeric_limits<uint32_t>::max();

            TransformServiceFeatureProcessor(const TransformServiceFeatureProcessor&) = delete;

            // Prepare GPU buffers for object transformation matrices
            // Create the buffers if they don't exist. Otherwise, resize them if they are not large enough for the matrices
            void PrepareBuffers();

            void UpdateSceneSrg(RPI::ShaderResourceGroup *sceneSrg);

            RPI::Scene::PrepareSceneSrgEvent::Handler m_updateSceneSrgHandler;
            RHI::ShaderInputNameIndex m_objectToWorldBufferIndex = "m_objectToWorldBuffer";
            RHI::ShaderInputNameIndex m_objectToWorldInverseTransposeBufferIndex = "m_objectToWorldInverseTransposeBuffer";
            RHI::ShaderInputNameIndex m_objectToWorldHistoryBufferIndex = "m_objectToWorldHistoryBuffer";

            // Stores transforms that are uploaded to a GPU buffer. Used slots have float12(matrix3x4) values, empty slots
            // have a uint32_t that points to the next empty slot like a linked list. m_firstAvailableMeshTransformIndex stores the first
            // empty slot, unless there are none then it's NoAvailableTransformIndices. This allows mesh object SRGs to be compiled once
            // with an index to their transform, and updates to the transform just update the buffer, not individual mesh SRGs.
            AZStd::vector<Float4x3> m_objectToWorldTransforms;
            AZStd::vector<Float4x3> m_objectToWorldInverseTransposeTransforms;
            AZStd::vector<Float4x3> m_objectToWorldHistoryTransforms;

            static const size_t TransformValueSize = sizeof(decltype(m_objectToWorldTransforms)::value_type);
            static const size_t NormalValueSize = sizeof(decltype(m_objectToWorldInverseTransposeTransforms)::value_type);

            // Transform 2: the matrix of the best fit of an object that SetMatrixForId replaced, and the exact matrix it wrote.
            // The exact one tells a second SetMatrixForId, with no SetTransformForId between, that the entry is no best fit any more.
            // Keyed by the index of the object. Empty unless a Transform 2 mesh has an exact matrix.
            struct BestFit
            {
                Float4x3 m_bestFitMatrix;
                Float4x3 m_exactMatrix;
            };
            AZStd::unordered_map<uint32_t, BestFit> m_bestFitByIndex;

            Data::Instance<RPI::Buffer> m_objectToWorldBuffer;
            Data::Instance<RPI::Buffer> m_objectToWorldInverseTransposeBuffer;
            Data::Instance<RPI::Buffer> m_objectToWorldHistoryBuffer;

            uint32_t m_firstAvailableTransformIndex = NoAvailableTransformIndices;
            bool m_deviceBufferNeedsUpdate = false;
            bool m_historyBufferNeedsUpdate = false;
            bool m_isWriteable = true;     //prevents write access during certain parts of the frame (for threadsafety)
        };
    }
}
