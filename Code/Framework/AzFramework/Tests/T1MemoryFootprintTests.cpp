/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

// Runtime memory footprint of 1000 entities that carry the Transform 1 component.
//
// This file reports numbers and asserts nothing about them. The same source is built against the baseline
// engine (no Transform 2) and against the Transform 2 branch, each binary is run, and the outputs are compared.
// Native and Transform 1 must read the same. The Transform 2 rows exist only where the Transform 2 component
// header exists, so they are compiled out of the baseline build.
//
// Rows, one test each:
//   Flat                   1000 entities with one TransformComponent: created, Init, Activate
//   FlatNotActivated       the same, stopped after Init, so construction and activation read apart
//   HierarchyChains        100 chains of 10 entities, parented through TransformBus::SetParent
//   HierarchyFan           one parent with 999 children, parented through TransformBus::SetParent
//   Transform2Flat         Flat, with Transform2Component at a flipped scale instead of the TransformComponent  (Transform 2 build only)
//   Transform2FlatNotActivated                                                   (Transform 2 build only)
//   Transform2Chains       HierarchyChains, with Transform2Component on every entity    (Transform 2 build only)
//   Transform2Fan          HierarchyFan, with Transform2Component on every entity       (Transform 2 build only)
//
// How to run (nothing here runs by itself):
// - Each row in its own process, because the first use of a bus in a process allocates its context.
// - Each row at least 3 times per tree. Delete the T1MEM_OUT file between runs that should not share it.
// - Each row and tree as a run pair:
//     a normal run             allocation records on: gives allocations and the per-allocator bytes
//     a run with T1MEM_RECORDS=0   allocation records off: gives the OS heap (T1MEMOS)
//   The OS heap also holds the allocation record tables (they use the OS allocator), and their size follows
//   record history and not Transform 1. So the OS heap is printed as numbers only when the records are off.
// - Both trees are built in the same configuration, and not _RELEASE (see allocations below).
//
// Verdict rule (also printed as T1MEMRULE). Transform 1 is cleared only if ALL of these are equal between the two
// trees, in the steady pass (pass 2), and each of them is identical across the repeats of one binary:
//   every T1MEMSIZE value, allocations, the T1MEMALLOC bytes of every allocator, and heap_blocks of T1MEMOS
//   from the records-off run.
// Equal total_bytes alone clears nothing: the allocators round every request up to a size class, so a growth
// smaller than the gap to the next class does not show in it. A value that differs across the repeats of one
// binary (pass 2, left_after_destroy, any of the values above) makes the row noisy, and a noisy row clears nothing.
//
// Output (stdout, one fixed form per line, key=value so it can be grepped):
//   T1MEMENV      <row> ...                     the state the process started from (build flags, counts, totals)
//   T1MEMENVALLOC <row> <allocator name> <bytes>   what each allocator held before the first pass
//   T1MEMCAL      <row> ...                     calibration of the OS heap reading (known blocks against the reading)
//   T1MEMRULE     <row> ...                     the verdict rule above
//   per pass (pass=1 role=first, pass=2 role=steady):
//   T1MEM         <row> pass=<n> role=<r> total_bytes=<n> per_entity=<x> allocations=<n|na> left_after_destroy=<n> private_bytes=<n>
//   T1MEMALLOC    <row> pass=<n> <allocator name> <bytes>   change per allocator while the entities were created
//   T1MEMLEFT     <row> pass=<n> <allocator name> <bytes>   what each allocator still holds after destroy (non-zero only)
//   T1MEMOS       <row> pass=<n> valid=1 heap_bytes=<n> ...  the raw OS heap, only in a records-off run with a good calibration
//   T1MEMOS       <row> pass=<n> valid=0 reason=<why>      no numbers when the reading cannot be trusted
//   T1MEMNOISY    <row> pass=<n> private_bytes=<n> ...      Windows only: process private bytes, noisy
//   T1MEMGROWTH   <row> total_bytes=<n> allocations=<n|na> heap_blocks=<n|na>   pass 1 minus pass 2: one-time growth
//   T1MEMEXTRA    <row> entities=<n> left_allocations=<n|na> warmup_rounds=<n> warmup_settled=<0|1>
//   T1MEMSIZE     <type> <bytes>
// The same rows go into a JSON array when the environment variable T1MEM_OUT names a file. Rows are
// appended, so delete the file between runs that should not share it.
//
// What the numbers mean, and where they are blind:
// - A pass is one create, read and destroy cycle. Pass 1 includes the one-time growth of the tables that the
//   entities register in (they are warmed with 16 entities and then take 1000). Pass 2 runs the same cycle
//   again in the same process, so it is the steady cost per entity. Compare pass 2; pass 1 minus pass 2 is
//   printed as T1MEMGROWTH so that any difference between the passes is visible.
// - total_bytes is the sum of NumAllocatedBytes over every registered allocator, as asked. A child allocator
//   (and a pool that takes its pages from the SystemAllocator) is counted again inside its parent, so the sum
//   double counts. The per-allocator lines are the real split. ThreadPoolAllocator is skipped, as
//   LeakDetectionBase skips it. The bytes are usable sizes of the underlying allocator, so they step in size
//   classes (see the verdict rule).
// - The fixture turns on RECORD_FULL for every allocator, and a guard value is added to each allocation while
//   records exist. The guard stays when T1MEM_RECORDS=0, because it comes from the allocator and not from the
//   record mode. It adds the same few bytes per allocation in both builds.
// - allocations is the change in the number of live allocation records, summed over allocators. It is not
//   quantized, so it is the sharpest signal of the record-on run. It is na in a _RELEASE build, where the
//   profile calls are compiled out (AzCore/Memory/Config.h), and na in a records-off run. A _RELEASE run gives
//   no verdict.
// - EBus handler and address nodes are allocated through AZStd::stateless_allocator, which is the raw OS
//   allocator and is not tracked by any AZ allocator. total_bytes cannot see them. T1MEMOS reads the busy
//   blocks of the default process heap (HeapWalk) to see them. heap_blocks is the primary signal because a
//   block count is not quantized. The reading is calibrated first (T1MEMCAL): known blocks are allocated
//   through the same allocator, and the reading must see exactly those blocks, otherwise the OS column is
//   marked unusable.
// - T1MEMNOISY (private bytes) sees everything and is coarse: it moves in page and segment steps and includes
//   other threads.
// - The test application runs worker threads (streamer, asset manager, job manager). Whatever they allocate
//   between two readings is in the numbers. That is why the repeats must be identical.

#include "FrameworkApplicationFixture.h"
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Memory/AllocationRecords.h>
#include <AzCore/Memory/AllocatorManager.h>
#include <AzCore/Memory/IAllocator.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/Utils/Utils.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/allocator_stateless.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/parallel/scoped_lock.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzTest/AzTest.h>

#include <cstdio>

// The Transform 2 rows exist only in a tree that has the Transform 2 component.
#if defined(__has_include)
#if __has_include(<AzFramework/Components/Transform2Component.h>)
#include <AzFramework/Components/Transform2Component.h>
#define T1MEM_HAS_TRANSFORM2 1
#define T1MEM_HAS_TRANSFORM2_TEXT "true"
#endif
#endif
#if !defined(T1MEM_HAS_TRANSFORM2)
#define T1MEM_HAS_TRANSFORM2 0
#define T1MEM_HAS_TRANSFORM2_TEXT "false"
#endif

#if defined(AZ_PLATFORM_WINDOWS)
#include <AzCore/PlatformIncl.h>
#include <Psapi.h>
#endif

namespace UnitTest
{
    namespace T1MemoryFootprint
    {
        //////////////////////////////////////////////////////////////////////////
        // Settings and scenarios

        constexpr size_t EntityCount = 1000;
        constexpr size_t WarmupEntityCount = 16;
        constexpr int MinWarmupRounds = 3;
        constexpr int MaxWarmupRounds = 12;
        constexpr size_t ChainDepth = 10;
        constexpr int MaxAllocatorsRead = 128;
        [[maybe_unused]] constexpr int MaxModulesRead = 1024; // Windows only
        constexpr size_t NoParent = static_cast<size_t>(-1);

        // Pass 1 meets every one-time growth. The last pass is the steady one, which the verdict compares.
        constexpr int PassCount = 2;

        // The OS heap calibration: this many blocks of this size are allocated and must be seen by the reading.
        constexpr size_t CalibrationBlockCount = 1000;
        constexpr size_t CalibrationBlockSize = 64;
        [[maybe_unused]] constexpr size_t CalibrationAlignment = 16; // Windows only

        // The profile calls that fill the allocation records are compiled out in a _RELEASE build (AzCore/Memory/Config.h).
#if defined(_RELEASE)
        constexpr bool RecordsCompiledIn = false;
#else
        constexpr bool RecordsCompiledIn = true;
#endif

        enum class Topology
        {
            Flat,   // no parents
            Chains, // chains of ChainDepth entities, each parented to the one before it
            Fan,    // entity 0 is the parent of all the others
        };

        struct Scenario
        {
            const char* m_row;
            Topology m_topology;
            bool m_activate;        // false stops every entity after Init
            bool m_addTransform2;   // creates Transform2Component at a flipped scale instead of TransformComponent (Transform 2 build only)
        };

        // Env T1MEM_RECORDS=0 runs the row with the allocation records off, which is the run that reads the OS heap.
        bool RecordsAreSwitchedOff()
        {
            char valueBuffer[16] = {};
            const AZ::Utils::GetEnvOutcome outcome = AZ::Utils::GetEnv(valueBuffer, "T1MEM_RECORDS");
            return outcome.IsSuccess() && outcome.GetValue() == "0";
        }

        //////////////////////////////////////////////////////////////////////////
        // Readings

        struct AllocatorReading
        {
            const AZ::IAllocator* m_allocator = nullptr;
            const char* m_name = "";
            size_t m_bytes = 0;
            size_t m_allocations = 0; // live allocation records, 0 when the allocator keeps none
        };

        // A reading holds no heap memory, so taking one does not change the numbers it reads.
        struct Reading
        {
            AllocatorReading m_allocators[MaxAllocatorsRead];
            int m_allocatorCount = 0;
            size_t m_totalBytes = 0;
            size_t m_totalAllocations = 0;

            bool m_hasPrivateBytes = false;
            size_t m_privateBytes = 0;

            bool m_hasOsHeap = false;
            size_t m_osHeapBytes = 0;
            size_t m_osHeapBlocks = 0;
        };

        long long Delta(size_t after, size_t before)
        {
            return static_cast<long long>(after) - static_cast<long long>(before);
        }

#if defined(AZ_PLATFORM_WINDOWS)
        bool ReadPrivateBytes(size_t& privateBytes)
        {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            if (!::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
            {
                return false;
            }
            privateBytes = counters.PrivateUsage;
            return true;
        }

        // The number of modules loaded in the process, to tell two processes that started from different states apart.
        bool ReadModuleCount(size_t& moduleCount)
        {
            HMODULE modules[MaxModulesRead] = {};
            DWORD bytesNeeded = 0;
            if (!::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &bytesNeeded))
            {
                return false;
            }
            moduleCount = bytesNeeded / sizeof(HMODULE);
            return true;
        }

        // Sums the busy blocks of the default process heap, which is where the CRT (and so the OS allocator
        // behind AZStd::stateless_allocator) allocates. Nothing may allocate between HeapLock and HeapUnlock.
        bool ReadOsHeap(size_t& heapBytes, size_t& heapBlocks)
        {
            HANDLE heap = ::GetProcessHeap();
            if (heap == nullptr || !::HeapLock(heap))
            {
                return false;
            }

            size_t bytes = 0;
            size_t blocks = 0;
            PROCESS_HEAP_ENTRY entry{};
            while (::HeapWalk(heap, &entry))
            {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0)
                {
                    bytes += entry.cbData;
                    ++blocks;
                }
            }
            const bool walkedToTheEnd = (::GetLastError() == ERROR_NO_MORE_ITEMS);
            ::HeapUnlock(heap);

            if (!walkedToTheEnd)
            {
                return false;
            }
            heapBytes = bytes;
            heapBlocks = blocks;
            return true;
        }
#endif

        // The allocators are read the way LeakDetectionBase reads them. Allocators are not created or destroyed
        // while a test runs on this thread, so the manager is not locked (locking allocates).
        void TakeReading(Reading& reading, [[maybe_unused]] bool includeOsReadings)
        {
            AZ::AllocatorManager& manager = AZ::AllocatorManager::Instance();
            const int registeredCount = manager.GetNumAllocators();

            for (int i = 0; i < registeredCount && reading.m_allocatorCount < MaxAllocatorsRead; ++i)
            {
                AZ::IAllocator* allocator = manager.GetAllocator(i);

                // Skipped by LeakDetectionBase too (https://github.com/o3de/o3de/issues/13263).
                if (AZStd::string_view(allocator->GetName()) == "ThreadPoolAllocator")
                {
                    continue;
                }

                AllocatorReading& entry = reading.m_allocators[reading.m_allocatorCount++];
                entry.m_allocator = allocator;
                entry.m_name = allocator->GetName();
                entry.m_bytes = allocator->NumAllocatedBytes();
                if (AZ::Debug::AllocationRecords* records = allocator->GetRecords())
                {
                    AZStd::scoped_lock lock(*records);
                    entry.m_allocations = records->GetMap().size();
                }

                reading.m_totalBytes += entry.m_bytes;
                reading.m_totalAllocations += entry.m_allocations;
            }

#if defined(AZ_PLATFORM_WINDOWS)
            if (includeOsReadings)
            {
                reading.m_hasPrivateBytes = ReadPrivateBytes(reading.m_privateBytes);
                reading.m_hasOsHeap = ReadOsHeap(reading.m_osHeapBytes, reading.m_osHeapBlocks);
            }
#endif
        }

        //////////////////////////////////////////////////////////////////////////
        // OS heap calibration

        struct Calibration
        {
            bool m_ran = false;      // false when the platform has no OS heap reading, or the reading failed
            bool m_agrees = false;   // the reading saw exactly the blocks that were allocated
            long long m_observedBlocks = 0;
            long long m_observedBytes = 0;
        };

        // Allocates a known number of blocks through the allocator that the EBus nodes use, and checks that the OS heap
        // reading sees exactly those blocks. It settles whether HeapWalk can be trusted here (for example with the
        // low fragmentation heap). Another thread that allocates during the calibration makes it disagree, and
        // the OS column is then unusable for that run.
        Calibration CalibrateOsHeap()
        {
            Calibration calibration;
#if defined(AZ_PLATFORM_WINDOWS)
            void* blocks[CalibrationBlockCount] = {}; // on the stack, so the bookkeeping is not on the heap being read

            size_t bytesBefore = 0;
            size_t blocksBefore = 0;
            if (!ReadOsHeap(bytesBefore, blocksBefore))
            {
                return calibration;
            }

            AZStd::stateless_allocator allocator;
            for (void*& block : blocks)
            {
                block = allocator.allocate(CalibrationBlockSize, CalibrationAlignment);
            }

            size_t bytesAfter = 0;
            size_t blocksAfter = 0;
            const bool readAfter = ReadOsHeap(bytesAfter, blocksAfter);

            for (void* block : blocks)
            {
                allocator.deallocate(block, CalibrationBlockSize, CalibrationAlignment);
            }

            if (!readAfter)
            {
                return calibration;
            }
            calibration.m_ran = true;
            calibration.m_observedBlocks = Delta(blocksAfter, blocksBefore);
            calibration.m_observedBytes = Delta(bytesAfter, bytesBefore);

            // The bytes are only checked from below: the OS allocator adds its own header to each block.
            calibration.m_agrees = calibration.m_observedBlocks == static_cast<long long>(CalibrationBlockCount) &&
                calibration.m_observedBytes >= static_cast<long long>(CalibrationBlockCount * CalibrationBlockSize);
#endif
            return calibration;
        }

        //////////////////////////////////////////////////////////////////////////
        // Entities

        AZ::Entity* CreateEntity(const Scenario& scenario)
        {
            AZ::Entity* entity = aznew AZ::Entity();
#if T1MEM_HAS_TRANSFORM2
            if (scenario.m_addTransform2)
            {
                // A flipped scale does not fit Transform 1, so the Transform 2 is what deploys on such an entity.
                // It replaces the Transform 1, and it also deploys on every descendant of one.
                entity->CreateComponent<AzFramework::Transform2Component>(AZ::Vector3(-1.0f, 1.0f, 1.0f));
                return entity;
            }
#else
            (void)scenario;
#endif
            entity->CreateComponent<AzFramework::TransformComponent>();
            return entity;
        }

        // The index of the entity that entity `index` is parented to, or NoParent for a root.
        size_t GetParentIndex(Topology topology, size_t index)
        {
            switch (topology)
            {
            case Topology::Chains:
                return (index % ChainDepth == 0) ? NoParent : index - 1;
            case Topology::Fan:
                return (index == 0) ? NoParent : 0;
            case Topology::Flat:
                break;
            }
            return NoParent;
        }

        // Parents come before their children, so a parent is already registered (and active) when its child
        // is parented to it. SetParent can activate a child by itself, so Activate is only called on an
        // entity that is still in Init. The caller reserves room in `entities` so that growing the vector
        // does not allocate during a reading.
        void CreateEntities(const Scenario& scenario, size_t count, AZStd::vector<AZ::Entity*>& entities)
        {
            EXPECT_GE(entities.capacity(), entities.size() + count) << "reserve the entity vector before the first reading";

            for (size_t i = 0; i < count; ++i)
            {
                AZ::Entity* entity = CreateEntity(scenario);
                entities.push_back(entity);
                entity->Init();

                const size_t parentIndex = GetParentIndex(scenario.m_topology, i);
                if (parentIndex != NoParent)
                {
                    AZ::TransformBus::Event(entity->GetId(), &AZ::TransformBus::Events::SetParent, entities[parentIndex]->GetId());
                }

                if (scenario.m_activate && entity->GetState() == AZ::Entity::State::Init)
                {
                    entity->Activate();
                }
            }
        }

        // Children go before their parents.
        void DestroyEntities(AZStd::vector<AZ::Entity*>& entities)
        {
            for (auto it = entities.rbegin(); it != entities.rend(); ++it)
            {
                delete *it;
            }
            entities.clear();
        }

        size_t CountEntitiesInState(const AZStd::vector<AZ::Entity*>& entities, AZ::Entity::State state)
        {
            size_t count = 0;
            for (const AZ::Entity* entity : entities)
            {
                if (entity->GetState() == state)
                {
                    ++count;
                }
            }
            return count;
        }

        //////////////////////////////////////////////////////////////////////////
        // Warm up

        // The first use of a bus allocates its context, and the first entities size the tables, and none of that is
        // a per-entity cost. Create and destroy a few entities until a whole round leaves the allocators exactly
        // where it found them, twice in a row. (The tables still grow from this size to the size of the 1000
        // entities in pass 1; pass 2 is the pass without that growth.)
        void WarmUp(const Scenario& scenario, int& rounds, bool& settled)
        {
            AZStd::vector<AZ::Entity*> entities;
            entities.reserve(WarmupEntityCount);

            int settledInARow = 0;
            rounds = 0;
            while (rounds < MaxWarmupRounds && !(rounds >= MinWarmupRounds && settledInARow >= 2))
            {
                Reading before;
                TakeReading(before, false);

                CreateEntities(scenario, WarmupEntityCount, entities);
                DestroyEntities(entities);

                Reading after;
                TakeReading(after, false);

                settledInARow = (after.m_totalBytes == before.m_totalBytes) ? settledInARow + 1 : 0;
                ++rounds;
            }
            settled = (settledInARow >= 2);
        }

        //////////////////////////////////////////////////////////////////////////
        // Summary

        struct AllocatorDelta
        {
            const char* m_name;
            long long m_created; // after create minus before
            long long m_left;    // after destroy minus before
        };

        struct AllocatorBytes
        {
            const char* m_name;
            size_t m_bytes;
        };

        struct TypeSize
        {
            const char* m_name;
            size_t m_bytes;
        };

        // The numbers of one create, read and destroy cycle.
        struct PassSummary
        {
            long long m_totalBytes = 0;
            long long m_allocations = 0;
            long long m_leftBytes = 0;
            long long m_leftAllocations = 0;

            bool m_hasPrivateBytes = false;
            long long m_privateBytes = 0;
            long long m_privateBytesLeft = 0;

            bool m_hasOsHeap = false;
            long long m_osHeapBytes = 0;
            long long m_osHeapBlocks = 0;
            long long m_osHeapLeftBytes = 0;
            long long m_osHeapLeftBlocks = 0;

            AZStd::vector<AllocatorDelta> m_allocators;
        };

        // The state the process was in before the first pass, so that two outputs can be told apart before a
        // delta is read: the same source in two trees can still start from different states.
        struct Environment
        {
            AZStd::string m_buildFlags;
            int m_allocatorCount = 0;
            bool m_hasModuleCount = false;
            size_t m_moduleCount = 0;
            size_t m_totalBytes = 0;
            size_t m_totalAllocations = 0;
            AZStd::vector<AllocatorBytes> m_allocators;
        };

        struct Summary
        {
            const char* m_row = "";
            const char* m_topology = "";
            bool m_activated = false;
            bool m_addsTransform2 = false;
            int m_warmupRounds = 0;
            bool m_warmupSettled = false;

            bool m_recordsCompiledIn = true; // false in a _RELEASE build, which gives no verdict
            bool m_recordsOn = true;         // false when T1MEM_RECORDS=0, and in a _RELEASE build

            Calibration m_calibration;
            Environment m_environment;
            PassSummary m_passes[PassCount];
            AZStd::vector<TypeSize> m_sizes;
        };

        double PerEntity(long long bytes)
        {
            return static_cast<double>(bytes) / static_cast<double>(EntityCount);
        }

        const char* GetPassRole(int passNumber)
        {
            return (passNumber == PassCount) ? "steady" : "first";
        }

        size_t BytesOf(const Reading& reading, const AZ::IAllocator* allocator)
        {
            for (int i = 0; i < reading.m_allocatorCount; ++i)
            {
                if (reading.m_allocators[i].m_allocator == allocator)
                {
                    return reading.m_allocators[i].m_bytes;
                }
            }
            return 0;
        }

        // Every allocator seen in any of the three readings, with its change by name. An allocator that
        // appeared in the middle counts from zero.
        AZStd::vector<AllocatorDelta> CollectAllocatorDeltas(const Reading& before, const Reading& afterCreate, const Reading& afterDestroy)
        {
            const Reading* readings[] = { &before, &afterCreate, &afterDestroy };

            AZStd::vector<AllocatorDelta> deltas;
            AZStd::vector<const AZ::IAllocator*> seen;
            for (const Reading* reading : readings)
            {
                for (int i = 0; i < reading->m_allocatorCount; ++i)
                {
                    const AZ::IAllocator* allocator = reading->m_allocators[i].m_allocator;
                    if (AZStd::find(seen.begin(), seen.end(), allocator) != seen.end())
                    {
                        continue;
                    }
                    seen.push_back(allocator);

                    const size_t beforeBytes = BytesOf(before, allocator);
                    deltas.push_back(AllocatorDelta{
                        reading->m_allocators[i].m_name,
                        Delta(BytesOf(afterCreate, allocator), beforeBytes),
                        Delta(BytesOf(afterDestroy, allocator), beforeBytes) });
                }
            }
            return deltas;
        }

        AZStd::vector<TypeSize> CollectTypeSizes()
        {
            AZStd::vector<TypeSize> sizes;
            sizes.push_back(TypeSize{ "AZ::Entity", sizeof(AZ::Entity) });
            sizes.push_back(TypeSize{ "AZ::Component", sizeof(AZ::Component) });
            sizes.push_back(TypeSize{ "AZ::EntityId", sizeof(AZ::EntityId) });
            sizes.push_back(TypeSize{ "AzFramework::TransformComponent", sizeof(AzFramework::TransformComponent) });
            sizes.push_back(TypeSize{ "AZ::Transform", sizeof(AZ::Transform) });
            sizes.push_back(TypeSize{ "AZ::TransformConfig", sizeof(AZ::TransformConfig) });
#if T1MEM_HAS_TRANSFORM2
            sizes.push_back(TypeSize{ "AzFramework::Transform2Component", sizeof(AzFramework::Transform2Component) });
#endif
            return sizes;
        }

        const char* GetTopologyName(Topology topology)
        {
            switch (topology)
            {
            case Topology::Chains:
                return "chains";
            case Topology::Fan:
                return "fan";
            case Topology::Flat:
                break;
            }
            return "flat";
        }

        void AddFlag(AZStd::string& flags, const char* flag)
        {
            flags += flags.empty() ? "" : ",";
            flags += flag;
        }

        // The configuration the source was compiled in. Both trees must show the same flags.
        AZStd::string GetBuildFlags()
        {
            AZStd::string flags;
#if defined(_RELEASE)
            AddFlag(flags, "_RELEASE");
#endif
#if defined(_DEBUG)
            AddFlag(flags, "_DEBUG");
#endif
#if defined(_PROFILE)
            AddFlag(flags, "_PROFILE");
#endif
#if defined(NDEBUG)
            AddFlag(flags, "NDEBUG");
#endif
#if defined(AZ_ENABLE_TRACING)
            AddFlag(flags, "AZ_ENABLE_TRACING");
#endif
            if (flags.empty())
            {
                flags = "none";
            }
            return flags;
        }

        Environment CaptureEnvironment()
        {
            Reading reading;
            TakeReading(reading, false);

            Environment environment;
            environment.m_buildFlags = GetBuildFlags();
            environment.m_allocatorCount = reading.m_allocatorCount;
            environment.m_totalBytes = reading.m_totalBytes;
            environment.m_totalAllocations = reading.m_totalAllocations;
            for (int i = 0; i < reading.m_allocatorCount; ++i)
            {
                environment.m_allocators.push_back(AllocatorBytes{ reading.m_allocators[i].m_name, reading.m_allocators[i].m_bytes });
            }
#if defined(AZ_PLATFORM_WINDOWS)
            environment.m_hasModuleCount = ReadModuleCount(environment.m_moduleCount);
#endif
            return environment;
        }

        // Everything about the row that is known before the passes run.
        Summary BeginSummary(const Scenario& scenario, int warmupRounds, bool warmupSettled, bool recordsSwitchedOff)
        {
            Summary summary;
            summary.m_row = scenario.m_row;
            summary.m_topology = GetTopologyName(scenario.m_topology);
            summary.m_activated = scenario.m_activate;
            summary.m_addsTransform2 = scenario.m_addTransform2;
            summary.m_warmupRounds = warmupRounds;
            summary.m_warmupSettled = warmupSettled;
            summary.m_recordsCompiledIn = RecordsCompiledIn;
            summary.m_recordsOn = RecordsCompiledIn && !recordsSwitchedOff;
            summary.m_calibration = CalibrateOsHeap();
            summary.m_environment = CaptureEnvironment();
            summary.m_sizes = CollectTypeSizes();
            return summary;
        }

        PassSummary SummarizePass(const Reading& before, const Reading& afterCreate, const Reading& afterDestroy)
        {
            PassSummary pass;
            pass.m_totalBytes = Delta(afterCreate.m_totalBytes, before.m_totalBytes);
            pass.m_allocations = Delta(afterCreate.m_totalAllocations, before.m_totalAllocations);
            pass.m_leftBytes = Delta(afterDestroy.m_totalBytes, before.m_totalBytes);
            pass.m_leftAllocations = Delta(afterDestroy.m_totalAllocations, before.m_totalAllocations);

            pass.m_hasPrivateBytes = before.m_hasPrivateBytes && afterCreate.m_hasPrivateBytes && afterDestroy.m_hasPrivateBytes;
            pass.m_privateBytes = Delta(afterCreate.m_privateBytes, before.m_privateBytes);
            pass.m_privateBytesLeft = Delta(afterDestroy.m_privateBytes, before.m_privateBytes);

            pass.m_hasOsHeap = before.m_hasOsHeap && afterCreate.m_hasOsHeap && afterDestroy.m_hasOsHeap;
            pass.m_osHeapBytes = Delta(afterCreate.m_osHeapBytes, before.m_osHeapBytes);
            pass.m_osHeapBlocks = Delta(afterCreate.m_osHeapBlocks, before.m_osHeapBlocks);
            pass.m_osHeapLeftBytes = Delta(afterDestroy.m_osHeapBytes, before.m_osHeapBytes);
            pass.m_osHeapLeftBlocks = Delta(afterDestroy.m_osHeapBlocks, before.m_osHeapBlocks);

            pass.m_allocators = CollectAllocatorDeltas(before, afterCreate, afterDestroy);
            return pass;
        }

        // Why the OS heap numbers of a pass cannot be trusted, or nullptr when they can. The record tables live on the
        // same heap, so the numbers only mean something with the records off.
        const char* GetOsHeapInvalidReason(const Summary& summary, const PassSummary& pass)
        {
            if (summary.m_recordsOn)
            {
                return "records_on";
            }
            if (!summary.m_calibration.m_ran)
            {
                return "calibration_not_run";
            }
            if (!summary.m_calibration.m_agrees)
            {
                return "calibration_disagrees";
            }
            if (!pass.m_hasOsHeap)
            {
                return "no_reading";
            }
            return nullptr;
        }

        bool IsOsHeapValid(const Summary& summary, const PassSummary& pass)
        {
            return GetOsHeapInvalidReason(summary, pass) == nullptr;
        }

        //////////////////////////////////////////////////////////////////////////
        // Passes

        // One create, read and destroy cycle. Everything that allocates for the report is outside the readings.
        PassSummary RunPass(const Scenario& scenario)
        {
            // The pointer vector is reserved before the first reading so that it is not part of what is measured.
            AZStd::vector<AZ::Entity*> entities;
            entities.reserve(EntityCount);

            Reading before;
            Reading afterCreate;
            Reading afterDestroy;

            TakeReading(before, true);
            CreateEntities(scenario, EntityCount, entities);
            TakeReading(afterCreate, true);

            // Only what makes the reading valid is asserted: every entity reached the state the row is about.
            const AZ::Entity::State expectedState = scenario.m_activate ? AZ::Entity::State::Active : AZ::Entity::State::Init;
            const size_t entitiesInExpectedState = CountEntitiesInState(entities, expectedState);

            // Everything is destroyed even when the check fails, so that leak detection stays quiet.
            DestroyEntities(entities);
            TakeReading(afterDestroy, true);

            EXPECT_EQ(entitiesInExpectedState, EntityCount) << "row " << scenario.m_row << ": not every entity reached the expected state";

            return SummarizePass(before, afterCreate, afterDestroy);
        }

        //////////////////////////////////////////////////////////////////////////
        // Output

        // A number that a platform may not be able to read: "na" in the text, null in JSON.
        AZStd::string OptionalNumber(bool available, long long value, const char* unavailableText)
        {
            return available ? AZStd::string::format("%lld", value) : AZStd::string(unavailableText);
        }

        AZStd::string FormatText(const Summary& summary)
        {
            const char* row = summary.m_row;
            const Environment& environment = summary.m_environment;
            const Calibration& calibration = summary.m_calibration;
            AZStd::string text;

            // The starting state comes first, so that two outputs can be told apart before a delta is read.
            text += AZStd::string::format(
                "T1MEMENV %s transform2_header=%s build_flags=%s pointer_bytes=%llu records=%s allocators=%d modules=%s "
                "before_total_bytes=%llu before_allocations=%s\n",
                row, T1MEM_HAS_TRANSFORM2_TEXT, environment.m_buildFlags.c_str(), static_cast<unsigned long long>(sizeof(void*)),
                summary.m_recordsOn ? "on" : "off", environment.m_allocatorCount,
                OptionalNumber(environment.m_hasModuleCount, static_cast<long long>(environment.m_moduleCount), "na").c_str(),
                static_cast<unsigned long long>(environment.m_totalBytes),
                OptionalNumber(summary.m_recordsOn, static_cast<long long>(environment.m_totalAllocations), "na").c_str());
            for (const AllocatorBytes& allocator : environment.m_allocators)
            {
                text += AZStd::string::format(
                    "T1MEMENVALLOC %s %s %llu\n", row, allocator.m_name, static_cast<unsigned long long>(allocator.m_bytes));
            }

            text += AZStd::string::format(
                "T1MEMCAL %s ran=%d agrees=%d blocks_expected=%llu blocks_observed=%lld bytes_expected_min=%llu bytes_observed=%lld\n",
                row, calibration.m_ran ? 1 : 0, calibration.m_agrees ? 1 : 0, static_cast<unsigned long long>(CalibrationBlockCount),
                calibration.m_observedBlocks, static_cast<unsigned long long>(CalibrationBlockCount * CalibrationBlockSize),
                calibration.m_observedBytes);

            text += AZStd::string::format(
                "T1MEMRULE %s t1_cleared_only_if_all_equal=sizeof,allocations,allocator_bytes,heap_blocks_records_off_run "
                "total_bytes_alone_clears_nothing=1 compare_pass=%d repeats_must_be_identical=1 verdict_possible=%d\n",
                row, PassCount, summary.m_recordsCompiledIn ? 1 : 0);

            for (int index = 0; index < PassCount; ++index)
            {
                const PassSummary& pass = summary.m_passes[index];
                const int passNumber = index + 1;

                text += AZStd::string::format(
                    "T1MEM %s pass=%d role=%s total_bytes=%lld per_entity=%.2f allocations=%s left_after_destroy=%lld private_bytes=%s\n",
                    row, passNumber, GetPassRole(passNumber), pass.m_totalBytes, PerEntity(pass.m_totalBytes),
                    OptionalNumber(summary.m_recordsOn, pass.m_allocations, "na").c_str(), pass.m_leftBytes,
                    OptionalNumber(pass.m_hasPrivateBytes, pass.m_privateBytes, "na").c_str());

                for (const AllocatorDelta& delta : pass.m_allocators)
                {
                    text += AZStd::string::format("T1MEMALLOC %s pass=%d %s %lld\n", row, passNumber, delta.m_name, delta.m_created);
                }
                for (const AllocatorDelta& delta : pass.m_allocators)
                {
                    if (delta.m_left != 0)
                    {
                        text += AZStd::string::format("T1MEMLEFT %s pass=%d %s %lld\n", row, passNumber, delta.m_name, delta.m_left);
                    }
                }

                if (IsOsHeapValid(summary, pass))
                {
                    text += AZStd::string::format(
                        "T1MEMOS %s pass=%d valid=1 heap_bytes=%lld heap_per_entity=%.2f heap_blocks=%lld heap_left_bytes=%lld "
                        "heap_left_blocks=%lld note=raw_os_heap_records_off\n",
                        row, passNumber, pass.m_osHeapBytes, PerEntity(pass.m_osHeapBytes), pass.m_osHeapBlocks, pass.m_osHeapLeftBytes,
                        pass.m_osHeapLeftBlocks);
                }
                else
                {
                    text += AZStd::string::format(
                        "T1MEMOS %s pass=%d valid=0 reason=%s\n", row, passNumber, GetOsHeapInvalidReason(summary, pass));
                }

                if (pass.m_hasPrivateBytes)
                {
                    text += AZStd::string::format(
                        "T1MEMNOISY %s pass=%d private_bytes=%lld private_bytes_after_destroy=%lld note=process_wide_and_noisy\n", row,
                        passNumber, pass.m_privateBytes, pass.m_privateBytesLeft);
                }
            }

            // Pass 1 minus the steady pass: what the first pass paid once and the steady pass does not.
            const PassSummary& first = summary.m_passes[0];
            const PassSummary& steady = summary.m_passes[PassCount - 1];
            const bool osGrowthValid = IsOsHeapValid(summary, first) && IsOsHeapValid(summary, steady);
            text += AZStd::string::format(
                "T1MEMGROWTH %s total_bytes=%lld allocations=%s heap_blocks=%s note=pass_1_minus_pass_%d\n", row,
                first.m_totalBytes - steady.m_totalBytes,
                OptionalNumber(summary.m_recordsOn, first.m_allocations - steady.m_allocations, "na").c_str(),
                OptionalNumber(osGrowthValid, first.m_osHeapBlocks - steady.m_osHeapBlocks, "na").c_str(), PassCount);

            text += AZStd::string::format(
                "T1MEMEXTRA %s entities=%llu left_allocations=%s warmup_rounds=%d warmup_settled=%d\n", row,
                static_cast<unsigned long long>(EntityCount),
                OptionalNumber(summary.m_recordsOn, steady.m_leftAllocations, "na").c_str(), summary.m_warmupRounds,
                summary.m_warmupSettled ? 1 : 0);

            for (const TypeSize& size : summary.m_sizes)
            {
                text += AZStd::string::format("T1MEMSIZE %s %llu\n", size.m_name, static_cast<unsigned long long>(size.m_bytes));
            }
            return text;
        }

        AZStd::string JsonEscape(const char* text)
        {
            AZStd::string escaped;
            for (const char* c = text; *c != '\0'; ++c)
            {
                if (*c == '"' || *c == '\\')
                {
                    escaped += '\\';
                    escaped += *c;
                }
                else if (static_cast<unsigned char>(*c) < 0x20)
                {
                    escaped += ' ';
                }
                else
                {
                    escaped += *c;
                }
            }
            return escaped;
        }

        // One pass as a JSON object.
        AZStd::string FormatPassJson(const Summary& summary, const PassSummary& pass, int passNumber)
        {
            const bool osValid = IsOsHeapValid(summary, pass);

            AZStd::string json;
            json += AZStd::string::format(
                "{\"pass\":%d,\"role\":\"%s\",\"total_bytes\":%lld,\"per_entity\":%.2f,\"allocations\":%s,\"left_after_destroy\":%lld,"
                "\"left_allocations\":%s,",
                passNumber, GetPassRole(passNumber), pass.m_totalBytes, PerEntity(pass.m_totalBytes),
                OptionalNumber(summary.m_recordsOn, pass.m_allocations, "null").c_str(), pass.m_leftBytes,
                OptionalNumber(summary.m_recordsOn, pass.m_leftAllocations, "null").c_str());
            json += AZStd::string::format(
                "\"private_bytes\":%s,\"private_bytes_after_destroy\":%s,",
                OptionalNumber(pass.m_hasPrivateBytes, pass.m_privateBytes, "null").c_str(),
                OptionalNumber(pass.m_hasPrivateBytes, pass.m_privateBytesLeft, "null").c_str());
            json += AZStd::string::format(
                "\"os_heap_valid\":%s,\"os_heap_bytes\":%s,\"os_heap_blocks\":%s,\"os_heap_left_bytes\":%s,\"os_heap_left_blocks\":%s,",
                osValid ? "true" : "false", OptionalNumber(osValid, pass.m_osHeapBytes, "null").c_str(),
                OptionalNumber(osValid, pass.m_osHeapBlocks, "null").c_str(),
                OptionalNumber(osValid, pass.m_osHeapLeftBytes, "null").c_str(),
                OptionalNumber(osValid, pass.m_osHeapLeftBlocks, "null").c_str());

            json += "\"allocators\":[";
            for (size_t i = 0; i < pass.m_allocators.size(); ++i)
            {
                json += AZStd::string::format(
                    "%s{\"name\":\"%s\",\"created\":%lld,\"left\":%lld}", i == 0 ? "" : ",", JsonEscape(pass.m_allocators[i].m_name).c_str(),
                    pass.m_allocators[i].m_created, pass.m_allocators[i].m_left);
            }
            json += "]}";
            return json;
        }

        // One JSON object on one line.
        AZStd::string FormatJson(const Summary& summary)
        {
            const Environment& environment = summary.m_environment;
            const Calibration& calibration = summary.m_calibration;

            AZStd::string json;
            json += AZStd::string::format(
                "{\"row\":\"%s\",\"topology\":\"%s\",\"activated\":%s,\"adds_transform2\":%s,\"transform2_header\":%s,\"entities\":%llu,",
                JsonEscape(summary.m_row).c_str(), summary.m_topology, summary.m_activated ? "true" : "false",
                summary.m_addsTransform2 ? "true" : "false", T1MEM_HAS_TRANSFORM2_TEXT, static_cast<unsigned long long>(EntityCount));
            json += AZStd::string::format(
                "\"records_on\":%s,\"verdict_possible\":%s,\"build_flags\":\"%s\",\"pointer_bytes\":%llu,\"allocator_count\":%d,"
                "\"module_count\":%s,\"before_total_bytes\":%llu,\"before_allocations\":%s,",
                summary.m_recordsOn ? "true" : "false", summary.m_recordsCompiledIn ? "true" : "false",
                JsonEscape(environment.m_buildFlags.c_str()).c_str(), static_cast<unsigned long long>(sizeof(void*)),
                environment.m_allocatorCount,
                OptionalNumber(environment.m_hasModuleCount, static_cast<long long>(environment.m_moduleCount), "null").c_str(),
                static_cast<unsigned long long>(environment.m_totalBytes),
                OptionalNumber(summary.m_recordsOn, static_cast<long long>(environment.m_totalAllocations), "null").c_str());
            json += AZStd::string::format(
                "\"calibration\":{\"ran\":%s,\"agrees\":%s,\"blocks_expected\":%llu,\"blocks_observed\":%lld,\"bytes_expected_min\":%llu,"
                "\"bytes_observed\":%lld},",
                calibration.m_ran ? "true" : "false", calibration.m_agrees ? "true" : "false",
                static_cast<unsigned long long>(CalibrationBlockCount), calibration.m_observedBlocks,
                static_cast<unsigned long long>(CalibrationBlockCount * CalibrationBlockSize), calibration.m_observedBytes);
            json += AZStd::string::format(
                "\"warmup_rounds\":%d,\"warmup_settled\":%s,", summary.m_warmupRounds, summary.m_warmupSettled ? "true" : "false");

            json += "\"sizeof\":{";
            for (size_t i = 0; i < summary.m_sizes.size(); ++i)
            {
                json += AZStd::string::format(
                    "%s\"%s\":%llu", i == 0 ? "" : ",", JsonEscape(summary.m_sizes[i].m_name).c_str(),
                    static_cast<unsigned long long>(summary.m_sizes[i].m_bytes));
            }
            json += "},\"before_allocators\":[";
            for (size_t i = 0; i < environment.m_allocators.size(); ++i)
            {
                json += AZStd::string::format(
                    "%s{\"name\":\"%s\",\"bytes\":%llu}", i == 0 ? "" : ",", JsonEscape(environment.m_allocators[i].m_name).c_str(),
                    static_cast<unsigned long long>(environment.m_allocators[i].m_bytes));
            }
            json += "],\"passes\":[";
            for (int index = 0; index < PassCount; ++index)
            {
                json += (index == 0) ? "" : ",";
                json += FormatPassJson(summary, summary.m_passes[index], index + 1);
            }
            json += "]}";
            return json;
        }

        bool IsJsonSpace(char c)
        {
            return c == ' ' || c == '\n' || c == '\r' || c == '\t';
        }

        void TrimTrailingSpace(AZStd::string& text)
        {
            while (!text.empty() && IsJsonSpace(text.back()))
            {
                text.pop_back();
            }
        }

        // Appends one row to the JSON array in the file named by T1MEM_OUT. Does nothing when it is not set.
        void AppendJsonRow(const AZStd::string& jsonRow)
        {
            char pathBuffer[1024] = {};
            const AZ::Utils::GetEnvOutcome envOutcome = AZ::Utils::GetEnv(pathBuffer, "T1MEM_OUT");
            if (!envOutcome.IsSuccess() || envOutcome.GetValue().empty())
            {
                return;
            }
            const AZStd::string path(envOutcome.GetValue().data(), envOutcome.GetValue().size());

            // Take the rows of an earlier run, if the file holds a JSON array, and drop its closing bracket.
            AZStd::string content;
            if (AZ::IO::SystemFile::Exists(path.c_str()))
            {
                auto existing = AZ::Utils::ReadFile<AZStd::string>(path);
                if (existing.IsSuccess())
                {
                    content = existing.TakeValue();
                }
            }
            TrimTrailingSpace(content);
            const bool isArray = content.size() >= 2 && content.front() == '[' && content.back() == ']';
            if (isArray)
            {
                content.pop_back();
                TrimTrailingSpace(content);
            }
            else
            {
                content = "[";
            }

            content += (content.back() == '[') ? "\n  " : ",\n  ";
            content += jsonRow;
            content += "\n]\n";

            const AZ::Outcome<void, AZStd::string> written = AZ::Utils::WriteFile(content, path);
            if (!written.IsSuccess())
            {
                ADD_FAILURE() << "T1MEM_OUT could not be written: " << written.GetError().c_str();
            }
        }

        void Report(const Summary& summary)
        {
            const AZStd::string text = FormatText(summary);
            std::fputs(text.c_str(), stdout);
            std::fflush(stdout);

            AppendJsonRow(FormatJson(summary));
        }
    } // namespace T1MemoryFootprint

    //////////////////////////////////////////////////////////////////////////
    // Fixture
    //
    // The lightest fixture that works: an entity needs the component descriptors for its dependency sort, and
    // Init registers it with the component application, both of which the Application provides. It is the
    // fixture Transform2Tests uses, and it exists in the baseline tree.

    class T1MemoryFootprintTest
        : public FrameworkApplicationFixture
    {
    protected:
        void RunRow(const T1MemoryFootprint::Scenario& scenario)
        {
            namespace Footprint = T1MemoryFootprint;

            // Switching the records off clears the record tables, so it is done once, here, before the warm up and
            // before any reading, and never inside a reading window. The default mode is switched too, so that an
            // allocator created later does not start recording.
            const bool recordsSwitchedOff = Footprint::RecordsAreSwitchedOff();
            if (recordsSwitchedOff)
            {
                AZ::AllocatorManager::Instance().SetDefaultTrackingMode(AZ::Debug::AllocationRecords::Mode::RECORD_NO_RECORDS);
                AZ::AllocatorManager::Instance().SetTrackingMode(AZ::Debug::AllocationRecords::Mode::RECORD_NO_RECORDS);
            }

            int warmupRounds = 0;
            bool warmupSettled = false;
            Footprint::WarmUp(scenario, warmupRounds, warmupSettled);

            // The calibration and the starting state are taken after the warm up and before the first pass.
            Footprint::Summary summary = Footprint::BeginSummary(scenario, warmupRounds, warmupSettled, recordsSwitchedOff);

            // Pass 1 meets the one-time growth. The last pass is the steady one.
            for (int pass = 0; pass < Footprint::PassCount; ++pass)
            {
                summary.m_passes[pass] = Footprint::RunPass(scenario);
            }

            Footprint::Report(summary);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Transform 1 rows (also built against the baseline)

    TEST_F(T1MemoryFootprintTest, Flat)
    {
        RunRow({ "flat", T1MemoryFootprint::Topology::Flat, true, false });
    }

    TEST_F(T1MemoryFootprintTest, FlatNotActivated)
    {
        RunRow({ "flat_not_activated", T1MemoryFootprint::Topology::Flat, false, false });
    }

    TEST_F(T1MemoryFootprintTest, HierarchyChains)
    {
        RunRow({ "hierarchy_chains", T1MemoryFootprint::Topology::Chains, true, false });
    }

    TEST_F(T1MemoryFootprintTest, HierarchyFan)
    {
        RunRow({ "hierarchy_fan", T1MemoryFootprint::Topology::Fan, true, false });
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2 rows (only where the Transform 2 component exists)

#if T1MEM_HAS_TRANSFORM2
    TEST_F(T1MemoryFootprintTest, Transform2Flat)
    {
        RunRow({ "t2_flat", T1MemoryFootprint::Topology::Flat, true, true });
    }

    TEST_F(T1MemoryFootprintTest, Transform2FlatNotActivated)
    {
        RunRow({ "t2_flat_not_activated", T1MemoryFootprint::Topology::Flat, false, true });
    }

    TEST_F(T1MemoryFootprintTest, Transform2Chains)
    {
        RunRow({ "t2_chains", T1MemoryFootprint::Topology::Chains, true, true });
    }

    TEST_F(T1MemoryFootprintTest, Transform2Fan)
    {
        RunRow({ "t2_fan", T1MemoryFootprint::Topology::Fan, true, true });
    }
#endif
} // namespace UnitTest
