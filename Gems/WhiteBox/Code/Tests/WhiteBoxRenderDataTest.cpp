/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "WhiteBoxTestFixtures.h"
#include "Rendering/WhiteBoxRenderDataUtil.h"

#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>
#include <vector>

namespace UnitTest
{
    using namespace WhiteBox;

    FaceTestData NonDegenerateFaceList = {
        {// Tri 0: non-degenerate
         AZ::Vector3{0.0f, 1.0f, 0.0f}, AZ::Vector3{1.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 1: non-degenerate
         AZ::Vector3{1.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 1.0f, 0.0f},

         // Tri 2: non-degenerate
         AZ::Vector3{0.0f, 0.0f, 1.0f}, AZ::Vector3{0.0f, 1.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 3: non-degenerate
         AZ::Vector3{0.0f, 1.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 1.0f}},
        0};

    FaceTestData DegenerateFaceList = {
        {// Tri 0: degenerate
         AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{1.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 1: degenerate
         AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 2: degenerate
         AZ::Vector3{0.0f, 0.0f, 1.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 3: degenerate
         AZ::Vector3{0.0f, 0.0f, 1.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 1.0f}},
        4};

    FaceTestData DegenerateAndNonDegenerateFaceList = {
        {// Tri 0: degenerate
         AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{1.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 1: non-degenerate
         AZ::Vector3{0.0f, 1.0f, 0.0f}, AZ::Vector3{1.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 2: degenerate
         AZ::Vector3{0.0f, 0.0f, 1.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 3: non-degenerate
         AZ::Vector3{1.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 1.0f, 0.0f},

         // Tri 4: non-degenerate
         AZ::Vector3{0.0f, 0.0f, 1.0f}, AZ::Vector3{0.0f, 1.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f},

         // Tri 5: non-degenerate
         AZ::Vector3{0.0f, 1.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 1.0f},

         // Tri 6: degenerate
         AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}, AZ::Vector3{0.0f, 0.0f, 0.0f}},
        3};

    TEST_P(WhiteBoxVertexDataTestFixture, BuildCulledTriangleList)
    {
        // given the raw positional data
        const FaceTestData& faceData = GetParam();

        // expect the vertex data to be composed of only triangle primitives
        // (we cannot proceed any further with the test if this isn't the case)
        ASSERT_EQ(faceData.m_positions.size() % 3, 0);

        // given an input list of valid and/or degenerate triangles
        const WhiteBoxFaces inVertexData = ConstructFaceData(faceData);

        // build the output list of visible triangles from the input list
        const WhiteBoxFaces outVertexData = BuildCulledWhiteBoxFaces(inVertexData);

        const size_t numInTriangles = inVertexData.size();
        const size_t numOutTriangles = outVertexData.size();

        // expect the number of triangles in the input list to always be at least as
        // as the the number of culled triangles in the output list
        EXPECT_GE(numInTriangles, numOutTriangles);

        // expect the number of triangles culled from the input list to equal that
        // of the expected number of triangles to be culled
        EXPECT_EQ(numOutTriangles, numInTriangles - faceData.m_numCulledFaces);
    }

    INSTANTIATE_TEST_SUITE_P(
        NonDegenerateFaceList, WhiteBoxVertexDataTestFixture, ::testing::Values(NonDegenerateFaceList));

    INSTANTIATE_TEST_SUITE_P(DegenerateFaceList, WhiteBoxVertexDataTestFixture, ::testing::Values(DegenerateFaceList));

    INSTANTIATE_TEST_SUITE_P(
        DegenerateAndNonDegenerateFaceList, WhiteBoxVertexDataTestFixture,
        ::testing::Values(DegenerateAndNonDegenerateFaceList));

    //////////////////////////////////////////////////////////////////////////
    // Transform 2: the visible geometry of a flipped White Box is what is drawn
    //
    // The mesh of a White Box with a negative scale axis is drawn mirrored about its entity. The visible geometry that navigation and
    // blockers read is mirrored the same way, and keeps the facing of each triangle when an odd number of axes are mirrored.

    namespace
    {
        //! Stands in for the Transform 2 of an entity: it answers the signed scale that the visible geometry asks for.
        //! The entity may sit under a parent. The world scale is then the scale of the parent times its own, which holds for the
        //! equal magnitudes and the signs these tests use.
        class MockTransform2Scale
            : public AZ::Transform2RequestBus::Handler
        {
        public:
            MockTransform2Scale(AZ::EntityId entityId, const AZ::Vector3& scale, const AZ::Vector3& parentWorldScale = AZ::Vector3::CreateOne())
                : m_scale(scale)
                , m_parentWorldScale(parentWorldScale)
            {
                AZ::Transform2RequestBus::Handler::BusConnect(entityId);
            }

            ~MockTransform2Scale() override
            {
                AZ::Transform2RequestBus::Handler::BusDisconnect();
            }

            // AZ::Transform2RequestBus overrides...
            AZ::Vector3 GetLocalScale() const override
            {
                return m_scale;
            }

            void SetLocalScale(const AZ::Vector3& scale) override
            {
                m_scale = scale;
            }

            AZ::Vector3 GetWorldScale() const override
            {
                return m_parentWorldScale * m_scale;
            }

            AZ::Vector3 m_scale;
            AZ::Vector3 m_parentWorldScale;
        };

        const AZ::EntityId FlipTestEntityId = AZ::EntityId(0x12345678);

        //! One triangle on the +X side of its entity, counter clockwise seen from +Z so that it faces +Z.
        WhiteBox::WhiteBoxRenderData MakeTriangleRenderData()
        {
            WhiteBox::WhiteBoxFace face;
            face.m_v1.m_position = AZ::Vector3(1.0f, 0.0f, 0.0f);
            face.m_v2.m_position = AZ::Vector3(2.0f, 0.0f, 0.0f);
            face.m_v3.m_position = AZ::Vector3(1.0f, 1.0f, 0.0f);
            face.m_v1.m_uv = AZ::Vector2::CreateZero();
            face.m_v2.m_uv = AZ::Vector2::CreateZero();
            face.m_v3.m_uv = AZ::Vector2::CreateZero();
            face.m_normal = AZ::Vector3::CreateAxisZ();

            WhiteBox::WhiteBoxRenderData renderData;
            renderData.m_faces.push_back(face);
            return renderData;
        }

        //! The position in the world of the vertex of the geometry that an index lists.
        AZ::Vector3 GetWorldVertex(const AzFramework::VisibleGeometry& geometry, uint32_t vertexIndex)
        {
            return geometry.m_transform * AZ::Vector3(
                geometry.m_vertices[vertexIndex * 3], geometry.m_vertices[vertexIndex * 3 + 1], geometry.m_vertices[vertexIndex * 3 + 2]);
        }

        //! The way the first triangle of the geometry faces in the world, from the order its indices list the corners in.
        AZ::Vector3 GetWorldFacing(const AzFramework::VisibleGeometry& geometry)
        {
            const AZ::Vector3 a = GetWorldVertex(geometry, geometry.m_indices[0]);
            const AZ::Vector3 b = GetWorldVertex(geometry, geometry.m_indices[1]);
            const AZ::Vector3 c = GetWorldVertex(geometry, geometry.m_indices[2]);
            return (b - a).Cross(c - a).GetNormalized();
        }
    } // namespace

    TEST_F(WhiteBoxTestFixture, VisibleGeometryWithoutATransform2_IsTheAuthoredMesh)
    {
        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[0], 0u);
        EXPECT_EQ(geometry.m_indices[1], 1u);
        EXPECT_EQ(geometry.m_indices[2], 2u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 1), IsCloseTolerance(AZ::Vector3(2.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 2), IsCloseTolerance(AZ::Vector3(1.0f, 1.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldFacing(geometry), IsCloseTolerance(AZ::Vector3::CreateAxisZ(), 1e-4f));
    }

    TEST_F(WhiteBoxTestFixture, VisibleGeometryFlippedInX_IsMirroredAndKeepsItsFacing)
    {
        MockTransform2Scale transform2(FlipTestEntityId, AZ::Vector3(-1.0f, 1.0f, 1.0f));

        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        // The triangle is drawn on the -X side, and its last two corners are listed the other way round
        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[0], 0u);
        EXPECT_EQ(geometry.m_indices[1], 2u);
        EXPECT_EQ(geometry.m_indices[2], 1u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(-1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 1), IsCloseTolerance(AZ::Vector3(-2.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 2), IsCloseTolerance(AZ::Vector3(-1.0f, 1.0f, 0.0f), 1e-4f));

        // A mirror in X does not turn a surface facing +Z around, and the order of the corners must not either
        EXPECT_THAT(GetWorldFacing(geometry), IsCloseTolerance(AZ::Vector3::CreateAxisZ(), 1e-4f));
    }

    TEST_F(WhiteBoxTestFixture, VisibleGeometryFlippedInZ_IsMirroredAboutTheEntityAndFacesTheOtherWay)
    {
        MockTransform2Scale transform2(FlipTestEntityId, AZ::Vector3(1.0f, 1.0f, -2.0f));

        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        // The triangle lies in the XY plane, which a mirror in Z leaves where it is. Only the facing of the surface is mirrored, so
        // the surface that faced +Z faces -Z, and the order of the corners is the one that lists it that way round.
        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[1], 2u);
        EXPECT_EQ(geometry.m_indices[2], 1u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldFacing(geometry), IsCloseTolerance(-AZ::Vector3::CreateAxisZ(), 1e-4f));
    }

    TEST_F(WhiteBoxTestFixture, VisibleGeometryFlippedInTwoAxes_IsAHalfTurnAndKeepsTheOrderOfTheCorners)
    {
        MockTransform2Scale transform2(FlipTestEntityId, AZ::Vector3(-1.0f, -1.0f, 1.0f));

        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[0], 0u);
        EXPECT_EQ(geometry.m_indices[1], 1u);
        EXPECT_EQ(geometry.m_indices[2], 2u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(-1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 1), IsCloseTolerance(AZ::Vector3(-2.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 2), IsCloseTolerance(AZ::Vector3(-1.0f, -1.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldFacing(geometry), IsCloseTolerance(AZ::Vector3::CreateAxisZ(), 1e-4f));
    }

    TEST_F(WhiteBoxTestFixture, VisibleGeometryWithAZeroAxis_IsNotFlipped)
    {
        MockTransform2Scale transform2(FlipTestEntityId, AZ::Vector3(0.0f, 1.0f, 1.0f));

        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[1], 1u);
        EXPECT_EQ(geometry.m_indices[2], 2u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-4f));
    }

    // A White Box on a child of a flipped entity has a scale of one of its own, and the world scale of the chain is what it is drawn with.

    TEST_F(WhiteBoxTestFixture, VisibleGeometryOfAChildOfAFlippedParent_IsMirroredByTheWorldScale)
    {
        // The child has a scale of one and its parent is flipped on X, so the world scale is (-1, 1, 1): the same drawing as the X flip.
        MockTransform2Scale transform2(FlipTestEntityId, AZ::Vector3(1.0f, 1.0f, 1.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[0], 0u);
        EXPECT_EQ(geometry.m_indices[1], 2u);
        EXPECT_EQ(geometry.m_indices[2], 1u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(-1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 1), IsCloseTolerance(AZ::Vector3(-2.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 2), IsCloseTolerance(AZ::Vector3(-1.0f, 1.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldFacing(geometry), IsCloseTolerance(AZ::Vector3::CreateAxisZ(), 1e-4f));
    }

    TEST_F(WhiteBoxTestFixture, VisibleGeometryOfAChildFlippedOnTheAxisOfItsParent_IsTheAuthoredMesh)
    {
        // The child is flipped on X by its own scale and so is its parent. The signs multiply, so the world scale is (1, 1, 1).
        MockTransform2Scale transform2(FlipTestEntityId, AZ::Vector3(-1.0f, 1.0f, 1.0f), AZ::Vector3(-1.0f, 1.0f, 1.0f));

        const AzFramework::VisibleGeometry geometry =
            WhiteBox::BuildVisibleGeometryFromWhiteBoxRenderData(FlipTestEntityId, MakeTriangleRenderData());

        ASSERT_EQ(geometry.m_indices.size(), 3u);
        EXPECT_EQ(geometry.m_indices[1], 1u);
        EXPECT_EQ(geometry.m_indices[2], 2u);
        EXPECT_THAT(GetWorldVertex(geometry, 0), IsCloseTolerance(AZ::Vector3(1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 1), IsCloseTolerance(AZ::Vector3(2.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldVertex(geometry, 2), IsCloseTolerance(AZ::Vector3(1.0f, 1.0f, 0.0f), 1e-4f));
        EXPECT_THAT(GetWorldFacing(geometry), IsCloseTolerance(AZ::Vector3::CreateAxisZ(), 1e-4f));
    }
} // namespace UnitTest
