#version 450

layout(set = 2, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 texColor = texture(texSampler, inTexCoord);
    vec3 N = normalize(inNormal);

    vec3 L = normalize(vec3(0.5, 1.0, 0.3));

    float nDotL = dot(N, L);

    float diffuse = nDotL * 0.5 + 0.5;

    diffuse = diffuse * diffuse;

    float ambient = 0.2;
    float lightIntensity = min(diffuse + ambient, 1.0);

    fragColor = vec4(inColor.rgb * texColor.rgb * lightIntensity, inColor.a * texColor.a);
}