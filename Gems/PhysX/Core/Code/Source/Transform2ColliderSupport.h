/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/optional.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzCore/std/smart_ptr/weak_ptr.h>
#include <AzFramework/Physics/Common/PhysicsEvents.h>
#include <AzFramework/Physics/Common/PhysicsTypes.h>
#include <AzFramework/Physics/Configuration/RigidBodyConfiguration.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Physics/Shape.h>
#include <AzFramework/Physics/ShapeConfiguration.h>

#include <PxPhysicsAPI.h>

namespace PhysX
{
    //////////////////////////////////////////////////////////////////////////
    // Mirror math
    //
    // A Transform 2 flip is a reflection across the local planes named by a sign vector, with -1 on every flipped
    // axis and +1 elsewhere. A flip never moves or turns the entity. Only what is expressed inside the flipped space
    // mirrors: the offset and rotation of each collider, and the geometry of the meshes.

    namespace Transform2Mirror
    {
        //! Reduces a signed scale to its signs: -1 for a negative axis, +1 for any other.
        AZ::Vector3 GetSigns(const AZ::Vector3& scale);

        //! The bit mask of the negative axes of a sign vector: X is 1, Y is 2 and Z is 4. Zero means no flip.
        AZ::u32 GetSignMask(const AZ::Vector3& signs);

        //! The sign vector of a bit mask, the inverse of GetSignMask: -1 on every axis whose bit is set, +1 elsewhere.
        AZ::Vector3 GetMaskSigns(AZ::u32 mask);

        //! Mirrors an offset: p' = M p.
        AZ::Vector3 MirrorPosition(const AZ::Vector3& signs, const AZ::Vector3& position);

        //! Mirrors a rotation: M R M. The vector part keeps its component on a single flipped axis and negates the other two.
        //! X flip gives (qx, -qy, -qz, qw). Z flip gives (-qx, -qy, qz, qw).
        AZ::Quaternion MirrorRotation(const AZ::Vector3& signs, const AZ::Quaternion& rotation);

        // Joint frames
        //
        // A joint frame is authored in the local space of the follower body, so it mirrors with the body (D.39): the frame (p, R)
        // becomes (M p, M R M), which is a proper rotation. A hinge turns about the X axis of its frame and a prismatic joint slides
        // along it, and the mirror can reverse that axis relative to the motion. The mirrored frame is then turned half a turn about
        // its own Y axis, which reverses X again, so the angle, the limits, the position readback and the motor values stay as
        // they were authored. Nothing authored is written: a joint takes the frame it is given and returns the one it uses.
        // A joint is correct at creation only (D.42), and D6 has no authored frame, so it is not flipped.

        //! True when the mirror reverses the axis a hinge turns about, the X axis of its frame: a turn by theta becomes a turn
        //! by sy * sz * theta about the mirrored X axis.
        bool ReversesHingeAxis(const AZ::Vector3& signs);

        //! True when the mirror reverses the axis a prismatic joint slides along, the X axis of its frame: a slide by t becomes
        //! a slide by sx * t along the mirrored X axis.
        bool ReversesSlideAxis(const AZ::Vector3& signs);

        //! The frame a flipped follower holds: (M p, M R M), turned half a turn about Y when the half turn is set. The frame is
        //! returned as it is when the signs name no flip, so a follower that is not flipped changes nothing. The scale is kept.
        AZ::Transform MirrorJointFrame(const AZ::Vector3& signs, const AZ::Transform& frame, bool halfTurn);

        //! The frame that MirrorJointFrame turns into the given one, for what a manipulator writes. Mirroring is an involution,
        //! so the same signs and the same half turn take a frame there and back.
        AZ::Transform UnmirrorJointFrame(const AZ::Vector3& signs, const AZ::Transform& frame, bool halfTurn);

        //! The signs of the scale that the Transform 2 of an entity serves, for the debug draw, the pick and the bounds queries of
        //! the editor viewport. All positive for an entity without a Transform 2, which costs a Transform 1 one request bus lookup
        //! per draw, pick or bounds query. Editor paths only: it must not appear on a runtime path of a collider.
        AZ::Vector3 GetEntitySigns(AZ::EntityId entityId);

        //! A copy of a collider configuration with its offset and rotation mirrored, for the debug draw of a flipped collider.
        //! Empty when the signs name no flip, so an entity that is not flipped copies nothing.
        AZStd::optional<Physics::ColliderConfiguration> MirrorConfiguration(
            const AZ::Vector3& signs, const Physics::ColliderConfiguration& colliderConfig);

        // Picking and bounds in the editor
        //
        // The editor-world body of a collider is built from the authored values and stays on the authored side. It is rebuilt
        // after every edit, later than any event that could flip it, and the game needs no flip of it, so it is not flipped.
        // The wireframe is drawn mirrored, and the queries of the editor reach the drawn side by mirroring about the entity:
        // W * diag(signs) * W^-1, with W the world transform of the entity without its scale, which a uniform scale cancels
        // out of. That is a reflection, or a rotation by pi for two flipped axes, so it is an isometry: a ray mirrored onto the
        // authored side keeps its distances, and a hit distance needs no mapping back. Each of these returns its input as it is
        // when the signs name no flip.
        //
        // Limit: everything else that reads the editor-world body or the authored collider data stays unflipped. That is every scene
        // query of the editor scene from another system, and ColliderShapeRequestBus.

        //! Mirrors a world point about an entity.
        AZ::Vector3 MirrorWorldPoint(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Vector3& point);

        //! Mirrors a world direction about an entity. The result has the length of the input.
        AZ::Vector3 MirrorWorldDirection(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Vector3& direction);

        //! Mirrors a world Aabb about an entity: the box of its mirrored corners, so it is exact when the entity is not rotated and
        //! never smaller than the box of the mirrored shape when it is. A null box stays null.
        AZ::Aabb MirrorWorldAabb(const AZ::Transform& world, const AZ::Vector3& signs, const AZ::Aabb& aabb);
    } // namespace Transform2Mirror

    //////////////////////////////////////////////////////////////////////////
    // One shape

    //! Mirrors one collider shape across the local axes named by a sign vector.
    //! It captures the authored pose and geometry when it is created and derives every flipped state from that capture,
    //! never from the current state, so flipping twice, or flipping back, lands on the authored shape.
    //!  - Box, sphere and capsule keep their geometry, because the shapes are symmetric. Only the local pose mirrors.
    //!  - Triangle mesh takes the sign in its mesh scale.
    //!  - Convex mesh cannot take a negative scale, so a mirrored copy of the hull is cooked once and swapped in.
    //!  - Heightfields are left alone.
    class Transform2ShapeFlip
    {
    public:
        explicit Transform2ShapeFlip(const AZStd::shared_ptr<Physics::Shape>& shape);
        ~Transform2ShapeFlip();

        Transform2ShapeFlip(const Transform2ShapeFlip&) = delete;
        Transform2ShapeFlip& operator=(const Transform2ShapeFlip&) = delete;

        //! True when the shape is the one this flip captured, and it is still alive.
        bool IsShape(const Physics::Shape* shape) const;

        //! True when the shape this flip captured no longer exists.
        bool IsExpired() const;

        //! Mirrors the authored shape across the axes with a negative sign. All positive signs restore the authored shape.
        void Apply(const AZ::Vector3& signs);

    private:
        //! Mirrors the geometry of a triangle or convex mesh shape. Returns false when the geometry was left as it was.
        bool ApplyGeometry(physx::PxShape& pxShape, const AZ::Vector3& signs);

        //! The convex hull of the authored mesh with its points mirrored, cooked on first use and then cached.
        physx::PxConvexMesh* GetFlippedConvexMesh(const AZ::Vector3& signs);

        AZStd::weak_ptr<Physics::Shape> m_shape;
        bool m_canMirror = false; //!< False for a shape this support cannot mirror, so it is skipped.
        bool m_canMirrorGeometry = false; //!< False when a mesh scale has a rotation, so only the pose of the shape mirrors.

        // Authored capture
        AZ::Vector3 m_authoredPosition = AZ::Vector3::CreateZero();
        AZ::Quaternion m_authoredRotation = AZ::Quaternion::CreateIdentity();
        physx::PxGeometryType::Enum m_geometryType = physx::PxGeometryType::eINVALID;
        physx::PxVec3 m_authoredTriangleMeshScale = physx::PxVec3(1.0f);
        physx::PxConvexMesh* m_authoredConvexMesh = nullptr; //!< Holds a reference of its own, released with this flip.
        physx::PxMeshScale m_authoredConvexScale;
        physx::PxConvexMeshGeometryFlags m_authoredConvexFlags;

        // Flipped state
        AZ::u32 m_appliedMask = 0; //!< The sign mask of the geometry on the shape now. Zero is the authored geometry.
        AZStd::array<AZStd::unique_ptr<Physics::CookedMeshShapeConfiguration>, 8> m_flippedConvexMeshes; //!< By sign mask.
    };

    //////////////////////////////////////////////////////////////////////////
    // The listener

    //! Keeps the colliders of Transform 2 entities in step with the signs of their scale.
    //! Owned by the PhysX system component, and the only thing that ever touches a collider for a flip. Nothing is added
    //! to collider components, so entities with a Transform 1 never reach this code.
    //! The same signs are pushed to the force region of the entity, if it has one, through ForceRegionRequestBus::SetFlipMask.
    //! The region mirrors its local space forces and its spline from that, and keeps its own record of the mask, which it clears
    //! when it deactivates. So the push is repeated when the entity finishes activating, even under an unchanged mask.
    //! The state kept here exists only for Transform 2 entities, and is dropped when the entity deactivates.
    //!
    //! The activation window
    //! The shapes of the collider components are reached through the collider bus when the entity finishes activating. The
    //! White Box collider answers no collider bus: it makes its own body in the default physics scene, in its Activate, and its
    //! shapes live and die with that body. It depends on the Transform2Service, so it activates after the Transform 2 of its
    //! entity and before the entity finishes. So such a body is always made in a window: from the announcement of the Transform 2
    //! (OnTransform2Activated) to the end of the activation of that entity (OnEntityActivated), or the deactivation of the
    //! Transform 2 if that comes first. The listener hears the bodies of the default scene only inside a window, and captures
    //! the shapes of a body that belongs to the entity of that window. They are flipped when the entity finishes, like the rest.
    //! Between windows it holds no handler on the scene, so a Transform 1 entity pays nothing. A body that a Transform 1 entity
    //! adds while a Transform 2 entity is still activating (a nested activation only) costs one scene lookup, one virtual call
    //! and one find in the map.
    //! The default scene is made when a game starts, before the game entities activate, and removed when the game ends. Outside a
    //! game, in the editor, it does not exist, so there is nothing to hear (the editor-world body of a White Box stays on the
    //! authored side). A body that is removed is not heard: the shape of a collider component outlives its body and keeps its one
    //! capture, and the shape of a White Box dies with its body and is forgotten by the next flip.
    //! Limit: a White Box collider that is activated on an entity whose Transform 2 is live already is not flipped.
    class Transform2ColliderSupport
        : public AZ::Transform2NotificationBus::Handler
        , public AZ::EntityBus::MultiHandler
        , public Physics::RigidBodyNotificationBus::MultiHandler
    {
    public:
        AZ_CLASS_ALLOCATOR(Transform2ColliderSupport, AZ::SystemAllocator);

        Transform2ColliderSupport();
        ~Transform2ColliderSupport() override;

        //! The signs of the scale of a Transform 2 entity as this listener knows them, or all positive when it knows none.
        //! A joint reads this once, when it makes its native joint (D.42), so it is not a bus lookup and it returns at once when
        //! the listener holds no entity: a Transform 1 project pays a pointer test and an empty test per joint made.
        //! The listener learns the scale when the Transform 2 of the entity activates, which is before the entity finishes
        //! activating and so before the rigid body, which makes its body then, can enable the joint. It reads the scale itself,
        //! and not the mask applied to the colliders, because that is only set when the entity finishes activating.
        static AZ::Vector3 FindSigns(AZ::EntityId entityId);

        //! How many entities have an open activation window, or zero when there is no listener. For the tests: every window that
        //! is opened must be closed, by the entity finishing its activation or by the Transform 2 deactivating.
        static AZ::u32 GetOpenWindowCount();

        //! True while the listener holds a handler on the default scene. For the tests: with no window open it holds none.
        static bool IsHearingBodies();

    private:
        //! What is known about one Transform 2 entity.
        struct EntityState
        {
            AZ::Vector3 m_scale = AZ::Vector3::CreateOne(); //!< The effective signed world scale last reported, with the flips of every parent in it.
            bool m_entityActive = false; //!< True once the entity finished activating, so its colliders exist.
            bool m_inWindow = false; //!< True from the announcement until the entity finishes activating. Counted in m_windowCount.
            AZ::u32 m_appliedMask = 0; //!< The sign mask last put on the colliders. Zero is the authored state.
            AZStd::vector<AZStd::unique_ptr<Transform2ShapeFlip>> m_shapes; //!< The authored capture of each collider and body shape.
        };

        // AZ::Transform2NotificationBus::Handler overrides...
        void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override;
        void OnTransform2Deactivated(AZ::EntityId entityId) override;
        void OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale) override;

        // AZ::EntityBus::MultiHandler overrides...
        void OnEntityActivated(const AZ::EntityId& entityId) override;

        // Physics::RigidBodyNotificationBus::MultiHandler overrides...
        // A rigid body made after the colliders were flipped is configured with the authored mass properties, so they
        // are mirrored once its physics is enabled.
        void OnPhysicsEnabled(const AZ::EntityId& entityId) override;

        // The bodies of the default scene, heard from m_bodyAddedHandler inside an activation window only.
        // A body of an entity in its window has its shapes captured, and they are flipped when the entity finishes activating.
        // Any other body, such as that of a Transform 1 entity, is left alone.
        void OnSimulationBodyAdded(AzPhysics::SceneHandle sceneHandle, AzPhysics::SimulatedBodyHandle bodyHandle);

        //! Opens the window of an entity that has announced itself and not finished activating, and starts to hear the default
        //! scene if it is not heard yet. The entity has no open window.
        void OpenWindow(EntityState& state);

        //! Closes the window of the entity, if it is open, and stops hearing the default scene when no window is left. The flag
        //! of the entity keeps the count from closing twice.
        void CloseWindow(EntityState& state);

        //! Connects m_bodyAddedHandler to the default scene, if the scene exists and the handler is not connected yet.
        void ConnectToDefaultScene();

        //! Flips every captured shape of the entity to match the scale held in the state. The shapes of the collider components
        //! are captured here, and the shapes of bodies as the window hears them added. When the sign mask changes, the
        //! body is synced, the collider AABB cache is dropped and the watchers of the collider are told it changed.
        //! The wake flag is passed on to the body sync: false while the colliders and the body are only being created.
        void ApplyFlip(AZ::EntityId entityId, EntityState& state, bool wake);

        //! Puts back the authored state of the shapes already captured. Captures nothing new.
        void RestoreAuthored(EntityState& state);

        //! Finds the capture of a shape, or captures it now.
        Transform2ShapeFlip& GetShapeFlip(EntityState& state, const AZStd::shared_ptr<Physics::Shape>& shape);

        //! How the PhysX rigid body component of the entity was configured, or null when it has none.
        static const AzPhysics::RigidBodyConfiguration* FindRigidBodyConfiguration(AZ::EntityId entityId);

        //! Brings the dynamic rigid body of the entity, if there is one, up to date with its flipped shapes. The signs name the
        //! flipped axes, and all positive signs restore the authored mass properties. The body is woken only when the wake flag
        //! is set: a body that is being created keeps how it was configured, such as Start asleep, while a flip during play
        //! moves a collider under a resting body, which has to wake.
        void SyncRigidBody(AZ::EntityId entityId, const AZ::Vector3& signs, bool wake);

        AZStd::unordered_map<AZ::EntityId, EntityState> m_entities;
        AzPhysics::SceneEvents::OnSimulationBodyAdded::Handler m_bodyAddedHandler; //!< On the default scene while a window is open.
        AZ::u32 m_windowCount = 0; //!< The number of entities with m_inWindow set.
    };
} // namespace PhysX
