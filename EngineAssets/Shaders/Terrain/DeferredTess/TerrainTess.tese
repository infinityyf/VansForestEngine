#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// 三角形域，使用均匀细分；cw 用于匹配当前 Vulkan 正面设置。
layout(triangles, equal_spacing, cw) in;

// 来自 TCS 的每顶点数组，长度由 layout(vertices=3) 决定。
layout(location = 0) in vec2 tcsOutUV[];
layout(location = 1) in vec2 tcsOutLocalXZ[];
layout(location = 2) in vec2 tcsOutOffset[];
layout(location = 3) in float tcsOutScale[];
layout(location = 4) in float tcsOutLod[];
layout(location = 5) in float tcsOutStitchFlags[];

// 来自 TCS 的每 patch 常量。
layout(location = 6) patch in vec2 tcsPatchOffset;
layout(location = 7) patch in float tcsPatchScale;
layout(location = 8) patch in float tcsPatchStitchFlags;

// 输出到 FS，必须与 TerrainTess.frag 的输入布局一致。
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outHeight;
layout(location = 3) out vec2 outNoiseGradient;

void main()
{
    // 使用重心坐标重建三角形内部的局部 XZ。
    vec2 localXZ;
    localXZ.x = gl_TessCoord.x * tcsOutLocalXZ[0].x +
                gl_TessCoord.y * tcsOutLocalXZ[1].x +
                gl_TessCoord.z * tcsOutLocalXZ[2].x;
    localXZ.y = gl_TessCoord.x * tcsOutLocalXZ[0].y +
                gl_TessCoord.y * tcsOutLocalXZ[1].y +
                gl_TessCoord.z * tcsOutLocalXZ[2].y;

    // 宏观位移：边缘缝合后采样高度图。
    vec2 heightUV;
    float rawHeight;
    vec3 worldPos = TerrainBuildWorldPosition(
        localXZ,
        tcsPatchOffset,
        tcsPatchScale,
        tcsPatchStitchFlags,
        heightUV,
        rawHeight
    );

    // 程序化噪声微细节，替代原 normal map Y 位移。
    float noiseDisp = 0.0;
    vec2 noiseGrad = vec2(0.0);

    if (noiseParams.noiseStrength > 0.0)
    {
        vec2 worldXZ = worldPos.xz;

        // 在 tessellation 有效距离边界处平滑淡出噪声细节。
        float distToCamera = length(worldPos - cameraPosition.xyz);
        float noiseFade = 1.0 - smoothstep(
            tessParams.tessDistance * noiseParams.fadeStart,
            tessParams.tessDistance,
            distToCamera
        );

        if (noiseFade > 0.001)
        {
            // 细分等级越高，可见细节越多，但不超过运行时配置。
            int effectiveOctaves = min(
                noiseParams.noiseOctaves,
                1 + int(log2(max(1.0, gl_TessLevelInner[0])))
            );

            float gradEps = 0.02;

            if (noiseParams.noiseWarpStrength > 0.001)
            {
                noiseDisp = terrainDetailFbmWarped(
                    worldXZ * noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity,
                    noiseParams.noiseWarpStrength
                );
                noiseGrad = terrainDetailGradientWarped(
                    worldXZ,
                    noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity,
                    noiseParams.noiseWarpStrength,
                    gradEps
                );
            }
            else
            {
                noiseDisp = terrainDetailFbm(
                    worldXZ * noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity
                );
                noiseGrad = terrainDetailGradient(
                    worldXZ,
                    noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity,
                    gradEps
                );
            }

            noiseDisp *= noiseParams.noiseStrength * noiseFade;
            worldPos.y += noiseDisp;
            noiseGrad *= noiseParams.noiseStrength * noiseFade;
        }
    }

    gl_Position = VPMatrix * vec4(worldPos, 1.0);
    outUV = heightUV;
    outWorldPos = worldPos;
    outHeight = rawHeight;
    outNoiseGradient = noiseGrad;
}
