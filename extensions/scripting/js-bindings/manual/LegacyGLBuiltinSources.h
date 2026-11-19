/*
 * cocos2d for iPhone: http://www.cocos2d-iphone.org
 *
 * Copyright (c) 2011 Ricardo Quesada
 * Copyright (c) 2012 Zynga Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


// Generated original cocos2d-x/v3 builtin GLSL source constants.
#pragma once
#include <string_view>
namespace axjs::legacygl {
struct BuiltinShaderSource {std::string_view name;bool vertex;std::string_view source;};
inline constexpr BuiltinShaderSource builtinShaderSources[]={
    {"position-texture.vert",true,R"AXBUILTIN0(

attribute vec4 a_position;
attribute vec2 a_texCoord;

#ifdef GL_ES
varying mediump vec2 v_texCoord;
#else
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_texCoord = a_texCoord;
}
)AXBUILTIN0"},
    {"position-texture.frag",false,R"AXBUILTIN1(

#ifdef GL_ES
precision lowp float;
#endif

varying vec2 v_texCoord;

void main()
{
    gl_FragColor =  texture2D(CC_Texture0, v_texCoord);
}
)AXBUILTIN1"},
    {"position-texture-color.vert",true,R"AXBUILTIN2(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
}
)AXBUILTIN2"},
    {"position-texture-color.frag",false,R"AXBUILTIN3(
#ifdef GL_ES
precision lowp float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

void main()
{
    gl_FragColor = v_fragmentColor * texture2D(CC_Texture0, v_texCoord);
}
)AXBUILTIN3"},
    {"position-texture-color-no-mvp.vert",true,R"AXBUILTIN4(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_PMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
}
)AXBUILTIN4"},
    {"position-texture-color-no-mvp.frag",false,R"AXBUILTIN5(

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

void main()
{
    gl_FragColor = v_fragmentColor * texture2D(CC_Texture0, v_texCoord);
}
)AXBUILTIN5"},
    {"position-texture-color-alpha-test.frag",false,R"AXBUILTIN6(

#ifdef GL_ES
precision lowp float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform float CC_alpha_value;

void main()
{
    vec4 texColor = texture2D(CC_Texture0, v_texCoord);

// mimic: glAlphaFunc(GL_GREATER)
// pass if ( incoming_pixel >= CC_alpha_value ) => fail if incoming_pixel < CC_alpha_value

    if ( texColor.a <= CC_alpha_value )
        discard;

    gl_FragColor = texColor * v_fragmentColor;
}
)AXBUILTIN6"},
};
} // namespace axjs::legacygl
