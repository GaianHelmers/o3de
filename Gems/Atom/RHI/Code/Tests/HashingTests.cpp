/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "RHITestFixture.h"
#include <Tests/Factory.h>
#include <AzCore/Math/Random.h>
#include <AzCore/Utils/TypeHash.h>
#include <Atom/RHI.Reflect/BufferDescriptor.h>
#include <Atom/RHI.Reflect/BufferViewDescriptor.h>
#include <Atom/RHI.Reflect/ImageDescriptor.h>
#include <Atom/RHI.Reflect/ImageViewDescriptor.h>
#include <Atom/RHI.Reflect/TransientBufferDescriptor.h>
#include <Atom/RHI.Reflect/TransientImageDescriptor.h>
#include <Atom/RHI.Reflect/PipelineLayoutDescriptor.h>
#include <Atom/RHI.Reflect/SamplerState.h>
#include <Atom/RHI.Reflect/InputStreamLayout.h>
#include <Atom/RHI.Reflect/ShaderStageFunction.h>
#include <Atom/RHI/PipelineStateDescriptor.h>
#include <cstddef>
#include <cstring>

namespace UnitTest
{
    using namespace AZ;

    class ShaderStageFunction : public RHI::ShaderStageFunction
    {
    public:
        AZ_CLASS_ALLOCATOR(ShaderStageFunction, SystemAllocator);

        ShaderStageFunction(uint64_t hash, RHI::ShaderStage shaderStage)
            : RHI::ShaderStageFunction(shaderStage)
            , m_declaredHash{hash}
        {}

        RHI::ResultCode FinalizeInternal() override
        {
            SetHash(HashValue64(m_declaredHash));
            return RHI::ResultCode::Success;
        }

    private:
        uint64_t m_declaredHash;
    };

    struct BaselineStructWithPadding
    {
        BaselineStructWithPadding()
        {
            m_b = 1;
            m_c = reinterpret_cast<const uint32_t*>(0x12345);
        }

        uint8_t m_b;
        // 7 bytes of padding.
        const uint32_t* m_c;

        HashValue64 GetHash() const
        {
            return TypeHash64(*this);
        }
    };

    static_assert(sizeof(BaselineStructWithPadding) == 16, "Baseline struct does not exhibit expected padding.");

    /**
     * These tests validate that RHI structs which expose a 'GetHash()' method
     * will produce a reliable hash. Specifically, it tests the hash under conditions
     * where the underlying memory includes pad bytes which are uninitialized.
     */

    class HashingTests
        : public RHITestFixture
    {
    public:
        HashingTests()
            : RHITestFixture()
        {
        }

    protected:
        enum class Expect
        {
            Success = 0,
            Failure
        };

        void ScrambleMemory(void* memory, size_t size)
        {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(memory);

            for (size_t i = 0; i < size; ++i)
            {
                bytes[i] = static_cast<uint8_t>(m_random.GetRandom());
            }
        }

        template <typename T, Expect expect = Expect::Success>
        void TestHash(AZStd::function<void(T&)> initFunction = nullptr)
        {
            void* memory = azmalloc(sizeof(T));
            T* type = nullptr;

            uint64_t hashPrev = 0;
            bool isFirstHash = true;

            const size_t IterationCount = 10;
            for (size_t i = 0; i < IterationCount; ++i)
            {
                ScrambleMemory(memory, sizeof(T));
                type = new (memory) T();

                if (initFunction)
                {
                    initFunction(*type);
                }

                uint64_t hashCurr = static_cast<uint64_t>(type->GetHash());

                if (!isFirstHash)
                {
                    if constexpr (expect == Expect::Success)
                    {
                        EXPECT_EQ(hashCurr, hashPrev);
                    }
                    else
                    {
                        EXPECT_NE(hashCurr, hashPrev);
                    }
                }

                isFirstHash = false;
                hashPrev = hashCurr;

                type->~T();
            }

            azfree(memory);
        }

        RHI::ConstPtr<RHI::ShaderResourceGroupLayout> CreateShaderResourceGroupLayout()
        {
            RHI::Ptr<RHI::ShaderResourceGroupLayout> layout = RHI::ShaderResourceGroupLayout::Create();

            // The values here are just arbitrary.
            layout->SetBindingSlot(0);
            layout->AddShaderInput(RHI::ShaderInputConstantDescriptor{ Name("InputA"), 0, 12, 0, 0});
            layout->AddShaderInput(RHI::ShaderInputConstantDescriptor{ Name("InputB"), 12, 12, 0, 0});
            layout->AddShaderInput(RHI::ShaderInputConstantDescriptor{ Name("InputC"), 24, 76, 0, 0});
            layout->AddStaticSampler(RHI::ShaderInputStaticSamplerDescriptor{ Name("StaticSamplerA"), RHI::SamplerState::CreateAnisotropic(16, RHI::AddressMode::Wrap), 1, 1});
            layout->Finalize();

            return layout;
        }

        RHI::ShaderResourceGroupBindingInfo CreateShaderResourceGroupBindingInfo()
        {
            RHI::ShaderResourceGroupBindingInfo bindingInfo;
            bindingInfo.m_constantDataBindingInfo.m_registerId = 0;
            bindingInfo.m_resourcesRegisterMap.emplace("StaticSamplerA", RHI::ResourceBindingInfo{RHI::ShaderStageMask::Vertex, 1, 1});

            return bindingInfo;
        }

        void InitStreamLayout(RHI::InputStreamLayout& layout)
        {
            // The values here are just arbitrary (Except for buffer index which is validated in Finalize())
            layout.SetTopology(RHI::PrimitiveTopology::TriangleList);
            layout.AddStreamBuffer(RHI::StreamBufferDescriptor{ RHI::StreamStepFunction::PerVertex, 1, 8 });
            layout.AddStreamBuffer(RHI::StreamBufferDescriptor{ RHI::StreamStepFunction::PerInstance, 3, 16 });
            layout.AddStreamChannel(RHI::StreamChannelDescriptor{ RHI::ShaderSemantic{Name("ChannelA"), 1}, RHI::Format::R8G8B8A8_UINT, 4, 0});
            layout.AddStreamChannel(RHI::StreamChannelDescriptor{ RHI::ShaderSemantic{Name("ChannelB"), 1}, RHI::Format::R10G10B10A2_UINT, 7, 1});
            layout.Finalize();
        }

        void InitRenderAttachmentLayout(RHI::RenderAttachmentLayout& layout)
        {
            // The values here are just arbitrary
            layout.m_subpassCount = 2;
            layout.m_attachmentFormats[0] = RHI::Format::ASTC_10x6_UNORM;
            layout.m_attachmentFormats[1] = RHI::Format::ASTC_8x8_UNORM;
            layout.m_attachmentFormats[2] = RHI::Format::R32G32B32_FLOAT;
            layout.m_attachmentFormats[3] = RHI::Format::R16G16_UINT;

            {
                auto& subpassLayout = layout.m_subpassLayouts[0];
                subpassLayout.m_rendertargetCount = 2;
                subpassLayout.m_subpassInputCount = 0;
                subpassLayout.m_depthStencilDescriptor = RHI::RenderAttachmentDescriptor{ 0, RHI::InvalidRenderAttachmentIndex, RHI::AttachmentLoadStoreAction() };
                subpassLayout.m_rendertargetDescriptors[0] = RHI::RenderAttachmentDescriptor{ 1, RHI::InvalidRenderAttachmentIndex, RHI::AttachmentLoadStoreAction() };
                subpassLayout.m_rendertargetDescriptors[1] = RHI::RenderAttachmentDescriptor{ 2, 3, RHI::AttachmentLoadStoreAction(RHI::ClearValue(), RHI::AttachmentLoadAction::DontCare) };
            }

            {
                auto& subpassLayout = layout.m_subpassLayouts[1];
                subpassLayout.m_rendertargetCount = 1;
                subpassLayout.m_subpassInputCount = 2;
                subpassLayout.m_rendertargetDescriptors[0] = RHI::RenderAttachmentDescriptor{ 0, 1, RHI::AttachmentLoadStoreAction() };
                subpassLayout.m_subpassInputDescriptors[0].m_attachmentIndex = 3;
            }
        }

        void InitRenderAttachmentConfiguration(RHI::RenderAttachmentConfiguration& configuration)
        {
            InitRenderAttachmentLayout(configuration.m_renderAttachmentLayout);
            configuration.m_subpassIndex = 1;
        }

    private:
        void SetUp() override
        {
            RHITestFixture::SetUp();
            m_factory.reset(aznew Factory);
        }

        void TearDown() override
        {
            m_factory = nullptr;
            RHITestFixture::TearDown();
        }

        SimpleLcgRandom m_random;
        AZStd::unique_ptr<Factory> m_factory;
    };

    TEST_F(HashingTests, BaselineTest)
    {
        // We expect this to fail, because padding exists in the baseline structure.
        TestHash<BaselineStructWithPadding, Expect::Failure>();
    }

    TEST_F(HashingTests, RenderStatesTest)
    {
        TestHash<RHI::RenderStates>();
    }

    TEST_F(HashingTests, RasterStateHasNoPaddingTest)
    {
        // RasterState is compared and hashed as raw bytes, so every field must be a 4 byte value with nothing between them.
        // Nine fields: fill mode, cull mode, depth bias, depth bias clamp, slope scale, multisample, depth clip, conservative
        // and forced sample count.
        EXPECT_EQ(9 * sizeof(uint32_t), sizeof(RHI::RasterState));
        EXPECT_EQ(0 * sizeof(uint32_t), offsetof(RHI::RasterState, m_fillMode));
        EXPECT_EQ(1 * sizeof(uint32_t), offsetof(RHI::RasterState, m_cullMode));
        EXPECT_EQ(2 * sizeof(uint32_t), offsetof(RHI::RasterState, m_depthBias));
        EXPECT_EQ(3 * sizeof(uint32_t), offsetof(RHI::RasterState, m_depthBiasClamp));
        EXPECT_EQ(4 * sizeof(uint32_t), offsetof(RHI::RasterState, m_depthBiasSlopeScale));
        EXPECT_EQ(5 * sizeof(uint32_t), offsetof(RHI::RasterState, m_multisampleEnable));
        EXPECT_EQ(6 * sizeof(uint32_t), offsetof(RHI::RasterState, m_depthClipEnable));
        EXPECT_EQ(7 * sizeof(uint32_t), offsetof(RHI::RasterState, m_conservativeRasterEnable));
        EXPECT_EQ(8 * sizeof(uint32_t), offsetof(RHI::RasterState, m_forcedSampleCount));
    }

    //////////////////////////////////////////////////////////////////////////
    // Transform 2: the front face winding in bit 31 of RasterState::m_multisampleEnable
    //
    // The winding has no member of its own, so a Transform 1 RasterState must stay byte-identical to an unmodified engine.

#if defined(AZ_COMPILER_MSVC)
    // Prototype pin. 36 and 396 are what sizeof(RasterState) and sizeof(RenderStates) measured on the MSVC x64 build of an unmodified
    // engine, so any growth of the raster state fails here. Other compilers lay RenderStates out differently, so the pin is only
    // checked on MSVC.
    TEST_F(HashingTests, PrototypePin_RasterStateAndRenderStatesKeepTheirSize)
    {
        EXPECT_EQ(36u, sizeof(RHI::RasterState));
        EXPECT_EQ(396u, sizeof(RHI::RenderStates));
    }
#endif

    TEST_F(HashingTests, RasterStateDefaultBytesMatchUnmodifiedEngineTest)
    {
        // Fill mode Solid = 0, cull mode Back = 2, three zero bias values, multisample 0, depth clip 1, conservative 0, forced 0.
        const uint32_t expectedBytes[] = { 0, 2, 0, 0, 0, 0, 1, 0, 0 };
        ASSERT_EQ(sizeof(expectedBytes), sizeof(RHI::RasterState));

        RHI::RasterState raster;
        EXPECT_EQ(0, memcmp(&raster, expectedBytes, sizeof(expectedBytes)));
    }

    TEST_F(HashingTests, RasterStateWritingFalseThroughAccessorKeepsBytesAndHashTest)
    {
        // A Transform 1 draw never writes the winding. Writing false anyway must leave the bytes and the hash of a default state alone.
        RHI::RenderStates untouched;
        RHI::RenderStates writtenFalse;
        writtenFalse.m_rasterState.SetFrontFaceClockwise(false);

        EXPECT_FALSE(untouched.m_rasterState.IsFrontFaceClockwise());
        EXPECT_FALSE(writtenFalse.m_rasterState.IsFrontFaceClockwise());
        EXPECT_EQ(0, memcmp(&untouched.m_rasterState, &writtenFalse.m_rasterState, sizeof(RHI::RasterState)));
        EXPECT_TRUE(untouched == writtenFalse);
        EXPECT_EQ(static_cast<uint64_t>(untouched.GetHash()), static_cast<uint64_t>(writtenFalse.GetHash()));
    }

    TEST_F(HashingTests, RasterStateFrontFaceClockwiseBitChangesHashAndReadsBackTest)
    {
        // The winding is part of what makes a pipeline state different, so it must change the hash and the equality.
        RHI::RenderStates counterClockwise;
        RHI::RenderStates clockwise;
        clockwise.m_rasterState.SetFrontFaceClockwise(true);

        EXPECT_FALSE(counterClockwise.m_rasterState.IsFrontFaceClockwise());
        EXPECT_TRUE(clockwise.m_rasterState.IsFrontFaceClockwise());
        EXPECT_FALSE(counterClockwise == clockwise);
        EXPECT_NE(static_cast<uint64_t>(counterClockwise.GetHash()), static_cast<uint64_t>(clockwise.GetHash()));

        // Setting it back to false restores the original bytes.
        clockwise.m_rasterState.SetFrontFaceClockwise(false);
        EXPECT_FALSE(clockwise.m_rasterState.IsFrontFaceClockwise());
        EXPECT_TRUE(counterClockwise == clockwise);
    }

    TEST_F(HashingTests, RasterStateCarrierFieldReadsItsNormalValueThroughTheMaskTest)
    {
        // A backend that reads m_multisampleEnable raw masks the bit off. For both authored values the masked read is the authored value.
        for (const uint32_t authored : { 0u, 1u })
        {
            RHI::RasterState raster;
            raster.m_multisampleEnable = authored;

            raster.SetFrontFaceClockwise(true);
            EXPECT_TRUE(raster.IsFrontFaceClockwise());
            EXPECT_EQ(authored, raster.m_multisampleEnable & ~RHI::RasterState::FrontFaceClockwiseBit);

            raster.SetFrontFaceClockwise(false);
            EXPECT_FALSE(raster.IsFrontFaceClockwise());
            EXPECT_EQ(authored, raster.m_multisampleEnable);
        }
    }

    TEST_F(HashingTests, RasterStateInvalidMultisampleValueReadsNotMirroredTest)
    {
        // RenderStates_InvalidBool has every bit set, bit 31 included. It must not read as mirrored, and the accessors must not
        // change the marker, because a merge compares the field against it.
        RHI::RasterState invalid = RHI::GetInvalidRasterState();
        EXPECT_EQ(RHI::RenderStates_InvalidBool, invalid.m_multisampleEnable);
        EXPECT_FALSE(invalid.IsFrontFaceClockwise());

        invalid.SetFrontFaceClockwise(true);
        EXPECT_EQ(RHI::RenderStates_InvalidBool, invalid.m_multisampleEnable);
        EXPECT_FALSE(invalid.IsFrontFaceClockwise());

        invalid.SetFrontFaceClockwise(false);
        EXPECT_EQ(RHI::RenderStates_InvalidBool, invalid.m_multisampleEnable);

        // An overlay that went through a stray Set call still carries no winding into the state it is merged into.
        invalid.SetFrontFaceClockwise(true);
        RHI::RasterState raster;
        RHI::MergeStateInto(invalid, raster);
        EXPECT_FALSE(raster.IsFrontFaceClockwise());
        EXPECT_EQ(0u, raster.m_multisampleEnable);
    }

    TEST_F(HashingTests, RasterStateMergeOfOverlayIntoDefaultLeavesBitClearTest)
    {
        RHI::RasterState overlay = RHI::GetInvalidRasterState();
        overlay.m_cullMode = RHI::CullMode::None;

        RHI::RasterState raster;
        RHI::MergeStateInto(overlay, raster);
        EXPECT_EQ(RHI::CullMode::None, raster.m_cullMode);
        EXPECT_FALSE(raster.IsFrontFaceClockwise());

        // An overlay with a valid multisample value replaces the field, and the result still has no winding.
        overlay.m_multisampleEnable = 1;
        RHI::MergeStateInto(overlay, raster);
        EXPECT_EQ(1u, raster.m_multisampleEnable);
        EXPECT_FALSE(raster.IsFrontFaceClockwise());
    }

    TEST_F(HashingTests, RasterStateInvalidOverlayLeavesTheWindingAloneTest)
    {
        // An overlay that does not set a multisample value leaves the field, and so the winding, as it was.
        RHI::RasterState overlay = RHI::GetInvalidRasterState();
        overlay.m_cullMode = RHI::CullMode::None;

        RHI::RasterState mirrored;
        mirrored.SetFrontFaceClockwise(true);
        RHI::MergeStateInto(overlay, mirrored);
        EXPECT_EQ(RHI::CullMode::None, mirrored.m_cullMode);
        EXPECT_TRUE(mirrored.IsFrontFaceClockwise());
    }

    TEST_F(HashingTests, RasterStateWindingSetAfterMergeSurvivesTest)
    {
        // The order the draw packets use: merge the material overlay first, then set the winding on the final descriptor.
        RHI::RasterState overlay = RHI::GetInvalidRasterState();
        overlay.m_multisampleEnable = 1;

        RHI::RasterState raster;
        RHI::MergeStateInto(overlay, raster);
        raster.SetFrontFaceClockwise(true);

        EXPECT_TRUE(raster.IsFrontFaceClockwise());
        EXPECT_EQ(1u, raster.m_multisampleEnable & ~RHI::RasterState::FrontFaceClockwiseBit);
    }

    TEST_F(HashingTests, RasterStateWindingSetBeforeMergeWithMultisampleOverlayIsOverwrittenTest)
    {
        // Documents the order rule. A valid overlay value replaces the whole field, bit included, so a winding set before the
        // merge is lost. This is why the draw packets set it after the merge.
        RHI::RasterState overlay = RHI::GetInvalidRasterState();
        overlay.m_multisampleEnable = 1;

        RHI::RasterState raster;
        raster.SetFrontFaceClockwise(true);
        RHI::MergeStateInto(overlay, raster);

        EXPECT_FALSE(raster.IsFrontFaceClockwise());
        EXPECT_EQ(1u, raster.m_multisampleEnable);
    }

    TEST_F(HashingTests, ImageTest)
    {
        TestHash<RHI::ImageViewDescriptor>();
        TestHash<RHI::ImageDescriptor>();
        TestHash<RHI::ClearValue>();

        RHI::ClearValue clearValue;
        TestHash<RHI::TransientImageDescriptor>([&clearValue](RHI::TransientImageDescriptor& desc)
        {
            desc.m_attachmentId = RHI::AttachmentId{"ABC"};
            desc.m_optimizedClearValue = &clearValue;
        });
    }

    TEST_F(HashingTests, BufferTest)
    {
        TestHash<RHI::BufferViewDescriptor>();
        TestHash<RHI::BufferDescriptor>();
        TestHash<RHI::TransientBufferDescriptor>([](RHI::TransientBufferDescriptor& desc)
        {
            desc.m_attachmentId = RHI::AttachmentId{"EFG"};
        });
    }

    TEST_F(HashingTests, SamplerStateTest)
    {
        TestHash<RHI::SamplerState>();
    }

    TEST_F(HashingTests, ShaderResourceGroupLayoutTest)
    {
        TestHash<RHI::ShaderInputBufferDescriptor>([](RHI::ShaderInputBufferDescriptor& input)
        {
            input.m_name = "InputA";
        });

        TestHash<RHI::ShaderInputImageDescriptor>([](RHI::ShaderInputImageDescriptor& input)
        {
            input.m_name = "InputA";
        });

        TestHash<RHI::ShaderInputSamplerDescriptor>([](RHI::ShaderInputSamplerDescriptor& input)
        {
            input.m_name = "InputA";
        });

        TestHash<RHI::ShaderInputConstantDescriptor>([](RHI::ShaderInputConstantDescriptor& input)
        {
            input.m_name = "InputA";
        });
    }

    TEST_F(HashingTests, StreamLayoutTest)
    {
        TestHash<RHI::ShaderSemantic>([](RHI::ShaderSemantic& semantic)
        {
            semantic.m_name = Name{"COLOR"};
            semantic.m_index = 1;
        });

        TestHash<RHI::StreamChannelDescriptor>([](RHI::StreamChannelDescriptor& layout)
        {
            layout.m_semantic = RHI::ShaderSemantic{Name("UV"), 1};
        });

        TestHash<RHI::StreamBufferDescriptor>();

        TestHash<RHI::InputStreamLayout>([this](RHI::InputStreamLayout& layout)
        {
            InitStreamLayout(layout);
        });
    }

    TEST_F(HashingTests, RenderAttachmentLayoutTest)
    {
        TestHash<RHI::RenderAttachmentLayout>([this](RHI::RenderAttachmentLayout& layout)
        {
            InitRenderAttachmentLayout(layout);
        });
    }

    TEST_F(HashingTests, RenderAttachmentConfigurationTest)
    {
        TestHash<RHI::RenderAttachmentConfiguration>([this](RHI::RenderAttachmentConfiguration& configuration)
        {
            InitRenderAttachmentConfiguration(configuration);
        });
    }

    TEST_F(HashingTests, PipelineStateTest)
    {
        // These are assigned by us, so just pick something arbitrary.
        const uint64_t vertFunctionHash = 0xABCDEF00;
        const uint64_t fragFunctionHash = 0xABCDEF01;

        RHI::Ptr<ShaderStageFunction> vertFunction = aznew ShaderStageFunction(vertFunctionHash, RHI::ShaderStage::Vertex);
        vertFunction->Finalize();

        RHI::Ptr<ShaderStageFunction> fragFunction = aznew ShaderStageFunction(fragFunctionHash, RHI::ShaderStage::Fragment);
        fragFunction->Finalize();

        RHI::InputStreamLayout inputStreamLayout;
        InitStreamLayout(inputStreamLayout);

        RHI::RenderAttachmentConfiguration renderAttachmentConfiguration;
        InitRenderAttachmentConfiguration(renderAttachmentConfiguration);

        RHI::ConstPtr<RHI::ShaderResourceGroupLayout> srgLayout = CreateShaderResourceGroupLayout();
        RHI::ShaderResourceGroupBindingInfo bindingInfo = CreateShaderResourceGroupBindingInfo();

        RHI::Ptr<RHI::PipelineLayoutDescriptor> pipelineLayoutDesc = RHI::PipelineLayoutDescriptor::Create();
        pipelineLayoutDesc->AddShaderResourceGroupLayoutInfo(*srgLayout, bindingInfo);
        pipelineLayoutDesc->Finalize();

        TestHash<RHI::PipelineStateDescriptorForDraw>([&] (RHI::PipelineStateDescriptorForDraw& desc)
        {
            desc.m_vertexFunction = vertFunction;
            desc.m_fragmentFunction = fragFunction;
            desc.m_pipelineLayoutDescriptor = pipelineLayoutDesc;
            desc.m_inputStreamLayout = inputStreamLayout;
            desc.m_renderAttachmentConfiguration = renderAttachmentConfiguration;
        });

        const uint64_t computeFunctionHash = 0xABCDEF02;
        RHI::Ptr<ShaderStageFunction> computeFunction = aznew ShaderStageFunction(computeFunctionHash, RHI::ShaderStage::Compute);

        TestHash<RHI::PipelineStateDescriptorForDispatch>([&](RHI::PipelineStateDescriptorForDispatch& desc)
        {
            desc.m_pipelineLayoutDescriptor = pipelineLayoutDesc;
            desc.m_computeFunction = computeFunction;
        });
    }
}
