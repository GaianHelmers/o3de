/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzToolsFramework/Prefab/PrefabFocusPublicInterface.h>
#include <AzToolsFramework/Prefab/Overrides/PrefabOverridePublicInterface.h>
#include <AzToolsFramework/ToolsComponents/TransformComponentBus.h>
#include <Prefab/PrefabTestFixture.h>

namespace UnitTest
{
    using namespace AzToolsFramework::Prefab;

    class PrefabOverrideTestFixture : public PrefabTestFixture
    {
    protected:
        void SetUpEditorFixtureImpl() override;

        //! Creates an entity within a nested prefab under level. The setup looks like:
        //! | Level
        //!   | Prefab (grandparentContainerId)
        //!     | Nested prefab (parentContainerId)
        //!       | New Entity
        //! @param newEntityId The new entity id created under nested prefab.
        //! @param parentContainerId The container entity id of the nested prefab.
        //! @param grandparentContainerId The container entity id of the top-level prefab.
        void CreateEntityInNestedPrefab(AZ::EntityId& newEntityId, AZ::EntityId& parentContainerId, AZ::EntityId& grandparentContainerId);

        //! Focuses on the owning instance of the ancestor entity id and modifies the entity matching the entity id.
        //! This will make the edit become an override.
        //! @param entityId The id of entity to modify.
        //! @param ancestorEntityId The id of an ancestor entity to use for focusing on its owning prefab.
        void CreateAndValidateEditEntityOverride(AZ::EntityId entityId, AZ::EntityId ancestorEntityId);

        //! Focuses on the owning instance of the entity id and modifies it, which makes this a template edit.
        //! @param entityId The Id of the entity to modify.
        void EditEntityAndValidateNoOverride(AZ::EntityId entityId);

        //! Reads the editor transform of an entity: its authored scale, and the options next to it.
        AzToolsFramework::Components::EditorTransform GetEditorTransform(AZ::EntityId entityId);

        //! Writes the editor transform of an entity as one undo step, the way an edit in the inspector is recorded, and
        //! propagates the change, which reloads the entity from the patched prefab. When the owning prefab of the entity is not
        //! the focused one, this makes the edit an override.
        //! @param entityId The id of the entity to edit.
        //! @param editorTransform The editor transform to write.
        void EditEditorTransform(AZ::EntityId entityId, const AzToolsFramework::Components::EditorTransform& editorTransform);

        //! Writes the authored scale of an entity as an edit, as EditEditorTransform does, and leaves the rest of its transform.
        //! @param entityId The id of the entity to edit.
        //! @param scale The authored scale to write.
        void EditAuthoredScale(AZ::EntityId entityId, const AZ::Vector3& scale);

        //! The path under which the Transform field of an entity is queried and reverted for overrides:
        //! Components/<alias of the transform>/Transform Data/<field>.
        //! @param entityId The id of the entity.
        //! @param field The serialized name of the field, such as UniformScale.
        AZStd::string GetTransformFieldPath(AZ::EntityId entityId, AZStd::string_view field);

        PrefabOverridePublicInterface* m_prefabOverridePublicInterface = nullptr;
        PrefabFocusPublicInterface* m_prefabFocusPublicInterface = nullptr;
    };
} // namespace UnitTest
