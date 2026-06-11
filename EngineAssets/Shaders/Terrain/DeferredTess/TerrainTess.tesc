#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// Triangular tessellation: 3 control points per input patch
layout(vertices = 3) out;

// ── From VS (per-vertex arrays) ──
layout(location = 0) in vec2 vsOutUV[];
layout(location = 1) in vec2 vsOutLocalXZ[];
layout(location = 2) in vec2 vsOutOffset[];
layout(location = 3) in float vsOutScale[];
layout(location = 4) in float vsOutLod[];
layout(location = 5) in float vsOutStitchFlags[];

// ── To TES (per-vertex) ──
layout(location = 0) out vec2 tcsOutUV[];
layout(location = 1) out vec2 tcsOutLocalXZ[];
layout(location = 2) out vec2 tcsOutOffset[];
layout(location = 3) out float tcsOutScale[];
layout(location = 4) out float tcsOutLod[];
layout(location = 5) out float tcsOutStitchFlags[];

// ── To TES (per-patch — constant across the tessellated patch) ──
layout(location = 6) patch out vec2 tcsPatchOffset;
layout(location = 7) patch out float tcsPatchScale;
layout(location = 8) patch out float tcsPatchStitchFlags;

void main() {
    // Pass-through: gl_out is the built-in TCS per-vertex output interface block
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tcsOutUV[gl_InvocationID]           = vsOutUV[gl_InvocationID];
    tcsOutLocalXZ[gl_InvocationID]      = vsOutLocalXZ[gl_InvocationID];
    tcsOutOffset[gl_InvocationID]       = vsOutOffset[gl_InvocationID];
    tcsOutScale[gl_InvocationID]        = vsOutScale[gl_InvocationID];
    tcsOutLod[gl_InvocationID]          = vsOutLod[gl_InvocationID];
    tcsOutStitchFlags[gl_InvocationID]  = vsOutStitchFlags[gl_InvocationID];

    // Only invocation 0 computes tessellation levels and per-patch outputs
    if (gl_InvocationID == 0) {
        // Instance data is identical across the 3 control points
        tcsPatchOffset       = vsOutOffset[0];
        tcsPatchScale        = vsOutScale[0];
        tcsPatchStitchFlags  = vsOutStitchFlags[0];

        // Compute patch center in world space for distance-based LOD
        vec3 localCenter = (gl_in[0].gl_Position.xyz +
                            gl_in[1].gl_Position.xyz +
                            gl_in[2].gl_Position.xyz) / 3.0;
        vec2 worldXZ   = localCenter.xz * vsOutScale[0] + vsOutOffset[0];
        vec2 heightUV  = TerrainWorldXZToHeightUV(worldXZ);
        float rawHeight = TerrainSampleRawHeight(heightUV);
        vec3 worldCenter = vec3(worldXZ.x, TerrainRawHeightToWorldY(rawHeight), worldXZ.y);

        // Distance-driven tessellation factor (exponential decay)
        float dist = distance(worldCenter, cameraPosition.xyz);
        float t = 1.0 - clamp(dist / tessParams.tessDistance, 0.0, 1.0);
        float tessLevel = max(1.0, tessParams.maxTessLevel * pow(t, tessParams.tessPower));

        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
        gl_TessLevelInner[0] = tessLevel;
    }
}
