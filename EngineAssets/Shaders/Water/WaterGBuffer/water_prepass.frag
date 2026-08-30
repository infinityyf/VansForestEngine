#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in float inLinearDepth;
layout(location = 2) in vec3 inWorldNormal;
layout(location = 3) flat in int inLodLevel;
layout(location = 4) in vec2 inWorldXZ;
layout(location = 5) in vec3 inMacroDPdx;
layout(location = 6) in vec3 inMacroDPdz;

layout(set = 1, binding = 0) uniform WaterSurfaceParams
{
    mat4 waterVPMatrix;
    mat4 waterViewMatrix;
    vec4 waterCameraPosition;
    ivec4 geometryParams;
    vec4 geometryScale;
    vec4 spectrumScale;
    vec4 windAndChop;
    ivec4 simulationParams;
    vec4 waveParticleParams0;
    vec4 waveParticleParams1;
    vec4 flowMapWorld;
    vec4 flowMapParams;
    vec4 flowMapFallback;
    vec4 surfaceOptics;
    vec4 scatteringCoeff;
    vec4 absorptionCoeff;
    ivec4 detailNormalFlags;
    vec4 detailNormalGlobal;
    vec4 detailLayerUvMotion[4];
    vec4 detailLayerStrengthFade[4];
    ivec4 detailLayerEnabled;
    vec4 effectiveRoughnessParams;
} params;
layout(set = 1, binding = 6) uniform sampler2D detailNormalMap;

layout(location = 0) out vec4 outWaterNormal;
layout(location = 1) out vec4 outWaterScatterThickness;
layout(location = 2) out vec4 outWaterAbsorptionFoam;
layout(location = 3) out vec4 outWaterPosDepth;

vec3 BuildFallbackTangent(vec3 normal)
{
    vec3 axis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(axis, normal));
}

vec3 BuildDetailNormal(vec3 macroNormal)
{
    vec3 normal = normalize(macroNormal);
    vec3 tangentCandidate = inMacroDPdx - normal * dot(normal, inMacroDPdx);
    vec3 tangent = dot(tangentCandidate, tangentCandidate) > 1e-8
        ? normalize(tangentCandidate)
        : BuildFallbackTangent(normal);
    vec3 bitangent = normalize(cross(normal, tangent));
    if (dot(bitangent, inMacroDPdz) < 0.0)
        bitangent = -bitangent;

    if (params.detailNormalFlags.x == 0 || params.detailNormalFlags.y == 0)
        return normal;

    vec2 worldDx = dFdx(inWorldXZ);
    vec2 worldDy = dFdy(inWorldXZ);
    float gradientScale = exp2(params.detailNormalGlobal.w);
    float distanceToCamera = distance(inWorldPos, params.waterCameraPosition.xyz);
    vec2 accumulatedSlope = vec2(0.0);

    for (int layerIndex = 0; layerIndex < 4; ++layerIndex)
    {
        if (params.detailLayerEnabled[layerIndex] == 0)
            continue;

        vec4 uvMotion = params.detailLayerUvMotion[layerIndex];
        vec4 strengthFade = params.detailLayerStrengthFade[layerIndex];
        vec2 direction = uvMotion.yz;
        float directionLengthSq = dot(direction, direction);
        direction = directionLengthSq > 1e-8
            ? direction * inversesqrt(directionLengthSq)
            : vec2(1.0, 0.0);
        vec2 perpendicular = vec2(-direction.y, direction.x);
        float inverseTileSize = max(uvMotion.x, 1e-6);

        vec2 uv = vec2(
            dot(inWorldXZ, direction),
            dot(inWorldXZ, perpendicular)) * inverseTileSize;
        uv.x += (uvMotion.w * params.detailNormalGlobal.x) * inverseTileSize
            + strengthFade.y;
        vec2 uvDx = vec2(dot(worldDx, direction), dot(worldDx, perpendicular))
            * inverseTileSize * gradientScale;
        vec2 uvDy = vec2(dot(worldDy, direction), dot(worldDy, perpendicular))
            * inverseTileSize * gradientScale;

        vec2 encodedXY = textureGrad(detailNormalMap, uv, uvDx, uvDy).rg * 2.0 - 1.0;
        if (params.detailNormalFlags.w != 0)
            encodedXY.y = -encodedXY.y;
        float encodedLength = length(encodedXY);
        if (encodedLength > 0.998)
            encodedXY *= 0.998 / encodedLength;
        float reconstructedZ = sqrt(max(1.0 - dot(encodedXY, encodedXY), 1e-5));
        vec2 slope = encodedXY / max(reconstructedZ, 1e-3);
        float distanceFade = 1.0 - smoothstep(
            strengthFade.z,
            max(strengthFade.w, strengthFade.z + 0.01),
            distanceToCamera);
        accumulatedSlope += slope * strengthFade.x
            * params.detailNormalGlobal.y * distanceFade;
    }

    float slopeLength = length(accumulatedSlope);
    float maxSlope = max(params.detailNormalGlobal.z, 0.01);
    if (slopeLength > maxSlope)
        accumulatedSlope *= maxSlope / slopeLength;
    return normalize(
        normal + tangent * accumulatedSlope.x + bitangent * accumulatedSlope.y);
}

float ComputeEffectiveRoughness()
{
    float baseRoughness = clamp(params.surfaceOptics.x, 0.002, 1.0);
    if (int(params.effectiveRoughnessParams.x + 0.5) == 0)
        return baseRoughness;

    float distanceWeight = smoothstep(
        params.effectiveRoughnessParams.y,
        max(params.effectiveRoughnessParams.z, params.effectiveRoughnessParams.y + 0.01),
        inLinearDepth);
    float distanceRoughness = distanceWeight * params.effectiveRoughnessParams.w;
    return clamp(sqrt(
        baseRoughness * baseRoughness + distanceRoughness * distanceRoughness),
        0.002,
        1.0);
}

void main()
{
    vec3 finalNormal = BuildDetailNormal(inWorldNormal);
    float roughness = ComputeEffectiveRoughness();
    float foam = 0.0;
    outWaterNormal = vec4(finalNormal, roughness);
    outWaterScatterThickness = vec4(max(params.scatteringCoeff.rgb, vec3(0.0)), 0.0);
    outWaterAbsorptionFoam = vec4(max(params.absorptionCoeff.rgb, vec3(0.0)), foam);
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
