#version 450

layout(location = 0) out vec2 outNdc;

void main()
{
    vec2 p      = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    outNdc      = p * 2.0 - 1.0;
    gl_Position = vec4(outNdc, 0.0, 1.0);
}
