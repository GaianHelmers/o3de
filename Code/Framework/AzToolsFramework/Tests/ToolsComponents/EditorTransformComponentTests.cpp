/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzTest/AzTest.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/EntityUtils.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/RTTI/AttributeReader.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/Json/JsonSerialization.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/UnitTest/UnitTest.h>
#include <AzCore/std/limits.h>
#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/Prefab/Spawnable/EditorOnlyEntityHandler/WorldEditorOnlyEntityHandler.h>
#include <AzToolsFramework/Slice/SliceCompilation.h>
#include <AzToolsFramework/ToolsComponents/TransformComponent.h>
#include <AzToolsFramework/UI/DocumentPropertyEditor/DPEComponentAdapter.h>
#include <AzToolsFramework/UI/PropertyEditor/ComponentEditorHeader.hxx>
#include <AzToolsFramework/UI/PropertyEditor/PropertyEditorAPI.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>
#include <AzToolsFramework/ViewportSelection/EditorTransformComponentSelection.h>
#include <Prefab/PrefabTestFixture.h>

namespace AzToolsFramework
{
    struct TransformTestEntityHierarchy
    {
        AZ::EntityId m_parentId;
        AZ::EntityId m_childId;
        AZ::EntityId m_grandchild1Id;
        AZ::EntityId m_grandchild2Id;
    };

    class EditorTransformComponentTest
        : public UnitTest::ToolsApplicationFixture<>
    {
    public:
        static TransformTestEntityHierarchy BuildTestHierarchy()
        {
            TransformTestEntityHierarchy result;

            result.m_parentId = UnitTest::CreateDefaultEditorEntity("Parent");
            result.m_childId = UnitTest::CreateDefaultEditorEntity("Child");
            result.m_grandchild1Id = UnitTest::CreateDefaultEditorEntity("Grandchild1");
            result.m_grandchild2Id = UnitTest::CreateDefaultEditorEntity("Grandchild2");

            // Set parent-child relationships
            AZ::TransformBus::Event(result.m_childId, &AZ::TransformBus::Events::SetParent, result.m_parentId);
            AZ::TransformBus::Event(result.m_grandchild1Id, &AZ::TransformBus::Events::SetParent, result.m_childId);
            AZ::TransformBus::Event(result.m_grandchild2Id, &AZ::TransformBus::Events::SetParent, result.m_childId);

            return result;
        }
    };

    TEST_F(EditorTransformComponentTest, TransformTests_EntityHasParent_WorldScaleInheritsParentScale)
    {
        TransformTestEntityHierarchy hierarchy = BuildTestHierarchy();

        // Set scale to parent entity
        const float parentScale = 2.0f;
        AZ::TransformBus::Event(hierarchy.m_parentId, &AZ::TransformInterface::SetLocalUniformScale, parentScale);

        // Set scale to child entity
        const float childScale = 5.0f;
        AZ::TransformBus::Event(hierarchy.m_childId, &AZ::TransformInterface::SetLocalUniformScale, childScale);

        const float expectedScale = childScale * parentScale;

        float childWorldScale = 1.0f;
        AZ::TransformBus::EventResult(childWorldScale, hierarchy.m_childId, &AZ::TransformBus::Events::GetWorldUniformScale);

        EXPECT_NEAR(childWorldScale, expectedScale, AZ::Constants::Tolerance);
    }

    TEST_F(EditorTransformComponentTest, TransformTests_GetChildren_DirectChildrenMatchHierarchy)
    {
        TransformTestEntityHierarchy hierarchy = BuildTestHierarchy();

        EntityIdList children;
        AZ::TransformBus::EventResult(children, hierarchy.m_parentId, &AZ::TransformBus::Events::GetChildren);

        EXPECT_EQ(children.size(), 1);
        EXPECT_EQ(children[0], hierarchy.m_childId);
    }

    TEST_F(EditorTransformComponentTest, TransformTests_GetAllDescendants_AllDescendantsMatchHierarchy)
    {
        TransformTestEntityHierarchy hierarchy = BuildTestHierarchy();

        EntityIdList descendants;
        AZ::TransformBus::EventResult(descendants, hierarchy.m_parentId, &AZ::TransformBus::Events::GetAllDescendants);

        using ::testing::UnorderedElementsAre;
        EXPECT_THAT(descendants, UnorderedElementsAre(hierarchy.m_childId, hierarchy.m_grandchild1Id, hierarchy.m_grandchild2Id));
    }

    TEST_F(EditorTransformComponentTest, TransformTests_GetEntityAndAllDescendants_AllDescendantsMatchHierarchyAndResultIncludesParentEntity)
    {
        TransformTestEntityHierarchy hierarchy = BuildTestHierarchy();

        EntityIdList entityAndDescendants;
        AZ::TransformBus::EventResult(entityAndDescendants, hierarchy.m_parentId, &AZ::TransformBus::Events::GetEntityAndAllDescendants);

        std::sort(entityAndDescendants.begin(), entityAndDescendants.end());

        // entity1 is selected after undo
        using ::testing::UnorderedElementsAre;
        EXPECT_THAT(entityAndDescendants, UnorderedElementsAre(hierarchy.m_parentId, hierarchy.m_childId, hierarchy.m_grandchild1Id, hierarchy.m_grandchild2Id));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 1 / Transform 2 scale split
    namespace ScaleSplit
    {
        using Components::EditorTransform;
        using Components::TransformComponent;

        static EditorTransform MakeEditorTransform(const AZ::Vector3& scale)
        {
            EditorTransform editorTransform;
            editorTransform.m_authoredScale = scale;
            editorTransform.m_uniformScale = editorTransform.ResolveUniformScale();
            return editorTransform;
        }

        static TransformComponent* CreateEntityWithScale(const AZ::Vector3& scale)
        {
            const AZ::EntityId entityId = UnitTest::CreateDefaultEditorEntity("Entity");
            auto* transform = GetEntity(entityId)->FindComponent<TransformComponent>();
            transform->SetLocalEditorTransform(MakeEditorTransform(scale));
            return transform;
        }

        static rapidjson::Document StoreToJson(const TransformComponent& transform)
        {
            rapidjson::Document document;
            document.SetObject();
            AZ::JsonSerialization::Store(document, document.GetAllocator(), transform);
            return document;
        }

        // Section: Transform 1 is defined by its memory.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Transform1_IsStillRotationPlusOneFloatScalePlusTranslation)
        {
            EXPECT_EQ(sizeof(AZ::Transform), 8 * sizeof(float));
        }

        // Section: a flip is depiction. It never changes position or rotation.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Flip_LeavesPositionAndBasisUnchanged)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f));
            transform->SetWorldTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));
            transform->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, 0.5f));
            const AZ::Transform unflipped = transform->GetWorldTM();

            for (const AZ::Vector3& flip : { AZ::Vector3(-2.0f, 2.0f, 2.0f), AZ::Vector3(2.0f, 2.0f, -2.0f), AZ::Vector3(-2.0f, 2.0f, -2.0f) })
            {
                EditorTransform flipped = transform->GetLocalEditorTransform();
                flipped.m_authoredScale = flip;
                transform->SetLocalEditorTransform(flipped);

                const AZ::Transform& world = transform->GetWorldTM();
                EXPECT_THAT(world.GetTranslation(), UnitTest::IsClose(unflipped.GetTranslation()));
                EXPECT_THAT(world.GetBasisX(), UnitTest::IsClose(unflipped.GetBasisX()));
                EXPECT_THAT(world.GetBasisZ(), UnitTest::IsClose(unflipped.GetBasisZ()));
                EXPECT_GT(world.GetUniformScale(), 0.0f);
            }
        }

        // Section: engine math the flip design relies on. These only read existing code.
        TEST_F(EditorTransformComponentTest, ScaleSplit_EngineFact_MirroredMatrixRoundTripsThroughTransformAndScale)
        {
            const AZ::Transform placed = AZ::Transform::CreateFromQuaternionAndTranslation(
                AZ::Quaternion::CreateRotationZ(0.7f), AZ::Vector3(1.0f, 2.0f, 3.0f));
            AZ::Matrix3x4 mirrored = AZ::Matrix3x4::CreateFromTransform(placed);
            mirrored.MultiplyByScale(AZ::Vector3(-2.0f, 2.0f, 2.0f));

            // The same two reads TransformServiceFeatureProcessor makes for cull bounds and ray tracing.
            AZ::Transform readTransform = AZ::Transform::CreateFromMatrix3x4(mirrored);
            readTransform.ExtractUniformScale();
            const AZ::Vector3 readScale = mirrored.RetrieveScale();

            EXPECT_LT(readScale.GetMaxElement(), 0.0f);

            AZ::Matrix3x4 rebuilt = AZ::Matrix3x4::CreateFromTransform(readTransform);
            rebuilt.MultiplyByScale(readScale);
            EXPECT_THAT(rebuilt, UnitTest::IsClose(mirrored));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EngineFact_DeterminantSeparatesFlipsFromRotations)
        {
            EXPECT_GT(AZ::Matrix3x4::CreateScale(AZ::Vector3(1.0f, 1.0f, 1.0f)).GetDeterminant3x3(), 0.0f);
            EXPECT_LT(AZ::Matrix3x4::CreateScale(AZ::Vector3(-1.0f, 1.0f, 1.0f)).GetDeterminant3x3(), 0.0f);
            EXPECT_LT(AZ::Matrix3x4::CreateScale(AZ::Vector3(1.0f, 1.0f, -1.0f)).GetDeterminant3x3(), 0.0f);
            EXPECT_GT(AZ::Matrix3x4::CreateScale(AZ::Vector3(-1.0f, 1.0f, -1.0f)).GetDeterminant3x3(), 0.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EngineFact_NegativeScaleInvertsBoundsAndLodRadius)
        {
            // Aabb::MultiplyByScale asserts that min stays below max, which a negative scale breaks, and the assert is live in
            // debug builds. So the corners are scaled by hand, which is the same arithmetic it does before the assert.
            const AZ::Aabb bounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-1.0f, -2.0f, -3.0f), AZ::Vector3(1.0f, 2.0f, 3.0f));
            const AZ::Vector3 scale(-1.0f);
            const AZ::Vector3 scaledMin = bounds.GetMin() * scale;
            const AZ::Vector3 scaledMax = bounds.GetMax() * scale;

            // m_lodSelectionRadius is half of the extents (max minus min) in ModelDataInstance::UpdateCullBounds.
            EXPECT_LT((scaledMax - scaledMin).GetMaxElement(), 0.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EngineFact_GeneralInverseGivesUpAtSmallScale)
        {
            const AZ::Matrix3x4 tiny = AZ::Matrix3x4::CreateScale(AZ::Vector3(0.001f));
            EXPECT_THAT(tiny.GetInverseFull(), UnitTest::IsClose(AZ::Matrix3x4::CreateIdentity()));

            const AZ::Matrix3x4 small = AZ::Matrix3x4::CreateScale(AZ::Vector3(0.1f));
            EXPECT_THAT(small.GetInverseFull(), UnitTest::IsClose(AZ::Matrix3x4::CreateScale(AZ::Vector3(10.0f))));
        }

        // Section: the rule.
        TEST_F(EditorTransformComponentTest, ScaleSplit_EqualPositiveAxes_FitTransform1)
        {
            EXPECT_TRUE(MakeEditorTransform(AZ::Vector3(1.0f)).IsTransform1Scale());
            EXPECT_TRUE(MakeEditorTransform(AZ::Vector3(2.5f)).IsTransform1Scale());
            EXPECT_TRUE(MakeEditorTransform(AZ::Vector3(0.001f)).IsTransform1Scale());
            EXPECT_TRUE(MakeEditorTransform(AZ::Vector3(1.0f, 1.0000001192f, 1.0000001192f)).IsTransform1Scale());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_AnythingElse_TargetsTransform2)
        {
            EXPECT_FALSE(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)).IsTransform1Scale());
            EXPECT_FALSE(MakeEditorTransform(AZ::Vector3(-1.0f)).IsTransform1Scale());
            EXPECT_FALSE(MakeEditorTransform(AZ::Vector3(2.0f, 1.0f, 1.0f)).IsTransform1Scale());
            EXPECT_FALSE(MakeEditorTransform(AZ::Vector3(1.0f, 1.0f, 2.0f)).IsTransform1Scale());
            EXPECT_FALSE(MakeEditorTransform(AZ::Vector3(0.0f)).IsTransform1Scale());
        }

        // Section: the float AZ::Transform carries.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Transform2StandIn_IsNeverNegativeOrZero)
        {
            EXPECT_FLOAT_EQ(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)).m_uniformScale, 1.0f);
            EXPECT_FLOAT_EQ(MakeEditorTransform(AZ::Vector3(-2.0f)).m_uniformScale, 2.0f);
            EXPECT_FLOAT_EQ(MakeEditorTransform(AZ::Vector3(-3.0f, 1.0f, 2.0f)).m_uniformScale, 3.0f);
            EXPECT_FLOAT_EQ(MakeEditorTransform(AZ::Vector3(0.0f)).m_uniformScale, AZ::MinTransformScale);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Transform1Float_IsNotClamped)
        {
            EXPECT_FLOAT_EQ(MakeEditorTransform(AZ::Vector3(0.001f)).m_uniformScale, 0.001f);
        }

        // Section: writes from the AZ::Transform side.
        TEST_F(EditorTransformComponentTest, ScaleSplit_UniformWrite_MovesTransform1Vector)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f));

            transform->SetLocalUniformScale(3.0f);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_UniformWrite_KeepsTransform2SignsAndRatios)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 2.0f, 1.0f));

            transform->SetLocalUniformScale(4.0f);

            EXPECT_THAT(transform->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(-2.0f, 4.0f, 2.0f)));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 4.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_MoveAndRotate_LeaveTransform2Untouched)
        {
            const AZ::Vector3 authoredScale(-1.0f, 1.0f, 1.0f);
            TransformComponent* transform = CreateEntityWithScale(authoredScale);

            transform->SetWorldTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));
            transform->SetLocalRotation(AZ::Vector3(0.5f, 0.0f, 0.0f));
            transform->SetWorldTM(transform->GetWorldTM());

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, authoredScale);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_NegativeUniformWrite_NeverReachesTransform)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));

            transform->SetLocalUniformScale(-2.0f);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(-2.0f));
            EXPECT_FALSE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_GT(transform->GetWorldTM().GetUniformScale(), 0.0f);
        }

        // Section: the save branch.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Save_Transform1WritesTheFloatAlone)
        {
            const rapidjson::Document document = StoreToJson(*CreateEntityWithScale(AZ::Vector3(2.0f)));

            const rapidjson::Value& transformData = document["Transform Data"];
            EXPECT_FLOAT_EQ(transformData["UniformScale"].GetFloat(), 2.0f);
            EXPECT_FALSE(transformData.HasMember("AuthoredScale"));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Save_DefaultTransform1WritesNoScaleAtAll)
        {
            const rapidjson::Document document = StoreToJson(*CreateEntityWithScale(AZ::Vector3(1.0f)));

            EXPECT_FALSE(document.HasMember("Transform Data"));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Save_Transform2WritesItsVectorAndAStandIn)
        {
            const rapidjson::Document document = StoreToJson(*CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f)));

            const rapidjson::Value& transformData = document["Transform Data"];
            EXPECT_TRUE(transformData.HasMember("AuthoredScale"));
            EXPECT_FLOAT_EQ(transformData["UniformScale"].GetFloat(), 2.0f);
        }

        // Section: loading.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Load_ExistingTransform1DataBecomesAUniformVector)
        {
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "UniformScale": 2.0 } })");

            TransformComponent transform;
            AZ::JsonSerialization::Load(transform, document);

            EXPECT_EQ(transform.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(2.0f));
            EXPECT_FLOAT_EQ(transform.GetLocalEditorTransform().m_uniformScale, 2.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Load_Transform2VectorWins)
        {
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "AuthoredScale": [-1.0, 1.0, 1.0], "UniformScale": 3.0 } })");

            TransformComponent transform;
            AZ::JsonSerialization::Load(transform, document);

            EXPECT_EQ(transform.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FLOAT_EQ(transform.GetLocalEditorTransform().m_uniformScale, 1.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Load_OldNegativeFloatNoLongerReachesTransform)
        {
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "UniformScale": -1.0 } })");

            TransformComponent transform;
            AZ::JsonSerialization::Load(transform, document);

            EXPECT_FALSE(transform.GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FLOAT_EQ(transform.GetLocalEditorTransform().m_uniformScale, 1.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Load_ZeroUniformScaleBecomesTransform2)
        {
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "UniformScale": 0.0 } })");

            TransformComponent transform;
            AZ::JsonSerialization::Load(transform, document);

            EXPECT_FALSE(transform.GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_EQ(transform.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(0.0f));
            EXPECT_FLOAT_EQ(transform.GetLocalEditorTransform().m_uniformScale, AZ::MinTransformScale);

            // It saves as a Transform 2, so the zero is not read back as a default Transform 1.
            const rapidjson::Document stored = StoreToJson(transform);
            EXPECT_TRUE(stored["Transform Data"].HasMember("AuthoredScale"));

            TransformComponent reloaded;
            AZ::JsonSerialization::Load(reloaded, stored);

            EXPECT_FALSE(reloaded.GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_EQ(reloaded.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(0.0f));
        }

        // Prefab templates are held in memory with defaults kept, so a Transform 1 carries a default vector there.
        TEST_F(EditorTransformComponentTest, ScaleSplit_RoundTrip_KeepingDefaultsPreservesTransform1Float)
        {
            AZ::JsonSerializerSettings settings;
            settings.m_keepDefaults = true;
            rapidjson::Document document;
            document.SetObject();
            AZ::JsonSerialization::Store(document, document.GetAllocator(), *CreateEntityWithScale(AZ::Vector3(2.0f)), settings);

            TransformComponent transform;
            AZ::JsonSerialization::Load(transform, document);

            EXPECT_EQ(transform.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(2.0f));
            EXPECT_FLOAT_EQ(transform.GetLocalEditorTransform().m_uniformScale, 2.0f);
        }

        // Section: the deploy branch.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_Transform1BuildsTheUnchangedRuntimeComponent)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f));

            AZ::Entity gameEntity;
            transform->BuildGameEntity(&gameEntity);

            auto* runtimeTransform = gameEntity.FindComponent<AzFramework::TransformComponent>();
            ASSERT_NE(runtimeTransform, nullptr);
            AZ::TransformConfig config;
            runtimeTransform->GetConfiguration(config);
            EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), 2.0f);
        }

        // Section: the header tag.
        static AZStd::string ReadHeaderTag(TransformComponent* transform)
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            const auto* editorData = serializeContext->FindClassData(azrtti_typeid<TransformComponent>())
                                         ->m_editData->FindElementData(AZ::Edit::ClassElements::EditorData);

            AZStd::string tag;
            AZ::AttributeReader reader(transform, editorData->FindAttribute(ComponentHeaderAttributes::Tag));
            EXPECT_TRUE(reader.Read<AZStd::string>(tag));
            return tag;
        }

        // The tooltip of the tag, read the way the component editor reads it.
        static AZStd::string ReadHeaderTagToolTip(TransformComponent* transform)
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            const auto* editorData = serializeContext->FindClassData(azrtti_typeid<TransformComponent>())
                                         ->m_editData->FindElementData(AZ::Edit::ClassElements::EditorData);

            AZStd::string toolTip;
            AZ::AttributeReader reader(transform, editorData->FindAttribute(ComponentHeaderAttributes::TagToolTip));
            EXPECT_TRUE(reader.Read<AZStd::string>(toolTip));
            return toolTip;
        }

        // The own scale of a deployed Transform 2, read through the interface its request bus serves. Calling GetLocalScale on
        // the component itself by name resolves to the deprecated Transform 1 getter, which is not what a test means.
        static AZ::Vector3 RuntimeLocalScale(const AzFramework::Transform2Component& transform2)
        {
            const AZ::Transform2Requests& requests = transform2;
            return requests.GetLocalScale();
        }

        // Writes the authored scale and leaves the rest of the transform, as typing into the Scale row does.
        static void SetAuthoredScale(TransformComponent* transform, const AZ::Vector3& scale)
        {
            EditorTransform editorTransform = transform->GetLocalEditorTransform();
            editorTransform.m_authoredScale = scale;
            transform->SetLocalEditorTransform(editorTransform);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_FollowsTheScaleBothWays)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            EXPECT_TRUE(ReadHeaderTag(transform).empty());

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            EXPECT_FALSE(ReadHeaderTag(transform).empty());

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f)));
            EXPECT_TRUE(ReadHeaderTag(transform).empty());
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: I.1d, a write of zero through the AZ::Transform API (Track View pop-in keys, scripts, tools).
        TEST_F(EditorTransformComponentTest, ScaleSplit_UniformWrite_ZeroThenPositive_ReturnsToTransform1)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));

            transform->SetLocalUniformScale(0.0f);

            // Zero is a Transform 2, and the float AZ::Transform carries is held at the smallest scale.
            EXPECT_FALSE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), AZ::MinTransformScale);

            transform->SetLocalUniformScale(1.0f);

            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 1.0f);
            EXPECT_TRUE(ReadHeaderTag(transform).empty());

            // Back on Transform 1, a scale saves the float alone.
            transform->SetLocalUniformScale(3.0f);

            const rapidjson::Document document = StoreToJson(*transform);
            const rapidjson::Value& transformData = document["Transform Data"];
            EXPECT_FLOAT_EQ(transformData["UniformScale"].GetFloat(), 3.0f);
            EXPECT_FALSE(transformData.HasMember("AuthoredScale"));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_UniformWrite_ZeroOnLiveTransform2_KeepsSignsAndRatios)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 0.5f));

            transform->SetLocalUniformScale(0.0f);

            // The scale folds to the smallest scale and keeps its signs and ratios. It does not collapse to a zero vector.
            const EditorTransform& editorTransform = transform->GetLocalEditorTransform();
            EXPECT_FALSE(editorTransform.IsTransform1Scale());
            EXPECT_GT(editorTransform.m_authoredScale.GetAbs().GetMaxElement(), 0.0f);
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), AZ::MinTransformScale);

            transform->SetLocalUniformScale(1.0f);

            EXPECT_THAT(editorTransform.m_authoredScale, UnitTest::IsClose(AZ::Vector3(-1.0f, 1.0f, 0.5f)));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 1.0f);
        }

        // A move rebuilds the transform from the float, which holds the stand-in of a zero scale. Writing that float back
        // is not a write of the stand-in, so a move must leave the zero alone.
        TEST_F(EditorTransformComponentTest, ScaleSplit_UniformWrite_MoveOnZeroScale_KeepsTheZero)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(0.0f));

            transform->SetWorldTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));
            transform->SetLocalTranslation(AZ::Vector3(4.0f, 5.0f, 6.0f));
            const AZ::Transform world = transform->GetWorldTM();
            transform->SetWorldTM(world);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(0.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), AZ::MinTransformScale);

            // The zero still returns to a Transform 1 when a size is written.
            transform->SetLocalUniformScale(1.0f);

            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: test helpers for the extended (Transform 2) services.

        static void SetDeployExtended(TransformComponent* transform, bool deployExtended)
        {
            EditorTransform editorTransform = transform->GetLocalEditorTransform();
            editorTransform.m_deployExtended = deployExtended;
            transform->SetLocalEditorTransform(editorTransform);
        }

        // Records what an extended transform announces, one letter each, in order:
        // A activated, S scale changed, W world changed, D deactivated.
        class Transform2Recorder
            : public AZ::Transform2NotificationBus::Handler
        {
        public:
            explicit Transform2Recorder(AZ::EntityId entityId)
                : m_entityId(entityId)
            {
                AZ::Transform2NotificationBus::Handler::BusConnect();
            }

            ~Transform2Recorder() override
            {
                AZ::Transform2NotificationBus::Handler::BusDisconnect();
            }

            void OnTransform2Activated(AZ::EntityId entityId, const AZ::Vector3& scale) override
            {
                Record('A', entityId, scale);
            }

            void OnTransform2Deactivated(AZ::EntityId entityId) override
            {
                Record('D', entityId, m_lastScale);
            }

            void OnTransform2ScaleChanged(AZ::EntityId entityId, const AZ::Vector3& scale) override
            {
                Record('S', entityId, scale);
            }

            void OnTransform2WorldChanged(AZ::EntityId entityId, const AZ::Transform&, const AZ::Vector3& scale) override
            {
                Record('W', entityId, scale);
            }

            AZStd::string m_log; //!< One letter per notification, in order.
            AZ::Vector3 m_lastScale = AZ::Vector3::CreateZero(); //!< The scale the last notification carried.

        private:
            void Record(char kind, AZ::EntityId entityId, const AZ::Vector3& scale)
            {
                if (entityId == m_entityId)
                {
                    m_log.push_back(kind);
                    m_lastScale = scale;
                }
            }

            AZ::EntityId m_entityId;
        };

        // Records, in one log and in order, what an entity announces on the Transform 1 side and on the Transform 2 side:
        // T the transform notification and E the transform changed event, then A S W D as Transform2Recorder has them.
        // The local and the world of the last announcement of each Transform 1 channel are kept.
        class TransformEventRecorder
            : public Transform2Recorder
            , public AZ::TransformNotificationBus::Handler
        {
        public:
            explicit TransformEventRecorder(AZ::EntityId entityId)
                : Transform2Recorder(entityId)
                , m_changedHandler(
                      [this](const AZ::Transform& local, const AZ::Transform& world)
                      {
                          m_log.push_back('E');
                          m_eventLocal = local;
                          m_eventWorld = world;
                      })
            {
                AZ::TransformNotificationBus::Handler::BusConnect(entityId);
                AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::BindTransformChangedEventHandler, m_changedHandler);
            }

            ~TransformEventRecorder() override
            {
                AZ::TransformNotificationBus::Handler::BusDisconnect();
            }

            void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override
            {
                m_log.push_back('T');
                m_notificationLocal = local;
                m_notificationWorld = world;
            }

            AZ::Transform m_notificationLocal = AZ::Transform::CreateIdentity();
            AZ::Transform m_notificationWorld = AZ::Transform::CreateIdentity();
            AZ::Transform m_eventLocal = AZ::Transform::CreateIdentity();
            AZ::Transform m_eventWorld = AZ::Transform::CreateIdentity();

        private:
            AZ::TransformChangedEvent::Handler m_changedHandler;
        };

        // How many times a kind of announcement is in a log.
        static int CountOf(const AZStd::string& log, char kind)
        {
            int count = 0;
            for (const char letter : log)
            {
                count += (letter == kind) ? 1 : 0;
            }
            return count;
        }

        // Counts the property change messages the inspector listens to for one entity.
        class ComponentPropertyChangeCounter
            : public PropertyEditorEntityChangeNotificationBus::Handler
        {
        public:
            explicit ComponentPropertyChangeCounter(AZ::EntityId entityId)
            {
                PropertyEditorEntityChangeNotificationBus::Handler::BusConnect(entityId);
            }

            ~ComponentPropertyChangeCounter() override
            {
                PropertyEditorEntityChangeNotificationBus::Handler::BusDisconnect();
            }

            void OnEntityComponentPropertyChanged(AZ::ComponentId) override
            {
                ++m_count;
            }

            int m_count = 0;
        };

        // Stands in for another owner of the single-handler non-uniform scale bus.
        class OtherResidualBusOwner
            : public AZ::NonUniformScaleRequestBus::Handler
        {
        public:
            explicit OtherResidualBusOwner(AZ::EntityId entityId)
            {
                AZ::NonUniformScaleRequestBus::Handler::BusConnect(entityId);
            }

            ~OtherResidualBusOwner() override
            {
                AZ::NonUniformScaleRequestBus::Handler::BusDisconnect();
            }

            AZ::Vector3 GetScale() const override
            {
                return AZ::Vector3(7.0f);
            }

            void SetScale(const AZ::Vector3&) override
            {
            }

            void RegisterScaleChangedEvent(AZ::NonUniformScaleChangedEvent::Handler&) override
            {
            }
        };

        //////////////////////////////////////////////////////////////////////////
        // Section: F.1, loading onto a live component, as the Python UpdateComponentForEntity does.
        TEST_F(EditorTransformComponentTest, ScaleSplit_LiveLoad_Transform2KeepsItsVectorWhenJsonHasNeitherKey)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 2.0f, 1.0f));
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "Translate": [1.0, 2.0, 3.0] } })");

            AZ::JsonSerialization::Load(*transform, document);

            const EditorTransform& editorTransform = transform->GetLocalEditorTransform();
            EXPECT_EQ(editorTransform.m_translate, AZ::Vector3(1.0f, 2.0f, 3.0f));
            EXPECT_EQ(editorTransform.m_authoredScale, AZ::Vector3(-1.0f, 2.0f, 1.0f));
            EXPECT_FLOAT_EQ(editorTransform.m_uniformScale, 2.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_LiveLoad_Transform1TakesTheLoadedFloat)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(3.0f));
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "UniformScale": 2.0 } })");

            AZ::JsonSerialization::Load(*transform, document);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(2.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, 2.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_LiveLoad_AnExplicitVectorStandsOverTheLiveOne)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            rapidjson::Document toTransform1;
            toTransform1.Parse(R"({ "Transform Data": { "AuthoredScale": [1.0, 1.0, 1.0] } })");

            AZ::JsonSerialization::Load(*transform, toTransform1);

            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, 1.0f);

            rapidjson::Document toNonUniform;
            toNonUniform.Parse(R"({ "Transform Data": { "AuthoredScale": [2.0, 1.0, 1.0] } })");

            AZ::JsonSerialization::Load(*transform, toNonUniform);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(2.0f, 1.0f, 1.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, 2.0f);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: F.2, the viewport reset clears a Transform 2.
        TEST_F(EditorTransformComponentTest, ScaleSplit_ResetScale_ClearsTransform2)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 2.0f, 1.0f));
            bool changedInternally = false;

            Etcs::SetEntityLocalScale(transform->GetEntityId(), 1.0f, changedInternally);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 1.0f);
            EXPECT_FALSE(changedInternally);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_SetEntityLocalScale_ReplacesTheWholeScaleWithTheGivenValue)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 2.0f, 1.0f));
            bool changedInternally = false;

            Etcs::SetEntityLocalScale(transform->GetEntityId(), 3.0f, changedInternally);

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 3.0f);
        }

        // The ditto scale gestures do not go through SetEntityLocalScale. They swap the uniform scale of the world transform of
        // each target for the copied one and write it back, so a Transform 2 target keeps its signs and axis ratios.
        TEST_F(EditorTransformComponentTest, ScaleSplit_DittoScale_KeepsTheTargetsSignsAndRatios)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 2.0f, 1.0f));

            AZ::Transform world = transform->GetWorldTM();
            world.ExtractUniformScale();
            transform->SetWorldTM(world * AZ::Transform::CreateUniformScale(3.0f));

            EXPECT_THAT(transform->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(-1.5f, 3.0f, 1.5f)));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 3.0f);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: F.3, paste carries the vector.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Paste_Transform2SourceCarriesItsVector)
        {
            TransformComponent* source = CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f));
            TransformComponent* destination = CreateEntityWithScale(AZ::Vector3(1.0f));

            TransformComponent::PasteOverComponent(source, destination);

            EXPECT_THAT(destination->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f)));
            EXPECT_FALSE(destination->GetLocalEditorTransform().IsTransform1Scale());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Paste_Transform2VectorScalesWithTheDestinationParent)
        {
            TransformComponent* source = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(2.0f));
            TransformComponent* destination = CreateEntityWithScale(AZ::Vector3(1.0f));
            destination->SetParent(parent->GetEntityId());

            TransformComponent::PasteOverComponent(source, destination);

            // The world scale matches the source, so the destination's local scale is halved under the doubled parent.
            EXPECT_THAT(destination->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(-0.5f, 0.5f, 0.5f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Paste_Transform1SourceStaysTheWorldTransformOnly)
        {
            TransformComponent* source = CreateEntityWithScale(AZ::Vector3(3.0f));
            TransformComponent* destination = CreateEntityWithScale(AZ::Vector3(1.0f));

            TransformComponent::PasteOverComponent(source, destination);

            EXPECT_EQ(destination->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
            EXPECT_TRUE(destination->GetLocalEditorTransform().IsTransform1Scale());
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: I.1b, the tag on a descendant of an extended transform.
        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_ChildShowsTheInheritedState)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));
            child->SetParent(parent->GetEntityId());
            EXPECT_TRUE(ReadHeaderTag(child).empty());

            parent->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            EXPECT_FALSE(ReadHeaderTag(child).empty());
            EXPECT_TRUE(child->GetLocalEditorTransform().IsTransform1Scale());

            parent->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f)));
            EXPECT_TRUE(ReadHeaderTag(child).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_InheritanceReachesEveryDescendant)
        {
            TransformComponent* root = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* grandchild = CreateEntityWithScale(AZ::Vector3(1.0f));
            child->SetParent(root->GetEntityId());
            grandchild->SetParent(child->GetEntityId());

            EXPECT_FALSE(ReadHeaderTag(grandchild).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_FollowsAReparent)
        {
            TransformComponent* extended = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* plain = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));

            child->SetParent(plain->GetEntityId());
            EXPECT_TRUE(ReadHeaderTag(child).empty());

            child->SetParent(extended->GetEntityId());
            EXPECT_FALSE(ReadHeaderTag(child).empty());

            child->SetParent(AZ::EntityId());
            EXPECT_TRUE(ReadHeaderTag(child).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_ParentAndReparentNotifyTheChildsInspector)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));
            child->SetParent(parent->GetEntityId());
            ComponentPropertyChangeCounter counter(child->GetEntityId());

            parent->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            const int afterParentChange = counter.m_count;
            EXPECT_GT(afterParentChange, 0);

            child->SetParent(AZ::EntityId());
            EXPECT_GT(counter.m_count, afterParentChange);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: I.1c, Deploy as Extended.
        TEST_F(EditorTransformComponentTest, ScaleSplit_DeployExtended_ForcesTheTagAtScaleOne)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));

            SetDeployExtended(transform, true);

            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_TRUE(transform->GetLocalEditorTransform().IsExtended());
            EXPECT_FALSE(ReadHeaderTag(transform).empty());

            SetDeployExtended(transform, false);

            EXPECT_FALSE(transform->GetLocalEditorTransform().IsExtended());
            EXPECT_TRUE(ReadHeaderTag(transform).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_DeployExtended_IsOmittedFromJsonAtItsDefault)
        {
            const rapidjson::Document transform2Document = StoreToJson(*CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f)));
            EXPECT_FALSE(transform2Document["Transform Data"].HasMember("DeployExtended"));

            TransformComponent* forced = CreateEntityWithScale(AZ::Vector3(1.0f));
            SetDeployExtended(forced, true);
            const rapidjson::Document forcedDocument = StoreToJson(*forced);

            // Only the option is written. The scale is still the default Transform 1 scale.
            const rapidjson::Value& transformData = forcedDocument["Transform Data"];
            EXPECT_TRUE(transformData["DeployExtended"].GetBool());
            EXPECT_FALSE(transformData.HasMember("AuthoredScale"));
            EXPECT_FALSE(transformData.HasMember("UniformScale"));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_DeployExtended_RoundTripsThroughJson)
        {
            TransformComponent* forced = CreateEntityWithScale(AZ::Vector3(2.0f));
            SetDeployExtended(forced, true);
            const rapidjson::Document document = StoreToJson(*forced);

            TransformComponent loaded;
            AZ::JsonSerialization::Load(loaded, document);

            EXPECT_TRUE(loaded.GetLocalEditorTransform().m_deployExtended);
            EXPECT_EQ(loaded.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(2.0f));
            EXPECT_TRUE(loaded.GetLocalEditorTransform().IsExtended());
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: the buses an extended transform serves.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_ArePresentOnlyWhileTheScaleNeedsThem)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));

            // A flip is extended but its magnitudes are equal, so there is no residual to serve.
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(2.0f, 1.0f, 1.0f)));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));

            AZ::Vector3 residual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleRequestBus::EventResult(residual, entityId, &AZ::NonUniformScaleRequests::GetScale);
            EXPECT_THAT(residual, UnitTest::IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f)));

            AZ::Vector3 signedScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(signedScale, entityId, &AZ::Transform2Requests::GetLocalScale);
            EXPECT_EQ(signedScale, AZ::Vector3(2.0f, 1.0f, 1.0f));

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f, 1.0f, -1.0f)));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f)));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_DeployAsExtendedServesTransform2AtScaleOne)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();

            SetDeployExtended(transform, true);

            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_NonUniformScaleBusIsLeftToAnotherOwner)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            OtherResidualBusOwner otherOwner(entityId);

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(2.0f, 1.0f, 1.0f)));

            AZ::Vector3 scale = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleRequestBus::EventResult(scale, entityId, &AZ::NonUniformScaleRequests::GetScale);
            EXPECT_EQ(scale, AZ::Vector3(7.0f));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_SetLocalScaleAndSetScaleWriteTheAuthoredScale)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-2.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();

            AZ::Transform2RequestBus::Event(entityId, &AZ::Transform2Requests::SetLocalScale, AZ::Vector3(1.0f, 1.0f, -3.0f));
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f, 1.0f, -3.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 3.0f);

            // The residual keeps the signs and the stand-in, so a uniform residual makes the magnitudes equal.
            AZ::NonUniformScaleRequestBus::Event(entityId, &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3(1.0f));
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f, 3.0f, -3.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: notifications are raised only while extended.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_Transform1RaisesNone)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            Transform2Recorder recorder(transform->GetEntityId());

            transform->SetWorldTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));
            transform->SetLocalUniformScale(2.0f);
            transform->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, 0.5f));

            EXPECT_STREQ(recorder.m_log.c_str(), "");
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_FollowTheTransformInAndOutOfExtended)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            Transform2Recorder recorder(transform->GetEntityId());

            // Crossing to Transform 2: activated, then the world.
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            EXPECT_STREQ(recorder.m_log.c_str(), "AW");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

            // The world, then the scale change, as Transform2Notifications says. The scale is the signed vector.
            recorder.m_log.clear();
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f, 1.0f, -1.0f)));
            EXPECT_STREQ(recorder.m_log.c_str(), "WS");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(1.0f, 1.0f, -1.0f));

            // A move leaves the scale alone: only the world.
            recorder.m_log.clear();
            transform->SetWorldTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "W");

            // Crossing back to Transform 1: the T1 scale, then deactivated. No world.
            recorder.m_log.clear();
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f)));
            EXPECT_STREQ(recorder.m_log.c_str(), "SD");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(1.0f));

            // And quiet again.
            recorder.m_log.clear();
            transform->SetWorldTranslation(AZ::Vector3(4.0f, 5.0f, 6.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "");
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_ActivationOfAnExtendedEntityIsAnnounced)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            Transform2Recorder recorder(transform->GetEntityId());

            transform->GetEntity()->Deactivate();
            EXPECT_STREQ(recorder.m_log.c_str(), "D");
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));

            recorder.m_log.clear();
            transform->GetEntity()->Activate();
            EXPECT_STREQ(recorder.m_log.c_str(), "AW");
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_DeployAsExtendedAnnouncesAtScaleOne)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            Transform2Recorder recorder(transform->GetEntityId());

            SetDeployExtended(transform, true);

            EXPECT_STREQ(recorder.m_log.c_str(), "AW");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(1.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: crossing between uniform and non-uniform queues one re-activation.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_QueuesOneReactivationForTheNextTick)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            Transform2Recorder recorder(entityId);

            // Three crossings in one tick. Presence follows at once, and the entity is not touched until the tick.
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            EXPECT_STREQ(recorder.m_log.c_str(), "WSWSWS");

            recorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();

            // One deactivate and one activate, and presence is right afterwards.
            EXPECT_STREQ(recorder.m_log.c_str(), "DAW");
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_EQ(transform->GetEntity()->GetState(), AZ::Entity::State::Active);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_AFlipAloneNeverReactivatesTheEntity)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            AZ::TickBus::ExecuteQueuedEvents();
            Transform2Recorder recorder(transform->GetEntityId());

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(1.0f, 1.0f, -1.0f)));
            recorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_STREQ(recorder.m_log.c_str(), "");
        }

        // Records, for every activation the entity system announces, whether the transform flags it as its own re-activation.
        class ReactivationProbe
            : public AZ::EntitySystemBus::Handler
        {
        public:
            explicit ReactivationProbe(AZ::EntityId entityId)
                : m_entityId(entityId)
            {
                AZ::EntitySystemBus::Handler::BusConnect();
            }

            ~ReactivationProbe() override
            {
                AZ::EntitySystemBus::Handler::BusDisconnect();
            }

            void OnEntityActivated(const AZ::EntityId& entityId) override
            {
                Record(entityId);
            }

            void OnEntityDeactivated(const AZ::EntityId& entityId) override
            {
                Record(entityId);
            }

            int m_announced = 0; //!< Activations and deactivations of the entity announced.
            int m_flagged = 0; //!< Of those, the ones flagged as the transform's own.

        private:
            void Record(AZ::EntityId entityId)
            {
                if (entityId == m_entityId)
                {
                    ++m_announced;
                    m_flagged += TransformComponent::IsReactivatingForTransform2(entityId) ? 1 : 0;
                }
            }

            AZ::EntityId m_entityId;
        };

        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_FlagsItsOwnReactivationSoTheInspectorKeepsItsTree)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            ReactivationProbe probe(entityId);

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            EXPECT_FALSE(TransformComponent::IsReactivatingForTransform2(entityId));
            AZ::TickBus::ExecuteQueuedEvents();

            // One deactivation and one activation, both flagged, and the flag is gone afterwards.
            EXPECT_EQ(probe.m_announced, 2);
            EXPECT_EQ(probe.m_flagged, 2);
            EXPECT_FALSE(TransformComponent::IsReactivatingForTransform2(entityId));

            // Any other re-activation of the entity is not flagged, so the inspector rebuilds for it as it always did.
            transform->GetEntity()->Deactivate();
            transform->GetEntity()->Activate();
            EXPECT_EQ(probe.m_announced, 4);
            EXPECT_EQ(probe.m_flagged, 2);
        }

        // A tick runs the functions that were queued before it, and one that is queued while it runs waits for the next. So one
        // call of ExecuteQueuedEvents cannot show a cycle that queues itself again: the entity has to be watched over several.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_ReactivatesOnceAndThenLeavesTheEntityAlone)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            ReactivationProbe probe(entityId);

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            for (int tick = 0; tick < 3; ++tick)
            {
                AZ::TickBus::ExecuteQueuedEvents();
            }

            // One deactivation and one activation, in the first tick. The activation finds the non-uniform bus served already, so it
            // sees no crossing of its own and queues nothing.
            EXPECT_EQ(probe.m_announced, 2);
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_APlainActivationOfAStretchedEntityQueuesNothing)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            ReactivationProbe probe(entityId);

            // The components change as they do in the editor, and the entity activates again with its scale as it was.
            transform->GetEntity()->Deactivate();
            transform->GetEntity()->Activate();
            ASSERT_EQ(probe.m_announced, 2);
            for (int tick = 0; tick < 2; ++tick)
            {
                AZ::TickBus::ExecuteQueuedEvents();
            }

            EXPECT_EQ(probe.m_announced, 2);
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
        }

        // The cycle clears the flag that holds back a second request, so a crossing in a later tick queues its own cycle.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_ASecondCrossingInALaterTickReactivatesAgain)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            Transform2Recorder recorder(entityId);

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            recorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();
            EXPECT_STREQ(recorder.m_log.c_str(), "DAW");
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));

            // Back to a flip: the bus goes at once, and the consumers that read its presence at activation are told by a second cycle.
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-1.0f, 1.0f, 1.0f)));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            recorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();
            EXPECT_STREQ(recorder.m_log.c_str(), "DAW");
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));

            // And once more, so that the second cycle cleared the flag as well.
            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-3.0f, 1.0f, 1.0f)));
            recorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();
            EXPECT_STREQ(recorder.m_log.c_str(), "DAW");
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
        }

        // The entity is looked up again when the tick comes. One that is not active then is not the cycle's to start.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_AnEntityDeactivatedBeforeTheTickIsNotActivatedByIt)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            AZ::Entity* entity = transform->GetEntity();
            AZ::TickBus::ExecuteQueuedEvents();
            ReactivationProbe probe(entity->GetId());

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            entity->Deactivate();
            AZ::TickBus::ExecuteQueuedEvents();

            // Only the deactivation made here was announced, and the tick left the entity as it was.
            EXPECT_EQ(probe.m_announced, 1);
            EXPECT_EQ(entity->GetState(), AZ::Entity::State::Init);

            entity->Activate();
            EXPECT_EQ(entity->GetState(), AZ::Entity::State::Active);
        }

        // The inspector adapter of a component, with the refresh level it has queued exposed.
        class InspectorAdapterProbe
            : public AZ::DocumentPropertyEditor::ComponentAdapter
        {
        public:
            explicit InspectorAdapterProbe(AZ::Component* component)
                : AZ::DocumentPropertyEditor::ComponentAdapter(component)
            {
            }

            PropertyModificationRefreshLevel QueuedRefreshLevel() const
            {
                return m_queuedRefreshLevel;
            }
        };

        // This covers what the adapter does, not what the person sees. That the Scale field keeps its caret and focus across
        // the crossing is a manual check: type -2 in X of a -1,1,1 Scale, then Tab to Y and type.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Crossing_InspectorAdapterKeepsItsDocumentAndStillRefreshes)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            AZ::Entity* entity = transform->GetEntity();
            AZ::TickBus::ExecuteQueuedEvents();

            InspectorAdapterProbe adapter(transform);
            int resetsQueued = 0;
            AZ::Event<>::Handler resetQueuedHandler([&resetsQueued]() { ++resetsQueued; });
            adapter.ConnectResetQueuedHandler(resetQueuedHandler);

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));
            EXPECT_LT(adapter.QueuedRefreshLevel(), Refresh_AttributesAndValues);
            AZ::TickBus::ExecuteQueuedEvents();

            // The entity cycled, and the adapter queued no new document. The refresh the transform broadcasts afterwards
            // reached it.
            EXPECT_EQ(resetsQueued, 0);
            EXPECT_GE(adapter.QueuedRefreshLevel(), Refresh_AttributesAndValues);

            // Any other re-activation of the entity still resets the document, as it always did.
            entity->Deactivate();
            entity->Activate();
            EXPECT_GT(resetsQueued, 0);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: the old Non-uniform Scale component owns the scale of its entity.
        TEST_F(EditorTransformComponentTest, ScaleSplit_OldNonUniformScaleComponent_KeepsTheExtendedServicesOut)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            AZ::Entity* entity = transform->GetEntity();
            const AZ::EntityId entityId = entity->GetId();

            // The old component joins the entity while it is inactive, as it does in the editor.
            entity->Deactivate();
            entity->CreateComponent<Components::EditorNonUniformScaleComponent>();
            entity->Activate();
            Transform2Recorder recorder(entityId);

            transform->SetLocalEditorTransform(MakeEditorTransform(AZ::Vector3(-2.0f, 1.0f, 1.0f)));

            // The transform is extended, yet nothing of Transform 2 runs: no bus, no announcement, and no deployed Transform 2.
            // The tag follows the memo and not the scale alone, so it is not shown either while the old component owns the scale.
            EXPECT_TRUE(transform->GetLocalEditorTransform().IsExtended());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_STREQ(recorder.m_log.c_str(), "");
            EXPECT_TRUE(ReadHeaderTag(transform).empty());

            AZ::Entity gameEntity;
            transform->BuildGameEntity(&gameEntity);
            EXPECT_NE(gameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
            EXPECT_EQ(gameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: converting the old Non-uniform Scale component into the Scale of the transform.
        //
        // The fixture has no prefab instance, so the undo system records nothing for these entities: one undo step that
        // restores the component and the old scale is not tested here, and stays a check for the Editor.

        using Components::EditorNonUniformScaleComponent;

        // Adds the old component to an entity the way the editor does, with the entity inactive, and gives it its scale.
        static EditorNonUniformScaleComponent* AddLegacyScaleComponent(TransformComponent* transform, const AZ::Vector3& legacyScale)
        {
            AZ::Entity* entity = transform->GetEntity();
            entity->Deactivate();
            auto* legacyComponent = entity->CreateComponent<EditorNonUniformScaleComponent>();
            entity->Activate();
            legacyComponent->SetScale(legacyScale);
            return legacyComponent;
        }

        // The "Convert to Transform Scale" button on the card of the old component: its only UI element.
        static const AZ::Edit::ElementData* FindConvertButton()
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            const AZ::Edit::ClassData* editData =
                serializeContext->FindClassData(azrtti_typeid<EditorNonUniformScaleComponent>())->m_editData;
            for (const AZ::Edit::ElementData& element : editData->m_elements)
            {
                if (element.m_elementId == AZ::Edit::ClassElements::UIElement)
                {
                    return &element;
                }
            }
            return nullptr;
        }

        // The pure part.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Fold_UniformScaleTimesTheOldScalePerAxis)
        {
            const AZ::Vector3 folded = EditorTransform::FoldLegacyScale(AZ::Vector3(3.0f), AZ::Vector3(2.0f, 1.0f, 1.0f));

            EXPECT_EQ(folded, AZ::Vector3(6.0f, 3.0f, 3.0f));
            EXPECT_FALSE(MakeEditorTransform(folded).IsTransform1Scale());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Fold_AnUniformOldScaleOnScaleOneFitsTransform1)
        {
            const AZ::Vector3 folded = EditorTransform::FoldLegacyScale(AZ::Vector3(1.0f), AZ::Vector3(2.0f));

            EXPECT_EQ(folded, AZ::Vector3(2.0f));
            EXPECT_TRUE(MakeEditorTransform(folded).IsTransform1Scale());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Fold_TheSignOfAnAuthoredTransform2Survives)
        {
            const AZ::Vector3 folded = EditorTransform::FoldLegacyScale(AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(2.0f, 1.0f, 1.0f));

            EXPECT_EQ(folded, AZ::Vector3(-2.0f, 1.0f, 1.0f));
            EXPECT_TRUE(AZ::Transform2::IsMirrored(folded));
        }

        // The whole conversion, through the same queue the button uses.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_FoldsTheScaleAndRemovesTheOldComponent)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(3.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AddLegacyScaleComponent(transform, AZ::Vector3(2.0f, 1.0f, 1.0f));
            ASSERT_NE(GetEntity(entityId)->FindComponent<EditorNonUniformScaleComponent>(), nullptr);

            TransformComponent::QueueLegacyScaleConversion(entityId);
            AZ::TickBus::ExecuteQueuedEvents();

            // The component is gone, the entity is active again, and the transform holds the folded vector.
            AZ::Entity* entity = GetEntity(entityId);
            ASSERT_NE(entity, nullptr);
            EXPECT_EQ(entity->FindComponent<EditorNonUniformScaleComponent>(), nullptr);
            EXPECT_EQ(entity->GetState(), AZ::Entity::State::Active);
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(6.0f, 3.0f, 3.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 6.0f);

            // The tag, the buses and the deploy branch follow, as they do for a Scale typed into the row.
            EXPECT_FALSE(ReadHeaderTag(transform).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            AZ::Vector3 residual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleRequestBus::EventResult(residual, entityId, &AZ::NonUniformScaleRequests::GetScale);
            EXPECT_THAT(residual, UnitTest::IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f)));

            AZ::Entity gameEntity;
            transform->BuildGameEntity(&gameEntity);
            const auto* runtimeTransform2 = gameEntity.FindComponent<AzFramework::Transform2Component>();
            ASSERT_NE(runtimeTransform2, nullptr);
            EXPECT_EQ(gameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
            EXPECT_EQ(RuntimeLocalScale(*runtimeTransform2), AZ::Vector3(6.0f, 3.0f, 3.0f));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_ParkedTransform2GoesLiveAndKeepsItsSign)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AddLegacyScaleComponent(transform, AZ::Vector3(2.0f, 1.0f, 1.0f));

            // Parked behind the old component: extended, and serving nothing.
            EXPECT_TRUE(transform->GetLocalEditorTransform().IsExtended());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));
            Transform2Recorder recorder(entityId);

            TransformComponent::QueueLegacyScaleConversion(entityId);
            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(-2.0f, 1.0f, 1.0f));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_NE(recorder.m_log.find('A'), AZStd::string::npos);
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(-2.0f, 1.0f, 1.0f));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_EachEntityFoldsItsOwnScaleAndAFittingResultReturnsToTransform1)
        {
            TransformComponent* stretched = CreateEntityWithScale(AZ::Vector3(3.0f));
            TransformComponent* uniform = CreateEntityWithScale(AZ::Vector3(1.0f));
            AddLegacyScaleComponent(stretched, AZ::Vector3(2.0f, 1.0f, 1.0f));
            AddLegacyScaleComponent(uniform, AZ::Vector3(2.0f));

            TransformComponent::QueueLegacyScaleConversion(stretched->GetEntityId());
            TransformComponent::QueueLegacyScaleConversion(uniform->GetEntityId());
            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_EQ(stretched->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(6.0f, 3.0f, 3.0f));
            EXPECT_EQ(GetEntity(stretched->GetEntityId())->FindComponent<EditorNonUniformScaleComponent>(), nullptr);

            // Old (2,2,2) on a uniform 1 is (2,2,2): Transform 1 again, with no extended services and no tag.
            const AZ::EntityId uniformId = uniform->GetEntityId();
            EXPECT_EQ(uniform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(2.0f));
            EXPECT_TRUE(uniform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_EQ(GetEntity(uniformId)->FindComponent<EditorNonUniformScaleComponent>(), nullptr);
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(uniformId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(uniformId));
            EXPECT_TRUE(ReadHeaderTag(uniform).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_ALockedTransformKeepsItsOldComponentAndItsScale)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(3.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            auto* legacyComponent = AddLegacyScaleComponent(transform, AZ::Vector3(2.0f, 1.0f, 1.0f));

            EditorTransform lockedTransform = transform->GetLocalEditorTransform();
            lockedTransform.m_locked = true;
            transform->SetLocalEditorTransform(lockedTransform);

            // The button greys out while the transform is locked.
            bool readOnly = false;
            AZ::AttributeReader readOnlyReader(legacyComponent, FindConvertButton()->FindAttribute(AZ::Edit::Attributes::ReadOnly));
            ASSERT_TRUE(readOnlyReader.Read<bool>(readOnly));
            EXPECT_TRUE(readOnly);

            // A conversion that was queued before the lock does nothing.
            TransformComponent::QueueLegacyScaleConversion(entityId);
            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_NE(GetEntity(entityId)->FindComponent<EditorNonUniformScaleComponent>(), nullptr);
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
        }

        // An entity that is gone when the tick comes is an id the application can no longer find, which is all the queued
        // conversion sees. The test passes by not crashing. Destroying a fixture entity through the entity context is not
        // known to work here, so the id comes from an entity that left scope.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_AnEntityGoneBeforeTheTickIsSkipped)
        {
            AZ::EntityId goneEntityId;
            {
                AZ::Entity goneEntity;
                goneEntityId = goneEntity.GetId();
            }
            ASSERT_EQ(GetEntity(goneEntityId), nullptr);

            TransformComponent::QueueLegacyScaleConversion(goneEntityId);
            AZ::TickBus::ExecuteQueuedEvents();
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_AnInactiveEntityIsSkipped)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(3.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AddLegacyScaleComponent(transform, AZ::Vector3(2.0f, 1.0f, 1.0f));

            TransformComponent::QueueLegacyScaleConversion(entityId);
            GetEntity(entityId)->Deactivate();
            AZ::TickBus::ExecuteQueuedEvents();
            GetEntity(entityId)->Activate();

            EXPECT_NE(GetEntity(entityId)->FindComponent<EditorNonUniformScaleComponent>(), nullptr);
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_AnEntityWithNoOldComponentIsLeftAlone)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(3.0f));

            TransformComponent::QueueLegacyScaleConversion(transform->GetEntityId());
            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
        }

        // The button: pressing it only queues, so the component is still there when its own notification returns.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Convert_ButtonPressQueuesTheConversionForTheNextTick)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(3.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            auto* legacyComponent = AddLegacyScaleComponent(transform, AZ::Vector3(2.0f, 1.0f, 1.0f));

            const AZ::Edit::ElementData* button = FindConvertButton();
            ASSERT_NE(button, nullptr);

            AZStd::string buttonText;
            AZ::AttributeReader textReader(legacyComponent, button->FindAttribute(AZ::Edit::Attributes::ButtonText));
            ASSERT_TRUE(textReader.Read<AZStd::string>(buttonText));
            EXPECT_STREQ(buttonText.c_str(), "Convert to Transform Scale");

            bool readOnly = true;
            AZ::AttributeReader readOnlyReader(legacyComponent, button->FindAttribute(AZ::Edit::Attributes::ReadOnly));
            ASSERT_TRUE(readOnlyReader.Read<bool>(readOnly));
            EXPECT_FALSE(readOnly);

            // Run the change notification on the component, as the property editor does for a press.
            AZ::u32 refreshLevel = 0;
            AZ::AttributeReader notifyReader(legacyComponent, button->FindAttribute(AZ::Edit::Attributes::ChangeNotify));
            ASSERT_TRUE(notifyReader.Read<AZ::u32>(refreshLevel));
            EXPECT_EQ(refreshLevel, static_cast<AZ::u32>(AZ::Edit::PropertyRefreshLevels::None));

            EXPECT_NE(GetEntity(entityId)->FindComponent<EditorNonUniformScaleComponent>(), nullptr);
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));

            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_EQ(GetEntity(entityId)->FindComponent<EditorNonUniformScaleComponent>(), nullptr);
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(6.0f, 3.0f, 3.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: the deploy branch for an extended transform.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_Transform1NeverGetsATransform2)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f));

            AZ::Entity gameEntity;
            transform->BuildGameEntity(&gameEntity);

            EXPECT_NE(gameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
            EXPECT_EQ(gameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_ExtendedDeploysATransform2InsteadOfTheTransform)
        {
            for (const AZ::Vector3& scale : { AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(2.0f, 1.0f, 1.0f), AZ::Vector3(1.0f, 1.0f, -1.0f) })
            {
                TransformComponent* transform = CreateEntityWithScale(scale);

                AZ::Entity gameEntity;
                transform->BuildGameEntity(&gameEntity);

                // One transform, and it is the Transform 2: FindComponent matches the exact type, so a Transform 1 beside it
                // would be found here. The old shape was a Transform 1 with a Transform 2 beside it.
                const auto* runtimeTransform2 = gameEntity.FindComponent<AzFramework::Transform2Component>();
                ASSERT_NE(runtimeTransform2, nullptr);
                EXPECT_EQ(gameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
                EXPECT_EQ(RuntimeLocalScale(*runtimeTransform2), scale);

                // The configuration it holds carries the positive stand-in in the Transform 1 float.
                AZ::TransformConfig config;
                runtimeTransform2->GetConfiguration(config);
                EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), AZ::Transform2::UniformStandIn(scale));
            }
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_DeployAsExtendedAddsTransform2AtScaleOne)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            SetDeployExtended(transform, true);

            AZ::Entity gameEntity;
            transform->BuildGameEntity(&gameEntity);

            const auto* runtimeTransform2 = gameEntity.FindComponent<AzFramework::Transform2Component>();
            ASSERT_NE(runtimeTransform2, nullptr);
            EXPECT_EQ(gameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
            EXPECT_EQ(RuntimeLocalScale(*runtimeTransform2), AZ::Vector3(1.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: I.1h, the typing path.
        //
        // Typing a Scale into the inspector does not call SetLocalEditorTransform. The property handler writes m_authoredScale
        // in place, and the ChangeNotify of the "Transform Data" row settles the rest. The tests write the member the same way
        // and run that notify, which is everything of the typing path except the widget.

        // The member the property handler of a row writes into.
        static EditorTransform& EditorTransformInPlace(TransformComponent* transform)
        {
            return const_cast<EditorTransform&>(transform->GetLocalEditorTransform());
        }

        // Runs the ChangeNotify of the "Transform Data" row on the component, as the property editor does after a write.
        // Returns the refresh level it asks for.
        static AZ::u32 RunTransformDataChangeNotify(TransformComponent* transform)
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            const AZ::Edit::ClassData* editData = serializeContext->FindClassData(azrtti_typeid<TransformComponent>())->m_editData;

            for (const AZ::Edit::ElementData& element : editData->m_elements)
            {
                if (element.m_serializeClassElement != nullptr && AZStd::string_view(element.m_serializeClassElement->m_name) == "Transform Data")
                {
                    AZ::u32 refreshLevel = 0;
                    AZ::AttributeReader notifyReader(transform, element.FindAttribute(AZ::Edit::Attributes::ChangeNotify));
                    EXPECT_TRUE(notifyReader.Read<AZ::u32>(refreshLevel));
                    return refreshLevel;
                }
            }

            ADD_FAILURE() << "The Transform Data row has no ChangeNotify to run.";
            return 0;
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Typing_ChangeNotifyResolvesTheStandInAndKeepsTheVector)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            const AZ::Vector3 typedScale(-3.0f, 1.0f, 2.0f);

            // The write leaves the float it derives from the vector as it was, until the notify.
            EditorTransformInPlace(transform).m_authoredScale = typedScale;
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, 1.0f);

            const AZ::u32 refreshLevel = RunTransformDataChangeNotify(transform);

            EXPECT_EQ(refreshLevel, static_cast<AZ::u32>(AZ::Edit::PropertyRefreshLevels::None));
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, typedScale);
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, AZ::Transform2::UniformStandIn(typedScale));
            EXPECT_FLOAT_EQ(transform->GetLocalUniformScale(), 3.0f);
            EXPECT_FLOAT_EQ(transform->GetWorldTM().GetUniformScale(), 3.0f);
            EXPECT_FALSE(ReadHeaderTag(transform).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Typing_AUniformScaleReturnsToTransform1)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FALSE(ReadHeaderTag(transform).empty());

            EditorTransformInPlace(transform).m_authoredScale = AZ::Vector3(2.0f);
            RunTransformDataChangeNotify(transform);

            EXPECT_TRUE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, 2.0f);
            EXPECT_FLOAT_EQ(transform->GetWorldTM().GetUniformScale(), 2.0f);
            EXPECT_TRUE(ReadHeaderTag(transform).empty());

            // The float is what a Transform 1 saves, and nothing more.
            const rapidjson::Document document = StoreToJson(*transform);
            const rapidjson::Value& transformData = document["Transform Data"];
            EXPECT_FLOAT_EQ(transformData["UniformScale"].GetFloat(), 2.0f);
            EXPECT_FALSE(transformData.HasMember("AuthoredScale"));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Typing_ZeroKeepsTheStandInAboveZero)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f));

            EditorTransformInPlace(transform).m_authoredScale = AZ::Vector3(0.0f);
            RunTransformDataChangeNotify(transform);

            EXPECT_FALSE(transform->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(0.0f));
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, AZ::MinTransformScale);
            EXPECT_GT(transform->GetWorldTM().GetUniformScale(), 0.0f);
        }

        // Deploy as Extended is a row of the same card, so it settles through the same notify.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Typing_DeployExtendedShowsTheTagThroughTheNotify)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));

            EditorTransformInPlace(transform).m_deployExtended = true;
            RunTransformDataChangeNotify(transform);

            EXPECT_TRUE(transform->GetLocalEditorTransform().IsExtended());
            EXPECT_FALSE(ReadHeaderTag(transform).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));

            EditorTransformInPlace(transform).m_deployExtended = false;
            RunTransformDataChangeNotify(transform);

            EXPECT_TRUE(ReadHeaderTag(transform).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));
        }

        // The notify also reaches the Transform 2 services, so a typed flip is served and announced like an applied one.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Typing_AFlipActivatesTheTransform2ServicesAndTypingBackLetsGo)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            Transform2Recorder recorder(entityId);

            EditorTransformInPlace(transform).m_authoredScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
            RunTransformDataChangeNotify(transform);

            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_NE(recorder.m_log.find('A'), AZStd::string::npos);
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

            EditorTransformInPlace(transform).m_authoredScale = AZ::Vector3(1.0f);
            RunTransformDataChangeNotify(transform);

            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));
            EXPECT_NE(recorder.m_log.find('D'), AZStd::string::npos);
        }

        // The float of a save comes from the vector, and not from whatever the float held when the save ran. A scale written in place
        // has not been through the notify yet, which is what settles the float, and it has to be saved all the same.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Save_TheFloatComesFromTheVectorBeforeTheNotifyRuns)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));

            EditorTransformInPlace(transform).m_authoredScale = AZ::Vector3(-3.0f, 1.0f, 2.0f);
            ASSERT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, 1.0f);

            const rapidjson::Document document = StoreToJson(*transform);

            ASSERT_TRUE(document.HasMember("Transform Data"));
            const rapidjson::Value& transformData = document["Transform Data"];
            ASSERT_TRUE(transformData.HasMember("UniformScale"));
            EXPECT_FLOAT_EQ(transformData["UniformScale"].GetFloat(), 3.0f);
            EXPECT_TRUE(transformData.HasMember("AuthoredScale"));
        }

        // A load onto a component whose entity is not active announces nothing, because nothing is listening to an entity that is not
        // running, and activating it reads the data afresh.
        TEST_F(EditorTransformComponentTest, ScaleSplit_LiveLoad_OntoAnEntityThatIsNotActiveAnnouncesNothing)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::Entity* entity = transform->GetEntity();
            AZ::TickBus::ExecuteQueuedEvents();
            TransformEventRecorder recorder(entityId);
            ComponentPropertyChangeCounter counter(entityId);
            entity->Deactivate();
            ASSERT_EQ(entity->GetState(), AZ::Entity::State::Init);
            recorder.m_log.clear();
            counter.m_count = 0;

            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "AuthoredScale": [-2.0, 1.0, 1.0], "UniformScale": 2.0 } })");
            AZ::JsonSerialization::Load(*transform, document);

            // The data is loaded, and no transform, no Transform 2 and no inspector heard of it.
            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(-2.0f, 1.0f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "");
            EXPECT_EQ(counter.m_count, 0);
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));

            // Activating it announces the loaded data.
            entity->Activate();
            EXPECT_NE(recorder.m_log.find('A'), AZStd::string::npos);
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(-2.0f, 1.0f, 1.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: I.4, a child of an extended transform serves Transform 2 by proxy (D.44, D.45).
        //
        // Every transform reads only its direct parent: whether it serves, and its exact world. A child serves when its parent
        // does, so the designation is derived and is written nowhere in the data of the child. The expected values are worked
        // out by hand. The flipped parent mirrors X and sits at x = 10, so a child at local x = 3 is at 10 + (-1)(3) = 7.

        // A root flipped on X, at x = 10.
        static TransformComponent* CreateFlippedParent()
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            parent->SetWorldTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f));
            return parent;
        }

        // A child that keeps the local it is given, so that what a test writes there is what it reads back.
        static TransformComponent* CreateChildOf(
            TransformComponent* parent, const AZ::Vector3& localTranslation, const AZ::Vector3& scale = AZ::Vector3(1.0f))
        {
            TransformComponent* child = CreateEntityWithScale(scale);
            child->SetParentRelative(parent->GetEntityId());
            child->SetLocalTranslation(localTranslation);
            return child;
        }

        // The game entity a transform deploys: the components it was given and nothing else.
        static AZStd::unique_ptr<AZ::Entity> Deploy(TransformComponent* transform)
        {
            auto gameEntity = AZStd::make_unique<AZ::Entity>();
            transform->BuildGameEntity(gameEntity.get());
            return gameEntity;
        }

        // FindComponent matches the exact type, so these two cannot both be true of one entity.
        static bool DeploysTransform2(AZ::Entity& gameEntity)
        {
            return gameEntity.FindComponent<AzFramework::Transform2Component>() != nullptr &&
                gameEntity.FindComponent<AzFramework::TransformComponent>() == nullptr;
        }

        static bool DeploysTransform1(AZ::Entity& gameEntity)
        {
            return gameEntity.FindComponent<AzFramework::TransformComponent>() != nullptr &&
                gameEntity.FindComponent<AzFramework::Transform2Component>() == nullptr;
        }

        // The configuration the deployed transform holds, whichever kind it is.
        static AZ::TransformConfig DeployedConfiguration(AZ::Entity& gameEntity)
        {
            AZ::TransformConfig config;
            auto* transform = AZ::EntityUtils::FindFirstDerivedComponent<AzFramework::TransformComponent>(&gameEntity);
            EXPECT_NE(transform, nullptr);
            if (transform)
            {
                transform->GetConfiguration(config);
            }
            return config;
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: deploy of the descendants (D.44, D.45).

        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_AChildOfAnExtendedTransformDeploysAsTransform2WithItsOwnScale)
        {
            // R is a plain parent. P is flipped and sits under it, beside the plain sibling B. C is a child of P and G a child of C.
            // S is a plain root that has nothing to do with any of them.
            TransformComponent* root = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* parent = CreateChildOf(root, AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* sibling = CreateChildOf(root, AZ::Vector3(5.0f, 0.0f, 0.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* grandchild = CreateChildOf(child, AZ::Vector3(1.0f, 0.0f, 0.0f));
            TransformComponent* unrelated = CreateEntityWithScale(AZ::Vector3(1.0f));

            auto rootGame = Deploy(root);
            auto parentGame = Deploy(parent);
            auto siblingGame = Deploy(sibling);
            auto childGame = Deploy(child);
            auto grandchildGame = Deploy(grandchild);
            auto unrelatedGame = Deploy(unrelated);

            // The flipped entity and everything below it deploy as Transform 2. Nothing else does.
            EXPECT_TRUE(DeploysTransform1(*rootGame));
            ASSERT_TRUE(DeploysTransform2(*parentGame));
            EXPECT_TRUE(DeploysTransform1(*siblingGame));
            ASSERT_TRUE(DeploysTransform2(*childGame));
            ASSERT_TRUE(DeploysTransform2(*grandchildGame));
            EXPECT_TRUE(DeploysTransform1(*unrelatedGame));

            // The own scale of a descendant is its own: only the flipped entity carries the flip. The flip reaches the others
            // through the chain at runtime.
            EXPECT_EQ(RuntimeLocalScale(*parentGame->FindComponent<AzFramework::Transform2Component>()), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_EQ(RuntimeLocalScale(*childGame->FindComponent<AzFramework::Transform2Component>()), AZ::Vector3(1.0f));
            EXPECT_EQ(RuntimeLocalScale(*grandchildGame->FindComponent<AzFramework::Transform2Component>()), AZ::Vector3(1.0f));

            // The links of the hierarchy are kept, and the authored locals are the ones deployed.
            const AZ::TransformConfig childConfig = DeployedConfiguration(*childGame);
            EXPECT_EQ(childConfig.m_parentId, parent->GetEntityId());
            EXPECT_THAT(childConfig.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
            const AZ::TransformConfig grandchildConfig = DeployedConfiguration(*grandchildGame);
            EXPECT_EQ(grandchildConfig.m_parentId, child->GetEntityId());
            EXPECT_THAT(grandchildConfig.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(1.0f, 0.0f, 0.0f)));

            // The world carried along is the true position: C is at 10 - 3 = 7 and G at 7 - 1 = 6.
            EXPECT_THAT(childConfig.m_worldTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(grandchildConfig.m_worldTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(6.0f, 0.0f, 0.0f)));

            // The deployed configuration of a plain entity is the one it always was.
            EXPECT_THAT(
                DeployedConfiguration(*siblingGame).m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(5.0f, 0.0f, 0.0f)));
            EXPECT_FLOAT_EQ(DeployedConfiguration(*siblingGame).m_localTransform.GetUniformScale(), 1.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_AChildKeepsItsFloatWhenItDeploysAsTransform2)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f), AZ::Vector3(2.0f));

            auto childGame = Deploy(child);

            // The child is still a plain scale of 2 in the editor, and the Transform 2 it deploys as holds the same scale.
            EXPECT_TRUE(child->GetLocalEditorTransform().IsTransform1Scale());
            ASSERT_TRUE(DeploysTransform2(*childGame));
            EXPECT_EQ(RuntimeLocalScale(*childGame->FindComponent<AzFramework::Transform2Component>()), AZ::Vector3(2.0f));
            EXPECT_FLOAT_EQ(DeployedConfiguration(*childGame).m_localTransform.GetUniformScale(), 2.0f);
        }

        // The Static flag and the parent activation mode have no setter on the transform, so they are set the way a file or a script
        // sets them. They have to reach the Transform 2 as they reach the Transform 1: a static entity that deployed movable, or a
        // child that deployed with the wrong mode, would behave differently from the same entity at scale one.
        // The interpolation modes are copied as well, but the runtime transform does not keep them in its configuration, so they
        // cannot be read back and are not tested here.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_TheStaticFlagAndTheActivationModeReachTheDeployedTransform)
        {
            rapidjson::Document options;
            options.Parse(R"({ "Parent Activation Transform Mode": 1, "IsStatic": true })");

            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* plain = CreateEntityWithScale(AZ::Vector3(2.0f));
            for (TransformComponent* transform : { parent, child, plain })
            {
                AZ::JsonSerialization::Load(*transform, options);
            }

            auto parentGame = Deploy(parent);
            auto childGame = Deploy(child);
            auto plainGame = Deploy(plain);
            ASSERT_TRUE(DeploysTransform2(*parentGame));
            ASSERT_TRUE(DeploysTransform2(*childGame));
            ASSERT_TRUE(DeploysTransform1(*plainGame));

            // The Transform 1 is the reference: it carries both, so the Transform 2 of the same settings has to as well.
            const auto expectBothOptions = [](AZ::Entity& gameEntity)
            {
                const AZ::TransformConfig config = DeployedConfiguration(gameEntity);
                EXPECT_TRUE(config.m_isStatic) << gameEntity.GetName().c_str();
                EXPECT_EQ(config.m_parentActivationTransformMode, AZ::TransformConfig::ParentActivationTransformMode::MaintainCurrentWorldTransform)
                    << gameEntity.GetName().c_str();
            };
            expectBothOptions(*plainGame);
            expectBothOptions(*parentGame);
            expectBothOptions(*childGame);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Deploy_TurningTheParentOffReturnsTheChildToTransform1)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* plain = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* flipped = CreateChildOf(parent, AZ::Vector3(0.0f, 4.0f, 0.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* forced = CreateChildOf(parent, AZ::Vector3(0.0f, 0.0f, 5.0f));
            SetDeployExtended(forced, true);

            // While the parent is extended all three deploy as Transform 2, and the plain one serves the buses.
            auto plainGame = Deploy(plain);
            EXPECT_TRUE(DeploysTransform2(*plainGame));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(plain->GetEntityId()));
            EXPECT_FALSE(ReadHeaderTag(plain).empty());
            const EditorTransform plainBefore = plain->GetLocalEditorTransform();

            SetAuthoredScale(parent, AZ::Vector3(1.0f));

            // The designation was only derived, so it is gone with nothing to undo: the plain child is a Transform 1 again, in
            // the deploy and in its live buses, and its own data was never touched.
            auto plainAgain = Deploy(plain);
            EXPECT_TRUE(DeploysTransform1(*plainAgain));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(plain->GetEntityId()));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(plain->GetEntityId()));
            EXPECT_TRUE(ReadHeaderTag(plain).empty());
            EXPECT_EQ(plain->GetLocalEditorTransform().m_authoredScale, plainBefore.m_authoredScale);
            EXPECT_EQ(plain->GetLocalEditorTransform().m_translate, plainBefore.m_translate);
            EXPECT_FLOAT_EQ(plain->GetLocalEditorTransform().m_uniformScale, plainBefore.m_uniformScale);

            // A child with a scale of its own, or with Deploy as Extended, stays a Transform 2 without its parent.
            auto flippedGame = Deploy(flipped);
            EXPECT_TRUE(DeploysTransform2(*flippedGame));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(flipped->GetEntityId()));
            auto forcedGame = Deploy(forced);
            EXPECT_TRUE(DeploysTransform2(*forcedGame));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(forced->GetEntityId()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Save_AChildOfAFlippedParentSavesItsOwnDataAlone)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            ASSERT_FALSE(ReadHeaderTag(child).empty());

            const rapidjson::Document document = StoreToJson(*child);

            // The child saves where it sits and nothing of the state it inherits: no vector, no float, no option.
            ASSERT_TRUE(document.HasMember("Transform Data"));
            const rapidjson::Value& transformData = document["Transform Data"];
            EXPECT_TRUE(transformData.HasMember("Translate"));
            EXPECT_FALSE(transformData.HasMember("AuthoredScale"));
            EXPECT_FALSE(transformData.HasMember("DeployExtended"));
            EXPECT_FALSE(transformData.HasMember("UniformScale"));

            // And it loads back as the plain child it was.
            TransformComponent loaded;
            AZ::JsonSerialization::Load(loaded, document);
            EXPECT_TRUE(loaded.GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FALSE(loaded.GetLocalEditorTransform().IsExtended());
            EXPECT_EQ(loaded.GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: reads and writes of the world under a flipped parent (D.47, D.48, D.50).

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_WorldReadsSeeTheTruePositionUnderAFlippedParent)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));

            // Composing the floats as Transform 1 would put the child at 10 + 3 = 13. The mirror puts it at 10 - 3 = 7.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_FLOAT_EQ(child->GetWorldX(), 7.0f);
            EXPECT_THAT(child->GetWorldTM().GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            AZ::Vector3 busPosition = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(busPosition, child->GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
            EXPECT_THAT(busPosition, UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            // The Transform 1 view never carries the sign: a positive float and the authored rotation.
            EXPECT_FLOAT_EQ(child->GetWorldTM().GetUniformScale(), 1.0f);
            EXPECT_THAT(child->GetWorldRotationQuaternion(), UnitTest::IsClose(AZ::Quaternion::CreateIdentity()));

            // The local calls are the authored values.
            EXPECT_THAT(child->GetLocalTranslation(), UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_MovingTheParentMovesTheChildByTheSameAmount)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));

            parent->SetWorldTranslation(AZ::Vector3(12.0f, 0.5f, 0.25f));

            // The child is still 3 to the left of its parent: 12 - 3 = 9. Y and Z are not mirrored.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(9.0f, 0.5f, 0.25f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ALiveFlipOfTheParentMovesEveryDescendantAndNoOneElse)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* first = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* second = CreateChildOf(parent, AZ::Vector3(0.0f, 4.0f, 0.0f));
            TransformComponent* grandchild = CreateChildOf(first, AZ::Vector3(1.0f, 0.0f, 0.0f));
            TransformComponent* unrelated = CreateEntityWithScale(AZ::Vector3(1.0f));
            unrelated->SetWorldTranslation(AZ::Vector3(20.0f, 0.0f, 0.0f));

            const AZ::Vector3 mirroredFirst(7.0f, 0.0f, 0.0f);
            const AZ::Vector3 mirroredSecond(10.0f, 4.0f, 0.0f);
            const AZ::Vector3 mirroredGrandchild(6.0f, 0.0f, 0.0f);
            EXPECT_THAT(first->GetWorldTranslation(), UnitTest::IsClose(mirroredFirst));
            EXPECT_THAT(second->GetWorldTranslation(), UnitTest::IsClose(mirroredSecond));
            EXPECT_THAT(grandchild->GetWorldTranslation(), UnitTest::IsClose(mirroredGrandchild));

            // No call is made on the descendants. The parent turns back to a plain scale: 10 + 3, 10 and 13 + 1.
            SetAuthoredScale(parent, AZ::Vector3(1.0f));
            EXPECT_THAT(first->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(13.0f, 0.0f, 0.0f)));
            EXPECT_THAT(second->GetWorldTranslation(), UnitTest::IsClose(mirroredSecond));
            EXPECT_THAT(grandchild->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(14.0f, 0.0f, 0.0f)));

            // And flipped again, which brings the mirrored positions back.
            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_THAT(first->GetWorldTranslation(), UnitTest::IsClose(mirroredFirst));
            EXPECT_THAT(second->GetWorldTranslation(), UnitTest::IsClose(mirroredSecond));
            EXPECT_THAT(grandchild->GetWorldTranslation(), UnitTest::IsClose(mirroredGrandchild));

            // A plain root elsewhere did not move and serves nothing.
            EXPECT_THAT(unrelated->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(20.0f, 0.0f, 0.0f)));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(unrelated->GetEntityId()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_WritingTheWorldBackWritesNothingAndStillAnnounces)
        {
            TransformComponent* parent = CreateFlippedParent();
            parent->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, 0.5f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 1.0f, 0.0f));
            child->SetLocalRotation(AZ::Vector3(0.0f, 0.3f, 0.0f));
            const EditorTransform before = child->GetLocalEditorTransform();
            const AZ::Transform worldBefore = child->GetWorldTM();
            Transform2Recorder recorder(child->GetEntityId());

            child->SetWorldTM(child->GetWorldTM());

            // Each part of the request is compared with the world exactly, so nothing differs and nothing is written. Not a bit of
            // float noise reaches the authored values that are saved.
            const EditorTransform& after = child->GetLocalEditorTransform();
            EXPECT_EQ(after.m_translate, before.m_translate);
            EXPECT_EQ(after.m_rotate, before.m_rotate);
            EXPECT_EQ(after.m_authoredScale, before.m_authoredScale);
            EXPECT_EQ(after.m_uniformScale, before.m_uniformScale);
            EXPECT_EQ(child->GetWorldTM(), worldBefore);

            // A write is announced even when nothing changed, as it is for a Transform 1.
            EXPECT_STREQ(recorder.m_log.c_str(), "W");
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_RotatingAChildNeverChangesItsAuthoredScale)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));

            // A drag is a run of small world rotations about Z, each written on top of the world that was read.
            for (int step = 1; step <= 10; ++step)
            {
                child->SetWorldRotationQuaternion(AZ::Quaternion::CreateRotationZ(0.05f * static_cast<float>(step)));

                EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
                EXPECT_FLOAT_EQ(child->GetLocalEditorTransform().m_uniformScale, 1.0f);
            }

            // A plain child never became extended by itself, and a rotation left its place alone.
            EXPECT_TRUE(child->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FALSE(child->GetLocalEditorTransform().IsExtended());
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            // The world rotation is the 0.5 radians that were written. The authored one is its mirror image, -0.5 radians, which is
            // -28.6479 degrees: a rotation about Z turns the other way under a parent that mirrors X.
            EXPECT_THAT(child->GetWorldRotationQuaternion(), UnitTest::IsClose(AZ::Quaternion::CreateRotationZ(0.5f)));
            const AZ::Vector3 authoredRotation = child->GetLocalEditorTransform().m_rotate;
            EXPECT_NEAR(authoredRotation.GetX(), 0.0f, 0.01f);
            EXPECT_NEAR(authoredRotation.GetY(), 0.0f, 0.01f);
            EXPECT_NEAR(authoredRotation.GetZ(), -28.6479f, 0.01f);
        }

        // The parents above have a float of one, where the float asked for and the ratio to the float held are the same number.
        // A parent of float 2 puts them apart: the world float is 2 while the own float is 1.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_AWorldWriteOfTheFloatUnderAMagnifiedParentIsWrittenByRatio)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f));
            parent->SetWorldTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            ASSERT_FLOAT_EQ(child->GetWorldTM().GetUniformScale(), 2.0f);

            // The world is asked for 4 where it had 2. The own scale of 1 takes the ratio of 2, and not the 4, which would make 8.
            AZ::Transform world = child->GetWorldTM();
            world.SetUniformScale(4.0f);
            child->SetWorldTM(world);

            EXPECT_THAT(child->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(2.0f, 2.0f, 2.0f)));
            EXPECT_FLOAT_EQ(child->GetLocalEditorTransform().m_uniformScale, 2.0f);
            EXPECT_FLOAT_EQ(child->GetWorldTM().GetUniformScale(), 4.0f);

            // The place is the one it had: 10 - 2 * 3 = 4.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(4.0f, 0.0f, 0.0f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_RotatingAChildUnderAMagnifiedParentNeverChangesItsAuthoredScale)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f));
            parent->SetWorldTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));

            // The request of each write carries the world float, which is 2 here, and the own scale is 1. A write that applied that
            // float to the own scale would turn it into 2 at the first step.
            for (int step = 1; step <= 10; ++step)
            {
                child->SetWorldRotationQuaternion(AZ::Quaternion::CreateRotationZ(0.05f * static_cast<float>(step)));

                EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f)) << "step " << step;
                EXPECT_FLOAT_EQ(child->GetLocalEditorTransform().m_uniformScale, 1.0f) << "step " << step;
            }
            EXPECT_FLOAT_EQ(child->GetWorldTM().GetUniformScale(), 2.0f);
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(4.0f, 0.0f, 0.0f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ARotationUnderAMirroredParentIsConjugated)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3::CreateZero());

            child->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, AZ::Constants::HalfPi));

            // The authored rotation reads back as it was written: 90 degrees about Z.
            EXPECT_THAT(child->GetLocalRotationQuaternion(), UnitTest::IsClose(AZ::Quaternion(0.0f, 0.0f, 0.70710678f, 0.70710678f)));

            // The Transform 1 view is the rotation the child is drawn with: mirrored in X, a turn about Z reverses.
            EXPECT_THAT(child->GetWorldRotationQuaternion(), UnitTest::IsClose(AZ::Quaternion(0.0f, 0.0f, -0.70710678f, 0.70710678f)));

            // The exact world is diag(-1,1,1) times a quarter turn about Z, whose columns are (0,1,0), (-1,0,0) and (0,0,1).
            // The first row changes sign: (0,1,0), (1,0,0) and (0,0,1). The determinant is -1.
            AZ::Matrix3x4 matrix = AZ::Matrix3x4::CreateIdentity();
            AZ::Transform2RequestBus::EventResult(matrix, child->GetEntityId(), &AZ::Transform2Requests::GetWorldMatrix);
            EXPECT_THAT(matrix.GetBasisX(), UnitTest::IsClose(AZ::Vector3(0.0f, 1.0f, 0.0f)));
            EXPECT_THAT(matrix.GetBasisY(), UnitTest::IsClose(AZ::Vector3(1.0f, 0.0f, 0.0f)));
            EXPECT_THAT(matrix.GetBasisZ(), UnitTest::IsClose(AZ::Vector3(0.0f, 0.0f, 1.0f)));
            EXPECT_LT(matrix.GetDeterminant3x3(), 0.0f);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ANonUniformParentGivesARotatedChildTheExactMatrixAndShear)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(1.0f, 2.0f, 0.0f));
            child->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, AZ::Constants::QuarterPi));
            const AZ::EntityId childId = child->GetEntityId();

            // The exact world is the plain product diag(2,1,1) * T(1,2,0) * Rz(45): the linear part is diag(2,1,1) times the
            // rotation, whose columns are (c,s,0) and (-s,c,0) with c = s = 0.70710678. The first row doubles.
            AZ::Matrix3x4 matrix = AZ::Matrix3x4::CreateIdentity();
            AZ::Transform2RequestBus::EventResult(matrix, childId, &AZ::Transform2Requests::GetWorldMatrix);
            EXPECT_THAT(matrix.GetBasisX(), UnitTest::IsClose(AZ::Vector3(1.41421356f, 0.70710678f, 0.0f)));
            EXPECT_THAT(matrix.GetBasisY(), UnitTest::IsClose(AZ::Vector3(-1.41421356f, 0.70710678f, 0.0f)));
            EXPECT_THAT(matrix.GetBasisZ(), UnitTest::IsClose(AZ::Vector3(0.0f, 0.0f, 1.0f)));
            EXPECT_THAT(matrix.GetTranslation(), UnitTest::IsClose(AZ::Vector3(2.0f, 2.0f, 0.0f)));

            // The first two columns are no longer orthogonal: the cosine between them is -0.6. No rotation and scale express that.
            bool sheared = false;
            AZ::Transform2RequestBus::EventResult(sheared, childId, &AZ::Transform2Requests::IsWorldSheared);
            EXPECT_TRUE(sheared);

            // The Transform 1 readers still see the true position.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(2.0f, 2.0f, 0.0f)));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: reparent (D.51).

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_KeepWorldReparentKeepsThePositionAndTheOwnScaleAndTakesTheMask)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));
            child->SetWorldTranslation(AZ::Vector3(7.0f, 0.0f, 0.0f));
            const AZ::EntityId childId = child->GetEntityId();

            child->SetParent(parent->GetEntityId());

            // The position is kept: the authored local is solved against the parent, 10 - t = 7.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(child->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));

            // The own scale is untouched, signs included. The child is still a plain scale in the editor.
            EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_TRUE(child->GetLocalEditorTransform().IsTransform1Scale());

            // It takes the flip of its new parent, as the mask: drawn mirrored where it stands.
            AZ::Vector3 worldScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(worldScale, childId, &AZ::Transform2Requests::GetWorldScale);
            EXPECT_EQ(worldScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            AZ::Vector3 localScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(localScale, childId, &AZ::Transform2Requests::GetLocalScale);
            EXPECT_EQ(localScale, AZ::Vector3(1.0f));
            AZ::Matrix3x4 matrix = AZ::Matrix3x4::CreateIdentity();
            AZ::Transform2RequestBus::EventResult(matrix, childId, &AZ::Transform2Requests::GetWorldMatrix);
            EXPECT_THAT(matrix.GetBasisX(), UnitTest::IsClose(AZ::Vector3(-1.0f, 0.0f, 0.0f)));
            EXPECT_THAT(matrix.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_FALSE(ReadHeaderTag(child).empty());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_KeepWorldReparentFoldsTheOwnFloatSoTheWorldMagnitudeStays)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f));
            parent->SetWorldTranslation(AZ::Vector3(10.0f, 0.0f, 0.0f));
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(2.0f));
            child->SetWorldTranslation(AZ::Vector3(7.0f, 0.0f, 0.0f));
            ASSERT_FLOAT_EQ(child->GetWorldTM().GetUniformScale(), 2.0f);

            child->SetParent(parent->GetEntityId());

            // The position is kept: 10 + (-2) t = 7 gives t = 1.5.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(child->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(1.5f, 0.0f, 0.0f)));

            // The parent now supplies a magnitude of 2, so the own scale of 2 would make the child twice as big. It folds to 1, by
            // ratio, and the world float is the 2 it was. The sign comes from the parent: the world scale is (-2,2,2).
            EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_FLOAT_EQ(child->GetLocalEditorTransform().m_uniformScale, 1.0f);
            EXPECT_FLOAT_EQ(child->GetWorldTM().GetUniformScale(), 2.0f);
            AZ::Vector3 worldScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(worldScale, child->GetEntityId(), &AZ::Transform2Requests::GetWorldScale);
            EXPECT_EQ(worldScale, AZ::Vector3(-2.0f, 2.0f, 2.0f));
        }

        // The identity rotation of the other reparent tests is the one rotation that the unmirrored lines of the reparent get right,
        // so none of them tells the solve of the rotation from its absence.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_KeepWorldReparentKeepsTheRotationUnderATurnedAndFlippedParent)
        {
            // The parent is flipped on X at x = 10 and turned 30 degrees about Z. The child is a root at x = 7, turned 90 degrees.
            TransformComponent* parent = CreateFlippedParent();
            parent->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, AZ::DegToRad(30.0f)));
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));
            child->SetWorldTranslation(AZ::Vector3(7.0f, 0.0f, 0.0f));
            child->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, AZ::Constants::HalfPi));

            child->SetParent(parent->GetEntityId());

            // The view of the child is kept, the rotation of it as well as the place.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(child->GetWorldRotationQuaternion(), UnitTest::IsClose(AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi)));

            // The authored rotation is solved against the chain: the parent turns the world by 30, so the child needs 60 more, and
            // the mirror reverses a turn about Z, so it is authored as -60. The unmirrored line would leave +60 there, which the
            // parent would draw as 30 - 60 = -30, 120 degrees away from where the child stood.
            const AZ::Vector3 authoredRotation = child->GetLocalEditorTransform().m_rotate;
            EXPECT_NEAR(authoredRotation.GetX(), 0.0f, 0.01f);
            EXPECT_NEAR(authoredRotation.GetY(), 0.0f, 0.01f);
            EXPECT_NEAR(authoredRotation.GetZ(), -60.0f, 0.01f);

            // The place is solved the same way: 7 - 10 = -3 along X of the world, which in the frame of the parent (turned back by 30,
            // then mirrored in X) is (2.598076, 1.5, 0) = (3 cos 30, 3 sin 30, 0).
            EXPECT_THAT(child->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(2.598076f, 1.5f, 0.0f)));
            EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_KeepWorldReparentOutOfAFlippedParentKeepsThePositionAndReturnsToTransform1)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            const AZ::EntityId childId = child->GetEntityId();
            Transform2Recorder recorder(childId);
            ASSERT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            child->SetParent(AZ::EntityId());

            // The authored local is the position the child kept: 7 on its own.
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(child->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));

            // And it is a plain Transform 1 again: no buses, no tag, and the listeners were told it let go.
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(childId));
            EXPECT_TRUE(ReadHeaderTag(child).empty());
            ASSERT_FALSE(recorder.m_log.empty());
            EXPECT_EQ(recorder.m_log.back(), 'D');
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_RelativeReparentKeepsTheLocalAndMovesTheChild)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateEntityWithScale(AZ::Vector3(1.0f));
            child->SetLocalTranslation(AZ::Vector3(3.0f, 0.0f, 0.0f));

            // The world a child cached under its old parent is applied again when the new parent activates, which is how a
            // parent change that arrives from outside (slice propagation) keeps its place. It is cleared so that this test sees
            // the relative path alone.
            child->ClearCachedWorldTransform();
            child->SetParentRelative(parent->GetEntityId());

            // The authored local is what it was, and the world is recomputed from it: 10 - 3 = 7 where it stood at 3.
            EXPECT_THAT(child->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
            EXPECT_THAT(child->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_FALSE(ReadHeaderTag(child).empty());
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: notifications and buses on a descendant (D.49, D.52).

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_NotificationsFollowTheParentIntoAndOutOfExtended)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            Transform2Recorder recorder(child->GetEntityId());

            // The parent becomes extended: the child is activated as a Transform 2 and then told its world. It carries the
            // effective signed world scale, which is the flip of the parent.
            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "AW");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

            // The parent moves: the child hears of its world alone.
            recorder.m_log.clear();
            parent->SetWorldTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "W");

            // The parent flips on another axis: the world scale of the child changed, so the scale and then the world.
            recorder.m_log.clear();
            SetAuthoredScale(parent, AZ::Vector3(1.0f, 1.0f, -1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "WS");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(1.0f, 1.0f, -1.0f));

            // The parent goes back to a plain scale: the child gets the plain scale and lets go.
            recorder.m_log.clear();
            SetAuthoredScale(parent, AZ::Vector3(1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "SD");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(1.0f));

            // And it is quiet again.
            recorder.m_log.clear();
            parent->SetWorldTranslation(AZ::Vector3(4.0f, 5.0f, 6.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "");
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_ScaleChangedUsesTheRelativeThreshold)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            Transform2Recorder recorder(transform->GetEntityId());

            // A change of one part in a million is below the 1e-5 that counts as a change of the scale: the world only.
            SetAuthoredScale(transform, AZ::Vector3(-1.000001f, 1.0f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "W");

            // A change of five parts in a hundred thousand is above it.
            recorder.m_log.clear();
            SetAuthoredScale(transform, AZ::Vector3(-1.00005f, 1.0f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "WS");
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: what was last announced is what a change is measured from (D.49, contract 15.5).
        //
        // A scale that creeps by less than the threshold each step adds up until it passes it, and only then is announced.
        // The residual event comes with a scale change and never alone, because a mesh overwrites its signed scale with the
        // positive residual on the event, and it is the scale notification after it that puts the flip back.

        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_SlowScaleRamp_StepsUnderTheThresholdAddUpUntilTheyAreAnnounced)
        {
            // X is the flip and the largest axis, so the residual is (1, y, 1) and a step of y moves the residual by the same amount.
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 0.5f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            Transform2Recorder recorder(entityId);
            int residualChanges = 0;
            AZ::NonUniformScaleChangedEvent::Handler residualHandler([&residualChanges](const AZ::Vector3&) { ++residualChanges; });
            AZ::NonUniformScaleRequestBus::Event(entityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, residualHandler);

            // The threshold is 1e-5 of the magnitude, which at 0.5 is 5e-6. A step of 4e-6 is under it: only the world.
            SetAuthoredScale(transform, AZ::Vector3(-1.0f, 0.5f + 4.0e-6f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "W");
            EXPECT_EQ(residualChanges, 0);

            // The second step is 4e-6 from the first and 8e-6 from what was announced, which is over it. The scale is announced.
            // The residual moved by 8e-6 from what was announced, which is under its own tolerance of 1e-5.
            SetAuthoredScale(transform, AZ::Vector3(-1.0f, 0.5f + 8.0e-6f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "WWS");
            EXPECT_EQ(residualChanges, 0);

            // Over many steps, every residual event comes with a scale change, and both come.
            for (int step = 3; step <= 100; ++step)
            {
                const int residualChangesBefore = residualChanges;
                const int scaleChangesBefore = CountOf(recorder.m_log, 'S');

                SetAuthoredScale(transform, AZ::Vector3(-1.0f, 0.5f + 4.0e-6f * static_cast<float>(step), 1.0f));

                if (residualChanges != residualChangesBefore)
                {
                    EXPECT_EQ(CountOf(recorder.m_log, 'S'), scaleChangesBefore + 1) << "step " << step;
                }
            }
            EXPECT_GT(residualChanges, 0);
            EXPECT_GT(CountOf(recorder.m_log, 'S'), 1);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_ResidualEventComesOnlyWithAChangeOfTheResidual)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            int residualChanges = 0;
            AZ::Vector3 lastResidual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleChangedEvent::Handler residualHandler(
                [&residualChanges, &lastResidual](const AZ::Vector3& residual)
                {
                    ++residualChanges;
                    lastResidual = residual;
                });
            AZ::NonUniformScaleRequestBus::Event(entityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, residualHandler);

            // Twice the size in the same shape: the residual (1, 0.5, 0.5) is the one it was.
            SetAuthoredScale(transform, AZ::Vector3(4.0f, 2.0f, 2.0f));
            EXPECT_EQ(residualChanges, 0);

            // A different shape at the same size: (4,1,1) over its stand-in of 4 is (1, 0.25, 0.25), and it is heard once.
            SetAuthoredScale(transform, AZ::Vector3(4.0f, 1.0f, 1.0f));
            EXPECT_EQ(residualChanges, 1);
            EXPECT_THAT(lastResidual, UnitTest::IsClose(AZ::Vector3(1.0f, 0.25f, 0.25f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ResidualEventOfADescendantFollowsTheShapeOfItsParent)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            const AZ::EntityId childId = child->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
            int residualChanges = 0;
            AZ::Vector3 lastResidual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleChangedEvent::Handler residualHandler(
                [&residualChanges, &lastResidual](const AZ::Vector3& residual)
                {
                    ++residualChanges;
                    lastResidual = residual;
                });
            AZ::NonUniformScaleRequestBus::Event(childId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, residualHandler);

            // The child has a scale of one. Its world scale is its parent's, so the parent sets what it hears.
            SetAuthoredScale(parent, AZ::Vector3(4.0f, 2.0f, 2.0f));
            EXPECT_EQ(residualChanges, 0);

            SetAuthoredScale(parent, AZ::Vector3(4.0f, 1.0f, 1.0f));
            EXPECT_EQ(residualChanges, 1);
            EXPECT_THAT(lastResidual, UnitTest::IsClose(AZ::Vector3(1.0f, 0.25f, 0.25f)));
        }

        // Stopping is always announced, and the residual with it when it changed: the listeners of a transform that stops serving let go
        // of a residual of one.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_TheResidualIsAnnouncedOnceMoreWhenTheTransformStopsServing)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            int residualChanges = 0;
            AZ::Vector3 lastResidual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleChangedEvent::Handler residualHandler(
                [&residualChanges, &lastResidual](const AZ::Vector3& residual)
                {
                    ++residualChanges;
                    lastResidual = residual;
                });
            AZ::NonUniformScaleRequestBus::Event(entityId, &AZ::NonUniformScaleRequests::RegisterScaleChangedEvent, residualHandler);

            SetAuthoredScale(transform, AZ::Vector3(1.0f));

            EXPECT_EQ(residualChanges, 1);
            EXPECT_THAT(lastResidual, UnitTest::IsClose(AZ::Vector3(1.0f)));
        }

        // The Transform 1 announcements of a transform that serves carry the best fit of its chain, and they come before the
        // Transform 2 ones, so whoever listens to both finds this entity as the Transform 1 ones left it.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_TheTransform1AnnouncementsCarryTheBestFitAndComeBeforeTheTransform2Ones)
        {
            // The child is turned by 0.5 about Z. Under a parent that mirrors X the view turns the other way, so the rotation tells the
            // best fit from the product of parent world and local as well as the position does.
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            child->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, 0.5f));
            TransformEventRecorder recorder(child->GetEntityId());

            // The parent moves to x = 12. The child stays 3 to its left: 12 - 3 = 9. The Transform 1 formula would say 12 + 3 = 15.
            parent->SetWorldTranslation(AZ::Vector3(12.0f, 0.0f, 0.0f));

            // Both Transform 1 announcements, then the world of Transform 2. The scale of the child did not change.
            EXPECT_STREQ(recorder.m_log.c_str(), "TEW");
            const AZ::Quaternion viewRotation = AZ::Quaternion::CreateRotationZ(-0.5f);
            EXPECT_THAT(recorder.m_notificationWorld.GetTranslation(), UnitTest::IsClose(AZ::Vector3(9.0f, 0.0f, 0.0f)));
            EXPECT_THAT(recorder.m_notificationWorld.GetRotation(), UnitTest::IsClose(viewRotation));
            EXPECT_THAT(recorder.m_eventWorld.GetTranslation(), UnitTest::IsClose(AZ::Vector3(9.0f, 0.0f, 0.0f)));
            EXPECT_THAT(recorder.m_eventWorld.GetRotation(), UnitTest::IsClose(viewRotation));

            // The local is the authored one, turned by +0.5.
            const AZ::Quaternion authoredRotation = AZ::Quaternion::CreateRotationZ(0.5f);
            EXPECT_THAT(recorder.m_notificationLocal.GetTranslation(), UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
            EXPECT_THAT(recorder.m_notificationLocal.GetRotation(), UnitTest::IsClose(authoredRotation));
            EXPECT_THAT(recorder.m_eventLocal.GetTranslation(), UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
            EXPECT_THAT(recorder.m_eventLocal.GetRotation(), UnitTest::IsClose(authoredRotation));

            // A flip of another axis of the parent moves nothing of the view, and the Transform 1 side is told all the same, as it is
            // told of every change of the parent. The world of Transform 2 follows them, and its scale comes last.
            recorder.m_log.clear();
            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, -1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "TEWS");
        }

        // A transform that is not active announces nothing, and gives up the buses it served.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Notifications_ADeactivatedTransformServesNothingAndAnnouncesNothing)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            Transform2Recorder recorder(entityId);

            transform->GetEntity()->Deactivate();
            EXPECT_STREQ(recorder.m_log.c_str(), "D");
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));

            // A write while it is inactive (a reload of the entity, an edit of its data): the data changes, and nobody is told.
            recorder.m_log.clear();
            SetAuthoredScale(transform, AZ::Vector3(-2.0f, 1.0f, 1.0f));
            EXPECT_STREQ(recorder.m_log.c_str(), "");
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(entityId));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(entityId));

            // Activating it announces what the data is now.
            transform->GetEntity()->Activate();
            EXPECT_STREQ(recorder.m_log.c_str(), "AW");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(-2.0f, 1.0f, 1.0f));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: the Transform 2 request surface of an editor transform.

        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_SetScaleOnADescendantTakesTheFactorEachWorldAxisNeeds)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(2.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            const AZ::EntityId childId = child->GetEntityId();
            ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));

            // The world scale of the child is (2,1,1). A residual of one on every axis asks for equal magnitudes, which at its stand-in
            // of 2 is (2,2,2). The own scale takes the factor each world axis needs for that: 2/2, 2/1 and 2/1.
            AZ::NonUniformScaleRequestBus::Event(childId, &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3(1.0f));

            EXPECT_THAT(child->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(1.0f, 2.0f, 2.0f)));
            AZ::Vector3 worldScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(worldScale, childId, &AZ::Transform2Requests::GetWorldScale);
            EXPECT_THAT(worldScale, UnitTest::IsClose(AZ::Vector3(2.0f, 2.0f, 2.0f)));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_ANonFiniteLocalScaleIsRefusedWithAWarning)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-2.0f, 1.0f, 1.0f));
            const EditorTransform before = transform->GetLocalEditorTransform();

            // The trace suppression of the test runner counts errors and asserts, and not warnings.
            struct WarningCounter
                : public AZ::Debug::TraceMessageBus::Handler
            {
                bool OnPreWarning(const char*, const char*, int, const char*, const char*) override
                {
                    ++m_count;
                    return true;
                }

                int m_count = 0;
            };
            WarningCounter warnings;
            warnings.AZ::Debug::TraceMessageBus::Handler::BusConnect();

            AZ::Transform2RequestBus::Event(
                transform->GetEntityId(), &AZ::Transform2Requests::SetLocalScale,
                AZ::Vector3(AZStd::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f));

            warnings.AZ::Debug::TraceMessageBus::Handler::BusDisconnect();
#if defined(AZ_ENABLE_TRACING)
            EXPECT_EQ(warnings.m_count, 1);
#endif

            EXPECT_EQ(transform->GetLocalEditorTransform().m_authoredScale, before.m_authoredScale);
            EXPECT_FLOAT_EQ(transform->GetLocalEditorTransform().m_uniformScale, before.m_uniformScale);
            EXPECT_TRUE(transform->GetWorldTM().IsFinite());
        }

        // The data changes behind the transform, without the notification that a write makes. Asked to read its parent again, the
        // transform finds the chain it should have, announces what changed once, and a second ask finds nothing to announce.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Buses_RefreshFromParentReadsTheChainAgainAndAnnouncesOnlyWhatChanged)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            const AZ::EntityId entityId = transform->GetEntityId();
            Transform2Recorder recorder(entityId);

            EditorTransformInPlace(transform).m_authoredScale = AZ::Vector3(1.0f, -1.0f, 1.0f);
            EXPECT_STREQ(recorder.m_log.c_str(), "");

            AZ::Transform2RequestBus::Event(entityId, &AZ::Transform2Requests::RefreshFromParent);

            EXPECT_STREQ(recorder.m_log.c_str(), "WS");
            EXPECT_EQ(recorder.m_lastScale, AZ::Vector3(1.0f, -1.0f, 1.0f));
            AZ::Vector3 worldScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(worldScale, entityId, &AZ::Transform2Requests::GetWorldScale);
            EXPECT_EQ(worldScale, AZ::Vector3(1.0f, -1.0f, 1.0f));

            recorder.m_log.clear();
            AZ::Transform2RequestBus::Event(entityId, &AZ::Transform2Requests::RefreshFromParent);
            EXPECT_STREQ(recorder.m_log.c_str(), "");
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_AFlippedAndTurnedChainIsNotSheared)
        {
            TransformComponent* parent = CreateFlippedParent();
            parent->SetLocalRotation(AZ::Vector3(0.0f, 0.0f, 0.5f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 1.0f, 0.0f));
            child->SetLocalRotation(AZ::Vector3(0.0f, 0.3f, 0.0f));

            // The columns of a mirrored and turned chain are still at right angles, so the Transform 1 view and the flip say it all.
            for (const AZ::EntityId entityId : { parent->GetEntityId(), child->GetEntityId() })
            {
                bool sheared = true;
                AZ::Transform2RequestBus::EventResult(sheared, entityId, &AZ::Transform2Requests::IsWorldSheared);
                EXPECT_FALSE(sheared);
            }
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_BusesOfADescendantFollowTheEffectiveWorldScale)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* sibling = CreateEntityWithScale(AZ::Vector3(1.0f));
            const AZ::EntityId childId = child->GetEntityId();
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(childId));

            // A flip: the child serves the request bus, with its own scale and the one it inherits. A flip has equal magnitudes,
            // so there is no residual to serve.
            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(childId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
            AZ::Vector3 localScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(localScale, childId, &AZ::Transform2Requests::GetLocalScale);
            EXPECT_EQ(localScale, AZ::Vector3(1.0f));
            AZ::Vector3 worldScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(worldScale, childId, &AZ::Transform2Requests::GetWorldScale);
            EXPECT_EQ(worldScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));

            // Non-uniform: the world scale of the child is (2,1,1) although its own scale is 1, so it serves the residual too.
            // The residual is the positive part on top of the float: |s| over the largest, (1,0.5,0.5).
            SetAuthoredScale(parent, AZ::Vector3(2.0f, 1.0f, 1.0f));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(childId));
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
            AZ::Vector3 residual = AZ::Vector3::CreateZero();
            AZ::NonUniformScaleRequestBus::EventResult(residual, childId, &AZ::NonUniformScaleRequests::GetScale);
            EXPECT_THAT(residual, UnitTest::IsClose(AZ::Vector3(1.0f, 0.5f, 0.5f)));
            AZ::Transform2RequestBus::EventResult(localScale, childId, &AZ::Transform2Requests::GetLocalScale);
            EXPECT_EQ(localScale, AZ::Vector3(1.0f));
            AZ::Transform2RequestBus::EventResult(worldScale, childId, &AZ::Transform2Requests::GetWorldScale);
            EXPECT_THAT(worldScale, UnitTest::IsClose(AZ::Vector3(2.0f, 1.0f, 1.0f)));

            // A plain root that is not below the parent serves nothing at any point.
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(sibling->GetEntityId()));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(sibling->GetEntityId()));

            // The parent lets go, and so does the child.
            SetAuthoredScale(parent, AZ::Vector3(1.0f));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(childId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_HasNonUniformScaleAnswersFromTheInheritedState)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3::CreateZero());
            TransformComponent* unrelated = CreateEntityWithScale(AZ::Vector3(1.0f));
            AZ::Entity& childEntity = *child->GetEntity();
            EXPECT_FALSE(TransformComponent::HasNonUniformScale(childEntity));

            // A flip has equal magnitudes, so it is not non-uniform.
            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FALSE(TransformComponent::HasNonUniformScale(childEntity));

            // The colliders of the child ask this at export, and a child of a stretched parent is stretched.
            SetAuthoredScale(parent, AZ::Vector3(2.0f, 1.0f, 1.0f));
            EXPECT_TRUE(TransformComponent::HasNonUniformScale(*parent->GetEntity()));
            EXPECT_TRUE(TransformComponent::HasNonUniformScale(childEntity));
            EXPECT_FALSE(TransformComponent::HasNonUniformScale(*unrelated->GetEntity()));

            SetAuthoredScale(parent, AZ::Vector3(1.0f));
            EXPECT_FALSE(TransformComponent::HasNonUniformScale(childEntity));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_HasExtendedOwnScaleLooksAtTheEntityAlone)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* plain = CreateChildOf(parent, AZ::Vector3::CreateZero());
            TransformComponent* forced = CreateChildOf(parent, AZ::Vector3::CreateZero());
            SetDeployExtended(forced, true);
            TransformComponent* unrelated = CreateEntityWithScale(AZ::Vector3(1.0f));

            // This is what decides whether an export initializes every entity first. An inherited state does not count, because
            // the parents are found through the entities that are initialized and this question is asked before that.
            EXPECT_TRUE(TransformComponent::HasExtendedOwnScale(*parent->GetEntity()));
            EXPECT_FALSE(TransformComponent::HasExtendedOwnScale(*plain->GetEntity()));
            EXPECT_TRUE(TransformComponent::HasExtendedOwnScale(*forced->GetEntity()));
            EXPECT_FALSE(TransformComponent::HasExtendedOwnScale(*unrelated->GetEntity()));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: a crossing between uniform and non-uniform scale (D.32, D.45).

        // Records, for every activation and deactivation the entity system announces for one entity, whether an undo batch is open.
        class UndoBatchProbe
            : public AZ::EntitySystemBus::Handler
        {
        public:
            explicit UndoBatchProbe(AZ::EntityId entityId)
                : m_entityId(entityId)
            {
                AZ::EntitySystemBus::Handler::BusConnect();
            }

            ~UndoBatchProbe() override
            {
                AZ::EntitySystemBus::Handler::BusDisconnect();
            }

            void OnEntityActivated(const AZ::EntityId& entityId) override
            {
                Record(entityId);
            }

            void OnEntityDeactivated(const AZ::EntityId& entityId) override
            {
                Record(entityId);
            }

            int m_announced = 0; //!< Activations and deactivations of the entity announced.
            int m_inBatch = 0; //!< Of those, the ones that happened with an undo batch open.

        private:
            void Record(AZ::EntityId entityId)
            {
                if (entityId == m_entityId)
                {
                    UndoSystem::URSequencePoint* batch = nullptr;
                    ToolsApplicationRequests::Bus::BroadcastResult(batch, &ToolsApplicationRequests::GetCurrentUndoBatch);
                    ++m_announced;
                    m_inBatch += batch ? 1 : 0;
                }
            }

            AZ::EntityId m_entityId;
        };

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ACrossingReactivatesDescendantsInTheUndoStepOfTheEdit)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            const AZ::EntityId parentId = parent->GetEntityId();
            const AZ::EntityId childId = child->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            UndoBatchProbe parentProbe(parentId);
            UndoBatchProbe childProbe(childId);

            // The scale of the parent goes from a flip to a stretch, as an edit in the inspector, which is one undo step. The
            // child has a scale of its own of 1, so it did not cross by an edit of its own.
            {
                ScopedUndoBatch undoBatch("Edit scale");
                SetAuthoredScale(parent, AZ::Vector3(-2.0f, 1.0f, 1.0f));
            }
            AZ::TickBus::ExecuteQueuedEvents();

            // Both cycled once, a deactivation and an activation each, so that the colliders on them read the new bus presence.
            // Both rejoined the undo step of the edit. A collider on the child saves a value it derives at activation, and the
            // next move of the parent marks the child dirty and compares it with its prefab, so it would write that value in a
            // step of its own if it were left out here. The transform of the child is not part of what changes.
            EXPECT_EQ(parentProbe.m_announced, 2);
            EXPECT_EQ(parentProbe.m_inBatch, 2);
            EXPECT_EQ(childProbe.m_announced, 2);
            EXPECT_EQ(childProbe.m_inBatch, 2);
            EXPECT_EQ(child->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(parentId));
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
            EXPECT_EQ(child->GetEntity()->GetState(), AZ::Entity::State::Active);

            // The step that was joined is closed again, so the next edit starts a step of its own.
            UndoSystem::URSequencePoint* openBatch = nullptr;
            ToolsApplicationRequests::Bus::BroadcastResult(openBatch, &ToolsApplicationRequests::GetCurrentUndoBatch);
            EXPECT_EQ(openBatch, nullptr);
        }

        // A parent that crosses in two ticks, back and forth, tells its descendants each time. Each descendant clears the flag of
        // its own request when its cycle runs, so the second crossing is not held back by the first.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ASecondCrossingOfTheParentInALaterTickReactivatesItsDescendantsAgain)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            const AZ::EntityId parentId = parent->GetEntityId();
            const AZ::EntityId childId = child->GetEntityId();
            AZ::TickBus::ExecuteQueuedEvents();
            Transform2Recorder parentRecorder(parentId);
            Transform2Recorder childRecorder(childId);

            SetAuthoredScale(parent, AZ::Vector3(-2.0f, 1.0f, 1.0f));
            parentRecorder.m_log.clear();
            childRecorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();
            EXPECT_STREQ(parentRecorder.m_log.c_str(), "DAW");
            EXPECT_STREQ(childRecorder.m_log.c_str(), "DAW");
            EXPECT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));

            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
            parentRecorder.m_log.clear();
            childRecorder.m_log.clear();
            AZ::TickBus::ExecuteQueuedEvents();
            EXPECT_STREQ(parentRecorder.m_log.c_str(), "DAW");
            EXPECT_STREQ(childRecorder.m_log.c_str(), "DAW");
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(parentId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_AFlipAloneReactivatesNobody)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            AZ::TickBus::ExecuteQueuedEvents();
            UndoBatchProbe parentProbe(parent->GetEntityId());
            UndoBatchProbe childProbe(child->GetEntityId());

            {
                ScopedUndoBatch undoBatch("Edit scale");
                SetAuthoredScale(parent, AZ::Vector3(1.0f, 1.0f, -1.0f));
            }
            AZ::TickBus::ExecuteQueuedEvents();

            // A flip never changes which buses are served, so there is nothing for a collider to read again.
            EXPECT_EQ(parentProbe.m_announced, 0);
            EXPECT_EQ(childProbe.m_announced, 0);
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(child->GetEntityId()));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: the legacy Non-uniform Scale component and the tag on a descendant.

        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_ALegacyComponentHidesTheTagWhateverTheParentDoes)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* transform = CreateChildOf(parent, AZ::Vector3::CreateZero());
            AddLegacyScaleComponent(transform, AZ::Vector3(2.0f, 1.0f, 1.0f));
            SetAuthoredScale(transform, AZ::Vector3(-1.0f, 1.0f, 1.0f));

            // The scale is extended and the legacy component owns it. Nothing serves, so there is nothing to tag.
            EXPECT_TRUE(transform->GetLocalEditorTransform().IsExtended());
            EXPECT_TRUE(ReadHeaderTag(transform).empty());

            // A parent that serves changes nothing: the legacy component and the transform would both claim the single
            // handler of the non-uniform scale bus. The entity acts on the best fit of its parent, as a Transform 1 does.
            SetAuthoredScale(parent, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FALSE(ReadHeaderTag(parent).empty());
            EXPECT_TRUE(ReadHeaderTag(transform).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));

            // The same for an entity with a plain scale of its own.
            TransformComponent* plain = CreateChildOf(parent, AZ::Vector3::CreateZero());
            EXPECT_FALSE(ReadHeaderTag(plain).empty());
            AddLegacyScaleComponent(plain, AZ::Vector3(2.0f, 1.0f, 1.0f));
            EXPECT_TRUE(ReadHeaderTag(plain).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(plain->GetEntityId()));

            AZ::Entity gameEntity;
            plain->BuildGameEntity(&gameEntity);
            EXPECT_NE(gameEntity.FindComponent<AzFramework::TransformComponent>(), nullptr);
            EXPECT_EQ(gameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_AddingTheLegacyComponentToAServingParentStopsItsChildren)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* grandchild = CreateChildOf(child, AZ::Vector3(1.0f, 0.0f, 0.0f));
            EXPECT_FALSE(ReadHeaderTag(child).empty());
            EXPECT_FALSE(ReadHeaderTag(grandchild).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(child->GetEntityId()));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(grandchild->GetEntityId()));

            // The component joins while the parent is inactive and the parent activates again with its data as it was. No change
            // of the data is announced, yet the parent no longer serves, and the children are told of it.
            AddLegacyScaleComponent(parent, AZ::Vector3(2.0f, 1.0f, 1.0f));

            EXPECT_TRUE(ReadHeaderTag(parent).empty());
            EXPECT_TRUE(ReadHeaderTag(child).empty());
            EXPECT_TRUE(ReadHeaderTag(grandchild).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(child->GetEntityId()));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(grandchild->GetEntityId()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_ConvertingTheLegacyComponentOfAParentMakesItsChildrenServe)
        {
            TransformComponent* parent = CreateEntityWithScale(AZ::Vector3(-1.0f, 1.0f, 1.0f));
            AddLegacyScaleComponent(parent, AZ::Vector3(2.0f, 1.0f, 1.0f));
            TransformComponent* child = CreateChildOf(parent, AZ::Vector3(3.0f, 0.0f, 0.0f));
            TransformComponent* grandchild = CreateChildOf(child, AZ::Vector3(1.0f, 0.0f, 0.0f));

            // The old component owns the scale of the parent, so nothing serves down the chain.
            EXPECT_TRUE(ReadHeaderTag(parent).empty());
            EXPECT_TRUE(ReadHeaderTag(child).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(child->GetEntityId()));

            TransformComponent::QueueLegacyScaleConversion(parent->GetEntityId());
            AZ::TickBus::ExecuteQueuedEvents();

            // The removal of the old component re-activates the parent with its data as the conversion left it, and the parent
            // serves from then on. The children are told of that, and the live state agrees with what an export gives.
            EXPECT_FALSE(ReadHeaderTag(parent).empty());
            EXPECT_FALSE(ReadHeaderTag(child).empty());
            EXPECT_FALSE(ReadHeaderTag(grandchild).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(child->GetEntityId()));
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(grandchild->GetEntityId()));

            AZ::Entity gameEntity;
            child->BuildGameEntity(&gameEntity);
            EXPECT_NE(gameEntity.FindComponent<AzFramework::Transform2Component>(), nullptr);
        }

        // An entity that the old component owns acts on the best fit of its parent, so a reparent of it is the Transform 1 one. A
        // reparent solved against the chain of the flipped parent, which the entity does not follow, would put it 6 away.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_AnEntityWithTheLegacyComponentReparentsAsATransform1)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* legacy = CreateEntityWithScale(AZ::Vector3(1.0f));
            legacy->SetWorldTranslation(AZ::Vector3(7.0f, 0.0f, 0.0f));
            AddLegacyScaleComponent(legacy, AZ::Vector3(2.0f, 1.0f, 1.0f));
            const AZ::EntityId legacyId = legacy->GetEntityId();

            // The world it cached at its last activation is applied again when the new parent activates, and would put the local right
            // whatever the reparent did to it. It is cleared so that this test sees the reparent alone.
            legacy->ClearCachedWorldTransform();
            legacy->SetParent(parent->GetEntityId());

            // The parent is at x = 10 with a scale of one in the view, so the place is kept by a local of 7 - 10 = -3.
            EXPECT_THAT(legacy->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(legacy->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(-3.0f, 0.0f, 0.0f)));
            EXPECT_TRUE(ReadHeaderTag(legacy).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(legacyId));

            // And out again: the local becomes the place, 10 + (-3) = 7.
            legacy->SetParent(AZ::EntityId());

            EXPECT_THAT(legacy->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(legacy->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(legacyId));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Load_OntoALiveTransformAnnouncesTheNewServingState)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateChildOf(transform, AZ::Vector3::CreateZero());
            EXPECT_TRUE(ReadHeaderTag(transform).empty());
            EXPECT_TRUE(ReadHeaderTag(child).empty());

            // A script updating the component in place does not activate it again.
            rapidjson::Document document;
            document.Parse(R"({ "Transform Data": { "AuthoredScale": [-1.0, 1.0, 1.0], "UniformScale": 1.0 } })");
            AZ::JsonSerialization::Load(*transform, document);

            EXPECT_FALSE(ReadHeaderTag(transform).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));
            EXPECT_FALSE(ReadHeaderTag(child).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(child->GetEntityId()));

            // And back to a plain scale.
            document.Parse(R"({ "Transform Data": { "UniformScale": 1.0 } })");
            AZ::JsonSerialization::Load(*transform, document);

            EXPECT_TRUE(ReadHeaderTag(transform).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(transform->GetEntityId()));
            EXPECT_TRUE(ReadHeaderTag(child).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(child->GetEntityId()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Hierarchy_APlainChildDoesNotReadItsMemoOnAMoveYetFollowsItsAncestor)
        {
            TransformComponent* root = CreateEntityWithScale(AZ::Vector3(1.0f));
            TransformComponent* child = CreateChildOf(root, AZ::Vector3(1.0f, 0.0f, 0.0f));
            TransformComponent* grandchild = CreateChildOf(child, AZ::Vector3(1.0f, 0.0f, 0.0f));

            // Moves along a chain with no extended transform announce nothing of Transform 2. A plain child does not read its
            // memo on a move, so when an ancestor starts or stops serving it is the ancestor that marks that memo dirty, and
            // the change still reaches the end of the chain.
            root->SetLocalTranslation(AZ::Vector3(2.0f, 0.0f, 0.0f));
            child->SetLocalTranslation(AZ::Vector3(3.0f, 0.0f, 0.0f));
            EXPECT_TRUE(ReadHeaderTag(grandchild).empty());
            EXPECT_THAT(grandchild->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3(6.0f, 0.0f, 0.0f)));

            SetAuthoredScale(root, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FALSE(ReadHeaderTag(child).empty());
            EXPECT_FALSE(ReadHeaderTag(grandchild).empty());
            EXPECT_TRUE(AZ::Transform2RequestBus::HasHandlers(grandchild->GetEntityId()));

            SetAuthoredScale(root, AZ::Vector3(1.0f));
            EXPECT_TRUE(ReadHeaderTag(child).empty());
            EXPECT_TRUE(ReadHeaderTag(grandchild).empty());
            EXPECT_FALSE(AZ::Transform2RequestBus::HasHandlers(grandchild->GetEntityId()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_HeaderTag_ToolTipNamesTheParentAndTheRuntimeLimit)
        {
            TransformComponent* transform = CreateEntityWithScale(AZ::Vector3(1.0f));

            const AZStd::string toolTip = ReadHeaderTagToolTip(transform);

            // A child follows the flip of its parent since I.4, so the old limit is gone.
            EXPECT_EQ(toolTip.find("Children do not follow"), AZStd::string::npos);
            // The first line still says that a parent can make this entity extended.
            EXPECT_NE(toolTip.find("this entity, or a parent of it"), AZStd::string::npos);
            // A child attached while the game runs is the case that still needs the option.
            EXPECT_NE(toolTip.find("attached while the game runs"), AZStd::string::npos);
            EXPECT_NE(toolTip.find("Deploy as Extended"), AZStd::string::npos);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: paste onto a descendant.

        TEST_F(EditorTransformComponentTest, ScaleSplit_Paste_Transform1SourceOntoAChildOfAFlippedParentFollowsTheWorldFloat)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* source = CreateEntityWithScale(AZ::Vector3(3.0f));
            TransformComponent* destination = CreateChildOf(parent, AZ::Vector3::CreateZero());

            TransformComponent::PasteOverComponent(source, destination);

            // The world float is the 3 of the source, written by ratio into the own scale, which stays a plain scale. The world
            // position is the one of the source (0, 0, 0): 10 - t = 0.
            EXPECT_EQ(destination->GetLocalEditorTransform().m_authoredScale, AZ::Vector3(3.0f));
            EXPECT_TRUE(destination->GetLocalEditorTransform().IsTransform1Scale());
            EXPECT_FLOAT_EQ(destination->GetWorldTM().GetUniformScale(), 3.0f);
            EXPECT_THAT(destination->GetLocalEditorTransform().m_translate, UnitTest::IsClose(AZ::Vector3(10.0f, 0.0f, 0.0f)));
            EXPECT_THAT(destination->GetWorldTranslation(), UnitTest::IsClose(AZ::Vector3::CreateZero()));
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Paste_Transform2SourceOntoAChildOfAFlippedParentKeepsTheWorldFloat)
        {
            TransformComponent* parent = CreateFlippedParent();
            TransformComponent* source = CreateEntityWithScale(AZ::Vector3(-2.0f, 2.0f, 2.0f));
            TransformComponent* destination = CreateChildOf(parent, AZ::Vector3::CreateZero());

            TransformComponent::PasteOverComponent(source, destination);

            // The vector of the source is carried to the destination as its own scale. Two flips on X cancel, so the destination
            // is drawn at (2,2,2), and the float of its world is the 2 of the source.
            EXPECT_THAT(destination->GetLocalEditorTransform().m_authoredScale, UnitTest::IsClose(AZ::Vector3(-2.0f, 2.0f, 2.0f)));
            EXPECT_FLOAT_EQ(destination->GetWorldTM().GetUniformScale(), 2.0f);
            AZ::Vector3 worldScale = AZ::Vector3::CreateZero();
            AZ::Transform2RequestBus::EventResult(worldScale, destination->GetEntityId(), &AZ::Transform2Requests::GetWorldScale);
            EXPECT_THAT(worldScale, UnitTest::IsClose(AZ::Vector3(2.0f, 2.0f, 2.0f)));
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: an editor-only parent that is removed at export (D.48).
        //
        // The handlers work on the runtime entities the export made, which are not initialized. The prefab handler and the slice
        // handler share the rule, so each test runs both. The entities are built as the export builds them: a configuration with
        // a local, and for a Transform 2 its own scale.

        using FixTransformRelationshipsFunction = void (*)(const EntityList&, const EntityIdSet&);

        static const FixTransformRelationshipsFunction FixTransformRelationshipsOfBothHandlers[] = {
            &Prefab::PrefabConversionUtils::WorldEditorOnlyEntityHandler::FixTransformRelationships,
            &WorldEditorOnlyEntityHandler::FixTransformRelationships,
        };

        static AZStd::unique_ptr<AZ::Entity> MakeRuntimeTransform1(
            const char* name, float uniformScale, const AZ::Vector3& localTranslation, AZ::EntityId parentId,
            const AZ::Quaternion& localRotation = AZ::Quaternion::CreateIdentity())
        {
            auto entity = AZStd::make_unique<AZ::Entity>(name);
            AZ::TransformConfig config;
            config.m_parentId = parentId;
            config.m_localTransform = AZ::Transform::CreateFromQuaternionAndTranslation(localRotation, localTranslation);
            config.m_localTransform.SetUniformScale(uniformScale);
            config.m_worldTransform = config.m_localTransform;
            entity->CreateComponent<AzFramework::TransformComponent>()->SetConfiguration(config);
            return entity;
        }

        static AZStd::unique_ptr<AZ::Entity> MakeRuntimeTransform2(
            const char* name, const AZ::Vector3& scale, const AZ::Vector3& localTranslation, AZ::EntityId parentId,
            const AZ::Quaternion& localRotation = AZ::Quaternion::CreateIdentity())
        {
            auto entity = AZStd::make_unique<AZ::Entity>(name);
            AZ::TransformConfig config;
            config.m_parentId = parentId;
            config.m_localTransform = AZ::Transform::CreateFromQuaternionAndTranslation(localRotation, localTranslation);
            config.m_localTransform.SetUniformScale(AZ::Transform2::UniformStandIn(scale));
            config.m_worldTransform = config.m_localTransform;
            entity->CreateComponent<AzFramework::Transform2Component>(scale)->SetConfiguration(config);
            return entity;
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_ARemovedFlippedParentKeepsItsFlipOnTheChild)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                // The removed parent is flipped at x = 10 and its child, a Transform 2 by inheritance, sits at its local x = 3.
                auto removed = MakeRuntimeTransform2("Removed", AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::EntityId());
                auto child = MakeRuntimeTransform2("Child", AZ::Vector3(1.0f), AZ::Vector3(3.0f, 0.0f, 0.0f), removed->GetId());

                fix(EntityList{ removed.get(), child.get() }, EntityIdSet{ removed->GetId() });

                // The child takes the place of its parent: no parent, at 10 - 3 = 7, and the flip of the removed entity is its own.
                const auto* childTransform2 = child->FindComponent<AzFramework::Transform2Component>();
                ASSERT_NE(childTransform2, nullptr);
                EXPECT_EQ(RuntimeLocalScale(*childTransform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
                AZ::TransformConfig config;
                childTransform2->GetConfiguration(config);
                EXPECT_FALSE(config.m_parentId.IsValid());
                EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
                EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), 1.0f);
            }
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_ARemovedPlainParentKeepsTheFlipOfItsChildAndItsMagnitude)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                // A plain parent of scale 2 at x = 10 and a flipped child at its local x = 3: world 10 + 2 * 3 = 16, scale (-2,2,2).
                auto removed = MakeRuntimeTransform1("Removed", 2.0f, AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::EntityId());
                auto child = MakeRuntimeTransform2("Child", AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(3.0f, 0.0f, 0.0f), removed->GetId());

                fix(EntityList{ removed.get(), child.get() }, EntityIdSet{ removed->GetId() });

                const auto* childTransform2 = child->FindComponent<AzFramework::Transform2Component>();
                ASSERT_NE(childTransform2, nullptr);
                EXPECT_EQ(RuntimeLocalScale(*childTransform2), AZ::Vector3(-2.0f, 2.0f, 2.0f));
                AZ::TransformConfig config;
                childTransform2->GetConfiguration(config);
                EXPECT_FALSE(config.m_parentId.IsValid());
                EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(16.0f, 0.0f, 0.0f)));
                EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), 2.0f);
            }
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_APairOfPlainTransformsTakesTheLinesItAlwaysDid)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                auto removed = MakeRuntimeTransform1("Removed", 2.0f, AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::EntityId());
                auto child = MakeRuntimeTransform1("Child", 1.0f, AZ::Vector3(3.0f, 0.0f, 0.0f), removed->GetId());

                fix(EntityList{ removed.get(), child.get() }, EntityIdSet{ removed->GetId() });

                // The local of the child is the local of the removed parent times its own: 10 + 2 * 3 = 16, at scale 2 * 1.
                EXPECT_EQ(child->FindComponent<AzFramework::Transform2Component>(), nullptr);
                const auto* childTransform = child->FindComponent<AzFramework::TransformComponent>();
                ASSERT_NE(childTransform, nullptr);
                AZ::TransformConfig config;
                childTransform->GetConfiguration(config);
                EXPECT_FALSE(config.m_parentId.IsValid());
                EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(16.0f, 0.0f, 0.0f)));
                EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), 2.0f);
            }
        }

        // The rotations of the removed parent and of its child: the child leaves with the rotation of its chain, which is the parent's
        // times the child's turned by the signs of the parent, and not with the rotation it was authored with.
        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_ARemovedTurnedAndFlippedParentLeavesItsChildTheRotationOfTheChain)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                // The removed parent is flipped on X, at x = 10, turned 30 degrees about Z. The child is turned 10 degrees, at its local
                // x = 3. The mirror reverses the turn of the child in the frame of the parent: 30 + (-10) = 20 degrees.
                auto removed = MakeRuntimeTransform2(
                    "Removed", AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::EntityId(),
                    AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f)));
                auto child = MakeRuntimeTransform2(
                    "Child", AZ::Vector3(1.0f), AZ::Vector3(3.0f, 0.0f, 0.0f), removed->GetId(),
                    AZ::Quaternion::CreateRotationZ(AZ::DegToRad(10.0f)));

                fix(EntityList{ removed.get(), child.get() }, EntityIdSet{ removed->GetId() });

                const auto* childTransform2 = child->FindComponent<AzFramework::Transform2Component>();
                ASSERT_NE(childTransform2, nullptr);
                EXPECT_EQ(RuntimeLocalScale(*childTransform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
                AZ::TransformConfig config;
                childTransform2->GetConfiguration(config);
                EXPECT_FALSE(config.m_parentId.IsValid());

                // The place is 3 to the left of the parent in the frame of the parent: (10,0,0) + Rz30 * (-3,0,0) = (10 - 3 cos 30, -3 sin 30, 0).
                EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.401924f, -1.5f, 0.0f)));
                EXPECT_THAT(
                    config.m_localTransform.GetRotation(), UnitTest::IsClose(AZ::Quaternion::CreateRotationZ(AZ::DegToRad(20.0f))));
                EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), 1.0f);
            }
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_ARemovedTurnedPlainParentLeavesAFlippedChildTheRotationOfTheChain)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                // The parent is a plain Transform 1 turned 30 degrees, at x = 10. Its child is flipped on X, turned 10 degrees, at local
                // x = 3. A flip of the child itself does not reverse its own turn: 30 + 10 = 40 degrees.
                auto removed = MakeRuntimeTransform1(
                    "Removed", 1.0f, AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::EntityId(), AZ::Quaternion::CreateRotationZ(AZ::DegToRad(30.0f)));
                auto child = MakeRuntimeTransform2(
                    "Child", AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(3.0f, 0.0f, 0.0f), removed->GetId(),
                    AZ::Quaternion::CreateRotationZ(AZ::DegToRad(10.0f)));

                fix(EntityList{ removed.get(), child.get() }, EntityIdSet{ removed->GetId() });

                const auto* childTransform2 = child->FindComponent<AzFramework::Transform2Component>();
                ASSERT_NE(childTransform2, nullptr);
                EXPECT_EQ(RuntimeLocalScale(*childTransform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
                AZ::TransformConfig config;
                childTransform2->GetConfiguration(config);
                EXPECT_FALSE(config.m_parentId.IsValid());

                // (10,0,0) + Rz30 * (3,0,0) = (10 + 3 cos 30, 3 sin 30, 0).
                EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(12.598076f, 1.5f, 0.0f)));
                EXPECT_THAT(
                    config.m_localTransform.GetRotation(), UnitTest::IsClose(AZ::Quaternion::CreateRotationZ(AZ::DegToRad(40.0f))));
            }
        }

        // The child takes the place of the removed entity, which includes its parent: the transform of a grandparent stays above it.
        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_ARemovedFlippedParentUnderAGrandparentHandsItsChildToTheGrandparent)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                // The grandparent is plain, at x = 5. The removed parent is flipped, at its local x = 10, and the child is at its local x = 3.
                // The child is 10 - 3 = 7 from the grandparent once the removed parent is gone, and carries the flip.
                auto grandparent = MakeRuntimeTransform1("Grandparent", 1.0f, AZ::Vector3(5.0f, 0.0f, 0.0f), AZ::EntityId());
                auto removed = MakeRuntimeTransform2(
                    "Removed", AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(10.0f, 0.0f, 0.0f), grandparent->GetId());
                auto child = MakeRuntimeTransform2("Child", AZ::Vector3(1.0f), AZ::Vector3(3.0f, 0.0f, 0.0f), removed->GetId());

                fix(EntityList{ grandparent.get(), removed.get(), child.get() }, EntityIdSet{ removed->GetId() });

                const auto* childTransform2 = child->FindComponent<AzFramework::Transform2Component>();
                ASSERT_NE(childTransform2, nullptr);
                EXPECT_EQ(RuntimeLocalScale(*childTransform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
                AZ::TransformConfig config;
                childTransform2->GetConfiguration(config);
                EXPECT_EQ(config.m_parentId, grandparent->GetId());
                EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            }
        }

        // Two removed entities in a row: the child of the second reaches the world, whichever of them the list meets first. The first
        // one that is handled hands its children over to the parent, and that hand-over has to be seen by the one that comes next.
        TEST_F(EditorTransformComponentTest, ScaleSplit_EditorOnly_TwoRemovedParentsInARowLeaveTheChildTheFlipAndTheSumOfBothLocals)
        {
            for (const FixTransformRelationshipsFunction fix : FixTransformRelationshipsOfBothHandlers)
            {
                for (const bool childFirst : { false, true })
                {
                    // The first removed parent is flipped, at x = 10. The second is plain, at its local x = 2. The child is at its local
                    // x = 3 under the second. In the frame of the first it is 2 + 3 = 5 along, and the flip puts it at 10 - 5 = 5.
                    auto removedFirst = MakeRuntimeTransform2("RemovedFirst", AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(10.0f, 0.0f, 0.0f), AZ::EntityId());
                    auto removedSecond = MakeRuntimeTransform2("RemovedSecond", AZ::Vector3(1.0f), AZ::Vector3(2.0f, 0.0f, 0.0f), removedFirst->GetId());
                    auto child = MakeRuntimeTransform2("Child", AZ::Vector3(1.0f), AZ::Vector3(3.0f, 0.0f, 0.0f), removedSecond->GetId());

                    const EntityIdSet removedIds{ removedFirst->GetId(), removedSecond->GetId() };
                    if (childFirst)
                    {
                        fix(EntityList{ child.get(), removedSecond.get(), removedFirst.get() }, removedIds);
                    }
                    else
                    {
                        fix(EntityList{ removedFirst.get(), removedSecond.get(), child.get() }, removedIds);
                    }

                    const auto* childTransform2 = child->FindComponent<AzFramework::Transform2Component>();
                    ASSERT_NE(childTransform2, nullptr);
                    EXPECT_EQ(RuntimeLocalScale(*childTransform2), AZ::Vector3(-1.0f, 1.0f, 1.0f)) << "child first: " << childFirst;
                    AZ::TransformConfig config;
                    childTransform2->GetConfiguration(config);
                    EXPECT_FALSE(config.m_parentId.IsValid()) << "child first: " << childFirst;
                    EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(5.0f, 0.0f, 0.0f)))
                        << "child first: " << childFirst;
                }
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: version 5 of the editor transform (D.56).
        //
        // Version 5 changes no data: cached spawnables are keyed on a fingerprint of the editor types, and the transform now
        // deploys differently for a child of an extended parent. So data of version 4 loads as it is, and older data still runs
        // the steps that it needs.

        TEST_F(EditorTransformComponentTest, ScaleSplit_Version_FourDataLoadsUnchangedAtVersionFive)
        {
            const char editorTransformData[] =
                R"DELIMITER(<ObjectStream version="1">
    <Class name="EditorTransform" field="element" version="4" type="{B02B7063-D238-4F40-A724-405F7A6D68CB}">
        <Class name="Vector3" field="Translate" value="1.0000000 2.0000000 3.0000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="Vector3" field="Rotate" value="0.0000000 0.0000000 45.0000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="Vector3" field="Scale" value="1.0000000 1.0000000 1.0000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="bool" field="Locked" value="false" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
        <Class name="float" field="UniformScale" value="3.0000000" type="{EA2C3E90-AFBE-44D4-A90D-FAAF79BAF93D}"/>
        <Class name="Vector3" field="AuthoredScale" value="-3.0000000 1.0000000 2.0000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="bool" field="DeployExtended" value="true" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
    </Class>
</ObjectStream>)DELIMITER";

            EditorTransform loaded;
            ASSERT_TRUE(AZ::Utils::LoadObjectFromBufferInPlace(editorTransformData, sizeof(editorTransformData), loaded));

            // Every value is the one in the data, the vector and the option included.
            EXPECT_EQ(loaded.m_translate, AZ::Vector3(1.0f, 2.0f, 3.0f));
            EXPECT_EQ(loaded.m_rotate, AZ::Vector3(0.0f, 0.0f, 45.0f));
            EXPECT_EQ(loaded.m_authoredScale, AZ::Vector3(-3.0f, 1.0f, 2.0f));
            EXPECT_FLOAT_EQ(loaded.m_uniformScale, 3.0f);
            EXPECT_TRUE(loaded.m_deployExtended);
            EXPECT_FALSE(loaded.m_locked);
            EXPECT_TRUE(loaded.IsExtended());
        }

        TEST_F(EditorTransformComponentTest, ScaleSplit_Version_ThreeDataStillGetsItsAuthoredScaleAtVersionFive)
        {
            // Version 3 has the uniform scale and not the authored one. The authored scale starts out as the uniform scale.
            const char editorTransformData[] =
                R"DELIMITER(<ObjectStream version="1">
    <Class name="EditorTransform" field="element" version="3" type="{B02B7063-D238-4F40-A724-405F7A6D68CB}">
        <Class name="Vector3" field="Translate" value="1.0000000 2.0000000 3.0000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="Vector3" field="Rotate" value="0.0000000 0.0000000 0.0000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="Vector3" field="Scale" value="2.5000000 2.5000000 2.5000000" type="{8379EB7D-01FA-4538-B64B-A6543B4BE73D}"/>
        <Class name="bool" field="Locked" value="false" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
        <Class name="float" field="UniformScale" value="2.5000000" type="{EA2C3E90-AFBE-44D4-A90D-FAAF79BAF93D}"/>
    </Class>
</ObjectStream>)DELIMITER";

            EditorTransform loaded;
            ASSERT_TRUE(AZ::Utils::LoadObjectFromBufferInPlace(editorTransformData, sizeof(editorTransformData), loaded));

            EXPECT_EQ(loaded.m_translate, AZ::Vector3(1.0f, 2.0f, 3.0f));
            EXPECT_EQ(loaded.m_authoredScale, AZ::Vector3(2.5f));
            EXPECT_FLOAT_EQ(loaded.m_uniformScale, 2.5f);
            EXPECT_FALSE(loaded.m_deployExtended);
            EXPECT_TRUE(loaded.IsTransform1Scale());
        }

        // The two tests above load the same values at version 4 or at version 5, so neither tells the two apart. The version is the
        // part of the fingerprint of the editor types that makes a cached spawnable export again.
        TEST_F(EditorTransformComponentTest, ScaleSplit_Version_TheEditorTransformIsAtVersionFive)
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);
            ASSERT_NE(serializeContext, nullptr);

            const AZ::SerializeContext::ClassData* classData = serializeContext->FindClassData(azrtti_typeid<EditorTransform>());
            ASSERT_NE(classData, nullptr);
            EXPECT_EQ(classData->m_version, 5u);
        }

        //////////////////////////////////////////////////////////////////////////
        // Section: undo and redo of the values a reparent and a world write solve (D.50, D.51).
        //
        // These run in the prefab fixture, whose undo stack records an edit and restores the entity from the patched prefab. An
        // undo reloads the entities, so the tests keep ids and read through the buses and never hold a pointer.
        class EditorTransformUndoTest
            : public UnitTest::PrefabTestFixture
        {
        protected:
            EditorTransform GetEditorTransform(AZ::EntityId entityId)
            {
                EditorTransform editorTransform;
                Components::TransformComponentMessages::Bus::EventResult(
                    editorTransform, entityId, &Components::TransformComponentMessages::Bus::Events::GetLocalEditorTransform);
                return editorTransform;
            }

            AZ::Vector3 GetWorldTranslation(AZ::EntityId entityId)
            {
                AZ::Vector3 translation = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(translation, entityId, &AZ::TransformBus::Events::GetWorldTranslation);
                return translation;
            }

            AZ::EntityId GetParentId(AZ::EntityId entityId)
            {
                AZ::EntityId parentId;
                AZ::TransformBus::EventResult(parentId, entityId, &AZ::TransformBus::Events::GetParentId);
                return parentId;
            }

            AZ::Quaternion GetWorldRotation(AZ::EntityId entityId)
            {
                AZ::Quaternion rotation = AZ::Quaternion::CreateZero();
                AZ::TransformBus::EventResult(rotation, entityId, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
                return rotation;
            }

            // The effective signed world scale, which only a transform that serves answers. The default stays one.
            AZ::Vector3 GetWorldScale(AZ::EntityId entityId)
            {
                AZ::Vector3 scale = AZ::Vector3::CreateOne();
                AZ::Transform2RequestBus::EventResult(scale, entityId, &AZ::Transform2Requests::GetWorldScale);
                return scale;
            }

            // Runs an edit as one undo step, the way the editor records one, and propagates the patched prefab.
            template<typename Edit>
            void EditAsOneUndoStep(AZ::EntityId entityId, Edit edit)
            {
                {
                    ScopedUndoBatch undoBatch("Edit transform");
                    edit();
                    m_prefabPublicInterface->GenerateUndoNodesForEntityChangeAndUpdateCache(entityId, undoBatch.GetUndoBatch());
                }
                PropagateAllTemplateChanges();
            }

            // A flipped parent at x = 10, written as an undo step of its own.
            AZ::EntityId CreateFlippedParentEntity()
            {
                const AZ::EntityId parentId = CreateEditorEntityUnderRoot("Parent");
                EditorTransform flipped = GetEditorTransform(parentId);
                flipped.m_translate = AZ::Vector3(10.0f, 0.0f, 0.0f);
                flipped.m_authoredScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
                EditAsOneUndoStep(
                    parentId,
                    [&]()
                    {
                        Components::TransformComponentMessages::Bus::Event(
                            parentId, &Components::TransformComponentMessages::Bus::Events::SetLocalEditorTransform, flipped);
                    });
                return parentId;
            }
        };

        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_AKeepWorldReparentIntoAFlippedParentIsTakenBack)
        {
            const AZ::EntityId parentId = CreateFlippedParentEntity();
            const AZ::EntityId childId = CreateEditorEntityUnderRoot("Child");
            EditAsOneUndoStep(
                childId,
                [&]()
                {
                    AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(7.0f, 0.0f, 0.0f));
                });
            const EditorTransform before = GetEditorTransform(childId);
            const AZ::EntityId parentBefore = GetParentId(childId);
            ASSERT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            ASSERT_NE(parentBefore, parentId);

            EditAsOneUndoStep(
                childId, [&]() { AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetParent, parentId); });

            // The position is kept, the authored local is solved against the flipped parent, and the own scale is untouched.
            EXPECT_EQ(GetParentId(childId), parentId);
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            const EditorTransform reparented = GetEditorTransform(childId);
            EXPECT_THAT(reparented.m_translate, UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
            EXPECT_EQ(reparented.m_authoredScale, AZ::Vector3(1.0f));

            // Undo gives back every authored value the reparent solved.
            Undo();
            EXPECT_EQ(GetParentId(childId), parentBefore);
            const EditorTransform undone = GetEditorTransform(childId);
            EXPECT_THAT(undone.m_translate, UnitTest::IsClose(before.m_translate));
            EXPECT_THAT(undone.m_rotate, UnitTest::IsClose(before.m_rotate));
            EXPECT_EQ(undone.m_authoredScale, before.m_authoredScale);
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            // And redo solves them again to the same values.
            Redo();
            EXPECT_EQ(GetParentId(childId), parentId);
            EXPECT_THAT(GetEditorTransform(childId).m_translate, UnitTest::IsClose(AZ::Vector3(3.0f, 0.0f, 0.0f)));
            EXPECT_EQ(GetEditorTransform(childId).m_authoredScale, AZ::Vector3(1.0f));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
        }

        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_ADescendantFollowsAReparentIntoAFlippedParentAfterAnUndo)
        {
            // The parent is at x = 10, flipped on X. K goes to x = 7, and its child is one further along its own X.
            const AZ::EntityId parentId = CreateFlippedParentEntity();
            const AZ::EntityId kId = CreateEditorEntityUnderRoot("K");
            const AZ::EntityId childId = CreateEditorEntity("Child", kId);
            EditAsOneUndoStep(
                kId, [&]() { AZ::TransformBus::Event(kId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(7.0f, 0.0f, 0.0f)); });
            EditAsOneUndoStep(
                childId, [&]() { AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(1.0f, 0.0f, 0.0f)); });
            ASSERT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(8.0f, 0.0f, 0.0f)));

            // Under the flipped parent, K stays at 7 and its +X now points the other way, so the child is at 6.
            EditAsOneUndoStep(kId, [&]() { AZ::TransformBus::Event(kId, &AZ::TransformBus::Events::SetParent, parentId); });
            EXPECT_THAT(GetWorldTranslation(kId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(6.0f, 0.0f, 0.0f)));

            Undo();
            EXPECT_THAT(GetWorldTranslation(kId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(8.0f, 0.0f, 0.0f)));

            // The same reparent again, on what the undo left behind.
            EditAsOneUndoStep(kId, [&]() { AZ::TransformBus::Event(kId, &AZ::TransformBus::Events::SetParent, parentId); });
            EXPECT_THAT(GetWorldTranslation(kId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(6.0f, 0.0f, 0.0f)));
        }

        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_AWorldWriteUnderAFlippedParentIsTakenBack)
        {
            const AZ::EntityId parentId = CreateFlippedParentEntity();
            const AZ::EntityId childId = CreateEditorEntityUnderRoot("Child");
            EditAsOneUndoStep(
                childId, [&]() { AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetParentRelative, parentId); });
            EditAsOneUndoStep(
                childId,
                [&]()
                {
                    AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(3.0f, 0.0f, 0.0f));
                });
            const EditorTransform before = GetEditorTransform(childId);
            ASSERT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            EditAsOneUndoStep(
                childId,
                [&]()
                {
                    AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(2.0f, 3.0f, 4.0f));
                });

            // The world is where it was put, and the authored local is the mirror image of the offset from the parent: the X
            // offset is 2 - 10 = -8, which the mirror turns into 8. The own scale was not written.
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(2.0f, 3.0f, 4.0f)));
            EXPECT_THAT(GetEditorTransform(childId).m_translate, UnitTest::IsClose(AZ::Vector3(8.0f, 3.0f, 4.0f)));
            EXPECT_EQ(GetEditorTransform(childId).m_authoredScale, AZ::Vector3(1.0f));

            Undo();
            EXPECT_THAT(GetEditorTransform(childId).m_translate, UnitTest::IsClose(before.m_translate));
            EXPECT_EQ(GetEditorTransform(childId).m_authoredScale, before.m_authoredScale);
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
        }

        // The tests above reparent a child with the identity rotation, which the unmirrored lines of the reparent get right. Here the
        // parent is turned and flipped and the child is turned a quarter turn, so the rotation that undo and redo bring back is one
        // that had to be solved.
        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_AKeepWorldReparentUnderATurnedAndFlippedParentIsTakenBack)
        {
            const AZ::EntityId parentId = CreateFlippedParentEntity();
            EditAsOneUndoStep(
                parentId,
                [&]()
                {
                    AZ::TransformBus::Event(
                        parentId, &AZ::TransformBus::Events::SetLocalRotation, AZ::Vector3(0.0f, 0.0f, AZ::DegToRad(30.0f)));
                });
            const AZ::EntityId childId = CreateEditorEntityUnderRoot("Child");
            EditAsOneUndoStep(
                childId,
                [&]()
                {
                    AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(7.0f, 0.0f, 0.0f));
                    AZ::TransformBus::Event(
                        childId, &AZ::TransformBus::Events::SetLocalRotation, AZ::Vector3(0.0f, 0.0f, AZ::Constants::HalfPi));
                });
            const EditorTransform before = GetEditorTransform(childId);
            const AZ::EntityId parentBefore = GetParentId(childId);
            const AZ::Quaternion quarterTurn = AZ::Quaternion::CreateRotationZ(AZ::Constants::HalfPi);
            ASSERT_THAT(GetWorldRotation(childId), UnitTest::IsClose(quarterTurn));
            ASSERT_NE(parentBefore, parentId);

            EditAsOneUndoStep(childId, [&]() { AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetParent, parentId); });

            // The view is kept, and the rotation that gives it under this parent is -60: the parent turns 30, the child needs 60 more
            // in the world, and the mirror reverses a turn about Z.
            EXPECT_EQ(GetParentId(childId), parentId);
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
            EXPECT_THAT(GetWorldRotation(childId), UnitTest::IsClose(quarterTurn));
            EXPECT_NEAR(GetEditorTransform(childId).m_rotate.GetZ(), -60.0f, 0.01f);

            Undo();
            EXPECT_EQ(GetParentId(childId), parentBefore);
            EXPECT_THAT(GetEditorTransform(childId).m_rotate, UnitTest::IsCloseTolerance(before.m_rotate, 0.01f));
            EXPECT_THAT(GetWorldRotation(childId), UnitTest::IsClose(quarterTurn));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            Redo();
            EXPECT_EQ(GetParentId(childId), parentId);
            EXPECT_NEAR(GetEditorTransform(childId).m_rotate.GetZ(), -60.0f, 0.01f);
            EXPECT_THAT(GetWorldRotation(childId), UnitTest::IsClose(quarterTurn));
        }

        // A crossing of the scale is one edit. The re-activation of the entities that follows it joins the step of that edit, and
        // the step is closed again. The fixture shows the step: the cycle runs inside it, none is open afterwards, the tick posts no
        // step of its own, and one undo takes everything back. The next test shows the capture of what a component derives.
        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_ACrossingAndTheReactivationThatFollowsItAreOneUndoStep)
        {
            const AZ::EntityId parentId = CreateFlippedParentEntity();
            const AZ::EntityId childId = CreateEditorEntity("Child", parentId);
            EditAsOneUndoStep(
                childId,
                [&]()
                {
                    AZ::TransformBus::Event(childId, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(3.0f, 0.0f, 0.0f));
                });
            AZ::TickBus::ExecuteQueuedEvents();
            ASSERT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));

            // The flip becomes a stretch: a crossing of the parent, and of the child by proxy.
            EditorTransform stretched = GetEditorTransform(parentId);
            stretched.m_authoredScale = AZ::Vector3(-2.0f, 1.0f, 1.0f);
            EditAsOneUndoStep(
                parentId,
                [&]()
                {
                    Components::TransformComponentMessages::Bus::Event(
                        parentId, &Components::TransformComponentMessages::Bus::Events::SetLocalEditorTransform, stretched);
                });
            ASSERT_TRUE(AZ::NonUniformScaleRequestBus::HasHandlers(parentId));
            UndoSystem::URSequencePoint* const stepOfTheEdit = m_undoStack->GetTop();
            UndoBatchProbe parentProbe(parentId);
            UndoBatchProbe childProbe(childId);

            AZ::TickBus::ExecuteQueuedEvents();

            EXPECT_EQ(parentProbe.m_announced, 2);
            EXPECT_EQ(parentProbe.m_inBatch, 2);
            EXPECT_EQ(childProbe.m_announced, 2);
            EXPECT_EQ(childProbe.m_inBatch, 2);
            UndoSystem::URSequencePoint* openBatch = nullptr;
            ToolsApplicationRequests::Bus::BroadcastResult(openBatch, &ToolsApplicationRequests::GetCurrentUndoBatch);
            EXPECT_EQ(openBatch, nullptr);
            EXPECT_EQ(m_undoStack->GetTop(), stepOfTheEdit);

            // One undo takes the stretch back, with the buses that followed it.
            Undo();
            EXPECT_EQ(GetEditorTransform(parentId).m_authoredScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(parentId));
            EXPECT_FALSE(AZ::NonUniformScaleRequestBus::HasHandlers(childId));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
        }

        // A component that saves a value it derives at activation, as a collider saves HasNonUniformScale. The cycle of a crossing
        // changes that value, so the capture after the cycle has something real to write into the step of the edit.
        class DerivedAtActivationComponent
            : public Components::EditorComponentBase
        {
        public:
            AZ_EDITOR_COMPONENT(DerivedAtActivationComponent, "{6F4F2C0E-7D8B-4B0A-9C55-2E1B7C3A9D41}");

            static void Reflect(AZ::ReflectContext* context)
            {
                if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
                {
                    serializeContext->Class<DerivedAtActivationComponent, Components::EditorComponentBase>()
                        ->Version(1)
                        ->Field("HasResidual", &DerivedAtActivationComponent::m_hasResidual);
                }
            }

            static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
            {
                required.push_back(AZ_CRC_CE("TransformService"));
            }

            void Activate() override
            {
                Components::EditorComponentBase::Activate();
                m_hasResidual = AZ::NonUniformScaleRequestBus::HasHandlers(GetEntityId());
            }

            bool m_hasResidual = false;
        };

        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_TheCycleOfACrossingCapturesWhatAComponentDerivesInTheStepOfTheEdit)
        {
            GetApplication()->RegisterComponentDescriptor(DerivedAtActivationComponent::CreateDescriptor());

            const AZ::EntityId parentId = CreateFlippedParentEntity();
            const AZ::EntityId childId = CreateEditorEntity("Child", parentId);
            EditAsOneUndoStep(
                childId,
                [&]()
                {
                    AZ::Entity* child = AzToolsFramework::GetEntityById(childId);
                    child->Deactivate();
                    child->CreateComponent<DerivedAtActivationComponent>();
                    child->Activate();
                });
            AZ::TickBus::ExecuteQueuedEvents();
            ASSERT_FALSE(AzToolsFramework::GetEntityById(childId)->FindComponent<DerivedAtActivationComponent>()->m_hasResidual);

            // The flip becomes a stretch: a crossing of the parent, and of the child by proxy.
            EditorTransform stretched = GetEditorTransform(parentId);
            stretched.m_authoredScale = AZ::Vector3(-2.0f, 1.0f, 1.0f);
            EditAsOneUndoStep(
                parentId,
                [&]()
                {
                    Components::TransformComponentMessages::Bus::Event(
                        parentId, &Components::TransformComponentMessages::Bus::Events::SetLocalEditorTransform, stretched);
                });
            UndoSystem::URSequencePoint* const stepOfTheEdit = m_undoStack->GetTop();
            const size_t nodesOfTheEdit = stepOfTheEdit->GetChildren().size();

            AZ::TickBus::ExecuteQueuedEvents();

            // The child derived the new value in its cycle, and the capture after the cycle wrote it into the step of the edit. Only
            // the child has anything to write: the transform of a descendant does not change.
            EXPECT_TRUE(AzToolsFramework::GetEntityById(childId)->FindComponent<DerivedAtActivationComponent>()->m_hasResidual);
            EXPECT_EQ(m_undoStack->GetTop(), stepOfTheEdit);
            EXPECT_GT(stepOfTheEdit->GetChildren().size(), nodesOfTheEdit);
            EXPECT_EQ(GetEditorTransform(childId).m_authoredScale, AZ::Vector3(1.0f, 1.0f, 1.0f));

            // One undo takes the stretch and the value back.
            Undo();
            AZ::TickBus::ExecuteQueuedEvents();
            EXPECT_FALSE(AzToolsFramework::GetEntityById(childId)->FindComponent<DerivedAtActivationComponent>()->m_hasResidual);
            EXPECT_EQ(GetEditorTransform(parentId).m_authoredScale, AZ::Vector3(-1.0f, 1.0f, 1.0f));
        }

        // An undo re-creates the parent entity and reloads only the entities whose saved data it patches. A child that sits on the
        // pivot of its parent does not move when the parent changes the axis it flips, so the saved data of the child is the same
        // before and after the edit and the undo leaves the entity as it is. It has to read the flip of the new parent entity,
        // because nothing else tells it, and so does the child of that child, which does move.
        TEST_F(EditorTransformUndoTest, ScaleSplit_Undo_AChildThatTheUndoDoesNotReloadReadsTheFlipOfItsParentAgain)
        {
            // The parent is flipped on X at x = 10. The child is at its local 0, with no rotation. Its own child is at (1,1,0).
            const AZ::EntityId parentId = CreateFlippedParentEntity();
            const AZ::EntityId childId = CreateEditorEntity("Child", parentId);
            const AZ::EntityId grandchildId = CreateEditorEntity("Grandchild", childId);
            EditAsOneUndoStep(
                grandchildId,
                [&]()
                {
                    AZ::TransformBus::Event(grandchildId, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(1.0f, 1.0f, 0.0f));
                });
            ASSERT_THAT(GetEditorTransform(childId).m_translate, UnitTest::IsClose(AZ::Vector3::CreateZero()));
            ASSERT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(10.0f, 0.0f, 0.0f)));

            // The grandchild is 1 along X of the child, which points to -X, and 1 along Y: (10 - 1, 0 + 1, 0).
            EXPECT_EQ(GetWorldScale(childId), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_THAT(GetWorldTranslation(grandchildId), UnitTest::IsClose(AZ::Vector3(9.0f, 1.0f, 0.0f)));

            // The parent flips on Y instead, as one edit. The child stays where it is, and the grandchild is at (10 + 1, 0 - 1, 0).
            EditorTransform reflipped = GetEditorTransform(parentId);
            reflipped.m_authoredScale = AZ::Vector3(1.0f, -1.0f, 1.0f);
            EditAsOneUndoStep(
                parentId,
                [&]()
                {
                    Components::TransformComponentMessages::Bus::Event(
                        parentId, &Components::TransformComponentMessages::Bus::Events::SetLocalEditorTransform, reflipped);
                });
            EXPECT_EQ(GetWorldScale(childId), AZ::Vector3(1.0f, -1.0f, 1.0f));
            EXPECT_THAT(GetWorldTranslation(childId), UnitTest::IsClose(AZ::Vector3(10.0f, 0.0f, 0.0f)));
            EXPECT_THAT(GetWorldTranslation(grandchildId), UnitTest::IsClose(AZ::Vector3(11.0f, -1.0f, 0.0f)));

            Undo();
            EXPECT_EQ(GetWorldScale(parentId), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_EQ(GetWorldScale(childId), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_EQ(GetWorldScale(grandchildId), AZ::Vector3(-1.0f, 1.0f, 1.0f));
            EXPECT_THAT(GetWorldTranslation(grandchildId), UnitTest::IsClose(AZ::Vector3(9.0f, 1.0f, 0.0f)));

            Redo();
            EXPECT_EQ(GetWorldScale(childId), AZ::Vector3(1.0f, -1.0f, 1.0f));
            EXPECT_EQ(GetWorldScale(grandchildId), AZ::Vector3(1.0f, -1.0f, 1.0f));
            EXPECT_THAT(GetWorldTranslation(grandchildId), UnitTest::IsClose(AZ::Vector3(11.0f, -1.0f, 0.0f)));
        }
    } // namespace ScaleSplit
} // namespace AzToolsFramework
