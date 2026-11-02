/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#if defined(HAVE_BENCHMARK)

#include <Atom/Feature/Decals/DecalFeatureProcessorInterface.h>
#include <Atom/Feature/Mesh/MeshCommon.h>

#include <AzCore/Math/Aabb.h>
#include <AzCore/Math/Frustum.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Obb.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/ShapeIntersection.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzTest/AzTest.h>

#include <numeric>
#include <random>
#include <vector>
#include <benchmark/benchmark.h>

//////////////////////////////////////////////////////////////////////////
// Transform 1 cost of the shared lines Transform 2 touched (T.10)
//
// Transform 1 has to stay as fast as it was. Every pair here is same-binary, like BM_MeshCullBounds in MeshCommonTests.cpp:
//   - Before: the upstream lines, reproduced exactly as a local function. They were read from the upstream commit 703bbae65966
//     (the tree before Transform 2) with git show 703bbae65966:<file>, and each comment names the file and line. Every line
//     number marked "upstream" in this file is a line number in that commit, not in the working tree.
//   - After: the lines the tree ships. They are called for real where they can be called (MeshCommon.h) and reproduced exactly
//     otherwise, with the file and line named, because the real function needs a scene, a model or a gem this module does not link.
// Both sides of a pair share one harness, one set of inputs and the same DoNotOptimize, and only the lines under test differ.
//
// One name filter catches all of them: BM_T1Cost_. The shape pairs (cache rebuild, effective offset) are in LmbrCentral.Tests,
// because ShapeComponentBus.h is a LmbrCentral header and this module does not link LmbrCentral.
//
// Run it from a profile build, in several separate processes: one run swings about 3 percent, so a pair is only read from the
// spread over the runs, not from one run. Nanoseconds per item is the per_item counter, or the time divided by the items counter.
// None of this decides pass or fail: it only measures.
//
// Keeping results alive. With MSVC, benchmark::DoNotOptimize is an out-of-line call that also stores the value to the stack
// (benchmark.h, the _MSC_VER branch). Paid once per item on a very short item (the ray clamp, the bounds scale), it is a fixed cost
// that both sides pay, it can hide the added vector work of the After side, and so it understates the added nanoseconds as well as
// diluting the percentage. So the short pairs add each result into one local and call DoNotOptimize once per pass. The sum is
// the same extra work on both sides. The long pairs (decal cull, decal bounds) keep their barrier once per pass.
// The items of a short pair are independent, so they measure throughput. In place, the clamp sits on the latency path in front of a
// divide, which this harness does not model.
//
// The ray clamp and the bounds scale have no denominator in this harness: no ray walk, no model, no cull. Read their added
// nanoseconds against the cost of the real function around them, not their percentage.
//
// A/A controls. Before and After are different template instantiations, so the compiler lays out and aligns their code separately.
// That placement bias is the same in every process of a series, because it is one binary, so the spread over the runs cannot show
// it. The BM_T1Cost_Control_ pairs run the same lines twice through two distinct instantiations (suffix _A and _B). Their percentage
// is the noise floor of code placement for a loop of that kind, and a Before and After difference is read against it. The control
// here is for the decal cull loop, and LmbrCentral.Tests has one for the box rebuild. The short pairs have none.

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
    // 1. Decal cull: the half lengths of the Obb in CullDecals, and the half extents in UpdateBounds
    //
    // Item: one decal, for the whole loop body of CullDecals (the draw distance test, the Obb, the frustum test and the push), so
    // the percentage is against the real per-decal work. CullDecals itself needs a view and a scene, so its loop is reproduced
    // exactly, without the sort, which only runs when r_maxVisibleDecals limits the count and is -1 (no limit) by default.
    // UpdateBounds is reproduced the same way, minus the handle lookup into m_decalData, so its percentage is an overstatement.
    //
    // The decals are spread around and in front of the camera, so some are inside the frustum and some are not, and half have
    // a draw distance. Every half length is positive, which is all a Transform 1 sender can write. The frustum is built here from
    // view attributes, where CullDecals builds it once per view from the view matrix; the test per decal is the same call.

    struct DecalCullData
    {
        static constexpr size_t DecalCount = 2048;

        DecalCullData()
            : m_viewFrustum(AZ::ViewFrustumAttributes(AZ::Transform::CreateIdentity(), 16.0f / 9.0f, AZ::DegToRad(70.0f), 0.1f, 100.0f))
        {
            SeededValues values;
            m_decals.resize(DecalCount);
            for (AZ::Render::DecalData& decal : m_decals)
            {
                const float x = values.Float(-60.0f, 60.0f);
                const float y = values.Float(-20.0f, 100.0f);
                const float z = values.Float(-40.0f, 40.0f);
                AZ::Vector3(x, y, z).StoreToFloat3(decal.m_position.data());
                values.Rotation().StoreToFloat4(decal.m_quaternion.data());
                values.Vector(0.2f, 3.0f).StoreToFloat3(decal.m_halfSize.data());

                const bool hasDrawDistance = values.Float(0.0f, 1.0f) < 0.5f;
                decal.m_maxDrawDistance = hasDrawDistance ? values.Float(20.0f, 120.0f) : 0.0f;
            }

            // CullDecals starts from the indices in order and only sorts them when the count is limited.
            m_sortedDecals.resize(DecalCount);
            std::iota(m_sortedDecals.begin(), m_sortedDecals.end(), 0);
        }

        std::vector<AZ::Render::DecalData> m_decals;
        std::vector<uint32_t> m_sortedDecals;
        AZ::Vector3 m_viewPos = AZ::Vector3::CreateZero();
        AZ::Frustum m_viewFrustum;
    };

    const DecalCullData& GetDecalCullData()
    {
        static const DecalCullData data;
        return data;
    }

    // The half lengths argument of the Obb in CullDecals as it was, DecalTextureArrayFeatureProcessor.cpp line 654 upstream.
    const auto DecalHalfLengthsBefore = [](const AZ::Render::DecalData& decalData)
    {
        return AZ::Vector3::CreateFromFloat3(decalData.m_halfSize.data());
    };

    // The same argument as it is now, DecalTextureArrayFeatureProcessor.cpp line 654: the one added call is GetAbs.
    const auto DecalHalfLengthsAfter = [](const AZ::Render::DecalData& decalData)
    {
        return AZ::Vector3::CreateFromFloat3(decalData.m_halfSize.data()).GetAbs();
    };

    // The half extents argument of the Aabb in UpdateBounds as it was, DecalTextureArrayFeatureProcessor.cpp line 756 upstream.
    const auto DecalHalfExtentsBefore = [](const AZ::Render::DecalData& data)
    {
        return AZ::Vector3(data.m_halfSize[0], data.m_halfSize[1], data.m_halfSize[2]);
    };

    // The same argument as it is now, DecalTextureArrayFeatureProcessor.cpp line 756: the one added call is GetAbs.
    const auto DecalHalfExtentsAfter = [](const AZ::Render::DecalData& data)
    {
        return AZ::Vector3(data.m_halfSize[0], data.m_halfSize[1], data.m_halfSize[2]).GetAbs();
    };

    // The loop of DecalTextureArrayFeatureProcessor::CullDecals, DecalTextureArrayFeatureProcessor.cpp lines 636 to 660. Only the
    // half lengths argument (line 654) is taken from the caller, so the two sides differ in nothing else.
    template<typename HalfLengthsOf>
    void CullDecalsLoop(const DecalCullData& data, HalfLengthsOf halfLengthsOf, std::vector<uint32_t>& visibilityBuffer)
    {
        const std::vector<AZ::Render::DecalData>& dataVector = data.m_decals;
        const std::vector<uint32_t>& sortedDecals = data.m_sortedDecals;
        const AZ::Vector3& viewPos = data.m_viewPos;
        const AZ::Frustum& viewFrustum = data.m_viewFrustum;
        const size_t numVisibleDecals = dataVector.size(); // r_maxVisibleDecals is -1, so no limit

        for (uint32_t i = 0; i < sortedDecals.size() && visibilityBuffer.size() < numVisibleDecals; ++i)
        {
            uint32_t dataIndex = sortedDecals[i];
            const auto& decalData = dataVector[dataIndex];

            // Zero means unlimited, so only cull when a limit is actually set.
            if (decalData.m_maxDrawDistance > 0.0f)
            {
                const float maxDrawDistanceSq = decalData.m_maxDrawDistance * decalData.m_maxDrawDistance;
                if ((AZ::Vector3::CreateFromFloat3(decalData.m_position.data()) - viewPos).GetLengthSq() > maxDrawDistanceSq)
                {
                    continue;
                }
            }

            AZ::Obb obb = AZ::Obb::CreateFromPositionRotationAndHalfLengths(
                AZ::Vector3::CreateFromFloat3(decalData.m_position.data()),
                AZ::Quaternion::CreateFromFloat4(decalData.m_quaternion.data()),
                halfLengthsOf(decalData));
            // Do the actual culling per decal and only add the indices for the visible ones.
            if (AZ::ShapeIntersection::Overlaps(viewFrustum, obb))
            {
                visibilityBuffer.push_back(dataIndex);
            }
        }
    }

    // DecalTextureArrayFeatureProcessor::UpdateBounds, DecalTextureArrayFeatureProcessor.cpp lines 751 to 757, for every decal.
    // Only the half extents argument (line 756) is taken from the caller.
    template<typename HalfExtentsOf>
    void UpdateBoundsLoop(const DecalCullData& data, HalfExtentsOf halfExtentsOf, std::vector<AZ::Aabb>& bounds)
    {
        for (size_t i = 0; i < data.m_decals.size(); ++i)
        {
            const AZ::Render::DecalData& decalData = data.m_decals[i];
            bounds[i] = AZ::Aabb::CreateCenterHalfExtents(
                AZ::Vector3(decalData.m_position[0], decalData.m_position[1], decalData.m_position[2]), halfExtentsOf(decalData));
        }
    }

    template<typename HalfLengthsOf>
    void RunDecalCull(benchmark::State& state, HalfLengthsOf halfLengthsOf)
    {
        const DecalCullData& data = GetDecalCullData();
        std::vector<uint32_t> visibilityBuffer;
        visibilityBuffer.reserve(data.m_decals.size());

        for ([[maybe_unused]] auto _ : state)
        {
            visibilityBuffer.clear();
            CullDecalsLoop(data, halfLengthsOf, visibilityBuffer);
            benchmark::DoNotOptimize(visibilityBuffer);
        }

        ReportPerItem(state, data.m_decals.size());
    }

    template<typename HalfExtentsOf>
    void RunDecalUpdateBounds(benchmark::State& state, HalfExtentsOf halfExtentsOf)
    {
        const DecalCullData& data = GetDecalCullData();
        std::vector<AZ::Aabb> bounds(data.m_decals.size());

        for ([[maybe_unused]] auto _ : state)
        {
            UpdateBoundsLoop(data, halfExtentsOf, bounds);
            benchmark::DoNotOptimize(bounds);
        }

        ReportPerItem(state, data.m_decals.size());
    }

    void BM_T1Cost_DecalCull_Before(benchmark::State& state)
    {
        RunDecalCull(state, DecalHalfLengthsBefore);
    }
    BENCHMARK(BM_T1Cost_DecalCull_Before);

    void BM_T1Cost_DecalCull_After(benchmark::State& state)
    {
        RunDecalCull(state, DecalHalfLengthsAfter);
    }
    BENCHMARK(BM_T1Cost_DecalCull_After);

    // A/A control for the cull loop (see the header): the Before line twice, each through its own type, so the loop is
    // instantiated twice from the same lines. Copy only tells the two types apart.
    template<int Copy>
    struct DecalHalfLengthsControl
    {
        AZ::Vector3 operator()(const AZ::Render::DecalData& decalData) const
        {
            return AZ::Vector3::CreateFromFloat3(decalData.m_halfSize.data());
        }
    };

    void BM_T1Cost_Control_DecalCull_A(benchmark::State& state)
    {
        RunDecalCull(state, DecalHalfLengthsControl<0>());
    }
    BENCHMARK(BM_T1Cost_Control_DecalCull_A);

    void BM_T1Cost_Control_DecalCull_B(benchmark::State& state)
    {
        RunDecalCull(state, DecalHalfLengthsControl<1>());
    }
    BENCHMARK(BM_T1Cost_Control_DecalCull_B);

    void BM_T1Cost_DecalUpdateBounds_Before(benchmark::State& state)
    {
        RunDecalUpdateBounds(state, DecalHalfExtentsBefore);
    }
    BENCHMARK(BM_T1Cost_DecalUpdateBounds_Before);

    void BM_T1Cost_DecalUpdateBounds_After(benchmark::State& state)
    {
        RunDecalUpdateBounds(state, DecalHalfExtentsAfter);
    }
    BENCHMARK(BM_T1Cost_DecalUpdateBounds_After);

    //////////////////////////////////////////////////////////////////////////
    // 2. Ray clamp: the scale clamp in front of every mesh ray test
    //
    // Item: one clamp of one scale, on a positive scale, which is all a Transform 1 entity has. The clamp is measured alone. In
    // Model::RayIntersection and SurfaceData::GetMeshRayIntersection it is compared with a ray walk, which comes after it in the
    // real function: two TransformPoint calls and divides, then LocalRayIntersectionAgainstModel walking the model. So the
    // percentage printed here is against nothing but the clamp and is large by construction. What matters is the added
    // nanoseconds per ray against the cost of that walk.
    //
    // The same two lines are in three places: Model::RayIntersection (Model.cpp), SurfaceData::GetMeshRayIntersection
    // (SurfaceDataUtility.cpp) and UiCanvasOnMeshComponent::CalculateUVFromRayIntersection (UiCanvasOnMeshComponent.cpp). Their
    // gems are not linked here.
    // The scales cover the usual range, and a few fall below AZ::MinTransformScale so the clamp does change them now and then.

    constexpr size_t RayClampScaleCount = 4096;

    const std::vector<AZ::Vector3>& GetRayClampScales()
    {
        static const std::vector<AZ::Vector3> scales = []()
        {
            SeededValues values;
            std::vector<AZ::Vector3> result;
            result.reserve(RayClampScaleCount);
            for (size_t i = 0; i < RayClampScaleCount; ++i)
            {
                result.push_back(values.Vector(0.005f, 3.0f));
            }
            return result;
        }();
        return scales;
    }

    // The clamp as it was: Model.cpp line 198 upstream, SurfaceDataUtility.cpp line 23 upstream, UiCanvasOnMeshComponent.cpp line 202
    // upstream.
    const auto RayClampBefore = [](const AZ::Vector3& nonUniformScale)
    {
        return nonUniformScale.GetMax(AZ::Vector3(AZ::MinTransformScale));
    };

    // The clamp as it is now: Model.cpp lines 200 to 202, SurfaceDataUtility.cpp lines 25 to 27, UiCanvasOnMeshComponent.cpp lines
    // 210 to 212. It keeps the sign of each axis, zero counting as positive.
    const auto RayClampAfter = [](const AZ::Vector3& nonUniformScale)
    {
        const AZ::Vector3 clampedMagnitude = nonUniformScale.GetAbs().GetMax(AZ::Vector3(AZ::MinTransformScale));
        return AZ::Vector3::CreateSelectCmpGreaterEqual(nonUniformScale, AZ::Vector3::CreateZero(), clampedMagnitude, -clampedMagnitude);
    };

    template<typename ClampOf>
    void RunRayClamp(benchmark::State& state, ClampOf clampOf)
    {
        const std::vector<AZ::Vector3>& scales = GetRayClampScales();

        for ([[maybe_unused]] auto _ : state)
        {
            // The sum keeps every clamp alive. DoNotOptimize runs once per pass, not once per item (see the header).
            AZ::Vector3 sum = AZ::Vector3::CreateZero();
            for (const AZ::Vector3& scale : scales)
            {
                sum = sum + clampOf(scale);
            }
            benchmark::DoNotOptimize(sum);
        }

        ReportPerItem(state, scales.size());
    }

    void BM_T1Cost_RayClamp_Before(benchmark::State& state)
    {
        RunRayClamp(state, RayClampBefore);
    }
    BENCHMARK(BM_T1Cost_RayClamp_Before);

    void BM_T1Cost_RayClamp_After(benchmark::State& state)
    {
        RunRayClamp(state, RayClampAfter);
    }
    BENCHMARK(BM_T1Cost_RayClamp_After);

    //////////////////////////////////////////////////////////////////////////
    // 3. Local bounds scale: the GetLocalBounds line of MeshComponentController
    //
    // Item: one box scaled by one positive scale. Before is Aabb::MultiplyByScale as MeshComponentController.cpp line 721 upstream
    // called it. After calls MeshCommon::MultiplyAabbBySignedScale for real, as MeshComponentController.cpp line 736 does. The
    // GetLocalAabb read and the IsValid test around the line are the same on both sides and are left out.

    struct ScaledAabbRecord
    {
        AZ::Aabb m_aabb;
        AZ::Vector3 m_scale;
    };

    constexpr size_t ScaledAabbCount = 4096;

    const std::vector<ScaledAabbRecord>& GetScaledAabbRecords()
    {
        static const std::vector<ScaledAabbRecord> records = []()
        {
            SeededValues values;
            std::vector<ScaledAabbRecord> result;
            result.reserve(ScaledAabbCount);
            for (size_t i = 0; i < ScaledAabbCount; ++i)
            {
                const AZ::Vector3 boxMin = -values.Vector(0.1f, 2.0f);
                const AZ::Vector3 boxMax = values.Vector(0.1f, 2.0f);
                result.push_back({ AZ::Aabb::CreateFromMinMax(boxMin, boxMax), values.Vector(0.5f, 2.0f) });
            }
            return result;
        }();
        return records;
    }

    // MeshComponentController.cpp lines 721 and 722 upstream.
    const auto ScaleLocalBoundsBefore = [](const ScaledAabbRecord& record)
    {
        AZ::Aabb aabb = record.m_aabb;
        aabb.MultiplyByScale(record.m_scale);
        return aabb;
    };

    // MeshComponentController.cpp lines 736 and 737, calling the helper in MeshCommon.h.
    const auto ScaleLocalBoundsAfter = [](const ScaledAabbRecord& record)
    {
        AZ::Aabb aabb = record.m_aabb;
        aabb = AZ::Render::MeshCommon::MultiplyAabbBySignedScale(aabb, record.m_scale);
        return aabb;
    };

    template<typename ScaleOf>
    void RunLocalBoundsScale(benchmark::State& state, ScaleOf scaleOf)
    {
        const std::vector<ScaledAabbRecord>& records = GetScaledAabbRecords();

        for ([[maybe_unused]] auto _ : state)
        {
            // The sum of both corners keeps every box alive. DoNotOptimize runs once per pass, not once per item (see the header).
            AZ::Vector3 sum = AZ::Vector3::CreateZero();
            for (const ScaledAabbRecord& record : records)
            {
                const AZ::Aabb scaled = scaleOf(record);
                sum = sum + scaled.GetMin() + scaled.GetMax();
            }
            benchmark::DoNotOptimize(sum);
        }

        ReportPerItem(state, records.size());
    }

    void BM_T1Cost_LocalBoundsScale_Before(benchmark::State& state)
    {
        RunLocalBoundsScale(state, ScaleLocalBoundsBefore);
    }
    BENCHMARK(BM_T1Cost_LocalBoundsScale_Before);

    void BM_T1Cost_LocalBoundsScale_After(benchmark::State& state)
    {
        RunLocalBoundsScale(state, ScaleLocalBoundsAfter);
    }
    BENCHMARK(BM_T1Cost_LocalBoundsScale_After);
} // namespace Benchmark::T1Cost

#endif // defined(HAVE_BENCHMARK)
