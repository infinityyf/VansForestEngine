#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 normalWS;
layout(location = 2) in vec3 tangentWS;
layout(location = 3) in vec3 bitangentWS;
layout(location = 4) in vec3 positionWS;

layout(set = 4, binding = 0) uniform sampler2D hairAlbedo;
layout(set = 4, binding = 1) uniform sampler2D hairAlpha;
layout(set = 4, binding = 2) uniform sampler2D hairNormal;
layout(set = 4, binding = 3) uniform sampler2D hairRoughness;
layout(set = 4, binding = 4) uniform sampler2D hairAO;
layout(set = 4, binding = 5) uniform sampler2D hairShift;
layout(set = 4, binding = 6) uniform sampler2D hairFlow;
layout(set = 4, binding = 7) uniform sampler2D hairID;
layout(set = 4, binding = 8) uniform HairParamsBlock
{
    vec4 absorption;
    vec4 roughnessScale;
    vec4 shiftParams;
    vec4 coverageParams;
} hairParams;

layout(r32ui, set = 1, binding = 0) uniform uimage2D hairOITHeads;

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

layout(std430, set = 1, binding = 1) buffer HairOITNodes
{
    HairOITNode nodes[];
} hairOITNodes;

layout(std430, set = 1, binding = 2) buffer HairOITCounter
{
    uint nodeCount;
    uint maxNodes;
} hairOITCounter;

vec2 OctEncode(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-5);
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.xy;
}

uint PackSnormOct(vec3 n)
{
    return packSnorm2x16(OctEncode(normalize(n)));
}

float Hash3D(vec3 s)
{
    return fract(sin(mod(dot(s, vec3(171.0, 131.0, 7.0)), 6.2831853)) * 53758.5453);
}

float HashedAlpha(float alpha, vec3 objPos, float frameJitter)
{
    float maxDeriv = max(max(fwidth(objPos.x), fwidth(objPos.y)), fwidth(objPos.z));
    float pixScale = 1.0 / max(maxDeriv, 1e-5);
    float pixScaleMin = exp2(floor(log2(pixScale)));
    float pixScaleMax = exp2(ceil(log2(pixScale)));
    float a0 = Hash3D(floor(pixScaleMin * objPos) + frameJitter);
    float a1 = Hash3D(floor(pixScaleMax * objPos) + frameJitter);
    float t = fract(log2(pixScale));
    return step(mix(a0, a1, t), alpha);
}

layout(early_fragment_tests) in;

void main()
{
    float alphaCutoff = hairParams.coverageParams.x;
    float coverageScale = hairParams.coverageParams.y;

    float alpha = texture(hairAlpha, fragUV).r;
    float coverage = clamp((alpha - alphaCutoff) * coverageScale, 0.0, 1.0);
    if (coverage <= 0.0001)
        discard;

    vec3 albedo = texture(hairAlbedo, fragUV).rgb;
    vec3 nTS = texture(hairNormal, fragUV).xyz * 2.0 - 1.0;
    nTS.xy *= hairParams.shiftParams.z;

    mat3 tbn = mat3(normalize(tangentWS), normalize(bitangentWS), normalize(normalWS));
    vec3 N = normalize(tbn * nTS);
    vec3 T = normalize(tangentWS);

    vec2 flow = texture(hairFlow, fragUV).rg * 2.0 - 1.0;
    float flowLen = clamp(length(flow), 0.0, 1.0);
    if (flowLen > 1e-3)
    {
        vec3 flowWS = normalize(tbn * vec3(flow, 0.0));
        T = normalize(mix(T, flowWS, flowLen * hairParams.shiftParams.y));
        N = normalize(mix(N, normalize(N - flowWS * flowLen), hairParams.shiftParams.w));
    }

    float rough = texture(hairRoughness, fragUV).r;
    float roughR = clamp(rough * hairParams.roughnessScale.x, 0.035, 1.0);
    float roughTT = clamp(rough * hairParams.roughnessScale.y, 0.025, 1.0);
    float roughTRT = clamp(rough * hairParams.roughnessScale.z, 0.08, 1.0);
    float ao = texture(hairAO, fragUV).r;
    float shift = texture(hairShift, fragUV).r * 2.0 - 1.0;
    float linearDepth = -(ViewMatrix * vec4(positionWS, 1.0)).z;

    uint nodeIndex = atomicAdd(hairOITCounter.nodeCount, 1u);
    if (nodeIndex >= hairOITCounter.maxNodes)
        return;

    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint previousHead = imageAtomicExchange(hairOITHeads, pixel, nodeIndex);

    hairOITNodes.nodes[nodeIndex].next = previousHead;
    hairOITNodes.nodes[nodeIndex].depth = linearDepth;
    hairOITNodes.nodes[nodeIndex].coverage = coverage;
    hairOITNodes.nodes[nodeIndex].albedoRG = packHalf2x16(albedo.rg);
    hairOITNodes.nodes[nodeIndex].albedoB_RoughR = packHalf2x16(vec2(albedo.b, roughR));
    hairOITNodes.nodes[nodeIndex].normalOct = PackSnormOct(N);
    hairOITNodes.nodes[nodeIndex].tangentOct = PackSnormOct(T);
    hairOITNodes.nodes[nodeIndex].roughTT_TRT = packHalf2x16(vec2(roughTT, roughTRT));
    hairOITNodes.nodes[nodeIndex].aoShift = packHalf2x16(vec2(ao, shift));
    hairOITNodes.nodes[nodeIndex].pad0 = 0u;
}
