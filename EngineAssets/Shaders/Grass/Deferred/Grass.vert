#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"

// ── Vertex attributes ──────────────────────────────────────────────────────
layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec2 inUV;
layout( location = 2 ) in vec3 inNormal;

// ── Varyings to fragment shader ────────────────────────────────────────────
layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout(location=2) out vec4 current_clip;
layout(location=3) out vec4 previous_clip;
layout( location = 4 ) out vec3 position_world;


// ── Push constants ─────────────────────────────────────────────────────────
#include "../GrassDrawData.glsl"
#include "../GrassSurfaceNormal.glsl"

// ─────────────────────────────────────────────────────────────────────────────
// Set 3: Vegetation Draw SSBOs
// ─────────────────────────────────────────────────────────────────────────────

// Binding 0 — Bone matrices computed by bone-sim (one mat4 per bone per instance)
#include "../GrassGeometry.glsl"

// ─────────────────────────────────────────────────────────────────────────────
void main()
{
    // gl_InstanceIndex = a * subBladeCount + s  where a ∈ [0..assignedCount)
    uint subBladeIdx  = gl_InstanceIndex % pc.subBladeCount;
    uint localInstIdx = gl_InstanceIndex / pc.subBladeCount;

    // Remap to global instance index
    uint globalInstIdx = instanceRemap[localInstIdx];

    // Instance data
    GrassInstance inst = instances[globalInstIdx];

    // P0: GPU 剔除检查 — 不可见实例的顶点直接退化为零面积三角形
    if (visibilityFlags[globalInstIdx] == 0u)
    {
        gl_Position = vec4(0.0);
        return;
    }

    // P1: 基于距离的子叶片 LOD — 远距离减少子叶片数，大幅降低 VS/FS 开销
    // 用实例 ID 的 golden-ratio hash 做 ±3m 抱动，避免固定距离处密度突变
    {
        float distXZ = distance(grassRoot(inst).xz, cameraPosition.xz);
        float dither = fract(float(inst.randomSeed) * 0.6180339887) * 6.0 - 3.0;
        float ditheredDist = distXZ + dither;

        uint maxSub = pc.subBladeCount;
        if (pc.lodFarDist > 0.0 && ditheredDist > pc.lodFarDist)
            maxSub = 2u;
        else if (pc.lodMidDist > 0.0 && ditheredDist > pc.lodMidDist)
            maxSub = max(3u, pc.subBladeCount / 2u);

        if (subBladeIdx >= maxSub)
        {
            gl_Position = vec4(0.0);
            return;
        }
    }

    // LOD factor (1 = full physics / frozen rest-pose, fades toward 0 at boundary).
    // We do NOT cull based on lod — even lod≈0 means rest-pose, not invisible.
    // The smoothstep produces lod≈0 right at lodFadeDist; culling there would create
    // a camera-tracking invisible ring.  Only skip truly degenerate vertices (lod<0).
    float lod = lodFactors[globalInstIdx];
    if (lod < 0.0)
    {
        gl_Position = vec4(0.0);
        return;
    }

    // 子叶片跟随实例坐标系；根位置只由 PCG 或手工覆盖确定。
    mat3 modelBasis = mat3(inst.modelMatrix);
    vec3 bladeNormal=inNormal;
    if((pc.vertexFeatureMask&1u)!=0u)
    {
        // 程序化草用圆柱截面法线形成连续叶面高光；外部模型保留作者法线。
        float side=(inUV.x*2.0-1.0)*0.6;
        bladeNormal=vec3(side,0.0,sqrt(1.0-side*side));
    }
    vec2 uv = inUV;

    // ── Dual-bone skinning ──────────────────────────────────────────
    uint globalBoneBase = globalInstIdx * pc.boneCount;
    vec4 bw = boneWeights[gl_VertexIndex]; // per-template-vertex weights

    mat3 skinning=mat3(boneMatrices[globalBoneBase+uint(bw.x)])*bw.z+
        mat3(boneMatrices[globalBoneBase+uint(bw.y)])*bw.w;
    vec3 skinnedNrm=GrassTransformNormal(skinning*modelBasis,bladeNormal);

    // ── Offset by sub-blade root (world-space XZ scatter + terrain Y) ─
    vec3 worldPos = grassWorldPosition(inPosition, globalInstIdx, subBladeIdx, pc.boneCount, bw);

    // LOD only controls whether Verlet physics ran (compute side).
    // Blade scale never changes — no size-based LOD collapse here,
    // which would create a visible discontinuity ring following the camera.

    vec3 N=normalize(skinnedNrm);

    // ── Output ──────────────────────────────────────────────────────
    gl_Position    = VPMatrix * vec4(worldPos, 1.0);
    frag_uv        = uv;
    normal_ws      = N;
    current_clip=UnjitteredVPMatrix*vec4(worldPos,1.0);
    uint previousBase=(pc.instanceCount+globalInstIdx)*pc.boneCount;
    vec3 previousPos=grassRoot(inst)+modelBasis*scatterOffsets[subBladeIdx].xyz+
        skinPosition(modelBasis*inPosition,previousBase,bw);
    previous_clip=LastUnjitteredVPMatrix*vec4(previousPos,1.0);
    position_world = worldPos;
}
