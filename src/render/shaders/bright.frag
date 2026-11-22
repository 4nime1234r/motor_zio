#version 450

layout(set = 0, binding = 0) uniform sampler2D source;
layout(push_constant) uniform Push
{
    vec4 a;
    vec4 b;
    vec4 tint;
} pc;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 c       = texture(source, inUv).rgb;
    float peak   = max(c.r, max(c.g, c.b));
    float weight = clamp((peak - pc.a.x) / max(1.0 - pc.a.x, 0.001), 0.0, 1.0);
    outColor     = vec4(c * weight, 1.0);
}
