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
    const float weights[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
    vec2 step = pc.b.zw;
    vec3 sum  = texture(source, inUv).rgb * weights[0];
    for (int i = 1; i < 5; ++i)
    {
        sum += texture(source, inUv + step * float(i)).rgb * weights[i];
        sum += texture(source, inUv - step * float(i)).rgb * weights[i];
    }
    outColor = vec4(sum, 1.0);
}
