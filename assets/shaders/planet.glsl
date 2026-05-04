#define PI 3.14159265359

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

float GetShadow(float NdotL, float dither) {
    return smoothstep(-0.2, 0.2, NdotL + (dither - 0.5) * 0.5);
}

vec3 Quantize(vec3 color, float dither) {
    color += (dither - 0.5) / 32.0;
    return floor(color * 32.0) / 32.0;
}