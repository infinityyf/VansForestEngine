#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../Common/Common.glsl"
#include "../GI/GIProbeStateData.glsl"
#include "../GI/GIProbeUpdateCommon.glsl"
#define GI_WORK_SET 1
#define GI_WORK_BINDING 14
#include "../GI/GIProbeWorkData.glsl"
#define PUNCTUAL_SHADOW_CONSUMER_GI 1
#ifdef GI_WORLD_ENABLED
#include "../GIWorld/GIWorldQuery.glsl"
#include "../GIWorld/GIWorldScattering.glsl"
#include "../GIWorld/GIWorldScatterData.glsl"
#endif
#include "../Lights/LightsData.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(set = 1, binding = 0, std430) buffer hitPositionBuffer { float hitDistance[]; } hitPositionInfoBuffer;
layout(set = 1, binding = 1, std430) buffer hitNormalBuffer { f16vec4 hitNormal[]; } hitNormalInfoBuffer;
layout(set = 1, binding = 2, std430) buffer rayRadianceBuffer { f16vec4 radiance[]; } rayRadiance;
layout(set = 1, binding = 3, std430) readonly buffer hitEmissionBuffer { f16vec4 emission[]; } hitEmission;
layout(set = 1, binding = 4) uniform samplerCube environmentMap;
layout(set = 1, binding = 8) uniform sampler2D shadowMap;
layout(set = 1, binding = 9) uniform sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT];
layout(set = 1, binding = 10, std430) buffer pbrDataBuffer { f16vec4 albedoRoughness[]; } pbrInfo;
layout(set = 1, binding = 11) uniform sampler2D giVisibilityAtlas[8];
layout(set = 1, binding = 12) uniform sampler2D giIrradianceAtlas[8];
layout(set = 1, binding = 13, std430) readonly buffer ProbeStateBuffer
{
    GIProbeState states[];
} giProbeStates[8];

#define GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex) giProbeStates[nonuniformEXT(regionIndex)].states[probeLinearIndex]
#define GI_LAYOUT_SET 1
#define GI_LAYOUT_BINDING 15
#include "../GI/GIProbeLayoutData.glsl"
#include "../GI/GIProbeCommon.glsl"

#ifdef GI_WORLD_ENABLED
vec4 GI_SampleWorldBounceRegion(uint region, vec3 p, vec3 n, float biasScale)
{
    vec4 size = GI_LayoutRegionSize(region);
    GIProbeLighting value = GI_SampleProbeIrradianceAtlasVisible(region,
        ivec3(GI_LayoutRegionWord(region, 2u).xyz),
        giIrradianceAtlas[nonuniformEXT(region)], giVisibilityAtlas[nonuniformEXT(region)],
        p, n, GI_LayoutRegionMin(region).xyz, size.xyz, size.w * biasScale, 0.0);
    return vec4(value.irradiance, value.published);
}
#define GI_BLEND_REGION_COUNT GI_LayoutRegionCount()
#define GI_BLEND_REGION_MIN(region) GI_LayoutRegionMin(region).xyz
#define GI_BLEND_REGION_SIZE(region) GI_LayoutRegionSize(region).xyz
#define GI_BLEND_REGION_FADE(region) GI_LayoutRegionTrace(region).y
#define GI_BLEND_REGION_PRIORITY(region) GI_LayoutRegionTrace(region).z
#define GI_BLEND_REGION_ALLOWED(region) GI_LayoutRegionUsesWorld(region)
#define GI_BLEND_NORMALIZE_FEEDBACK true
#define GI_SAMPLE_REGION GI_SampleWorldBounceRegion
#define GI_SAMPLE_SKY(N) vec3(0.0)
#include "../GI/GIRegionBlend.glsl"
#endif

layout(push_constant) uniform PushConstants
{
    vec4 gridParams;
    vec4 dispatchParams;
    vec4 frameParams;
    vec4 regionParams;
    vec4 lightingParams;
    vec4 temporalParams;
} pushConstants;


void main()
{
    ivec3 probeCounts = ivec3(pushConstants.dispatchParams.xyz);
    uint raysPerUpdate = giProbeWork.header.y;
    uint rayLinear = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    uint activeLinear = rayLinear / raysPerUpdate;
    if (activeLinear >= giProbeWork.header.x) return;
    uint localRay = rayLinear % raysPerUpdate;
    uint resultIndex = rayLinear;
#ifdef GI_WORLD_ENABLED
    float worldTransmission = float(rayRadiance.radiance[resultIndex].w);
    if (hitPositionInfoBuffer.hitDistance[resultIndex] < -1.5) { rayRadiance.radiance[resultIndex] = f16vec4(0); return; }
#endif
    // 几何射线不执行材质照明或 DDGI 二次查询。
    if (localRay < GI_FixedRayCount(raysPerUpdate))
    { rayRadiance.radiance[resultIndex] = f16vec4(0.0); return; }
    uvec4 work = giProbeWork.entries[activeLinear];
    ivec3 probeIndex = GI_WorkProbeCoordinate(work.x, probeCounts);
    vec3 volumeSize = vec3(probeCounts) * pushConstants.gridParams.xyz;
    vec3 volumeMin = pushConstants.regionParams.xyz - volumeSize * 0.5;
    GIProbeRayDirectionContext context = GI_BuildProbeRayDirectionContext(probeIndex, probeCounts, raysPerUpdate, work.z);
    bool fixedRay;
    vec3 rayDirection = GI_ProbeRayDirectionPrepared(context, localRay, fixedRay);
    float hitT = hitPositionInfoBuffer.hitDistance[resultIndex];
    vec3 origin = GI_LayoutProbePosition(giProbeWork.header.z, work.x, probeCounts, volumeMin, pushConstants.gridParams.xyz) +
        giProbeStates[nonuniformEXT(giProbeWork.header.z)].states[work.x].relocationAndConfidence.xyz;
    vec4 hitPos = vec4(origin + rayDirection * max(hitT, 0.0), hitT < 0.0 ? 0.0 : 1.0);
    // 动态射线保留原有背面翻转法线后的材质照明，几何固定射线才跳过照明。
        vec3 newRadiance;
        if (hitPos.w < 0.5)
        {
            newRadiance = SampleSkyRadiance(environmentMap, rayDirection);
        }
        else
        {
            vec3 hitNormal = normalize(vec3(hitNormalInfoBuffer.hitNormal[resultIndex]));
            vec4 albedoRoughness = vec4(pbrInfo.albedoRoughness[resultIndex]);
			vec3 emittedRadiance = max(vec3(hitEmission.emission[resultIndex]), vec3(0.0));
            vec3 directDiffuse = vec3(0.0);
            // Hit geometry is transient: direct lighting must be evaluated for
            // this ray, not recovered from a prior frame's unrelated surface.
            CalculateDirectDiffuse(hitPos.xyz, hitNormal, shadowMap, punctualShadowMap,
                albedoRoughness, directDiffuse);
            vec3 indirectDiffuse = vec3(0.0);
#ifdef GI_WORLD_ENABLED
            // 户外各级使用当前已发布图集；硬件室内区域不参与世界反弹。
            indirectDiffuse = GI_BlendRegionLighting(hitPos.xyz, hitNormal, 1.0);
#else
            uint bounceRegion = giProbeWork.header.w != 0u ? GI_LayoutSelectRegion(hitPos.xyz) : giProbeWork.header.z;
            if (bounceRegion != GI_INVALID_ADDRESS)
            {
                vec3 bounceMin = volumeMin, bounceSize = volumeSize;
                float bias = pushConstants.regionParams.w;
                if (giProbeWork.header.w != 0u)
                {
                    bounceMin = GI_LayoutRegionMin(bounceRegion).xyz;
                    bounceSize = GI_LayoutRegionSize(bounceRegion).xyz;
                    bias = GI_LayoutRegionSize(bounceRegion).w;
                }
                indirectDiffuse = GI_SampleProbeIrradianceAtlasVisible(bounceRegion, probeCounts,
                    giIrradianceAtlas[nonuniformEXT(bounceRegion)], giVisibilityAtlas[nonuniformEXT(bounceRegion)],
                    hitPos.xyz, hitNormal, bounceMin, bounceSize, bias, 0.0).irradiance; // 反弹采光不再次乘接收区域 fade。
            }
#endif
			newRadiance = emittedRadiance + directDiffuse + min(indirectDiffuse * clamp(albedoRoughness.rgb, 0.0, 1.0),
                vec3(pushConstants.lightingParams.x));
        }

		// A single extreme emissive/sky/direct sample must never contaminate a
		// probe tile for its entire temporal history.
#ifdef GI_WORLD_ENABLED
        vec3 scatteredRadiance=vec3(0);
        vec4 scatter=gwScatterEvents[GIWorldScatterAddress(giProbeWork.header.z,resultIndex)];
        if(scatter.w>=0.0 && any(greaterThan(scatter.rgb,vec3(0))))
        {
            uint seed=GI_HashCombine(GI_HashCombine(work.x,giProbeWork.header.z),GI_HashCombine(localRay,work.z));
            vec3 position=origin+rayDirection*scatter.w;
            vec3 sun=max(uDirectionLight.color.rgb*uDirectionLight.intensity,vec3(0));
            bool hasSun=any(greaterThan(sun,vec3(0)));
            bool sampleSun=hasSun && GI_Hash01(seed^0xa511e9b3u)<.5;
            float sourceProbability=hasSun?.5:1.0;
            vec3 incomingDirection,incomingRadiance;
            if(sampleSun)
            {
                incomingDirection=normalize(uDirectionLight.direction.xyz);
                incomingRadiance=sun*(.25*INV_PI); // 归一化各向同性相函数 1/(4*pi)。
            }
            else
            {
                float y=1.0-2.0*GI_Hash01(seed^0x63d83595u);
                float azimuth=TWO_PI*GI_Hash01(seed^0xb5297a4du);
                float r=sqrt(max(1.0-y*y,0.0));
                incomingDirection=vec3(r*cos(azimuth),y,r*sin(azimuth));
                // 均匀球面采样的 PDF 抵消相函数；地表/树干命中仍为遮挡。
                incomingRadiance=SampleSkyRadiance(environmentMap,incomingDirection);
            }
            float distance=GIWorldEnvironmentDistance(position);
            float spacing=giProbeWork.header.w!=0u?GI_LayoutPosition(giProbeWork.header.z,work.x).w:
                min(pushConstants.gridParams.x,min(pushConstants.gridParams.y,pushConstants.gridParams.z));
            GIWorldHit incoming=GIWorldTrace(position,incomingDirection,.0001,distance,true,GIWorldScatterFootprint(spacing),-1.0,GIWorldScatterStepBudget());
            if(!incoming.valid)
            {
                hitPositionInfoBuffer.hitDistance[resultIndex]=GI_TraceFailureDistance(incoming.failure);
                rayRadiance.radiance[resultIndex]=f16vec4(0);return;
            }
            float visibility=incoming.t<distance?0.0:incoming.transmission;
            if(sampleSun)visibility=min(visibility,SampleGICascadeShadow(position,incomingDirection,shadowMap));
            // 八组动态射线轮转，概率补偿保持均值；单条动态射线最多一次入射查询。
            scatteredRadiance=GIWorldSparseScatterRadiance(scatter.rgb,incomingRadiance,visibility,sourceProbability,pushConstants.lightingParams.y);
        }
        newRadiance=GIWorldCompositeRadiance(newRadiance,worldTransmission,scatteredRadiance,pushConstants.lightingParams.y);
#else
        newRadiance = min(max(newRadiance, vec3(0.0)), vec3(pushConstants.lightingParams.y));
#endif
		rayRadiance.radiance[resultIndex] = f16vec4(newRadiance, 1.0);
}
