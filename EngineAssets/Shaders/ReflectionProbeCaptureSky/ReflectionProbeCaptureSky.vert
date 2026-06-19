#version 450

layout(location = 0) out vec2 ndcPosition;

void main()
{
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    ndcPosition = position * 2.0 - 1.0;
    gl_Position = vec4(ndcPosition, 0.999999, 1.0);
}
