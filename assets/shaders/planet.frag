#version 450

#include "ubo.glsl"

layout (location = 0) in vec3 input_world_normal;
layout (location = 1) in vec4 input_color;
layout (location = 2) in vec2 input_uv;
layout (location = 3) in vec3 input_world_position;

layout (location = 0) out vec4 output_color;

layout (set = 2, binding = 0) uniform sampler2D u_diffuse;
layout (set = 2, binding = 1) uniform sampler2D u_overlay;
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

    if (u.overlay.x > 0.0) {
        vec4 overlay = texture(u_overlay, vec2(input_uv.x + u.sun_direction.w * u.overlay.y, input_uv.y));

        float mask = overlay.a > 0.0 ? overlay.a : max(max(overlay.r, overlay.g), overlay.b);
        albedo.rgb = mix(albedo.rgb, overlay.rgb, mask * u.overlay.x);
    }

    vec3 N = normalize(input_world_normal);
    vec3 L = normalize(u.sun_direction.xyz);
    vec3 V = normalize(-input_world_position);

    float n_dot_l = max(dot(N, L), 0.0);
    float n_dot_v = max(dot(N, V), 0.0);

    float threshold = bayer4x4(ivec2(gl_FragCoord.xy)) - 0.5;
    float lit = n_dot_l + threshold * /* the width of the dithered band around n_dot_l = 0 */ 0.25;
    lit = floor(clamp(lit, 0.0, 1.0) + 0.5);

    float limb = pow(n_dot_v, 0.5);

    vec3 surface = albedo.rgb * input_color.rgb * lit * limb;
    vec3 atmosphere = vec3(0.0);

    if (u.atmosphere.w > 0.0) {
        atmosphere = u.atmosphere.rgb * u.atmosphere.w
        * pow(1.0 - n_dot_v, 2.0)
        * lit;
    }

    // ambient = 0.0
    // light = 0.0 + (1.0 - 0.0) * n_dot_l
    // = 1.0 * n_dot_l
    // = n_dot_l
    output_color = vec4(surface + atmosphere, albedo.a * input_color.a);
}