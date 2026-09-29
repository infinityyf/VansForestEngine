#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "../Common/CameraData.glsl"
#include "RainWaterLighting.glsl"

layout(set = 1, binding = 3) uniform sampler2D rainSplashAtlasSampler;

layout(location = 0) in vec2 inUV;
layout(location = 1) in float inAge;
layout(location = 2) in float inAlpha;
layout(location = 3) in float inCrownWeight;
layout(location = 4) in vec3 inPositionWS;
layout(location = 5) flat in vec3 inTangentWS;
layout(location = 6) flat in vec3 inBillboardUpWS;
layout(location = 0) out vec4 outColor;

void main()
{
    int frame = min(int(floor(clamp(inAge, 0.0, 0.9999) * 16.0)), 15);
    vec2 frameUV = vec2(inUV.x, 1.0 - inUV.y);
    vec2 frameExtent = vec2(textureSize(rainSplashAtlasSampler, 0)) * 0.25;
    vec2 frameHalfTexel = 0.5 / frameExtent;
    vec2 safeFrameUV = clamp(frameUV, frameHalfTexel, vec2(1.0) - frameHalfTexel);
    vec2 atlasUV = (safeFrameUV + vec2(float(frame % 4), float(frame / 4))) * 0.25;
    vec4 splashData = texture(rainSplashAtlasSampler, atlasUV);

    // Atlas RGB 是结构切线空间法线，A 是 coverage；不包含摄影高光、环境色或曝光。
    float waterResponse = mix(0.28, 1.0, clamp(inCrownWeight, 0.0, 1.0));
    float coverage = splashData.a * inAlpha * waterResponse;
    if (coverage < 0.004)
        discard;

    vec3 viewWS = normalize(cameraPosition.xyz - inPositionWS);
    vec3 tangentWS = normalize(inTangentWS);
    vec3 billboardUpWS = normalize(inBillboardUpWS);
    vec3 carrierNormalWS = normalize(cross(tangentWS, billboardUpWS));
    if (dot(carrierNormalWS, viewWS) < 0.0)
        carrierNormalWS = -carrierNormalWS;
    vec3 normalTS = normalize(splashData.rgb * 2.0 - 1.0);
    vec3 normalWS = normalize(mat3(
        tangentWS, billboardUpWS, carrierNormalWS) * normalTS);
    if (dot(normalWS, viewWS) < 0.0)
        normalWS = -normalWS;

    // 水花没有可用的逐像素 SceneColor 折射输入，因此只能把运行时求得的水面
    // 反射和直接镜面光作为 additive radiance 合入。coverage 只调制该辐射，绝不
    // 衰减目标颜色；暗处水花应不可见，而不是把 Atlas 轮廓画成黑色剪影。
    vec3 radiance = RainSampleReflection(inPositionWS, normalWS, viewWS) +
        RainEvaluateDirectLighting(inPositionWS, normalWS, viewWS);
    outColor = vec4(max(radiance, vec3(0.0)) * coverage, 0.0);
}
