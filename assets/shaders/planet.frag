#version 450

#include "ubo.glsl"

layout (location = 0) in vec3 input_world_normal;
layout (location = 1) in vec4 input_color;
layout (location = 2) in vec2 input_uv;

layout (location = 0) out vec4 output_color;

layout (set = 2, binding = 0) uniform sampler2D u_diffuse;
layout (set = 2, binding = 1) uniform sampler2D u_tex1; // unused
layout (set = 2, binding = 2) uniform sampler2D u_tex2; // unused
layout (set = 2, binding = 3) uniform sampler2D u_tex3; // unused

float bayer4x4(ivec2 p) {
    const float m[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    return m[(p.y & 3) * 4 + (p.x & 3)];
}

void main() {
    vec4 albedo = texture(u_diffuse, input_uv);

    vec3 N = normalize(input_world_normal);
    vec3 L = normalize(u.sun_direction.xyz);

    float n_dot_l = max(dot(N, L), 0.0);

    float threshold = bayer4x4(ivec2(gl_FragCoord.xy)) - 0.5;
    float lit = n_dot_l + threshold * /* the width of the dithered band around n_dot_l = 0 */ 0.25;
    lit = floor(clamp(lit, 0.0, 1.0) + 0.5);

    // ambient = 0.0
    // light = 0.0 + (1.0 - 0.0) * n_dot_l
    // = 1.0 * n_dot_l
    // = n_dot_l
    output_color = vec4(albedo.rgb * input_color.rgb * lit, albedo.a * input_color.a);
}