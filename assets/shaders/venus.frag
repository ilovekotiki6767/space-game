#version 450
#extension GL_GOOGLE_include_directive : require
#include "planet.glsl"

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vObjectPos;
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 2, binding = 1) uniform sampler2D uAtmosphere;
layout(set = 3, binding = 0) uniform FragUBO {
    float uRotAngle;
};
layout(location = 0) out vec4 FragColor;

void main() {
    vec3 sunDir = ToObjectSpace(normalize(vec3(0.5, 1.0, 0.3)), uRotAngle);
    vec3 N      = normalize(vNormal);
    float NdotL = dot(N, sunDir);

    float dither = BayerDither(gl_FragCoord.xy);
    float shadowTransition = GetShadow(NdotL, dither);

    vec3 surface = texture(uTexture, vTexCoord).rgb;

    vec2 atmosUV = vTexCoord;
    atmosUV.x = mod(atmosUV.x + uRotAngle * 20.0, 1.0);
    vec4 atmos = texture(uAtmosphere, atmosUV);

    float atmosBlend = dot(atmos.rgb, vec3(0.333));
    vec3 color = mix(surface, atmos.rgb, atmosBlend);

    color *= 1.2;
    color  = mix(vec3(0.0), color, shadowTransition);

    color = Quantize(color, dither);

    FragColor = vec4(color, 1.0);
}