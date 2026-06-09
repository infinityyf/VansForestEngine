#version 450

// ============================================================
// water_composite.vert — 水面 Composite Pass 顶点着色器
//
// 全屏单三角形（无顶点缓冲）。
// gl_VertexIndex = 0, 1, 2 → NDC 覆盖 [-1, 1]²
// ============================================================

layout(location = 0) out vec2 outUV;

void main()
{
    // 一个大三角形覆盖全屏
    // 顶点索引 0: (-1, -1), 1: ( 3, -1), 2: (-1,  3)
    vec2 pos  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUV     = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
