#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 texColor = texture(texSampler, inTexCoord);
    fragColor = inColor * texColor;
}