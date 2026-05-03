#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aTexCoord;

layout(set = 1, binding = 0) uniform UBO {
    mat4 uMVP;
};

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vTexCoord = aTexCoord;
    vNormal = aNorm;
}