// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include "base.hlsli"

#if defined(USE_POINT_SAMPLER) && USE_POINT_SAMPLER
#define TEXTURE_SAMPLER PointClamp
#else
#define TEXTURE_SAMPLER LinearClamp
#endif

struct PS_IN {
    float4 v_color : COLOR0;
    float2 v_texCoord : TEXCOORD0;
#if defined(AXSLC_TARGET_SPIRV) && AXSLC_TARGET_SPIRV
    float4 v_position : SV_Position;
#endif
};

Texture2D u_tex0;

cbuffer fs_ub {
    float u_alpha_value;
};

#if defined(AXSLC_TARGET_SPIRV) && AXSLC_TARGET_SPIRV
struct PS_OUT {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

PS_OUT main(PS_IN input)
#else
float4 main(PS_IN input) : SV_Target0
#endif
{
    float4 texColor = u_tex0.Sample(TEXTURE_SAMPLER, input.v_texCoord);

    if (texColor.a <= u_alpha_value)
        discard;

#if defined(AXSLC_TARGET_SPIRV) && AXSLC_TARGET_SPIRV
    // Keep alpha discard ahead of Vulkan depth/stencil updates, including devices
    // that otherwise update the stencil of a discarded fragment.
    PS_OUT output;
    output.color = texColor * input.v_color;
    output.depth = input.v_position.z;
    return output;
#else
    return texColor * input.v_color;
#endif
}
