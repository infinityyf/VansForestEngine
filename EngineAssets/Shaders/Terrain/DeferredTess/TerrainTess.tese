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

// ── Output to FS — must match Terrain.frag input EXACTLY ──
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outHeight;

// ── Normal map array for micro-displacement (binding 4, same as frag shader) ──
layout(set = 1, binding = 4) uniform sampler2D terrainNormals[8];

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

    // ── Micro displacement: sample normal map Y component ──
    // Use layer 0's normal map at tiled UV for fine surface detail.
    // Tangent-space Y maps to "up" — for mostly flat terrain this translates
    // to world Y displacement, producing ground bumps/indentations.
    float microDisp = 0.0;
    if (tessParams.displacementStrength > 0.0) {
        float layer0Tiling = terrainParams.tilingFactors[0];  // from TerrainCommon.glsl
        vec2 tiledUV = heightUV * layer0Tiling;
        float nmY = texture(terrainNormals[0], tiledUV).g;  // [0,1] tangent-space up
        microDisp = nmY * tessParams.displacementStrength;  // 0..strength in world units
    }
    worldPos.y += microDisp;

    gl_Position = VPMatrix * vec4(worldPos, 1.0);
    outUV       = heightUV;
    outWorldPos = worldPos;
    outHeight   = rawHeight;
}
