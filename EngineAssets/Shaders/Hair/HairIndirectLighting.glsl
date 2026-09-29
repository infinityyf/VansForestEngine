#ifndef VANS_HAIR_INDIRECT_LIGHTING_GLSL
#define VANS_HAIR_INDIRECT_LIGHTING_GLSL
#include "../BRDF/AmbientSkyTransmittance.glsl"
#if HAIR_HAS_GI_PROBES
#include "../GI/GIProbeStateData.glsl"
layout(set=1, binding=5) uniform sampler2D hairGIIrradiance[8];
layout(set=1, binding=6) uniform sampler2D hairGIVisibility[8];
layout(set=1, binding=7, std430) readonly buffer HairGIProbeStateBuffer
{
    GIProbeState states[];
} hairGIStates[8];

#define GI_LOAD_PROBE_STATE(region, probe) hairGIStates[nonuniformEXT(region)].states[probe]
#define GI_LAYOUT_SET 1
#define GI_LAYOUT_BINDING 8
#include "../GI/GIProbeLayoutData.glsl"
// Transparent receivers query world-space probe moments. Opaque screen receiver
// records and SSGI history describe a different surface and cannot be reused.
#include "../GI/GIProbeCommon.glsl"
vec4 SampleHairGIRegion(uint region, vec3 position, vec3 normal, float biasScale)
{
    GIProbeLighting value = GI_SampleProbeIrradianceAtlasVisible(region,
        ivec3(GI_LayoutRegionWord(region, 2u).xyz),
        hairGIIrradiance[nonuniformEXT(region)], hairGIVisibility[nonuniformEXT(region)],
        position, normal, GI_LayoutRegionMin(region).xyz, GI_LayoutRegionSize(region).xyz,
        GI_LayoutRegionSize(region).w * biasScale, 0.0);
    return vec4(value.irradiance, value.published);
}
#endif
layout(set=1, binding=9) uniform samplerCube hairSkyDiffuse;
vec3 SampleHairSkyDiffuse(vec3 position, vec3 direction)
{
    return SampleSkyDiffuseCube(hairSkyDiffuse, direction) *
        SampleAmbientSkyTransmittance(position, direction, 1.0).visibility;
}
#if HAIR_HAS_GI_PROBES
#define GI_BLEND_REGION_COUNT GI_LayoutRegionCount()
#define GI_BLEND_REGION_MIN(region) GI_LayoutRegionMin(region).xyz
#define GI_BLEND_REGION_SIZE(region) GI_LayoutRegionSize(region).xyz
#define GI_BLEND_REGION_FADE(region) GI_LayoutRegionTrace(region).y
#define GI_BLEND_REGION_PRIORITY(region) GI_LayoutRegionTrace(region).z
#define GI_SAMPLE_REGION SampleHairGIRegion
#define GI_SAMPLE_SKY(N) SampleHairSkyDiffuse(worldPosition, N)
#include "../GI/GIRegionBlend.glsl"
#endif
vec3 SampleHairLowFrequency(vec3 position, vec3 normal)
{
#if HAIR_HAS_GI_PROBES
    return GI_BlendRegionLighting(position, normal, 1.0);
#else
    return SampleHairSkyDiffuse(position, normal);
#endif
}
vec3 SampleHairEnvironmentRadiance(vec3 position, vec3 normal, vec3 direction, float roughness)
{
    ReflectionProbeSample probe = SampleReflectionProbes(position, normal, direction, roughness);
    if (probe.coverage >= 1.0) return probe.specular;
    float lod = roughness * float(textureQueryLevels(PreConvSpecularEnvironment) - 1);
    vec3 sky = SampleSkySpecularCube(PreConvSpecularEnvironment, direction, lod) *
        SampleAmbientSkyTransmittance(position, direction, roughness).visibility;
    return mix(sky, probe.specular, probe.coverage);
}
#endif
