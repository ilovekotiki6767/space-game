#version 450

#include "ubo.glsl"

layout (location = 0) in vec3 input_world_normal;
layout (location = 1) in vec4 input_color;
layout (location = 2) in vec2 input_uv;
layout (location = 3) in vec3 input_world_position;

layout (location = 0) out vec4 output_color;

layout (set = 2, binding = 0) uniform sampler2D u_diffuse;
layout (set = 2, binding = 1) uniform sampler2D u_overlay;
layout (set = 2, binding = 2) uniform sampler2D u_specular;
layout (set = 2, binding = 3) uniform sampler2D u_tex3; // unused

float bayer8x8(ivec2 p) {
    const float m[64] = float[64](
    0.0 / 64.0, 32.0 / 64.0, 8.0 / 64.0, 40.0 / 64.0, 2.0 / 64.0, 34.0 / 64.0, 10.0 / 64.0, 42.0 / 64.0,
    48.0 / 64.0, 16.0 / 64.0, 56.0 / 64.0, 24.0 / 64.0, 50.0 / 64.0, 18.0 / 64.0, 58.0 / 64.0, 26.0 / 64.0,
    12.0 / 64.0, 44.0 / 64.0, 4.0 / 64.0, 36.0 / 64.0, 14.0 / 64.0, 46.0 / 64.0, 6.0 / 64.0, 38.0 / 64.0,
    60.0 / 64.0, 28.0 / 64.0, 52.0 / 64.0, 20.0 / 64.0, 62.0 / 64.0, 30.0 / 64.0, 54.0 / 64.0, 22.0 / 64.0,
    3.0 / 64.0, 35.0 / 64.0, 11.0 / 64.0, 43.0 / 64.0, 1.0 / 64.0, 33.0 / 64.0, 9.0 / 64.0, 41.0 / 64.0,
    51.0 / 64.0, 19.0 / 64.0, 59.0 / 64.0, 27.0 / 64.0, 49.0 / 64.0, 17.0 / 64.0, 57.0 / 64.0, 25.0 / 64.0,
    15.0 / 64.0, 47.0 / 64.0, 7.0 / 64.0, 39.0 / 64.0, 13.0 / 64.0, 45.0 / 64.0, 5.0 / 64.0, 37.0 / 64.0,
    63.0 / 64.0, 31.0 / 64.0, 55.0 / 64.0, 23.0 / 64.0, 61.0 / 64.0, 29.0 / 64.0, 53.0 / 64.0, 21.0 / 64.0
    );
    return m[(p.y & 7) * 8 + (p.x & 7)];
}

void main() {
    vec4 albedo = texture(u_diffuse, input_uv);

    float coverage = 0.0;

    if (u.overlay.x > 0.0) {
        vec4 overlay = texture(u_overlay,
                               vec2(input_uv.x + u.sun_direction.w * u.overlay.y, input_uv.y));

        float mask = (overlay.a > 0.0)
        ? overlay.a
        : max(max(overlay.r, overlay.g), overlay.b);

        coverage = mask * u.overlay.x;
        albedo.rgb = mix(albedo.rgb, overlay.rgb, coverage);
    }

    vec3 N = normalize(input_world_normal);
    vec3 L = normalize(u.sun_direction.xyz);
    vec3 V = normalize(-input_world_position);
    vec3 H = normalize(L + V);

    float n_dot_l = max(dot(N, L), 0.0);
    float n_dot_v = max(dot(N, V), 0.0);
    float n_dot_h = max(dot(N, H), 0.0);

    float threshold = bayer8x8(ivec2(gl_FragCoord.xy)) - 0.5;
    float lit = n_dot_l + threshold * /* the width of the dithered band around n_dot_l = 0 */ 0.25;
    lit = floor(clamp(lit, 0.0, 1.0) + 0.5);

    float limb = pow(n_dot_v, 0.5);

    vec3 surface = albedo.rgb * input_color.rgb * lit * limb;

    vec3 specular = vec3(0.0);
    if (u.specular.x > 0.0) {
        float mask = texture(u_specular, input_uv).r;
        mask *= (1.0 - coverage);

        float shininess = max(u.specular.y, 1.0);
        float terminator = pow(n_dot_h, shininess) * n_dot_l;

        vec3 color = mix(albedo.rgb, vec3(1.0), clamp(u.specular.z, 0.0, 1.0));

        specular = color * terminator * mask * u.specular.x * lit;
    }

    vec3 atmosphere = vec3(0.0);
    if (u.atmosphere.w > 0.0) {
        atmosphere = u.atmosphere.rgb * u.atmosphere.w
        * pow(1.0 - n_dot_v, 2.0)
        * lit;
    }

    output_color = vec4(surface + specular + atmosphere, albedo.a * input_color.a);
}