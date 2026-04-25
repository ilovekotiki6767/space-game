#version 450

layout(std140, set = 1, binding = 0) uniform Uniforms {
    mat4 mvp;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    outColor = color;
}