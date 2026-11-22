#version 450

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 3, binding = 0) uniform sampler2D bloom;
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
    vec4 base  = texture(scene, inUv);
    vec3 color = base.rgb;
    float glow = 0.0;
    if (pc.b.y > 0.5)
    {
        vec3 b = texture(bloom, inUv).rgb * pc.a.y;
        color += b;
        glow = clamp(max(b.r, max(b.g, b.b)), 0.0, 1.0);
    }
    color *= pc.a.z;
    color *= pc.tint.rgb;
    color = (color - 0.5) * pc.a.w + 0.5;
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, pc.b.x);
    outColor = vec4(clamp(color, 0.0, 1.0), clamp(max(base.a, glow), 0.0, 1.0));
}
