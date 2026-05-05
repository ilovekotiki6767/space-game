#version 450
#include "../dither.glsl"

layout (set = 2, binding = 0) uniform sampler2D u_scene;

layout (location = 0) in vec2 input_uv;
layout (location = 0) out vec4 output_color;

void main() {
    vec3 color = texture(u_scene, vec2(input_uv.x, 1.0 - input_uv.y)).rgb;

    float levels = exp2(3.0) - 1.0;

    float dither = (bayer8x8(ivec2(gl_FragCoord.xy)) - 0.5) / levels;
    color = clamp(color + vec3(dither), 0.0, 1.0);

    color = floor(color * levels + 0.5) / levels;

    output_color = vec4(color, 1.0);
}