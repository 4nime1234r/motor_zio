#version 450

layout(push_constant) uniform Push
{
    mat4 lightViewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 4) in vec4 inRow0;
layout(location = 5) in vec4 inRow1;
layout(location = 6) in vec4 inRow2;

layout(location = 14) in uvec4 inJoints0;
layout(location = 15) in uvec4 inJoints1;
layout(location = 16) in vec4 inWeights0;
layout(location = 17) in vec4 inWeights1;

layout(std140, set = 3, binding = 4) uniform Bones
{
    mat4 bones[128];
};

mat4 skinMatrix()
{
    mat4 m = bones[inJoints0.x] * inWeights0.x + bones[inJoints0.y] * inWeights0.y +
             bones[inJoints0.z] * inWeights0.z + bones[inJoints0.w] * inWeights0.w +
             bones[inJoints1.x] * inWeights1.x + bones[inJoints1.y] * inWeights1.y +
             bones[inJoints1.z] * inWeights1.z + bones[inJoints1.w] * inWeights1.w;
    float total = dot(inWeights0, vec4(1.0)) + dot(inWeights1, vec4(1.0));
    return total > 0.0 ? m * (1.0 / total) : mat4(1.0);
}

void main()
{
    vec4 p     = skinMatrix() * vec4(inPosition, 1.0);
    vec3 world = vec3(dot(inRow0, p), dot(inRow1, p), dot(inRow2, p));
    gl_Position = pc.lightViewProj * vec4(world, 1.0);
}
