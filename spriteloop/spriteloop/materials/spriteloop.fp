#version 140

// Fragment shader for SpriteLoop runtime component rendering.
// Samples the part texture and multiplies it by vertex color and material tint.
in mediump vec2 var_texcoord0;
in lowp vec4 var_color;

uniform lowp sampler2D texture_sampler;

uniform fs_uniforms
{
    mediump vec4 tint;
};

out vec4 out_fragColor;

// Produces the final alpha-blended part pixel.
void main()
{
    out_fragColor = texture(texture_sampler, var_texcoord0.xy) * var_color * tint;
}
