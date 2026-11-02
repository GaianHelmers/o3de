/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Components/EditorWhiteBoxColliderComponent.h"
#include "Components/WhiteBoxColliderComponent.h"
#include "WhiteBox/EditorWhiteBoxComponentBus.h"
#include "WhiteBox/WhiteBoxToolApi.h"
#include "WhiteBoxTestFixtures.h"
#include "WhiteBoxTestUtil.h"

#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/Slice/SliceAssetHandler.h>
#include <AzQtComponents/Utilities/QtPluginPaths.h>
#include <AzTest/GemTestEnvironment.h>
#include <AzToolsFramework/Entity/EditorEntityContextComponent.h>
#include <AzToolsFramework/ToolsComponents/TransformComponent.h>

namespace UnitTest
{
    //! Stands in for the Transform 2 component, which lives in AzFramework: it provides the service and does nothing else.
    class Transform2ServiceStandIn : public AZ::Component
    {
    public:
        AZ_COMPONENT(Transform2ServiceStandIn, "{5E2C7A41-9B63-4D8F-A1C5-3F7D2B906E18}");

        static void Reflect([[maybe_unused]] AZ::ReflectContext* context)
        {
        }

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
        {
            provided.push_back(AZ_CRC_CE("Transform2Service"));
        }

        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
        {
            required.push_back(AZ_CRC_CE("TransformService"));
        }

        // AZ::Component overrides...
        void Activate() override
        {
        }

        void Deactivate() override
        {
        }
    };

    class EditorWhiteBoxPhysicsTestEnvironment : public AZ::Test::GemTestEnvironment
    {
        // AZ::Test::GemTestEnvironment overrides ...
        void AddGemsAndComponents() override;
        AZ::ComponentApplication* CreateApplicationInstance() override;
        void PostSystemEntityActivate() override;

    public:
        EditorWhiteBoxPhysicsTestEnvironment() = default;
        ~EditorWhiteBoxPhysicsTestEnvironment() override = default;
    };

    void EditorWhiteBoxPhysicsTestEnvironment::AddGemsAndComponents()
    {
        AddDynamicModulePaths({"PhysX5.Editor.Gem"});
        AddComponentDescriptors(
            {WhiteBox::EditorWhiteBoxComponent::CreateDescriptor(),
             WhiteBox::WhiteBoxComponent::CreateDescriptor(),
             WhiteBox::WhiteBoxColliderComponent::CreateDescriptor(),
             WhiteBox::EditorWhiteBoxColliderComponent::CreateDescriptor()});
        AddComponentDescriptors({Transform2ServiceStandIn::CreateDescriptor()});
    }

    AZ::ComponentApplication* EditorWhiteBoxPhysicsTestEnvironment::CreateApplicationInstance()
    {
        // Using ToolsTestApplication to have AzFramework and AzToolsFramework components.
        return aznew UnitTest::ToolsTestApplication("EditorWhiteBoxPhysics");
    }

    void EditorWhiteBoxPhysicsTestEnvironment::PostSystemEntityActivate()
    {
        AZ::UserSettingsComponentRequestBus::Broadcast(&AZ::UserSettingsComponentRequests::DisableSaveOnFinalize);
    }

    class WhiteBoxPhysicsFixture : public ::testing::Test
    {
    };

    TEST_F(WhiteBoxPhysicsFixture, EditorWhiteBoxColliderComponentCanBeAddedToAnEmptyWhiteBoxComponent)
    {
        // given
        // create an entity with a transform and editor white box component
        AZ::Entity entity;
        entity.CreateComponent<AzToolsFramework::Components::TransformComponent>();
        auto editorWhiteBoxComponent = entity.CreateComponent<WhiteBox::EditorWhiteBoxComponent>();

        entity.Init();
        entity.Activate();

        WhiteBox::WhiteBoxMesh* whiteBox = nullptr;
        WhiteBox::EditorWhiteBoxComponentRequestBus::EventResult(
            whiteBox, AZ::EntityComponentIdPair(entity.GetId(), editorWhiteBoxComponent->GetId()),
            &WhiteBox::EditorWhiteBoxComponentRequests::GetWhiteBoxMesh);

        // clear all data from the white box mesh
        WhiteBox::Api::Clear(*whiteBox);

        // error messages present in EditorWhiteBoxComponent prior to fix for empty WhiteBoxMesh
        UnitTest::ErrorHandler physxCookFailed("Failed to cook triangle mesh. Please check the data is correct");
        UnitTest::ErrorHandler colliderCookFailed("Failed to cook mesh data");
        UnitTest::ErrorHandler invalidShape("Trying to add an invalid shape");
        UnitTest::ErrorHandler invalidConfiguration("Unable to create a shape from configuration");
        UnitTest::ErrorHandler physxError("TriangleMesh::loadFromDesc: desc.isValid() failed!");

        // when
        // add an editor white box collider component
        entity.Deactivate();
        entity.CreateComponent<WhiteBox::EditorWhiteBoxColliderComponent>();
        entity.Activate();

        // then
        // ensure none of the previous error messages are reported
        EXPECT_EQ(physxCookFailed.GetWarningCount(), 0);
        EXPECT_EQ(colliderCookFailed.GetWarningCount(), 0);
        EXPECT_EQ(invalidShape.GetErrorCount(), 0);
        EXPECT_EQ(invalidConfiguration.GetErrorCount(), 0);
        EXPECT_EQ(physxError.GetErrorCount(), 0);
    }

    // The collider makes its body when it activates, and the Transform 2 listener only sees a body that is made after the
    // Transform 2 of its entity has announced the entity. So the collider has to sort behind the Transform 2. The sort puts
    // components that provide services ahead of those that provide none, which already does it for a plain entity. The
    // dependent service is what guarantees it, and the first test reads it from the descriptor.
    TEST_F(WhiteBoxPhysicsFixture, WhiteBoxColliderComponentDeclaresTheTransform2ServiceAsDependent)
    {
        AZ::ComponentDescriptor* descriptor = nullptr;
        AZ::ComponentDescriptorBus::EventResult(
            descriptor, azrtti_typeid<WhiteBox::WhiteBoxColliderComponent>(), &AZ::ComponentDescriptorBus::Events::GetDescriptor);
        ASSERT_NE(descriptor, nullptr);

        AZ::ComponentDescriptor::DependencyArrayType dependent;
        descriptor->GetDependentServices(dependent, nullptr);
        EXPECT_NE(AZStd::find(dependent.begin(), dependent.end(), AZ_CRC_CE("Transform2Service")), dependent.end());

        // Dependent, not required: an entity with a Transform 1 only has no Transform2Service, and still activates.
        AZ::ComponentDescriptor::DependencyArrayType required;
        descriptor->GetRequiredServices(required, nullptr);
        EXPECT_EQ(AZStd::find(required.begin(), required.end(), AZ_CRC_CE("Transform2Service")), required.end());
    }

    TEST_F(WhiteBoxPhysicsFixture, WhiteBoxColliderComponentIsSortedBehindTheTransform2OfItsEntity)
    {
        AZ::Entity entity;
        auto* transform = entity.CreateComponent<AzToolsFramework::Components::TransformComponent>();
        auto* collider = entity.CreateComponent<WhiteBox::WhiteBoxColliderComponent>();
        auto* transform2 = entity.CreateComponent<Transform2ServiceStandIn>();

        // Listed in the order that would sort worst: the collider first and the Transform 2 last.
        AZ::Entity::ComponentArrayType components{collider, transform2, transform};
        ASSERT_TRUE(AZ::Entity::DependencySort(components).IsSuccess());

        const auto indexOf = [&components](const AZ::Component* component)
        {
            return AZStd::distance(components.begin(), AZStd::find(components.begin(), components.end(), component));
        };
        EXPECT_LT(indexOf(transform), indexOf(transform2));
        EXPECT_LT(indexOf(transform2), indexOf(collider));
    }

    // The service is a dependency the collider can do without: an entity with a Transform 1 only has no such service, and
    // its collider is sorted the way it always was.
    TEST_F(WhiteBoxPhysicsFixture, WhiteBoxColliderComponentWithoutATransform2_IsSortedBehindTheTransformOnly)
    {
        AZ::Entity entity;
        auto* transform = entity.CreateComponent<AzToolsFramework::Components::TransformComponent>();
        auto* collider = entity.CreateComponent<WhiteBox::WhiteBoxColliderComponent>();

        AZ::Entity::ComponentArrayType components{collider, transform};
        ASSERT_TRUE(AZ::Entity::DependencySort(components).IsSuccess());
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0], transform);
        EXPECT_EQ(components[1], collider);
    }

} // namespace UnitTest

// required to support running integration tests with Qt and PhysX
AZTEST_EXPORT int AZ_UNIT_TEST_HOOK_NAME(int argc, char** argv)
{
    ::testing::InitGoogleMock(&argc, argv);
    AzQtComponents::PrepareQtPaths();
    QApplication app(argc, argv);
    AZ::Test::printUnusedParametersWarning(argc, argv);
    AZ::Test::addTestEnvironments({new UnitTest::EditorWhiteBoxPhysicsTestEnvironment()});
    int result = RUN_ALL_TESTS();
    return result;
}

IMPLEMENT_TEST_EXECUTABLE_MAIN();
