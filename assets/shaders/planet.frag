#version 450

#include "ubo.glsl"

layout(location = 0) in vec3 input_normal;
layout(location = 1) in vec4 input_color;
layout(location = 2) in vec2 input_uv;

layout(location = 0) out vec4 output_color;

layout(set = 2, binding = 0) uniform sampler2D u_diffuse;
layout(set = 2, binding = 1) uniform sampler2D u_tex1; // unused
layout(set = 2, binding = 2) uniform sampler2D u_tex2; // unused
layout(set = 2, binding = 3) uniform sampler2D u_tex3; // unused

void main() {
    vec4 albedo = texture(u_diffuse, input_uv);
    output_color = albedo * input_color;
}