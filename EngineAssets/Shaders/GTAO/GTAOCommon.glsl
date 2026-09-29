#ifndef FOREST_GTAO_COMMON_GLSL
#define FOREST_GTAO_COMMON_GLSL

// Horizon integration and depth weighting adapted from Intel XeGTAO.
// Copyright (C) 2016-2021, Intel Corporation. SPDX-License-Identifier: MIT
// See LICENSE.XeGTAO.txt in this directory.
// ForestEngine convention: camera forward is -Z, depth is positive, UV Y is down.
layout(push_constant) uniform GTAOParameters
{
    float radiusMeters;
    float falloffFraction;
    float sampleDistributionPower;
    float depthMipSamplingOffset;
    int sliceCount;
    int stepsPerSide;
    int sourceMip;
    int mipCount;
} gtaoParams;

vec3 GTAOViewPosition(vec2 uv, float depth)
{
    vec4 ray = InverseProjectionMatrix * vec4(uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 1.0, 1.0);
    return ray.xyz * (depth / max(-ray.z, 1e-8));
}

uint GTAOPackEdges(vec4 edges)
{
    uvec4 bits = uvec4(round(clamp(edges, 0.0, 1.0) * 3.0));
    return (bits.x << 6) | (bits.y << 4) | (bits.z << 2) | bits.w;
}
#endif
