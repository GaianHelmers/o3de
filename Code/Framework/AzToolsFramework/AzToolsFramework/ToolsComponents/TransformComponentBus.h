/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/EBus/EBus.h>
#include <AzToolsFramework/AzToolsFrameworkAPI.h>


namespace AzToolsFramework
{
    namespace Components
    {
        //////////////////////////////////////////////////////////////////////////
        // Manages transform data as separate vector fields for editing purposes.
        struct EditorTransform
        {
            AZ_TYPE_INFO(EditorTransform, "{B02B7063-D238-4F40-A724-405F7A6D68CB}")

            EditorTransform()
            {
                m_translate = AZ::Vector3::CreateZero();
                m_legacyScale = AZ::Vector3::CreateOne();
                m_uniformScale = 1.0f;
                m_authoredScale = AZ::Vector3::CreateOne();
                m_deployExtended = false;
                m_rotate = AZ::Vector3::CreateZero();
                m_locked = false;
            }

            static EditorTransform Identity()
            {
                return EditorTransform();
            }

            //////////////////////////////////////////////////////////////////////////
            // Transform 1 / Transform 2 scale split
            //
            // The inspector authors m_authoredScale. While it fits the single float of AZ::Transform this is a
            // Transform 1: the float is what gets saved and deployed, exactly as before. Anything else targets
            // Transform 2, and the float only holds a safe stand-in for systems that read AZ::Transform.

            //! True when the authored scale fits Transform 1: all axes equal and positive.
            //! The rule has one definition, in AZ::Transform2.
            bool IsTransform1Scale() const
            {
                return AZ::Transform2::FitsTransform1(m_authoredScale);
            }

            //! The float AZ::Transform carries. Transform 1 keeps its exact value. Transform 2 uses its largest
            //! magnitude, because no system reading AZ::Transform can survive a negative or zero scale.
            float ResolveUniformScale() const
            {
                return AZ::Transform2::UniformStandIn(m_authoredScale);
            }

            //! True when this transform deploys as an extended transform (Transform 2): its own scale does not fit
            //! Transform 1, or the user forced it with "Deploy as Extended". An extended ancestor is not counted here.
            bool IsExtended() const
            {
                return !IsTransform1Scale() || m_deployExtended;
            }

            //! Takes a write from the AZ::Transform side (gizmo, script, SetLocalTM). A positive write on a Transform 2 keeps its
            //! signs and axis ratios. A negative write negates every sign, because the fold multiplies by the written float.
            //! A scale with no magnitude has no signs or ratios left to keep, so the write is the value, as in Transform 1:
            //! a write of 0 followed by a write of 1 ends as a Transform 1 at 1. A write of 0 on a Transform 2 that still has
            //! magnitude folds to the smallest scale instead, which keeps its signs and ratios for the write that follows.
            //! With no magnitude, a write equal to the float already held is not a write: SetLocalTM hands that float back
            //! on every move, and it must not turn a scale of 0 into the stand-in.
            void ApplyUniformScale(float uniformScale)
            {
                const bool hasMagnitude = m_authoredScale.GetAbs().GetMaxElement() > 0.0f;
                if (IsTransform1Scale())
                {
                    m_authoredScale = AZ::Vector3(uniformScale);
                }
                else if (!hasMagnitude)
                {
                    if (!AZ::IsCloseMag(uniformScale, m_uniformScale))
                    {
                        m_authoredScale = AZ::Vector3(uniformScale);
                    }
                }
                else
                {
                    const float foldedScale = (uniformScale == 0.0f) ? AZ::MinTransformScale : uniformScale;
                    if (!AZ::IsCloseMag(foldedScale, m_uniformScale))
                    {
                        m_authoredScale *= foldedScale / m_uniformScale;
                    }
                }
                m_uniformScale = ResolveUniformScale();
            }

            //! Settles the scale after a JSON load into this transform. The flags say which scale keys the JSON carried,
            //! because the load may land on a live component and a key that was absent must not change anything.
            //! A float with no vector, or with the default vector, is the value: a Transform 1 saves only the float.
            //! Otherwise the vector stands. Either way the float is re-resolved from the vector.
            void ReconcileLoadedScale(bool jsonHadUniformScale, bool jsonHadAuthoredScale)
            {
                if (jsonHadUniformScale && (!jsonHadAuthoredScale || m_authoredScale == AZ::Vector3::CreateOne()))
                {
                    m_authoredScale = AZ::Vector3(m_uniformScale);
                }
                m_uniformScale = ResolveUniformScale();
            }

            //! The scale an entity gets when its old Non-uniform Scale component is folded into this transform: the authored
            //! scale multiplied per axis by the old component's scale. The old scale is positive, so the signs of an
            //! authored Transform 2 survive. An old (2,2,2) on a uniform 1 gives (2,2,2), which fits Transform 1 again.
            static AZ::Vector3 FoldLegacyScale(const AZ::Vector3& authoredScale, const AZ::Vector3& legacyScale)
            {
                return authoredScale * legacyScale;
            }

            AZ::Vector3 m_translate; //!< Translation in engine units (meters)
            AZ::Vector3 m_legacyScale; //!< Legacy vector scale value, retained only for migration.
            float m_uniformScale; //!< Single scale value applied uniformly.
            AZ::Vector3 m_authoredScale; //!< Scale as entered in the inspector. Only saved for a Transform 2. m_uniformScale derives from it.
            bool m_deployExtended; //!< Forces the extended (Transform 2) target at any scale. Only saved when set.
            AZ::Vector3 m_rotate; //!< Rotation in degrees
            bool m_locked;
        };

        //////////////////////////////////////////////////////////////////////////
        // messages controlling or polling the hierarchy
        //////////////////////////////////////////////////////////////////////////
        class TransformComponentMessages
            : public AZ::ComponentBus
        {
        public:

            using Bus = AZ::EBus<TransformComponentMessages>;

            //////////////////////////////////////////////////////////////////////////
            // Bus configuration
            static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple; // every listener registers unordered on this bus

            virtual const EditorTransform& GetLocalEditorTransform() = 0;
            virtual void SetLocalEditorTransform(const EditorTransform& dest) = 0;

            virtual void TranslateBy(const AZ::Vector3&) = 0;
            virtual void RotateBy(const AZ::Vector3&) = 0;

            virtual bool IsTransformLocked() = 0;
        };
    } // namespace Components
} // namespace AzToolsFramework

AZ_DECLARE_EBUS_MULTI_ADDRESS(AZTF_API, AzToolsFramework::Components::TransformComponentMessages);
