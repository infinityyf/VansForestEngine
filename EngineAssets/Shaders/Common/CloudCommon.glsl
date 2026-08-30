#ifndef CLOUD_COMMON_GLSL
#define CLOUD_COMMON_GLSL

// ==========================================================================
// CloudCommon.glsl — 体积云公共函数库
//
// 包含:
//   - Remap 工具函数
//   - 高度梯度计算（云层垂直轮廓）
//   - 预计算 Perlin-Worley 3D 噪声采样
//   - 云密度模型
//   - 太阳可见性（逐点光程积分）
//   - 散射光照计算
//   - 球壳射线相交（支持相机在云层内/外/上方全部情形）
//
// 噪声说明:
//   cloudMainNoise: 128^3 RGBA, R=Perlin-Worley 主形状, GBA=Worley 侵蚀
//   cloudDetailNoise: 32^3 RGBA, RGB=低/中/高频细节侵蚀
// ==========================================================================

// --------------------------------------------------------------------------
// 通用工具函数
// --------------------------------------------------------------------------

// 将 value 从 [oldMin, oldMax] 映射到 [newMin, newMax]
float CloudRemap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    return newMin + (value - oldMin) * (newMax - newMin) / max(oldMax - oldMin, 1e-6);
}

// 计算采样点在云层中的归一化高度 [0, 1]，0=云底，1=云顶
float GetCloudHeightFraction(float worldHeight, float cloudMinH, float cloudMaxH)
{
    return clamp((worldHeight - cloudMinH) / max(cloudMaxH - cloudMinH, 1.0), 0.0, 1.0);
}

// 抛物线型高度梯度：云层底部和顶部密度为 0，中部最高
// 相比文档中的指数衰减更易于参数调节，Phase 2 可替换为 Nubis 双 remap 梯度
float GetCloudHeightGradient(float heightFrac)
{
    return heightFrac * (1.0 - heightFrac) * 4.0;
}

// --------------------------------------------------------------------------
// 程序化噪声（保留为调试后备；正式路径使用 3D 噪声纹理）
// --------------------------------------------------------------------------

// quintic fade，减少插值接缝
float _CloudFade5(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// 无 sin 的低开销哈希函数
float _CloudHash3(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

// 3D Value Noise，返回 [0, 1]
float CloudGet3DNoise(vec3 pos)
{
    vec3 ip = floor(pos);
    vec3 fp = fract(pos);

    float n000 = _CloudHash3(ip + vec3(0, 0, 0));
    float n100 = _CloudHash3(ip + vec3(1, 0, 0));
    float n010 = _CloudHash3(ip + vec3(0, 1, 0));
    float n110 = _CloudHash3(ip + vec3(1, 1, 0));
    float n001 = _CloudHash3(ip + vec3(0, 0, 1));
    float n101 = _CloudHash3(ip + vec3(1, 0, 1));
    float n011 = _CloudHash3(ip + vec3(0, 1, 1));
    float n111 = _CloudHash3(ip + vec3(1, 1, 1));

    vec3 u = vec3(_CloudFade5(fp.x), _CloudFade5(fp.y), _CloudFade5(fp.z));

    float x00 = mix(n000, n100, u.x);
    float x10 = mix(n010, n110, u.x);
    float x01 = mix(n001, n101, u.x);
    float x11 = mix(n011, n111, u.x);
    float y0  = mix(x00, x10, u.y);
    float y1  = mix(x01, x11, u.y);
    return mix(y0, y1, u.z);
}

// --------------------------------------------------------------------------
// 云密度采样
//
// worldPos: 以 Environment.Planet 中心为原点的米制坐标。
//   worldPos.y = 实际距星球中心距离（注：当前使用平坦地球近似，直接用 worldPos.y）
// 体积云参数从 CloudRayMarch.comp 的 uCloud 读取，Inspector 可实时调节。
// --------------------------------------------------------------------------
float SampleCloudDensity(vec3 worldPos)
{
    // 径向距离减去 Planet.bottomRadius 得到真实海拔。
    float heightAbove = length(worldPos) - uCloud.planetBottomRadius;

    if (heightAbove < uCloud.cloudMinHeight || heightAbove > uCloud.cloudMaxHeight)
        return 0.0;

    float heightFrac = GetCloudHeightFraction(heightAbove, uCloud.cloudMinHeight, uCloud.cloudMaxHeight);
    float heightGrad = pow(max(GetCloudHeightGradient(heightFrac), 0.0), max(uCloud.verticalShapePower, 0.1));

    // 主噪声使用水平大尺度重复采样，Z 轴使用归一化高度，避免云层在垂直方向被拉成柱状。
    // 纹理由工具导出时已保证 toroidal tiling，因此直接 fract 后 repeat 采样无接缝。
    float mainTile = max(uCloud.mainTileMeters, 1000.0);
    float detailTile = max(uCloud.detailTileMeters, 500.0);
    vec3 mainUVW = fract(vec3(worldPos.x / mainTile,
                              worldPos.z / mainTile,
                              heightFrac * uCloud.mainHeightScale + heightAbove / (mainTile * 2.5)));
    vec3 detailUVW = fract(vec3(worldPos.x / detailTile,
                                worldPos.z / detailTile,
                                heightFrac * uCloud.detailHeightScale + heightAbove / (detailTile * 3.0)));

    vec4 mainNoise   = texture(cloudMainNoise, mainUVW);
    vec3 detailNoise = texture(cloudDetailNoise, detailUVW).rgb;

    // R: Perlin-Worley 主形状。coverage 越大阈值越低，云量越多。
    float threshold = mix(uCloud.thresholdHighCoverage,
                          uCloud.thresholdLowCoverage,
                          clamp(uCloud.coverage, 0.0, 1.0));
    float baseMask  = clamp(CloudRemap(mainNoise.r, threshold, 1.0, 0.0, 1.0), 0.0, 1.0);

    // GBA: 逐级 Worley 侵蚀；Detail RGB: 高频细节侵蚀。
    float mainErosion   = dot(mainNoise.gba, vec3(0.50, 0.32, 0.18));
    float detailErosion = dot(detailNoise, vec3(0.32, 0.34, 0.34));

    // 云层上下边缘和云团外轮廓更容易被侵蚀，中部保留主体形状。
    float heightEdgeErosion = mix(0.70, 0.26, heightGrad) * uCloud.edgeErosionStrength;
    float shapeEdgeErosion  = mix(1.65, 0.40, smoothstep(0.18, 0.86, baseMask));
    float detailStrength    = mix(uCloud.detailErosionStrength * uCloud.detailEdgeStrength,
                                  uCloud.detailErosionStrength * 0.35,
                                  smoothstep(0.35, 0.92, baseMask));
    float cloudMask = baseMask;
    cloudMask -= mainErosion * heightEdgeErosion * shapeEdgeErosion * uCloud.mainErosionStrength;
    cloudMask -= smoothstep(uCloud.detailErosionLow, uCloud.detailErosionHigh, detailErosion) * detailStrength;
    cloudMask = smoothstep(uCloud.densityRemapLow, uCloud.densityRemapHigh, clamp(cloudMask, 0.0, 1.0));

    return cloudMask * heightGrad * uCloud.density;
}

// --------------------------------------------------------------------------
// 球壳射线相交
//
// 支持相机在云层以下 / 云层内 / 云层以上全部六种情形（见文档 §8.8）。
// 返回 vec4: xy = 内球交点对(t_in, t_out)，zw = 外球交点对(t_in, t_out)
// 若分量 .x >= .y，则对应球无有效交叉。
// --------------------------------------------------------------------------

// 两点球面相交，返回 (t_near, t_far)，若无交叉则返回 (-1, -1)
vec2 _CloudRaySphereHit(vec3 rayOrigin, vec3 rayDir, float radius)
{
    float b    = dot(rayOrigin, rayDir);
    float c    = dot(rayOrigin, rayOrigin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return vec2(-1.0, -1.0);
    float sq = sqrt(max(disc, 0.0));
    return vec2(-b - sq, -b + sq);
}

// 云层球壳相交：返回 ray-march 的 [tStart, tEnd]。
// tStart >= tEnd 或 tEnd <= 0 时表示无有效交叉（hit = false 时同）。
struct CloudShellResult
{
    float tStart;
    float tEnd;
    bool  hit;
};

// rayOrigin: 相对于 Planet 中心的米制相机位置。
CloudShellResult IntersectCloudShell(vec3 rayOrigin, vec3 rayDir,
                                      float planetRadius, float shellBot, float shellTop)
{
    float innerR    = planetRadius + shellBot;
    float outerR    = planetRadius + shellTop;
    float camHeight = length(rayOrigin) - planetRadius;

    vec2 outerHit = _CloudRaySphereHit(rayOrigin, rayDir, outerR);
    vec2 innerHit = _CloudRaySphereHit(rayOrigin, rayDir, innerR);

    CloudShellResult res;
    res.hit    = false;
    res.tStart = 0.0;
    res.tEnd   = 0.0;

    if (outerHit.y <= 0.0) return res; // 外球完全在相机后方

    if (camHeight < shellBot)
    {
        // 相机在云层以下：从内球远端出射（=云底入射）到外球远端出射（=云顶出射）
        res.tStart = max(0.0, innerHit.y);
        res.tEnd   = outerHit.y;
    }
    else if (camHeight > shellTop)
    {
        // 相机在云层以上：从外球出射点（向下穿入）到内球入射点（或外球出射）
        if (outerHit.x < 0.0) return res; // 射线背向
        res.tStart = max(0.0, outerHit.x);
        res.tEnd   = (innerHit.x > 0.0) ? innerHit.x : outerHit.y;
    }
    else
    {
        // 相机在云层内部：从相机位置（t=0）到内球（或外球）出射点
        res.tStart = 0.0;
        res.tEnd   = (innerHit.x > 0.0 && innerHit.x > 0.0)
                     ? innerHit.x : outerHit.y;
    }

    res.hit = (res.tEnd > res.tStart) && (res.tEnd > 0.0);
    return res;
}

#endif // CLOUD_COMMON_GLSL
