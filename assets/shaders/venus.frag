#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vObjectPos;
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 2, binding = 1) uniform sampler2D uAtmosphere;
layout(set = 3, binding = 0) uniform FragUBO {
    float uRotAngle;
};
layout(location = 0) out vec4 FragColor;

float BayerDither(vec2 p) {
    int x = int(mod(p.x, 4.0));
    int y = int(mod(p.y, 4.0));
    int index = x + y * 4;
    float m[16] = float[16](
    0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
    3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
    );
    return m[index] / 16.0;
}

vec3 ToObjectSpace(vec3 v, float angle) {
    float s = sin(-angle);
    float c = cos(-angle);
    return vec3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}

void main() {
    vec3 sunDir = ToObjectSpace(normalize(vec3(0.5, 1.0, 0.3)), uRotAngle);
    vec3 N      = normalize(vNormal);
    float NdotL = dot(N, sunDir);
    float dither = BayerDither(gl_FragCoord.xy);
    float shadowTransition = smoothstep(-0.2, 0.2, NdotL + (dither - 0.5) * 0.5);

    vec3 surface = texture(uTexture, vTexCoord).rgb;

    vec2 atmosUV = vTexCoord;
    atmosUV.x = mod(atmosUV.x + uRotAngle * 20.0, 1.0);
    vec4 atmos = texture(uAtmosphere, atmosUV);

    float atmosBlend = dot(atmos.rgb, vec3(0.333));
    vec3 color = mix(surface, atmos.rgb, atmosBlend);

    color *= 1.2;
    color  = mix(vec3(0.0), color, shadowTransition);
    color += (dither - 0.5) / 32.0;
    color  = floor(color * 32.0) / 32.0;

    FragColor = vec4(color, 1.0);
}