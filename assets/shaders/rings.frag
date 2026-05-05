#version 450

layout (set = 3, binding = 0) uniform fragment_uniforms {
    /// normalized direction from the planet to the origin
    /// `w` is unused
    vec4 sun;
    /// parent planet
    /// `xyz` is the center in the world space and `w` is the radius
    vec4 planet;
} u;

layout (set = 2, binding = 0) uniform sampler2D u_diffuse;

layout (location = 1) in vec4 input_color;
layout (location = 2) in vec2 input_uv;
layout (location = 3) in vec3 input_world_position;

layout (location = 0) out vec4 output_color;

void main() {
    vec4 color = texture(u_diffuse, input_uv);

    if (color.a < 0.1) {
        discard;
    }

    vec3 P = input_world_position;
    vec3 L = normalize(u.sun.xyz);
    vec3 C = u.planet.xyz;
    float R = u.planet.w;

    vec3 oc = P - C;
    float b = dot(oc, L);
    float c = dot(oc, oc) - R * R;
    float disc = b * b - c;

    float lit = 1.0;
    if (disc >= 0.0 && (-b + sqrt(disc)) > 0.0) {
        lit = 0.0;
    }

    output_color = vec4(color.rgb * lit, color.a) * input_color;
}