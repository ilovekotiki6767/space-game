layout (set = 3, binding = 0) uniform fragment_uniforms {
    /// normalized direction from the planet to the origin
    /// w is unused
    vec4 sun_direction;
} u;