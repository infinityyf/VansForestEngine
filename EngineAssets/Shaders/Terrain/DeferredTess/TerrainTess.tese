#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// Triangular domain, equal (uniform) spacing
// NOTE: winding must be cw to match VK_FRONT_FACE_COUNTER_CLOCKWISE
layout(triangles, equal_spacing, cw) in;

// ── From TCS (per-vertex arrays, sized by layout(vertices=3)) ──
layout(location = 0) in vec2 tcsOutUV[];
layout(location = 1) in vec2 tcsOutLocalXZ[];
layout(location = 2) in vec2 tcsOutOffset[];
layout(location = 3) in float tcsOutScale[];
layout(location = 4) in float tcsOutLod[];
layout(location = 5) in float tcsOutStitchFlags[];

// ── From TCS (per-patch — constant across the tessellated patch) ──
layout(location = 6) patch in vec2 tcsPatchOffset;
layout(location = 7) patch in float tcsPatchScale;
layout(location = 8) patch in float tcsPatchStitchFlags;

// ── Output to FS — must match TerrainTess.frag input EXACTLY ──
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outHeight;
layout(location = 3) out vec2 outNoiseGradient;  // 新增：噪声梯度 (∂noise/∂x, ∂noise/∂z)

void main() {
    // Barycentric interpolation: reconstruct local XZ within the triangle
    vec2 localXZ;
    localXZ.x = gl_TessCoord.x * tcsOutLocalXZ[0].x +
                gl_TessCoord.y * tcsOutLocalXZ[1].x +
                gl_TessCoord.z * tcsOutLocalXZ[2].x;
    localXZ.y = gl_TessCoord.x * tcsOutLocalXZ[0].y +
                gl_TessCoord.y * tcsOutLocalXZ[1].y +
                gl_TessCoord.z * tcsOutLocalXZ[2].y;

    // Macro displacement: heightmap sampling + stitch snapping
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

    // ── 3. 程序化噪声微细节（替代原 normal map Y 位移） ──
    float noiseDisp = 0.0;
    vec2  noiseGrad = vec2(0.0);

    if (noiseParams.noiseStrength > 0.0) {
        vec2 worldXZ = worldPos.xz;

        // 距离衰减：在 tessellation 边界处平滑淡出
        float distToCamera = length(worldPos - cameraPosition.xyz);
        float noiseFade = 1.0 - smoothstep(
            tessParams.tessDistance * noiseParams.fadeStart,
            tessParams.tessDistance,
            distToCamera
        );

        if (noiseFade > 0.001) {
            // 自适应 octave 数：tess level 越高，octave 越多
            int effectiveOctaves = min(
                noiseParams.noiseOctaves,
                1 + int(log2(max(1.0, gl_TessLevelInner[0])))
            );

            // 噪声梯度差分步长（世界单位）
            float gradEps = 0.02;

            if (noiseParams.noiseWarpStrength > 0.001) {
                // ── 域扭曲路径：displacement 和 gradient 使用同一噪声函数 ──
                noiseDisp = terrainDetailFbmWarped(
                    worldXZ * noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity,
                    noiseParams.noiseWarpStrength
                );
                noiseGrad = terrainDetailGradientWarped(
                    worldXZ, noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity,
                    noiseParams.noiseWarpStrength,
                    gradEps
                );
            } else {
                // ── 标准 FBM 路径 ──
                noiseDisp = terrainDetailFbm(
                    worldXZ * noiseParams.noiseFrequency,
                    effectiveOctaves,
                    noiseParams.noiseGain,
                    noiseParams.noiseLacunarity
                );
                noiseGrad = terrainDetailGradient(
                    worldXZ, noiseParams.noiseFrequency,
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

    // ── 4. 输出 ──
    gl_Position = VPMatrix * vec4(worldPos, 1.0);
    outUV            = heightUV;
    outWorldPos      = worldPos;
    outHeight        = rawHeight;
    outNoiseGradient = noiseGrad;
}
