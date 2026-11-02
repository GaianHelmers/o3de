/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Source/Transform2ColliderSupport.h>

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/optional.h>
#include <AzFramework/Physics/ColliderComponentBus.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Physics/SimulatedBodies/RigidBody.h>
#include <AzFramework/Physics/SimulatedBodies/StaticRigidBody.h>
#include <AzFramework/Visibility/EntityBoundsUnionBus.h>

#include <PhysX/ColliderComponentBus.h>
#include <PhysX/ColliderShapeBus.h>
#include <PhysX/ForceRegionComponentBus.h>
#include <PhysX/MathConversion.h>
#include <PhysX/PhysXLocks.h>
#include <Source/RigidBodyComponent.h>
#include <Source/Shape.h>
#include <Source/Utils.h>

namespace PhysX
{
    namespace
    {
        // The scene a shape lives in, or null while it is attached to no actor yet. A null scene takes no lock.
        physx::PxScene* GetScene(const physx::PxShape& pxShape)
        {
            const physx::PxRigidActor* actor = pxShape.getActor();
            return actor ? actor->getScene() : nullptr;
        }

        physx::PxShape* GetPxShape(const AZStd::shared_ptr<Physics::Shape>& shape)
        {
            auto* physxShape = azrtti_cast<PhysX::Shape*>(shape.get());
            return physxShape ? physxShape->GetPxShape() : nullptr;
        }

        // Adds the shapes of a rigid body or a static rigid body to a list. The two kinds of body have no common shape interface.
        template<typename BodyType>
        void AppendBodyShapes(BodyType& body, AZStd::vector<AZStd::shared_ptr<Physics::Shape>>& shapes)
        {
            for (AZ::u32 shapeIndex = 0; shapeIndex < body.GetShapeCount(); ++shapeIndex)
            {
                if (AZStd::shared_ptr<Physics::Shape> shape = body.GetShape(shapeIndex))
                {
                    shapes.push_back(AZStd::move(shape));
                }
            }
        }

        // The shapes of a body. The other kinds of body (characters, ragdolls) are not rigid bodies and hand out none.
        AZStd::vector<AZStd::shared_ptr<Physics::Shape>> GetBodyShapes(AzPhysics::SimulatedBody& body)
        {
            AZStd::vector<AZStd::shared_ptr<Physics::Shape>> shapes;
            if (auto* rigidBody = azdynamic_cast<AzPhysics::RigidBody*>(&body))
            {
                AppendBodyShapes(*rigidBody, shapes);
            }
            else if (auto* staticBody = azdynamic_cast<AzPhysics::StaticRigidBody*>(&body))
            {
                AppendBodyShapes(*staticBody, shapes);
            }
            return shapes;
        }

        // The body a scene event names, or null when the scene hands out none, as a scene that is being destroyed does.
        AzPhysics::SimulatedBody* FindBody(AzPhysics::SceneHandle sceneHandle, AzPhysics::SimulatedBodyHandle bodyHandle)
        {
            auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
            return sceneInterface ? sceneInterface->GetSimulatedBodyFromHandle(sceneHandle, bodyHandle) : nullptr;
        }

        // The listener of this module, for Transform2ColliderSupport::FindSigns. Set while the PhysX system component lives.
        Transform2ColliderSupport* s_listener = nullptr;
    } // namespace

    //////////////////////////////////////////////////////////////////////////
    // Mirror math

    namespace Transform2Mirror
    {
        AZ::Vector3 GetSigns(const AZ::Vector3& scale)
        {
            return AZ::Vector3(
                scale.GetX() < 0.0f ? -1.0f : 1.0f,
                scale.GetY() < 0.0f ? -1.0f : 1.0f,
                scale.GetZ() < 0.0f ? -1.0f : 1.0f);
        }

        AZ::u32 GetSignMask(const AZ::Vector3& signs)
        {
            return (signs.GetX() < 0.0f ? 1u : 0u) | (signs.GetY() < 0.0f ? 2u : 0u) | (signs.GetZ() < 0.0f ? 4u : 0u);
        }

        AZ::Vector3 GetMaskSigns(AZ::u32 mask)
        {
            return AZ::Vector3((mask & 1u) ? -1.0f : 1.0f, (mask & 2u) ? -1.0f : 1.0f, (mask & 4u) ? -1.0f : 1.0f);
        }

        AZ::Vector3 MirrorPosition(const AZ::Vector3& signs, const AZ::Vector3& position)
        {
            return position * signs;
        }

        AZ::Quaternion MirrorRotation(const AZ::Vector3& signs, const AZ::Quaternion& rotation)
        {
            // The vector part of a rotation is an axial vector, which a reflection M carries as det(M) * M v.
            const float determinant = signs.GetX() * signs.GetY() * signs.GetZ();
            const AZ::Vector3 axis = AZ::Vector3(rotation.GetX(), rotation.GetY(), rotation.GetZ()) * signs * determinant;
            return AZ::Quaternion(axis.GetX(), axis.GetY(), axis.GetZ(), rotation.GetW());
        }

        bool ReversesHingeAxis(const AZ::Vector3& signs)
        {
            return signs.GetY() * signs.GetZ() < 0.0f;
        }

        bool ReversesSlideAxis(const AZ::Vector3& signs)
        {
            return signs.GetX() < 0.0f;
        }

        AZ::Transform MirrorJointFrame(const AZ::Vector3& signs, const AZ::Transform& frame, bool halfTurn)
        {
            if (GetSignMask(signs) == 0)
            {
                return frame;
            }

            // The half turn is post-multiplied, so it turns the mirrored frame about its own Y axis. Y reverses X and Z.
            AZ::Quaternion rotation = MirrorRotation(signs, frame.GetRotation());
            if (halfTurn)
            {
                rotation = rotation * AZ::Quaternion(0.0f, 1.0f, 0.0f, 0.0f);
            }

            AZ::Transform mirrored = frame;
            mirrored.SetTranslation(MirrorPosition(signs, frame.GetTranslation()));
            mirrored.SetRotation(rotation);
            return mirrored;
        }

        AZ::Transform UnmirrorJointFrame(const AZ::Vector3& signs, const AZ::Transform& frame, bool halfTurn)
        {
            if (GetSignMask(signs) == 0)
            {
                return frame;
            }

            // Undoes the half turn with its conjugate, which is exact, and then the mirror, which is its own inverse.
            AZ::Quaternion rotation = frame.GetRotation();
            if (halfTurn)
            {
                rotation = rotation * AZ::Quaternion(0.0f, -1.0f, 0.0f, 0.0f);
            }

            AZ::Transform authored = frame;
            authored.SetTranslation(MirrorPosition(signs, frame.GetTranslation()));
            authored.SetRotation(MirrorRotation(signs, rotation));
            return authored;
        }

        AZ::Vector3 GetEntitySigns(AZ::EntityId entityId)
        {
            // Only an entity with a Transform 2 has a handler. Without one the scale stays at one and no axis is flipped.
            // The world scale carries the flips of every parent, so a child of a flipped entity mirrors too.
            AZ::Vector3 scale = AZ::Vector3::CreateOne();
            AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetWorldScale);
            return GetSigns(scale);
        }

        AZStd::optional<Physics::ColliderConfiguration> MirrorConfiguration(
            const AZ::Vector3& signs, const Physics::ColliderConfiguration& colliderConfig)
        {
            if (GetSignMask(signs) == 0)
            {
                return AZStd::nullopt;
            }

            Physics::ColliderConfiguration mirrored = colliderConfig;
            mirrored.m_position = MirrorPosition(signs, colliderConfig.m_position);
            mirrored.m_rotation = MirrorRotation(signs, colliderConfig.m_rotation);
            return mirrored;
        }

        namespace
        {
            // W * diag(signs) * W^-1 as a matrix, with W the world transform without its scale. W is orthonormal, so its inverse
            // is the fast one, and the product keeps distances.
            AZ::Matrix3x4 GetMirrorMatrix(const AZ::Transform& world, const AZ::Vector3& signs)
            {
                const AZ::Matrix3x4 unscaledWorld =
                    AZ::Matrix3x4::CreateFromQuaternionAndTranslation(world.GetRotation(), world.GetTranslation());
                return unscaledWorld * AZ::Matrix3x4::CreateScale(signs) * unscaledWorld.GetInverseFast();
            }
        } // namespace

        AZ::Vector3 MirrorWorldPoint(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Vector3& point)
        {
            return GetSignMask(signs) == 0 ? point : GetMirrorMatrix(world, signs).TransformPoint(point);
        }

        AZ::Vector3 MirrorWorldDirection(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Vector3& direction)
        {
            return GetSignMask(signs) == 0 ? direction : GetMirrorMatrix(world, signs).TransformVector(direction);
        }

        AZ::Aabb MirrorWorldAabb(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Aabb& aabb)
        {
            return (GetSignMask(signs) == 0 || !aabb.IsValid()) ? aabb : aabb.GetTransformedAabb(GetMirrorMatrix(world, signs));
        }
    } // namespace Transform2Mirror

    //////////////////////////////////////////////////////////////////////////
    // One shape: capture

    Transform2ShapeFlip::Transform2ShapeFlip(const AZStd::shared_ptr<Physics::Shape>& shape)
        : m_shape(shape)
    {
        physx::PxShape* pxShape = GetPxShape(shape);
        if (pxShape == nullptr)
        {
            return;
        }

        m_geometryType = pxShape->getGeometryType();
        if (m_geometryType == physx::PxGeometryType::eHEIGHTFIELD)
        {
            return;
        }
        m_canMirror = true;

        // The shape reports its pose in the frame of the entity. For a capsule it hides the rotation PhysX adds to lay it along Z.
        const AZStd::pair<AZ::Vector3, AZ::Quaternion> pose = shape->GetLocalPose();
        m_authoredPosition = pose.first;
        m_authoredRotation = pose.second;

        // Only the mesh shapes have geometry to mirror. A mesh scale with a rotation turns the scaling axes away from the
        // local axes, so a sign cannot be dropped into it and only the pose of that shape follows the flip.
        [[maybe_unused]] physx::PxScene* scene = GetScene(*pxShape);
        PHYSX_SCENE_READ_LOCK(scene);

        bool hasMeshGeometry = false;
        physx::PxMeshScale meshScale;
        if (m_geometryType == physx::PxGeometryType::eTRIANGLEMESH)
        {
            physx::PxTriangleMeshGeometry geometry;
            if (pxShape->getTriangleMeshGeometry(geometry))
            {
                hasMeshGeometry = true;
                meshScale = geometry.scale;
                m_authoredTriangleMeshScale = geometry.scale.scale;
            }
        }
        else if (m_geometryType == physx::PxGeometryType::eCONVEXMESH)
        {
            physx::PxConvexMeshGeometry geometry;
            if (pxShape->getConvexMeshGeometry(geometry) && geometry.convexMesh != nullptr)
            {
                hasMeshGeometry = true;
                meshScale = geometry.scale;

                // The shape swaps this mesh out for a mirrored one and back, so keep it alive on our own reference.
                m_authoredConvexMesh = geometry.convexMesh;
                m_authoredConvexMesh->acquireReference();
                m_authoredConvexScale = geometry.scale;
                m_authoredConvexFlags = geometry.meshFlags;
            }
        }

        if (hasMeshGeometry)
        {
            m_canMirrorGeometry = meshScale.rotation.isIdentity();
            AZ_Warning("PhysX", m_canMirrorGeometry,
                "Transform 2 cannot mirror the geometry of a mesh collider whose mesh scale has a rotation. Only its pose follows the flip.");
        }
    }

    Transform2ShapeFlip::~Transform2ShapeFlip()
    {
        if (m_authoredConvexMesh != nullptr)
        {
            m_authoredConvexMesh->release();
        }
    }

    bool Transform2ShapeFlip::IsShape(const Physics::Shape* shape) const
    {
        return m_shape.lock().get() == shape;
    }

    bool Transform2ShapeFlip::IsExpired() const
    {
        return m_shape.expired();
    }

    //////////////////////////////////////////////////////////////////////////
    // One shape: flip

    void Transform2ShapeFlip::Apply(const AZ::Vector3& signs)
    {
        AZStd::shared_ptr<Physics::Shape> shape = m_shape.lock();
        if (!m_canMirror || !shape)
        {
            return;
        }

        // Always derived from the authored pose, never from the current one. The shape adds and strips the capsule rotation itself.
        shape->SetLocalPose(
            Transform2Mirror::MirrorPosition(signs, m_authoredPosition),
            Transform2Mirror::MirrorRotation(signs, m_authoredRotation));

        const AZ::u32 mask = Transform2Mirror::GetSignMask(signs);
        if (m_canMirrorGeometry && mask != m_appliedMask)
        {
            if (physx::PxShape* pxShape = GetPxShape(shape); pxShape != nullptr && ApplyGeometry(*pxShape, signs))
            {
                m_appliedMask = mask;
            }
        }
    }

    bool Transform2ShapeFlip::ApplyGeometry(physx::PxShape& pxShape, const AZ::Vector3& signs)
    {
        [[maybe_unused]] physx::PxScene* scene = GetScene(pxShape);

        if (m_geometryType == physx::PxGeometryType::eTRIANGLEMESH)
        {
            // PxMeshScale takes a negative component for a triangle mesh, and PhysX flips the normals itself when the
            // determinant is negative. The sign multiplies the authored scale, so the magnitudes stay as they were.
            PHYSX_SCENE_WRITE_LOCK(scene);
            physx::PxTriangleMeshGeometry geometry;
            if (!pxShape.getTriangleMeshGeometry(geometry))
            {
                return false;
            }
            geometry.scale.scale = m_authoredTriangleMeshScale.multiply(PxMathConvert(signs));
            pxShape.setGeometry(geometry);
            return true;
        }

        if (m_geometryType == physx::PxGeometryType::eCONVEXMESH)
        {
            // A convex mesh cannot take a negative scale, so a flip swaps in a hull with mirrored points.
            physx::PxConvexMesh* mesh = m_authoredConvexMesh;
            if (Transform2Mirror::GetSignMask(signs) != 0)
            {
                mesh = GetFlippedConvexMesh(signs);
                if (mesh == nullptr)
                {
                    return false;
                }
            }

            const physx::PxConvexMeshGeometry geometry(mesh, m_authoredConvexScale, m_authoredConvexFlags);
            PHYSX_SCENE_WRITE_LOCK(scene);
            pxShape.setGeometry(geometry);
            return true;
        }

        return false;
    }

    physx::PxConvexMesh* Transform2ShapeFlip::GetFlippedConvexMesh(const AZ::Vector3& signs)
    {
        AZStd::unique_ptr<Physics::CookedMeshShapeConfiguration>& cached = m_flippedConvexMeshes[Transform2Mirror::GetSignMask(signs)];
        if (cached)
        {
            return static_cast<physx::PxConvexMesh*>(cached->GetCachedNativeMesh());
        }

        // Read the vertices of the authored hull, mirror them and cook them again.
        const physx::PxVec3* vertices = m_authoredConvexMesh->getVertices();
        const physx::PxU32 vertexCount = m_authoredConvexMesh->getNbVertices();
        AZStd::vector<AZ::Vector3> points;
        points.reserve(vertexCount);
        for (physx::PxU32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            points.push_back(Transform2Mirror::MirrorPosition(signs, PxMathConvert(vertices[vertexIndex])));
        }

        AZStd::optional<Physics::CookedMeshShapeConfiguration> cooked =
            Utils::CreatePxCookedMeshConfiguration(points, PxMathConvert(m_authoredConvexScale.scale));
        if (!cooked.has_value())
        {
            return nullptr;
        }

        // Creating the geometry builds the native mesh and keeps it in the configuration, which releases it with this flip.
        auto configuration = AZStd::make_unique<Physics::CookedMeshShapeConfiguration>(*cooked);
        physx::PxGeometryHolder geometry;
        if (!Utils::CreatePxGeometryFromConfig(*configuration, geometry))
        {
            return nullptr;
        }

        cached = AZStd::move(configuration);
        return static_cast<physx::PxConvexMesh*>(cached->GetCachedNativeMesh());
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener: lifetime

    Transform2ColliderSupport::Transform2ColliderSupport()
        : m_bodyAddedHandler(
              [this](AzPhysics::SceneHandle sceneHandle, AzPhysics::SimulatedBodyHandle bodyHandle)
              {
                  OnSimulationBodyAdded(sceneHandle, bodyHandle);
              })
    {
        AZ::Transform2NotificationBus::Handler::BusConnect();
        s_listener = this;
    }

    Transform2ColliderSupport::~Transform2ColliderSupport()
    {
        m_bodyAddedHandler.Disconnect();
        if (s_listener == this)
        {
            s_listener = nullptr;
        }
        AZ::Transform2NotificationBus::Handler::BusDisconnect();
        AZ::EntityBus::MultiHandler::BusDisconnect();
        Physics::RigidBodyNotificationBus::MultiHandler::BusDisconnect();
        m_entities.clear();
    }

    AZ::Vector3 Transform2ColliderSupport::FindSigns(AZ::EntityId entityId)
    {
        // An empty map is the whole cost for a project with no Transform 2 entity.
        if (s_listener == nullptr || s_listener->m_entities.empty())
        {
            return AZ::Vector3::CreateOne();
        }

        const auto found = s_listener->m_entities.find(entityId);
        return found == s_listener->m_entities.end() ? AZ::Vector3::CreateOne() : Transform2Mirror::GetSigns(found->second.m_scale);
    }

    AZ::u32 Transform2ColliderSupport::GetOpenWindowCount()
    {
        return s_listener ? s_listener->m_windowCount : 0u;
    }

    bool Transform2ColliderSupport::IsHearingBodies()
    {
        return s_listener != nullptr && s_listener->m_bodyAddedHandler.IsConnected();
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener: notifications of a Transform 2 entity

    void Transform2ColliderSupport::OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale)
    {
        EntityState& state = m_entities[entityId];
        state.m_scale = scale;

        if (!Physics::RigidBodyNotificationBus::MultiHandler::BusIsConnectedId(entityId))
        {
            Physics::RigidBodyNotificationBus::MultiHandler::BusConnect(entityId);
        }

        if (state.m_entityActive)
        {
            // Announced again on an entity that is already live, so a body resting there has to see the flip. Its window is over.
            ApplyFlip(entityId, state, true);
        }
        else if (!AZ::EntityBus::MultiHandler::BusIsConnectedId(entityId))
        {
            // The colliders do not exist yet while the entity activates. Open the window and wait for the entity to finish. The
            // window opens before the connect, because connecting to an entity that is already active calls OnEntityActivated at
            // once, and that closes it.
            OpenWindow(state);
            AZ::EntityBus::MultiHandler::BusConnect(entityId);
        }
    }

    void Transform2ColliderSupport::OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        // A flip during play moves a collider under a resting body, so the body wakes.
        found->second.m_scale = scale;
        if (found->second.m_entityActive)
        {
            ApplyFlip(entityId, found->second, true);
        }
    }

    void Transform2ColliderSupport::OnTransform2Deactivated(AZ::EntityId entityId)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        // An entity that lives on after leaving Transform 2 gets its authored colliders back. When the entity is going
        // away its colliders are already gone, and the captured shapes have expired. With no sign applied the colliders
        // are the authored ones already, so the body is not touched or woken.
        const bool wasFlipped = found->second.m_appliedMask != 0;
        if (!found->second.m_shapes.empty() && wasFlipped)
        {
            RestoreAuthored(found->second);
            SyncRigidBody(entityId, AZ::Vector3::CreateOne(), true);
        }

        // The same for a force region. One whose entity is going away has undone its mirror when it deactivated, and then
        // there is no handler to hear this.
        if (wasFlipped)
        {
            ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::SetFlipMask, static_cast<AZ::u8>(0));
        }

        // An entity that is deactivated before it finished activating leaves its window open otherwise.
        CloseWindow(found->second);
        AZ::EntityBus::MultiHandler::BusDisconnect(entityId);
        Physics::RigidBodyNotificationBus::MultiHandler::BusDisconnect(entityId);
        m_entities.erase(found);
    }

    void Transform2ColliderSupport::OnEntityActivated(const AZ::EntityId& entityId)
    {
        auto found = m_entities.find(entityId);
        if (found == m_entities.end())
        {
            return;
        }

        // The window closes first: the colliders are all there now, and ApplyFlip captures them through the collider bus.
        // The colliders and the body are only being created, so the body keeps how it was configured, such as Start asleep.
        EntityState& state = found->second;
        CloseWindow(state);
        state.m_entityActive = true;
        ApplyFlip(entityId, state, false);

        // ApplyFlip tells the force region only when the mask changed, and the mask of this entity can be the same as the one
        // the last activation ended with. A force region undoes its mirror when it deactivates, so one that comes back must be
        // told again, or it would stay unflipped under an unchanged mask. A region that is flipped already ignores it.
        if (state.m_appliedMask != 0)
        {
            ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::SetFlipMask, static_cast<AZ::u8>(state.m_appliedMask));
        }
    }

    void Transform2ColliderSupport::OnPhysicsEnabled(const AZ::EntityId& entityId)
    {
        // Only a body that came after the flip needs this. One made before it is synced by the flip itself, and an entity
        // whose colliders are not flipped yet has nothing to mirror.
        auto found = m_entities.find(entityId);
        if (found != m_entities.end() && found->second.m_entityActive && found->second.m_appliedMask != 0)
        {
            // The body was just made, and PhysX has put it to sleep if it starts asleep. It stays that way.
            SyncRigidBody(entityId, Transform2Mirror::GetSigns(found->second.m_scale), false);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener: the activation window

    void Transform2ColliderSupport::OpenWindow(EntityState& state)
    {
        state.m_inWindow = true;
        ++m_windowCount;
        ConnectToDefaultScene();
    }

    void Transform2ColliderSupport::CloseWindow(EntityState& state)
    {
        if (!state.m_inWindow)
        {
            return;
        }

        state.m_inWindow = false;
        if (--m_windowCount == 0)
        {
            m_bodyAddedHandler.Disconnect();
        }
    }

    void Transform2ColliderSupport::ConnectToDefaultScene()
    {
        // A scene that is removed disconnects the handlers from its events, so a handler that is connected is on a live scene.
        // The default scene exists before the game entities activate. In the editor outside a game it does not, and there is
        // nothing to hear.
        auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
        if (sceneInterface == nullptr || m_bodyAddedHandler.IsConnected())
        {
            return;
        }

        const AzPhysics::SceneHandle defaultScene = sceneInterface->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName);
        if (defaultScene != AzPhysics::InvalidSceneHandle)
        {
            sceneInterface->RegisterSimulationBodyAddedHandler(defaultScene, m_bodyAddedHandler);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener: the bodies of the default scene

    void Transform2ColliderSupport::OnSimulationBodyAdded(AzPhysics::SceneHandle sceneHandle, AzPhysics::SimulatedBodyHandle bodyHandle)
    {
        // The body of a Transform 1 entity ends here, at one lookup in the map. So does the body of a Transform 2 entity that
        // is not in its window, which the nested activation of another entity can add.
        AzPhysics::SimulatedBody* body = FindBody(sceneHandle, bodyHandle);
        auto found = body ? m_entities.find(body->GetEntityId()) : m_entities.end();
        if (found == m_entities.end() || !found->second.m_inWindow)
        {
            return;
        }

        // The shape holds its authored pose and geometry now, because nothing has changed it yet. A collider component hands out
        // the same shapes, and the capture is by shape, so a shape that both reach is captured once. The entity is still
        // activating, so the shapes are flipped when it finishes.
        for (const AZStd::shared_ptr<Physics::Shape>& shape : GetBodyShapes(*body))
        {
            GetShapeFlip(found->second, shape);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // The listener: flipping the colliders of an entity

    void Transform2ColliderSupport::ApplyFlip(AZ::EntityId entityId, EntityState& state, bool wake)
    {
        const AZ::Vector3 signs = Transform2Mirror::GetSigns(state.m_scale);

        // Forget the shapes that are gone. AZStd::remove_if copies its elements, so it cannot take a unique_ptr.
        for (auto flip = state.m_shapes.begin(); flip != state.m_shapes.end();)
        {
            if ((*flip)->IsExpired())
            {
                flip = state.m_shapes.erase(flip);
            }
            else
            {
                ++flip;
            }
        }

        // Every collider component of the entity, so a shape is found no matter which component made it. Only the runtime
        // collider components answer this bus, so finding one also says the entity runs with a runtime collider.
        bool hasRuntimeCollider = false;
        ColliderComponentRequestBus::EnumerateHandlersId(
            entityId,
            [this, &state, &hasRuntimeCollider](ColliderComponentRequests* handler)
            {
                hasRuntimeCollider = true;
                for (const AZStd::shared_ptr<Physics::Shape>& shape : handler->GetShapes())
                {
                    GetShapeFlip(state, shape);
                }
                return true;
            });

        // Every shape captured, whether it came from a collider component just now or from a body the activation window heard
        // added for the entity (the White Box collider has no component that hands out its shape).
        for (const AZStd::unique_ptr<Transform2ShapeFlip>& flip : state.m_shapes)
        {
            flip->Apply(signs);
        }

        // Mass properties and sleep only depend on which sides are flipped, not on how large the scale is.
        const AZ::u32 mask = Transform2Mirror::GetSignMask(signs);
        if (mask != state.m_appliedMask)
        {
            // The applied mask is set before anything is sent, so nothing that answers an event can flip again and loop.
            state.m_appliedMask = mask;
            SyncRigidBody(entityId, signs, wake);

            // A pure flip fires no transform event, but the AABB cache of a collider reads the pose the flip just changed.
            ColliderShapeRequestBus::Event(entityId, &ColliderShapeRequests::InvalidateShapeCache);

            // A force region mirrors its local space forces and its spline. An entity with no force region has no handler.
            // The region carries the mask across the rebuild that the collider event below causes in it.
            ForceRegionRequestBus::Event(entityId, &ForceRegionRequests::SetFlipMask, static_cast<AZ::u8>(mask));

            // Force regions and surface data rebuild from the collider when it changes. Only a runtime collider is announced.
            // In the editor the rigid body component rebuilds its body on this event, which every flip would trigger.
            if (hasRuntimeCollider)
            {
                Physics::ColliderComponentEventBus::Event(entityId, &Physics::ColliderComponentEvents::OnColliderChanged);
            }
            else if (auto* boundsUnion = AZ::Interface<AzFramework::IEntityBoundsUnion>::Get())
            {
                // The editor colliders answer their bounds on the drawn side, and a pure flip fires no transform event, so an entity
                // with only colliders would keep the visibility bounds of the authored side. Once per change of the signs.
                boundsUnion->RefreshEntityLocalBoundsUnion(entityId);
            }
        }
    }

    void Transform2ColliderSupport::RestoreAuthored(EntityState& state)
    {
        const AZ::Vector3 noFlip = AZ::Vector3::CreateOne();
        for (const AZStd::unique_ptr<Transform2ShapeFlip>& flip : state.m_shapes)
        {
            flip->Apply(noFlip);
        }
        state.m_appliedMask = 0;
    }

    Transform2ShapeFlip& Transform2ColliderSupport::GetShapeFlip(EntityState& state, const AZStd::shared_ptr<Physics::Shape>& shape)
    {
        for (const AZStd::unique_ptr<Transform2ShapeFlip>& flip : state.m_shapes)
        {
            if (flip->IsShape(shape.get()))
            {
                return *flip;
            }
        }

        // First time this shape is seen: its pose is the authored pose, because nothing here has changed it yet.
        state.m_shapes.push_back(AZStd::make_unique<Transform2ShapeFlip>(shape));
        return *state.m_shapes.back();
    }

    const AzPhysics::RigidBodyConfiguration* Transform2ColliderSupport::FindRigidBodyConfiguration(AZ::EntityId entityId)
    {
        // The configuration stays with the PhysX rigid body component. A body made by anything else is not read.
        AZ::Entity* entity = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, entityId);
        RigidBodyComponent* component = entity ? entity->FindComponent<RigidBodyComponent>() : nullptr;
        return component ? &component->GetConfiguration() : nullptr;
    }

    void Transform2ColliderSupport::SyncRigidBody(AZ::EntityId entityId, const AZ::Vector3& signs, bool wake)
    {
        // Only a dynamic rigid body answers with a body. A static body has nothing to sync, and a body that does not
        // exist yet reads the flipped shapes itself when it is created.
        AzPhysics::RigidBody* body = nullptr;
        Physics::RigidBodyRequestBus::EventResult(body, entityId, &Physics::RigidBodyRequests::GetRigidBody);
        if (body == nullptr)
        {
            return;
        }

        // A kinematic body is moved by hand, so its mass properties do not matter.
        if (!body->IsKinematic())
        {
            // The mass is kept as it is. The center of mass and the inertia follow how the body was configured. One that is
            // computed is computed again from the mirrored shapes, and one that was authored stays authored. An authored
            // center of mass is mirrored with the colliders. An authored inertia is the moments about the local axes, which a
            // reflection across those axes leaves as they are. Without a configuration to read, the body is left alone.
            if (const AzPhysics::RigidBodyConfiguration* configuration = FindRigidBodyConfiguration(entityId))
            {
                AzPhysics::MassComputeFlags flags = AzPhysics::MassComputeFlags::NONE;
                if (configuration->m_computeCenterOfMass)
                {
                    flags = flags | AzPhysics::MassComputeFlags::COMPUTE_COM;
                }
                if (configuration->m_computeInertiaTensor)
                {
                    flags = flags | AzPhysics::MassComputeFlags::COMPUTE_INERTIA;
                }
                if (configuration->m_includeAllShapesInMassCalculation)
                {
                    flags = flags | AzPhysics::MassComputeFlags::INCLUDE_ALL_SHAPES;
                }

                body->UpdateMassProperties(
                    flags,
                    Transform2Mirror::MirrorPosition(signs, configuration->m_centerOfMassOffset),
                    configuration->m_inertiaTensor,
                    body->GetMass());
            }
        }

        // A body that starts asleep has been put to sleep when it was added, and a sync while it is made must not undo that.
        if (wake)
        {
            body->ForceAwake();
        }
    }
} // namespace PhysX
