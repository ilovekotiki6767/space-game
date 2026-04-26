#version 450

layout(std140, set = 1, binding = 0) uniform Uniforms {
    mat4 mvp;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    gl_Position.y = -gl_Position.y;

    outColor = color;
    outTexCoord = texCoord;
}