/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzTest/AzTest.h>
#include <AzToolsFramework/ActionManager/Action/ActionManagerInternalInterface.h>
#include <AzToolsFramework/ActionManager/HotKey/HotKeyManagerInternalInterface.h>
#include <AzToolsFramework/ActionManager/Menu/MenuManagerInternalInterface.h>
#include <AzToolsFramework/ActionManager/ToolBar/ToolBarManagerInternalInterface.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/ToolsComponents/TransformComponent.h>
#include <AzToolsFramework/UnitTest/AzToolsFrameworkTestHelpers.h>

#include <AtomLyIntegration/CommonFeatures/Mesh/MeshComponentBus.h>
#include <AtomLyIntegration/CommonFeatures/Mesh/MeshHandleStateBus.h>

#include <Mesh/EditorMeshComponent.h>
#include <Mesh/MeshComponentController.h>

namespace UnitTest
{
    class IntersectionNotificationDetector : public AzFramework::RenderGeometry::IntersectionNotificationBus::Handler
    {
    public:
        void Connect(const AzFramework::EntityContextId& entityContextId);
        void Disconnect();

        // IntersectionNotificationBus overrides ...
        void OnEntityConnected(AZ::EntityId entityId) override;
        void OnEntityDisconnected(AZ::EntityId entityId) override;
        void OnGeometryChanged(AZ::EntityId entityId) override;

        AZ::EntityId m_lastEntityIdChanged;
    };

    void IntersectionNotificationDetector::Connect(const AzFramework::EntityContextId& entityContextId)
    {
        AzFramework::RenderGeometry::IntersectionNotificationBus::Handler::BusConnect(entityContextId);
    }

    void IntersectionNotificationDetector::Disconnect()
    {
        AzFramework::RenderGeometry::IntersectionNotificationBus::Handler::BusDisconnect();
    }

    void IntersectionNotificationDetector::OnEntityConnected([[maybe_unused]] AZ::EntityId entityId)
    {
    }

    void IntersectionNotificationDetector::OnEntityDisconnected([[maybe_unused]] AZ::EntityId entityId)
    {
    }

    void IntersectionNotificationDetector::OnGeometryChanged(AZ::EntityId entityId)
    {
        m_lastEntityIdChanged = entityId;
    }

    class MeshComponentControllerFixture : public ToolsApplicationFixture<false>
    {
    public:
        void SetUpEditorFixtureImpl() override
        {
            m_meshComponentDescriptor = AZStd::unique_ptr<AZ::ComponentDescriptor>(AZ::Render::MeshComponent::CreateDescriptor());
            m_meshComponentDescriptor->Reflect(GetApplication()->GetSerializeContext());

            m_editorMeshComponentDescriptor =
                AZStd::unique_ptr<AZ::ComponentDescriptor>(AZ::Render::EditorMeshComponent::CreateDescriptor());
            m_editorMeshComponentDescriptor->Reflect(GetApplication()->GetSerializeContext());

            m_entity = AZStd::make_unique<AZ::Entity>();
            m_entity->Init();
            m_entity->CreateComponent<AzToolsFramework::Components::TransformComponent>();
            m_entity->Activate();

            AzFramework::EntityContextId contextId(AZStd::string_view{"123456"});
            m_intersectionNotificationDetector.Connect(contextId);
        }

        void TearDownEditorFixtureImpl() override
        {
            m_entity.reset();

            m_intersectionNotificationDetector.Disconnect();

            // The application outlives a test, so undo the reflection for the next test of this fixture to do it again.
            AZ::SerializeContext* serializeContext = GetApplication()->GetSerializeContext();
            serializeContext->EnableRemoveReflection();
            m_editorMeshComponentDescriptor->Reflect(serializeContext);
            m_meshComponentDescriptor->Reflect(serializeContext);
            serializeContext->DisableRemoveReflection();

            m_meshComponentDescriptor.reset();
            m_editorMeshComponentDescriptor.reset();
        }

        AZStd::unique_ptr<AZ::Entity> m_entity;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_meshComponentDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_editorMeshComponentDescriptor;
        IntersectionNotificationDetector m_intersectionNotificationDetector;
    };

    TEST_F(MeshComponentControllerFixture, IntersectionNotificationBusIsNotifiedWhenMeshComponentControllerTransformIsModified)
    {
        // suppress warning when feature process is not created in test environment
        UnitTest::ErrorHandler featureProcessorNotFound("Unable to find a MeshFeatureProcessorInterface on the entityId.");

        m_entity->Deactivate();
        m_entity->CreateComponent<AZ::Render::EditorMeshComponent>();
        m_entity->Activate();

        AZ::TransformBus::Event(
            m_entity->GetId(), &AZ::TransformBus::Events::SetWorldTM, AZ::Transform::CreateTranslation(AZ::Vector3(1.0f, 2.0f, 3.0f)));

        using ::testing::Eq;
        EXPECT_THAT(m_entity->GetId(), Eq(m_intersectionNotificationDetector.m_lastEntityIdChanged));
    }

    TEST_F(MeshComponentControllerFixture, RenderScaleIsOneUntilTransform2GivesTheMeshTheSign)
    {
        // suppress warning when feature process is not created in test environment
        UnitTest::ErrorHandler featureProcessorNotFound("Unable to find a MeshFeatureProcessorInterface on the entityId.");

        m_entity->Deactivate();
        m_entity->CreateComponent<AZ::Render::EditorMeshComponent>();
        m_entity->Activate();

        // A mesh of a Transform 1 entity is drawn with the positive scale of the entity, which is one with no non-uniform scale
        AZ::Vector3 renderScale = AZ::Vector3::CreateZero();
        AZ::Render::MeshComponentRequestBus::EventResult(
            renderScale, m_entity->GetId(), &AZ::Render::MeshComponentRequestBus::Events::GetRenderScale);
        EXPECT_TRUE(renderScale.IsClose(AZ::Vector3::CreateOne()));

        // Transform 2 gives the mesh its signed scale through the mesh handle state bus, and readers get it back from the mesh component
        AZ::Render::MeshHandleStateRequestBus::Event(
            m_entity->GetId(), &AZ::Render::MeshHandleStateRequestBus::Events::SetRenderScale, AZ::Vector3(-1.0f, 2.0f, 1.0f));
        AZ::Render::MeshComponentRequestBus::EventResult(
            renderScale, m_entity->GetId(), &AZ::Render::MeshComponentRequestBus::Events::GetRenderScale);
        EXPECT_TRUE(renderScale.IsClose(AZ::Vector3(-1.0f, 2.0f, 1.0f)));

        // And it goes back to the positive scale when the entity leaves Transform 2
        AZ::Render::MeshHandleStateRequestBus::Event(
            m_entity->GetId(), &AZ::Render::MeshHandleStateRequestBus::Events::SetRenderScale, AZ::Vector3::CreateOne());
        AZ::Render::MeshComponentRequestBus::EventResult(
            renderScale, m_entity->GetId(), &AZ::Render::MeshComponentRequestBus::Events::GetRenderScale);
        EXPECT_TRUE(renderScale.IsClose(AZ::Vector3::CreateOne()));
    }
} // namespace UnitTest
