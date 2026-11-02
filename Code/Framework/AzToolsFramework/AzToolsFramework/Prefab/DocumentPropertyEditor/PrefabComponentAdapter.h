/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Crc.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/string/string.h>
#include <AzToolsFramework/UI/DocumentPropertyEditor/DPEComponentAdapter.h>
#include <AzToolsFramework/AzToolsFrameworkAPI.h>

namespace AZ
{
    class SerializeContext;
}

namespace AzToolsFramework::Prefab
{
    class PrefabOverridePublicInterface;
    class PrefabPublicInterface;

    //! Edit context attributes the prefab inspector reads from a reflected DataElement.
    namespace PrefabOverrideAttributes
    {
        //! The serialized name of a field next to the row, such as "UniformScale", whose prefab override also counts as an
        //! override of the row. For a row that shows one field and is written through another, so that its override is
        //! patched under a path the row does not have. The label is marked when either path holds an override, and reverting
        //! the row reverts both.
        inline constexpr AZ::Crc32 AdditionalOverrideField = AZ_CRC_CE("PrefabAdditionalOverrideField");
    } // namespace PrefabOverrideAttributes

    class AZTF_API PrefabComponentAdapter
        : public AZ::DocumentPropertyEditor::ComponentAdapter
    {
    public:
        PrefabComponentAdapter();
        ~PrefabComponentAdapter();

        void SetComponent(AZ::Component* componentInstance) override;

        void CreateLabel(
            AZ::DocumentPropertyEditor::AdapterBuilder* adapterBuilder,
            AZStd::string_view labelText,
            AZStd::string_view serializedPath) override;

        AZ::Dom::Value HandleMessage(const AZ::DocumentPropertyEditor::AdapterMessage& message) override;

        //! Updates the DPE DOM using the property change information provided. If the property is owned by the focused prefab,
        //! the change is applied as direct template edit. If the property is owned by descendant of the focused prefab, it is
        //! applied as an override from the focused prefab.
        //! @param propertyChangeInfo The object containing information about the property change.
        void UpdateDomContents(const PropertyChangeInfo& propertyChangeInfo) override;

    private:

        //! Creates and applies a component edit prefab patch using the property change information provided.
        //! @param relativePathFromOwningPrefab The path to the property in the prefab from the owning prefab.
        //! @param propertyChangeInfo The object containing information about the property change.
        bool CreateAndApplyComponentEditPatch(
            AZStd::string_view relativePathFromOwningPrefab,
            const AZ::DocumentPropertyEditor::ReflectionAdapter::PropertyChangeInfo& propertyChangeInfo);

        //! Creates and applies a component override prefab patch using the property change information provided. 
        //! @param relativePathFromOwningPrefab The path to the property in the prefab from the owning prefab.
        //! @param propertyChangeInfo The object containing information about the property change.
        bool CreateAndApplyComponentOverridePatch(
            AZ::Dom::Path relativePathFromOwningPrefab,
            const AZ::DocumentPropertyEditor::ReflectionAdapter::PropertyChangeInfo& propertyChangeInfo);

        //! Checks if the component is disabled.
        static bool IsComponentDisabled(const AZ::Component* component);

        //! Checks if the component is pending.
        static bool IsComponentPending(const AZ::Component* component);

        //! Finds the other path the override of a row can be patched under, as the row declares with
        //! PrefabOverrideAttributes::AdditionalOverrideField.
        //! @param serializedPath The serialized path of the row, as CreateLabel is handed it.
        //! @return The path relative to the entity, or an empty string when the row declares none.
        AZStd::string FindAdditionalOverridePath(const AZ::Dom::Path& serializedPath) const;

        AZStd::string m_entityAlias;
        AZStd::string m_componentAlias;

        PrefabOverridePublicInterface* m_prefabOverridePublicInterface = nullptr;
        PrefabPublicInterface* m_prefabPublicInterface = nullptr;
        AZ::SerializeContext* m_serializeContext = nullptr;

        //! The additional override path of each row that has one, by the relative path of the row from the entity, which is
        //! what its label carries. The revert message names only that path, so this is how it finds the other one.
        AZStd::unordered_map<AZStd::string, AZStd::string> m_additionalOverridePaths;
    };

} // namespace AzToolsFramework::Prefab
