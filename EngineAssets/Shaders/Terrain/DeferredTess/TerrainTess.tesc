#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

layout(vertices = 3) out;

layout(location = 0) in vec2 vsOutLocalXZ[];
layout(location = 1) in vec2 vsOutOffset[];
layout(location = 2) in float vsOutScale[];
layout(location = 3) flat in uint vsOutEdgeFlags[];
layout(location = 4) in vec2 vsOutMorphRange[];

layout(location = 0) out vec2 tcsOutLocalXZ[];
layout(location = 1) patch out vec2 tcsPatchOffset;
layout(location = 2) patch out float tcsPatchScale;
layout(location = 3) patch out uint tcsPatchEdgeFlags;
layout(location = 4) patch out vec2 tcsPatchMorphRange;

uint TerrainControlEdge(vec2 a, vec2 b)
{
    const float epsilon = 0.001;
    float patchSize = TerrainPatchGridResolution();
    if (abs(a.x) <= epsilon && abs(b.x) <= epsilon) return TerrainEdgeLeft;
    if (abs(a.x - patchSize) <= epsilon && abs(b.x - patchSize) <= epsilon) return TerrainEdgeRight;
    if (abs(a.y) <= epsilon && abs(b.y) <= epsilon) return TerrainEdgeTop;
    if (abs(a.y - patchSize) <= epsilon && abs(b.y - patchSize) <= epsilon) return TerrainEdgeBottom;
    return 0u;
}

vec3 TerrainControlWorldPosition(vec2 localPosition)
{
    vec2 heightUV;
    float worldHeight;
    return TerrainBuildWorldPosition(
        localPosition,
        vsOutOffset[0],
        vsOutScale[0],
        vsOutEdgeFlags[0],
        vsOutMorphRange[0],
        heightUV,
        worldHeight);
}

float TerrainEdgeTessLevel(vec2 localA, vec2 localB)
{
    uint patchEdge = TerrainControlEdge(localA, localB);
    uint disabledEdges = (vsOutEdgeFlags[0] >> 8u) & TerrainEdgeMask;
    if ((patchEdge & disabledEdges) != 0u)
        return 1.0;

    vec3 worldA = TerrainControlWorldPosition(localA);
    vec3 worldB = TerrainControlWorldPosition(localB);
    vec3 midpoint = (worldA + worldB) * 0.5;
    float distanceFade = 1.0 - smoothstep(
        tessParams.tessDistance * 0.8,
        tessParams.tessDistance,
        distance(midpoint, cameraPosition.xyz));
    if (distanceFade <= 0.0)
        return 1.0;

    vec4 clipA = VPMatrix * vec4(worldA, 1.0);
    vec4 clipB = VPMatrix * vec4(worldB, 1.0);
    if (clipA.w <= 0.0 || clipB.w <= 0.0)
        return 1.0;

    vec2 ndcA = clipA.xy / clipA.w;
    vec2 ndcB = clipB.xy / clipB.w;
    float edgePixels = length((ndcA - ndcB) * 0.5 * ScreenParams.xy);
    float desiredLevel = ceil(edgePixels * distanceFade / max(tessParams.targetEdgePixels, 1.0));
    return clamp(desiredLevel, 1.0, tessParams.maxTessLevel);
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tcsOutLocalXZ[gl_InvocationID] = vsOutLocalXZ[gl_InvocationID];

    if (gl_InvocationID == 0)
    {
        tcsPatchOffset = vsOutOffset[0];
        tcsPatchScale = vsOutScale[0];
        tcsPatchEdgeFlags = vsOutEdgeFlags[0];
        tcsPatchMorphRange = vsOutMorphRange[0];

        gl_TessLevelOuter[0] = TerrainEdgeTessLevel(vsOutLocalXZ[1], vsOutLocalXZ[2]);
        gl_TessLevelOuter[1] = TerrainEdgeTessLevel(vsOutLocalXZ[2], vsOutLocalXZ[0]);
        gl_TessLevelOuter[2] = TerrainEdgeTessLevel(vsOutLocalXZ[0], vsOutLocalXZ[1]);
        gl_TessLevelInner[0] = max(
            gl_TessLevelOuter[0],
            max(gl_TessLevelOuter[1], gl_TessLevelOuter[2]));
    }
}
