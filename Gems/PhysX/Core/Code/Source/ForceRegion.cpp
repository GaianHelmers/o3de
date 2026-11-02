/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <Source/ForceRegion.h>

#include <PhysX/ColliderShapeBus.h>
#include <PhysX/MeshAsset.h>
#include <Source/Transform2ColliderSupport.h>
#include <Source/Utils.h>

#include <AzCore/Math/Color.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzFramework/Physics/ColliderComponentBus.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Translation/TranslationDef.h>

namespace PhysX
{
    /// Aggregates the AABB of all trigger collider components in an entity.
    struct TriggerAabbAggregator
    {
        AZ::Aabb operator()(AZ::Aabb& lhs, const AZ::Aabb& rhs) const
        {
            if (rhs == AZ::Aabb::CreateNull()) // Ignore non-trigger colliders that may have null AABB.
            {
                return lhs;
            }
            else
            {
                lhs.AddAabb(rhs);
                return lhs;
            }
        }
    };

    /// Aggregates points on trigger collider components in an entity.
    struct TriggerRandomPointsAggregator
    {
        Utils::Geometry::PointList operator()(Utils::Geometry::PointList& left
            , const Utils::Geometry::PointList& right) const
        {
            Utils::Geometry::PointList combinedPoints;
            combinedPoints.reserve(left.size() + right.size());
            combinedPoints.insert(combinedPoints.end(), left.begin(), left.end());
            combinedPoints.insert(combinedPoints.end(), right.begin(), right.end());
            return combinedPoints;
        }
    };

    void ForceRegion::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            BaseForce::Reflect(*serializeContext);

            serializeContext->Class<ForceRegion>()
                ->Version(1)
                ->Field("Forces", &ForceRegion::m_forces)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceRegion>(
                    QT_TRANSLATE_NOOP("PhysX", "Force Region"), QT_TRANSLATE_NOOP("PhysX", "Applies forces on entities within a region."))
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceRegion::m_forces, QT_TRANSLATE_NOOP("PhysX", "Forces"), QT_TRANSLATE_NOOP("PhysX", "Forces acting in the region."))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ;
            }
        }
    }

    ForceRegion::ForceRegion(const ForceRegion& forceRegion)
    {
        // Force region must be deep copied as it contains pointers
        AZ::SerializeContext* context = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(context, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
        context->CloneObjectInplace<ForceRegion>(*this, &forceRegion);
    }

    void ForceRegion::Activate(AZ::EntityId entityId)
    {
        m_entityId = entityId;
        m_regionParams = ForceRegionUtil::CreateRegionParams(m_entityId);
        AZ::TransformNotificationBus::MultiHandler::BusConnect(m_entityId);
        LmbrCentral::SplineComponentNotificationBus::Handler::BusConnect(m_entityId);
        ForceRegionRequestBus::Handler::BusConnect(m_entityId);
        Physics::ColliderComponentEventBus::Handler::BusConnect(m_entityId);
        for (auto& force : m_forces)
        {
            force->Activate(m_entityId);
        }

        AZ::TransformBus::EventResult(m_worldTransform, m_entityId, &AZ::TransformBus::Events::GetWorldTM);
    }

    void ForceRegion::Deactivate()
    {
        // Transform 2: the forces survive a deactivation, so the mirror is undone while the entity is still known. The next
        // activation then starts from the authored forces with a mask of zero, and waits for a new push.
        SetFlipMask(0);

        m_entityId.SetInvalid();
        for (auto& force : m_forces)
        {
            force->Deactivate();
        }
        Physics::ColliderComponentEventBus::Handler::BusDisconnect();
        ForceRegionRequestBus::Handler::BusDisconnect();
        LmbrCentral::SplineComponentNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::MultiHandler::BusDisconnect();
    }

    AZ::Vector3 ForceRegion::CalculateNetForce(const EntityParams& entity) const
    {
        auto totalForce = AZ::Vector3::CreateZero();
        for (auto& force : m_forces)
        {
            totalForce += force->CalculateForce(entity, m_regionParams);
        }
        PhysX::ForceRegionNotificationBus::Broadcast(&ForceRegionNotificationBus::Events::OnCalculateNetForce
            , m_regionParams.m_id
            , entity.m_id
            , totalForce.GetNormalized()
            , totalForce.GetLength());
        return totalForce;
    }

    void ForceRegion::ClearForces()
    {
        for (auto& force : m_forces)
        {
            force->Deactivate();
        }
        m_forces.clear();
    }

    PhysX::RegionParams ForceRegion::GetRegionParams() const
    {
        return m_regionParams;
    }

    void ForceRegion::OnTransformChanged(const AZ::Transform& /*local*/, const AZ::Transform& world)
    {
        m_worldTransform = world;
        m_regionParams.m_position = world.GetTranslation();
        m_regionParams.m_scale = world.GetUniformScale();
        m_regionParams.m_rotation = world.GetRotation();
        AZ::EBusReduceResult<AZ::Aabb, PhysX::TriggerAabbAggregator> triggerAabb;
        triggerAabb.value = AZ::Aabb::CreateNull();

        ColliderShapeRequestBus::EventResult(triggerAabb
            , m_entityId
            , &ColliderShapeRequestBus::Events::GetColliderShapeAabb);

        m_regionParams.m_aabb = triggerAabb.value;
    }

    void ForceRegion::OnColliderChanged()
    {
        // Transform 2: the flip is not part of the collider, and the rebuild below starts from the authored spline and a mask of
        // zero. The mask and the mirrored spline are carried across it. This is a rare path, and a region that is not flipped
        // pays two byte tests.
        const AZ::u8 flipMask = m_regionParams.m_flipMask;
        AZ::SplinePtr flippedSpline;
        if (flipMask)
        {
            flippedSpline = AZStd::move(m_regionParams.m_spline);
        }

        m_regionParams = ForceRegionUtil::CreateRegionParams(m_entityId);

        if (flipMask)
        {
            m_regionParams.m_flipMask = flipMask;
            m_regionParams.m_spline = AZStd::move(flippedSpline);
        }
    }

    void ForceRegion::AddForceWorldSpace(const AZ::Vector3& direction, float magnitude)
    {
        AddAndActivateForce(AZStd::make_unique<ForceWorldSpace>(direction, magnitude));
    }

    void ForceRegion::AddForceLocalSpace(const AZ::Vector3& direction, float magnitude)
    {
        AddAndActivateForce(AZStd::make_unique<ForceLocalSpace>(direction, magnitude));
    }

    void ForceRegion::AddForcePoint(float magnitude)
    {
        AddAndActivateForce(AZStd::make_unique<ForcePoint>(magnitude));
    }

    void ForceRegion::AddForceSplineFollow(float dampingRatio, float frequency, float targetSpeed, float lookAhead)
    {
        AddAndActivateForce(AZStd::make_unique<ForceSplineFollow>(dampingRatio, frequency, targetSpeed, lookAhead));
    }

    void ForceRegion::AddForceSimpleDrag(float dragCoefficient, float volumeDensity)
    {
        AddAndActivateForce(AZStd::make_unique<ForceSimpleDrag>(dragCoefficient, volumeDensity));
    }

    void ForceRegion::AddForceLinearDamping(float damping)
    {
        AddAndActivateForce(AZStd::make_unique<ForceLinearDamping>(damping));
    }

    void ForceRegion::SetFlipMask(AZ::u8 flipMask)
    {
        const AZ::u32 changedAxes = static_cast<AZ::u32>(m_regionParams.m_flipMask ^ flipMask);
        if (changedAxes == 0)
        {
            return;
        }

        // The forces hold their directions as drawn. Mirroring by the axes that changed takes them from the old flip to the new
        // one, and each force keeps its own direction, which a push through the force bus could not do: that bus reaches every
        // force of its kind on the entity with one value. Multiplying by minus one is exact, so flipping back is exact too.
        const AZ::Vector3 changeSigns = Transform2Mirror::GetMaskSigns(changedAxes);
        for (auto& force : m_forces)
        {
            force->Mirror(changeSigns);
        }

        // The region follows the spline of the Spline component as it is authored, or a mirrored clone of it while flipped.
        // The spline handler fetches the authored one again and makes the clone from it, or takes the authored one back at zero.
        m_regionParams.m_flipMask = flipMask;
        OnSplineChanged();
    }

    void ForceRegion::AddAndActivateForce(AZStd::unique_ptr<BaseForce> force)
    {
        AZ_Assert(force, "Failed to add and activate null force.");
        if (nullptr == force)
        {
            return;
        }
        m_forces.push_back(AZStd::move(force));
        m_forces.back()->Activate(m_entityId);

        // Transform 2: a force comes in as authored, and every force of a flipped region is held as drawn.
        if (m_regionParams.m_flipMask)
        {
            m_forces.back()->Mirror(Transform2Mirror::GetMaskSigns(m_regionParams.m_flipMask));
        }
    }

    void ForceRegion::OnSplineChanged()
    {
        LmbrCentral::SplineComponentRequestBus::EventResult(m_regionParams.m_spline
            , m_entityId
            , &LmbrCentral::SplineComponentRequestBus::Events::GetSpline);

        // Transform 2: the spline just fetched is the shared one. While flipped, the region follows a mirrored clone of it, which
        // is made again for every change. A script that moves the vertices of a flipped spline every frame pays a clone each time.
        if (m_regionParams.m_flipMask)
        {
            m_regionParams.m_spline = ForceRegionUtil::CreateFlippedSpline(
                m_regionParams.m_spline, Transform2Mirror::GetMaskSigns(m_regionParams.m_flipMask));
        }
    }

    RegionParams ForceRegionUtil::CreateRegionParams(AZ::EntityId entityId)
    {
        RegionParams regionParams;
        regionParams.m_id = entityId;

        AZ::Transform worldTransform = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(worldTransform
            , entityId
            , &AZ::TransformBus::Events::GetWorldTM);
        regionParams.m_position = worldTransform.GetTranslation();
        regionParams.m_scale = worldTransform.GetUniformScale();
        regionParams.m_rotation = worldTransform.GetRotation();

        LmbrCentral::SplineComponentRequestBus::EventResult(regionParams.m_spline
            , entityId
            , &LmbrCentral::SplineComponentRequestBus::Events::GetSpline);
        AZ::EBusReduceResult<AZ::Aabb, PhysX::TriggerAabbAggregator> triggerAabb;
        triggerAabb.value = AZ::Aabb::CreateNull();
        ColliderShapeRequestBus::EventResult(triggerAabb
            , entityId
            , &ColliderShapeRequestBus::Events::GetColliderShapeAabb);
        regionParams.m_aabb = triggerAabb.value;
        return regionParams;
    }

    EntityParams ForceRegionUtil::CreateEntityParams(AZ::EntityId entityId)
    {
        EntityParams entityParams;
        entityParams.m_id = entityId;

        AzPhysics::RigidBody* rigidBody = nullptr;

        if (auto* handler = Physics::RigidBodyRequestBus::FindFirstHandler(entityId))
        {
            rigidBody = handler->GetRigidBody();
        }

        if (!rigidBody)
        {
            AZ_Error("PhysX", false, "ForceRegionUtil::CreateEntityParams: No rigid body for entity [%llu]", entityId);
            return entityParams;
        }

        entityParams.m_position = rigidBody->GetPosition();
        entityParams.m_velocity = rigidBody->GetLinearVelocity();
        entityParams.m_mass = rigidBody->GetMass();
        entityParams.m_aabb = GetForceRegionAabb(entityId);

        return entityParams;
    }

    AZ::Aabb ForceRegionUtil::GetForceRegionAabb(AZ::EntityId entityId)
    {
        AZ::EBusReduceResult<AZ::Aabb, PhysX::TriggerAabbAggregator> triggerAabb;
        triggerAabb.value = AZ::Aabb::CreateNull();
        ColliderShapeRequestBus::EventResult(triggerAabb
            , entityId
            , &ColliderShapeRequestBus::Events::GetColliderShapeAabb);
        return triggerAabb.value;
    }

    AZ::SplinePtr ForceRegionUtil::CreateFlippedSpline(const AZ::SplinePtr& spline, const AZ::Vector3& signs)
    {
        // Fewer than two vertices answer no query. A Bezier spline with one vertex also reads past its vertices when it builds
        // its control points, so there is nothing to gain by cloning it.
        if (!spline || spline->GetVertexCount() < 2)
        {
            return spline;
        }

        // The clone has the type of the spline, copied with its own settings: closed, granularity and knot parameterization.
        // A clone made as a base Spline would turn a Catmull-Rom into the default one.
        AZ::SplinePtr flipped;
        if (auto* linear = azrtti_cast<AZ::LinearSpline*>(spline.get()))
        {
            flipped = AZStd::make_shared<AZ::LinearSpline>(*linear);
        }
        else if (auto* bezier = azrtti_cast<AZ::BezierSpline*>(spline.get()))
        {
            flipped = AZStd::make_shared<AZ::BezierSpline>(*bezier);
        }
        else if (auto* catmullRom = azrtti_cast<AZ::CatmullRomSpline*>(spline.get()))
        {
            flipped = AZStd::make_shared<AZ::CatmullRomSpline>(*catmullRom);
        }
        else
        {
            AZ_Warning("PhysX", false, "A force region cannot mirror a spline of this type, so it follows the spline as authored.");
            return spline;
        }

        // Setting the vertices makes a Bezier spline build its control points again from the mirrored vertices. They are built
        // from vertex differences and distances only, which a reflection keeps, so they land on the mirror of the authored ones.
        AZStd::vector<AZ::Vector3> vertices = spline->GetVertices();
        for (AZ::Vector3& vertex : vertices)
        {
            vertex = Transform2Mirror::MirrorPosition(signs, vertex);
        }
        flipped->m_vertexContainer.SetVertices(AZStd::move(vertices));
        return flipped;
    }
}
