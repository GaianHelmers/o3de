/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI.Reflect/Limits.h>
#include <Atom/RHI.Reflect/Format.h>
#include <Atom/RHI.Reflect/SamplerState.h>
#include <Atom/RHI.Reflect/MultisampleState.h>
#include <AzCore/Preprocessor/Enum.h>

namespace AZ
{
    class ReflectContext;
}

namespace AZ::RHI
{
    AZ_ENUM_CLASS_WITH_UNDERLYING_TYPE(CullMode, uint32_t,
        None,
        Front,
        Back,
        Invalid
    );

    AZ_ENUM_CLASS_WITH_UNDERLYING_TYPE(FillMode, uint32_t,
        Solid,
        Wireframe,
        Invalid
    );

    AZ_ENUM_CLASS_WITH_UNDERLYING_TYPE(DepthWriteMask, uint32_t,
        Zero,
        All,
        Invalid
    );

    AZ_ENUM_CLASS_WITH_UNDERLYING_TYPE(StencilOp, uint32_t,
        Keep,
        Zero,
        Replace,
        IncrementSaturate,
        DecrementSaturate,
        Invert,
        Increment,
        Decrement,
        Invalid
    );

    AZ_ENUM_CLASS_WITH_UNDERLYING_TYPE(BlendFactor, uint32_t,
        Zero,
        One,
        ColorSource,
        ColorSourceInverse,
        AlphaSource,
        AlphaSourceInverse,
        AlphaDest,
        AlphaDestInverse,
        ColorDest,
        ColorDestInverse,
        AlphaSourceSaturate,
        Factor,
        FactorInverse,
        ColorSource1,
        ColorSource1Inverse,
        AlphaSource1,
        AlphaSource1Inverse,
        Invalid
    );

    AZ_ENUM_CLASS_WITH_UNDERLYING_TYPE(BlendOp, uint32_t,
        Add,               //!< Result = Source + Destination
        Subtract,          //!< Result = Source - Destination
        SubtractReverse,   //!< Result = Destination - Source
        Minimum,           //!< Result = MIN(Source, Destination)
        Maximum,           //!< Result = MAX(Source, Destination)
        Invalid
    );

    ATOM_RHI_REFLECT_API void ReflectRenderStateEnums(ReflectContext* context);

    struct ATOM_RHI_REFLECT_API RasterState
    {
        AZ_TYPE_INFO(RasterState, "{57D4BE50-EBE2-4ABE-90A4-C99BF2EA43FB}");
        static void Reflect(ReflectContext* context);

        bool operator == (const RasterState& rhs) const;

        FillMode m_fillMode = FillMode::Solid;
        CullMode m_cullMode = CullMode::Back;
        int32_t m_depthBias = 0;
        float m_depthBiasClamp = 0.0f;
        float m_depthBiasSlopeScale = 0.0f;
        // Transform 2: bit 31 of this field carries the front face winding. Read the rules at the end of this struct.
        uint32_t m_multisampleEnable = 0;
        uint32_t m_depthClipEnable = 1;
        uint32_t m_conservativeRasterEnable = 0;
        uint32_t m_forcedSampleCount = 0;

        //////////////////////////////////////////////////////////////////////////
        // Transform 2: front face winding in bit 31 of m_multisampleEnable
        //
        // A mirrored object winds its triangles the other way on screen, so its draws need clockwise triangles as the front faces.
        // RasterState is compared and hashed as raw bytes and stays nine 4 byte fields (36 bytes), so a Transform 1 draw has the
        // exact bytes and hash of an unmodified engine. The winding therefore has no member of its own. It rides in bit 31 of
        // m_multisampleEnable, a flag whose valid values are 0 and 1.
        //
        // Rules:
        //  - Read the winding only with IsFrontFaceClockwise and write it only with SetFrontFaceClockwise.
        //  - Code that reads m_multisampleEnable raw must mask FrontFaceClockwiseBit off first (DX12 ConvertRasterState does).
        //    A new reader in another backend needs the same mask.
        //  - Set the winding on the final descriptor, after MergeStateInto. A merge copies a valid overlay value over the whole
        //    field, bit included, so a bit set before the merge can be lost. Never set it on a merge overlay, where the bit would
        //    make the field look like a valid multisample value and overwrite the real one.
        //  - A mirrored draw is the only code that sets the bit. A Transform 1 draw never writes this field.
        //  - RenderStates_InvalidBool has every bit set, so the accessors treat it as "not mirrored" and leave it untouched.
        static constexpr uint32_t FrontFaceClockwiseBit = 0x80000000u;

        // Defined inline further down in this header, after RenderStates_InvalidBool.
        bool IsFrontFaceClockwise() const;
        void SetFrontFaceClockwise(bool frontFaceClockwise);
    };

    struct ATOM_RHI_REFLECT_API DepthState
    {
        AZ_TYPE_INFO(DepthState, "{5F321456-052F-41F1-BD35-2D34CB26DD9D}");
        static void Reflect(ReflectContext* context);

        bool operator == (const DepthState& rhs) const;

        uint32_t m_enable = 1;
        DepthWriteMask m_writeMask = DepthWriteMask::All;
        ComparisonFunc m_func = ComparisonFunc::Less;
    };

    struct ATOM_RHI_REFLECT_API StencilOpState
    {
        AZ_TYPE_INFO(StencilOpState, "{6B0894AA-7FE9-4EB0-8171-FF0872CB9B7F}");
        static void Reflect(ReflectContext* context);

        bool operator == (const StencilOpState& rhs) const;

        StencilOp m_failOp = StencilOp::Keep;
        StencilOp m_depthFailOp = StencilOp::Keep;
        StencilOp m_passOp = StencilOp::Keep;
        ComparisonFunc m_func = ComparisonFunc::Always;
    };

    struct ATOM_RHI_REFLECT_API StencilState
    {
        AZ_TYPE_INFO(StencilState, "{098EAE83-A3F3-4270-B7AC-ACD11366BBB9}");
        static void Reflect(ReflectContext* context);

        bool operator == (const StencilState& rhs) const;

        uint32_t m_enable = 0;
        uint32_t m_readMask = 0xFF;
        uint32_t m_writeMask = 0xFF;
        StencilOpState m_frontFace;
        StencilOpState m_backFace;
    };

    struct ATOM_RHI_REFLECT_API DepthStencilState
    {
        AZ_TYPE_INFO(DepthStencilState, "{8AB45110-0727-4923-8098-B9926C1789FE}");
        static void Reflect(ReflectContext* context);

        bool operator == (const DepthStencilState& rhs) const;

        static DepthStencilState CreateDepth();
        static DepthStencilState CreateReverseDepth();
        static DepthStencilState CreateDisabled();

        DepthState m_depth;
        StencilState m_stencil;
    };

    enum class WriteChannelMask : uint8_t
    {
        ColorWriteMaskNone = 0,
        ColorWriteMaskRed = AZ_BIT(0),
        ColorWriteMaskGreen = AZ_BIT(1),
        ColorWriteMaskBlue = AZ_BIT(2),
        ColorWriteMaskAlpha = AZ_BIT(3),
        ColorWriteMaskAll = ColorWriteMaskRed | ColorWriteMaskGreen | ColorWriteMaskBlue | ColorWriteMaskAlpha
    };
    
    struct ATOM_RHI_REFLECT_API TargetBlendState
    {
        AZ_TYPE_INFO(TargetBlendState, "{2CDF00FE-614D-44FC-929F-E6B50C348578}");
        static void Reflect(ReflectContext* context);

        bool operator == (const TargetBlendState& rhs) const;

        uint32_t m_enable = 0;
        uint32_t m_writeMask = 0xF;
        BlendFactor m_blendSource = BlendFactor::One;
        BlendFactor m_blendDest = BlendFactor::Zero;
        BlendOp m_blendOp = BlendOp::Add;
        BlendFactor m_blendAlphaSource = BlendFactor::One;
        BlendFactor m_blendAlphaDest = BlendFactor::Zero;
        BlendOp m_blendAlphaOp = BlendOp::Add;
    };

    struct ATOM_RHI_REFLECT_API BlendState
    {
        AZ_TYPE_INFO(BlendState, "{EDB2333A-EF10-4A98-A157-B204E90FA179}");
        static void Reflect(ReflectContext* context);

        bool operator == (const BlendState& rhs) const;

        uint32_t m_alphaToCoverageEnable = 0;
        uint32_t m_independentBlendEnable = 0;
        AZStd::array<TargetBlendState, Limits::Pipeline::AttachmentColorCountMax> m_targets;
    };

    struct ATOM_RHI_REFLECT_API RenderStates
    {
        AZ_TYPE_INFO(RenderStates, "{521D72D5-DD69-4380-B637-9CC3D8479D2B}");
        static void Reflect(ReflectContext* context);

        HashValue64 GetHash(HashValue64 seed = HashValue64{ 0 }) const;

        bool operator == (const RenderStates& rhs) const;

        MultisampleState m_multisampleState;
        RasterState m_rasterState;
        BlendState m_blendState;
        DepthStencilState m_depthStencilState;
    };

    static constexpr uint32_t RenderStates_InvalidBool = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t RenderStates_InvalidUInt16 = std::numeric_limits<uint16_t>::max();
    static constexpr uint32_t RenderStates_InvalidUInt = std::numeric_limits<uint32_t>::max();
    static constexpr int32_t RenderStates_InvalidInt = std::numeric_limits<int32_t>::max();
    static constexpr float RenderStates_InvalidFloat = std::numeric_limits<float>::max();

    //////////////////////////////////////////////////////////////////////////
    // Transform 2: front face winding accessors
    //
    // These two functions are the only code that reads or writes the bit. The rules are in RasterState above. They are defined
    // here, after RenderStates_InvalidBool, and inline, so a backend folds the check into its own code and a Transform 1 pipeline
    // state creation makes no call into this library for it. Viewport.h defines its members the same way.

    inline bool RasterState::IsFrontFaceClockwise() const
    {
        // RenderStates_InvalidBool has every bit set, so without the comparison an invalid field would read as mirrored.
        return m_multisampleEnable != RenderStates_InvalidBool && (m_multisampleEnable & FrontFaceClockwiseBit) != 0;
    }

    inline void RasterState::SetFrontFaceClockwise(bool frontFaceClockwise)
    {
        // The invalid value is a marker for "not set", not a multisample value. Clearing the bit would corrupt the marker, so an
        // invalid field is left alone. This is also what makes a stray call on a merge overlay harmless.
        if (m_multisampleEnable == RenderStates_InvalidBool)
        {
            return;
        }

        if (frontFaceClockwise)
        {
            m_multisampleEnable |= FrontFaceClockwiseBit;
        }
        else
        {
            m_multisampleEnable &= ~FrontFaceClockwiseBit;
        }
    }

    //! Merges any render states in stateToMerge into the result state object. 
    //! The values in stateToMerge are only copied over into the result if they are
    //! not invalid (see also GetInvalidState below).
    ATOM_RHI_REFLECT_API void MergeStateInto(const DepthState& stateToMerge, DepthState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const RasterState& stateToMerge, RasterState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const StencilOpState& stateToMerge, StencilOpState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const StencilState& stateToMerge, StencilState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const DepthStencilState& stateToMerge, DepthStencilState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const TargetBlendState& stateToMerge, TargetBlendState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const BlendState& stateToMerge, BlendState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const MultisampleState& stateToMerge, MultisampleState& result);
    ATOM_RHI_REFLECT_API void MergeStateInto(const RenderStates& statesToMerge, RenderStates& result);

    //! Mark each property in the render states to an invalid value.
    //! This set of functions can be typically used by MergeStateInto
    //! to filter out the actual properties that need to be overwritten.
    ATOM_RHI_REFLECT_API const RasterState& GetInvalidRasterState();
    ATOM_RHI_REFLECT_API const DepthState& GetInvalidDepthState();
    ATOM_RHI_REFLECT_API const StencilOpState& GetInvalidStencilOpState();
    ATOM_RHI_REFLECT_API const StencilState& GetInvalidStencilState();
    ATOM_RHI_REFLECT_API const DepthStencilState& GetInvalidDepthStencilState();
    ATOM_RHI_REFLECT_API const TargetBlendState& GetInvalidTargetBlendState();
    ATOM_RHI_REFLECT_API const BlendState& GetInvalidBlendState();
    ATOM_RHI_REFLECT_API const MultisampleState& GetInvalidMultisampleState();
    ATOM_RHI_REFLECT_API const RenderStates& GetInvalidRenderStates();

    AZ_TYPE_INFO_SPECIALIZE(CullMode, "{AABEEE39-9185-4A9C-9BD7-229DAAAE885D}");
    AZ_TYPE_INFO_SPECIALIZE(FillMode, "{A164B54D-0A74-4F7C-89F3-032D6B6BF107}");
    AZ_TYPE_INFO_SPECIALIZE(DepthWriteMask, "{11B00B11-AC7E-4F8C-B2D9-5A09BB4D92B5}");
    AZ_TYPE_INFO_SPECIALIZE(StencilOp, "{FADAFC88-8638-4104-A73D-CA5CF4C16F74}");
    AZ_TYPE_INFO_SPECIALIZE(BlendFactor, "{BD14C7A1-3DC9-4670-8A13-2017B8CEECB6}");
    AZ_TYPE_INFO_SPECIALIZE(BlendOp, "{23DD9B83-875F-43D1-B1BB-5655C6A59739}");
}
