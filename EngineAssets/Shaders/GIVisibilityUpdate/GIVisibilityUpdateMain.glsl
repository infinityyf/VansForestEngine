#extension GL_GOOGLE_include_directive : require
#include "../Common/Common.glsl"
#include "../GI/GIProbeCommon.glsl"
#include "../GI/GIProbeUpdateCommon.glsl"
#include "../GI/GIProbeStateData.glsl"
#define GI_WORK_SET 0
#define GI_WORK_BINDING 5
#define GI_LAYOUT_SET 0
#define GI_LAYOUT_BINDING 6
#include "../GI/GIProbeLayoutData.glsl"
#include "../GI/GIProbeWorkData.glsl"
#ifdef GI_WORLD_ENABLED
#include "../GI/GIProbeIrradianceHistory.glsl"
#define GI_WORLD_SET 0
#define GI_WORLD_SCATTER_BINDING 8
#include "../GIWorld/GIWorldScatterData.glsl"
#endif

layout(local_size_x = 16, local_size_y = 16) in;
layout(set = 0, binding = 0, std430) readonly buffer HitDistance { float values[]; } hitDistance;
layout(set = 0, binding = 1, rg32f) uniform image2D visibilityAtlas;
layout(set = 0, binding = 2, std430) readonly buffer Radiance { f16vec4 values[]; } radiance;
layout(set = 0, binding = 3, rgba16f) uniform image2D irradianceAtlas;
layout(set = 0, binding = 4, std430) readonly buffer State { GIProbeState values[]; } states;
#include "../GI/GIProbeDispatch.glsl"

const uint CHUNK = 256u;
shared vec4 rayDirectionDistance[256];
shared vec4 rayRadiance[256];
shared vec4 irradianceTile[64];
shared vec2 distanceTile[256];
shared vec4 stableRotation, cycleRotation;
shared vec2 lightingResponse;
#ifdef GI_WORLD_ENABLED
shared uint unknownRay;
shared vec3 rayScattering[256];
shared uvec2 componentIntegration;
#endif

// 八面体边界连接反向的相邻边，不能简单复制同侧 texel。
ivec2 BorderSource(ivec2 p, int size)
{
    bool bx = p.x == 0 || p.x == size - 1;
    bool by = p.y == 0 || p.y == size - 1;
    ivec2 q = clamp(p, ivec2(1), ivec2(size - 2));
    if (bx && by) return ivec2(size - 1) - q;
    if (bx) q.y = size - 1 - q.y;
    if (by) q.x = size - 1 - q.x;
    return q;
}
vec3 Direction(ivec2 p, int interior)
{ return GI_OctahedralDecode((vec2(p - 1) + 0.5) / float(interior)); }
int Index(ivec2 p, int size) { return p.y * size + p.x; }

void main()
{
    uint activeLinear = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    if (activeLinear >= giProbeWork.header.x) return;
    uvec4 work = giProbeWork.entries[activeLinear].scheduling;
    uint rays = giProbeWork.header.y;
    uint fixedCount = GI_FixedRayCount(rays);
    ivec3 counts = ivec3(giDispatch.dispatchParams.xyz);
    GIProbeState state = states.values[work.x];
    uint lane = gl_LocalInvocationIndex;
    ivec2 p = ivec2(gl_LocalInvocationID.xy);
    float cell = giProbeWork.header.w != 0u ? GI_LayoutPosition(giProbeWork.header.z, work.x).w :
        min(giDispatch.gridParams.x, min(giDispatch.gridParams.y, giDispatch.gridParams.z));
    bool resetGeometry = (work.w & GI_WORK_RESET_GEOMETRY) != 0u || state.metadata.z == 0u || GI_ProbeTraceOriginChanged(state, cell);
#ifdef GI_WORLD_ENABLED
    if(lane==0u)unknownRay=0u;
    barrier();
    for(uint r=lane;r<rays;r+=256u)
    {
        if(hitDistance.values[activeLinear*rays+r]<-1.5)atomicOr(unknownRay,1u);
        uint lightingFailure=GI_LightingFailureStatus(float(radiance.values[activeLinear*rays+r].w));
        if((lightingFailure&GI_FEEDBACK_PRIMARY_FAILURE_MASK)!=0u)atomicOr(unknownRay,1u);
        if((lightingFailure&GI_FEEDBACK_SCATTER_FAILURE_MASK)!=0u)atomicOr(unknownRay,2u);
    }
    barrier();
    bool scatterComplete=(unknownRay&2u)==0u;
    if((unknownRay&1u)!=0u || (!scatterComplete && !GI_ProbeCanRetainScattering(state,cell,resetGeometry)))return;
    if(lane==0u)componentIntegration=resetGeometry?uvec2(0u):histories[work.x].integration;
    barrier();
#endif
    bool distanceInterior = all(greaterThan(p, ivec2(0))) && all(lessThan(p, ivec2(15)));
    bool irradianceInterior = all(greaterThan(p, ivec2(0))) && all(lessThan(p, ivec2(7)));
    vec3 nd = Direction(p, 14), ni = Direction(p, 6);
    if (lane == 0u)
    {
        GIProbeRayDirectionContext context = GI_BuildProbeRayDirectionContext(
            GI_WorkProbeCoordinate(work.x, counts), counts, rays, work.z);
        stableRotation = context.stableRotation; cycleRotation = context.cycleRotation;
        // 光源响应为整探针常量，复用已有同步点，避免每个 texel 重复计算指数。
        vec4 lighting=giProbeWork.entries[activeLinear].lighting;
        lightingResponse.x=GI_LightingHistoryBlend(lighting.x,lighting.z);
#ifdef GI_WORLD_ENABLED
        lightingResponse.y=GI_LightingHistoryBlend(lighting.y,lighting.w);
#endif
    }
    barrier();
    float localRange = giProbeWork.header.w != 0u ? GI_LayoutVisibilityRange(giProbeWork.header.z, work.x) :
        length(giDispatch.gridParams.xyz) + 0.45 * max(giDispatch.gridParams.x, max(giDispatch.gridParams.y, giDispatch.gridParams.z));
    float distanceLimit = min(giDispatch.traceParams.x, localRange);
    vec3 sumE = vec3(0.0);
#ifdef GI_WORLD_ENABLED
    vec3 sumS = vec3(0.0);
#endif
    vec2 sumD = vec2(0.0);
    float sumW = 0.0;
    // 每块射线只读取一次。所有方向的分子和权重累积完成后才进行除法。
    for (uint base = 0u; base < rays; base += CHUNK)
    {
        uint ray = base + lane;
        if (ray < rays)
        {
            GIProbeRayDirectionContext context;
            context.fixedRayCount = fixedCount; context.dynamicRayCount = rays - fixedCount;
            context.stableRotation = stableRotation; context.cycleRotation = cycleRotation;
            bool fixedRay;
            vec3 direction = GI_ProbeRayDirectionPrepared(context, ray, fixedRay);
            float d = hitDistance.values[activeLinear * rays + ray];
            d = d >= 0.0 ? min(d, distanceLimit) : distanceLimit;
            rayDirectionDistance[lane] = vec4(direction, d);
            rayRadiance[lane] = fixedRay ? vec4(0.0) : vec4(radiance.values[activeLinear * rays + ray]);
#ifdef GI_WORLD_ENABLED
            // 任一辅助查询未知时保留整个散射估计；不能丢弃缺失样本后重新归一化。
            rayScattering[lane] = fixedRay || !scatterComplete ? vec3(0) :
                gwScatterEvents[GIWorldScatterAddress(giProbeWork.header.z,activeLinear*rays+ray)].rgb;
#endif
        }
        barrier();
        uint n = min(CHUNK, rays - base);
        for (uint i = 0u; i < n; ++i)
        {
            vec4 sampleRay = rayDirectionDistance[i];
            if (irradianceInterior && base + i >= fixedCount)
            {
                float cosine=max(dot(ni,sampleRay.xyz),0.0);
                sumE += rayRadiance[i].rgb*cosine;
#ifdef GI_WORLD_ENABLED
                sumS += rayScattering[i]*cosine;
#endif
            }
            if (distanceInterior)
            {
                float w = pow(max(dot(nd, sampleRay.xyz), 0.0), clamp(giDispatch.temporalParams.z, 8.0, 16.0));
                if (w > 1e-5) { sumW += w; sumD += vec2(sampleRay.w, sampleRay.w * sampleRay.w) * w; }
            }
        }
        barrier();
    }
    int distanceColumns = imageSize(visibilityAtlas).x / 16;
    int irradianceColumns = imageSize(irradianceAtlas).x / 8;
    ivec2 distanceBase = ivec2(int(work.x) % distanceColumns, int(work.x) / distanceColumns) * 16;
    ivec2 irradianceBase = ivec2(int(work.x) % irradianceColumns, int(work.x) / irradianceColumns) * 8;
    float warmup = state.metadata.z < 4u ? 1.0 / float(state.metadata.z + 1u) : 0.0;
    if (distanceInterior)
    {
        vec2 target = sumW > 1e-5 ? sumD / sumW : vec2(distanceLimit, distanceLimit * distanceLimit);
        float alpha = resetGeometry ? 1.0 : max(warmup, GI_HistoryBlend(giDispatch.temporalParams.y));
        vec2 value = resetGeometry ? target : mix(imageLoad(visibilityAtlas, distanceBase + p).rg, target, alpha);
        distanceTile[lane] = value;
    }
    if (irradianceInterior)
    {
        // 存储 E；接收端原有 INV_PI 负责 Lambert BRDF，不能在此多除一次 pi。
        vec3 target = sumE * (4.0 * PI / float(rays - fixedCount));
#ifdef GI_WORLD_ENABLED
        int direction=(p.y-1)*6+p.x-1;
        float primaryWarmup=componentIntegration.x<4u?1.0/float(componentIntegration.x+1u):0.0;
        float primaryAlpha=resetGeometry?1.0:max(lightingResponse.x,max(primaryWarmup,GI_HistoryBlend(giDispatch.temporalParams.x)));
        vec3 primary=primaryAlpha>=1.0?target:mix(histories[work.x].primary[direction].rgb,target,primaryAlpha);
        vec3 scattering=vec3(0);
        if(scatterComplete)
        {
            float scatterWarmup=componentIntegration.y<4u?1.0/float(componentIntegration.y+1u):0.0;
            float scatterAlpha=resetGeometry?1.0:max(lightingResponse.y,max(scatterWarmup,GI_HistoryBlend(giDispatch.temporalParams.x)));
            vec3 scatterTarget=sumS*(4.0*PI/float(rays-fixedCount));
            scattering=scatterAlpha>=1.0?scatterTarget:mix(histories[work.x].scattering[direction].rgb,scatterTarget,scatterAlpha);
            histories[work.x].scattering[direction]=vec4(scattering,0);
        }
        else scattering=histories[work.x].scattering[direction].rgb;
        histories[work.x].primary[direction]=vec4(primary,0);
        irradianceTile[Index(p,8)]=vec4(primary+scattering,1.0);
#else
        vec3 previous = imageLoad(irradianceAtlas, irradianceBase + p).rgb;
        float alpha = resetGeometry ? 1.0 : max(lightingResponse.x,max(warmup, GI_HistoryBlend(giDispatch.temporalParams.x)));
        irradianceTile[Index(p, 8)] = vec4(alpha>=1.0?target:mix(previous, target, alpha), 1.0);
#endif
    }
    barrier();
#ifdef GI_WORLD_ENABLED
    if(lane==0u)
        histories[work.x].integration=uvec2(min(componentIntegration.x+1u,128u),
            scatterComplete?min(componentIntegration.y+1u,128u):componentIntegration.y);
#endif
    // 所有边界从完整的 interior 复制，不再为 border 重复积分。
    imageStore(visibilityAtlas, distanceBase + p, vec4(distanceTile[Index(BorderSource(p, 16), 16)], 0.0, 0.0));
    if (all(lessThan(p, ivec2(8))))
    {
        ivec2 source = BorderSource(p, 8);
        vec4 directional = irradianceTile[Index(source, 8)];
        imageStore(irradianceAtlas, irradianceBase + p, directional);
    }
}
