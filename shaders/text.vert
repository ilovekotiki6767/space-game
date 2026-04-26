#version 450

layout(std140, set = 1, binding = 0) uniform Uniforms {
    mat4 mvp;
};

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;

void main() {
    gl_Position = mvp * vec4(position, 0.0, 1.0);

    outColor = vec4(1.0);
    outTexCoord = texCoord;
}