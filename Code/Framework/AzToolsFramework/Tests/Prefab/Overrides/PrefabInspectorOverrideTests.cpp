/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzToolsFramework/Prefab/DocumentPropertyEditor/PrefabPropertyEditorNodes.h>
#include <AzToolsFramework/UI/PropertyEditor/ComponentEditor.hxx>
#include <Prefab/Overrides/PrefabInspectorOverrideTestFixture.h>

namespace UnitTest
{
    using PrefabInspectorOverrideTest = PrefabInspectorOverrideTestFixture;

    // These index paths depend on multiple factors like the data in the component, how its reflected to serialize and edit contexts,
    // how different DPE adapters likes ReflectionAdapter and PrefabAdapter construct the DPE DOM etc. Therefore, these may
    // change in the future if the data stored in the DPE DOM itself changes and need to be modified accordingly to prevent test failures.
    constexpr AZStd::string_view pathToTranslateRow = "/1/2";

    TEST_F(PrefabInspectorOverrideTest, ValidatePresenceOfOverrideProperty)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);

        CreateAndValidateEditEntityOverride(newEntityId, grandparentContainerId);

        GenerateComponentAdapterDoms(newEntityId);

        AzToolsFramework::ComponentEditor::VisitComponentAdapterContentsCallback callback = [](const AZ::Dom::Value& adapterContents)
        {
            using AZ::DocumentPropertyEditor::Nodes::PropertyEditor;
            using AzToolsFramework::Prefab::PrefabPropertyEditorNodes::PrefabOverrideLabel;

            EXPECT_FALSE(adapterContents.IsArrayEmpty());

            // Validate the translate row has overridden data.
            AZ::Dom::Path domPathToTranslateRow(pathToTranslateRow);
            AZ::Dom::Value translateRow = adapterContents[domPathToTranslateRow];
            EXPECT_EQ(translateRow.GetType(), AZ::Dom::Type::Node);
            EXPECT_EQ(translateRow.ArraySize(), 2);

            AZ::Dom::Value labelPropertyEditor = translateRow[0];
            EXPECT_EQ(labelPropertyEditor[PropertyEditor::Type.GetName()].GetString(), PrefabOverrideLabel::Name);
            EXPECT_EQ(labelPropertyEditor[PrefabOverrideLabel::Value.GetName()].GetString(), "Translate");
            EXPECT_FALSE(labelPropertyEditor[PrefabOverrideLabel::RelativePath.GetName()].GetString().empty());
            EXPECT_FALSE(labelPropertyEditor[PrefabOverrideLabel::RevertOverride.GetName()].IsNull());

            EXPECT_TRUE(labelPropertyEditor[PrefabOverrideLabel::IsOverridden.GetName()].GetBool());

            AZ::Dom::Value valuePropertyEditor = translateRow[1];
            EXPECT_EQ(valuePropertyEditor[PropertyEditor::Value.GetName()][0].GetDouble(), 10.0);
        };

        ValidateComponentEditorDomContents(callback);
    }

    TEST_F(PrefabInspectorOverrideTest, ValidateAbsenceOfOverrideProperty)
    {
        AZ::EntityId newEntityId, parentContainerId, grandparentContainerId;
        CreateEntityInNestedPrefab(newEntityId, parentContainerId, grandparentContainerId);

        EditEntityAndValidateNoOverride(newEntityId);
        m_prefabFocusPublicInterface->FocusOnOwningPrefab(grandparentContainerId);

        GenerateComponentAdapterDoms(newEntityId);

        AzToolsFramework::ComponentEditor::VisitComponentAdapterContentsCallback callback = [](const AZ::Dom::Value& adapterContents)
        {
            using AZ::DocumentPropertyEditor::Nodes::PropertyEditor;
            using AzToolsFramework::Prefab::PrefabPropertyEditorNodes::PrefabOverrideLabel;

            EXPECT_FALSE(adapterContents.IsArrayEmpty());

            // Validate the translate row has no overridden data.
            AZ::Dom::Path domPathToTranslateRow(pathToTranslateRow);
            AZ::Dom::Value translateRow = adapterContents[domPathToTranslateRow];
            EXPECT_EQ(translateRow.GetType(), AZ::Dom::Type::Node);
            EXPECT_EQ(translateRow.ArraySize(), 2);

            AZ::Dom::Value labelPropertyEditor = translateRow[0];
            EXPECT_EQ(labelPropertyEditor[PropertyEditor::Type.GetName()].GetString(), PrefabOverrideLabel::Name);
            EXPECT_EQ(labelPropertyEditor[PrefabOverrideLabel::Value.GetName()].GetString(), "Translate");
            EXPECT_FALSE(labelPropertyEditor[PrefabOverrideLabel::RelativePath.GetName()].GetString().empty());
            EXPECT_FALSE(labelPropertyEditor[PrefabOverrideLabel::RevertOverride.GetName()].IsNull());

            EXPECT_FALSE(labelPropertyEditor[PrefabOverrideLabel::IsOverridden.GetName()].GetBool());

            AZ::Dom::Value valuePropertyEditor = translateRow[1];
            EXPECT_EQ(valuePropertyEditor[PropertyEditor::Value.GetName()][0].GetDouble(), 10.0);
        };

        ValidateComponentEditorDomContents(callback);
    }

    //////////////////////////////////////////////////////////////////////////
    // The Scale row of the Transform (I.1f).
    //
    // The row shows the AuthoredScale field. A Transform 1 scale is saved into UniformScale and AuthoredScale keeps its default,
    // so a prefab override of a plain Scale is patched under UniformScale alone. The label of the row has to look under both.

    class PrefabInspectorScaleRowTest : public PrefabInspectorOverrideTestFixture
    {
    protected:
        void SetUpEditorFixtureImpl() override
        {
            PrefabInspectorOverrideTestFixture::SetUpEditorFixtureImpl();

            // Edits of the entity are overrides, because the prefab that owns the entity is not the focused one.
            CreateEntityInNestedPrefab(m_entityId, m_nestedContainerId, m_topContainerId);
            m_prefabFocusPublicInterface->FocusOnOwningPrefab(m_topContainerId);
        }

        bool IsRowOverridden(AZStd::string_view labelText)
        {
            using AzToolsFramework::Prefab::PrefabPropertyEditorNodes::PrefabOverrideLabel;

            const AZ::Dom::Value label = FindRowLabel(m_entityId, labelText);
            EXPECT_TRUE(label.IsNode()) << "The inspector has no row labeled " << AZStd::string(labelText).c_str();
            return label.IsNode() && label[PrefabOverrideLabel::IsOverridden.GetName()].GetBool();
        }

        bool AreOverridesPresentAtField(AZStd::string_view field)
        {
            return m_prefabOverridePublicInterface->AreOverridesPresent(m_entityId, GetTransformFieldPath(m_entityId, field));
        }

        // Overrides the scale, then sends the revert that the context menu of the label sends. The revert has to give back the
        // scale of the source prefab on every path the override is under, and a single Undo has to bring it all back.
        void ExpectRevertThroughTheRowToBeOneUndoStep(const AZ::Vector3& overriddenScale)
        {
            using AzToolsFramework::Prefab::PrefabPropertyEditorNodes::PrefabOverrideLabel;

            EditAuthoredScale(m_entityId, overriddenScale);
            ASSERT_EQ(GetEditorTransform(m_entityId).m_authoredScale, overriddenScale);

            const AZ::Dom::Value scaleLabel = FindRowLabel(m_entityId, "Scale");
            ASSERT_TRUE(scaleLabel.IsNode());
            ASSERT_TRUE(scaleLabel[PrefabOverrideLabel::IsOverridden.GetName()].GetBool());

            const AZStd::string relativePath(scaleLabel[PrefabOverrideLabel::RelativePath.GetName()].GetString());
            ASSERT_TRUE(PrefabOverrideLabel::RevertOverride.InvokeOnDomNode(scaleLabel, AZStd::string_view(relativePath)).IsSuccess());
            PropagateAllTemplateChanges();

            EXPECT_EQ(GetEditorTransform(m_entityId).m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_FALSE(AreOverridesPresentAtField("AuthoredScale"));
            EXPECT_FALSE(AreOverridesPresentAtField("UniformScale"));
            EXPECT_FALSE(IsRowOverridden("Scale"));

            Undo();

            EXPECT_EQ(GetEditorTransform(m_entityId).m_authoredScale, overriddenScale);
            EXPECT_TRUE(IsRowOverridden("Scale"));
        }

        AZ::EntityId m_entityId;
        AZ::EntityId m_nestedContainerId;
        AZ::EntityId m_topContainerId;
    };

    TEST_F(PrefabInspectorScaleRowTest, ScaleRowIsNotMarkedWithoutAnOverride)
    {
        EXPECT_FALSE(IsRowOverridden("Scale"));
    }

    TEST_F(PrefabInspectorScaleRowTest, PlainScaleOverrideMarksTheScaleRow)
    {
        EditAuthoredScale(m_entityId, AZ::Vector3(2.0f));

        // The patch is under UniformScale alone, which is not the field the row shows.
        EXPECT_TRUE(AreOverridesPresentAtField("UniformScale"));
        EXPECT_FALSE(AreOverridesPresentAtField("AuthoredScale"));

        EXPECT_TRUE(IsRowOverridden("Scale"));
    }

    // A gizmo or a script writes the float through the transform bus, which ends in the same patch as a typed Scale.
    TEST_F(PrefabInspectorScaleRowTest, ScaleWrittenThroughTheTransformBusMarksTheScaleRow)
    {
        AZ::TransformBus::Event(m_entityId, &AZ::TransformInterface::SetLocalUniformScale, 2.0f);
        m_prefabPublicInterface->GenerateUndoNodesForEntityChangeAndUpdateCache(m_entityId, m_undoStack->GetTop());
        PropagateAllTemplateChanges();

        EXPECT_TRUE(AreOverridesPresentAtField("UniformScale"));
        EXPECT_FALSE(AreOverridesPresentAtField("AuthoredScale"));
        EXPECT_TRUE(IsRowOverridden("Scale"));
    }

    TEST_F(PrefabInspectorScaleRowTest, FlipOverrideMarksTheScaleRow)
    {
        EditAuthoredScale(m_entityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(AreOverridesPresentAtField("AuthoredScale"));

        EXPECT_TRUE(IsRowOverridden("Scale"));
    }

    TEST_F(PrefabInspectorScaleRowTest, ScaleOverrideDoesNotMarkTheOtherRows)
    {
        EditAuthoredScale(m_entityId, AZ::Vector3(2.0f));

        EXPECT_FALSE(IsRowOverridden("Translate"));
        EXPECT_FALSE(IsRowOverridden("Rotate"));
        EXPECT_FALSE(IsRowOverridden("Deploy as Extended"));
    }

    TEST_F(PrefabInspectorScaleRowTest, RevertingAPlainScaleThroughTheRowIsOneUndoStep)
    {
        ExpectRevertThroughTheRowToBeOneUndoStep(AZ::Vector3(2.0f));
    }

    TEST_F(PrefabInspectorScaleRowTest, RevertingAFlipThroughTheRowIsOneUndoStep)
    {
        ExpectRevertThroughTheRowToBeOneUndoStep(AZ::Vector3(-1.0f, 1.0f, 1.0f));
    }

    // A flip with a larger magnitude changes the float next to the vector as well, so both fields hold an override.
    TEST_F(PrefabInspectorScaleRowTest, RevertingAScaleOverriddenUnderBothFieldsIsOneUndoStep)
    {
        ExpectRevertThroughTheRowToBeOneUndoStep(AZ::Vector3(-2.0f, 2.0f, 2.0f));
    }

} // namespace UnitTest
