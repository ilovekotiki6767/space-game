#version 450

// surface
layout(set = 2, binding = 0) uniform sampler2D texSampler0;
// clouds
layout(set = 2, binding = 1) uniform sampler2D texSampler1;

layout(set = 3, binding = 0) uniform FragUBO {
    float uTime;
    float _pad0;
    float _pad1;
    float _pad2;
} ubo;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 fragColor;

const float ditherMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
   12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
   15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

void main() {
    vec4 dayColor = texture(texSampler0, inTexCoord);

    float cloudSpeed = 1.0 / 86400.0 * 2.0;
    vec2 cloudUV = vec2(inTexCoord.x + ubo.uTime * cloudSpeed, inTexCoord.y);
    vec4 cloudTex = texture(texSampler1, cloudUV);

    vec3 N = normalize(inNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));
    float nDotL = dot(N, L);

    float lightIntensity = smoothstep(-0.05, 0.15, nDotL);
    float ambient = 0.03;
    float sunLight = clamp(lightIntensity + ambient * lightIntensity, 0.0, 1.0);

    vec3 surface = inColor.rgb * dayColor.rgb;
    surface = pow(surface, vec3(0.95));

    float cloudCoverage = dot(cloudTex.rgb, vec3(0.299, 0.587, 0.114));
    vec3 cloudColor = vec3(1.0);

    vec3 litSurface = mix(surface, cloudColor, cloudCoverage);
    vec3 baseColor  = litSurface * sunLight;

    float colorDepth = 32.0;
    int x = int(mod(gl_FragCoord.x, 4.0));
    int y = int(mod(gl_FragCoord.y, 4.0));
    float ditherValue = ditherMatrix[x + y * 4] - 0.5;

    vec3 ditheredColor = baseColor + (ditherValue / colorDepth);
    vec3 finalColor = floor(ditheredColor * colorDepth + 0.5) / colorDepth;

    fragColor = vec4(finalColor, inColor.a * dayColor.a);
}