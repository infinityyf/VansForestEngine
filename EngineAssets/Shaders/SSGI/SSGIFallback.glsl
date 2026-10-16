#ifndef SSGI_FALLBACK_GLSL
#define SSGI_FALLBACK_GLSL

// Cache 与全分辨率 miss 共用相同的 E/pi 查询；几何偏移和材质积分方向独立传递。
// 这里保留现有薄叶正背面混合，方向辐亮度与屏幕条件采样的限制另行处理。
vec3 SSGI_EvaluateFallback(vec3 worldPosition, GIReceiverNormals normals, float vegetationTranslucency)
{
    vec3 front = GI_BlendRegionLighting(worldPosition, normals, 1.0);
    if (vegetationTranslucency <= 0.001) return front;
    vec3 back = GI_BlendRegionLighting(worldPosition,
        GIReceiverNormals(-normals.geometric, -normals.shading), 1.0);
    return mix(front, max(front, back), clamp(vegetationTranslucency * 0.7, 0.0, 0.7));
}

#endif
