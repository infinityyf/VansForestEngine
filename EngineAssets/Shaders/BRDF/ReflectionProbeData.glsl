#ifndef REFLECTION_PROBE_DATA_INCLUDED
#define REFLECTION_PROBE_DATA_INCLUDED
#extension GL_EXT_nonuniform_qualifier : require

const uint REFLECTION_PROBE_ENABLED = 1u;
const uint REFLECTION_PROBE_BOX_PROJECTION = 2u;
const int REFLECTION_PROBE_MAX_BLEND = 4;

struct ReflectionProbeGPU
{
    vec4 positionAndRadius;
    vec4 boxMinAndType;
    vec4 boxMaxAndPriority;
    vec4 fadeAndIntensity;
    vec4 capturePositionAndLayer;
    uvec4 regionAndFlags;
    vec4 specularAndMip;
};

layout(set = 0, binding = 13) uniform samplerCubeArray ReflectionProbeSpecular[64];
layout(set = 0, binding = 14, std430) readonly buffer ReflectionProbeBuffer
{
    uint reflectionProbeCount;
    uint reflectionProbeActiveCount;
    uint reflectionProbeMaxBlendCount;
    uint reflectionProbeDebugView;
    vec4 reflectionProbeLightingParams;
    vec4 reflectionProbeUniformGridOrigin;
    vec4 reflectionProbeUniformGridInvCellSize;
    uvec4 reflectionProbeUniformGridDimensionsAndFlags;
    ReflectionProbeGPU reflectionProbes[];
};

layout(set = 0, binding = 36, std430) readonly buffer ReflectionProbeSpatialIndex
{
    vec4 reflectionProbeIndexOrigin;
    vec4 reflectionProbeIndexInverseCellSize;
    uvec4 reflectionProbeIndexDimensionsAndCellCount;
    uint reflectionProbeIndexWords[];
};

uvec2 ReflectionProbeCandidateRange(vec3 P)
{
    vec3 gridPosition = (P - reflectionProbeIndexOrigin.xyz) * reflectionProbeIndexInverseCellSize.xyz;
    uvec3 dimensions = reflectionProbeIndexDimensionsAndCellCount.xyz;
    if (any(isnan(gridPosition)) || any(isinf(gridPosition)) ||
        any(lessThan(gridPosition, vec3(0.0))) ||
        any(greaterThanEqual(gridPosition, vec3(dimensions)))) return uvec2(0u);
    uvec3 cell = uvec3(floor(gridPosition));
    uint index = cell.x + dimensions.x * (cell.y + dimensions.y * cell.z);
    return uvec2(reflectionProbeIndexWords[index * 2u], reflectionProbeIndexWords[index * 2u + 1u]);
}

struct ReflectionProbeSample
{
    vec3 specular;
    float coverage;
    float topWeight;
    int topIndex;
    vec3 parallaxDelta;
};

#include "../SkyLighting/SkyLighting.glsl"

bool ReflectionProbeContainsBox(vec3 P, ReflectionProbeGPU probe)
{
    return all(greaterThanEqual(P, probe.boxMinAndType.xyz)) &&
        all(lessThanEqual(P, probe.boxMaxAndPriority.xyz));
}

float ReflectionProbeInfluence(vec3 P, ReflectionProbeGPU probe)
{
    if ((probe.regionAndFlags.y & REFLECTION_PROBE_ENABLED) == 0u) return 0.0;
    if (probe.boxMinAndType.w > 0.5)
    {
        // The authored box is the full-strength volume. Fade outside it so
        // adjacent tiled probes overlap instead of both reaching zero at the
        // shared face and exposing a line of the global sky fallback.
        vec3 outside = max(max(probe.boxMinAndType.xyz - P,
            P - probe.boxMaxAndPriority.xyz), vec3(0.0));
        float outsideDistance = length(outside);
        if (outsideDistance <= 0.0) return 1.0;
        return 1.0 - smoothstep(0.0, max(probe.fadeAndIntensity.x, 0.001), outsideDistance);
    }

    float radius = max(probe.positionAndRadius.w, 0.001);
    float blend = clamp(probe.fadeAndIntensity.x, 0.001, radius);
    float distanceToCenter = length(P - probe.positionAndRadius.xyz);
    if (distanceToCenter > radius) return 0.0;
    return 1.0 - smoothstep(max(radius - blend, 0.0), radius, distanceToCenter);
}

float ReflectionProbeWeightFromInfluence(float influence, ReflectionProbeGPU probe)
{
    float priority = exp2(clamp(probe.boxMaxAndPriority.w, -8.0, 8.0));
    return max(influence, 0.0) * priority * max(probe.fadeAndIntensity.y, 0.0);
}

vec3 ReflectionProbeBoxProject(vec3 P, vec3 R, ReflectionProbeGPU probe)
{
    vec3 signs = mix(vec3(-1.0), vec3(1.0), greaterThanEqual(R, vec3(0.0)));
    vec3 safeR = signs * max(abs(R), vec3(1e-5));
    vec3 towardMax = (probe.boxMaxAndPriority.xyz - P) / safeR;
    vec3 towardMin = (probe.boxMinAndType.xyz - P) / safeR;
    vec3 distances = mix(towardMax, towardMin, lessThan(safeR, vec3(0.0)));
    float hitDistance = max(min(min(distances.x, distances.y), distances.z), 0.0);
    return normalize(P + R * hitDistance - probe.capturePositionAndLayer.xyz);
}

void ReflectionProbeInsertCandidate(float weight, int index,
    inout float weights[REFLECTION_PROBE_MAX_BLEND], inout int indices[REFLECTION_PROBE_MAX_BLEND])
{
    if (weight <= weights[REFLECTION_PROBE_MAX_BLEND - 1]) return;
    int target = REFLECTION_PROBE_MAX_BLEND - 1;
    while (target > 0 && weight > weights[target - 1])
    {
        weights[target] = weights[target - 1];
        indices[target] = indices[target - 1];
        --target;
    }
    weights[target] = weight;
    indices[target] = index;
}

void ReflectionProbeGatherCandidates(vec3 P,
    out float weights[REFLECTION_PROBE_MAX_BLEND], out int indices[REFLECTION_PROBE_MAX_BLEND], out float coverageSum)
{
    weights = float[](0.0, 0.0, 0.0, 0.0);
    indices = int[](-1, -1, -1, -1);
    coverageSum = 0.0;
    if (reflectionProbeUniformGridDimensionsAndFlags.w != 0u)
    {
		vec3 gridPosition = (P - reflectionProbeUniformGridOrigin.xyz) *
			reflectionProbeUniformGridInvCellSize.xyz;
        ivec3 dimensions = ivec3(reflectionProbeUniformGridDimensionsAndFlags.xyz);
		ivec3 primaryCell = clamp(ivec3(floor(gridPosition)), ivec3(0), dimensions - ivec3(1));
		vec3 primaryCenter = vec3(primaryCell) + vec3(0.5);
		ivec3 neighborCell = primaryCell + ivec3(mix(
			vec3(-1.0), vec3(1.0), greaterThanEqual(gridPosition, primaryCenter)));

		// A validated uniform grid has boxes contained by their cell and fades no
		// wider than half a cell. Consequently only one neighbour per axis can
		// influence P: 2 x 2 x 2 = at most eight probes instead of 27.
		for (int z = 0; z < 2; ++z)
		for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
        {
			ivec3 candidateCell = ivec3(
				x == 0 ? primaryCell.x : neighborCell.x,
				y == 0 ? primaryCell.y : neighborCell.y,
				z == 0 ? primaryCell.z : neighborCell.z);
            if (any(lessThan(candidateCell, ivec3(0))) || any(greaterThanEqual(candidateCell, dimensions)))
                continue;
            uint i = uint(candidateCell.x + dimensions.x *
                (candidateCell.y + dimensions.y * candidateCell.z));
            if (i < reflectionProbeCount)
            {
                float influence = ReflectionProbeInfluence(P, reflectionProbes[i]);
                coverageSum += influence;
				ReflectionProbeInsertCandidate(
					ReflectionProbeWeightFromInfluence(influence, reflectionProbes[i]),
					int(i), weights, indices);
            }
        }
    }
    else
    {
        // 非规则布局读取完整局部候选；规则布局保留解析查询及并列权重顺序。
        uvec2 candidateRange = ReflectionProbeCandidateRange(P);
        for (uint candidate = 0u; candidate < candidateRange.y; ++candidate)
        {
            uint i = reflectionProbeIndexWords[candidateRange.x + candidate];
            float influence = ReflectionProbeInfluence(P, reflectionProbes[i]);
            coverageSum += influence;
            ReflectionProbeInsertCandidate(
                ReflectionProbeWeightFromInfluence(influence, reflectionProbes[i]), int(i), weights, indices);
        }
    }

}

ReflectionProbeSample SampleReflectionProbes(vec3 P, vec3 N, vec3 R, float roughness)
{
    ReflectionProbeSample result;
    result.specular = vec3(0.0); result.coverage = 0.0;
    result.topWeight = 0.0; result.topIndex = -1; result.parallaxDelta = vec3(0.0);
    float weights[REFLECTION_PROBE_MAX_BLEND];
    int indices[REFLECTION_PROBE_MAX_BLEND];
    float coverageSum;
    ReflectionProbeGatherCandidates(P, weights, indices, coverageSum);

    float weightSum = 0.0;
    int sampleCount = roughness > 0.75 ? 1 : clamp(int(reflectionProbeMaxBlendCount), 1, REFLECTION_PROBE_MAX_BLEND);
    float dominantPriority = indices[0] >= 0 ? reflectionProbes[indices[0]].boxMaxAndPriority.w : -100000.0;
    for (int slot = 0; slot < sampleCount; ++slot)
    {
        if (indices[slot] < 0 || weights[slot] <= 0.0) continue;
        ReflectionProbeGPU probe = reflectionProbes[indices[slot]];
        // Region priority is a hard leakage boundary. Portal probes use the
        // half-step priority and therefore still bridge neighbouring volumes.
        if (probe.boxMaxAndPriority.w < dominantPriority - 0.75) continue;
        vec3 sampleDirection = R;
        if (probe.boxMinAndType.w > 0.5 && ReflectionProbeContainsBox(P, probe) &&
            (probe.regionAndFlags.y & REFLECTION_PROBE_BOX_PROJECTION) != 0u)
            sampleDirection = ReflectionProbeBoxProject(P, R, probe);
        float layer = probe.capturePositionAndLayer.w;
        float maxMip = max(probe.specularAndMip.z, 0.0);
        vec3 specular = textureLod(ReflectionProbeSpecular[nonuniformEXT(probe.regionAndFlags.w)],
            vec4(sampleDirection, layer), clamp(roughness * maxMip, 0.0, maxMip)).rgb;
        result.specular += specular * weights[slot] * probe.specularAndMip.y;
        weightSum += weights[slot];
        if (slot == 0)
        {
            result.topWeight = weights[slot]; result.topIndex = indices[slot];
            result.parallaxDelta = sampleDirection - R;
        }
    }
    if (weightSum > 1e-5)
    {
        result.specular /= weightSum;
        // Overlapping probes fill each other's internal blend edges. Only the
        // outer edge of the combined influence fades back to the sky.
        result.coverage = clamp(coverageSum, 0.0, 1.0);
    }
    return result;
}

#endif
