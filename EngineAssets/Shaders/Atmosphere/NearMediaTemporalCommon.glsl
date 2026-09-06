#ifndef VANS_NEAR_MEDIA_TEMPORAL_COMMON_GLSL
#define VANS_NEAR_MEDIA_TEMPORAL_COMMON_GLSL

// 每个 XY 列使用相同的 Z 偏移，避免把相邻深度切片推入彼此的采样区间。
// 16 帧内遍历每个等宽子区间的中心，空间哈希只改变遍历相位。
const float NEAR_MEDIA_SUB_FROXEL_SEQUENCE[16] = float[](
    0.03125, 0.53125, 0.28125, 0.78125,
    0.15625, 0.65625, 0.40625, 0.90625,
    0.09375, 0.59375, 0.34375, 0.84375,
    0.21875, 0.71875, 0.46875, 0.96875);

uint NearMediaHashColumn(uvec2 column)
{
    uint value = column.x * 0x8da6b343u ^ column.y * 0xd8163841u;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    return value;
}

float NearMediaSubFroxelJitter(uvec2 column, uint frameIndex)
{
    uint phase = NearMediaHashColumn(column) & 15u;
    return NEAR_MEDIA_SUB_FROXEL_SEQUENCE[(frameIndex + phase) & 15u];
}

vec4 NearMediaHistoryUVW(vec3 worldPosition, float nearViewDepth,
    float farViewDepth, float slicePower)
{
    vec4 previousClip = LastProjectionMatrix * LastViewMatrix *
        vec4(worldPosition, 1.0);
    if (previousClip.w <= 0.0)
        return vec4(0.0);

    vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
    previousUv.y = 1.0 - previousUv.y;
    float previousViewDepth =
        -(LastViewMatrix * vec4(worldPosition, 1.0)).z;
    if (previousViewDepth < nearViewDepth ||
        previousViewDepth > farViewDepth)
    {
        return vec4(0.0);
    }

    float normalized = (previousViewDepth - nearViewDepth) /
        max(farViewDepth - nearViewDepth, 1.0e-5);
    float sliceUv = pow(clamp(normalized, 0.0, 1.0),
        1.0 / max(slicePower, 1.0e-4));
    return vec4(previousUv, sliceUv, 1.0);
}

#endif
