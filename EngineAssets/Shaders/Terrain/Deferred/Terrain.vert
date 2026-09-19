#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in uint instanceEdgeFlags;
layout(location = 6) in vec2 instanceMorphRange;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec4 motionCurrentClip;
layout(location = 3) out vec4 motionPreviousClip;
layout(location = 4) out vec2 outHeightDetailGradient;

void main()
{
    vec2 heightUV;
    float worldHeight;
    vec3 worldPos = TerrainBuildWorldPosition(
        vec2(inPos.xz),
        instanceOffset,
        instanceScale,
        instanceEdgeFlags,
        instanceMorphRange,
        heightUV,
        worldHeight);

    gl_Position = VPMatrix * vec4(worldPos, 1.0);
    motionCurrentClip = UnjitteredVPMatrix * vec4(worldPos, 1.0);
    motionPreviousClip = LastUnjitteredVPMatrix * vec4(worldPos, 1.0);
    outUV = heightUV;
    outWorldPos = worldPos;
    outHeightDetailGradient = vec2(0.0);
}
