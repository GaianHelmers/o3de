/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/RTTI/AttributeReader.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/ToolsComponents/TransformComponent.h>
#include <AzToolsFramework/UI/PropertyEditor/ComponentEditorHeader.hxx>
#include <Prefab/Overrides/PrefabOverrideTestFixture.h>

namespace UnitTest
{
    using PrefabOverridePublicInterfaceTest = PrefabOverrideTestFixture;

    TEST_F(PrefabOverridePublicInterfaceTest, AreOverridesPresentWorksWithOverrideFromImmediateParent)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);
        
        CreateAndValidateEditEntityOverride(newEntityId, grandparentContainerId);
    }

    TEST_F(PrefabOverridePublicInterfaceTest, AreOverridesPresentWorksWithOverrideFromLevel)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);
        AZ::EntityId levelContainerId = m_prefabEditorEntityOwnershipInterface->GetRootPrefabInstance()->get().GetContainerEntityId();
        CreateAndValidateEditEntityOverride(newEntityId, levelContainerId);
    }

    TEST_F(PrefabOverridePublicInterfaceTest, AreOverridesPresentReturnsFalseWhenNoOverride)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);

        EditEntityAndValidateNoOverride(newEntityId);
    }

    TEST_F(PrefabOverridePublicInterfaceTest, RevertOverridesOnEntityWithOverrides)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);

        m_prefabFocusPublicInterface->FocusOnOwningPrefab(grandparentContainerId);

        // Modify the transform component.
        AZ::TransformBus::Event(newEntityId, &AZ::TransformInterface::SetWorldX, 10.0f);
        m_prefabPublicInterface->GenerateUndoNodesForEntityChangeAndUpdateCache(newEntityId, m_undoStack->GetTop());
        PropagateAllTemplateChanges();

        // Validate that overrides are present on the entity.
        ASSERT_TRUE(m_prefabOverridePublicInterface->AreOverridesPresent(newEntityId));

        // Revert the overrides on the entity.
        EXPECT_TRUE(m_prefabOverridePublicInterface->RevertOverrides(newEntityId));
        PropagateAllTemplateChanges();

        // Validate that overrides are absent upon reverting.
        EXPECT_FALSE(m_prefabOverridePublicInterface->AreOverridesPresent(newEntityId));
        float worldX;
        AZ::TransformBus::EventResult(worldX, newEntityId, &AZ::TransformInterface::GetWorldX);
        EXPECT_EQ(worldX, 0.0f);

        Undo();
        EXPECT_TRUE(m_prefabOverridePublicInterface->AreOverridesPresent(newEntityId));
        AZ::TransformBus::EventResult(worldX, newEntityId, &AZ::TransformInterface::GetWorldX);
        EXPECT_EQ(worldX, 10.0f);

        Redo();
        EXPECT_FALSE(m_prefabOverridePublicInterface->AreOverridesPresent(newEntityId));
        AZ::TransformBus::EventResult(worldX, newEntityId, &AZ::TransformInterface::GetWorldX);
        EXPECT_EQ(worldX, 0.0f);

        // Second undo to validate that override subtree was populated back in previous redo.
        Undo();
        EXPECT_TRUE(m_prefabOverridePublicInterface->AreOverridesPresent(newEntityId));
        AZ::TransformBus::EventResult(worldX, newEntityId, &AZ::TransformInterface::GetWorldX);
        EXPECT_EQ(worldX, 10.0f);
    }

    TEST_F(PrefabOverridePublicInterfaceTest, RevertOverridesOnEntityWithoutOverrides)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);

        m_prefabFocusPublicInterface->FocusOnOwningPrefab(grandparentContainerId);

        // Validate that overrides are present on the entity.
        ASSERT_FALSE(m_prefabOverridePublicInterface->AreOverridesPresent(newEntityId));

        // RevertOverrides should return false since there are no overrides on the entity.
        EXPECT_FALSE(m_prefabOverridePublicInterface->RevertOverrides(newEntityId));
    }

    //////////////////////////////////////////////////////////////////////////
    // The Transform scale split inside nested prefab instances (I.1h).
    //
    // The entity is in a prefab nested in the focused one, so every edit of it is an override. Propagating the change reloads
    // the entity from the patched prefab, which is the path a saved override takes when the level or the prefab loads.

    class PrefabTransformOverrideTest : public PrefabOverrideTestFixture
    {
    protected:
        using EditorTransform = AzToolsFramework::Components::EditorTransform;

        void SetUpEditorFixtureImpl() override
        {
            PrefabOverrideTestFixture::SetUpEditorFixtureImpl();

            CreateEntityInNestedPrefab(m_entityId, m_nestedContainerId, m_topContainerId);
            m_prefabFocusPublicInterface->FocusOnOwningPrefab(m_topContainerId);
        }

        // The tag in the header of the Transform card, read the way the card reads it.
        AZStd::string ReadHeaderTag(AZ::EntityId entityId)
        {
            using AzToolsFramework::Components::TransformComponent;

            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            const auto* editorData = serializeContext->FindClassData(azrtti_typeid<TransformComponent>())
                                         ->m_editData->FindElementData(AZ::Edit::ClassElements::EditorData);

            AZ::Entity* entity = AzToolsFramework::GetEntityById(entityId);
            auto* transform = entity ? entity->FindComponent<TransformComponent>() : nullptr;
            EXPECT_TRUE(transform != nullptr);

            AZStd::string tag;
            AZ::AttributeReader reader(transform, editorData->FindAttribute(AzToolsFramework::ComponentHeaderAttributes::Tag));
            EXPECT_TRUE(reader.Read<AZStd::string>(tag));
            return tag;
        }

        // The float next to the vector is always the stand-in of the vector, so it is compared as that.
        void ExpectSameScaleAndOption(const EditorTransform& actual, const EditorTransform& expected)
        {
            EXPECT_EQ(actual.m_authoredScale, expected.m_authoredScale);
            EXPECT_FLOAT_EQ(actual.m_uniformScale, AZ::Transform2::UniformStandIn(expected.m_authoredScale));
            EXPECT_EQ(actual.m_deployExtended, expected.m_deployExtended);
        }

        // Edits the transform of the entity as an override, then checks the edit survives the propagation that reloads the
        // entity, that Undo gives back the transform of the source prefab, and that Redo brings the override back.
        void ExpectOverrideToSurvivePropagationUndoAndRedo(const EditorTransform& edited)
        {
            const EditorTransform original = GetEditorTransform(m_entityId);
            ASSERT_FALSE(m_prefabOverridePublicInterface->AreOverridesPresent(m_entityId));

            EditEditorTransform(m_entityId, edited);
            ExpectSameScaleAndOption(GetEditorTransform(m_entityId), edited);
            EXPECT_TRUE(m_prefabOverridePublicInterface->AreOverridesPresent(m_entityId));

            Undo();
            ExpectSameScaleAndOption(GetEditorTransform(m_entityId), original);
            EXPECT_FALSE(m_prefabOverridePublicInterface->AreOverridesPresent(m_entityId));

            Redo();
            ExpectSameScaleAndOption(GetEditorTransform(m_entityId), edited);
            EXPECT_TRUE(m_prefabOverridePublicInterface->AreOverridesPresent(m_entityId));
        }

        AZ::EntityId m_entityId;
        AZ::EntityId m_nestedContainerId;
        AZ::EntityId m_topContainerId;
    };

    TEST_F(PrefabTransformOverrideTest, ScaleOverrideSurvivesPropagationUndoAndRedo)
    {
        EditorTransform edited = GetEditorTransform(m_entityId);
        edited.m_authoredScale = AZ::Vector3(2.0f);

        ExpectOverrideToSurvivePropagationUndoAndRedo(edited);
    }

    // A flip keeps the float next to the vector at 1, so only the vector is patched.
    TEST_F(PrefabTransformOverrideTest, AuthoredScaleOverrideSurvivesPropagationUndoAndRedo)
    {
        EditorTransform edited = GetEditorTransform(m_entityId);
        edited.m_authoredScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);

        ExpectOverrideToSurvivePropagationUndoAndRedo(edited);
    }

    TEST_F(PrefabTransformOverrideTest, DeployExtendedOverrideSurvivesPropagationUndoAndRedo)
    {
        EditorTransform edited = GetEditorTransform(m_entityId);
        edited.m_deployExtended = true;

        ExpectOverrideToSurvivePropagationUndoAndRedo(edited);
    }

    TEST_F(PrefabTransformOverrideTest, ChildOfAFlippedNestedPrefabShowsTheInheritedTag)
    {
        EXPECT_TRUE(ReadHeaderTag(m_entityId).empty());

        // The container entity of the nested prefab is the parent of the entity, and the flip is an override from the focused
        // prefab. The entity itself keeps its own plain scale.
        EditAuthoredScale(m_nestedContainerId, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_FALSE(ReadHeaderTag(m_entityId).empty());
        EXPECT_TRUE(GetEditorTransform(m_entityId).IsTransform1Scale());
        EXPECT_FALSE(GetEditorTransform(m_entityId).IsExtended());

        Undo();

        EXPECT_TRUE(ReadHeaderTag(m_entityId).empty());
    }

    // The same flip decides what the entity deploys as. A prefab boundary does not matter: the child is a Transform 2 at export
    // because its parent serves, and it is a Transform 1 again when the override is undone.
    TEST_F(PrefabTransformOverrideTest, ChildOfAFlippedNestedPrefabDeploysAsTransform2)
    {
        using AzToolsFramework::Components::TransformComponent;

        auto deploy = [this](AZ::Entity& gameEntity)
        {
            AZ::Entity* entity = AzToolsFramework::GetEntityById(m_entityId);
            ASSERT_NE(entity, nullptr);
            auto* transform = entity->FindComponent<TransformComponent>();
            ASSERT_NE(transform, nullptr);
            transform->BuildGameEntity(&gameEntity);
        };

        AZ::Entity plainGameEntity;
        deploy(plainGameEntity);
        EXPECT_NE(plainGameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
        EXPECT_EQ(plainGameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);

        EditAuthoredScale(m_nestedContainerId, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        AZ::Entity flippedGameEntity;
        deploy(flippedGameEntity);
        EXPECT_NE(flippedGameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);
        EXPECT_EQ(flippedGameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);

        Undo();

        AZ::Entity undoneGameEntity;
        deploy(undoneGameEntity);
        EXPECT_NE(undoneGameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
        EXPECT_EQ(undoneGameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);
    }
} // namespace UnitTest
