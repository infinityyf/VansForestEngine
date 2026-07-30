#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// Terrain motion-vector vertex stage. Current and previous clip positions are
// both generated here; the fragment stage converts them to UV-space velocity.
layout(location = 0) in f16vec3 inPos;
layout(location = 1) in f16vec2 inUV;
layout(location = 2) in f16vec3 inNormal;

layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags;

layout(location = 0) out vec4 vCurrentClipPos;
layout(location = 1) out vec4 vPreviousClipPos;

vec3 ApplyTerrainVertexNoise(vec3 worldPos)
{
    if (noiseParams.noiseStrength <= 0.0)
    {
        return worldPos;
    }

    float distToCamera = length(worldPos - cameraPosition.xyz);
    float noiseFade = 1.0 - smoothstep(
        tessParams.tessDistance * noiseParams.fadeStart,
        tessParams.tessDistance,
        distToCamera
    );

    if (noiseFade <= 0.001)
    {
        return worldPos;
    }

    int effectiveOctaves = min(noiseParams.noiseOctaves, 2);
    float noiseDisp = 0.0;
    if (noiseParams.noiseWarpStrength > 0.001)
    {
        noiseDisp = terrainDetailFbmWarped(
            worldPos.xz * noiseParams.noiseFrequency,
            effectiveOctaves,
            noiseParams.noiseGain,
            noiseParams.noiseLacunarity,
            noiseParams.noiseWarpStrength
        );
    }
    else
    {
        noiseDisp = terrainDetailFbm(
            worldPos.xz * noiseParams.noiseFrequency,
            effectiveOctaves,
            noiseParams.noiseGain,
            noiseParams.noiseLacunarity
        );
    }

    worldPos.y += noiseDisp * noiseParams.noiseStrength * noiseFade;
    return worldPos;
}

void main()
{
    vec2 heightUV;
    float height;
    vec3 worldPos = TerrainBuildWorldPosition(inPos.xz, instanceOffset, instanceScale, instanceStitchFlags, heightUV, height);
    worldPos = ApplyTerrainVertexNoise(worldPos);

    // Motion vectors use unjittered matrices; gl_Position remains jittered to match GBuffer rasterization.
    vec4 currentClip = UnjitteredVPMatrix * vec4(worldPos, 1.0);
    vec4 previousClip = LastUnjitteredVPMatrix * vec4(worldPos, 1.0);

    gl_Position = VPMatrix * vec4(worldPos, 1.0);
    vCurrentClipPos = currentClip;
    vPreviousClipPos = previousClip;
}
