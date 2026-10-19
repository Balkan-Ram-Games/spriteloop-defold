#version 140

// Fragment shader for SpriteLoop runtime component rendering.
// Samples the part texture and multiplies it by vertex color and material tint.
in mediump vec2 var_texcoord0;
in lowp vec4 var_color;
in lowp float var_texture_is_premultiplied;

uniform lowp sampler2D texture_sampler;

uniform fs_uniforms
{
    mediump vec4 tint;
};

out vec4 out_fragColor;

// Produces the final alpha-blended part pixel.
void main()
{
    vec4 sampled = texture(texture_sampler, var_texcoord0.xy);
    // Native mask compositing produces premultiplied pixels. Convert after
    // filtering, then use the same straight-alpha scene blend as ordinary art.
    if (var_texture_is_premultiplied > 0.5) {
        sampled.rgb = sampled.a > 0.0 ? sampled.rgb / sampled.a : vec3(0.0);
    }
    out_fragColor = sampled * var_color * tint;
}
