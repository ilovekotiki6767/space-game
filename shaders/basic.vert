#version 450

layout(std140, set = 1, binding = 0) uniform Uniforms {
    mat4 mvp;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;
layout(location = 3) in vec2 texCoord;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outNormal;

const float VERTEX_SNAP_RES = 160.0;

void main() {
    vec4 clipPos = mvp * vec4(position, 1.0);

    vec3 ndc = clipPos.xyz / clipPos.w;

    ndc.xy = floor(ndc.xy * VERTEX_SNAP_RES) / VERTEX_SNAP_RES;

    clipPos.xyz = ndc * clipPos.w;

    gl_Position = clipPos;
    gl_Position.y = -gl_Position.y;

    outColor = color;
    outTexCoord = texCoord;
    outNormal = normal;
}