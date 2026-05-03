#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;

layout(set = 2, binding = 0) uniform sampler2D uEarthTexture;
layout(set = 2, binding = 1) uniform sampler2D uCloudTexture;
layout(set = 2, binding = 2) uniform sampler2D uSpecularMap;
layout(set = 2, binding = 3) uniform sampler2D uNormalMap;

layout(set = 3, binding = 0) uniform FragUBO {
    float uTime;
    float uCamDirX;
    float uCamDirY;
    float uCamDirZ;
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
    vec3 sunDirWorld = normalize(vec3(0.5, 1.0, 0.3));
    vec3 camDirWorld = normalize(vec3(uCamDirX, uCamDirY, uCamDirZ));
    vec3 sunDir = ToObjectSpace(sunDirWorld, uRotAngle);
    vec3 camDir = ToObjectSpace(camDirWorld, uRotAngle);

    vec3 N_geom = normalize(vNormal);

    vec3 T   = normalize(cross(vec3(0.0, 1.0, 0.0), N_geom));
    vec3 B   = cross(N_geom, T);
    mat3 TBN = mat3(T, B, N_geom);
    vec3 normalSample = texture(uNormalMap, vTexCoord).rgb * 2.0 - 1.0;
    normalSample = vec3(normalSample.xy * 0.15, normalSample.z);
    vec3 N = normalize(TBN * normalSample);

    float NdotL = dot(N_geom, sunDir);

    vec4 earthColor = texture(uEarthTexture, vTexCoord);

    float cloudUOffset = uTime * (0.1 / 86400.0);
    vec2  cloudUV      = vec2(vTexCoord.x + cloudUOffset, vTexCoord.y);
    vec4  cloudSample  = texture(uCloudTexture, cloudUV);
    float cloudAlpha   = dot(cloudSample.rgb, vec3(0.2126, 0.7152, 0.0722));

    vec2  shadowOffset = vec2(-sunDir.x, sunDir.z) * 0.018;
    float shadowCloud  = dot(texture(uCloudTexture, cloudUV + shadowOffset).rgb,
                             vec3(0.2126, 0.7152, 0.0722));
    float shadowMask   = shadowCloud * (1.0 - cloudAlpha) * max(NdotL, 0.0);
    vec3  earthShaded  = earthColor.rgb * (1.0 - shadowMask * 0.55);

    vec3 blended = mix(earthShaded, vec3(1.0), cloudAlpha * 0.85);

    float dither = BayerDither(gl_FragCoord.xy);

    float waterMask = texture(uSpecularMap, vTexCoord).r;
    vec3  halfVec   = normalize(sunDir + camDir);
    float spec      = max(dot(N, halfVec), 0.0);
    float specThreshold = 0.94 + dither * 0.04;
    float specular  = step(specThreshold, spec) * waterMask * max(NdotL, 0.0);
    specular       *= (1.0 - cloudAlpha);
    blended        += vec3(1.0, 0.97, 0.9) * specular * 0.25;

    float shadowTransition = smoothstep(-0.2, 0.2, NdotL + (dither - 0.5) * 0.5);

    vec3 litColor = blended * 1.2;
    vec3 color    = mix(vec3(0.0), litColor, shadowTransition);

    float rim    = pow(1.0 - max(dot(N_geom, camDir), 0.0), 3.0);
    float rimLit = smoothstep(-0.3, 0.3, NdotL);
    color       += vec3(0.15, 0.4, 1.0) * rim * rimLit * 0.35;

    color += (dither - 0.5) / 32.0;
    color  = floor(color * 32.0) / 32.0;

    FragColor = vec4(color, 1.0);
}