#version 140
in mediump vec2 var_texcoord0;
in lowp vec4 var_color;
in highp float var_selected;
in highp float var_mode;
uniform lowp sampler2D texture_sampler;
uniform lowp sampler2D coverage_sampler;
uniform fs_uniforms {
    highp vec4 texture_size;
};
out vec4 out_fragColor;

const float ARTWORK_LOD_BIAS = -0.5;
const float MAX_ARTWORK_MIP_LEVEL = 1.0;
void main() {
    if (var_selected > 0.25) discard;
    // Coarse atlas levels mix unrelated images. Bound the footprint to the
    // first mip level; strong minification trades smoothing for image isolation.
    vec2 dx = dFdx(var_texcoord0);
    vec2 dy = dFdy(var_texcoord0);
    vec2 dimensions = texture_size.xy;
    float footprint = max(length(dx * dimensions), length(dy * dimensions));
    float gradient_scale = min(exp2(ARTWORK_LOD_BIAS),
        exp2(MAX_ARTWORK_MIP_LEVEL) / max(footprint, 0.000001));
    vec4 sample_color = textureGrad(texture_sampler, var_texcoord0,
        dx * gradient_scale, dy * gradient_scale);
    if (var_mode > 1.5) {
        out_fragColor = sample_color * texture(coverage_sampler, var_texcoord0).a;
    } else if (var_mode > 0.5) {
        float a = sample_color.a * var_color.a;
        out_fragColor = vec4(a);
    } else {
        float a = sample_color.a * var_color.a;
        out_fragColor = vec4(sample_color.rgb * var_color.rgb * var_color.a, a);
    }
}
