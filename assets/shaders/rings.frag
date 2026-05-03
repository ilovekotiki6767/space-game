#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vObjectPos;

layout(set = 2, binding = 0) uniform sampler2D uRingTexture;

layout(set = 3, binding = 0) uniform FragUBO {
    float uRotationAngle;
};

layout(location = 0) out vec4 FragColor;

void main() {
    vec4 color = texture(uRingTexture, vTexCoord);
    if (color.a < 0.01) discard;

    vec3 sunWorld = normalize(vec3(1.0, 0.15, 0.3));

    float ca = cos(-uRotationAngle);
    float sa = sin(-uRotationAngle);
    vec3 sunObj = vec3(
    sunWorld.x * ca + sunWorld.z * sa,
    sunWorld.y,
    -sunWorld.x * sa + sunWorld.z * ca
    );

    const float Ry = 54364000.0 / 60268000.0;
    vec3 invR = vec3(1.0, 1.0 / Ry, 1.0);

    vec3 P = vObjectPos * invR;
    vec3 D = sunObj    * invR;

    float a   = dot(D, D);
    float b2  = dot(P, D);         // half-b
    float c   = dot(P, P) - 1.0;
    float disc = b2 * b2 - a * c;

    float shadow = 1.0;
    float t_closest = -b2 / a;
    if (t_closest > 0.0) {
        float d_center = sqrt(max(0.0, 1.0 - disc / a));

        float d_surface = d_center - 1.0;

        float sunAngularRadius = 0.03;

        float penumbraSize = t_closest * sunAngularRadius;
        float litFactor = smoothstep(-penumbraSize, penumbraSize, d_surface);

        shadow = mix(0.08, 1.0, litFactor);
    }

    FragColor = vec4(color.rgb * shadow, color.a);
}