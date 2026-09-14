#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

layout(triangles, equal_spacing, cw) in;

layout(location = 0) in vec2 tcsOutLocalXZ[];
layout(location = 1) patch in vec2 tcsPatchOffset;
layout(location = 2) patch in float tcsPatchScale;
layout(location = 3) patch in uint tcsPatchEdgeFlags;
layout(location = 4) patch in vec2 tcsPatchMorphRange;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec4 motionCurrentClip;
layout(location = 3) out vec4 motionPreviousClip;
layout(location = 4) out vec2 outNoiseGradient;

void main()
{
    vec2 localXZ =
        gl_TessCoord.x * tcsOutLocalXZ[0] +
        gl_TessCoord.y * tcsOutLocalXZ[1] +
        gl_TessCoord.z * tcsOutLocalXZ[2];

    vec2 heightUV;
    float worldHeight;
    vec3 worldPos = TerrainBuildWorldPosition(
        localXZ,
        tcsPatchOffset,
        tcsPatchScale,
        tcsPatchEdgeFlags,
        tcsPatchMorphRange,
        heightUV,
        worldHeight);

    gl_Position = VPMatrix * vec4(worldPos, 1.0);
    motionCurrentClip = UnjitteredVPMatrix * vec4(worldPos, 1.0);
    motionPreviousClip = LastUnjitteredVPMatrix * vec4(worldPos, 1.0);
    outUV = heightUV;
    outWorldPos = worldPos;
    outNoiseGradient = TerrainDetailedNoiseGradient(worldPos);
}
