#version 450

const int kMaxLights = 9;
const int kMaxTiles  = 13;

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
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inVertexColor;

layout(location = 4) in vec4 inRow0;
layout(location = 5) in vec4 inRow1;
layout(location = 6) in vec4 inRow2;
layout(location = 7) in vec4 inNormalRow0;
layout(location = 8) in vec4 inNormalRow1;
layout(location = 9) in vec4 inNormalRow2;
layout(location = 10) in vec4 inColor;
layout(location = 11) in vec4 inMaterial;
layout(location = 12) in vec4 inEmissive;
layout(location = 13) in vec4 inFlagsUv;

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
    vec4 p     = vec4(inPosition, 1.0);
    vec3 world = vec3(dot(inRow0, p), dot(inRow1, p), dot(inRow2, p));
    gl_Position = viewProj * vec4(world, 1.0);

    outColor    = inColor * inVertexColor;
    outUV       = inUV * inFlagsUv.yz;
    outNormal   = normalize(vec3(dot(inNormalRow0.xyz, inNormal), dot(inNormalRow1.xyz, inNormal),
                                 dot(inNormalRow2.xyz, inNormal)));
    outWorld    = world;
    outLit      = 1.0;
    outFlags    = inFlagsUv.x;
    outMaterial = inMaterial;
    outEmissive = inEmissive;
}
