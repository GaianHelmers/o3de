/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorSplineComponentMode.h"
#include "EditorSplineComponent.h" // for GetEditorSplineFlipSigns

#include <AzToolsFramework/ActionManager/Action/ActionManagerInterface.h>
#include <AzToolsFramework/Manipulators/SplineHoverSelection.h>
#include <AzToolsFramework/Manipulators/ManipulatorManager.h>

namespace LmbrCentral
{
    namespace
    {
        // The spline draws at the uniform stand-in scale and ignores the non-uniform residual, so its translation manipulator
        // must too. The vertex selection builds that manipulator with the residual from the bus, and then calls this.
        //
        // The flip signs of a Transform 2 entity are given to the manipulators through RefreshSpace below, as their non-uniform
        // scale. This is a plain function pointer that is told nothing of the entity, so it cannot read the signs. The vertex
        // selection remembers what RefreshSpace gave it, and puts that on a translation manipulator it creates itself when a vertex
        // is clicked, after this has run.
        void ConfigureSplineTranslationManipulator(AzToolsFramework::TranslationManipulators* translationManipulators)
        {
            AzToolsFramework::ConfigureTranslationManipulatorAppearance3d(translationManipulators);
            translationManipulators->SetNonUniformScale(AZ::Vector3::CreateOne());
        }
    } // namespace

    AZ_CLASS_ALLOCATOR_IMPL(EditorSplineComponentMode, AZ::SystemAllocator)

    EditorSplineComponentMode::EditorSplineComponentMode(
        const AZ::EntityComponentIdPair& entityComponentIdPair, const AZ::Uuid componentType)
        : EditorBaseComponentMode(entityComponentIdPair, componentType)
        , m_vertexSelection(entityComponentIdPair)
    {
        AZ::TransformNotificationBus::Handler::BusConnect(entityComponentIdPair.GetEntityId());
        SplineComponentNotificationBus::Handler::BusConnect(entityComponentIdPair.GetEntityId());
        EditorSplineComponentNotificationBus::Handler::BusConnect(entityComponentIdPair.GetEntityId());

        CreateManipulators();
    }

    EditorSplineComponentMode::~EditorSplineComponentMode()
    {
        EditorSplineComponentNotificationBus::Handler::BusDisconnect();
        SplineComponentNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::Handler::BusDisconnect();

        m_vertexSelection.Destroy();
    }

    void EditorSplineComponentMode::Reflect(AZ::ReflectContext* context)
    {
        AzToolsFramework::ComponentModeFramework::ReflectEditorBaseComponentModeDescendant<EditorSplineComponentMode>(context);
    }

    void EditorSplineComponentMode::RegisterActions()
    {
        AzToolsFramework::EditorVertexSelectionActionManagement::RegisterEditorVertexSelectionActions();
    }

    void EditorSplineComponentMode::BindActionsToModes()
    {
        auto actionManagerInterface = AZ::Interface<AzToolsFramework::ActionManagerInterface>::Get();
        AZ_Assert(actionManagerInterface, "EditorSplineComponentMode - could not get ActionManagerInterface on RegisterActions.");

        AZ::SerializeContext* serializeContext = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
        
        AZStd::string modeIdentifier = AZStd::string::format(
            "o3de.context.mode.%s", serializeContext->FindClassData(azrtti_typeid<EditorSplineComponentMode>())->m_name);

        actionManagerInterface->AssignModeToAction(modeIdentifier, "o3de.action.vertexSelection.duplicate");
        actionManagerInterface->AssignModeToAction(modeIdentifier, "o3de.action.vertexSelection.delete");
        actionManagerInterface->AssignModeToAction(modeIdentifier, "o3de.action.vertexSelection.clearSelection");
    }

    void EditorSplineComponentMode::BindActionsToMenus()
    {
        AzToolsFramework::EditorVertexSelectionActionManagement::BindEditorVertexSelectionActionsToMenus();
    }

    void EditorSplineComponentMode::Refresh()
    {
        ContainerChanged();
    }

    AZStd::vector<AzToolsFramework::ActionOverride> EditorSplineComponentMode::PopulateActionsImpl()
    {
        return m_vertexSelection.ActionOverrides();
    }

    bool EditorSplineComponentMode::HandleMouseInteraction(
        const AzToolsFramework::ViewportInteraction::MouseInteractionEvent& mouseInteraction)
    {
        return m_vertexSelection.HandleMouse(mouseInteraction);
    }

    AZStd::string EditorSplineComponentMode::GetComponentModeName() const
    {
        return "Spline Edit Mode";
    }

    AZ::Uuid EditorSplineComponentMode::GetComponentModeType() const
    {
        return azrtti_typeid<EditorSplineComponentMode>();
    }

    void EditorSplineComponentMode::CreateManipulators()
    {
        using namespace AzToolsFramework;

        bool empty = false;
        SplineComponentRequestBus::EventResult(
            empty, GetEntityId(), &SplineComponentRequests::Empty);

        // if we have no vertices, do not attempt to create any manipulators
        if (empty)
        {
            return;
        }

        AZ::SplinePtr spline = nullptr;
        SplineComponentRequestBus::EventResult(
            spline, GetEntityId(), &SplineComponentRequests::GetSpline);

        m_vertexSelection.Create(
            GetEntityComponentIdPair(), GetMainManipulatorManagerId(),
            AZStd::make_unique<SplineHoverSelection>(
                GetEntityComponentIdPair(), GetMainManipulatorManagerId(), spline),
            TranslationManipulators::Dimensions::Three, ConfigureSplineTranslationManipulator);

        // The selection dots and the hover selection are built with the residual from the bus as well, so the space is
        // refreshed here with a scale of one. The translation manipulator is covered by the configurator above.
        // On a flipped entity the scale is the flip signs, which puts every manipulator on the mirrored side of its authored vertex
        // while the vertex it edits stays the authored one. Editor only: the signs are read from the Transform 2 of the entity.
        m_vertexSelection.RefreshSpace(WorldFromLocalWithUniformScale(GetEntityId()), GetEditorSplineFlipSigns(GetEntityId()));
    }

    void EditorSplineComponentMode::ContainerChanged()
    {
        // destroy and recreate manipulators when container is modified (vertices are added or removed)
        m_vertexSelection.Destroy();
        CreateManipulators();
    }

    void EditorSplineComponentMode::OnSplineChanged()
    {
        m_vertexSelection.RefreshLocal();
    }

    void EditorSplineComponentMode::OnVertexAdded(size_t index)
    {
        ContainerChanged();

        AZ::SplinePtr spline = nullptr;
        SplineComponentRequestBus::EventResult(
            spline, GetEntityId(), &SplineComponentRequests::GetSpline);

        m_vertexSelection.CreateTranslationManipulator(
            GetEntityComponentIdPair(), AzToolsFramework::GetMainManipulatorManagerId(),
            spline->m_vertexContainer.GetVertices()[index], index);
    }

    void EditorSplineComponentMode::OnVertexRemoved(size_t /*index*/)
    {
        ContainerChanged();
    }

    void EditorSplineComponentMode::OnVertexUpdated(size_t /*index*/)
    {
        m_vertexSelection.RefreshLocal();
    }

    void EditorSplineComponentMode::OnVerticesSet(const AZStd::vector<AZ::Vector3>& /*vertices*/)
    {
        ContainerChanged();
    }

    void EditorSplineComponentMode::OnVerticesCleared()
    {
        ContainerChanged();
    }

    void EditorSplineComponentMode::OnTransformChanged(
        const AZ::Transform& /*local*/, const AZ::Transform& world)
    {
        // update the space manipulators are in after the entity has moved
        m_vertexSelection.RefreshSpace(world, GetEditorSplineFlipSigns(GetEntityId()));
    }

    void EditorSplineComponentMode::OnSplineTypeChanged()
    {
        ContainerChanged();
    }
} // namespace LmbrCentral
