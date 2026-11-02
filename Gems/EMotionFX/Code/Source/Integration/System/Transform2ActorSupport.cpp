/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/RTTI/BehaviorContext.h>

#include <AzFramework/Physics/CharacterBus.h>

#include <EMotionFX/Source/Actor.h>
#include <EMotionFX/Source/ActorInstance.h>
#include <EMotionFX/Source/Allocators.h>
#include <EMotionFX/Source/EMotionFXConfig.h>
#include <EMotionFX/Source/Transform.h>

#include <Integration/MotionExtractionBus.h>
#include <Integration/System/CVars.h>
#include <Integration/System/Transform2ActorSupport.h>

namespace EMotionFX
{
    namespace Integration
    {
        //////////////////////////////////////////////////////////////////////////
        // Root motion correction (pure math)

        AZ::Vector3 CalcRootMotionCorrection(const AZ::Vector3& appliedDelta, const AZ::Quaternion& actorRotation, const AZ::Vector3& renderScale)
        {
            // F * d: undo the actor rotation, scale per axis in the actor's own frame, rotate back. The rotation is a
            // unit quaternion, so its conjugate is its inverse.
            const AZ::Vector3 actorSpaceDelta = actorRotation.GetConjugate().TransformVector(appliedDelta);
            const AZ::Vector3 scaledDelta = actorRotation.TransformVector(actorSpaceDelta * renderScale);
            return scaledDelta - appliedDelta;
        }

        namespace
        {
            // ExtractMotion rotated the delta by the actor rotation from before this tick, and ApplyMotionExtractionDelta
            // then multiplied the delta rotation onto it. Take the delta rotation back off to get the rotation that was used.
            AZ::Quaternion GetActorRotationBeforeTick(const ActorInstance& actorInstance)
            {
                const AZ::Quaternion rotationBeforeTick = actorInstance.GetLocalSpaceTransform().m_rotation
                    * actorInstance.GetTrajectoryDeltaTransform().m_rotation.GetNormalized().GetConjugate();
                return rotationBeforeTick.GetNormalized();
            }
        } // namespace

        AZ::Vector3 CalcActorRootMotionCorrection(const ActorInstance& actorInstance, const AZ::Vector3& renderScale)
        {
            const Transform& trajectoryDelta = actorInstance.GetTrajectoryDeltaTransform();
            const Transform& actorTransform = actorInstance.GetLocalSpaceTransform();

            // ActorInstance::ApplyMotionExtractionDelta added the delta to the actor position, multiplied per axis by the
            // actor scale. The delta is already in the space the actor instance lives in: MotionInstance::ExtractMotion
            // rotated it by the actor rotation. That space is the entity world when the actor is not an attachment.
            AZ::Vector3 appliedDelta = trajectoryDelta.m_position;
            EMFX_SCALECODE(appliedDelta *= actorTransform.m_scale;)

            return CalcRootMotionCorrection(appliedDelta, GetActorRotationBeforeTick(actorInstance), renderScale);
        }

        AZ::Quaternion CalcActorRootMotionRotation(const ActorInstance& actorInstance, const AZ::Vector3& renderScale)
        {
            const AZ::Quaternion deltaRotation = actorInstance.GetTrajectoryDeltaTransform().m_rotation.GetNormalized();
            return (GetActorRotationBeforeTick(actorInstance) * CalcRootMotionRotation(deltaRotation, renderScale)).GetNormalized();
        }

        //////////////////////////////////////////////////////////////////////////
        // Pose frame mapping (pure math)

        AZ::Vector3 MapPointIntoPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Vector3& point)
        {
            return CalcScaleAboutEntity(world, renderScale.GetReciprocal()).TransformPoint(point);
        }

        AZ::Vector3 MapPointFromPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Vector3& point)
        {
            return CalcScaleAboutEntity(world, renderScale).TransformPoint(point);
        }

        //////////////////////////////////////////////////////////////////////////
        // Joints of a drawn actor (pure math)

        AZ::Transform MapJointFromPoseFrame(const AZ::Transform& world, const AZ::Vector3& renderScale, const AZ::Transform& poseJoint)
        {
            // The joint is a model space joint placed by the world, and the renderer draws the model space joint scaled.
            return world * MapModelSpaceJointFromPoseFrame(world.GetInverse() * poseJoint, renderScale);
        }

        //////////////////////////////////////////////////////////////////////////
        // Transform2ActorSupport

        AZ_CLASS_ALLOCATOR_IMPL(Transform2ActorSupport, EMotionFXAllocator);

        Transform2ActorSupport::Transform2ActorSupport()
        {
            AZ::Transform2NotificationBus::Handler::BusConnect();
        }

        Transform2ActorSupport::~Transform2ActorSupport()
        {
            AZ::TickBus::Handler::BusDisconnect();
            ActorComponentNotificationBus::MultiHandler::BusDisconnect();
            AZ::Transform2NotificationBus::Handler::BusDisconnect();
        }

        //////////////////////////////////////////////////////////////////////////
        // Script methods

        void Transform2ActorSupport::Reflect(AZ::ReflectContext* context)
        {
            // The plain joint read of ActorComponentRequestBus is reflected to script and stays unflipped, because EMotionFX
            // poses the actor unflipped and its own users of that read depend on it. This method is the flipped read.
            if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
            {
                behaviorContext->Method("Transform2GetJointWorldTransform", &Transform2ActorSupport::GetJointWorldTransformChecked,
                    { { { "EntityId", "The entity with the actor." }, { "JointName", "The name of the joint. Not case sensitive." } } })
                    ->Attribute(AZ::Script::Attributes::ToolTip,
                        "Gets the world transform of a joint of an actor where the joint is drawn. On an entity with a Transform 2 "
                        "the joint is mirrored by the signed world scale about the entity, with the flips of its parents in it, so "
                        "the hand of a character flipped on X is read on the side it is drawn. "
                        "On a Transform 1 entity it is the same as the plain read, with no error. "
                        "ActorComponentRequestBus GetJointTransform stays unflipped: it gives the pose before the flip, which is "
                        "what the animation system works with. Reports an error when the entity has no actor or the joint is not found.")
                    ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                    ->Attribute(AZ::Script::Attributes::Category, "Entity")
                    ->Attribute(AZ::Script::Attributes::Module, "entity")
                    ;
            }
        }

        AZ::Transform Transform2ActorSupport::GetJointWorldTransformChecked(AZ::EntityId entityId, const AZStd::string& jointName)
        {
            // An actor component that has no actor instance yet answers the joint queries by dereferencing it.
            EMotionFX::ActorInstance* actorInstance = nullptr;
            ActorComponentRequestBus::EventResult(actorInstance, entityId, &ActorComponentRequests::GetActorInstance);
            AZ_Error("Transform2ActorSupport", actorInstance,
                "Entity %s has no actor to read the joint \"%s\" from. It has no actor component, or its actor is not loaded yet.",
                entityId.ToString().c_str(), jointName.c_str());
            if (!actorInstance)
            {
                return AZ::Transform::CreateIdentity();
            }

            size_t jointIndex = ActorComponentRequests::s_invalidJointIndex;
            ActorComponentRequestBus::EventResult(jointIndex, entityId, &ActorComponentRequests::GetJointIndexByName, jointName.c_str());
            AZ_Error("Transform2ActorSupport", jointIndex != ActorComponentRequests::s_invalidJointIndex,
                "Entity %s has no joint named \"%s\".", entityId.ToString().c_str(), jointName.c_str());
            if (jointIndex == ActorComponentRequests::s_invalidJointIndex)
            {
                return AZ::Transform::CreateIdentity();
            }

            AZ::Transform joint = AZ::Transform::CreateIdentity();
            ActorComponentRequestBus::EventResult(joint, entityId, &ActorComponentRequests::GetJointTransform, jointIndex, Space::WorldSpace);

            // The scale is read once. An entity without a Transform 2 has no handler, which leaves the scale at one.
            // The world scale, so a joint of a child under a flipped parent is drawn on the flipped side too.
            AZ::Vector3 scale = AZ::Vector3::CreateOne();
            AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetWorldScale);

            const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);
            if (renderScale.IsClose(AZ::Vector3::CreateOne()))
            {
                return joint;
            }

            AZ::Transform world = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(world, entityId, &AZ::TransformBus::Events::GetWorldTM);
            return MapJointFromPoseFrame(world, renderScale, joint);
        }

        //////////////////////////////////////////////////////////////////////////
        // Transform 2 notifications

        void Transform2ActorSupport::OnTransform2Activated(AZ::EntityId entityId, [[maybe_unused]] const AZ::Vector3& scale)
        {
            // The Transform activates before the actor does. Listening on the entity delivers the actor instance whenever it
            // is created, or right now when it already exists. An entity without an actor never gets an event and costs
            // nothing per tick.
            ActorComponentNotificationBus::MultiHandler::BusConnect(entityId);
        }

        void Transform2ActorSupport::OnTransform2Deactivated(AZ::EntityId entityId)
        {
            ActorComponentNotificationBus::MultiHandler::BusDisconnect(entityId);
            RemoveActorInstance(entityId);
        }

        //////////////////////////////////////////////////////////////////////////
        // Actor instance tracking

        void Transform2ActorSupport::OnActorInstanceCreated(ActorInstance* actorInstance)
        {
            m_actorInstances[actorInstance->GetEntityId()] = actorInstance;

            // Root motion is only known after the animation update, so the correction needs a tick.
            AZ::TickBus::Handler::BusConnect();
        }

        void Transform2ActorSupport::OnActorInstanceDestroyed(ActorInstance* actorInstance)
        {
            RemoveActorInstance(actorInstance->GetEntityId());
        }

        void Transform2ActorSupport::RemoveActorInstance(AZ::EntityId entityId)
        {
            m_actorInstances.erase(entityId);
            if (m_actorInstances.empty())
            {
                AZ::TickBus::Handler::BusDisconnect();
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Root motion

        int Transform2ActorSupport::GetTickOrder()
        {
            // SystemComponent::OnTick updates the animation and applies the root motion at TICK_ANIMATION. The correction
            // needs that delta, and has to land before the physics system consumes the character velocity at
            // TICK_PHYSICS_SYSTEM.
            return AZ::TICK_ANIMATION + 1;
        }

        void Transform2ActorSupport::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint timePoint)
        {
            // SystemComponent::OnTick does not update the animation while this is off, and the last delta would repeat.
            if (!CVars::emfx_updateEnabled)
            {
                return;
            }

            // Work out every correction first and apply them second. Applying moves entities, and whatever listens to
            // that may add or remove actor instances.
            m_pendingCorrections.clear();
            for (const auto& [entityId, actorInstance] : m_actorInstances)
            {
                if (!IsMotionExtracted(*actorInstance))
                {
                    continue;
                }

                // The scale is read live, so a flip made earlier in this frame is already included. It is the world scale, so an
                // actor on a child of a flipped entity runs the way the flipped parent faces.
                AZ::Vector3 scale = AZ::Vector3::CreateOne();
                AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetWorldScale);

                // An unflipped T2 moves exactly as EMotionFX did.
                const AZ::Vector3 renderScale = AZ::Transform2::RenderScale(scale);
                if (renderScale.IsClose(AZ::Vector3::CreateOne()))
                {
                    continue;
                }

                // A motion can turn on the spot, so a rotation counts even when the displacement is zero.
                const AZ::Vector3 correction = CalcActorRootMotionCorrection(*actorInstance, renderScale);
                const AZ::Quaternion rotation = CalcActorRootMotionRotation(*actorInstance, renderScale);
                const bool turned = !rotation.IsClose(actorInstance->GetLocalSpaceTransform().m_rotation, AZ::Constants::FloatEpsilon);
                if (turned || !correction.IsZero())
                {
                    m_pendingCorrections.push_back({ entityId, correction, rotation });
                }
            }

            for (const PendingCorrection& pending : m_pendingCorrections)
            {
                ApplyCorrection(pending.m_entityId, pending.m_correction, pending.m_rotation, deltaTime);
            }
            m_pendingCorrections.clear();
        }

        bool Transform2ActorSupport::IsMotionExtracted(const ActorInstance& actorInstance)
        {
            // These are the conditions under which SystemComponent::ApplyMotionExtraction moved the entity this tick.
            // The motion extraction flag is also what lets the editor skip it outside game mode. When any of them fails,
            // the trajectory delta may be stale and must not be applied again.
            // Attachments are left out: their actor transform is not the entity world transform.
            return actorInstance.GetIsEnabled()
                && actorInstance.GetEntity()
                && actorInstance.GetMotionExtractionEnabled()
                && actorInstance.GetActor()->GetMotionExtractionNode()
                && !actorInstance.GetIsAttachment();
        }

        void Transform2ActorSupport::ApplyCorrection(AZ::EntityId entityId, const AZ::Vector3& correction, const AZ::Quaternion& rotation, float deltaTime)
        {
            // Same choice as SystemComponent::ApplyMotionExtraction.
            bool hasPhysicsController = false;
            Physics::CharacterRequestBus::EventResult(hasPhysicsController, entityId, &Physics::CharacterRequests::IsPresent);

            bool movedByController = true;
            if (hasPhysicsController)
            {
                // The controller adds up the velocities requested for the tick, and moves by the sum when physics steps
                // after this tick. So the correction is one more velocity request, on top of the one SystemComponent made.
                const float deltaTimeInv = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;
                Physics::CharacterRequestBus::Event(entityId, &Physics::CharacterRequests::AddVelocityForTick, correction * deltaTimeInv);
            }
            else if (MotionExtractionRequestBus::FindFirstHandler(entityId))
            {
                MotionExtractionRequestBus::Event(entityId, &MotionExtractionRequestBus::Events::ExtractMotion, correction, deltaTime);
            }
            else
            {
                movedByController = false;
            }

            // SystemComponent already wrote the uncorrected rotation, and the uncorrected position when no controller moves
            // the entity. Writing the entity updates the actor instance through the transform notification, so both end up
            // at the corrected values.
            AZ::Transform world = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(world, entityId, &AZ::TransformBus::Events::GetWorldTM);
            if (!movedByController)
            {
                world.SetTranslation(world.GetTranslation() + correction);
            }
            world.SetRotation(rotation);
            AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetWorldTM, world);
        }
    } // namespace Integration
} // namespace EMotionFX
