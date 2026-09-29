#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "../Common/CameraData.glsl"
#include "WeatherFrameData.glsl"
#include "RainWaterLighting.glsl"

layout(set = 1, binding = 0) uniform sampler2D surfacePositionSampler;
layout(set = 1, binding = 2) uniform sampler2D rainLayerTextureSampler;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inPositionWS;
layout(location = 0) out vec4 outColor;

vec2 RainLayerUV(vec2 uv, vec2 scale, float fixedAngleRadians, float scrollSpeed)
{
    // The double-cone axis already expresses the configured wind direction in
    // world space. Each layer may cross that base direction by a small fixed
    // angle, but its direction must never oscillate over time.
    float c = cos(fixedAngleRadians);
    float s = sin(fixedAngleRadians);
    vec2 centered = uv - vec2(0.5);
    vec2 rotated = mat2(c, -s, s, c) * centered + vec2(0.5);
    return rotated * scale + vec2(0.0,
        -surfaceWeatherFrame.motion.w * scrollSpeed * surfaceWeatherFrame.surface.z);
}

vec3 RainLayerNormal(vec4 rainData, float fixedAngleRadians)
{
    vec3 normalTS = normalize(rainData.gba * 2.0 - 1.0);
    float c = cos(fixedAngleRadians);
    float s = sin(fixedAngleRadians);
    normalTS.xy = mat2(c, s, -s, c) * normalTS.xy;
    return normalTS;
}

mat3 RainStreakFrame(vec3 viewWS)
{
    vec2 windDirection = normalize(surfaceWeatherFrame.motion.xy);
    vec3 fallDirectionWS = normalize(vec3(
        windDirection.x * surfaceWeatherFrame.motion.z,
        -max(surfaceWeatherFrame.motion.w, 0.1),
        windDirection.y * surfaceWeatherFrame.motion.z));
    vec3 longitudinalWS = -fallDirectionWS;
    vec3 widthWS = cross(longitudinalWS, viewWS);
    if (dot(widthWS, widthWS) < 1.0e-6)
        widthWS = cross(longitudinalWS, vec3(0.0, 0.0, 1.0));
    if (dot(widthWS, widthWS) < 1.0e-6)
        widthWS = cross(longitudinalWS, vec3(1.0, 0.0, 0.0));
    widthWS = normalize(widthWS);
    vec3 faceNormalWS = normalize(cross(widthWS, longitudinalWS));
    if (dot(faceNormalWS, viewWS) < 0.0)
    {
        widthWS = -widthWS;
        faceNormalWS = -faceNormalWS;
    }
    return mat3(widthWS, longitudinalWS, faceNormalWS);
}

void main()
{
    float intensity = clamp(surfaceWeatherFrame.state.y * step(0.5, surfaceWeatherFrame.state.x), 0.0, 1.0);
    if (intensity <= 0.0)
        discard;

    ivec2 extent = textureSize(surfacePositionSampler, 0);
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), extent - 1);
    float sceneDepth = texelFetch(surfacePositionSampler, pixel, 0).w;
    if (sceneDepth <= 0.0)
        sceneDepth = surfaceWeatherFrame.precipitation.x;

    vec4 depthStart = surfaceWeatherFrame.precipitation.x * vec4(0.025, 0.10, 0.28, 0.58);
    vec4 depthRange = surfaceWeatherFrame.precipitation.x * vec4(0.12, 0.24, 0.38, 0.42);
    vec4 mask = clamp((vec4(sceneDepth) - depthStart) / max(depthRange, vec4(0.001)),
        vec4(0.0), vec4(1.0));

    vec4 layerData0 = texture(rainLayerTextureSampler,
        RainLayerUV(inUV, vec2(12.0, 4.0), -0.055, 0.2850));
    vec4 layerData1 = texture(rainLayerTextureSampler,
        RainLayerUV(inUV, vec2(21.0, 6.0), -0.018, 0.2500) + vec2(0.17, 0.31));
    vec4 layerData2 = texture(rainLayerTextureSampler,
        RainLayerUV(inUV, vec2(36.0, 10.0), 0.022, 0.2200) + vec2(0.43, 0.11));
    vec4 layerData3 = texture(rainLayerTextureSampler,
        RainLayerUV(inUV, vec2(60.0, 16.0), 0.050, 0.1800) + vec2(0.71, 0.53));
    vec4 values = vec4(layerData0.r, layerData1.r, layerData2.r, layerData3.r);

    vec4 layerCoverage = max(values * mask, vec4(0.0));
    float rawCoverage = dot(values, mask);
    float rainCoverage = 1.0 - exp(-max(rawCoverage, 0.0));
    float alpha = clamp(rainCoverage * intensity * 0.42, 0.0, 0.78);
    if (alpha < 0.002)
        discard;

    float coverageSum = max(dot(layerCoverage, vec4(1.0)), 1.0e-5);
    float virtualDepth = surfaceWeatherFrame.precipitation.x *
        dot(layerCoverage, vec4(0.08, 0.22, 0.46, 0.74)) / coverageSum;
    vec3 cameraRayWS = normalize(inPositionWS - cameraPosition.xyz);
    vec3 positionWS = cameraPosition.xyz + cameraRayWS * max(virtualDepth, 0.25);
    vec3 viewWS = normalize(cameraPosition.xyz - positionWS);

    // GBA is structural tangent-space normal data, not baked lighting. Blend
    // the four layers with the same coverage weights used for virtual depth.
    vec3 normalTS =
        RainLayerNormal(layerData0, -0.055) * layerCoverage.x +
        RainLayerNormal(layerData1, -0.018) * layerCoverage.y +
        RainLayerNormal(layerData2, 0.022) * layerCoverage.z +
        RainLayerNormal(layerData3, 0.050) * layerCoverage.w;
    normalTS = normalize(normalTS / coverageSum);
    // The double cone is only a texture carrier. Its two hemispheres meet at
    // the horizon, so using its geometric normal creates a visible lighting
    // seam there. Reconstruct the streak normal in one continuous frame from
    // the configured world-space fall direction and the current view instead.
    vec3 normalWS = normalize(RainStreakFrame(viewWS) * normalTS);
    if (dot(normalWS, viewWS) < 0.0)
        normalWS = -normalWS;

    vec3 radiance = RainSampleEnvironment(positionWS, normalWS, viewWS) +
        RainEvaluateDirectLighting(positionWS, normalWS, viewWS);
    outColor = vec4(max(radiance, vec3(0.0)) * alpha, alpha);
}
