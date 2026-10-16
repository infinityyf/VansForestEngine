#extension GL_GOOGLE_include_directive : require

#include "../Common/Common.glsl"
#include "../GI/GIProbeUpdateCommon.glsl"
#define GI_WORK_SET 0
#define GI_WORK_BINDING 3
#define GI_LAYOUT_SET 0
#define GI_LAYOUT_BINDING 4
#include "../GI/GIProbeLayoutData.glsl"
#include "../GI/GIProbeWorkData.glsl"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer HitPositionBuffer { float hitDistance[]; } hitPositions;
layout(set = 0, binding = 1, std430) readonly buffer HitNormalBuffer { f16vec4 hitNormal[]; } hitNormals;

#include "../GI/GIProbeStateData.glsl"
layout(set = 0, binding = 2, std430) buffer ProbeStateBuffer { GIProbeState states[]; } probeStates;

layout(set = 0, binding = 5, std430) writeonly buffer ProbeFeedbackBuffer { uvec4 records[]; } feedback;
#ifdef GI_WORLD_ENABLED
layout(set = 0, binding = 6, std430) readonly buffer RadianceBuffer { f16vec4 values[]; } radiance;
#endif

#include "../GI/GIProbeDispatch.glsl"


void main()
{
    uint activeLinear = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_NumWorkGroups.x;
    if (activeLinear >= giProbeWork.header.x) return;
    uvec4 work = giProbeWork.entries[activeLinear].scheduling;
    uint rays = giProbeWork.header.y;
    uint fixedCount = GI_FixedRayCount(rays);
    uint offset = GI_WorkRayOffset(activeLinear);
    GIProbeState state = probeStates.states[work.x];
    float cell = giProbeWork.header.w != 0u ? GI_LayoutPosition(giProbeWork.header.z, work.x).w :
        min(giDispatch.gridParams.x, min(giDispatch.gridParams.y, giDispatch.gridParams.z));
#ifdef GI_WORLD_ENABLED
    uint failures=0u;
    bool incomplete=false,primaryIncomplete=false;
    for(uint ray=0u;ray<rays;++ray)
    {
        if(hitPositions.hitDistance[offset+ray]<-1.5)
        {
            incomplete=true;primaryIncomplete=true;
            failures|=GI_TraceFailureStatus(hitPositions.hitDistance[offset+ray]);
        }
        float validity=float(radiance.values[offset+ray].w);
        incomplete=incomplete || validity<0.0;
        uint lightingFailure=GI_LightingFailureStatus(validity);
        primaryIncomplete=primaryIncomplete || (lightingFailure&GI_FEEDBACK_PRIMARY_FAILURE_MASK)!=0u;
        failures|=lightingFailure;
    }
    if(primaryIncomplete || (incomplete && !GI_ProbeCanRetainScattering(state,cell,(work.w&GI_WORK_RESET_GEOMETRY)!=0u)))
    {feedback.records[activeLinear]=uvec4(work.x,failures,work.y,work.z);return;}
#endif
    ivec3 counts = ivec3(giDispatch.dispatchParams.xyz);
    ivec3 coordinate = GI_WorkProbeCoordinate(work.x, counts);
    vec3 traceOffset = state.relocationAndConfidence.xyz;
    // 只修正局部表面间隙。远处几何不能驱动采样原点持续漂移。
    float clearance = cell * 0.1;
    vec3 spacing = giProbeWork.header.w != 0u ? vec3(cell) : giDispatch.gridParams.xyz;
    float localReach = length(spacing * 0.45);
    GIProbeRayDirectionContext context = GI_BuildProbeRayDirectionContext(coordinate, counts,
        uint(giDispatch.dispatchParams.w), work.z);
    uint closeBackfaces = 0u, backfaces = 0u;
    float closestFront = localReach, closestBack = localReach;
    vec3 frontDirection = vec3(0.0), backDirection = vec3(0.0);
    for (uint ray = 0u; ray < fixedCount; ++ray)
    {
        float distance = hitPositions.hitDistance[offset + ray];
        if (distance < 0.0) continue;
        bool fixedRay;
        vec3 direction = GI_ProbeRayDirectionPrepared(context, ray, fixedRay);
        bool backface = float(hitNormals.hitNormal[offset + ray].w) < 0.0;
        if (backface)
        {
            ++backfaces;
            if (distance < clearance) ++closeBackfaces;
            if (distance < closestBack) { closestBack = distance; backDirection = direction; }
        }
        else if (distance < closestFront) { closestFront = distance; frontDirection = direction; }
    }
    // miss 同样属于球面覆盖；少数远处背面不能被放大成墙内证据。
    float ratio = float(backfaces) / float(max(fixedCount, 1u));
    vec3 target = traceOffset;
    // 背面表面在射线前方，脱离局部实体应沿射线越过表面；正面则沿反方向留出间隙。
    if (closestBack < closestFront)
        target += backDirection * (closestBack + clearance);
    else if (closestFront < clearance)
        target -= frontDirection * (clearance - closestFront);
    // 约束整体位移，保留方向；达到间隙后保持原点，不再追逐远处命中或拉回初始点。
    float normalizedDistance = length(target / spacing);
    if (normalizedDistance > 0.45) target *= 0.45 / normalizedDistance;
    bool moved = length(target - traceOffset) > cell * 0.002;
    // 这里只累计几何采样历史；改灯不能重新预热距离可见性。
    uint updates = (work.w & GI_WORK_RESET_GEOMETRY) != 0u || GI_ProbeTraceOriginChanged(state, cell) ? 0u : state.metadata.z;
    // 图集发布在本次实际追踪位置，目标偏移仅供下一批射线使用。
    // 主几何完整、散射沿用同位置历史时也能发布；背面统计仅用于偏移校正与诊断。
    state.traceOffsetAndBackface = vec4(traceOffset, ratio);
    state.relocationAndConfidence = vec4(moved ? target : traceOffset, 1.0);
    state.metadata = uvec4(1u, uint(giDispatch.frameParams.x), min(updates + 1u, 128u), closeBackfaces);
    probeStates.states[work.x] = state;
    uint status=GI_FEEDBACK_COMPLETE;
#ifdef GI_WORLD_ENABLED
    if(incomplete)status=GI_FEEDBACK_PRIMARY_PUBLISHED|failures;
#endif
    feedback.records[activeLinear] = uvec4(work.x, status, work.y, work.z);
}
