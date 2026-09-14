// 草和树共用保守视锥及上一帧 max-depth HZB 判定。
#ifndef VANS_VEGETATION_CULLING_GLSL
#define VANS_VEGETATION_CULLING_GLSL

struct Plane
{
    vec3 normal;
    float d;
};

Plane extractPlane(mat4 m, int row, float sign)
{
    Plane p;
    p.normal.x = m[0][3] + sign * m[0][row];
    p.normal.y = m[1][3] + sign * m[1][row];
    p.normal.z = m[2][3] + sign * m[2][row];
    p.d        = m[3][3] + sign * m[3][row];
    float len = length(p.normal);
    p.normal /= len;
    p.d      /= len;
    return p;
}

shared Plane sharedFrustum[6];

bool sphereInsideFrustum(vec3 center, float radius)
{
    for (int i = 0; i < 6; ++i)
    {
        if (dot(sharedFrustum[i].normal, center) + sharedFrustum[i].d + radius < 0.0)
            return false;
    }
    return true;
}

float sampleHiZCell(ivec2 cell, int mipLevel, int mipMaxIndex)
{
    int   lod      = clamp(mipLevel, 0, mipMaxIndex);
    ivec2 mipSize  = textureSize(hizDepth, lod);
    return texelFetch(hizDepth, clamp(cell, ivec2(0), mipSize - 1), lod).r;
}

bool occlusionCellFullyInFront(
    ivec2 cell,
    int mipLevel,
    int mipMaxIndex,
    float sphereNearDepth,
    float bias)
{
    float sceneMaxDepth = sampleHiZCell(cell, mipLevel, mipMaxIndex);
    if (sceneMaxDepth >= 1e4)
        return false;

    return sphereNearDepth > sceneMaxDepth + bias;
}

bool projectSphereToPreviousFrame(
    vec3 sphereCenter,
    float sphereRadius,
    out vec2 uvMin,
    out vec2 uvMax,
    out float sphereNearDepth)
{
    vec3 previousViewCenter = (LastViewMatrix * vec4(sphereCenter, 1.0)).xyz;
    sphereNearDepth = -previousViewCenter.z - sphereRadius;

    // 球与上一帧近平面相交时，历史投影不完整，必须保留。
    if (sphereNearDepth <= 0.001)
        return false;

    // 用包含球体的 view-space AABB 八角点求屏幕矩形。相比中心点半径近似，
    // 该矩形在宽视角和屏幕边缘也不会低估 footprint。
    uvMin = vec2(1e20);
    uvMax = vec2(-1e20);
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        vec3 cornerSign = vec3(
            (corner & 1u) != 0u ? 1.0 : -1.0,
            (corner & 2u) != 0u ? 1.0 : -1.0,
            (corner & 4u) != 0u ? 1.0 : -1.0);
        vec3 viewCorner = previousViewCenter + cornerSign * sphereRadius;
        vec4 clipCorner = LastProjectionMatrix * vec4(viewCorner, 1.0);
        if (clipCorner.w <= 0.0001)
            return false;

        vec2 ndc = clipCorner.xy / clipCorner.w;
        vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
    }

    // footprint 只要有一部分在上一帧屏幕外，就没有完整的历史遮挡证据。
    return uvMin.x >= 0.0 && uvMax.x <= 1.0 &&
           uvMin.y >= 0.0 && uvMax.y <= 1.0;
}

bool conservativePreviousFrameHiZOccluded(vec3 sphereCenter, float sphereRadius)
{
    vec2 uvMin;
    vec2 uvMax;
    float sphereNearDepth;
    if (!projectSphereToPreviousFrame(
            sphereCenter, sphereRadius, uvMin, uvMax, sphereNearDepth))
        return false;

    int mipMaxIndex = pc.hizMipCount - 1;
    if (mipMaxIndex < 0)
        return false;

    ivec2 baseSize = textureSize(hizDepth, 0);
    vec2 rectPixels = max((uvMax - uvMin) * vec2(baseSize), vec2(0.0));
    float maxRectPixels = max(rectPixels.x, rectPixels.y);
    if (maxRectPixels <= 0.5)
        return false;

    // ceil(log2) 使 footprint 在该 mip 的每个轴上不超过一个 texel 宽，
    // 因而最多与 2x2 个 cell 相交。必须逐个 cell 通过 max-depth 测试。
    int mipLevel = clamp(
        int(ceil(log2(max(maxRectPixels, 1.0)))),
        0,
        mipMaxIndex);
    ivec2 mipSize = textureSize(hizDepth, mipLevel);
    ivec2 minCell = clamp(
        ivec2(floor(uvMin * vec2(mipSize))),
        ivec2(0),
        mipSize - 1);
    ivec2 maxCell = clamp(
        ivec2(ceil(uvMax * vec2(mipSize))) - ivec2(1),
        ivec2(0),
        mipSize - 1);

    ivec2 cellCount = maxCell - minCell + ivec2(1);
    if (cellCount.x <= 0 || cellCount.y <= 0 ||
        cellCount.x > 2 || cellCount.y > 2)
        return false;

    for (int y = minCell.y; y <= maxCell.y; ++y)
    {
        for (int x = minCell.x; x <= maxCell.x; ++x)
        {
            if (!occlusionCellFullyInFront(
                    ivec2(x, y),
                    mipLevel,
                    mipMaxIndex,
                    sphereNearDepth,
                    pc.hizSampleBias))
                return false;
        }
    }
    return true;
}

#endif
