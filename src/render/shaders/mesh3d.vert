#version 450

layout(location = 0) in vec4 inClipPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec3 inWorld;
layout(location = 5) in vec2 inParams;
layout(location = 6) in vec4 inMaterial;
layout(location = 7) in vec4 inEmissive;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outWorld;
layout(location = 4) out float outLit;
layout(location = 5) out float outFlags;
layout(location = 6) out vec4 outMaterial;
layout(location = 7) out vec4 outEmissive;

void main()
{
    gl_Position = inClipPos;
    outColor    = inColor;
    outUV       = inUV;
    outNormal   = inNormal;
    outWorld    = inWorld;
    outLit      = inParams.x;
    outFlags    = inParams.y;
    outMaterial = inMaterial;
    outEmissive = inEmissive;
}
