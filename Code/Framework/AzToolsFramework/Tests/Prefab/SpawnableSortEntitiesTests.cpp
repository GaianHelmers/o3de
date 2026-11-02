/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Prefab/SpawnableSortEntitiesTestFixture.h>

#include <AzCore/Component/TransformBus.h>
#include <AzFramework/Components/Transform2Component.h>

namespace UnitTest
{
    using SpawnableSortEntitiesTests = SpawnableSortEntitiesTestFixture;


    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_0Entities_IsOk)
    {
        SortAndSanityCheck();
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_1Entity_IsOk)
    {
        AddEntity(E1);

        SortAndSanityCheck();
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_ParentAndChild_SortsCorrectly)
    {
        AddEntity(E2, E1);
        AddEntity(E1);

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_6EntitiesWith2Roots_SortsCorrectly)
    {
        // Hierarchy looks like:
        // 1
        // + 2
        //   + 3
        // 4
        // + 5
        // + 6
        // The entities are added in "random-ish" order on purpose
        AddEntity(E3, E2);
        AddEntity(E1);
        AddEntity(E6, E4);
        AddEntity(E5, E4);
        AddEntity(E2, E1);
        AddEntity(E4);

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
        EXPECT_TRUE(IsChildAfterParent(E3, E2));
        EXPECT_TRUE(IsChildAfterParent(E5, E4));
        EXPECT_TRUE(IsChildAfterParent(E6, E4));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_ParentNotFound_ChildTreatedAsRoot)
    {
        AddEntity(E1);
        AddEntity(E2, E1);
        AddEntity(E3, MissingNo); // E3's parent not found
        AddEntity(E4, E3);

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
        EXPECT_TRUE(IsChildAfterParent(E4, E2));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_NullptrEntry_RemovedFromSorted)
    {
        AddEntity(E2, E1);
        AddEntity(nullptr, false);
        AddEntity(E1);

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_DuplicateEntityId_RemovedFromSorted)
    {
        AddEntity(E2, E1);
        AddEntity(E1);
        AddEntity(E1, AZ::EntityId(), false); // duplicate EntityId

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_LoopingHierarchy_PicksAnyParentAsRoot)
    {
        // loop: E1 -> E2 -> E3 -> E1 -> ...
        AddEntity(E2, E1);
        AddEntity(E3, E2);
        AddEntity(E1, E3);

        SortAndSanityCheck();

        AZ::EntityId first = m_sorted.front()->GetId();

        if (first == E1)
        {
            EXPECT_TRUE(IsChildAfterParent(E2, E1));
            EXPECT_TRUE(IsChildAfterParent(E3, E2));
        }
        else if (first == E2)
        {
            EXPECT_TRUE(IsChildAfterParent(E3, E2));
            EXPECT_TRUE(IsChildAfterParent(E1, E3));
        }
        else // if (first == E3)
        {
            EXPECT_TRUE(IsChildAfterParent(E1, E3));
            EXPECT_TRUE(IsChildAfterParent(E2, E1));
        }
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_EntityLackingTransformComponent_IsTreatedLikeItHasNoParent)
    {
        AddEntity(E2, E1);
        AddEntity(aznew AZ::Entity(E1));

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_EntityParentedToSelf_IsTreatedLikeItHasNoParent)
    {
        AddEntity(E2, E1);
        AddEntity(E1, E1); // parented to self

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_EntityWithInvalidId_RemovedFromSorted)
    {
        AddEntity(E2, E1);
        AddEntity(E1);
        AddEntity(AZ::EntityId(), AZ::EntityId(), false); // entity using invalid ID as its own ID

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }

    // A Transform 2 derives from the Transform 1 component and has a type id of its own, so a lookup of the exact Transform 1
    // type does not find it. The sort asks for the transform interface instead.
    static AZ::Entity* MakeTransform2Entity(AZ::EntityId id, AZ::EntityId parentId, const AZ::Vector3& scale)
    {
        auto* entity = aznew AZ::Entity(id);
        AZ::TransformConfig config;
        config.m_parentId = parentId;
        entity->CreateComponent<AzFramework::Transform2Component>(scale)->SetConfiguration(config);
        return entity;
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_Transform2Entities_SortAfterTheirParents)
    {
        // Hierarchy looks like:
        // 1 (Transform 1)
        // + 2 (Transform 2, flipped on X)
        //   + 3 (Transform 2, as every child of a Transform 2 is)
        //     + 4 (Transform 1, as one attached at runtime is)
        // 5 (Transform 2, flipped on Y)
        // + 6 (Transform 2)
        // The entities are added children first on purpose.
        AddEntity(E4, E3);
        AddEntity(MakeTransform2Entity(E6, E5, AZ::Vector3(1.0f)));
        AddEntity(MakeTransform2Entity(E3, E2, AZ::Vector3(1.0f)));
        AddEntity(MakeTransform2Entity(E5, AZ::EntityId(), AZ::Vector3(1.0f, -1.0f, 1.0f)));
        AddEntity(MakeTransform2Entity(E2, E1, AZ::Vector3(-1.0f, 1.0f, 1.0f)));
        AddEntity(E1);

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
        EXPECT_TRUE(IsChildAfterParent(E3, E2));
        EXPECT_TRUE(IsChildAfterParent(E4, E3));
        EXPECT_TRUE(IsChildAfterParent(E6, E5));
    }

    TEST_F(SpawnableSortEntitiesTests, SpawnableSortEntities_Transform2Parent_IsNotTreatedLikeAnEntityWithoutATransform)
    {
        // The child is listed before its Transform 2 parent. An entity without a transform has no parent, so the order of the
        // two would be left to chance if the sort could not see the transform of the parent.
        AddEntity(E2, E1);
        AddEntity(MakeTransform2Entity(E1, AZ::EntityId(), AZ::Vector3(-1.0f, 1.0f, 1.0f)));

        SortAndSanityCheck();

        EXPECT_TRUE(IsChildAfterParent(E2, E1));
    }
}
