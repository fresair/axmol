

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
