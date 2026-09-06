#ifndef VANS_NEAR_MEDIA_DEPTH_COMMON_GLSL
#define VANS_NEAR_MEDIA_DEPTH_COMMON_GLSL

// NearMedia Z 按视空间前向深度分层。光学积分仍沿当前像素射线
// 使用真实米制路径，因此必须在 view depth 和 ray distance 之间显式换算。
float NearMediaViewForwardCosine(vec3 unitWorldRayDirection)
{
    vec3 viewDirection =
        (ViewMatrix * vec4(unitWorldRayDirection, 0.0)).xyz;
    return max(-viewDirection.z, 1.0e-4);
}

float NearMediaRayDistanceFromViewDepth(
    vec3 unitWorldRayDirection,
    float viewDepthMeters)
{
    return max(viewDepthMeters, 0.0) /
        NearMediaViewForwardCosine(unitWorldRayDirection);
}

float NearMediaViewDepthFromRayDistance(
    vec3 unitWorldRayDirection,
    float rayDistanceMeters)
{
    return max(rayDistanceMeters, 0.0) *
        NearMediaViewForwardCosine(unitWorldRayDirection);
}

float NearMediaSliceViewDepth(
    float sliceCoordinate,
    float nearViewDepthMeters,
    float farViewDepthMeters,
    float sliceCount,
    float slicePower)
{
    float normalized = clamp(
        sliceCoordinate / max(sliceCount, 1.0), 0.0, 1.0);
    return mix(nearViewDepthMeters, farViewDepthMeters,
        pow(normalized, slicePower));
}

// 将任意世界位置映射到当前相机的 NearMedia 视锥体素。
// w 为有效标记；调用方必须在采样前检查 xyz 范围，不能依赖 clamp
// 把视锥外介质复制到边界。
vec4 NearMediaCurrentUVW(vec3 worldPosition,
    float nearViewDepthMeters,
    float farViewDepthMeters,
    float slicePower)
{
    vec4 viewPosition = ViewMatrix * vec4(worldPosition, 1.0);
    float viewDepth = -viewPosition.z;
    if (viewDepth < nearViewDepthMeters ||
        viewDepth > farViewDepthMeters)
    {
        return vec4(0.0);
    }

    vec4 clipPosition = ProjectionMatrix * viewPosition;
    if (clipPosition.w <= 0.0)
        return vec4(0.0);
    vec2 uv = clipPosition.xy / clipPosition.w * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float normalizedDepth =
        (viewDepth - nearViewDepthMeters) /
        max(farViewDepthMeters - nearViewDepthMeters, 1.0e-5);
    float sliceUv = pow(clamp(normalizedDepth, 0.0, 1.0),
        1.0 / max(slicePower, 1.0e-4));
    return vec4(uv, sliceUv, 1.0);
}

#endif
