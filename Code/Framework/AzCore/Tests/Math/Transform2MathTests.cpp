/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

// Unit tests of the Transform 2 chain math (AzCore/Component/Transform2Math.h).
//
// These are the ports of the cases of t2_math_check.py (P1 to P18 of the I.4 tests audit), adapted to the chain rule
// of design contract 15.2. That rule REPLACES the harness's best fit (strip the authored signs, Gram-Schmidt), so a
// harness case that tested that algorithm tests the contract's rule here instead:
//   P1, P17  -> ChainWorld_IsThePlainProductOfTrsMatrices, RotationOfAMirroredParent_IsTheConjugate
//   P2       -> MirrorParity_FollowsTheSignsOfTheChain
//   P3       -> MirrorParity_SurvivesTinyScales
//   P4       -> EqualMagnitudeChains_StayConformal
//   P5       -> NonUniformParent_*
//   P6       -> REPLACED: no rotation is fitted, so there is no degenerate fit; ZeroAxisAndZeroScale_StayFinite
//   P7, P8   -> REPLACED: RotationWithoutFlippedAncestors_IsTheAuthoredProduct, OwnSignsNeverChangeTheRotation
//   P9       -> REPLACED: PositiveUniformChain_ReportsTheTransform1Product (the Transform 1 closure on its own ground)
//   P10      -> REPLACED: there is no axis choice; IdentityChildUnderFlippedParent_TakesTheParentsRotationAndMask
//   P11      -> REPLACED: the rotation under shear is the authored product; NonUniformParent_RotatedChildIsSheared
//   P12      -> SolveLocal_RoundTripsUnderMirroredAndRotatedParents
//   P13      -> ResidualSplit_StaysFiniteAndMultipliesBack
//   P14      -> InverseRelative_*
//   P15, P16 -> not AzCore: mesh bounds (Atom) and root motion (EMotionFX) belong to their own gems
//   P18      -> the tolerances: the harness f32 depth 8 matrix chain error is 1.3e-6 relative to the largest entry
//
// Every expected value comes from a matrix, quaternion or number built in the test, never from the function under test.

#include <AzCore/Component/Transform2Bus.h>
#include <AzCore/Component/Transform2Math.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Random.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/UnitTest/TestTypes.h>

namespace UnitTest
{
    namespace
    {
        namespace T2 = AZ::Transform2;
        using AZ::Matrix3x4;
        using AZ::Quaternion;
        using AZ::Transform;
        using AZ::Vector3;

        //////////////////////////////////////////////////////////////////////////
        // Tolerances

        // The largest chain, as the harness runs it.
        constexpr int MaxDepth = 8;

        // Relative to the largest entry of the expected matrix. The harness f32 matrix chain at depth 8 is 1.3e-6.
        constexpr float WorldTolerance = 5e-5f;

        // Half the angle between two rotations, in radians. The harness f32 rotation error at depth 8 is 7e-7.
        constexpr float RotationTolerance = 1e-5f;

        //////////////////////////////////////////////////////////////////////////
        // Random input

        class TestRandom
        {
        public:
            explicit TestRandom(AZ::u64 seed)
                : m_random(seed)
            {
            }

            float Range(float low, float high)
            {
                return low + (high - low) * m_random.GetRandomFloat();
            }

            float Sign()
            {
                return m_random.GetRandomFloat() < 0.5f ? -1.0f : 1.0f;
            }

            Vector3 Translation()
            {
                return Vector3(Range(-3.0f, 3.0f), Range(-3.0f, 3.0f), Range(-3.0f, 3.0f));
            }

            Quaternion Rotation()
            {
                Vector3 axis(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
                if (axis.GetLengthSq() < 0.01f)
                {
                    axis = Vector3::CreateAxisX();
                }
                return Quaternion::CreateFromAxisAngle(axis.GetNormalized(), Range(0.0f, AZ::Constants::TwoPi));
            }

            // Random signs on every axis, with the same magnitude on every axis or a magnitude per axis.
            Vector3 Scale(bool sameMagnitude)
            {
                const float magnitude = Range(0.5f, 2.0f);
                if (sameMagnitude)
                {
                    return Vector3(Sign() * magnitude, Sign() * magnitude, Sign() * magnitude);
                }
                return Vector3(Sign() * Range(0.5f, 2.0f), Sign() * Range(0.5f, 2.0f), Sign() * Range(0.5f, 2.0f));
            }

        private:
            AZ::SimpleLcgRandom m_random;
        };

        //////////////////////////////////////////////////////////////////////////
        // Chains built by hand

        struct Node
        {
            Vector3 m_translation = Vector3::CreateZero();
            Quaternion m_rotation = Quaternion::CreateIdentity();
            Vector3 m_scale = Vector3::CreateOne();
        };

        Node RandomNode(TestRandom& random, bool sameMagnitude)
        {
            Node node;
            node.m_translation = random.Translation();
            node.m_rotation = random.Rotation();
            node.m_scale = random.Scale(sameMagnitude);
            return node;
        }

        // T * R * S as three plain matrices. The function under test builds the same matrix in another way.
        Matrix3x4 LocalMatrix(const Node& node)
        {
            return Matrix3x4::CreateTranslation(node.m_translation)
                * Matrix3x4::CreateFromQuaternion(node.m_rotation)
                * Matrix3x4::CreateScale(node.m_scale);
        }

        // The world of a chain of nodes, as a plain product.
        Matrix3x4 ProductOf(const Node* nodes, int depth)
        {
            Matrix3x4 world = Matrix3x4::CreateIdentity();
            for (int i = 0; i < depth; ++i)
            {
                world = world * LocalMatrix(nodes[i]);
            }
            return world;
        }

        // The same chain through the function under test, from a root.
        T2::ChainState ComposeAll(const Node* nodes, int depth)
        {
            T2::ChainState chain = T2::ChainFromTransform(Transform::CreateIdentity());
            for (int i = 0; i < depth; ++i)
            {
                chain = T2::ComposeChain(chain, nodes[i].m_translation, nodes[i].m_rotation, nodes[i].m_scale);
            }
            return chain;
        }

        // The signs of the pattern: bit 0 negates x, bit 1 negates y, bit 2 negates z.
        Vector3 PatternSigns(int pattern)
        {
            return Vector3((pattern & 1) ? -1.0f : 1.0f, (pattern & 2) ? -1.0f : 1.0f, (pattern & 4) ? -1.0f : 1.0f);
        }

        // The signs that flip one axis.
        Vector3 FlipOn(int axis)
        {
            return PatternSigns(1 << axis);
        }

        // True when an odd number of the pattern's axes are negative, counted here and not by the code under test.
        bool IsOddPattern(const Vector3& signs)
        {
            int negative = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                negative += signs.GetElement(axis) < 0.0f ? 1 : 0;
            }
            return (negative % 2) == 1;
        }

        //////////////////////////////////////////////////////////////////////////
        // Comparisons

        // The largest entry difference over the first `columns` columns, relative to the largest expected entry.
        float RelativeDifference(const Matrix3x4& actual, const Matrix3x4& expected, int columns)
        {
            float largest = 1e-37f;
            float worst = 0.0f;
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < columns; ++column)
                {
                    largest = AZ::GetMax(largest, AZ::GetAbs(expected.GetElement(row, column)));
                    worst = AZ::GetMax(worst, AZ::GetAbs(actual.GetElement(row, column) - expected.GetElement(row, column)));
                }
            }
            return worst / largest;
        }

        // Half the angle of the rotation that takes b to a, in radians. q and -q are the same rotation.
        float RotationError(const Quaternion& a, const Quaternion& b)
        {
            return (a * b.GetInverseFull()).GetImaginary().GetLength();
        }

        bool SameVector(const Vector3& a, const Vector3& b, float tolerance)
        {
            return a.IsClose(b, tolerance);
        }

        // A vector scaled by its largest element and then normalized, so that tiny columns keep their direction.
        Vector3 UnitDirection(const Vector3& v)
        {
            const float largest = v.GetAbs().GetMaxElement();
            return largest > 0.0f ? (v / largest).GetNormalized() : Vector3::CreateZero();
        }

        // The determinant of the unit columns: +1 for a rotation, -1 for a mirror, however small the matrix is.
        float UnitDeterminant(const Matrix3x4& m)
        {
            return UnitDirection(m.GetColumn(0)).Dot(UnitDirection(m.GetColumn(1)).Cross(UnitDirection(m.GetColumn(2))));
        }

        // The largest dot product between two different unit columns. Zero means the columns are orthogonal.
        float WorstColumnDot(const Matrix3x4& m)
        {
            const Vector3 x = UnitDirection(m.GetColumn(0));
            const Vector3 y = UnitDirection(m.GetColumn(1));
            const Vector3 z = UnitDirection(m.GetColumn(2));
            return AZ::GetMax(AZ::GetAbs(x.Dot(y)), AZ::GetMax(AZ::GetAbs(x.Dot(z)), AZ::GetAbs(y.Dot(z))));
        }

        bool IsEverythingFinite(const T2::ChainState& chain)
        {
            return chain.m_world.IsFinite() && chain.m_rotation.IsFinite() && chain.m_signs.IsFinite()
                && chain.m_worldScale.IsFinite() && T2::BestFit(chain).IsFinite();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Chain: the exact world (P1, P17)

    TEST(MATH_Transform2Math, ChainWorld_IsThePlainProductOfTrsMatrices)
    {
        // Random signs, rotations and per-axis magnitudes, so shear is common. The exact world is parent * TRS
        // in every case, which a per-axis composition would get wrong as soon as a rotated node has a
        // non-uniform parent (the harness's S1, S2, S4, S5, F3, F4 and D8).
        TestRandom random(20261101);
        for (int depth = 1; depth <= MaxDepth; ++depth)
        {
            for (int trial = 0; trial < 40; ++trial)
            {
                Node nodes[MaxDepth];
                Vector3 expectedSigns = Vector3::CreateOne();
                for (int i = 0; i < depth; ++i)
                {
                    nodes[i] = RandomNode(random, false);
                    expectedSigns = expectedSigns * PatternSigns(
                        (nodes[i].m_scale.GetX() < 0.0f ? 1 : 0) | (nodes[i].m_scale.GetY() < 0.0f ? 2 : 0) | (nodes[i].m_scale.GetZ() < 0.0f ? 4 : 0));
                }

                const T2::ChainState chain = ComposeAll(nodes, depth);
                const Matrix3x4 expected = ProductOf(nodes, depth);

                EXPECT_TRUE(IsEverythingFinite(chain));
                EXPECT_LT(RelativeDifference(chain.m_world, expected, 4), WorldTolerance) << "depth " << depth << " trial " << trial;
                EXPECT_TRUE(chain.m_signs == expectedSigns) << "depth " << depth << " trial " << trial;

                // The magnitude of every world scale axis is the length of that column of the exact world
                // and its sign is the sign tracked down the chain.
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float length = expected.GetColumn(axis).GetLength();
                    EXPECT_NEAR(AZ::GetAbs(chain.m_worldScale.GetElement(axis)), length, 1e-4f * length);
                    EXPECT_EQ(chain.m_worldScale.GetElement(axis) < 0.0f, expectedSigns.GetElement(axis) < 0.0f);
                }
            }
        }
    }

    TEST(MATH_Transform2Math, RotationOfAMirroredParent_IsTheConjugate)
    {
        // A parent flipped on x and a child rotated by q: the child's rotation is the parent's times q with y and z
        // negated, (x, -y, -z, w). Dropping that conjugation is the wrong rule that the harness check 1b caught.
        TestRandom random(3);
        for (int trial = 0; trial < 50; ++trial)
        {
            Node parentNode;
            parentNode.m_rotation = random.Rotation();
            parentNode.m_scale = Vector3(-1.5f, 1.5f, 1.5f);
            Node childNode;
            childNode.m_translation = random.Translation();
            childNode.m_rotation = random.Rotation();

            const T2::ChainState parent = ComposeAll(&parentNode, 1);
            const T2::ChainState child = T2::ComposeChain(parent, childNode.m_translation, childNode.m_rotation, childNode.m_scale);

            const Quaternion q = childNode.m_rotation;
            const Quaternion mirrored(q.GetX(), -q.GetY(), -q.GetZ(), q.GetW());
            EXPECT_LT(RotationError(child.m_rotation, parentNode.m_rotation * mirrored), RotationTolerance);

            // With an odd number of flipped ancestors the rotation is not the naive product of the authored ones.
            const float naiveError = RotationError(child.m_rotation, parentNode.m_rotation * q);
            EXPECT_GT(naiveError, 0.0f);
        }
    }

    TEST(MATH_Transform2Math, RotationWithoutFlippedAncestors_IsTheAuthoredProduct)
    {
        // REPLACES harness P7. Positive uniform ancestors, depth 1 to 8, and the leaf takes each of the 8 sign
        // patterns: the rotation is the product of the authored rotations in every case, with no half turn.
        TestRandom random(5);
        for (int depth = 1; depth <= MaxDepth; ++depth)
        {
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                Node nodes[MaxDepth];
                Quaternion product = Quaternion::CreateIdentity();
                for (int i = 0; i < depth; ++i)
                {
                    nodes[i] = RandomNode(random, true);
                    nodes[i].m_scale = nodes[i].m_scale.GetAbs();
                    if (i == depth - 1)
                    {
                        nodes[i].m_scale = nodes[i].m_scale * PatternSigns(pattern);
                    }
                    product = product * nodes[i].m_rotation;
                }

                const T2::ChainState chain = ComposeAll(nodes, depth);
                EXPECT_LT(RotationError(chain.m_rotation, product), RotationTolerance) << "depth " << depth << " pattern " << pattern;
            }
        }
    }

    TEST(MATH_Transform2Math, OwnSignsNeverChangeTheRotation)
    {
        // REPLACES harness P8. Under a flipped ancestor, the 8 sign patterns of the entity's own scale
        // leave the rotation of the chain exactly as it is: the spread is zero.
        TestRandom random(9);
        for (int trial = 0; trial < 20; ++trial)
        {
            Node parentNode = RandomNode(random, true);
            parentNode.m_scale = parentNode.m_scale.GetAbs() * FlipOn(trial % 3);
            const T2::ChainState parent = ComposeAll(&parentNode, 1);

            const Vector3 translation = random.Translation();
            const Quaternion rotation = random.Rotation();
            const T2::ChainState reference = T2::ComposeChain(parent, translation, rotation, Vector3(1.25f));
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                const T2::ChainState child = T2::ComposeChain(parent, translation, rotation, Vector3(1.25f) * PatternSigns(pattern));
                EXPECT_TRUE(child.m_rotation == reference.m_rotation) << "trial " << trial << " pattern " << pattern;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Chain: flips (P2, P3, P10)

    TEST(MATH_Transform2Math, IdentityChildUnderFlippedParent_TakesTheParentsRotationAndMask)
    {
        // REPLACES harness P10: with no best fit there is no axis to choose. A child with an identity local under a
        // parent flipped on X, on Y or on Z has that parent's rotation and mask, as if it were flipped itself.
        TestRandom random(11);
        for (int axis = 0; axis < 3; ++axis)
        {
            for (int trial = 0; trial < 20; ++trial)
            {
                const float magnitude = random.Range(0.5f, 2.0f);

                Node flippedNode;
                flippedNode.m_translation = random.Translation();
                flippedNode.m_rotation = random.Rotation();
                flippedNode.m_scale = Vector3(magnitude) * FlipOn(axis);
                Node plainNode = flippedNode;
                plainNode.m_scale = Vector3(magnitude);

                const T2::ChainState flipped = ComposeAll(&flippedNode, 1);
                const T2::ChainState plain = ComposeAll(&plainNode, 1);
                const T2::ChainState child = T2::ComposeChain(flipped, Vector3::CreateZero(), Quaternion::CreateIdentity(), Vector3::CreateOne());

                // The parent's flip does not turn its rotation, and the child takes it unchanged.
                EXPECT_LT(RotationError(flipped.m_rotation, flippedNode.m_rotation), RotationTolerance);
                EXPECT_LT(RotationError(plain.m_rotation, flippedNode.m_rotation), RotationTolerance);
                EXPECT_LT(RotationError(child.m_rotation, flippedNode.m_rotation), RotationTolerance);

                // The child takes the parent's mask, scale and exact world.
                EXPECT_TRUE(child.m_signs == FlipOn(axis));
                EXPECT_TRUE(SameVector(child.m_worldScale, Vector3(magnitude) * FlipOn(axis), 1e-6f));
                EXPECT_LT(RelativeDifference(child.m_world, LocalMatrix(flippedNode), 4), WorldTolerance);
                EXPECT_TRUE(T2::IsMirrored(child.m_signs));

                // The same child flipped by itself under the unflipped parent is the same chain state.
                const T2::ChainState self = T2::ComposeChain(plain, Vector3::CreateZero(), Quaternion::CreateIdentity(), FlipOn(axis));
                EXPECT_TRUE(self.m_signs == child.m_signs);
                EXPECT_TRUE(self.m_rotation == child.m_rotation);
                EXPECT_TRUE(SameVector(self.m_worldScale, child.m_worldScale, 1e-6f));
                EXPECT_LT(RelativeDifference(self.m_world, child.m_world, 4), WorldTolerance);
            }
        }
    }

    TEST(MATH_Transform2Math, DoubleMirror_Cancels)
    {
        TestRandom random(13);
        for (int axis = 0; axis < 3; ++axis)
        {
            Node nodes[3];
            for (int i = 0; i < 3; ++i)
            {
                nodes[i] = RandomNode(random, true);
                nodes[i].m_scale = nodes[i].m_scale.GetAbs() * FlipOn(axis);
            }

            const T2::ChainState one = ComposeAll(nodes, 1);
            const T2::ChainState two = ComposeAll(nodes, 2);
            const T2::ChainState three = ComposeAll(nodes, 3);

            // One mirror mirrors, two cancel, three mirror again.
            EXPECT_TRUE(T2::IsMirrored(one.m_signs));
            EXPECT_FALSE(T2::IsMirrored(two.m_signs));
            EXPECT_TRUE(T2::IsMirrored(three.m_signs));
            EXPECT_TRUE(two.m_signs == Vector3::CreateOne());
            EXPECT_GT(two.m_worldScale.GetElement(axis), 0.0f);
            EXPECT_GT(UnitDeterminant(two.m_world), 0.0f);
            EXPECT_LT(UnitDeterminant(three.m_world), 0.0f);
            EXPECT_LT(RelativeDifference(two.m_world, ProductOf(nodes, 2), 4), WorldTolerance);
        }

        // Two different axes make a half turn, not a mirror: the signs are (-1,-1,1).
        Node x;
        x.m_scale = Vector3(-1.0f, 1.0f, 1.0f);
        Node y;
        y.m_scale = Vector3(1.0f, -1.0f, 1.0f);
        const Node xy[2] = { x, y };
        const T2::ChainState chain = ComposeAll(xy, 2);
        EXPECT_FALSE(T2::IsMirrored(chain.m_signs));
        EXPECT_TRUE(chain.m_signs == Vector3(-1.0f, -1.0f, 1.0f));
    }

    TEST(MATH_Transform2Math, MirroredParent_PutsALocalPositiveXOnTheWorldNegativeX)
    {
        // Harness check 5: a child at local +x under a parent (-s, s, s) lands at world -x.
        Node parentNode;
        parentNode.m_scale = Vector3(-2.0f, 2.0f, 2.0f);
        const T2::ChainState parent = ComposeAll(&parentNode, 1);
        const T2::ChainState child = T2::ComposeChain(parent, Vector3(5.0f, 0.0f, 0.0f), Quaternion::CreateIdentity(), Vector3::CreateOne());
        EXPECT_TRUE(SameVector(child.m_world.GetTranslation(), Vector3(-10.0f, 0.0f, 0.0f), 1e-5f));
    }

    TEST(MATH_Transform2Math, ConjugateBySigns_IsItsOwnInverseAndAProperRotation)
    {
        TestRandom random(17);
        for (int pattern = 0; pattern < 8; ++pattern)
        {
            const Vector3 signs = PatternSigns(pattern);
            const Matrix3x4 signMatrix = Matrix3x4::CreateDiagonal(signs);
            for (int trial = 0; trial < 20; ++trial)
            {
                const Quaternion q = random.Rotation();
                const Quaternion conjugated = T2::ConjugateBySigns(q, signs);
                const Quaternion back = T2::ConjugateBySigns(conjugated, signs);

                // Its own inverse, to the bit: only signs change.
                EXPECT_EQ(back.GetX(), q.GetX());
                EXPECT_EQ(back.GetY(), q.GetY());
                EXPECT_EQ(back.GetZ(), q.GetZ());
                EXPECT_EQ(back.GetW(), q.GetW());

                // A rotation of the same size, and the rotation matrix of the conjugate is D * R * D.
                EXPECT_NEAR(conjugated.GetLength(), q.GetLength(), 1e-6f);
                const Matrix3x4 rotation = Matrix3x4::CreateFromQuaternion(conjugated);
                const Matrix3x4 expected = signMatrix * Matrix3x4::CreateFromQuaternion(q) * signMatrix;
                EXPECT_LT(RelativeDifference(rotation, expected, 3), 1e-5f) << "pattern " << pattern;
                EXPECT_NEAR(rotation.GetDeterminant3x3(), 1.0f, 1e-5f) << "pattern " << pattern;
            }
        }

        // For (-1,1,1) it negates y and z.
        const Quaternion q(0.1f, 0.2f, 0.3f, 0.9f);
        const Quaternion x = T2::ConjugateBySigns(q, Vector3(-1.0f, 1.0f, 1.0f));
        EXPECT_EQ(x.GetX(), 0.1f);
        EXPECT_EQ(x.GetY(), -0.2f);
        EXPECT_EQ(x.GetZ(), -0.3f);
        EXPECT_EQ(x.GetW(), 0.9f);
    }

    TEST(MATH_Transform2Math, MirrorParity_FollowsTheSignsOfTheChain)
    {
        // Harness P2. The 8 sign patterns of a root: mirrored exactly when an odd number of axes are negative,
        // which is also the sign of the determinant of the exact world.
        for (int pattern = 0; pattern < 8; ++pattern)
        {
            Node node;
            node.m_scale = Vector3(1.5f) * PatternSigns(pattern);
            const T2::ChainState chain = ComposeAll(&node, 1);
            EXPECT_EQ(T2::IsMirrored(chain.m_signs), IsOddPattern(PatternSigns(pattern))) << "pattern " << pattern;
            EXPECT_EQ(UnitDeterminant(chain.m_world) < 0.0f, IsOddPattern(PatternSigns(pattern))) << "pattern " << pattern;
        }

        // Random chains, depth 1 to 8: the parity of the negative axes counted over all nodes.
        TestRandom random(19);
        for (int depth = 1; depth <= MaxDepth; ++depth)
        {
            for (int trial = 0; trial < 40; ++trial)
            {
                Node nodes[MaxDepth];
                int negatives = 0;
                for (int i = 0; i < depth; ++i)
                {
                    nodes[i] = RandomNode(random, false);
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        negatives += nodes[i].m_scale.GetElement(axis) < 0.0f ? 1 : 0;
                    }
                }
                const T2::ChainState chain = ComposeAll(nodes, depth);
                const bool expectedMirrored = (negatives % 2) == 1;
                EXPECT_EQ(T2::IsMirrored(chain.m_signs), expectedMirrored) << "depth " << depth << " trial " << trial;
                EXPECT_EQ(UnitDeterminant(chain.m_world) < 0.0f, expectedMirrored) << "depth " << depth << " trial " << trial;
            }
        }
    }

    TEST(MATH_Transform2Math, MirrorParity_SurvivesTinyScales)
    {
        // Harness P3 (check 2b): the determinant of a float matrix underflows below a column scale of 1e-16 and
        // loses the sign. The signs are tracked, so the parity holds down to 1e-30 and the exact world stays finite.
        float scale = 1.0f;
        for (int exponent = 1; exponent <= 30; ++exponent)
        {
            scale *= 0.1f;

            Node mirrorNode;
            mirrorNode.m_scale = Vector3(-scale, scale, scale);
            const T2::ChainState mirror = ComposeAll(&mirrorNode, 1);
            EXPECT_TRUE(T2::IsMirrored(mirror.m_signs)) << "scale " << scale;
            EXPECT_LT(UnitDeterminant(mirror.m_world), 0.0f) << "scale " << scale;
            EXPECT_TRUE(IsEverythingFinite(mirror)) << "scale " << scale;

            Node plainNode;
            plainNode.m_scale = Vector3(scale);
            const T2::ChainState plain = ComposeAll(&plainNode, 1);
            EXPECT_FALSE(T2::IsMirrored(plain.m_signs)) << "scale " << scale;
            EXPECT_GT(UnitDeterminant(plain.m_world), 0.0f) << "scale " << scale;
        }

        // A chain of 8 nodes at 0.01 has a column scale of 1e-16. Flip x on 3 of them (odd) and then on 2 (even).
        TestRandom random(23);
        for (int flips = 2; flips <= 3; ++flips)
        {
            Node nodes[MaxDepth];
            float magnitude = 1.0f;
            for (int i = 0; i < MaxDepth; ++i)
            {
                nodes[i].m_rotation = random.Rotation();
                nodes[i].m_translation = random.Translation();
                nodes[i].m_scale = Vector3(i < flips ? -0.01f : 0.01f, 0.01f, 0.01f);
                magnitude = magnitude * 0.01f;
            }
            const T2::ChainState chain = ComposeAll(nodes, MaxDepth);
            EXPECT_EQ(T2::IsMirrored(chain.m_signs), (flips % 2) == 1);
            EXPECT_EQ(UnitDeterminant(chain.m_world) < 0.0f, (flips % 2) == 1);
            EXPECT_FLOAT_EQ(AZ::GetAbs(chain.m_worldScale.GetX()), magnitude);
            EXPECT_TRUE(IsEverythingFinite(chain));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Chain: the scale it reports (D.47)

    TEST(MATH_Transform2Math, Root_ReportsItsOwnScaleBitForBit)
    {
        const Vector3 scales[] = {
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(0.1f, 0.3f, 1.7f),
            Vector3(-0.1f, 0.3f, -1.7f),
            Vector3(-2.0f, -2.0f, -2.0f),
            Vector3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f),
            Vector3(0.007f, 0.007f, 0.007f),
            Vector3(0.0f, 1.0f, 1.0f),
        };
        TestRandom random(29);
        for (const Vector3& scale : scales)
        {
            const T2::ChainState chain = T2::ComposeChain(
                T2::ChainFromTransform(Transform::CreateIdentity()), random.Translation(), random.Rotation(), scale);
            EXPECT_EQ(chain.m_worldScale.GetX(), scale.GetX());
            EXPECT_EQ(chain.m_worldScale.GetY(), scale.GetY());
            EXPECT_EQ(chain.m_worldScale.GetZ(), scale.GetZ());
        }
    }

    TEST(MATH_Transform2Math, PositiveUniformChain_ReportsTheTransform1Product)
    {
        // The chain starts at a Transform 1 parent and every node is a positive uniform scale, so the chain must
        // equal AZ::Transform composition: the world scale bit for bit, the rotation and position to float precision.
        // This is also the Transform 1 closure of harness P9, for the chain where it still holds (D.48).
        TestRandom random(31);
        for (int trial = 0; trial < 50; ++trial)
        {
            Transform t1World = Transform::CreateFromQuaternionAndTranslation(random.Rotation(), random.Translation());
            t1World.SetUniformScale(random.Range(0.5f, 2.0f));
            T2::ChainState chain = T2::ChainFromTransform(t1World);

            for (int depth = 1; depth <= MaxDepth; ++depth)
            {
                Transform local = Transform::CreateFromQuaternionAndTranslation(random.Rotation(), random.Translation());
                const float scale = random.Range(0.5f, 2.0f);
                local.SetUniformScale(scale);

                t1World = t1World * local;
                chain = T2::ComposeChain(chain, local.GetTranslation(), local.GetRotation(), Vector3(scale));

                EXPECT_EQ(chain.m_worldScale.GetX(), t1World.GetUniformScale());
                EXPECT_EQ(chain.m_worldScale.GetY(), t1World.GetUniformScale());
                EXPECT_EQ(chain.m_worldScale.GetZ(), t1World.GetUniformScale());

                const Transform bestFit = T2::BestFit(chain);
                EXPECT_EQ(bestFit.GetUniformScale(), t1World.GetUniformScale());
                EXPECT_LT(RotationError(bestFit.GetRotation(), t1World.GetRotation()), RotationTolerance);
                EXPECT_LT((bestFit.GetTranslation() - t1World.GetTranslation()).GetLength(), 2e-5f * (1.0f + t1World.GetTranslation().GetLength()));

                EXPECT_TRUE(chain.m_conformal);
                EXPECT_FALSE(chain.m_sheared);
                EXPECT_TRUE(chain.m_signs == Vector3::CreateOne());
                EXPECT_FALSE(T2::IsNonUniformEffective(chain.m_worldScale));
            }
        }
    }

    TEST(MATH_Transform2Math, BestFitTimesRenderScale_IsTheWorld_WhenNotSheared)
    {
        // Every node but the last has the same magnitude on its axes, so the chain is conformal until the last
        // node, which may be non-uniform and is still not sheared. The pair (BestFit, RenderScale) must then
        // multiply back to the exact world, which is built by hand.
        TestRandom random(37);
        for (int depth = 1; depth <= MaxDepth; ++depth)
        {
            for (int trial = 0; trial < 40; ++trial)
            {
                Node nodes[MaxDepth];
                for (int i = 0; i < depth; ++i)
                {
                    nodes[i] = RandomNode(random, i < depth - 1);
                }

                const T2::ChainState chain = ComposeAll(nodes, depth);
                EXPECT_FALSE(chain.m_sheared);

                const Matrix3x4 rebuilt =
                    Matrix3x4::CreateFromTransform(T2::BestFit(chain)) * Matrix3x4::CreateDiagonal(T2::RenderScale(chain.m_worldScale));
                EXPECT_LT(RelativeDifference(rebuilt, ProductOf(nodes, depth), 4), WorldTolerance) << "depth " << depth << " trial " << trial;
            }
        }
    }

    TEST(MATH_Transform2Math, EqualMagnitudeChains_StayConformal)
    {
        // Harness P4: equal magnitude on the axes of every node, any signs, any rotations: the columns of the
        // exact world stay orthogonal with one length, and the flags say so.
        TestRandom random(41);
        for (int depth = 1; depth <= MaxDepth; ++depth)
        {
            for (int trial = 0; trial < 40; ++trial)
            {
                Node nodes[MaxDepth];
                for (int i = 0; i < depth; ++i)
                {
                    nodes[i] = RandomNode(random, true);
                }
                const T2::ChainState chain = ComposeAll(nodes, depth);

                EXPECT_TRUE(chain.m_conformal);
                EXPECT_FALSE(chain.m_sheared);
                EXPECT_LT(WorstColumnDot(chain.m_world), 1e-5f);
                const float length = chain.m_world.GetColumn(0).GetLength();
                EXPECT_NEAR(chain.m_world.GetColumn(1).GetLength(), length, 1e-4f * length);
                EXPECT_NEAR(chain.m_world.GetColumn(2).GetLength(), length, 1e-4f * length);
                EXPECT_FALSE(T2::IsNonUniformEffective(chain.m_worldScale));
            }
        }
    }

    TEST(MATH_Transform2Math, UniformStandInBelowTheMinimum_StaysUnclamped)
    {
        // A positive uniform scale below MinTransformScale fits Transform 1 and is never clamped, so the float and
        // the render scale of the pair still multiply to the world. Four nodes of 0.2 are 0.0016.
        Node nodes[4];
        for (int i = 0; i < 4; ++i)
        {
            nodes[i].m_translation = Vector3(0.5f, -1.0f, 2.0f);
            nodes[i].m_rotation = Quaternion::CreateRotationZ(0.3f * static_cast<float>(i + 1));
            nodes[i].m_scale = Vector3(0.2f);
        }
        const T2::ChainState stacked = ComposeAll(nodes, 4);
        const Transform stackedFit = T2::BestFit(stacked);
        EXPECT_LT(stackedFit.GetUniformScale(), AZ::MinTransformScale);
        EXPECT_EQ(stackedFit.GetUniformScale(), stacked.m_worldScale.GetX());
        EXPECT_TRUE(SameVector(T2::RenderScale(stacked.m_worldScale), Vector3::CreateOne(), 1e-6f));
        EXPECT_LT(RelativeDifference(
            Matrix3x4::CreateFromTransform(stackedFit) * Matrix3x4::CreateDiagonal(T2::RenderScale(stacked.m_worldScale)),
            ProductOf(nodes, 4), 4), WorldTolerance);

        // One root of 0.005 alone.
        Node tiny;
        tiny.m_scale = Vector3(0.005f);
        const T2::ChainState root = ComposeAll(&tiny, 1);
        EXPECT_EQ(T2::BestFit(root).GetUniformScale(), 0.005f);
        EXPECT_TRUE(SameVector(T2::RenderScale(root.m_worldScale), Vector3::CreateOne(), 1e-6f));

        // A mirrored 0.005 does not fit Transform 1, so the float is clamped, and the render scale carries the rest.
        Node mirrored;
        mirrored.m_translation = Vector3(1.0f, 2.0f, 3.0f);
        mirrored.m_rotation = Quaternion::CreateRotationX(0.7f);
        mirrored.m_scale = Vector3(-0.005f, 0.005f, 0.005f);
        const T2::ChainState mirror = ComposeAll(&mirrored, 1);
        const Transform mirrorFit = T2::BestFit(mirror);
        EXPECT_EQ(mirrorFit.GetUniformScale(), AZ::MinTransformScale);
        EXPECT_LT(RelativeDifference(
            Matrix3x4::CreateFromTransform(mirrorFit) * Matrix3x4::CreateDiagonal(T2::RenderScale(mirror.m_worldScale)),
            LocalMatrix(mirrored), 4), WorldTolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Chain: a non-uniform parent (P5, P11)

    TEST(MATH_Transform2Math, NonUniformParent_RotatedChildIsSheared)
    {
        // Harness P5 and P11: a (2,1,1) parent with a child turned 45 degrees about Z. The exact world is parent * TRS
        // and is sheared: the angle between its first two columns is 126.9 degrees. The rotation of the chain is the
        // authored product (the harness's Gram-Schmidt was 36.9 degrees away from it).
        TestRandom random(43);
        for (int trial = 0; trial < 10; ++trial)
        {
            Node parentNode;
            parentNode.m_translation = random.Translation();
            parentNode.m_rotation = random.Rotation();
            parentNode.m_scale = Vector3(2.0f, 1.0f, 1.0f);
            Node childNode;
            childNode.m_translation = Vector3(1.0f, 0.0f, 0.0f);
            childNode.m_rotation = Quaternion::CreateRotationZ(AZ::DegToRad(45.0f));

            const T2::ChainState parent = ComposeAll(&parentNode, 1);
            const T2::ChainState child = T2::ComposeChain(parent, childNode.m_translation, childNode.m_rotation, childNode.m_scale);

            EXPECT_FALSE(parent.m_conformal);
            EXPECT_FALSE(parent.m_sheared);
            EXPECT_TRUE(child.m_sheared);
            EXPECT_FALSE(child.m_conformal);
            EXPECT_LT(RotationError(child.m_rotation, parentNode.m_rotation * childNode.m_rotation), RotationTolerance);

            const Node nodes[2] = { parentNode, childNode };
            const Matrix3x4 expected = ProductOf(nodes, 2);
            EXPECT_LT(RelativeDifference(child.m_world, expected, 4), WorldTolerance);

            // The cosine of the angle between the first two columns is -0.6, whatever the parent's rotation.
            const float cosine = UnitDirection(expected.GetColumn(0)).Dot(UnitDirection(expected.GetColumn(1)));
            EXPECT_NEAR(cosine, -0.6f, 1e-4f);

            // The scale is the signed column lengths: sqrt(2.5), sqrt(2.5) and 1.
            const float length = 1.5811388f;
            EXPECT_TRUE(SameVector(child.m_worldScale, Vector3(length, length, 1.0f), 1e-4f));
            EXPECT_TRUE(child.m_signs == Vector3::CreateOne());
        }
    }

    TEST(MATH_Transform2Math, NonUniformParent_UnrotatedChildIsNotSheared)
    {
        TestRandom random(47);
        Node parentNode;
        parentNode.m_rotation = random.Rotation();
        parentNode.m_scale = Vector3(2.0f, 1.0f, 1.0f);
        Node childNode;
        childNode.m_translation = Vector3(1.0f, 0.0f, 0.0f);

        const T2::ChainState parent = ComposeAll(&parentNode, 1);
        const T2::ChainState child = T2::ComposeChain(parent, childNode.m_translation, childNode.m_rotation, childNode.m_scale);

        EXPECT_FALSE(child.m_sheared);
        EXPECT_FALSE(child.m_conformal);
        EXPECT_TRUE(SameVector(child.m_worldScale, Vector3(2.0f, 1.0f, 1.0f), 1e-5f));
        EXPECT_LT(RotationError(child.m_rotation, parentNode.m_rotation), RotationTolerance);
    }

    TEST(MATH_Transform2Math, NonUniformParent_QuarterTurnChildIsNotShearedAndSwapsTheScale)
    {
        // A child turned 90 degrees keeps the columns orthogonal: the (2,1,1) lands on the second axis, and the
        // pair (BestFit, RenderScale) still multiplies to the world.
        TestRandom random(53);
        Node nodes[2];
        nodes[0].m_rotation = random.Rotation();
        nodes[0].m_scale = Vector3(2.0f, 1.0f, 1.0f);
        nodes[1].m_translation = Vector3(1.0f, 0.0f, 0.0f);
        nodes[1].m_rotation = Quaternion::CreateRotationZ(AZ::DegToRad(90.0f));

        const T2::ChainState chain = ComposeAll(nodes, 2);
        EXPECT_FALSE(chain.m_sheared);
        EXPECT_TRUE(SameVector(chain.m_worldScale, Vector3(1.0f, 2.0f, 1.0f), 1e-5f));

        const Matrix3x4 rebuilt =
            Matrix3x4::CreateFromTransform(T2::BestFit(chain)) * Matrix3x4::CreateDiagonal(T2::RenderScale(chain.m_worldScale));
        EXPECT_LT(RelativeDifference(rebuilt, ProductOf(nodes, 2), 4), WorldTolerance);
    }

    TEST(MATH_Transform2Math, NonUniformParent_ShearIsFoundOnEveryPairOfColumns)
    {
        // A parent stretched along each axis, and a child turned 45 degrees about x, y, z and a diagonal. A turn about the
        // stretched axis leaves the columns orthogonal. Any other turn shears them, and the pair of columns that are no longer
        // orthogonal depends on the two axes: (0,1), (0,2) or (1,2).
        const Vector3 stretches[] = { Vector3(2.0f, 1.0f, 1.0f), Vector3(1.0f, 2.0f, 1.0f), Vector3(1.0f, 1.0f, 2.0f) };
        const Quaternion turns[] = {
            Quaternion::CreateRotationX(AZ::Constants::QuarterPi),
            Quaternion::CreateRotationY(AZ::Constants::QuarterPi),
            Quaternion::CreateRotationZ(AZ::Constants::QuarterPi),
            Quaternion::CreateFromAxisAngle(Vector3(1.0f, 1.0f, 1.0f).GetNormalized(), AZ::Constants::QuarterPi),
        };

        int shearedCases = 0;
        for (int stretch = 0; stretch < 3; ++stretch)
        {
            for (int turn = 0; turn < 4; ++turn)
            {
                Node nodes[2];
                nodes[0].m_scale = stretches[stretch];
                nodes[1].m_translation = Vector3(1.0f, 2.0f, 3.0f);
                nodes[1].m_rotation = turns[turn];
                const T2::ChainState child = ComposeAll(nodes, 2);

                const bool expectedSheared = (turn == 3) || (turn != stretch);
                // The expectation itself, read from the plain product: some pair of unit columns is far from orthogonal, or none is.
                const float worstDot = WorstColumnDot(ProductOf(nodes, 2));
                EXPECT_TRUE(expectedSheared ? worstDot > 0.3f : worstDot < 1e-5f) << "stretch " << stretch << " turn " << turn;

                EXPECT_EQ(child.m_sheared, expectedSheared) << "stretch " << stretch << " turn " << turn;
                EXPECT_FALSE(child.m_conformal);
                shearedCases += expectedSheared ? 1 : 0;
            }
        }
        // Three stretches, each with two axis turns and the diagonal one.
        EXPECT_EQ(shearedCases, 9);
    }

    TEST(MATH_Transform2Math, NonUniformParent_ShearStraddlesTheToleranceOfOneTenThousandth)
    {
        // A (2,1,1) parent and a child turned by a small angle about z: the dot product of the first two unit columns is
        // -3 cos sin / (|c0| |c1|), which is -1.5 times the angle for a small angle, and the limit is 1e-4.
        const struct
        {
            float m_angle;
            bool m_sheared;
        } rows[] = {
            { 1e-3f, true },  // 1.5e-3
            { 1e-4f, true },  // 1.5e-4
            { 5e-5f, false }, // 7.5e-5
            { 1e-6f, false }, // 1.5e-6
        };
        for (const auto& row : rows)
        {
            Node nodes[2];
            nodes[0].m_scale = Vector3(2.0f, 1.0f, 1.0f);
            nodes[1].m_rotation = Quaternion::CreateRotationZ(row.m_angle);
            EXPECT_EQ(ComposeAll(nodes, 2).m_sheared, row.m_sheared) << "angle " << row.m_angle;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Degenerate input (P6)

    TEST(MATH_Transform2Math, ZeroAxisAndZeroScale_StayFinite)
    {
        // REPLACES harness P6, whose best fit printed det(Q) = nan on a (0,1,1) scale. Nothing is fitted here, so a
        // collapsed axis, an all-zero scale and a column scale of 1e-30 give finite values everywhere, under a root
        // and under a rotated child and grandchild.
        const Vector3 scales[] = {
            Vector3(0.0f, 1.0f, 1.0f),
            Vector3(1.0f, 0.0f, 1.0f),
            Vector3(1.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(-0.0f, 1.0f, 1.0f),
            Vector3(1e-30f, 1e-30f, 1e-30f),
            Vector3(-1e-30f, 1e-30f, 1e-30f),
            Vector3(1e-30f, 1.0f, 1.0f),
        };
        TestRandom random(59);
        for (const Vector3& scale : scales)
        {
            Node nodes[3];
            nodes[0] = RandomNode(random, true);
            nodes[0].m_scale = scale;
            nodes[1] = RandomNode(random, true);
            nodes[2] = RandomNode(random, false);

            T2::ChainState chain = T2::ChainFromTransform(Transform::CreateIdentity());
            for (int depth = 0; depth < 3; ++depth)
            {
                const T2::ChainState parent = chain;
                chain = T2::ComposeChain(parent, nodes[depth].m_translation, nodes[depth].m_rotation, nodes[depth].m_scale);

                EXPECT_TRUE(IsEverythingFinite(chain)) << "depth " << depth << " scale " << scale.GetX() << " " << scale.GetY() << " " << scale.GetZ();
                EXPECT_TRUE(T2::InverseRelative(chain.m_world).IsFinite());
                EXPECT_TRUE(T2::SolveLocalTranslation(chain, random.Translation()).IsFinite());
                EXPECT_TRUE(T2::SolveLocalRotation(chain, random.Rotation()).IsFinite());
                EXPECT_TRUE(T2::SolveLocalTranslation(parent, chain.m_world.GetTranslation()).IsFinite());
            }
        }
    }

    TEST(MATH_Transform2Math, ZeroScale_CountsAsPositiveInTheSigns)
    {
        // Zero, negative zero included, is a positive sign. A scale animated through exactly zero on one axis must not flip
        // the mask, or a subtree that is hidden with a zero scale would come back mirrored.
        const Vector3 scales[] = {
            Vector3(0.0f, 1.0f, 1.0f),
            Vector3(-0.0f, 1.0f, 1.0f),
            Vector3(0.0f, 0.0f, 0.0f),
        };
        TestRandom random(89);
        for (const Vector3& scale : scales)
        {
            const Quaternion rootRotation = random.Rotation();
            const T2::ChainState root = T2::ComposeChain(
                T2::ChainFromTransform(Transform::CreateIdentity()), random.Translation(), rootRotation, scale);
            EXPECT_TRUE(root.m_signs == Vector3::CreateOne());

            // A child keeps the mask, and its rotation is the parent's times its own with no conjugation.
            const Quaternion childRotation = random.Rotation();
            const T2::ChainState child = T2::ComposeChain(root, random.Translation(), childRotation, Vector3::CreateOne());
            EXPECT_TRUE(child.m_signs == Vector3::CreateOne());
            EXPECT_LT(RotationError(child.m_rotation, rootRotation * childRotation), RotationTolerance);
        }
    }

    TEST(MATH_Transform2Math, TinyNonConformalColumns_KeepTheirLengthsAndInvert)
    {
        // The column lengths under a non-conformal parent come from a scaled norm. A column of 3e-25 has a squared length of
        // 9e-50, below the smallest float, so a plain GetLength reads it as zero and calls the axis collapsed: the world scale
        // would be (0,0,0) and the inverse all zeros. A rotation does not change a length, so the expected lengths are the
        // scale of the root itself, known by hand. The mirrored root checks the sign as well.
        const Vector3 rootScales[] = { Vector3(3e-25f, 1e-25f, 1e-25f), Vector3(-3e-25f, 1e-25f, 1e-25f) };
        TestRandom random(83);
        for (const Vector3& rootScale : rootScales)
        {
            for (int trial = 0; trial < 10; ++trial)
            {
                const T2::ChainState root = T2::ComposeChain(
                    T2::ChainFromTransform(Transform::CreateIdentity()), random.Translation(), random.Rotation(), rootScale);
                ASSERT_FALSE(root.m_conformal);

                // An identity child under a non-conformal parent takes the column-length branch.
                const T2::ChainState child = T2::ComposeChain(root, Vector3::CreateZero(), Quaternion::CreateIdentity(), Vector3::CreateOne());
                EXPECT_TRUE(IsEverythingFinite(child));
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float expected = rootScale.GetElement(axis);
                    EXPECT_NEAR(child.m_worldScale.GetElement(axis), expected, AZ::GetAbs(expected) * 1e-4f)
                        << "axis " << axis << " trial " << trial;
                }

                const Matrix3x4 inverse = T2::InverseRelative(child.m_world);
                EXPECT_TRUE(inverse.IsFinite());
                EXPECT_LT(RelativeDifference(inverse * child.m_world, Matrix3x4::CreateIdentity(), 3), 1e-5f) << "trial " << trial;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Solving authored values (P12)

    TEST(MATH_Transform2Math, SolveLocal_RoundTripsUnderMirroredAndRotatedParents)
    {
        // For each of the 8 sign patterns on a parent, under a rotated non-uniform grandparent: the authored
        // translation and rotation come back from the world that they made, and a requested world is reached.
        TestRandom random(61);
        for (int pattern = 0; pattern < 8; ++pattern)
        {
            for (int trial = 0; trial < 20; ++trial)
            {
                Node nodes[2];
                nodes[0] = RandomNode(random, false);
                nodes[1] = RandomNode(random, false);
                nodes[1].m_scale = nodes[1].m_scale.GetAbs() * PatternSigns(pattern);
                const T2::ChainState parent = ComposeAll(nodes, 2);

                const Vector3 translation = random.Translation();
                const Quaternion rotation = random.Rotation();
                const Vector3 scale = random.Scale(false);
                const T2::ChainState child = T2::ComposeChain(parent, translation, rotation, scale);

                const Vector3 solvedTranslation = T2::SolveLocalTranslation(parent, child.m_world.GetTranslation());
                EXPECT_TRUE(SameVector(solvedTranslation, translation, 1e-4f)) << "pattern " << pattern << " trial " << trial;
                EXPECT_LT(RotationError(T2::SolveLocalRotation(parent, child.m_rotation), rotation), RotationTolerance)
                    << "pattern " << pattern << " trial " << trial;

                // A requested world: solve the authored values, compose, and read the world back.
                const Vector3 wantedTranslation = random.Translation();
                const Quaternion wantedRotation = random.Rotation();
                const T2::ChainState written = T2::ComposeChain(
                    parent,
                    T2::SolveLocalTranslation(parent, wantedTranslation),
                    T2::SolveLocalRotation(parent, wantedRotation),
                    scale);
                EXPECT_TRUE(SameVector(written.m_world.GetTranslation(), wantedTranslation, 1e-4f)) << "pattern " << pattern << " trial " << trial;
                EXPECT_LT(RotationError(written.m_rotation, wantedRotation), RotationTolerance) << "pattern " << pattern << " trial " << trial;
            }
        }
    }

    TEST(MATH_Transform2Math, SolveLocal_UnderARoot_IsTheRequestedValue)
    {
        TestRandom random(67);
        const T2::ChainState root = T2::ChainFromTransform(Transform::CreateIdentity());
        const Vector3 translation = random.Translation();
        const Quaternion rotation = random.Rotation();
        EXPECT_TRUE(T2::SolveLocalTranslation(root, translation) == translation);
        EXPECT_LT(RotationError(T2::SolveLocalRotation(root, rotation), rotation), RotationTolerance);
    }

    TEST(MATH_Transform2Math, SolveLocalTranslation_UnderTinyUniformParents_ReachesTheRequestedWorld)
    {
        // Matrix3x4::GetInverseFull gives the identity below a uniform scale of about 0.0049. Solved with it, the local would be the
        // world, and the child would land the inverse of the scale too far away. The local that is asked for is the offset over the
        // scale, turned and mirrored back, so it is as long as the offset over the scale.
        const float scales[] = { 0.004f, 1e-3f, 1e-5f };
        TestRandom random(97);
        for (const float scale : scales)
        {
            for (int pattern = 0; pattern < 8; ++pattern)
            {
                for (int trial = 0; trial < 5; ++trial)
                {
                    Node parentNode;
                    parentNode.m_translation = random.Translation();
                    parentNode.m_rotation = random.Rotation();
                    parentNode.m_scale = Vector3(scale) * PatternSigns(pattern);
                    const T2::ChainState parent = ComposeAll(&parentNode, 1);

                    const Vector3 wanted = random.Translation();
                    const Vector3 local = T2::SolveLocalTranslation(parent, wanted);

                    // The rounding of the offset is about 1e-6, and the scale divides it.
                    const float expectedLength = (wanted - parentNode.m_translation).GetLength() / scale;
                    EXPECT_NEAR(local.GetLength(), expectedLength, 1e-4f * expectedLength + 1e-6f / scale)
                        << "scale " << scale << " pattern " << pattern << " trial " << trial;

                    const T2::ChainState child = T2::ComposeChain(parent, local, Quaternion::CreateIdentity(), Vector3::CreateOne());
                    EXPECT_TRUE(SameVector(child.m_world.GetTranslation(), wanted, 1e-4f))
                        << "scale " << scale << " pattern " << pattern << " trial " << trial;
                }
            }
        }
    }

    TEST(MATH_Transform2Math, SolveLocalTranslation_UnderEightStackedHalfScaleNodes_ReachesTheRequestedWorld)
    {
        // Eight nodes of 0.5 are a column scale of 0.0039, with a third of the nodes flipped on a random axis.
        TestRandom random(101);
        for (int trial = 0; trial < 50; ++trial)
        {
            Node nodes[MaxDepth];
            for (int i = 0; i < MaxDepth; ++i)
            {
                nodes[i].m_translation = random.Translation();
                nodes[i].m_rotation = random.Rotation();
                nodes[i].m_scale = Vector3(0.5f);
                if (random.Range(0.0f, 1.0f) < 0.33f)
                {
                    nodes[i].m_scale = nodes[i].m_scale * FlipOn(static_cast<int>(random.Range(0.0f, 2.99f)));
                }
            }
            const T2::ChainState parent = ComposeAll(nodes, MaxDepth);

            const Vector3 wanted = random.Translation();
            const T2::ChainState child = T2::ComposeChain(
                parent, T2::SolveLocalTranslation(parent, wanted), Quaternion::CreateIdentity(), Vector3::CreateOne());
            EXPECT_TRUE(SameVector(child.m_world.GetTranslation(), wanted, 1e-3f)) << "trial " << trial;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // InverseRelative (P14)

    TEST(MATH_Transform2Math, InverseRelative_InvertsUniformScalesThatGetInverseFullGivesUpOn)
    {
        // Matrix3x4::GetInverseFull returns the identity for a uniform scale below about 0.0049, because its
        // singular test is absolute. This one tests the columns against their own lengths.
        const float scales[] = { 0.1f, 0.01f, 0.004f, 1e-3f, 1e-5f, 1e-8f, 1e-12f };
        TestRandom random(71);
        for (const float scale : scales)
        {
            Node node;
            node.m_translation = Vector3(1.0f, 2.0f, 3.0f);
            node.m_rotation = random.Rotation();
            node.m_scale = Vector3(scale);
            const Matrix3x4 world = LocalMatrix(node);
            const Matrix3x4 inverse = T2::InverseRelative(world);

            // S^-1 R^T T(-t), from other matrices.
            const Matrix3x4 expected = Matrix3x4::CreateScale(Vector3(1.0f / scale))
                * Matrix3x4::CreateFromQuaternion(node.m_rotation.GetConjugate())
                * Matrix3x4::CreateTranslation(-node.m_translation);

            EXPECT_TRUE(inverse.IsFinite()) << "scale " << scale;
            EXPECT_LT(RelativeDifference(inverse, expected, 4), 1e-5f) << "scale " << scale;
            EXPECT_FALSE(inverse.IsClose(Matrix3x4::CreateIdentity(), 1e-3f)) << "scale " << scale;

            // A point goes out and comes back.
            const Vector3 point(0.5f, -1.5f, 2.0f);
            EXPECT_TRUE(SameVector(world.TransformPoint(inverse.TransformPoint(point)), point, 1e-4f)) << "scale " << scale;
        }
    }

    TEST(MATH_Transform2Math, InverseRelative_InvertsEightStackedHalfScaleNodes)
    {
        // Eight nodes of 0.5 are a column scale of 0.0039, with a third of the nodes flipped on a random axis.
        TestRandom random(73);
        for (int trial = 0; trial < 50; ++trial)
        {
            Node nodes[MaxDepth];
            for (int i = 0; i < MaxDepth; ++i)
            {
                nodes[i].m_translation = random.Translation();
                nodes[i].m_rotation = random.Rotation();
                nodes[i].m_scale = Vector3(0.5f);
                if (random.Range(0.0f, 1.0f) < 0.33f)
                {
                    nodes[i].m_scale = nodes[i].m_scale * FlipOn(static_cast<int>(random.Range(0.0f, 2.99f)));
                }
            }
            const Matrix3x4 world = ComposeAll(nodes, MaxDepth).m_world;
            const Matrix3x4 inverse = T2::InverseRelative(world);

            EXPECT_TRUE(inverse.IsFinite());
            EXPECT_LT(RelativeDifference(inverse * world, Matrix3x4::CreateIdentity(), 3), 1e-5f) << "trial " << trial;
            EXPECT_FALSE(inverse.IsClose(Matrix3x4::CreateIdentity(), 1e-3f));
        }
    }

    TEST(MATH_Transform2Math, InverseRelative_InvertsAMirror)
    {
        Node node;
        node.m_translation = Vector3(4.0f, -5.0f, 6.0f);
        node.m_rotation = Quaternion::CreateRotationY(0.8f);
        node.m_scale = Vector3(-0.003f, 0.003f, 0.003f);
        const Matrix3x4 world = LocalMatrix(node);
        const Matrix3x4 inverse = T2::InverseRelative(world);

        const Matrix3x4 expected = Matrix3x4::CreateScale(Vector3(-1.0f / 0.003f, 1.0f / 0.003f, 1.0f / 0.003f))
            * Matrix3x4::CreateFromQuaternion(node.m_rotation.GetConjugate())
            * Matrix3x4::CreateTranslation(-node.m_translation);
        EXPECT_LT(RelativeDifference(inverse, expected, 4), 1e-5f);
        EXPECT_FALSE(inverse.IsClose(Matrix3x4::CreateIdentity(), 1e-3f));
    }

    TEST(MATH_Transform2Math, InverseRelative_InvertsShear)
    {
        // A (2,1,1) scale with a 45 degree turn under it, and a translation: parent * TRS is sheared.
        const float angle = AZ::DegToRad(45.0f);
        const Vector3 translation(1.0f, -2.0f, 3.0f);
        const Matrix3x4 world = Matrix3x4::CreateTranslation(translation)
            * Matrix3x4::CreateScale(Vector3(2.0f, 1.0f, 1.0f))
            * Matrix3x4::CreateRotationZ(angle);
        const Matrix3x4 expected = Matrix3x4::CreateRotationZ(-angle)
            * Matrix3x4::CreateScale(Vector3(0.5f, 1.0f, 1.0f))
            * Matrix3x4::CreateTranslation(-translation);

        const Matrix3x4 inverse = T2::InverseRelative(world);
        EXPECT_LT(RelativeDifference(inverse, expected, 4), 1e-5f);
        EXPECT_FALSE(inverse.IsClose(Matrix3x4::CreateIdentity(), 1e-3f));

        // Two columns at an angle theta apart: the relative determinant is sin(theta). Down to 1e-3 the inverse is
        // exact to the float precision divided by it.
        const float thetas[] = { 1e-1f, 1e-2f, 1e-3f };
        for (const float theta : thetas)
        {
            const Matrix3x4 sheared = Matrix3x4::CreateFromColumns(
                Vector3(2.0f, 0.0f, 0.0f),
                Vector3(AZ::Cos(theta), AZ::Sin(theta), 0.0f) * 0.5f,
                Vector3(0.0f, 0.0f, 1.0f),
                translation);
            const Matrix3x4 shearedInverse = T2::InverseRelative(sheared);
            EXPECT_LT(RelativeDifference(shearedInverse * sheared, Matrix3x4::CreateIdentity(), 3), 2e-3f) << "theta " << theta;
        }

        // The singular test is relative to the unit columns. The same shear at a small scale has a raw determinant far below
        // 1e-5 (2e-9 and 2e-18) and still inverts. An absolute test would take the transpose fallback, which is wrong by
        // order one here, because the unit columns are 126.9 degrees apart.
        const float scales[] = { 1e-3f, 1e-6f };
        for (const float scale : scales)
        {
            const Matrix3x4 smallWorld = Matrix3x4::CreateTranslation(translation)
                * Matrix3x4::CreateScale(Vector3(2.0f * scale, scale, scale))
                * Matrix3x4::CreateRotationZ(angle);
            const Matrix3x4 smallInverse = T2::InverseRelative(smallWorld);
            EXPECT_TRUE(smallInverse.IsFinite()) << "scale " << scale;
            EXPECT_LT(RelativeDifference(smallInverse * smallWorld, Matrix3x4::CreateIdentity(), 3), 1e-5f) << "scale " << scale;
        }
    }

    TEST(MATH_Transform2Math, InverseRelative_OfASingularMatrix_IsFiniteAndNotTheIdentity)
    {
        // Two columns closer than the relative singular limit of 1e-5: no inverse exists, and the best available
        // one is finite and is not the identity.
        const float thetas[] = { 1e-7f, 1e-8f, 0.0f };
        for (const float theta : thetas)
        {
            const Matrix3x4 singular = Matrix3x4::CreateFromColumns(
                Vector3(2.0f, 0.0f, 0.0f),
                Vector3(AZ::Cos(theta), AZ::Sin(theta), 0.0f) * 0.5f,
                Vector3(0.0f, 0.0f, 1.0f),
                Vector3(1.0f, 2.0f, 3.0f));
            const Matrix3x4 inverse = T2::InverseRelative(singular);
            EXPECT_TRUE(inverse.IsFinite()) << "theta " << theta;
            EXPECT_FALSE(inverse.IsClose(Matrix3x4::CreateIdentity(), 1e-3f)) << "theta " << theta;
        }
    }

    TEST(MATH_Transform2Math, InverseRelative_OfACollapsedAxis_InvertsThatAxisToZero)
    {
        TestRandom random(79);
        Node node;
        node.m_translation = Vector3(1.0f, 2.0f, 3.0f);
        node.m_rotation = random.Rotation();
        node.m_scale = Vector3(0.0f, 1.0f, 1.0f);
        const Matrix3x4 world = LocalMatrix(node);
        const Matrix3x4 inverse = T2::InverseRelative(world);
        EXPECT_TRUE(inverse.IsFinite());

        // What survives is inverted, what collapsed is gone: inverse * world is diag(0,1,1) in its linear part.
        EXPECT_LT(RelativeDifference(inverse * world, Matrix3x4::CreateDiagonal(Vector3(0.0f, 1.0f, 1.0f)), 3), 1e-5f);

        // Nothing is left of an all-zero scale, and it is still not NaN.
        node.m_scale = Vector3(0.0f);
        const Matrix3x4 zeroInverse = T2::InverseRelative(LocalMatrix(node));
        EXPECT_TRUE(zeroInverse.IsFinite());
        EXPECT_TRUE(zeroInverse.IsClose(Matrix3x4::CreateZero(), 1e-6f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Scale tests (P13)

    TEST(MATH_Transform2Math, IsNonUniformEffective_UsesARelativeToleranceOfOneTenThousandth)
    {
        EXPECT_FALSE(T2::IsNonUniformEffective(Vector3(1.0f, 1.0f, 1.0f)));
        EXPECT_FALSE(T2::IsNonUniformEffective(Vector3(-2.0f, 2.0f, -2.0f)));
        EXPECT_FALSE(T2::IsNonUniformEffective(Vector3(1.0f, 1.00001f, 1.0f)));
        EXPECT_FALSE(T2::IsNonUniformEffective(Vector3(0.0f, 0.0f, 0.0f)));
        EXPECT_FALSE(T2::IsNonUniformEffective(Vector3(1e-30f, 1e-30f, 1e-30f)));
        EXPECT_TRUE(T2::IsNonUniformEffective(Vector3(1.0f, 1.001f, 1.0f)));
        EXPECT_TRUE(T2::IsNonUniformEffective(Vector3(2.0f, 1.0f, 1.0f)));
        EXPECT_TRUE(T2::IsNonUniformEffective(Vector3(0.0f, 1.0f, 1.0f)));
        EXPECT_TRUE(T2::IsNonUniformEffective(Vector3(1e-30f, 2e-30f, 1e-30f)));
    }

    TEST(MATH_Transform2Math, HasWorldScaleChanged_OnASignChangeAndOnOneHundredThousandthRelative)
    {
        const Vector3 before(1.0f, -2.0f, 3.0f);
        EXPECT_FALSE(T2::HasWorldScaleChanged(before, before));

        // Any sign change counts, on any axis.
        EXPECT_TRUE(T2::HasWorldScaleChanged(before, Vector3(-1.0f, -2.0f, 3.0f)));
        EXPECT_TRUE(T2::HasWorldScaleChanged(before, Vector3(1.0f, 2.0f, 3.0f)));
        EXPECT_TRUE(T2::HasWorldScaleChanged(before, Vector3(1.0f, -2.0f, -3.0f)));

        // A magnitude change counts above 1e-5 of the magnitude, on any axis, and not below it.
        EXPECT_TRUE(T2::HasWorldScaleChanged(before, Vector3(1.00002f, -2.0f, 3.0f)));
        EXPECT_TRUE(T2::HasWorldScaleChanged(before, Vector3(1.0f, -2.00004f, 3.0f)));
        EXPECT_TRUE(T2::HasWorldScaleChanged(before, Vector3(1.0f, -2.0f, 3.00006f)));
        EXPECT_FALSE(T2::HasWorldScaleChanged(before, Vector3(1.000005f, -2.00001f, 3.00002f)));

        // Zero is positive, and a collapse to or from zero is a change.
        EXPECT_FALSE(T2::HasWorldScaleChanged(Vector3(0.0f, 1.0f, 1.0f), Vector3(-0.0f, 1.0f, 1.0f)));
        EXPECT_FALSE(T2::HasWorldScaleChanged(Vector3(0.0f), Vector3(0.0f)));
        EXPECT_TRUE(T2::HasWorldScaleChanged(Vector3(0.0f, 1.0f, 1.0f), Vector3(1e-30f, 1.0f, 1.0f)));
    }

    TEST(MATH_Transform2Math, ResidualSplit_StaysFiniteAndMultipliesBack)
    {
        // Harness P13 (checks 6 and 6b), against the scale helpers that sit beside this math. The stand-in is
        // clamped to MinTransformScale, and the residual is divided by the clamped value, so the product holds.
        const Vector3 tiny(0.002f, 0.004f, 0.002f);
        EXPECT_EQ(AZ::Transform2::UniformStandIn(tiny), AZ::MinTransformScale);
        EXPECT_TRUE(SameVector(AZ::Transform2::Residual(tiny), Vector3(0.2f, 0.4f, 0.2f), 1e-6f));
        EXPECT_TRUE(SameVector(AZ::Transform2::Residual(tiny) * AZ::Transform2::UniformStandIn(tiny), tiny, 1e-6f));

        // An all-zero scale and a zero axis give a finite, positive residual.
        const Vector3 zero(0.0f);
        const Vector3 zeroResidual = AZ::Transform2::Residual(zero);
        EXPECT_TRUE(zeroResidual.IsFinite());
        EXPECT_GT(zeroResidual.GetMinElement(), 0.0f);
        const Vector3 axisResidual = AZ::Transform2::Residual(Vector3(0.0f, 1.0f, 1.0f));
        EXPECT_TRUE(axisResidual.IsFinite());
        EXPECT_GT(axisResidual.GetMinElement(), 0.0f);
    }
} // namespace UnitTest
