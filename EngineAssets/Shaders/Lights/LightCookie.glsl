#ifndef VANS_LIGHT_COOKIE_GLSL
#define VANS_LIGHT_COOKIE_GLSL
#extension GL_EXT_nonuniform_qualifier : require
#include "../Common/CameraData.glsl"
struct LightCookieData
{
    mat4 worldToLight;
    vec4 scaleOffset;
    vec4 projection;
    vec4 options;
};
layout(set=0, binding=38) uniform sampler2D lightCookieTextures[161];
layout(set=0, binding=39, std430) readonly buffer LightCookieBuffer
{
    LightCookieData lightCookies[161];
};

float SampleSurfaceLightCookie(int slot, vec3 positionWS)
{
    LightCookieData c = lightCookies[slot];
    if (c.options.x <= 0.0) return 1.0;
    vec3 q = (c.worldToLight * vec4(positionWS, 1.0)).xyz;
    int kind = int(c.options.y);
    vec2 uv;
    if (kind == 1)
    {
        // 全景图：中心朝本地 -Z，上方为 +Y，横向环绕。
        vec3 d = q / max(length(q), 1e-6);
        uv = vec2(atan(d.x, -d.z) / 6.28318530718 + 0.5, acos(clamp(d.y,-1.0,1.0)) / 3.14159265359);
    }
    else
    {
        if (kind == 2 && q.z >= -1e-5) return 1.0 - c.options.x;
        uv = q.xy / c.projection.xy;
        if (kind == 2) uv /= -q.z;
        uv.y = -uv.y;
        uv += 0.5;
    }
    vec2 centered = (uv - 0.5) * c.scaleOffset.xy;
    uv = vec2(c.projection.z * centered.x - c.projection.w * centered.y,
              c.projection.w * centered.x + c.projection.z * centered.y) + 0.5 + c.scaleOffset.zw;
    // 使用世界像素足迹估算显式 LOD，Tile 灯列表和材质分支内不调用屏幕导数。
    float viewDepth = abs((ViewMatrix * vec4(positionWS,1)).z);
    float pixelMeters = 2.0 * viewDepth / max(abs(ProjectionMatrix[1][1]) * ScreenParams.y, 1.0);
    vec2 footprint = vec2(pixelMeters) / c.projection.xy;
    if (kind == 2) footprint /= max(-q.z, 1e-5);
    if (kind == 1) { footprint = vec2(pixelMeters / max(length(q),1e-5) / 3.14159265); uv.x = fract(uv.x); }
    vec2 resolution = vec2(textureSize(lightCookieTextures[nonuniformEXT(slot)],0));
    float lod = log2(max(max(footprint.x * abs(c.scaleOffset.x) * resolution.x,
                            footprint.y * abs(c.scaleOffset.y) * resolution.y), 1.0));
    if (c.options.z > 0.5) uv = fract(uv);
    else if (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) return 1.0 - c.options.x;
    vec2 halfTexel = 0.5 / vec2(textureSize(lightCookieTextures[nonuniformEXT(slot)],0));
    uv = clamp(uv, halfTexel, 1.0-halfTexel);
    vec4 texel = textureLod(lightCookieTextures[nonuniformEXT(slot)], uv, lod);
    float mask = c.options.w > 0.5 ? texel.a : texel.r;
    return mix(1.0, clamp(mask,0.0,1.0), c.options.x);
}
#endif
