/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzFramework/DocumentPropertyEditor/PropertyEditorNodes.h>
#include <AzToolsFramework/Prefab/DocumentPropertyEditor/PrefabPropertyEditorNodes.h>
#include <AzToolsFramework/Prefab/PrefabEditorPreferences.h>
#include <AzToolsFramework/UI/PropertyEditor/ComponentEditor.hxx>
#include <Prefab/Overrides/PrefabInspectorOverrideTestFixture.h>

namespace UnitTest
{
    namespace
    {
        // Looks below a DOM node, and in the node, for the prefab override label of the row with the given text.
        AZ::Dom::Value FindLabelNode(const AZ::Dom::Value& node, AZStd::string_view labelText)
        {
            using AZ::DocumentPropertyEditor::Nodes::PropertyEditor;
            using AzToolsFramework::Prefab::PrefabPropertyEditorNodes::PrefabOverrideLabel;

            if (!node.IsNode())
            {
                return AZ::Dom::Value();
            }

            const bool isLabel = node.HasMember(PropertyEditor::Type.GetName()) && node[PropertyEditor::Type.GetName()].IsString() &&
                node[PropertyEditor::Type.GetName()].GetString() == PrefabOverrideLabel::Name;
            if (isLabel && node.HasMember(PrefabOverrideLabel::Value.GetName()) && node[PrefabOverrideLabel::Value.GetName()].IsString() &&
                node[PrefabOverrideLabel::Value.GetName()].GetString() == labelText)
            {
                return node;
            }

            for (size_t childIndex = 0; childIndex < node.ArraySize(); ++childIndex)
            {
                AZ::Dom::Value found = FindLabelNode(node[childIndex], labelText);
                if (found.IsNode())
                {
                    return found;
                }
            }
            return AZ::Dom::Value();
        }
    } // namespace

    void PrefabInspectorOverrideTestFixture::SetUpEditorFixtureImpl()
    {
        // Enable feature flags for DPE and InspectorOverrideManagement
        if (auto* console = AZ::Interface<AZ::IConsole>::Get(); console != nullptr)
        {
            console->GetCvarValue("ed_enableInspectorOverrideManagement", m_ed_enableInspectorOverrideManagement);
            console->GetCvarValue("ed_enableDPEInspector", m_ed_enableDPEInspector);
            console->PerformCommand("ed_enableInspectorOverrideManagement true");
            console->PerformCommand("ed_enableDPEInspector true");

        }
        PrefabOverrideTestFixture::SetUpEditorFixtureImpl();

        m_testEntityPropertyEditor = AZStd::make_unique<AzToolsFramework::EntityPropertyEditor>(nullptr, Qt::WindowFlags(), false);
    }

    void PrefabInspectorOverrideTestFixture::TearDownEditorFixtureImpl()
    {
        m_testEntityPropertyEditor.reset();
        PrefabOverrideTestFixture::TearDownEditorFixtureImpl();
        AZ::SettingsRegistryInterface* registry = AZ::SettingsRegistry::Get();
        registry->Set("/O3DE/Autoexec/ConsoleCommands/ed_enableDPEInspector", m_ed_enableDPEInspector);
        registry->Set("/O3DE/Autoexec/ConsoleCommands/ed_enableInspectorOverrideManagement", m_ed_enableInspectorOverrideManagement);
    }

    void PrefabInspectorOverrideTestFixture::GenerateComponentAdapterDoms(AZ::EntityId entityId)
    {
        // Calling UpdateContents will trigger the ReflectionAdapters of the components to build the DPE DOMs.
        AzToolsFramework::ToolsApplicationRequestBus::Broadcast(
            &AzToolsFramework::ToolsApplicationRequests::SetSelectedEntities, AzToolsFramework::EntityIdList{ entityId });
        QMetaObject::invokeMethod(m_testEntityPropertyEditor.get(), "UpdateContents", Qt::DirectConnection);
    }

    void PrefabInspectorOverrideTestFixture::ValidateComponentEditorDomContents(
        const AzToolsFramework::ComponentEditor::VisitComponentAdapterContentsCallback& callback)
    {
        AZStd::vector<const AzToolsFramework::ComponentEditor*> componentEditors;

        AzToolsFramework::EntityPropertyEditorRequestBus::Broadcast(
            &AzToolsFramework::EntityPropertyEditorRequestBus::Events::VisitComponentEditors,
            [&componentEditors](const AzToolsFramework::ComponentEditor* componentEditor)
            {
                componentEditors.push_back(componentEditor);
                return true;
            });
        ASSERT_EQ(componentEditors.size(), 1);
        componentEditors.front()->VisitComponentAdapterContents(callback);
    }

    AZ::Dom::Value PrefabInspectorOverrideTestFixture::FindRowLabel(AZ::EntityId entityId, AZStd::string_view labelText)
    {
        GenerateComponentAdapterDoms(entityId);

        // The node is a shared copy, so it stays valid after the visit, for a test that invokes the callbacks it carries.
        AZ::Dom::Value labelNode;
        ValidateComponentEditorDomContents(
            [&labelNode, labelText](const AZ::Dom::Value& adapterContents)
            {
                labelNode = FindLabelNode(adapterContents, labelText);
            });
        return labelNode;
    }

} // namespace UnitTest
