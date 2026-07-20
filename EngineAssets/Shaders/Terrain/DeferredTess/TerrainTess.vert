#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

// 细分管线的轻量顶点阶段。
// 此阶段不采样高度图，只把局部坐标和实例数据传给 TCS。
// 高度位移统一在 TES 细分后执行。

// 顶点输入（基础 16x16 patch，局部坐标 0..16）。
layout(location = 0) in f16vec3 inPos;
layout(location = 1) in f16vec2 inUV;
layout(location = 2) in f16vec3 inNormal;

// 实例输入。
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags;

// 输出到 TCS，保持每顶点局部数据，不提前做世界空间高度位移。
layout(location = 0) out vec2 vsOutUV;
layout(location = 1) out vec2 vsOutLocalXZ;
layout(location = 2) out vec2 vsOutOffset;
layout(location = 3) out float vsOutScale;
layout(location = 4) out float vsOutLod;
layout(location = 5) out float vsOutStitchFlags;

void main() {
    // w=1 保证 TES 中重心插值按线性局部坐标重建。
    gl_Position = vec4(vec3(inPos), 1.0);

    vsOutUV           = vec2(inUV);
    vsOutLocalXZ      = vec2(inPos.xz);
    vsOutOffset       = instanceOffset;
    vsOutScale        = instanceScale;
    vsOutLod          = instanceLod;
    vsOutStitchFlags  = instanceStitchFlags;
}
