#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// 三角形细分：每个输入 patch 使用 3 个控制点。
layout(vertices = 3) out;

// 来自 VS 的每顶点数据。
layout(location = 0) in vec2 vsOutUV[];
layout(location = 1) in vec2 vsOutLocalXZ[];
layout(location = 2) in vec2 vsOutOffset[];
layout(location = 3) in float vsOutScale[];
layout(location = 4) in float vsOutLod[];
layout(location = 5) in float vsOutStitchFlags[];

// 输出到 TES 的每顶点数据。
layout(location = 0) out vec2 tcsOutUV[];
layout(location = 1) out vec2 tcsOutLocalXZ[];
layout(location = 2) out vec2 tcsOutOffset[];
layout(location = 3) out float tcsOutScale[];
layout(location = 4) out float tcsOutLod[];
layout(location = 5) out float tcsOutStitchFlags[];

// 输出到 TES 的每 patch 常量数据。
layout(location = 6) patch out vec2 tcsPatchOffset;
layout(location = 7) patch out float tcsPatchScale;
layout(location = 8) patch out float tcsPatchStitchFlags;

void main() {
    // 透传内建控制点位置和实例数据。
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tcsOutUV[gl_InvocationID]           = vsOutUV[gl_InvocationID];
    tcsOutLocalXZ[gl_InvocationID]      = vsOutLocalXZ[gl_InvocationID];
    tcsOutOffset[gl_InvocationID]       = vsOutOffset[gl_InvocationID];
    tcsOutScale[gl_InvocationID]        = vsOutScale[gl_InvocationID];
    tcsOutLod[gl_InvocationID]          = vsOutLod[gl_InvocationID];
    tcsOutStitchFlags[gl_InvocationID]  = vsOutStitchFlags[gl_InvocationID];

    // 仅 invocation 0 计算细分等级和每 patch 常量。
    if (gl_InvocationID == 0) {
        // 三个控制点共享同一份实例数据。
        tcsPatchOffset       = vsOutOffset[0];
        tcsPatchScale        = vsOutScale[0];
        tcsPatchStitchFlags  = vsOutStitchFlags[0];

        // 基于 patch 中心到相机的距离计算细分等级。
        vec3 localCenter = (gl_in[0].gl_Position.xyz +
                            gl_in[1].gl_Position.xyz +
                            gl_in[2].gl_Position.xyz) / 3.0;
        vec2 worldXZ   = localCenter.xz * vsOutScale[0] + vsOutOffset[0];
        vec2 heightUV  = TerrainWorldXZToHeightUV(worldXZ);
        float rawHeight = TerrainSampleRawHeight(heightUV);
        vec3 worldCenter = vec3(worldXZ.x, TerrainRawHeightToWorldY(rawHeight), worldXZ.y);

        // 距离驱动的指数衰减细分因子。
        float dist = distance(worldCenter, cameraPosition.xyz);
        float t = 1.0 - clamp(dist / tessParams.tessDistance, 0.0, 1.0);
        float tessLevel = max(1.0, tessParams.maxTessLevel * pow(t, tessParams.tessPower));

        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
        gl_TessLevelInner[0] = tessLevel;
    }
}
