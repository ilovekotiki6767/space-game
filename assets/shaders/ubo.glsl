layout (set = 3, binding = 0) uniform fragment_uniforms {
    /// normalized direction from the planet to the origin
    /// `w` is unused
    vec4 sun_direction;
    /// `x` is strength, `y` is the scroll speed and everything else is unused
    vec4 overlay;
    /// `xyz` is atmosphere color and `w` is intensity
    vec4 atmosphere;
    /// `x` is specular strength, `y` is shininess
    /// `z` is normalized tint-to-white factor and `w` is unused
    vec4 specular;
} u;