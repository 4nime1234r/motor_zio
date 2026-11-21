#version 450

layout(location = 0) in vec4 inClipPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inLight;
layout(location = 5) in vec4 inShadow;
layout(location = 6) in vec2 inShadowParams;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec4 outLight;
layout(location = 4) out vec4 outShadow;
layout(location = 5) out float outShadowStrength;

void main()
{
    gl_Position       = inClipPos;
    outColor          = inColor;
    outUV             = inUV;
    outNormal         = inNormal;
    outLight          = inLight;
    outShadow         = inShadow;
    outShadowStrength = inShadowParams.x;
}
