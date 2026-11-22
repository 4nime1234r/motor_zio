#version 450

const int kMaxLights = 9;
const int kMaxTiles  = 13;

layout(set = 0, binding = 0) uniform sampler2D panorama;

layout(std140, set = 2, binding = 0) uniform Scene
{
    vec4 head;
    vec4 camPos;
    mat4 tileMat[kMaxTiles];
    vec4 tileRect[kMaxTiles];
    vec4 tileParam[kMaxTiles];
    vec4 lightPos[kMaxLights];
    vec4 lightColor[kMaxLights];
    vec4 lightDir[kMaxLights];
    vec4 lightMisc[kMaxLights];
    mat4 viewProj;
    vec4 camRight;
    vec4 camUp;
    vec4 camForward;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 skyGround;
    vec4 fogColor;
    vec4 fogParams;
};

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 dir = normalize(camForward.xyz + camRight.xyz * (inNdc.x * camRight.w) + camUp.xyz * (inNdc.y * camUp.w));
    if (fogParams.w > 1.5)
    {
        vec2 uv = vec2(atan(dir.z, dir.x) * 0.15915494 + 0.5, acos(clamp(dir.y, -1.0, 1.0)) * 0.31830989);
        outColor = vec4(texture(panorama, uv).rgb, 1.0);
        return;
    }
    float t = dir.y;
    vec3 color = t >= 0.0 ? mix(skyHorizon.rgb, skyZenith.rgb, pow(t, 0.55)) : mix(skyHorizon.rgb, skyGround.rgb, pow(-t, 0.45));
    outColor = vec4(color, 1.0);
}
