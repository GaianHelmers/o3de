/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Mesh/Transform2MeshSupport.h>

#include <Atom/Feature/Mesh/MeshFeatureProcessorInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <AtomLyIntegration/CommonFeatures/CoreLights/AreaLightBus.h>
#include <AtomLyIntegration/CommonFeatures/Decals/DecalBus.h>
#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AtomLyIntegration/CommonFeatures/Mesh/MeshHandleStateBus.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/std/algorithm.h>
#include <AzFramework/Visibility/EntityBoundsUnionBus.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>

namespace AZ
{
    namespace Render
    {
        // The determinant of a world matrix below which a mesh takes the exact path. Matrix3x4::GetInverseFull gives up at FLT_EPSILON
        // (about 1.2e-7), so this keeps clear of it.
        constexpr float CollapsedAxisDeterminant = 1e-6f;

        //////////////////////////////////////////////////////////////////////////
        // EntityMesh: what the listener knows about one Transform 2 entity
        //
        // The same object pushes the sign to the area light and the decal of the entity. Neither of them gives a notice when it
        // starts, so it listens to the one notice each of them sends while it configures itself, and pushes again at that time.
        // That covers a first start, and the restarts that no entity activation tells of: the editor starts an area light again
        // whenever one of its properties changes, and a decal when its entity is hidden and shown.

        class Transform2MeshSupport::EntityMesh
            : private MeshHandleStateNotificationBus::Handler
            , private AreaLightNotificationBus::Handler
            , private DecalNotificationBus::Handler
        {
        public:
            AZ_CLASS_ALLOCATOR(EntityMesh, AZ::SystemAllocator);

            EntityMesh(Transform2MeshSupport& owner, AZ::EntityId entityId, const AZ::Vector3& scale);
            ~EntityMesh();

            //! Gives the mesh the sign of a new scale and has the bounds of the entity calculated again.
            //! The world transform is read from Transform 1.
            void SetScale(const AZ::Vector3& scale);

            //! Gives the mesh the sign of the scale it has now and has the bounds of the entity calculated again.
            //! The world transform is read from Transform 1.
            void Refresh();

            //! Gives the mesh the sign of the scale, with a world transform that Transform 1 just signaled.
            //! A move carries no new sign, because a change of the scale reaches SetScale as well, so the bounds are not refreshed.
            void SetWorld(const AZ::Transform& world, const AZ::Vector3& scale);

            //! Puts the mesh, the light and the decal back to what the Transform 1 components keep: unmirrored, with the positive
            //! non-uniform scale.
            void Restore();

        private:
            using MeshHandle = MeshFeatureProcessorInterface::MeshHandle;

            // MeshHandleStateNotificationBus::Handler overrides...
            void OnMeshHandleSet(const MeshHandle* meshHandle) override;

            // AreaLightNotificationBus::Handler overrides...
            void OnColorOrIntensityChanged(const AZ::Color& color, float intensity) override;

            // DecalNotificationBus::Handler overrides...
            void OnMaterialChanged(AZ::Data::Asset<AZ::RPI::MaterialAsset> materialAsset) override;

            //! Gives the area light and the decal of the entity the signed scale, with a world transform that Transform 1 holds.
            //! Does nothing for an entity that has neither, which is nearly all of them, so a move costs it nothing.
            void PushToLightAndDecal(const AZ::Transform& world, const AZ::Vector3& renderScale) const;

            //! Gives the area light and the decal the sign of the scale this entity has now, with the world read from Transform 1.
            void RefreshLightAndDecal() const;

            //! Returns the valid mesh handle of the entity, or null when it has no mesh yet or any more.
            const MeshHandle* FindMeshHandle() const;

            //! Sets the transform and the mirrored state of a mesh, and hands the scale to whoever answers for the mesh of the entity,
            //! so its bounds and ray tests use the same scale. Does nothing when the entity has no mesh feature processor.
            //! A non null exactWorld is the exact world matrix of a sheared entity. It is drawn in place of the transform, which
            //! stays as the best fit that ray tracing keeps. Null puts the mesh back to the transform alone.
            //! A change of the flip the mesh holds is announced to the readers of the mesh, see m_flipMask, and ends the motion
            //! vector history of the mesh, because the mesh did not move across the mirror axis.
            void PushToMesh(
                const MeshHandle& meshHandle,
                const AZ::Transform& world,
                const AZ::Vector3& nonUniformScale,
                bool mirrored,
                const AZ::Matrix3x4* exactWorld);

            //! Sets the transform, the mirrored state and the exact world of a mesh from the signed scale and the entity.
            //! Only a sheared world, or one with a collapsed axis, needs the exact matrix: every other world is drawn from the best fit
            //! and the render scale.
            void PushScaleToMesh(const MeshHandle& meshHandle, const AZ::Transform& world);

            //! Asks for the bounds of the entity to be calculated again. A flip writes nothing to Transform 1, so no transform
            //! event fires for it and nothing else would move the bounds the visibility system holds.
            void RefreshBounds() const;

            Transform2MeshSupport& m_owner;
            AZ::EntityId m_entityId;
            AZ::Vector3 m_scale = AZ::Vector3::CreateOne();
            bool m_hasLightOrDecal = false;

            //! The flip (a bit for every negative axis, as LmbrCentral::FlipMaskFromScale) that the mesh of the entity holds, and that
            //! its readers were told of. A mesh starts out unflipped. A pure flip changes neither the Transform 1 world nor the
            //! positive non-uniform scale, so the readers that cache what they took from the mesh (surface data, vegetation
            //! blockers) are not told by anything else, and are told through MeshComponentNotificationBus when this changes.
            AZ::u8 m_flipMask = 0;
        };

        Transform2MeshSupport::EntityMesh::EntityMesh(Transform2MeshSupport& owner, AZ::EntityId entityId, const AZ::Vector3& scale)
            : m_owner(owner)
            , m_entityId(entityId)
            , m_scale(scale)
        {
            // If the mesh exists already this calls OnMeshHandleSet right away. If not, it is called once the model is ready.
            MeshHandleStateNotificationBus::Handler::BusConnect(m_entityId);

            // A light or a decal that is there already, because the entity was live before it went to Transform 2, gets the sign now.
            // One that starts later sends the notices below while it configures itself, and gets it then.
            AreaLightNotificationBus::Handler::BusConnect(m_entityId);
            DecalNotificationBus::Handler::BusConnect(m_entityId);
            m_hasLightOrDecal = AreaLightRequestBus::HasHandlers(m_entityId) || DecalRequestBus::HasHandlers(m_entityId);
            if (m_hasLightOrDecal)
            {
                RefreshLightAndDecal();
            }
        }

        Transform2MeshSupport::EntityMesh::~EntityMesh()
        {
            DecalNotificationBus::Handler::BusDisconnect();
            AreaLightNotificationBus::Handler::BusDisconnect();
            MeshHandleStateNotificationBus::Handler::BusDisconnect();
        }

        void Transform2MeshSupport::EntityMesh::SetScale(const AZ::Vector3& scale)
        {
            m_scale = scale;
            Refresh();
        }

        void Transform2MeshSupport::EntityMesh::Refresh()
        {
            const MeshHandle* meshHandle = FindMeshHandle();
            AZ::TransformInterface* transform = AZ::TransformBus::FindFirstHandler(m_entityId);
            if (transform)
            {
                const AZ::Transform world = transform->GetWorldTM();
                PushToLightAndDecal(world, AZ::Transform2::RenderScale(m_scale));

                if (meshHandle)
                {
                    PushScaleToMesh(*meshHandle, world);
                    RefreshBounds();
                }
            }
        }

        void Transform2MeshSupport::EntityMesh::SetWorld(const AZ::Transform& world, const AZ::Vector3& scale)
        {
            m_scale = scale;

            // A move writes the unflipped world to the light and the decal first, and this is the push that puts the flip back.
            PushToLightAndDecal(world, AZ::Transform2::RenderScale(m_scale));

            if (const MeshHandle* meshHandle = FindMeshHandle())
            {
                PushScaleToMesh(*meshHandle, world);
            }
        }

        void Transform2MeshSupport::EntityMesh::Restore()
        {
            const MeshHandle* meshHandle = FindMeshHandle();
            AZ::TransformInterface* transform = AZ::TransformBus::FindFirstHandler(m_entityId);
            if (transform)
            {
                // The Transform 1 mesh component keeps the scale it reads from this bus, so read it the same way.
                // An entity with a uniform scale has no handler, which leaves the scale at one.
                AZ::Vector3 nonUniformScale = AZ::Vector3::CreateOne();
                AZ::NonUniformScaleRequestBus::EventResult(nonUniformScale, m_entityId, &AZ::NonUniformScaleRequests::GetScale);
                const AZ::Transform world = transform->GetWorldTM();
                PushToLightAndDecal(world, nonUniformScale);

                if (meshHandle)
                {
                    PushToMesh(*meshHandle, world, nonUniformScale, false, nullptr);
                    RefreshBounds();
                }
            }
        }

        void Transform2MeshSupport::EntityMesh::OnColorOrIntensityChanged(
            [[maybe_unused]] const AZ::Color& color, [[maybe_unused]] float intensity)
        {
            // Sent while an area light configures itself, which a light does each time it starts. See the note at the top.
            m_hasLightOrDecal = true;
            RefreshLightAndDecal();
        }

        void Transform2MeshSupport::EntityMesh::OnMaterialChanged(
            [[maybe_unused]] AZ::Data::Asset<AZ::RPI::MaterialAsset> materialAsset)
        {
            // Sent while a decal configures itself, which a decal does each time it starts. See the note at the top.
            m_hasLightOrDecal = true;
            RefreshLightAndDecal();
        }

        void Transform2MeshSupport::EntityMesh::PushToLightAndDecal(const AZ::Transform& world, const AZ::Vector3& renderScale) const
        {
            if (m_hasLightOrDecal)
            {
                AreaLightRequestBus::Event(m_entityId, &AreaLightRequestBus::Events::SetRenderScale, world, renderScale);
                DecalRequestBus::Event(m_entityId, &DecalRequestBus::Events::SetRenderScale, world, renderScale);
            }
        }

        void Transform2MeshSupport::EntityMesh::RefreshLightAndDecal() const
        {
            if (AZ::TransformInterface* transform = AZ::TransformBus::FindFirstHandler(m_entityId))
            {
                PushToLightAndDecal(transform->GetWorldTM(), AZ::Transform2::RenderScale(m_scale));
            }
        }

        void Transform2MeshSupport::EntityMesh::OnMeshHandleSet(const MeshHandle* meshHandle)
        {
            // A new mesh starts out unmirrored with the positive scale, and it is announced here once its model is ready.
            // A mesh that is being released is announced too, with a handle that is no longer valid.
            AZ::TransformInterface* transform = AZ::TransformBus::FindFirstHandler(m_entityId);
            if (meshHandle && meshHandle->IsValid() && transform)
            {
                PushScaleToMesh(*meshHandle, transform->GetWorldTM());

                // The bounds were calculated when the model became ready, which is before the mesh had the sign.
                RefreshBounds();

                // Not every producer sets the transform of its mesh before it announces it. The White Box announces the mesh
                // of a rebuild first and sets the plain transform right after, which would take the sign away again.
                m_owner.QueueRefresh(m_entityId);
            }
        }

        const MeshFeatureProcessorInterface::MeshHandle* Transform2MeshSupport::EntityMesh::FindMeshHandle() const
        {
            const MeshHandle* meshHandle = nullptr;
            MeshHandleStateRequestBus::EventResult(meshHandle, m_entityId, &MeshHandleStateRequestBus::Events::GetMeshHandle);
            return (meshHandle && meshHandle->IsValid()) ? meshHandle : nullptr;
        }

        void Transform2MeshSupport::EntityMesh::PushToMesh(
            const MeshHandle& meshHandle,
            const AZ::Transform& world,
            const AZ::Vector3& nonUniformScale,
            bool mirrored,
            const AZ::Matrix3x4* exactWorld)
        {
            if (auto* meshFeatureProcessor = RPI::Scene::GetFeatureProcessorForEntity<MeshFeatureProcessorInterface>(m_entityId))
            {
                // SetTransform first: it does the bookkeeping of a move, and the best fit it stores is what ray tracing keeps.
                meshFeatureProcessor->SetTransform(meshHandle, world, nonUniformScale);
                meshFeatureProcessor->SetMirrored(meshHandle, mirrored);

                // Then the exact matrix replaces the entry the shaders and the culling bounds read. Called for every push, so a mesh
                // that leaves the shear (or Transform 2) is put back by the same call. It lowers the exact path unless the mesh is mirrored.
                meshFeatureProcessor->SetExactWorld(
                    meshHandle, exactWorld ? *exactWorld : AZ::Matrix3x4::CreateIdentity(), exactWorld != nullptr);

                // A mesh component keeps this scale, sends it with every move, and reads it for its bounds and ray tests. A non-uniform
                // scale event overwrites it with the positive scale, and that event comes before the one that ends up here.
                MeshHandleStateRequestBus::Event(m_entityId, &MeshHandleStateRequestBus::Events::SetRenderScale, nonUniformScale);

                // The readers are told once for each change of the flip, and not for every move. Only Transform 2 gets here, and
                // nothing that handles the notification writes a scale, so it cannot come back around into another change.
                const AZ::u8 flipMask = LmbrCentral::FlipMaskFromScale(nonUniformScale);
                if (flipMask != m_flipMask)
                {
                    m_flipMask = flipMask;

                    // A flip is not movement. Without this the previous frame matrix is still the unflipped one, every vertex shows a
                    // full width motion vector across the mirror axis, and TAA ghosts the mesh for a frame. Keyed on the mask and not on
                    // the handedness, so a flip of two axes counts, and so does a parent flip that reaches this entity, which changes
                    // its world scale. The matrix is final here: SetTransform, SetMirrored and SetExactWorld have run.
                    meshFeatureProcessor->ResetTransformHistory(meshHandle);

                    MeshComponentNotificationBus::Event(m_entityId, &MeshComponentNotificationBus::Events::OnRenderScaleChanged);
                }
            }
        }

        void Transform2MeshSupport::EntityMesh::PushScaleToMesh(const MeshHandle& meshHandle, const AZ::Transform& world)
        {
            // A sheared world is the one thing the transform and the render scale cannot say. The entity answers, and the matrix is read
            // live: this runs inside the notification of the entity, after all of its data is final (Transform2Bus.h).
            bool sheared = false;
            AZ::Transform2RequestBus::EventResult(sheared, m_entityId, &AZ::Transform2Requests::IsWorldSheared);

            // A collapsed axis takes the exact path too. The normal matrix of the best fit is an inverse, and Matrix3x4::GetInverseFull
            // gives the identity (and a warning) for a determinant that small, which would light a flattened mesh with its unrotated
            // normals. The exact path builds the normal matrix from cofactors, which a collapsed axis does not defeat.
            // Without shear the world is a rotation times the stand-in and the render scale, so its determinant is their product.
            // The warning of SetTransform, which runs first, stays.
            const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(m_scale);
            const float standIn = AZ::Transform2::UniformStandIn(m_scale);
            const float determinant = standIn * standIn * standIn * renderScale.GetX() * renderScale.GetY() * renderScale.GetZ();
            const bool collapsed = AZ::GetAbs(determinant) < CollapsedAxisDeterminant;

            AZ::Matrix3x4 exactWorld = AZ::Matrix3x4::CreateIdentity();
            if (sheared)
            {
                AZ::Transform2RequestBus::EventResult(exactWorld, m_entityId, &AZ::Transform2Requests::GetWorldMatrix);
            }
            else if (collapsed)
            {
                // The true matrix of a zero axis has a zero column, and nothing in it says which side a mirror turned the surface to.
                // The best fit and the render scale carry the sign, on a floor too small to see.
                exactWorld = AZ::Matrix3x4::CreateFromTransform(world);
                exactWorld.MultiplyByScale(renderScale);
            }

            PushToMesh(meshHandle, world, renderScale, AZ::Transform2::IsMirrored(m_scale), (sheared || collapsed) ? &exactWorld : nullptr);
        }

        void Transform2MeshSupport::EntityMesh::RefreshBounds() const
        {
            // The visibility system is not there in a test that runs without it.
            if (auto* boundsUnion = AZ::Interface<AzFramework::IEntityBoundsUnion>::Get())
            {
                boundsUnion->RefreshEntityLocalBoundsUnion(m_entityId);
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Transform2MeshSupport

        Transform2MeshSupport::Transform2MeshSupport()
        {
            AZ::Transform2NotificationBus::Handler::BusConnect();
        }

        Transform2MeshSupport::~Transform2MeshSupport()
        {
            AZ::TickBus::Handler::BusDisconnect();
            AZ::Transform2NotificationBus::Handler::BusDisconnect();
        }

        void Transform2MeshSupport::OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale)
        {
            // The mesh may not exist yet. EntityMesh waits for the mesh handle notification, and uses it right away if the mesh is there.
            m_entityMeshes.erase(entityId);
            m_entityMeshes.emplace(entityId, AZStd::make_unique<EntityMesh>(*this, entityId, scale));
        }

        void Transform2MeshSupport::OnTransform2Deactivated(AZ::EntityId entityId)
        {
            if (auto entityMesh = m_entityMeshes.find(entityId); entityMesh != m_entityMeshes.end())
            {
                entityMesh->second->Restore();
                m_entityMeshes.erase(entityMesh);
            }
        }

        void Transform2MeshSupport::OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale)
        {
            if (auto entityMesh = m_entityMeshes.find(entityId); entityMesh != m_entityMeshes.end())
            {
                entityMesh->second->SetScale(scale);
            }
        }

        void Transform2MeshSupport::OnTransform2WorldChanged(AZ::EntityId entityId, const AZ::Transform& world, const AZ::Vector3& scale)
        {
            if (auto entityMesh = m_entityMeshes.find(entityId); entityMesh != m_entityMeshes.end())
            {
                entityMesh->second->SetWorld(world, scale);
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // The refresh on the next tick

        void Transform2MeshSupport::QueueRefresh(AZ::EntityId entityId)
        {
            if (AZStd::find(m_pendingRefresh.begin(), m_pendingRefresh.end(), entityId) == m_pendingRefresh.end())
            {
                m_pendingRefresh.push_back(entityId);
            }

            if (!AZ::TickBus::Handler::BusIsConnected())
            {
                AZ::TickBus::Handler::BusConnect();
            }
        }

        void Transform2MeshSupport::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
        {
            // The list is taken first, so a refresh requested while this one runs waits for the next tick.
            AZStd::vector<AZ::EntityId> pendingRefresh;
            pendingRefresh.swap(m_pendingRefresh);
            AZ::TickBus::Handler::BusDisconnect();

            // An entity that left Transform 2 in the meantime has no listener any more, and its mesh was restored.
            for (const AZ::EntityId entityId : pendingRefresh)
            {
                if (auto entityMesh = m_entityMeshes.find(entityId); entityMesh != m_entityMeshes.end())
                {
                    entityMesh->second->Refresh();
                }
            }
        }

        int Transform2MeshSupport::GetTickOrder()
        {
            // Before the render data of the frame is updated, so a repair never shows for a frame.
            return AZ::TICK_PRE_RENDER;
        }
    } // namespace Render
} // namespace AZ
