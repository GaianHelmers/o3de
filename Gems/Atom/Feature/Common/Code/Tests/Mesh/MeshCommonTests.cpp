/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/Feature/Mesh/MeshCommon.h>

#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/UnitTest.h>

#if defined(HAVE_BENCHMARK)
#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Matrix3x4.h>
#include <AzCore/Math/Obb.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Sphere.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>

#include <random>
#include <vector>
#include <benchmark/benchmark.h>
#endif

namespace UnitTest
{
    using namespace AZ;

    //////////////////////////////////////////////////////////////////////////
    // MultiplyAabbBySignedScale

    TEST(MeshCommonTest, MultiplyAabbBySignedScale_PositiveScale_MatchesMultiplyByScale)
    {
        const Aabb aabb = Aabb::CreateFromMinMax(Vector3(-1.0f, 0.0f, 2.0f), Vector3(3.0f, 4.0f, 5.0f));
        const Vector3 scale(2.0f, 0.5f, 3.0f);

        Aabb expected = aabb;
        expected.MultiplyByScale(scale);

        EXPECT_TRUE(Render::MeshCommon::MultiplyAabbBySignedScale(aabb, scale).IsClose(expected));
    }

    TEST(MeshCommonTest, MultiplyAabbBySignedScale_NegativeX_FlipsTheBoxAboutTheOriginOnX)
    {
        const Aabb aabb = Aabb::CreateFromMinMax(Vector3(1.0f, 0.0f, 0.0f), Vector3(3.0f, 2.0f, 1.0f));

        const Aabb result = Render::MeshCommon::MultiplyAabbBySignedScale(aabb, Vector3(-1.0f, 1.0f, 1.0f));

        EXPECT_TRUE(result.IsValid());
        EXPECT_TRUE(result.GetMin().IsClose(Vector3(-3.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(result.GetMax().IsClose(Vector3(-1.0f, 2.0f, 1.0f)));
    }

    TEST(MeshCommonTest, MultiplyAabbBySignedScale_AllAxesNegative_IsScaledAndOrdered)
    {
        // This is what a mirrored transform reads back as: the lengths of the matrix columns, all negated
        const Aabb aabb = Aabb::CreateFromMinMax(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));

        const Aabb result = Render::MeshCommon::MultiplyAabbBySignedScale(aabb, Vector3(-2.0f, -2.0f, -2.0f));

        EXPECT_TRUE(result.IsValid());
        EXPECT_TRUE(result.GetMin().IsClose(Vector3(-8.0f, -10.0f, -12.0f)));
        EXPECT_TRUE(result.GetMax().IsClose(Vector3(-2.0f, -4.0f, -6.0f)));
        EXPECT_TRUE(result.GetExtents().IsClose(Vector3(6.0f, 6.0f, 6.0f)));
    }
} // namespace UnitTest

#if defined(HAVE_BENCHMARK)

namespace Benchmark
{
    //////////////////////////////////////////////////////////////////////////
    // Transform 1 cost of the exact world flag test in ModelDataInstance::UpdateCullBounds (T.10)
    //
    // There is no baseline binary, so the comparison is same-binary: one harness, one set of meshes, and a scaling step
    // without the flag test (T1CullBounds_Before) against the one in the source today (T1CullBounds_After).
    // T2CullBounds_Exact is the exact world path (MeshCommon::CalculateExactCullBounds, which UpdateExactCullBounds runs) on
    // the same meshes flipped on X, which is the Transform 2 cost.
    //
    // Run it from a profile build. Nanoseconds per mesh is the per_mesh counter, or the time divided by RecordCount.

    // What UpdateCullBounds reads for one mesh.
    struct CullBoundsRecord
    {
        // Same members, order and width as ModelDataInstance::Flags in MeshFeatureProcessor.h, which is private, so it
        // cannot be used here. m_mirrored is the 14th bit there too, and m_exactWorld the 15th. If that struct changes, change this one.
        struct Flags
        {
            bool m_cullBoundsNeedsUpdate : 1;
            bool m_cullableNeedsRebuild : 1;
            bool m_needsInit : 1;
            bool m_objectSrgNeedsUpdate : 1;
            bool m_isAlwaysDynamic : 1;
            bool m_dynamic : 1;
            bool m_isDrawMotion : 1;
            bool m_visible : 1;
            bool m_useForwardPassIblSpecular : 1;
            bool m_hasForwardPassIblSpecularMaterial : 1;
            bool m_needsSetRayTracingData : 1;
            bool m_hasRayTracingReflectionProbe : 1;
            bool m_keepBufferAssetsInMemory : 1;
            bool m_mirrored : 1;
            bool m_exactWorld : 1;
        };

        AZ::Aabb m_aabb;
        AZ::Transform m_localToWorld;
        AZ::Vector3 m_nonUniformScale;
        AZ::Matrix3x4 m_world;          //!< The matrix of the pair above, which the exact world path reads. Only Transform 2 meshes use it.
        Flags m_flags;
    };

    // What UpdateCullBounds writes into the cullable.
    struct CullBoundsResult
    {
        float m_lodSelectionRadius;
        AZ::Sphere m_boundingSphere;
        AZ::Obb m_boundingObb;
        AZ::Aabb m_boundingVolume;
    };

    // The scaling step as it was before this work.
    const auto ScaleLocalAabbBefore = [](const CullBoundsRecord& record)
    {
        AZ::Aabb localAabb = record.m_aabb;
        localAabb.MultiplyByScale(record.m_nonUniformScale);
        return localAabb;
    };

    // The scaling step as it is in UpdateCullBounds today. A mesh with an exact world leaves UpdateCullBounds before this step,
    // for UpdateExactCullBounds, so for a Transform 1 mesh the only addition is the one bit test.
    const auto ScaleLocalAabbAfter = [](const CullBoundsRecord& record)
    {
        AZ::Aabb localAabb = record.m_aabb;
        if (record.m_flags.m_exactWorld)
        {
            return localAabb;
        }
        localAabb.MultiplyByScale(record.m_nonUniformScale);
        return localAabb;
    };

    // Everything UpdateCullBounds does after the scaling step, up to but excluding RegisterOrUpdateCullable.
    AZ_FORCE_INLINE CullBoundsResult BuildCullBounds(const AZ::Aabb& localAabb, const AZ::Transform& localToWorld)
    {
        AZ::Vector3 center;
        float radius;
        localAabb.GetTransformedAabb(localToWorld).GetAsSphere(center, radius);

        return {
            0.5f * localAabb.GetExtents().GetMaxElement(),
            AZ::Sphere(center, radius),
            localAabb.GetTransformedObb(localToWorld),
            localAabb.GetTransformedAabb(localToWorld) };
    }

    template<typename ScaleFunction>
    void RunCullBounds(benchmark::State& state, const std::vector<CullBoundsRecord>& records, ScaleFunction scaleLocalAabb)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            for (const CullBoundsRecord& record : records)
            {
                CullBoundsResult result = BuildCullBounds(scaleLocalAabb(record), record.m_localToWorld);
                benchmark::DoNotOptimize(result);
            }
        }

        const double meshesPerIteration = static_cast<double>(records.size());
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(records.size()));
        // The inverse of meshes per second, which the console shows as a time (for example 20.1ns) in the counter column.
        state.counters["per_mesh"] =
            benchmark::Counter(meshesPerIteration, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    }

    class BM_MeshCullBounds
        : public benchmark::Fixture
    {
    public:
        static constexpr size_t RecordCount = 10000;

        void SetUp(const benchmark::State&) override
        {
            BuildRecords();
        }
        void SetUp(benchmark::State&) override
        {
            BuildRecords();
        }

        std::vector<CullBoundsRecord> m_records;         //!< Transform 1 meshes: positive scale, not mirrored.
        std::vector<CullBoundsRecord> m_mirroredRecords; //!< The same meshes flipped on X: negative X scale, mirrored, exact world.

    private:
        void BuildRecords()
        {
            // A fixed seed gives every run, and every benchmark, the same data.
            std::mt19937 rng(1);
            const auto nextFloat = [&rng](float low, float high)
            {
                return std::uniform_real_distribution<float>(low, high)(rng);
            };
            // One statement per value, so the order the numbers are drawn in does not depend on the compiler.
            const auto nextVector = [&nextFloat](float low, float high)
            {
                const float x = nextFloat(low, high);
                const float y = nextFloat(low, high);
                const float z = nextFloat(low, high);
                return AZ::Vector3(x, y, z);
            };

            m_records.clear();
            m_mirroredRecords.clear();
            for (size_t i = 0; i < RecordCount; ++i)
            {
                CullBoundsRecord record{};

                const AZ::Vector3 boxMin = -nextVector(0.1f, 2.0f);
                const AZ::Vector3 boxMax = nextVector(0.1f, 2.0f);
                record.m_aabb = AZ::Aabb::CreateFromMinMax(boxMin, boxMax);

                const AZ::Vector3 axis = nextVector(0.1f, 1.0f);
                const float w = nextFloat(0.1f, 1.0f);
                const AZ::Vector3 position = nextVector(-100.0f, 100.0f);
                record.m_localToWorld = AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion(axis, w).GetNormalized(), position);
                record.m_localToWorld.SetUniformScale(nextFloat(0.5f, 2.0f));

                record.m_nonUniformScale = nextVector(0.5f, 2.0f);
                record.m_flags.m_cullBoundsNeedsUpdate = true;
                m_records.push_back(record);

                record.m_nonUniformScale.SetX(-record.m_nonUniformScale.GetX());
                record.m_flags.m_mirrored = true;
                record.m_flags.m_exactWorld = true;
                // What the transform service stores for the pair, as SetTransformForId builds it.
                record.m_world = AZ::Matrix3x4::CreateFromTransform(record.m_localToWorld);
                record.m_world.MultiplyByScale(record.m_nonUniformScale);
                m_mirroredRecords.push_back(record);
            }
        }
    };

    BENCHMARK_F(BM_MeshCullBounds, T1CullBounds_Before)(benchmark::State& state)
    {
        RunCullBounds(state, m_records, ScaleLocalAabbBefore);
    }

    BENCHMARK_F(BM_MeshCullBounds, T1CullBounds_After)(benchmark::State& state)
    {
        RunCullBounds(state, m_records, ScaleLocalAabbAfter);
    }

    BENCHMARK_F(BM_MeshCullBounds, T2CullBounds_Exact)(benchmark::State& state)
    {
        for ([[maybe_unused]] auto _ : state)
        {
            for (const CullBoundsRecord& record : m_mirroredRecords)
            {
                AZ::Render::MeshCommon::ExactCullBounds result =
                    AZ::Render::MeshCommon::CalculateExactCullBounds(record.m_aabb, record.m_world);
                benchmark::DoNotOptimize(result);
            }
        }

        const double meshesPerIteration = static_cast<double>(m_mirroredRecords.size());
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(m_mirroredRecords.size()));
        state.counters["per_mesh"] =
            benchmark::Counter(meshesPerIteration, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    }
} // namespace Benchmark

#endif // defined(HAVE_BENCHMARK)
