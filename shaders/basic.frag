#version 450

layout(set = 2, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 fragColor;

const float COLOR_LEVELS = 8.0;
const float PIXEL_SIZE = 1.0;

const float dither[16] = float[](
     0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
     3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

void main() {
    vec4 texColor = texture(texSampler, inTexCoord);
    vec3 N = normalize(inNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));

    float nDotL = dot(N, L);
    float diffuse = nDotL * 0.5 + 0.5;
    diffuse = diffuse * diffuse;

    float ambient = 0.2;
    float lightIntensity = min(diffuse + ambient, 1.0);

    vec3 finalColor = inColor.rgb * texColor.rgb * lightIntensity;

    vec2 pixelCoord = floor(gl_FragCoord.xy / PIXEL_SIZE);

    int ditherX = int(mod(pixelCoord.x, 4.0));
    int ditherY = int(mod(pixelCoord.y, 4.0));
    float ditherValue = dither[ditherY * 4 + ditherX];

    ditherValue = ditherValue - 0.5;

    finalColor += ditherValue * (1.0 / COLOR_LEVELS);

    finalColor = floor(finalColor * COLOR_LEVELS + 0.5) / COLOR_LEVELS;

    fragColor = vec4(finalColor, inColor.a * texColor.a);
}