#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#define LightCBBind 0
#include "../../Lights/LightsData.glsl"
#include "../../BRDF/BRDFData.glsl"
#include "../../BRDF/BRDFHair.glsl"

layout(location = 0) in vec2 fragUV;

layout(r32ui, set = 1, binding = 0) uniform readonly uimage2D hairOITHeads;

struct HairOITNode
{
    uint next;
    float depth;
    float coverage;
    uint albedoRG;
    uint albedoB_RoughR;
    uint normalOct;
    uint tangentOct;
    uint roughTT_TRT;
    uint aoShift;
    uint pad0;
};

layout(std430, set = 1, binding = 1) readonly buffer HairOITNodes
{
    HairOITNode nodes[];
} hairOITNodes;

layout(std430, set = 1, binding = 2) readonly buffer HairOITCounter
{
    uint nodeCount;
    uint maxNodes;
} hairOITCounter;

layout(set = 1, binding = 3) uniform sampler2D hairDeepOpacity;
layout(set = 1, binding = 4) uniform sampler2DArray cascadeShadowMap;

layout(location = 0) out vec4 outHairColor;

const uint HAIR_OIT_EMPTY = 0xffffffffu;
const int HAIR_OIT_SORT_LIMIT = 16;

HairParams DefaultHairParams()
{
    HairParams params;
    params.absorption = vec4(0.35, 0.22, 0.12, 1.0);
    params.shiftParams = vec4(1.0, 0.0, 0.0, 0.0);
    return params;
}

vec4 SliceMaskBefore(float s)
{
    float x = clamp(s, 0.0, 1.0) * 4.0;
    return vec4(
        step(0.0, x),
        step(1.0, x),
        step(2.0, x),
        step(3.0, x));
}

vec3 ReconstructWorldPosition(vec2 uv, float linearDepth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y *= -1.0;
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec3 viewRay = normalize((InverseProjectionMatrix * clip).xyz);
    vec3 viewPos = viewRay * (linearDepth / max(-viewRay.z, 1e-5));
    return (InverseViewMatrix * vec4(viewPos, 1.0)).xyz;
}

vec3 OctDecode(vec2 e)
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return normalize(v);
}

vec4 ProjectToMainLight(vec3 positionWS, out float lightDepth)
{
    vec4 clip = uDirectionLight.shadowMatrix[0] * vec4(positionWS, 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6);
    lightDepth = clamp(ndc.z * 0.5 + 0.5, 0.0, 1.0);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float valid = (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0 && lightDepth > 0.0 && lightDepth < 1.0) ? 1.0 : 0.0;
    return vec4(uv, lightDepth, valid);
}

vec3 DecodeNodeNormal(HairOITNode node)
{
    vec3 normalWS = OctDecode(unpackSnorm2x16(node.normalOct));
    if (length(normalWS) < 0.1)
        normalWS = vec3(0.0, 1.0, 0.0);
    return normalWS;
}

vec4 ShadeHairNode(HairOITNode node, float surfaceVisibility, float sharedHairOpacity)
{
    vec2 albedoRG = unpackHalf2x16(node.albedoRG);
    vec2 albedoB_RoughR = unpackHalf2x16(node.albedoB_RoughR);
    vec2 roughTT_TRT = unpackHalf2x16(node.roughTT_TRT);
    vec2 aoShift = unpackHalf2x16(node.aoShift);

    vec3 albedo = max(vec3(albedoRG, albedoB_RoughR.x), vec3(0.0));
    vec3 normalWS = DecodeNodeNormal(node);

    vec3 tangentWS = OctDecode(unpackSnorm2x16(node.tangentOct));
    if (length(tangentWS) < 0.1)
        tangentWS = normalize(cross(abs(normalWS.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), normalWS));

    float roughnessR = clamp(albedoB_RoughR.y, 0.035, 1.0);
    float roughnessTT = clamp(roughTT_TRT.x, 0.025, 1.0);
    float roughnessTRT = clamp(roughTT_TRT.y, 0.08, 1.0);
    float ao = clamp(aoShift.x, 0.0, 1.0);
    float shift = aoShift.y;

    float linearDepth = node.depth;
    vec3 positionWS = ReconstructWorldPosition(fragUV, linearDepth);
    vec3 V = normalize(cameraPosition.xyz - positionWS);
    vec3 L = normalize(uDirectionLight.direction.xyz);
    vec3 lightColor = max(uDirectionLight.color.rgb * uDirectionLight.intensity, vec3(0.0));

    HairData hair;
    hair.positionWS = positionWS;
    hair.normalWS = normalWS;
    hair.tangentWS = tangentWS;
    hair.viewDirWS = V;
    hair.albedo = albedo;
    hair.coverage = node.coverage;
    hair.roughnessR = roughnessR;
    hair.roughnessTT = roughnessTT;
    hair.roughnessTRT = roughnessTRT;
    hair.shift = shift;
    hair.ao = ao;
    hair.params = DefaultHairParams();

    HairLobes lobes = EvaluateHairLobes(hair, L);
    vec3 sigmaA = HairAbsorptionFromAlbedo(albedo, hair.params.absorption.a);
    vec3 transmittance = exp(-sigmaA * sharedHairOpacity);

    vec3 direct = (lobes.diffuse + lobes.R + lobes.TT * transmittance + lobes.TRT * transmittance)
        * lightColor * surfaceVisibility * ao;
    vec3 indirect = SampleSkyDiffuseCube(PreConvDiffuseEnvironment, normalWS) * albedo * ao;

    float opacity = clamp(node.coverage, 0.0, 1.0);
    return vec4(max(direct + indirect, vec3(0.0)) * opacity, opacity);
}

void InsertSorted(inout HairOITNode sortedNodes[HAIR_OIT_SORT_LIMIT], inout int sortedCount, HairOITNode node)
{
    int insertAt = sortedCount;
    int maxMove = min(sortedCount, HAIR_OIT_SORT_LIMIT - 1);
    for (int i = 0; i < HAIR_OIT_SORT_LIMIT; ++i)
    {
        if (i >= sortedCount)
            break;
        if (node.depth < sortedNodes[i].depth)
        {
            insertAt = i;
            break;
        }
    }
    if (insertAt >= HAIR_OIT_SORT_LIMIT)
        return;

    for (int i = HAIR_OIT_SORT_LIMIT - 1; i > 0; --i)
    {
        if (i <= insertAt || i > maxMove)
            continue;
        sortedNodes[i] = sortedNodes[i - 1];
    }
    sortedNodes[insertAt] = node;
    sortedCount = min(sortedCount + 1, HAIR_OIT_SORT_LIMIT);
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint head = imageLoad(hairOITHeads, pixel).r;
    if (head == HAIR_OIT_EMPTY)
        discard;

    HairOITNode sortedNodes[HAIR_OIT_SORT_LIMIT];
    int sortedCount = 0;
    uint nodeIndex = head;
    for (int i = 0; i < HAIR_OIT_SORT_LIMIT; ++i)
    {
        if (nodeIndex == HAIR_OIT_EMPTY || nodeIndex >= hairOITCounter.maxNodes)
            break;
        HairOITNode node = hairOITNodes.nodes[nodeIndex];
        InsertSorted(sortedNodes, sortedCount, node);
        nodeIndex = node.next;
    }

    vec3 frontNormalWS = DecodeNodeNormal(sortedNodes[0]);
    vec3 frontPositionWS = ReconstructWorldPosition(fragUV, sortedNodes[0].depth);
    float surfaceVisibility = SampleCascadeShadow(frontPositionWS, frontNormalWS, cascadeShadowMap, sortedNodes[0].depth);
    float lightDepth;
    vec4 lightCoord = ProjectToMainLight(frontPositionWS, lightDepth);
    vec4 opacityLayers = lightCoord.w > 0.5 ? texture(hairDeepOpacity, lightCoord.xy) : vec4(0.0);
    float sharedHairOpacity = dot(opacityLayers, SliceMaskBefore(lightDepth));

    vec4 accum = vec4(0.0);
    for (int i = 0; i < HAIR_OIT_SORT_LIMIT; ++i)
    {
        if (i >= sortedCount || accum.a >= 0.999)
            break;
        vec4 layer = ShadeHairNode(sortedNodes[i], surfaceVisibility, sharedHairOpacity);
        accum.rgb += (1.0 - accum.a) * layer.rgb;
        accum.a += (1.0 - accum.a) * layer.a;
    }

    if (accum.a <= 0.0001)
        discard;
    outHairColor = accum;
}
