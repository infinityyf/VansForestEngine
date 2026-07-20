#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// 顶点输入（基础 16x16 patch，局部坐标 0..16）。
layout(location = 0) in f16vec3 inPos;
layout(location = 1) in f16vec2 inUV;
layout(location = 2) in f16vec3 inNormal;

// 实例输入。
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outHeight;

void main()
{
    vec2 heightUV;
    float height;
    vec3 worldPos = TerrainBuildWorldPosition(inPos.xz, instanceOffset, instanceScale, instanceStitchFlags, heightUV, height);

    // 远场普通 VS 也执行与 TES 同源的低频噪声位移，避免近/远管线高度不一致。
    if (noiseParams.noiseStrength > 0.0)
    {
        float distToCamera = length(worldPos - cameraPosition.xyz);
        float noiseFade = 1.0 - smoothstep(
            tessParams.tessDistance * noiseParams.fadeStart,
            tessParams.tessDistance,
            distToCamera
        );

        if (noiseFade > 0.001)
        {
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
        }
    }

    gl_Position = VPMatrix * vec4(worldPos, 1.0);

    outUV = heightUV;
    outWorldPos = worldPos;
    outHeight = height;
}
