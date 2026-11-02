/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Memory/Memory.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

#include <Integration/ActorComponentBus.h>

namespace EMotionFX
{
    class ActorInstance;

    namespace Integration
    {
        // The functions below that are defined in this header are inline on purpose. AtomActorInstance, in the EMotionFXAtom
        // gem, calls them to put the joints and the bounds of a flipped actor where they are drawn, and that gem does not
        // link the source file of this one.

        //////////////////////////////////////////////////////////////////////////
        // Root motion correction (pure math)

        //! The extra displacement that makes a root motion delta follow a signed per-axis scale.
        //! EMotionFX applies the delta unflipped. The target is the same delta scaled per axis in the actor's own frame:
        //! F * d, with F = R * diag(renderScale) * R^-1, so the correction is F * d - d.
        //! A single world space flip would mirror across the wrong plane on a rotated actor.
        //! @param appliedDelta The displacement EMotionFX applied this tick, in the space the actor instance lives in.
        //! @param actorRotation The (unit) actor rotation that EMotionFX used to build the delta.
        //! @param renderScale The signed per-axis scale on top of the actor, AZ::Transform2::RenderScale of the entity's scale.
        //! @return The displacement to add on top of appliedDelta.
        AZ::Vector3 CalcRootMotionCorrection(const AZ::Vector3& appliedDelta, const AZ::Quaternion& actorRotation, const AZ::Vector3& renderScale);

        //! The rotation that makes a root motion turn follow the flips of a signed scale: S * q * S^-1, so a mirrored actor
        //! turns the other way. Without it the path stops being a mirror image of the original, and an actor whose motion
        //! carries a large yaw keeps running the way it was. Only the signs matter, a rotation has no magnitude to scale.
        //! The same conjugation turns the rotation of a joint into the rotation it is drawn with.
        //! @param deltaRotation The rotation EMotionFX multiplied onto the actor this tick, in the actor's own frame.
        //! @param renderScale The signed per-axis scale on top of the actor, AZ::Transform2::RenderScale of the entity's scale.
        //! @return The rotation to use in its place.
        inline AZ::Quaternion CalcRootMotionRotation(const AZ::Quaternion& deltaRotation, const AZ::Vector3& renderScale)
        {
            // The vector part of a quaternion is an axis, which picks up the sign of the determinant when it is reflected.
            const float signX = renderScale.GetX() < 0.0f ? -1.0f : 1.0f;
            const float signY = renderScale.GetY() < 0.0f ? -1.0f : 1.0f;
            const float signZ = renderScale.GetZ() < 0.0f ? -1.0f : 1.0f;
            const float determinant = signX * signY * signZ;

            return AZ::Quaternion(
                determinant * signX * deltaRotation.GetX(),
                determinant * signY * deltaRotation.GetY(),
                determinant * signZ * deltaRotation.GetZ(),
                deltaRotation.GetW());
        }

        //! Reads the displacement and rotation that EMotionFX used this tick from the actor instance, and returns the correction.
        //! Call it after the actor instance was updated and before anything else moves it.
        AZ::Vector3 CalcActorRootMotionCorrection(const ActorInstance& actorInstance, const AZ::Vector3& renderScale);

        //! Reads the rotations that EMotionFX used this tick from the actor instance, and returns the rotation the actor
        //! should have ended the tick with. Call it at the same moment as CalcActorRootMotionCorrection.
        AZ::Quaternion CalcActorRootMotionRotation(const ActorInstance& actorInstance, const AZ::Vector3& renderScale);

        //////////////////////////////////////////////////////////////////////////
        // Pose frame mapping (pure math)
        //
        // The actor instance is posed at the stand-in scale of its entity, and the renderer draws that pose with the render
        // scale on top: drawn = world * diag(renderScale) * world^-1 * pose. The Editor picks against the pose, so a point or
        // a ray that was meant for the drawn actor has to be moved into the pose frame first.
        // world is the Transform 1 world of the entity. It holds only the uniform stand-in, which commutes with the diagonal.
        // renderScale is AZ::Transform2::RenderScale of the entity's scale, which is never zero on any axis.

        //! Scales per axis in the frame of the entity, about the entity origin: world * diag(scale) * world^-1.
        inline AZ::Matrix3x4 CalcScaleAboutEntity(const AZ::Transform& world, const AZ::Vector3& scale)
        {
            return AZ::Matrix3x4::CreateFromTransform(world)
                * AZ::Matrix3x4::CreateDiagonal(scale)
                * AZ::Matrix3x4::CreateFromTransform(world.GetInverse());
        }

        //! Where a point that is drawn at renderScale sits in the pose: world * diag(renderScale)^-1 * world^-1 * point.
        //! A flip never moves the entity origin, so the origin maps to itself.
        AZ::Vector3 MapPointIntoPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Vector3& point);

        //! Where a point of the pose is drawn: world * diag(renderScale) * world^-1 * point. The inverse of MapPointIntoPoseFrame.
        //! Not an isometry for a non-uniform scale, so a distance has to be measured after the hit point is mapped back.
        AZ::Vector3 MapPointFromPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Vector3& point);

        //! The box around where a box of the pose is drawn. A flip mirrors the box about the entity, and a non-uniform scale
        //! squeezes it about the entity, in the frame of the entity. A null box stays null.
        //! Pass an identity world for a box that is already in the frame of the entity, such as a local bounding box.
        inline AZ::Aabb MapAabbFromPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Aabb& poseBounds)
        {
            // The transform of a null box would be garbage, and the Editor treats a null box as "nothing to select".
            if (!poseBounds.IsValid())
            {
                return poseBounds;
            }

            // The box of a linear map is found from the extremes of each row, so a negative axis needs no special case.
            return poseBounds.GetTransformedAabb(CalcScaleAboutEntity(world, renderScale));
        }

        //////////////////////////////////////////////////////////////////////////
        // Joints of a drawn actor (pure math)
        //
        // A joint of the pose is drawn where its model space position is scaled by renderScale. A bone attachment or a script
        // that wants the drawn bone needs the joint mapped the same way: the position scaled per axis, the rotation conjugated
        // by the signs, so the frame of the joint stays right handed. The scale of the joint is kept.

        //! The signs of a render scale, one bit per axis: bit 0 is X, bit 1 is Y and bit 2 is Z, set when the axis is negative.
        //! A render scale is never zero on an axis. This is all that AtomActorInstance keeps of it, in one byte.
        inline AZ::u8 CalcRenderSignMask(const AZ::Vector3& renderScale)
        {
            return static_cast<AZ::u8>(
                (renderScale.GetX() < 0.0f ? 1 : 0) | (renderScale.GetY() < 0.0f ? 2 : 0) | (renderScale.GetZ() < 0.0f ? 4 : 0));
        }

        //! The render scale a sign mask stands for: one on every axis, negated on the marked ones.
        inline AZ::Vector3 CalcRenderSignScale(AZ::u8 signMask)
        {
            return AZ::Vector3((signMask & 1) ? -1.0f : 1.0f, (signMask & 2) ? -1.0f : 1.0f, (signMask & 4) ? -1.0f : 1.0f);
        }

        //! Where a joint is drawn, in the frame of the actor (model space): the position scaled per axis by renderScale,
        //! the rotation conjugated by its signs (CalcRootMotionRotation), the scale kept. The entity world transform placed in
        //! front of it gives the drawn joint in the world, which is where a bone attachment belongs.
        //! A render scale of one returns the joint as it was.
        inline AZ::Transform MapModelSpaceJointFromPoseFrame(const AZ::Transform& modelSpaceJoint, const AZ::Vector3& renderScale)
        {
            return AZ::Transform(
                modelSpaceJoint.GetTranslation() * renderScale,
                CalcRootMotionRotation(modelSpaceJoint.GetRotation(), renderScale),
                modelSpaceJoint.GetUniformScale());
        }

        //! Where a joint that was read in the world is drawn: world * MapModelSpaceJointFromPoseFrame(world^-1 * joint).
        //! The position is mirrored or squeezed about the entity in its own frame, so a hand 0.4 to the right of a pivot that is
        //! flipped on X is drawn 0.4 to its left, whichever way the entity faces.
        //! @param world The Transform 1 world of the entity, which holds only the uniform stand-in.
        //! @param renderScale AZ::Transform2::RenderScale of the entity's scale.
        //! @param poseJoint The joint in the world, as ActorComponentRequests::GetJointTransform reads it with Space::WorldSpace.
        AZ::Transform MapJointFromPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Transform& poseJoint);

        //////////////////////////////////////////////////////////////////////////
        // Transform 2 support for actors

        //! Owned by the EMotionFX integration SystemComponent. EMotionFX itself stays unflipped and the mesh listener flips
        //! what is drawn, so this class only corrects root motion: running right under an X flip has to move the entity left.
        //! Only Transform 2 entities ever raise the notifications this class listens to, so a project with no Transform 2
        //! entity pays for one idle handler object. The tick connects only while a Transform 2 entity has an actor instance.
        class Transform2ActorSupport
            : public AZ::Transform2NotificationBus::Handler
            , public ActorComponentNotificationBus::MultiHandler
            , public AZ::TickBus::Handler
        {
        public:
            AZ_CLASS_ALLOCATOR_DECL

            Transform2ActorSupport();
            ~Transform2ActorSupport() override;

            //! Reflects the script method Transform2GetJointWorldTransform. Called by the EMotionFX SystemComponent.
            static void Reflect(AZ::ReflectContext* context);

        private:
            ////////////////////////////////////////////////////////////////////////
            // AZ::Transform2NotificationBus::Handler
            void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override;
            void OnTransform2Deactivated(AZ::EntityId entityId) override;
            ////////////////////////////////////////////////////////////////////////

            ////////////////////////////////////////////////////////////////////////
            // ActorComponentNotificationBus::MultiHandler
            void OnActorInstanceCreated(ActorInstance* actorInstance) override;
            void OnActorInstanceDestroyed(ActorInstance* actorInstance) override;
            ////////////////////////////////////////////////////////////////////////

            ////////////////////////////////////////////////////////////////////////
            // AZ::TickBus::Handler
            void OnTick(float deltaTime, AZ::ScriptTimePoint timePoint) override;
            int GetTickOrder() override;
            ////////////////////////////////////////////////////////////////////////

            //! Forgets the actor instance of an entity and stops ticking once no Transform 2 actor is left.
            void RemoveActorInstance(AZ::EntityId entityId);

            //! The script method behind Transform2GetJointWorldTransform: the world transform of a joint as it is drawn.
            //! A Transform 1 entity has no Transform 2 and gets the plain read, with no error. An entity with no actor, or a
            //! joint that is not found, reports an error and gives identity.
            static AZ::Transform GetJointWorldTransformChecked(AZ::EntityId entityId, const AZStd::string& jointName);

            //! True when SystemComponent::ApplyMotionExtraction moved the entity by this actor instance's delta this tick.
            static bool IsMotionExtracted(const ActorInstance& actorInstance);

            //! Moves the entity by the correction the same way SystemComponent does: character controller velocity,
            //! a custom motion extraction controller, or the entity transform. Then gives it the corrected rotation.
            static void ApplyCorrection(AZ::EntityId entityId, const AZ::Vector3& correction, const AZ::Quaternion& rotation, float deltaTime);

            struct PendingCorrection
            {
                AZ::EntityId m_entityId;
                AZ::Vector3 m_correction = AZ::Vector3::CreateZero();
                AZ::Quaternion m_rotation = AZ::Quaternion::CreateIdentity();
            };

            //! Transform 2 entities that currently have an actor instance. The instance is owned by the ActorComponent.
            AZStd::unordered_map<AZ::EntityId, ActorInstance*> m_actorInstances;

            //! Scratch list reused every tick, so applying corrections never walks m_actorInstances while it can change.
            AZStd::vector<PendingCorrection> m_pendingCorrections;
        };
    } // namespace Integration
} // namespace EMotionFX
