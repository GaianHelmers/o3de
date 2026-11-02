/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/Feature/Mesh/MeshCommon.h>
#include <TransformService/TransformServiceFeatureProcessor.h>

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Name/NameDictionary.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzTest/AzTest.h>

//////////////////////////////////////////////////////////////////////////
// The parts of an exact world mesh that need no scene (I.4j)
//
// MeshCommon::CreateNormalMatrix and MeshCommon::CalculateExactCullBounds are pure functions, and the transform service stores
// an exact matrix without a scene. What these cannot reach, because it needs the renderer, is in the record of the stage.

namespace UnitTest
{
    using namespace AZ;
    using namespace AZ::Render;

    namespace
    {
        // A normal of every kind: along each axis, and a few that point between them.
        const Vector3 SampleNormals[] = {
            Vector3(1.0f, 0.0f, 0.0f),  Vector3(0.0f, 1.0f, 0.0f),  Vector3(0.0f, 0.0f, 1.0f),
            Vector3(1.0f, 1.0f, 0.0f),  Vector3(-1.0f, 2.0f, 3.0f), Vector3(0.3f, -0.8f, 0.5f),
        };

        // The direction a matrix gives a normal. The shaders normalize, so only the direction counts.
        Vector3 DirectionOf(const Matrix3x4& normalMatrix, const Vector3& normal)
        {
            return normalMatrix.TransformVector(normal).GetNormalized();
        }

        // The reference: the inverse transpose of a well conditioned matrix, as the transform service builds it for a Transform 1 mesh.
        Matrix3x4 InverseTransposeOf(const Matrix3x4& objectToWorld)
        {
            return objectToWorld.GetInverseFull().GetTranspose3x3();
        }

        void ExpectSameDirections(const Matrix3x4& actual, const Matrix3x4& expected)
        {
            for (const Vector3& normal : SampleNormals)
            {
                EXPECT_TRUE(DirectionOf(actual, normal).IsClose(DirectionOf(expected, normal), 1e-4f));
            }
        }

        // The box that holds the eight corners of a box under a matrix, one corner at a time.
        Aabb BoxOfTheCorners(const Aabb& box, const Matrix3x4& objectToWorld)
        {
            Aabb result = Aabb::CreateNull();
            for (int corner = 0; corner < 8; ++corner)
            {
                const Vector3 point(
                    (corner & 1) ? box.GetMax().GetX() : box.GetMin().GetX(),
                    (corner & 2) ? box.GetMax().GetY() : box.GetMin().GetY(),
                    (corner & 4) ? box.GetMax().GetZ() : box.GetMin().GetZ());
                result.AddPoint(objectToWorld.TransformPoint(point));
            }
            return result;
        }

        Matrix3x4 Shear()
        {
            // x gains a copy of y: the basis vectors are not orthogonal.
            return Matrix3x4::CreateFromColumns(
                Vector3(1.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector3(4.0f, 5.0f, 6.0f));
        }

        Matrix3x4 RotatedMirroredAndSheared()
        {
            const Matrix3x4 rotation =
                Matrix3x4::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(Vector3(1.0f, 2.0f, 3.0f).GetNormalized(), 0.7f));
            return rotation * Matrix3x4::CreateScale(Vector3(-2.0f, 0.5f, 3.0f)) * Shear();
        }

        // The best fit pair of a Transform 2 mesh: a rotation of 0.5 radian about z at a place, with a scale per axis.
        Transform BestFitRigid(const Vector3& place)
        {
            return Transform::CreateFromQuaternionAndTranslation(Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), 0.5f), place);
        }

        // The matrix of that pair worked out by hand: each column of the rotation times the scale of its axis, then the place.
        // cos(0.5) = 0.87758256 and sin(0.5) = 0.47942554.
        Matrix3x4 BestFitMatrixByHand(const Vector3& scale, const Vector3& place)
        {
            constexpr float cosine = 0.87758256f;
            constexpr float sine = 0.47942554f;
            return Matrix3x4::CreateFromColumns(
                Vector3(cosine, sine, 0.0f) * scale.GetX(), Vector3(-sine, cosine, 0.0f) * scale.GetY(), Vector3(0.0f, 0.0f, scale.GetZ()), place);
        }

        // What the ray tracing instance is built from: the transform and the scale per axis it was given.
        Matrix3x4 MatrixOfTheBestFit(const Transform& transform, const Vector3& nonUniformScale)
        {
            Matrix3x4 result = Matrix3x4::CreateFromTransform(transform);
            result.MultiplyByScale(nonUniformScale);
            return result;
        }
    } // namespace

    //////////////////////////////////////////////////////////////////////////
    // MeshCommon::CreateNormalMatrix

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_Identity_IsIdentity)
    {
        EXPECT_TRUE(MeshCommon::CreateNormalMatrix(Matrix3x4::CreateIdentity()).IsClose(Matrix3x4::CreateIdentity()));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_UniformScale_KeepsEveryDirection)
    {
        const Matrix3x4 normalMatrix = MeshCommon::CreateNormalMatrix(Matrix3x4::CreateScale(Vector3(3.0f)));

        ExpectSameDirections(normalMatrix, Matrix3x4::CreateIdentity());
        // The largest element is one, whatever the scale was.
        EXPECT_NEAR(normalMatrix(0, 0), 1.0f, 1e-6f);
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_NonUniformScaleOnARotation_MatchesTheInverseTranspose)
    {
        const Matrix3x4 objectToWorld =
            Matrix3x4::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), 0.9f)) *
            Matrix3x4::CreateScale(Vector3(2.0f, 1.0f, 0.5f));

        ExpectSameDirections(MeshCommon::CreateNormalMatrix(objectToWorld), InverseTransposeOf(objectToWorld));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_Mirror_KeepsTheSideOfTheSurface)
    {
        // One negative axis reverses the determinant. A cofactor matrix alone would turn every normal around.
        const Matrix3x4 objectToWorld = Matrix3x4::CreateScale(Vector3(-2.0f, 1.0f, 1.0f));
        const Matrix3x4 normalMatrix = MeshCommon::CreateNormalMatrix(objectToWorld);

        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(1.0f, 0.0f, 0.0f)).IsClose(Vector3(-1.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(0.0f, 1.0f, 0.0f)).IsClose(Vector3(0.0f, 1.0f, 0.0f)));
        ExpectSameDirections(normalMatrix, InverseTransposeOf(objectToWorld));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_RotatedMirror_MatchesTheInverseTranspose)
    {
        const Matrix3x4 objectToWorld =
            Matrix3x4::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(Vector3(1.0f, 1.0f, 0.0f).GetNormalized(), 1.1f)) *
            Matrix3x4::CreateScale(Vector3(1.0f, -3.0f, 2.0f));

        ExpectSameDirections(MeshCommon::CreateNormalMatrix(objectToWorld), InverseTransposeOf(objectToWorld));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_Shear_MatchesTheInverseTranspose)
    {
        ExpectSameDirections(MeshCommon::CreateNormalMatrix(Shear()), InverseTransposeOf(Shear()));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_RotatedMirroredShear_MatchesTheInverseTranspose)
    {
        ExpectSameDirections(MeshCommon::CreateNormalMatrix(RotatedMirroredAndSheared()), InverseTransposeOf(RotatedMirroredAndSheared()));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_CollapsedAxis_IsFiniteAndFollowsTheFlatSurface)
    {
        // The determinant is far below FLT_EPSILON, where GetInverseFull gives the identity and a warning.
        // A surface flattened onto the xy plane has the z axis for its normal, whatever normal the mesh had.
        const Matrix3x4 normalMatrix = MeshCommon::CreateNormalMatrix(Matrix3x4::CreateScale(Vector3(1.0f, 1.0f, 1e-8f)));

        for (int row = 0; row < 3; ++row)
        {
            EXPECT_TRUE(normalMatrix.GetRow(row).IsFinite());
        }
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(0.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, 0.0f, 1.0f), 1e-6f));
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(1.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, 0.0f, 1.0f), 1e-6f));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_RotatedFlatSheet_FacesTheWayTheSheetIsTurned)
    {
        // A sheet flattened along z to the floor of the render scale (stand-in 0.4, so 0.4 by 0.4 by 4e-7), turned a quarter about x.
        // The determinant is 6.4e-8, under FLT_EPSILON, where GetInverseFull gives the identity and the sheet would be lit with the
        // normals it had before it was turned. The columns are those of the rotation times the scale: x stays, y goes to z, z goes to -y.
        const Matrix3x4 objectToWorld = Matrix3x4::CreateFromColumns(
            Vector3(0.4f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.4f), Vector3(0.0f, -4e-7f, 0.0f), Vector3::CreateZero());

        const Matrix3x4 normalMatrix = MeshCommon::CreateNormalMatrix(objectToWorld);

        // The sheet lies in the xz plane now and faces -y, whatever normal the mesh had.
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(0.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, -1.0f, 0.0f), 1e-4f));
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(1.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, -1.0f, 0.0f), 1e-4f));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_MirroredSheetWithAZeroAxis_KeepsTheSideOnTheFloorOfTheRenderScale)
    {
        // Scale (-0.4, 0.4, 0). The mesh listener sends the best fit (stand-in 0.4) times the render scale (-1, 1, 1e-6), so the
        // matrix is 0.4 times that and its determinant is negative. The mirror keeps the sheet facing +z.
        Matrix3x4 onTheFloor = Matrix3x4::CreateScale(Vector3(0.4f));
        onTheFloor.MultiplyByScale(Vector3(-1.0f, 1.0f, 1e-6f));
        EXPECT_TRUE(DirectionOf(MeshCommon::CreateNormalMatrix(onTheFloor), Vector3(0.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, 0.0f, 1.0f), 1e-4f));

        // The true matrix has a zero column, and a zero determinant has no sign to read, which is why the listener does not send it.
        const Matrix3x4 trueMatrix = Matrix3x4::CreateScale(Vector3(-0.4f, 0.4f, 0.0f));
        EXPECT_TRUE(DirectionOf(MeshCommon::CreateNormalMatrix(trueMatrix), Vector3(0.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, 0.0f, -1.0f), 1e-4f));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_TinyMirror_KeepsTheSideOfTheSurface)
    {
        // A determinant of -6.4e-8 is under FLT_EPSILON as well, and the mirror has to turn the normal around on x only.
        const Matrix3x4 normalMatrix = MeshCommon::CreateNormalMatrix(Matrix3x4::CreateScale(Vector3(-0.004f, 0.004f, 0.004f)));

        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(1.0f, 0.0f, 0.0f)).IsClose(Vector3(-1.0f, 0.0f, 0.0f), 1e-4f));
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(0.0f, 1.0f, 0.0f)).IsClose(Vector3(0.0f, 1.0f, 0.0f), 1e-4f));
        EXPECT_TRUE(DirectionOf(normalMatrix, Vector3(0.0f, 0.0f, 1.0f)).IsClose(Vector3(0.0f, 0.0f, 1.0f), 1e-4f));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_TinyAndHugeUniformScale_StaysUsable)
    {
        // The cross products of the raw basis would underflow or overflow here. The basis is divided by its largest element first.
        EXPECT_TRUE(MeshCommon::CreateNormalMatrix(Matrix3x4::CreateScale(Vector3(1e-20f))).IsClose(Matrix3x4::CreateIdentity()));
        EXPECT_TRUE(MeshCommon::CreateNormalMatrix(Matrix3x4::CreateScale(Vector3(1e20f))).IsClose(Matrix3x4::CreateIdentity()));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_NoNormalAtAll_IsIdentity)
    {
        // All zero, and every basis vector on one line (nothing but a line is left, so no plane has a normal).
        EXPECT_TRUE(MeshCommon::CreateNormalMatrix(Matrix3x4::CreateZero()).IsClose(Matrix3x4::CreateIdentity()));

        const Matrix3x4 onOneLine = Matrix3x4::CreateFromColumns(
            Vector3(1.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f), Vector3(-3.0f, 0.0f, 0.0f), Vector3::CreateZero());
        EXPECT_TRUE(MeshCommon::CreateNormalMatrix(onOneLine).IsClose(Matrix3x4::CreateIdentity()));
    }

    TEST(MeshCommonExactWorldTest, CreateNormalMatrix_HasNoTranslation)
    {
        const Matrix3x4 normalMatrix = MeshCommon::CreateNormalMatrix(Shear());

        EXPECT_TRUE(normalMatrix.GetTranslation().IsZero());
    }

    //////////////////////////////////////////////////////////////////////////
    // MeshCommon::CalculateExactCullBounds

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_Identity_IsTheLocalBox)
    {
        const Aabb local = Aabb::CreateFromMinMax(Vector3(-1.0f, -2.0f, -3.0f), Vector3(1.0f, 2.0f, 3.0f));

        const MeshCommon::ExactCullBounds bounds = MeshCommon::CalculateExactCullBounds(local, Matrix3x4::CreateIdentity());

        EXPECT_TRUE(bounds.m_aabb.IsClose(local));
        EXPECT_TRUE(bounds.m_sphere.GetCenter().IsClose(Vector3::CreateZero()));
        EXPECT_NEAR(bounds.m_sphere.GetRadius(), Vector3(1.0f, 2.0f, 3.0f).GetLength(), 1e-5f);
        EXPECT_TRUE(bounds.m_obb.GetHalfLengths().IsClose(Vector3(1.0f, 2.0f, 3.0f)));
        EXPECT_NEAR(bounds.m_lodSelectionRadius, 3.0f, 1e-6f);
    }

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_Mirror_GivesAnOrderedBoxAtTheMirroredPlace)
    {
        // Every axis reads back negative from a mirror, which turns a box inside out when it is scaled before it is moved.
        const Aabb local = Aabb::CreateFromMinMax(Vector3(1.0f, 0.0f, 0.0f), Vector3(3.0f, 2.0f, 1.0f));
        const Matrix3x4 mirror = Matrix3x4::CreateFromColumns(
            Vector3(-2.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector3(10.0f, 0.0f, 0.0f));

        const MeshCommon::ExactCullBounds bounds = MeshCommon::CalculateExactCullBounds(local, mirror);

        EXPECT_TRUE(bounds.m_aabb.IsValid());
        EXPECT_TRUE(bounds.m_aabb.GetMin().IsClose(Vector3(4.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(bounds.m_aabb.GetMax().IsClose(Vector3(8.0f, 2.0f, 1.0f)));
        EXPECT_TRUE(bounds.m_obb.GetHalfLengths().IsClose(Vector3(2.0f, 1.0f, 0.5f)));
    }

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_Shear_HoldsEveryCornerOfTheBox)
    {
        const Aabb local = Aabb::CreateFromMinMax(Vector3(-1.0f), Vector3(1.0f));

        const MeshCommon::ExactCullBounds bounds = MeshCommon::CalculateExactCullBounds(local, Shear());

        // x gains y, so x runs from -2 to 2 around the translation (4, 5, 6).
        EXPECT_TRUE(bounds.m_aabb.GetMin().IsClose(Vector3(2.0f, 4.0f, 5.0f)));
        EXPECT_TRUE(bounds.m_aabb.GetMax().IsClose(Vector3(6.0f, 6.0f, 7.0f)));

        const Aabb corners = BoxOfTheCorners(local, Shear());
        EXPECT_TRUE(bounds.m_aabb.IsClose(corners));
    }

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_RotatedMirroredShear_MatchesTheCornersAndTheSphereHoldsThem)
    {
        const Aabb local = Aabb::CreateFromMinMax(Vector3(-1.0f, -0.5f, -2.0f), Vector3(3.0f, 1.5f, 1.0f));
        const Matrix3x4 objectToWorld = RotatedMirroredAndSheared();

        const MeshCommon::ExactCullBounds bounds = MeshCommon::CalculateExactCullBounds(local, objectToWorld);

        EXPECT_TRUE(bounds.m_aabb.IsValid());
        EXPECT_TRUE(bounds.m_aabb.IsClose(BoxOfTheCorners(local, objectToWorld), 1e-4f));

        // The sphere and the Obb come from the world box, so they hold what the box holds.
        const Aabb corners = BoxOfTheCorners(local, objectToWorld);
        EXPECT_TRUE(bounds.m_sphere.GetCenter().IsClose(corners.GetCenter(), 1e-4f));
        EXPECT_GE(bounds.m_sphere.GetRadius() + 1e-4f, (corners.GetMax() - corners.GetCenter()).GetLength());
        EXPECT_TRUE(bounds.m_obb.GetPosition().IsClose(corners.GetCenter(), 1e-4f));
        EXPECT_TRUE(bounds.m_obb.GetHalfLengths().IsClose(corners.GetExtents() * 0.5f, 1e-4f));
        EXPECT_GE(bounds.m_obb.GetHalfLengths().GetMinElement(), 0.0f);
    }

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_LodRadius_UsesTheLengthOfEachBasisVectorWithoutTheSign)
    {
        const Aabb local = Aabb::CreateFromMinMax(Vector3(-1.0f, -2.0f, -3.0f), Vector3(1.0f, 2.0f, 3.0f));
        // A mirror: Matrix3x4::RetrieveScale would give all three of these a negative sign.
        const Matrix3x4 mirror = Matrix3x4::CreateScale(Vector3(-3.0f, 2.0f, 1.0f));

        const MeshCommon::ExactCullBounds bounds = MeshCommon::CalculateExactCullBounds(local, mirror);

        // The extents (2, 4, 6) times the lengths (3, 2, 1) are (6, 8, 6): the longest side is 8, on y.
        EXPECT_NEAR(bounds.m_lodSelectionRadius, 4.0f, 1e-5f);
    }

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_LodRadius_IsWhatTheRigidPathGivesWithoutShearOrMirror)
    {
        // The pair the transform service reads back for a Transform 1 mesh: a rigid transform and the lengths of the basis vectors.
        const Aabb local = Aabb::CreateFromMinMax(Vector3(-1.0f, -2.0f, -3.0f), Vector3(2.0f, 1.0f, 0.5f));
        const Matrix3x4 objectToWorld =
            Matrix3x4::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 0.4f)) *
            Matrix3x4::CreateScale(Vector3(2.0f, 0.5f, 1.5f));

        Aabb scaled = local;
        scaled.MultiplyByScale(objectToWorld.RetrieveScale());

        EXPECT_NEAR(
            MeshCommon::CalculateExactCullBounds(local, objectToWorld).m_lodSelectionRadius,
            0.5f * scaled.GetExtents().GetMaxElement(),
            1e-5f);
    }

    TEST(MeshCommonExactWorldTest, CalculateExactCullBounds_Shear_LodRadiusFollowsTheLengthOfTheSlantedAxis)
    {
        // The basis vector (1, 1, 0) is sqrt(2) long, so the extents (2, 2, 2) become (2, 2.83, 2).
        const Aabb local = Aabb::CreateFromMinMax(Vector3(-1.0f), Vector3(1.0f));

        const MeshCommon::ExactCullBounds bounds = MeshCommon::CalculateExactCullBounds(local, Shear());

        EXPECT_NEAR(bounds.m_lodSelectionRadius, 0.5f * 2.0f * Sqrt(2.0f), 1e-5f);
    }

    //////////////////////////////////////////////////////////////////////////
    // TransformServiceFeatureProcessor::SetMatrixForId and GetMatrixForId
    //
    // Not activated, so there is no scene and no buffer: only the arrays on the processor, which is all the matrix path touches.
    // The processor holds shader input names, so a name dictionary has to exist while it does.

    class ExactWorldTransformServiceTest
        : public LeakDetectionFixture
    {
    protected:
        void SetUp() override
        {
            LeakDetectionFixture::SetUp();
            if (!NameDictionary::IsReady())
            {
                NameDictionary::Create();
                m_createdNameDictionary = true;
            }
            m_transformService = AZStd::make_unique<TransformServiceFeatureProcessor>();
        }

        void TearDown() override
        {
            m_transformService.reset();
            if (m_createdNameDictionary)
            {
                NameDictionary::Destroy();
            }
            LeakDetectionFixture::TearDown();
        }

        AZStd::unique_ptr<TransformServiceFeatureProcessor> m_transformService;
        bool m_createdNameDictionary = false;
    };

    TEST_F(ExactWorldTransformServiceTest, SetMatrixForId_ShearedMatrix_ReadsBackExactly)
    {
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();

        m_transformService->SetMatrixForId(id, RotatedMirroredAndSheared());

        EXPECT_TRUE(m_transformService->GetMatrixForId(id).IsClose(RotatedMirroredAndSheared(), 1e-6f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, SetMatrixForId_AfterSetTransformForId_ReplacesTheBestFit)
    {
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, Transform::CreateTranslation(Vector3(1.0f, 2.0f, 3.0f)), Vector3(1.0f));
        EXPECT_TRUE(m_transformService->GetMatrixForId(id).GetTranslation().IsClose(Vector3(1.0f, 2.0f, 3.0f)));

        m_transformService->SetMatrixForId(id, Shear());

        EXPECT_TRUE(m_transformService->GetMatrixForId(id).IsClose(Shear(), 1e-6f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, GetMatrixForId_AfterSetTransformForId_IsTheMatrixOfThePair)
    {
        // A mirror written as a rigid transform and a scale per axis is what T2 sends first, before the exact matrix.
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        const Transform rigid = Transform::CreateFromQuaternionAndTranslation(
            Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), 0.5f), Vector3(1.0f, 2.0f, 3.0f));
        const Vector3 scale(-2.0f, 2.0f, 2.0f);

        m_transformService->SetTransformForId(id, rigid, scale);

        Matrix3x4 expected = Matrix3x4::CreateFromTransform(rigid);
        expected.MultiplyByScale(scale);
        EXPECT_TRUE(m_transformService->GetMatrixForId(id).IsClose(expected, 1e-6f));
        m_transformService->ReleaseObjectId(id);
    }

    //////////////////////////////////////////////////////////////////////////
    // TransformServiceFeatureProcessor::GetBestFitForId
    //
    // The pair that ray tracing keeps for a sheared mesh. SetMatrixForId takes it from the entry SetTransformForId wrote just before.

    TEST_F(ExactWorldTransformServiceTest, GetBestFitForId_WithoutAnExactMatrix_HasNone)
    {
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(Vector3(1.0f, 2.0f, 3.0f)), Vector3(2.0f, 3.0f, 4.0f));

        Transform transform = Transform::CreateTranslation(Vector3(9.0f, 9.0f, 9.0f));
        Vector3 nonUniformScale(7.0f);
        EXPECT_FALSE(m_transformService->GetBestFitForId(id, transform, nonUniformScale));

        // The outputs are left as they were.
        EXPECT_TRUE(transform.GetTranslation().IsClose(Vector3(9.0f, 9.0f, 9.0f)));
        EXPECT_TRUE(nonUniformScale.IsClose(Vector3(7.0f)));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, GetBestFitForId_AfterSetMatrixForId_IsThePairSetTransformForIdWrote)
    {
        const Vector3 place(1.0f, 2.0f, 3.0f);
        const Vector3 scale(2.0f, 3.0f, 4.0f);
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(place), scale);

        m_transformService->SetMatrixForId(id, Shear());

        Transform transform = Transform::CreateIdentity();
        Vector3 nonUniformScale = Vector3::CreateOne();
        ASSERT_TRUE(m_transformService->GetBestFitForId(id, transform, nonUniformScale));
        EXPECT_TRUE(transform.GetTranslation().IsClose(place, 1e-5f));
        EXPECT_NEAR(transform.GetUniformScale(), 1.0f, 1e-6f);
        EXPECT_TRUE(nonUniformScale.IsClose(scale, 1e-5f));
        EXPECT_TRUE(MatrixOfTheBestFit(transform, nonUniformScale).IsClose(BestFitMatrixByHand(scale, place), 1e-5f));

        // The entry itself is the exact matrix, which is what the shaders and the culling bounds read.
        EXPECT_TRUE(m_transformService->GetMatrixForId(id).IsClose(Shear(), 1e-6f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, GetBestFitForId_OfAMirror_BuildsTheMirroredMatrix)
    {
        // The signed scale of a mirror is what T2 hands SetTransformForId. The pair read back has to build the same mirror.
        const Vector3 place(1.0f, 2.0f, 3.0f);
        const Vector3 scale(-2.0f, 2.0f, 2.0f);
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(place), scale);

        m_transformService->SetMatrixForId(id, Shear());

        Transform transform = Transform::CreateIdentity();
        Vector3 nonUniformScale = Vector3::CreateOne();
        ASSERT_TRUE(m_transformService->GetBestFitForId(id, transform, nonUniformScale));
        EXPECT_TRUE(MatrixOfTheBestFit(transform, nonUniformScale).IsClose(BestFitMatrixByHand(scale, place), 1e-5f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, GetBestFitForId_SetMatrixForIdTwiceInARow_KeepsTheBestFitOfTheFirst)
    {
        // The second call finds the exact matrix of the first in the entry, and that is no best fit.
        const Vector3 place(1.0f, 2.0f, 3.0f);
        const Vector3 scale(2.0f, 3.0f, 4.0f);
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(place), scale);

        m_transformService->SetMatrixForId(id, Shear());
        m_transformService->SetMatrixForId(id, RotatedMirroredAndSheared());

        Transform transform = Transform::CreateIdentity();
        Vector3 nonUniformScale = Vector3::CreateOne();
        ASSERT_TRUE(m_transformService->GetBestFitForId(id, transform, nonUniformScale));
        EXPECT_TRUE(MatrixOfTheBestFit(transform, nonUniformScale).IsClose(BestFitMatrixByHand(scale, place), 1e-5f));
        EXPECT_TRUE(m_transformService->GetMatrixForId(id).IsClose(RotatedMirroredAndSheared(), 1e-6f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, GetBestFitForId_AfterANewSetTransformForId_IsTheNewPair)
    {
        // The exact matrix is the same both times, as it is for a mesh that moves while its shear stays. Only the entry tells.
        const Vector3 firstPlace(1.0f, 2.0f, 3.0f);
        const Vector3 firstScale(2.0f, 3.0f, 4.0f);
        const Vector3 secondPlace(7.0f, 8.0f, 9.0f);
        const Vector3 secondScale(1.0f, 5.0f, 2.0f);
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(firstPlace), firstScale);
        m_transformService->SetMatrixForId(id, Shear());

        m_transformService->SetTransformForId(id, BestFitRigid(secondPlace), secondScale);
        m_transformService->SetMatrixForId(id, Shear());

        Transform transform = Transform::CreateIdentity();
        Vector3 nonUniformScale = Vector3::CreateOne();
        ASSERT_TRUE(m_transformService->GetBestFitForId(id, transform, nonUniformScale));
        EXPECT_TRUE(MatrixOfTheBestFit(transform, nonUniformScale).IsClose(BestFitMatrixByHand(secondScale, secondPlace), 1e-5f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, ClearBestFitForId_DropsTheBestFitAndLeavesTheEntry)
    {
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(Vector3(1.0f, 2.0f, 3.0f)), Vector3(2.0f, 3.0f, 4.0f));
        m_transformService->SetMatrixForId(id, Shear());

        m_transformService->ClearBestFitForId(id);

        Transform transform = Transform::CreateIdentity();
        Vector3 nonUniformScale = Vector3::CreateOne();
        EXPECT_FALSE(m_transformService->GetBestFitForId(id, transform, nonUniformScale));
        EXPECT_TRUE(m_transformService->GetMatrixForId(id).IsClose(Shear(), 1e-6f));
        m_transformService->ReleaseObjectId(id);
    }

    TEST_F(ExactWorldTransformServiceTest, ReleaseObjectId_DropsTheBestFitOfTheSlot)
    {
        TransformServiceFeatureProcessor::ObjectId id = m_transformService->ReserveObjectId();
        m_transformService->SetTransformForId(id, BestFitRigid(Vector3(1.0f, 2.0f, 3.0f)), Vector3(2.0f, 3.0f, 4.0f));
        m_transformService->SetMatrixForId(id, Shear());
        const uint32_t index = id.GetIndex();
        m_transformService->ReleaseObjectId(id);

        // The slot is reused by the next object, which must not inherit the best fit.
        TransformServiceFeatureProcessor::ObjectId reused = m_transformService->ReserveObjectId();
        ASSERT_EQ(reused.GetIndex(), index);
        m_transformService->SetTransformForId(reused, Transform::CreateIdentity(), Vector3::CreateOne());

        Transform transform = Transform::CreateIdentity();
        Vector3 nonUniformScale = Vector3::CreateOne();
        EXPECT_FALSE(m_transformService->GetBestFitForId(reused, transform, nonUniformScale));
        m_transformService->ReleaseObjectId(reused);
    }
} // namespace UnitTest
