#version 450

layout(push_constant) uniform Push
{
    mat4 lightViewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 4) in vec4 inRow0;
layout(location = 5) in vec4 inRow1;
layout(location = 6) in vec4 inRow2;

void main()
{
    vec4 p     = vec4(inPosition, 1.0);
    vec3 world = vec3(dot(inRow0, p), dot(inRow1, p), dot(inRow2, p));
    gl_Position = pc.lightViewProj * vec4(world, 1.0);
}
