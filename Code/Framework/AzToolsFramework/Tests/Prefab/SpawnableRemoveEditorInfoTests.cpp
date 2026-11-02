/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Prefab/SpawnableRemoveEditorInfoTestFixture.h>

#include <AzCore/Component/EntityBus.h>
#include <AzCore/Component/EntityUtils.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/string/string.h>
#include <AZTestShared/Math/MathTestHelpers.h>
#include <AzFramework/Components/Transform2Component.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzToolsFramework/Prefab/Spawnable/EntityAliasTypes.h>
#include <AzToolsFramework/Prefab/Spawnable/PrefabDocument.h>
#include <AzToolsFramework/ToolsComponents/TransformComponent.h>

namespace UnitTest
{
    using SpawnableRemoveEditorInfoTests = SpawnableRemoveEditorInfoTestFixture;

    TEST_F(SpawnableRemoveEditorInfoTests, SpawnableRemoveEditorInfo_OnlyRuntimeEntityExported)
    {
        // Create one entity that's flagged as Editor-Only, and one that's enabled for runtime.
        CreateSourceEntity("EditorOnly", true);
        CreateSourceEntity("EditorAndRuntime", false);

        ConvertRuntimePrefab();

        // Only the runtime entity exists in the converted Prefab DOM.
        EXPECT_FALSE(GetRuntimeEntity("EditorOnly"));
        EXPECT_TRUE(GetRuntimeEntity("EditorAndRuntime"));
    }

    TEST_F(SpawnableRemoveEditorInfoTests, SpawnableRemoveEditorInfo_RuntimeComponentExportedSuccessfully)
    {
        // Create a component with RuntimeExportCallback and successfully exports itself.
        CreateSourceTestExportRuntimeEntity("EntityWithRuntimeComponent", true, true);

        ConvertRuntimePrefab();

        // Expected result: processed entity contains the component.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithRuntimeComponent");
        ASSERT_TRUE(entity);
        EXPECT_TRUE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_RuntimeComponentExportRemoved)
    {
        // Create a component with RuntimeExportCallback and successfully removes itself from exporting.
        CreateSourceTestExportRuntimeEntity("EntityWithRuntimeComponent", false, true);

        ConvertRuntimePrefab();

        // Expected result: processed entity does NOT contain the component.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithRuntimeComponent");
        ASSERT_TRUE(entity);
        EXPECT_FALSE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_RuntimeComponentExportUnhandled)
    {
        // Create a component with RuntimeExportCallback, returns a pointer to itself, but says it wasn't handled.
        CreateSourceTestExportRuntimeEntity("EntityWithRuntimeComponent", true, false);

        ConvertRuntimePrefab();

        // Expected result:  processed entity contains the component, because the default behavior is "clone/add" for
        // runtime components.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithRuntimeComponent");
        ASSERT_TRUE(entity);
        EXPECT_TRUE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_RuntimeComponentExportRemovedAndUnhandled)
    {
        // Create a component with RuntimeExportCallback and removes itself from exporting, but says it wasn't handled.
        CreateSourceTestExportRuntimeEntity("EntityWithRuntimeComponent", false, false);

        ConvertRuntimePrefab();

        // Expected result:  processed entity contains the component, because by saying it wasn't handled, it
        // should fall back on the default behavior of "clone/add" for runtime components.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithRuntimeComponent");
        ASSERT_TRUE(entity);
        EXPECT_TRUE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_EditorComponentExportedSuccessfully)
    {
        // Create an editor component that has a RuntimeExportCallback and successfully exports itself.
        CreateSourceTestExportEditorEntity(
            "EntityWithEditorComponent",
            TestExportEditorComponent::ExportComponentType::ExportRuntimeComponentWithoutCallBack,
            true);

        ConvertRuntimePrefab();

        // Expected result:  processed entity contains the runtime component, exported from RuntimeExportCallback.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithEditorComponent");
        ASSERT_TRUE(entity);
        EXPECT_FALSE(entity->FindComponent<TestExportEditorComponent>());
        EXPECT_FALSE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
        EXPECT_TRUE(entity->FindComponent<TestExportRuntimeComponentWithoutCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_EditorComponentExportRemoved)
    {
        // Create an editor component that has a RuntimeExportCallback and successfully removes itself from exporting.
        CreateSourceTestExportEditorEntity(
            "EntityWithEditorComponent",
            TestExportEditorComponent::ExportComponentType::ExportNullComponent,
            true);

        ConvertRuntimePrefab();

        // Expected result: processed entity does NOT contain either component.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithEditorComponent");
        ASSERT_TRUE(entity);
        EXPECT_FALSE(entity->FindComponent<TestExportEditorComponent>());
        EXPECT_FALSE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
        EXPECT_FALSE(entity->FindComponent<TestExportRuntimeComponentWithoutCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_EditorComponentExportUnhandledFallBackToBuildGameEntity)
    {
        // Create an editor component that has a RuntimeExportCallback, returns a pointer to itself, but says it wasn't handled.
        CreateSourceTestExportEditorEntity(
            "EntityWithEditorComponent",
            TestExportEditorComponent::ExportComponentType::ExportEditorComponent,
            false);

        ConvertRuntimePrefab();

        // Expected result:  processed entity contains the runtime component, because the fallback to BuildGameEntity()
        // produced a runtime component.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithEditorComponent");
        ASSERT_TRUE(entity);
        EXPECT_FALSE(entity->FindComponent<TestExportEditorComponent>());
        EXPECT_TRUE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
        EXPECT_FALSE(entity->FindComponent<TestExportRuntimeComponentWithoutCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_EditorComponentExportRemovedAndUnhandledFallBackToBuildGameEntity)
    {
        // Create an editor component that has a RuntimeExportCallback and removes itself from exporting, but says it wasn't handled.
        CreateSourceTestExportEditorEntity(
            "EntityWithEditorComponent",
            TestExportEditorComponent::ExportComponentType::ExportNullComponent,
            false);

        ConvertRuntimePrefab();

        // Expected result:  processed entity contains the runtime component, because the fallback to BuildGameEntity()
        // produced a runtime component.
        AZ::Entity* entity = GetRuntimeEntity("EntityWithEditorComponent");
        ASSERT_TRUE(entity);
        EXPECT_FALSE(entity->FindComponent<TestExportEditorComponent>());
        EXPECT_TRUE(entity->FindComponent<TestExportRuntimeComponentWithCallback>());
        EXPECT_FALSE(entity->FindComponent<TestExportRuntimeComponentWithoutCallback>());
    }

    TEST_F(SpawnableRemoveEditorInfoTests, RuntimeExportCallback_EditorComponentFailsToExportItself)
    {
        // Create an editor component that has a RuntimeExportCallback and removes itself from exporting, but says it wasn't handled.
        CreateSourceTestExportEditorEntity(
            "EntityWithEditorComponent",
            TestExportEditorComponent::ExportComponentType::ExportEditorComponent,
            true);

        // We expect the exporting to fail, since an editor component is being exported as a game component.
        ConvertRuntimePrefab(false);
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 in the export (D.44, D.45, D.54, D.56).
    //
    // A child of an extended transform deploys as a Transform 2 too, and it learns that from its parent through the entities that
    // are initialized. So when any transform of a prefab is extended, every entity is initialized before the first is exported.
    // A prefab without one takes the path it always took, and each entity is initialized as it is exported.

    // Logs the order in which the entities that carry the probe are initialized (I) and exported (B). The probe logs both, so the
    // container entity of the instance, which is initialized and exported with the rest and carries no probe, is not in the log.
    class InitOrderLog
    {
    public:
        InitOrderLog()
        {
            s_active = this;
        }

        ~InitOrderLog()
        {
            s_active = nullptr;
        }

        static void Record(char kind)
        {
            if (s_active)
            {
                s_active->m_log.push_back(kind);
            }
        }

        AZStd::string m_log;

    private:
        static inline InitOrderLog* s_active = nullptr;
    };

    // An editor component that does nothing but log the moments its entity is initialized and exported.
    class InitOrderProbeComponent
        : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_COMPONENT(InitOrderProbeComponent, "{9A4D2C1E-6B3F-4E58-8D7A-3C5F1B2E9A60}", AzToolsFramework::Components::EditorComponentBase);

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serializeContext->Class<InitOrderProbeComponent, AzToolsFramework::Components::EditorComponentBase>()->Version(1);
            }
        }

        void Init() override
        {
            EditorComponentBase::Init();
            InitOrderLog::Record('I');
        }

        void Activate() override
        {
        }

        void Deactivate() override
        {
        }

        void BuildGameEntity(AZ::Entity*) override
        {
            InitOrderLog::Record('B');
        }
    };

    class SpawnableRemoveEditorInfoTransform2Tests
        : public SpawnableRemoveEditorInfoTestFixture
    {
    protected:
        using EditorTransformComponent = AzToolsFramework::Components::TransformComponent;
        using EditorTransform = AzToolsFramework::Components::EditorTransform;

        void SetUpEditorFixtureImpl() override
        {
            SpawnableRemoveEditorInfoTestFixture::SetUpEditorFixtureImpl();
            GetApplication()->RegisterComponentDescriptor(InitOrderProbeComponent::CreateDescriptor());
        }

        // The own scale of a deployed Transform 2, read through the interface its request bus serves. Calling GetLocalScale on
        // the component itself by name resolves to the deprecated Transform 1 getter, which is not what a test means.
        static AZ::Vector3 RuntimeLocalScale(const AzFramework::Transform2Component& transform2)
        {
            const AZ::Transform2Requests& requests = transform2;
            return requests.GetLocalScale();
        }

        static AZ::TransformConfig ConfigurationOf(const AzFramework::TransformComponent& transform)
        {
            AZ::TransformConfig config;
            transform.GetConfiguration(config);
            return config;
        }

        static AZStd::string LinkName(int index)
        {
            return AZStd::string::format("Link%d", index);
        }

        // An initialized and active source entity with an editor transform and the probe. The prefab takes it over at conversion.
        EditorTransformComponent* CreateTransformEntity(const char* name, const AZ::Vector3& scale, const AZ::Vector3& translation)
        {
            AZ::Entity* entity = aznew AZ::Entity(name);
            auto* transform = entity->CreateComponent<EditorTransformComponent>();
            entity->CreateComponent<InitOrderProbeComponent>();
            m_sourceEntities.push_back(entity);
            entity->Init();
            entity->Activate();

            EditorTransform editorTransform;
            editorTransform.m_translate = translation;
            editorTransform.m_authoredScale = scale;
            editorTransform.m_uniformScale = editorTransform.ResolveUniformScale();
            transform->SetLocalEditorTransform(editorTransform);
            return transform;
        }

        // A chain of entities named Link0, Link1 and so on, each the child of the one before. Link0 is at x = 10 and the others sit
        // at a local x of 1, each with the scale it is given.
        void CreateChain(AZStd::initializer_list<AZ::Vector3> scales)
        {
            AZ::EntityId previousId;
            int index = 0;
            for (const AZ::Vector3& scale : scales)
            {
                const bool first = (index == 0);
                EditorTransformComponent* link =
                    CreateTransformEntity(LinkName(index).c_str(), scale, first ? AZ::Vector3(10.0f, 0.0f, 0.0f) : AZ::Vector3::CreateZero());
                if (!first)
                {
                    link->SetParentRelative(previousId);
                    link->SetLocalTranslation(AZ::Vector3(1.0f, 0.0f, 0.0f));
                }
                previousId = link->GetEntityId();
                ++index;
            }
        }

        // Exports the source entities and returns the order in which they were initialized and exported.
        AZStd::string ConvertAndLogInitOrder()
        {
            ConvertSourceEntitiesToPrefab();

            AzToolsFramework::Prefab::PrefabConversionUtils::PrefabDocument prefab("Test");
            EXPECT_TRUE(prefab.SetPrefabDom(m_prefabDom));

            // The log starts after the prefab is loaded, which initializes nothing.
            InitOrderLog log;
            EXPECT_TRUE(m_editorInfoRemover.RemoveEditorInfo(prefab, m_serializeContext, m_prefabProcessorContext).IsSuccess());

            prefab.GetInstance().DetachAllEntitiesInHierarchy(
                [this](AZStd::unique_ptr<AZ::Entity> entity)
                {
                    m_runtimeEntities.emplace_back(entity.release());
                });
            return log.m_log;
        }
    };

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, ExtendedChain_EveryLinkDeploysAsTransform2InWhateverOrderTheInstanceHoldsThem)
    {
        // The instance keeps its entities in no order that a test can pick, so a chain this long meets a child before its parent
        // at some link on nearly every run. A link exported before its parent was initialized would not find the flip, would
        // deploy as a Transform 1 and would carry the position of a parent it cannot see.
        constexpr int ChainLength = 8;
        CreateChain({ AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(1.0f), AZ::Vector3(1.0f), AZ::Vector3(1.0f),
                      AZ::Vector3(1.0f), AZ::Vector3(1.0f), AZ::Vector3(1.0f), AZ::Vector3(1.0f) });

        ConvertRuntimePrefab();

        for (int index = 0; index < ChainLength; ++index)
        {
            AZ::Entity* link = GetRuntimeEntity(LinkName(index).c_str());
            ASSERT_NE(link, nullptr) << "Link" << index;
            auto* transform2 = link->FindComponent<AzFramework::Transform2Component>();
            ASSERT_NE(transform2, nullptr) << "Link" << index;
            EXPECT_EQ(link->FindComponent<AzFramework::TransformComponent>(), nullptr) << "Link" << index;

            // Only the first link has a scale of its own. The others get the flip from it through the chain at runtime.
            EXPECT_EQ(RuntimeLocalScale(*transform2), index == 0 ? AZ::Vector3(-1.0f, 1.0f, 1.0f) : AZ::Vector3(1.0f)) << "Link" << index;

            // The authored local is kept, and the world carried along is the true position: the flip points every later link to the
            // left of the one before, so the first is at 10 and the last at 10 - 7 = 3.
            const AZ::TransformConfig config = ConfigurationOf(*transform2);
            EXPECT_THAT(
                config.m_localTransform.GetTranslation(),
                UnitTest::IsClose(index == 0 ? AZ::Vector3(10.0f, 0.0f, 0.0f) : AZ::Vector3(1.0f, 0.0f, 0.0f)))
                << "Link" << index;
            EXPECT_THAT(
                config.m_worldTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(10.0f - static_cast<float>(index), 0.0f, 0.0f)))
                << "Link" << index;
            if (index > 0)
            {
                AZ::Entity* previous = GetRuntimeEntity(LinkName(index - 1).c_str());
                ASSERT_NE(previous, nullptr);
                EXPECT_EQ(config.m_parentId, previous->GetId()) << "Link" << index;
            }
        }
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, ExtendedChild_OnlyTheChildAndItsDescendantsDeployAsTransform2)
    {
        // The parent is plain and the middle link is flipped. Link0 stays a Transform 1, and Link1 and Link2 are Transform 2.
        CreateChain({ AZ::Vector3(1.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(1.0f) });

        ConvertRuntimePrefab();

        AZ::Entity* first = GetRuntimeEntity("Link0");
        AZ::Entity* second = GetRuntimeEntity("Link1");
        AZ::Entity* third = GetRuntimeEntity("Link2");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(third, nullptr);
        EXPECT_NE(first->FindComponent<AzFramework::TransformComponent>(), nullptr);
        EXPECT_EQ(first->FindComponent<AzFramework::Transform2Component>(), nullptr);
        ASSERT_NE(second->FindComponent<AzFramework::Transform2Component>(), nullptr);
        ASSERT_NE(third->FindComponent<AzFramework::Transform2Component>(), nullptr);
        EXPECT_EQ(RuntimeLocalScale(*third->FindComponent<AzFramework::Transform2Component>()), AZ::Vector3(1.0f));

        // Link1 sits at 10 + 1 = 11 and is flipped itself, which does not move it. Link2 is its child at local x = 1: 11 - 1 = 10.
        EXPECT_THAT(
            ConfigurationOf(*second->FindComponent<AzFramework::Transform2Component>()).m_worldTransform.GetTranslation(),
            UnitTest::IsClose(AZ::Vector3(11.0f, 0.0f, 0.0f)));
        EXPECT_THAT(
            ConfigurationOf(*third->FindComponent<AzFramework::Transform2Component>()).m_worldTransform.GetTranslation(),
            UnitTest::IsClose(AZ::Vector3(10.0f, 0.0f, 0.0f)));
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, ExtendedPrefab_InitializesEveryEntityBeforeTheFirstExport)
    {
        CreateChain({ AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(1.0f), AZ::Vector3(1.0f) });

        const AZStd::string order = ConvertAndLogInitOrder();

        // All three are initialized, then all three are exported.
        EXPECT_STREQ(order.c_str(), "IIIBBB");
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, PlainPrefab_ExportsThroughTheOldPathWithNoInitPass)
    {
        // Link0 is a plain scale of 2 at x = 10, and Link1 and Link2 are plain scales of 1 at a local x of 1.
        CreateChain({ AZ::Vector3(2.0f), AZ::Vector3(1.0f), AZ::Vector3(1.0f) });

        const AZStd::string order = ConvertAndLogInitOrder();

        // Each entity is initialized as it is exported, which is how the exporter always worked.
        EXPECT_STREQ(order.c_str(), "IBIBIB");

        // Every link is the Transform 1 it always was, with the configuration it always had.
        for (int index = 0; index < 3; ++index)
        {
            AZ::Entity* link = GetRuntimeEntity(LinkName(index).c_str());
            ASSERT_NE(link, nullptr) << "Link" << index;
            auto* transform = link->FindComponent<AzFramework::TransformComponent>();
            ASSERT_NE(transform, nullptr) << "Link" << index;
            EXPECT_EQ(link->FindComponent<AzFramework::Transform2Component>(), nullptr) << "Link" << index;

            const AZ::TransformConfig config = ConfigurationOf(*transform);
            EXPECT_THAT(
                config.m_localTransform.GetTranslation(),
                UnitTest::IsClose(index == 0 ? AZ::Vector3(10.0f, 0.0f, 0.0f) : AZ::Vector3(1.0f, 0.0f, 0.0f)))
                << "Link" << index;
            EXPECT_FLOAT_EQ(config.m_localTransform.GetUniformScale(), index == 0 ? 2.0f : 1.0f) << "Link" << index;
            if (index > 0)
            {
                AZ::Entity* previous = GetRuntimeEntity(LinkName(index - 1).c_str());
                ASSERT_NE(previous, nullptr);
                EXPECT_EQ(config.m_parentId, previous->GetId()) << "Link" << index;
            }
        }
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, EditorOnlyFlippedParent_IsRemovedAndItsChildKeepsTheFlip)
    {
        // The parent is editor-only, flipped on X and at x = 10. Its child is at a local x of 3.
        CreateSourceEntity("RemovedParent", true);
        CreateSourceEntity("Child", false);
        auto* parentTransform = m_sourceEntities[0]->FindComponent<EditorTransformComponent>();
        auto* childTransform = m_sourceEntities[1]->FindComponent<EditorTransformComponent>();
        EditorTransform flipped;
        flipped.m_translate = AZ::Vector3(10.0f, 0.0f, 0.0f);
        flipped.m_authoredScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        flipped.m_uniformScale = flipped.ResolveUniformScale();
        parentTransform->SetLocalEditorTransform(flipped);
        childTransform->SetParentRelative(parentTransform->GetEntityId());
        childTransform->SetLocalTranslation(AZ::Vector3(3.0f, 0.0f, 0.0f));

        ConvertRuntimePrefab();

        // The parent is gone. The child takes its place with no parent and the flip of the removed entity as its own scale, at
        // 10 - 3 = 7.
        EXPECT_EQ(GetRuntimeEntity("RemovedParent"), nullptr);
        AZ::Entity* child = GetRuntimeEntity("Child");
        ASSERT_NE(child, nullptr);
        auto* transform2 = child->FindComponent<AzFramework::Transform2Component>();
        ASSERT_NE(transform2, nullptr);
        EXPECT_EQ(RuntimeLocalScale(*transform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        const AZ::TransformConfig config = ConfigurationOf(*transform2);
        EXPECT_FALSE(config.m_parentId.IsValid());
        EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, EditorOnlyTurnedAndFlippedParent_IsRemovedAndItsChildKeepsTheRotationOfTheChain)
    {
        // The parent is editor-only, flipped on X, at x = 10 and turned 30 degrees about Z. Its child is turned 10 degrees and sits at
        // a local x of 3.
        CreateSourceEntity("RemovedParent", true);
        CreateSourceEntity("Child", false);
        auto* parentTransform = m_sourceEntities[0]->FindComponent<EditorTransformComponent>();
        auto* childTransform = m_sourceEntities[1]->FindComponent<EditorTransformComponent>();
        EditorTransform flipped;
        flipped.m_translate = AZ::Vector3(10.0f, 0.0f, 0.0f);
        flipped.m_rotate = AZ::Vector3(0.0f, 0.0f, 30.0f);
        flipped.m_authoredScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        flipped.m_uniformScale = flipped.ResolveUniformScale();
        parentTransform->SetLocalEditorTransform(flipped);
        childTransform->SetParentRelative(parentTransform->GetEntityId());
        EditorTransform turned = childTransform->GetLocalEditorTransform();
        turned.m_translate = AZ::Vector3(3.0f, 0.0f, 0.0f);
        turned.m_rotate = AZ::Vector3(0.0f, 0.0f, 10.0f);
        childTransform->SetLocalEditorTransform(turned);

        ConvertRuntimePrefab();

        // The child takes the place of its parent, with the flip. In the frame of the parent it is 3 to the left, which the turn of
        // 30 degrees puts at (10 - 3 cos 30, -3 sin 30, 0). The mirror reverses the turn of the child in that frame, so the child
        // ends turned 30 - 10 = 20 degrees. Left as it was authored it would be turned 10.
        EXPECT_EQ(GetRuntimeEntity("RemovedParent"), nullptr);
        AZ::Entity* child = GetRuntimeEntity("Child");
        ASSERT_NE(child, nullptr);
        auto* transform2 = child->FindComponent<AzFramework::Transform2Component>();
        ASSERT_NE(transform2, nullptr);
        EXPECT_EQ(RuntimeLocalScale(*transform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        const AZ::TransformConfig config = ConfigurationOf(*transform2);
        EXPECT_FALSE(config.m_parentId.IsValid());
        EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.401924f, -1.5f, 0.0f)));
        EXPECT_THAT(config.m_localTransform.GetRotation(), UnitTest::IsClose(AZ::Quaternion::CreateRotationZ(AZ::DegToRad(20.0f))));
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, EditorOnlyFlippedParentUnderAGrandparent_IsRemovedAndItsChildMovesUnderTheGrandparent)
    {
        // The grandparent is plain, at x = 5. The editor-only parent is flipped and sits at a local x of 10 under it. Its child is at a
        // local x of 3.
        CreateSourceEntity("Grandparent", false);
        CreateSourceEntity("RemovedParent", true);
        CreateSourceEntity("Child", false);
        auto* grandparentTransform = m_sourceEntities[0]->FindComponent<EditorTransformComponent>();
        auto* parentTransform = m_sourceEntities[1]->FindComponent<EditorTransformComponent>();
        auto* childTransform = m_sourceEntities[2]->FindComponent<EditorTransformComponent>();
        grandparentTransform->SetLocalTranslation(AZ::Vector3(5.0f, 0.0f, 0.0f));
        parentTransform->SetParentRelative(grandparentTransform->GetEntityId());
        EditorTransform flipped = parentTransform->GetLocalEditorTransform();
        flipped.m_translate = AZ::Vector3(10.0f, 0.0f, 0.0f);
        flipped.m_authoredScale = AZ::Vector3(-1.0f, 1.0f, 1.0f);
        flipped.m_uniformScale = flipped.ResolveUniformScale();
        parentTransform->SetLocalEditorTransform(flipped);
        childTransform->SetParentRelative(parentTransform->GetEntityId());
        childTransform->SetLocalTranslation(AZ::Vector3(3.0f, 0.0f, 0.0f));

        ConvertRuntimePrefab();

        // Without the parent, the child hangs from the grandparent: 10 - 3 = 7 from it, with the flip it carried through the parent.
        // Its world stays 5 + 7 = 12, where the editor had it.
        EXPECT_EQ(GetRuntimeEntity("RemovedParent"), nullptr);
        AZ::Entity* grandparent = GetRuntimeEntity("Grandparent");
        AZ::Entity* child = GetRuntimeEntity("Child");
        ASSERT_NE(grandparent, nullptr);
        ASSERT_NE(child, nullptr);
        auto* transform2 = child->FindComponent<AzFramework::Transform2Component>();
        ASSERT_NE(transform2, nullptr);
        EXPECT_EQ(RuntimeLocalScale(*transform2), AZ::Vector3(-1.0f, 1.0f, 1.0f));
        const AZ::TransformConfig config = ConfigurationOf(*transform2);
        EXPECT_EQ(config.m_parentId, grandparent->GetId());
        EXPECT_THAT(config.m_localTransform.GetTranslation(), UnitTest::IsClose(AZ::Vector3(7.0f, 0.0f, 0.0f)));
    }

    TEST_F(SpawnableRemoveEditorInfoTransform2Tests, EntityAlias_ReplacingATransform2Child_LeavesATransform1PlaceholderThatKeepsTheParentLink)
    {
        using namespace AzToolsFramework::Prefab::PrefabConversionUtils;

        // Link1 deploys as a Transform 2, because Link0 is flipped.
        CreateChain({ AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(1.0f) });
        ConvertSourceEntitiesToPrefab();
        PrefabDocument source("Source");
        ASSERT_TRUE(source.SetPrefabDom(m_prefabDom));
        ASSERT_TRUE(m_editorInfoRemover.RemoveEditorInfo(source, m_serializeContext, m_prefabProcessorContext).IsSuccess());

        // Finds the alias of Link1 and the id of Link0 in the converted prefab.
        const PrefabDocument& constSource = source;
        AzToolsFramework::Prefab::AliasPath childAlias;
        AZ::EntityId parentId;
        constSource.ListEntitiesWithComponentType<AzFramework::Transform2Component>(
            [&](AzToolsFramework::Prefab::AliasPath&& path)
            {
                const AzToolsFramework::Prefab::EntityAlias entityAlias(path.Filename().Native());
                const auto entity = constSource.GetInstance().GetEntity(entityAlias);
                if (entity.has_value() && entity->get().GetName() == "Link1")
                {
                    childAlias = path;
                }
                else if (entity.has_value() && entity->get().GetName() == "Link0")
                {
                    parentId = entity->get().GetId();
                }
                return true;
            });
        ASSERT_FALSE(childAlias.empty());
        ASSERT_TRUE(parentId.IsValid());

        // Replacing the entity hands the original to the target and leaves a placeholder in its place. The placeholder needs a
        // Transform 1 that points at the same parent, so the entities of the spawnable sort the same way. The original is a
        // Transform 2, which a lookup of the exact Transform 1 type does not find.
        PrefabDocument target("Target");
        AZ::Entity* original = target.CreateEntityAlias(
            source, childAlias, EntityAliasType::Replace, EntityAliasSpawnableLoadBehavior::NoLoad, AZ::Crc32(), m_prefabProcessorContext);
        ASSERT_NE(original, nullptr);
        EXPECT_NE(original->FindComponent<AzFramework::Transform2Component>(), nullptr);

        const AzToolsFramework::Prefab::EntityAlias childEntityAlias(childAlias.Filename().Native());
        const auto placeholder = source.GetInstance().GetEntity(childEntityAlias);
        ASSERT_TRUE(placeholder.has_value());
        auto* placeholderTransform = placeholder->get().FindComponent<AzFramework::TransformComponent>();
        ASSERT_NE(placeholderTransform, nullptr);
        EXPECT_EQ(placeholderTransform->GetParentId(), parentId);
    }
}
