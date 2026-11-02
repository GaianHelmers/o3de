/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#if defined(HAVE_BENCHMARK)

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Obb.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzTest/AzTest.h>
#include <LmbrCentral/Shape/BoxShapeComponentBus.h>
#include <LmbrCentral/Shape/ShapeComponentBus.h>

#include <random>
#include <vector>
#include <benchmark/benchmark.h>

//////////////////////////////////////////////////////////////////////////
// Transform 1 cost of the shape lines Transform 2 touched (T.10)
//
// These are the shape half of the T1 cost pairs. The rest are in Atom_Feature_Common.Tests (Tests/Mesh/T1CostBenchmarks.cpp), which
// does not link LmbrCentral and so cannot include ShapeComponentBus.h. This module links LmbrCentral.Static, which gives the
// LmbrCentral include directory, and AzTest, which gives Google Benchmark, so it builds with what is already linked.
//
// Every pair is same-binary, like BM_MeshCullBounds in MeshCommonTests.cpp: "Before" is the upstream lines reproduced exactly (read
// from the upstream commit 703bbae65966, the tree before Transform 2, with git show 703bbae65966:<file>), and "After" is the lines
// the tree ships, called for real where they can be called and otherwise reproduced exactly with the file and line named. Both sides
// share one harness and one set of inputs. Every line number marked "upstream" in this file is a line number in that commit, not
// in the working tree.
//
// No shape component and no EBus handler is created: the benchmark hook of this module (AZ_UNIT_TEST_HOOK in LmbrCentralTest.cpp)
// sets up no test environment, and a handler would add nothing to what is measured. What is used is the real
// ShapeComponentRequests interface and the real IntersectionTestDataCache.
//
// Run it from a profile build, in several separate processes: one run swings about 3 percent, so a pair is only read from the
// spread over the runs, not from one run.
//   AzTestRunner.exe LmbrCentral.Tests AzRunBenchmarks --benchmark_filter=BM_T1Cost_
// Nanoseconds per item is the per_item counter, or the time divided by the items counter. None of this decides pass or fail.
//
// Keeping results alive. With MSVC, benchmark::DoNotOptimize is an out-of-line call that also stores the value to the stack
// (benchmark.h, the _MSC_VER branch). Paid once per item on a very short item (the effective offset), it is a fixed cost that both
// sides pay, it can hide the added work of the After side, and so it understates the added nanoseconds as well as diluting the
// percentage. So the effective offset pair adds each result into one local and calls DoNotOptimize once per pass. The sum is the
// same extra work on both sides. The box rebuild is long enough to keep its barrier after every rebuild. The items of a short pair
// are independent, so they measure throughput.
//
// A/A control. Before and After are different template instantiations, so the compiler lays out and aligns their code separately.
// That placement bias is the same in every process of a series, because it is one binary, so the spread over the runs cannot show
// it. BM_T1Cost_Control_ShapeCacheRebuild_A and _B run the Before lines twice through two distinct instantiations. Their percentage
// is the noise floor of code placement for the rebuild, and a Before and After difference is read against it. The effective
// offset pair has no control.

namespace Benchmark::T1Cost
{
    //////////////////////////////////////////////////////////////////////////
    // Shared

    // A fixed seed gives every run, and every benchmark, the same data.
    class SeededValues
    {
    public:
        float Float(float low, float high)
        {
            return std::uniform_real_distribution<float>(low, high)(m_rng);
        }

        // One statement per value, so the order the numbers are drawn in does not depend on the compiler.
        AZ::Vector3 Vector(float low, float high)
        {
            const float x = Float(low, high);
            const float y = Float(low, high);
            const float z = Float(low, high);
            return AZ::Vector3(x, y, z);
        }

        AZ::Quaternion Rotation()
        {
            const AZ::Vector3 axis = Vector(0.1f, 1.0f);
            const float w = Float(0.1f, 1.0f);
            return AZ::Quaternion(axis, w).GetNormalized();
        }

    private:
        std::mt19937 m_rng{ 1 };
    };

    // Reports the time of one item, in the same style as BM_MeshCullBounds. The per_item counter is the inverse of items per second,
    // which the console shows as a time (for example 20.1ns). The items counter is the number of items in one iteration, so a
    // script can divide the real time by it.
    inline void ReportPerItem(benchmark::State& state, size_t itemsPerIteration)
    {
        const double items = static_cast<double>(itemsPerIteration);
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(itemsPerIteration));
        state.counters["items"] = items;
        state.counters["per_item"] =
            benchmark::Counter(items, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    }

    //////////////////////////////////////////////////////////////////////////
    // 1. Shape cache rebuild: the box offset read through Flip() at mask 0
    //
    // Item: one rebuild of a box cache, called the way a transform change calls it: InvalidateCache, then UpdateIntersectionParams.
    // BoxShape::BoxIntersectionDataCache is protected inside BoxShape, and a BoxShape is an EBus handler, so its rebuild is
    // reproduced exactly instead, as a cache of the same base. UpdateIntersectionParams, InvalidateCache and Flip are the real
    // ones from ShapeComponentBus.h. Both sides go through one harness, so whatever the compiler does with the rebuild call it
    // does to both, and the cache goes to DoNotOptimize after every rebuild, so a rebuild cannot be dropped.
    // The shared_mutex that BoxShape passes is left out (nullptr), as it is the same on both sides and would only enlarge the
    // rebuild this is measured against.
    //
    // The inputs change on every rebuild: position, rotation, entity scale, non-uniform scale, dimensions and offset. Half of the
    // boxes are not rotated, which takes the Aabb branch of the rebuild, and half are, which takes the Obb branch.
    // The flip mask is zero throughout, which is every Transform 1 box.

    using BoxCache = LmbrCentral::IntersectionTestDataCache<LmbrCentral::BoxShapeConfig>;

    // The offset the rebuild reads as it was: BoxShape.cpp line 318 upstream, configuration.m_translationOffset.
    struct ReadOffset
    {
        static AZ::Vector3 From(const BoxCache&, const LmbrCentral::BoxShapeConfig& configuration)
        {
            return configuration.m_translationOffset;
        }
    };

    // The offset the rebuild reads as it is now: BoxShape.cpp line 364, Flip(configuration.m_translationOffset). Flip is the real
    // IntersectionTestDataCache::Flip, ShapeComponentBus.h lines 147 to 150.
    struct FlipOffset
    {
        static AZ::Vector3 From(const BoxCache& cache, const LmbrCentral::BoxShapeConfig& configuration)
        {
            return cache.Flip(configuration.m_translationOffset);
        }
    };

    // BoxShape::BoxIntersectionDataCache::UpdateIntersectionParamsImpl, BoxShape.cpp lines 359 to 394 (lines 313 to 348 upstream).
    // Everything is as written there, except that the offset (line 364, line 318 upstream) is read through OffsetOf.
    // Copy only tells two instantiations of the same lines apart, for the A/A controls. Before and After leave it at zero.
    template<typename OffsetOf, int Copy = 0>
    class BoxCacheRebuild
        : public BoxCache
    {
    protected:
        void UpdateIntersectionParamsImpl(
            const AZ::Transform& currentTransform,
            const LmbrCentral::BoxShapeConfig& configuration,
            const AZ::Vector3& currentNonUniformScale) override
        {
            AZ::Transform worldFromLocalNormalized = currentTransform;
            const float entityScale = worldFromLocalNormalized.ExtractUniformScale();
            const AZ::Vector3 scaledTranslationOffset = entityScale * currentNonUniformScale * OffsetOf::From(*this, configuration);

            m_currentPosition = worldFromLocalNormalized.TransformPoint(scaledTranslationOffset);
            m_scaledDimensions = configuration.m_dimensions * currentNonUniformScale * entityScale;

            AZ::Quaternion worldFromLocalQuaternion = worldFromLocalNormalized.GetRotation();
            if (worldFromLocalQuaternion.IsClose(AZ::Quaternion::CreateIdentity()))
            {
                AZ::Vector3 boxMin = m_scaledDimensions * -0.5f + scaledTranslationOffset;
                boxMin = worldFromLocalNormalized.TransformPoint(boxMin);

                AZ::Vector3 boxMax = m_scaledDimensions * 0.5f + scaledTranslationOffset;
                boxMax = worldFromLocalNormalized.TransformPoint(boxMax);

                m_aabb = AZ::Aabb::CreateFromMinMax(boxMin, boxMax);
                m_obb = AZ::Obb::CreateFromAabb(m_aabb);

                m_axisAligned = true;
            }
            else
            {
                const AZ::Vector3 halfLengthVector = m_scaledDimensions * 0.5f;
                m_obb = AZ::Obb::CreateFromPositionRotationAndHalfLengths(
                    m_currentPosition,
                    worldFromLocalNormalized.GetRotation(),
                    halfLengthVector);

                m_aabb = AZ::Aabb::CreateFromObb(m_obb);
                m_axisAligned = false;
            }
        }

    private:
        AZ::Aabb m_aabb;
        AZ::Obb m_obb;
        AZ::Vector3 m_currentPosition;
        AZ::Vector3 m_scaledDimensions;
        bool m_axisAligned = true;
    };

    struct BoxRebuildRecord
    {
        AZ::Transform m_transform;
        LmbrCentral::BoxShapeConfig m_configuration;
        AZ::Vector3 m_nonUniformScale;
    };

    constexpr size_t BoxRebuildCount = 4096;

    const std::vector<BoxRebuildRecord>& GetBoxRebuildRecords()
    {
        static const std::vector<BoxRebuildRecord> records = []()
        {
            SeededValues values;
            std::vector<BoxRebuildRecord> result;
            result.reserve(BoxRebuildCount);
            for (size_t i = 0; i < BoxRebuildCount; ++i)
            {
                BoxRebuildRecord record;
                const AZ::Quaternion rotation = (i % 2 == 0) ? AZ::Quaternion::CreateIdentity() : values.Rotation();
                const AZ::Vector3 position = values.Vector(-100.0f, 100.0f);
                record.m_transform = AZ::Transform::CreateFromQuaternionAndTranslation(rotation, position);
                record.m_transform.SetUniformScale(values.Float(0.5f, 2.0f));
                record.m_configuration.m_dimensions = values.Vector(0.5f, 4.0f);
                record.m_configuration.m_translationOffset = values.Vector(-1.0f, 1.0f);
                record.m_nonUniformScale = values.Vector(0.5f, 2.0f);
                result.push_back(record);
            }
            return result;
        }();
        return records;
    }

    template<typename Cache>
    void RunBoxCacheRebuild(benchmark::State& state, Cache& cache)
    {
        const std::vector<BoxRebuildRecord>& records = GetBoxRebuildRecords();

        for ([[maybe_unused]] auto _ : state)
        {
            for (const BoxRebuildRecord& record : records)
            {
                cache.InvalidateCache(LmbrCentral::InvalidateShapeCacheReason::TransformChange);
                cache.UpdateIntersectionParams(record.m_transform, record.m_configuration, nullptr, record.m_nonUniformScale);
                benchmark::DoNotOptimize(cache);
            }
        }

        ReportPerItem(state, records.size());
    }

    void BM_T1Cost_ShapeCacheRebuild_Before(benchmark::State& state)
    {
        BoxCacheRebuild<ReadOffset> cache;
        RunBoxCacheRebuild(state, cache);
    }
    BENCHMARK(BM_T1Cost_ShapeCacheRebuild_Before);

    void BM_T1Cost_ShapeCacheRebuild_After(benchmark::State& state)
    {
        BoxCacheRebuild<FlipOffset> cache;
        RunBoxCacheRebuild(state, cache);
    }
    BENCHMARK(BM_T1Cost_ShapeCacheRebuild_After);

    // A/A control for the rebuild (see the header): the Before lines twice, as two distinct types.
    void BM_T1Cost_Control_ShapeCacheRebuild_A(benchmark::State& state)
    {
        BoxCacheRebuild<ReadOffset, 1> cache;
        RunBoxCacheRebuild(state, cache);
    }
    BENCHMARK(BM_T1Cost_Control_ShapeCacheRebuild_A);

    void BM_T1Cost_Control_ShapeCacheRebuild_B(benchmark::State& state)
    {
        BoxCacheRebuild<ReadOffset, 2> cache;
        RunBoxCacheRebuild(state, cache);
    }
    BENCHMARK(BM_T1Cost_Control_ShapeCacheRebuild_B);

    //////////////////////////////////////////////////////////////////////////
    // 2. Effective translation offset: the default body a Disk, Quad or Polygon Prism takes
    //
    // Disk, Quad and Polygon Prism no longer take this path. The first series measured it at 2.00 against 4.13 ns, which a T1 light
    // on one of them paid on every move, so each now overrides GetEffectiveTranslationOffset and answers zero in one virtual call,
    // the cost of the call it replaced. This pair stays as the price of the default body, for any shape that adds an offset reader.
    //
    // Item: one offset asked of one shape, as a virtual call through ShapeComponentRequests.
    // Before is GetTranslationOffset, the call the offset readers made before this work (ShapeComponentBus.h line 178 upstream,
    // line 239 now; AreaLightComponentController.cpp line 607 upstream): one virtual call. For these shapes it is the base answer,
    // zero, because none of them overrides it.
    // After is GetEffectiveTranslationOffset, the real default body (ShapeComponentBus.h lines 271 to 274), which none of
    // them overrides either (a caller is AreaLightComponentController.cpp line 608): GetTranslationOffset() * GetFlipSigns(),
    // three virtual calls and a multiply.
    //
    // The shape is a stand-in for the three: it overrides GetFlipSigns like they do (DiskShape.cpp lines 205 to 209,
    // QuadShape.cpp lines 250 to 254, PolygonPrismShape.cpp lines 453 to 457, each one byte through FlipMaskToSigns,
    // ShapeComponentBus.h lines 49 to 52) and nothing else of the offset. The EBus dispatch that surrounds the call in some
    // callers is the same on both sides and is left out.
    //
    // This call takes no input. What changes on every call is the shape it is asked of, one of 1024 separate objects, and the
    // results are summed and kept with DoNotOptimize once per pass. A call this short is a few nanoseconds, so its percentage is
    // large by construction: read the added nanoseconds against how often a caller asks. It has no denominator in this harness.

    // The five pure virtuals of ShapeComponentRequests answer nothing: the benchmark never asks them.
    class DefaultOffsetShape final
        : public LmbrCentral::ShapeComponentRequests
    {
    public:
        AZ::Crc32 GetShapeType() const override
        {
            return AZ::Crc32();
        }

        AZ::Aabb GetEncompassingAabb() const override
        {
            return AZ::Aabb::CreateNull();
        }

        void GetTransformAndLocalBounds(AZ::Transform& transform, AZ::Aabb& bounds) const override
        {
            transform = AZ::Transform::CreateIdentity();
            bounds = AZ::Aabb::CreateNull();
        }

        bool IsPointInside([[maybe_unused]] const AZ::Vector3& point) const override
        {
            return false;
        }

        float DistanceSquaredFromPoint([[maybe_unused]] const AZ::Vector3& point) const override
        {
            return 0.0f;
        }

        // The flip mask is zero, which is every Transform 1 shape.
        AZ::Vector3 GetFlipSigns() const override
        {
            return LmbrCentral::FlipMaskToSigns(m_flipMask);
        }

    private:
        AZ::u8 m_flipMask = 0;
    };

    struct EffectiveOffsetShapes
    {
        static constexpr size_t ShapeCount = 1024;

        EffectiveOffsetShapes()
            : m_shapes(ShapeCount)
        {
            m_requests.reserve(m_shapes.size());
            for (const DefaultOffsetShape& shape : m_shapes)
            {
                m_requests.push_back(&shape);
            }
        }

        std::vector<DefaultOffsetShape> m_shapes;
        std::vector<const LmbrCentral::ShapeComponentRequests*> m_requests;
    };

    const EffectiveOffsetShapes& GetEffectiveOffsetShapes()
    {
        static const EffectiveOffsetShapes shapes;
        return shapes;
    }

    // The offset read as it was: one virtual call to GetTranslationOffset.
    const auto TranslationOffsetBefore = [](const LmbrCentral::ShapeComponentRequests& shape)
    {
        return shape.GetTranslationOffset();
    };

    // The offset read as it is now: GetEffectiveTranslationOffset, called for real.
    const auto TranslationOffsetAfter = [](const LmbrCentral::ShapeComponentRequests& shape)
    {
        return shape.GetEffectiveTranslationOffset();
    };

    template<typename OffsetOf>
    void RunEffectiveOffset(benchmark::State& state, OffsetOf offsetOf)
    {
        const EffectiveOffsetShapes& shapes = GetEffectiveOffsetShapes();

        for ([[maybe_unused]] auto _ : state)
        {
            // The sum keeps every offset alive. DoNotOptimize runs once per pass, not once per item (see the header).
            AZ::Vector3 sum = AZ::Vector3::CreateZero();
            for (const LmbrCentral::ShapeComponentRequests* shape : shapes.m_requests)
            {
                sum = sum + offsetOf(*shape);
            }
            benchmark::DoNotOptimize(sum);
        }

        ReportPerItem(state, shapes.m_requests.size());
    }

    void BM_T1Cost_EffectiveOffset_Before(benchmark::State& state)
    {
        RunEffectiveOffset(state, TranslationOffsetBefore);
    }
    BENCHMARK(BM_T1Cost_EffectiveOffset_Before);

    void BM_T1Cost_EffectiveOffset_After(benchmark::State& state)
    {
        RunEffectiveOffset(state, TranslationOffsetAfter);
    }
    BENCHMARK(BM_T1Cost_EffectiveOffset_After);
} // namespace Benchmark::T1Cost

#endif // defined(HAVE_BENCHMARK)
