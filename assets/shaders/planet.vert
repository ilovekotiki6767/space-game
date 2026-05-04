#version 450

layout(location = 0) in vec3 input_position;
layout(location = 1) in vec3 input_normal;
layout(location = 2) in vec4 input_color;
layout(location = 3) in vec2 input_uv;

layout(location = 0) out vec3 output_world_normal;
layout(location = 1) out vec4 output_color;
layout(location = 2) out vec2 output_uv;

layout(set = 1, binding = 0) uniform vertex_uniforms {
    mat4 mvp;
    mat4 model;
} u;

void main() {
    gl_Position = u.mvp * vec4(input_position, 1.0);

    output_world_normal = normalize(mat3(u.model) * input_normal);
    output_color = input_color;
    output_uv = input_uv;
}