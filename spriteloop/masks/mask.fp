#version 140
in mediump vec2 var_texcoord0;
in lowp vec4 var_color;
in highp float var_selected;
in highp float var_mode;
uniform lowp sampler2D texture_sampler;
uniform lowp sampler2D coverage_sampler;
out vec4 out_fragColor;
void main() {
    if (var_selected > 0.25) discard;
    vec4 sample_color = texture(texture_sampler, var_texcoord0);
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
