#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 inMeshPos;

layout(push_constant) uniform WaterPatchPC
{
    vec2  patchWorldOrigin;
    float patchWorldSize;
    int   lodLevel;
    float waterLevel;
    uint  outerEdgeMask;
    uint  innerEdgeMask;
    vec2  pad;
} pc;

layout(set = 1, binding = 0) uniform WaterGBufferParams
{
    mat4  waterVPMatrix;
    mat4  waterViewMatrix;
    vec4  waterCameraPosition;
    float minLodDist;
    int   lodLevels;
    int   meshDim;
    float clipmapBaseScale;
    float maxWaveAmp;
    float detailBalance;
    float morphStartRatio;
    float pad1;
    vec4  waveTimeAndScale;
    vec4  pad3[8];
} waterParams;

layout(set = 1, binding = 1) uniform sampler2DArray waterDisplacementMap;
layout(set = 1, binding = 4) uniform sampler2DArray waterDerivativeMap;

layout(set = 1, binding = 2, std430) readonly buffer WaveSSBO
{
    float waveData[];
} waveBuffer;

const uint EDGE_LEFT  = 1u << 0;
const uint EDGE_RIGHT = 1u << 1;
const uint EDGE_DOWN  = 1u << 2;
const uint EDGE_UP    = 1u << 3;

float ComputeEdgeMorph(vec2 meshPos)
{
    float width = clamp(waterParams.morphStartRatio, 0.001, 1.0);
    float edgeMorph = 0.0;

    if ((pc.outerEdgeMask & EDGE_LEFT) != 0u)
        edgeMorph = max(edgeMorph, 1.0 - smoothstep(0.0, width, meshPos.x));
    if ((pc.outerEdgeMask & EDGE_RIGHT) != 0u)
        edgeMorph = max(edgeMorph, smoothstep(1.0 - width, 1.0, meshPos.x));
    if ((pc.outerEdgeMask & EDGE_DOWN) != 0u)
        edgeMorph = max(edgeMorph, 1.0 - smoothstep(0.0, width, meshPos.y));
    if ((pc.outerEdgeMask & EDGE_UP) != 0u)
        edgeMorph = max(edgeMorph, smoothstep(1.0 - width, 1.0, meshPos.y));

    return clamp(edgeMorph, 0.0, 1.0);
}

vec2 WorldToClipmapUV(vec2 worldXZ, float lodScale)
{
    vec2 camXZ = waterParams.waterCameraPosition.xz;
    vec2 relative = worldXZ - camXZ;
    return (relative / lodScale) + 0.5;
}

vec4 SampleDisplacement(vec2 worldXZ, float lodScale, int lodIdx)
{
    vec2 uv = WorldToClipmapUV(worldXZ, lodScale);
    return textureLod(waterDisplacementMap, vec3(uv, float(lodIdx)), 0.0);
}

int GetWaveMode()
{
    return int(waterParams.pad3[2].x + 0.5);
}

bool UseDerivativeNormal()
{
    return waterParams.pad3[2].y > 0.5;
}

int GetFFTLODCount()
{
    return int(waterParams.pad3[2].z + 0.5);
}

vec3 SampleWaterNormalFromHeight(vec2 worldXZ, float lodScale, int lodIdx)
{
    vec2 texel = 1.0 / vec2(textureSize(waterDisplacementMap, 0).xy);
    float worldStep = max(lodScale, 1.0) * texel.x;
    float hL = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ - vec2(worldStep, 0.0), lodScale), float(lodIdx)), 0.0).y;
    float hR = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ + vec2(worldStep, 0.0), lodScale), float(lodIdx)), 0.0).y;
    float hD = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ - vec2(0.0, worldStep), lodScale), float(lodIdx)), 0.0).y;
    float hU = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ + vec2(0.0, worldStep), lodScale), float(lodIdx)), 0.0).y;
    vec2 gradient = vec2((hR - hL) / (2.0 * worldStep),
                         (hU - hD) / (2.0 * worldStep));
    return normalize(vec3(-gradient.x * waterParams.waveTimeAndScale.w,
                           1.0,
                          -gradient.y * waterParams.waveTimeAndScale.w));
}

vec3 SampleWaterNormalFromDerivative(vec2 worldXZ, float lodScale, int lodIdx)
{
    vec2 uv = WorldToClipmapUV(worldXZ, lodScale);
    vec4 d = textureLod(waterDerivativeMap, vec3(uv, float(lodIdx)), 0.0);
    return normalize(vec3(-d.x * waterParams.waveTimeAndScale.w,
                           1.0,
                          -d.y * waterParams.waveTimeAndScale.w));
}

vec3 SampleWaterNormal(vec2 worldXZ, float lodScale, int lodIdx)
{
    int mode = GetWaveMode();
    if (mode == 1 && UseDerivativeNormal())
        return SampleWaterNormalFromDerivative(worldXZ, lodScale, lodIdx);
    if (mode == 2 && UseDerivativeNormal() && lodIdx < GetFFTLODCount())
        return SampleWaterNormalFromDerivative(worldXZ, lodScale, lodIdx);
    return SampleWaterNormalFromHeight(worldXZ, lodScale, lodIdx);
}

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out float outLinearDepth;
layout(location = 2) out vec3 outWorldNormal;
layout(location = 3) flat out int outLodLevel;

void main()
{
    float edgeMorph = ComputeEdgeMorph(inMeshPos);

    vec2 morphedMeshPos = inMeshPos;
    if (edgeMorph > 0.001)
    {
        float gridDim = float(waterParams.meshDim - 1);
        vec2 gridCoord = inMeshPos * gridDim;
        vec2 snappedGrid = floor(gridCoord * 0.5) * 2.0;
        vec2 snappedMeshPos = snappedGrid / gridDim;
        morphedMeshPos = mix(inMeshPos, snappedMeshPos, edgeMorph);
    }

    vec2 morphedWorldXZ = pc.patchWorldOrigin + morphedMeshPos * pc.patchWorldSize;

    float lodScale = waterParams.clipmapBaseScale * pow(max(waterParams.detailBalance, 1.0), float(pc.lodLevel));
    vec4 waveDisp = SampleDisplacement(morphedWorldXZ, lodScale, pc.lodLevel);

    if (edgeMorph > 0.001 && pc.lodLevel < waterParams.lodLevels - 1)
    {
        float nextLodScale = lodScale * max(waterParams.detailBalance, 1.0);
        vec4 waveDispNext = SampleDisplacement(morphedWorldXZ, nextLodScale, pc.lodLevel + 1);
        waveDisp = mix(waveDisp, waveDispNext, edgeMorph);
    }

    vec2 displacedWorldXZ = morphedWorldXZ + waveDisp.xz;
    vec3 worldPos = vec3(displacedWorldXZ.x, pc.waterLevel + waveDisp.y, displacedWorldXZ.y);

    vec3 worldNormal = SampleWaterNormal(displacedWorldXZ, lodScale, pc.lodLevel);
    if (edgeMorph > 0.001 && pc.lodLevel < waterParams.lodLevels - 1)
    {
        float nextLodScaleN = lodScale * max(waterParams.detailBalance, 1.0);
        vec3 normalNext = SampleWaterNormal(displacedWorldXZ, nextLodScaleN, pc.lodLevel + 1);
        worldNormal = normalize(mix(worldNormal, normalNext, edgeMorph));
    }

    vec4 viewPos = waterParams.waterViewMatrix * vec4(worldPos, 1.0);
    vec4 clipPos = waterParams.waterVPMatrix * vec4(worldPos, 1.0);

    outWorldPos = worldPos;
    outLinearDepth = -viewPos.z;
    outWorldNormal = worldNormal;
    outLodLevel = pc.lodLevel;

    gl_Position = clipPos;
}
