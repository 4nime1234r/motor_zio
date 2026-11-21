#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inLight;
layout(location = 4) in vec4 inShadow;
layout(location = 5) in float inShadowStrength;

layout(location = 0) out vec4 outColor;

const float kDepthBias = 0.0004;

float shadowVisibility()
{
    if (inShadow.w <= 0.0)
        return 1.0;

    vec3 p  = inShadow.xyz / inShadow.w;
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0)
        return 1.0;

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit  = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            float stored = texture(shadowMap, uv + vec2(float(x), float(y)) * texel).r;
            lit += (p.z - kDepthBias <= stored) ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

void main()
{
    float diffuse    = clamp(dot(normalize(inNormal), inLight.xyz), 0.0, 1.0);
    float visibility = mix(1.0, shadowVisibility(), inShadowStrength);
    float k          = inLight.w + (1.0 - inLight.w) * diffuse * visibility;
    outColor         = texture(tex, inUV) * vec4(inColor.rgb * k, inColor.a);
}
