#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vObjectPos;

layout(set = 2, binding = 0) uniform sampler2D uTexture;

layout(set = 3, binding = 0) uniform FragUBO {
    bool uHasAtmosphere;
    float uAtmosphereIntensity;
    float uAtmosphereColorR;
    float uAtmosphereColorG;
    float uAtmosphereColorB;
    float uRotAngle;
    float uAxialTilt;
    float uCamX;
    float uCamY;
    float uCamZ;
    float uScaleX;
    float uScaleY;
    float uScaleZ;
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

vec3 InverseRotZ(vec3 v, float angle) {
    float s = sin(-angle);
    float c = cos(-angle);
    return vec3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

void main() {
    vec3 sunDir = ToObjectSpace(normalize(vec3(0.5, 1.0, 0.3)), uRotAngle);
    vec3 N      = normalize(vNormal);
    float NdotL = dot(N, sunDir);

    vec3 camRel = vec3(uCamX, uCamY, uCamZ);
    vec3 camObj = ToObjectSpace(InverseRotZ(camRel, uAxialTilt), uRotAngle);

    vec3 fragObjPos = vObjectPos * vec3(uScaleX, uScaleY, uScaleZ);
    vec3 V = normalize(camObj - fragObjPos);

    float NdotV = max(dot(N, V), 0.0);

    vec4  texColor = texture(uTexture, vTexCoord);
    float dither   = BayerDither(gl_FragCoord.xy);

    float shadowTransition = smoothstep(-0.2, 0.2, NdotL + (dither - 0.5) * 0.5);

    vec3 litColor = texColor.rgb * 1.2;
    vec3 color    = mix(vec3(0.0), litColor, shadowTransition);

    float limbDarkening = pow(NdotV, 1.0);

    color *= mix(0.2, 1.0, limbDarkening);

    if (uHasAtmosphere) {
        float rim = 1.0 - NdotV;

        rim = pow(rim, 3.0);

        float sunWrap = smoothstep(-0.3, 0.4, dot(N, sunDir));
        vec3 atmosphereColor = vec3(uAtmosphereColorR, uAtmosphereColorG, uAtmosphereColorB);

        vec3 rimGlow = atmosphereColor * rim * uAtmosphereIntensity * sunWrap;

        color += rimGlow;
    }

    color += (dither - 0.5) / 32.0;
    color  = floor(color * 32.0) / 32.0;

    FragColor = vec4(color, 1.0);
}