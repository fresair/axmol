// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Derived compatibility variant: RG8 luminance/alpha with the legacy dynamic sampler.
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

void main()
{
    vec4 sampleColor = texture2D(CC_Texture0, v_texCoord);
    gl_FragColor = v_fragmentColor * vec4(sampleColor.rrr, sampleColor.g);
}
