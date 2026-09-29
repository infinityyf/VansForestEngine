#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"
#include "WeatherFrameData.glsl"

layout(set = 1, binding = 0) uniform sampler2D surfacePositionSampler;
layout(set = 1, binding = 1) uniform sampler2D surfaceNormalSampler;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outAge;
layout(location = 2) out float outAlpha;
layout(location = 3) out float outCrownWeight;
layout(location = 4) out vec3 outPositionWS;
layout(location = 5) flat out vec3 outTangentWS;
layout(location = 6) flat out vec3 outBillboardUpWS;

uint RainSplashHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float RainSplashHash01(uint value)
{
    return float(RainSplashHash(value) & 0x00ffffffu) / 16777215.0;
}

bool RainSplashHasCover(vec3 surfacePosition, vec3 rainDirection)
{
    for (int sampleIndex = 1; sampleIndex <= 4; ++sampleIndex)
    {
        vec3 rainOrigin = surfacePosition - rainDirection * (float(sampleIndex) * 1.5);
        vec4 clip = VPMatrix * vec4(rainOrigin, 1.0);
        if (clip.w <= 0.0)
            continue;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
            continue;
        ivec2 size = textureSize(surfacePositionSampler, 0);
        ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
        vec4 blocker = texelFetch(surfacePositionSampler, pixel, 0);
        float rayDepth = -(ViewMatrix * vec4(rainOrigin, 1.0)).z;
        if (blocker.w > 0.0 && blocker.w < rayDepth - 0.08 &&
            blocker.y > surfacePosition.y + 0.20)
            return true;
    }
    return false;
}

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
        vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));
    uint id = uint(gl_InstanceIndex);
    float lifetime = max(surfaceWeatherFrame.precipitation.y, 0.05);
    float phase = RainSplashHash01(id * 23u + 7u);
    float continuousCycle = surfaceWeatherFrame.surface.z / lifetime + phase;
    uint cycle = uint(max(floor(continuousCycle), 0.0));
    float age = fract(continuousCycle);
    uint seed = RainSplashHash(id * 131u + cycle * 977u + 17u);
    ivec2 textureExtent = textureSize(surfacePositionSampler, 0);
    vec2 randomUV = vec2(
        RainSplashHash01(seed),
        RainSplashHash01(seed ^ 0x9e3779b9u));
    ivec2 pixel = clamp(ivec2(randomUV * vec2(textureExtent)),
        ivec2(0), textureExtent - 1);
    vec4 surfacePositionDepth = texelFetch(surfacePositionSampler, pixel, 0);
    vec3 encodedNormal = texelFetch(surfaceNormalSampler, pixel, 0).xyz;
    float normalLengthSquared = dot(encodedNormal, encodedNormal);
    vec3 normal = normalLengthSquared > 1.0e-6
        ? encodedNormal * inversesqrt(normalLengthSquared)
        : vec3(0.0, 1.0, 0.0);
    float intensity = clamp(surfaceWeatherFrame.state.y * step(0.5, surfaceWeatherFrame.state.x), 0.0, 1.0);
    vec2 windDirection = normalize(surfaceWeatherFrame.motion.xy);
    vec3 rainDirection = normalize(vec3(
        windDirection.x * surfaceWeatherFrame.motion.z,
        -max(surfaceWeatherFrame.motion.w, 0.1),
        windDirection.y * surfaceWeatherFrame.motion.z));
    float viewDistance = distance(cameraPosition.xyz, surfacePositionDepth.xyz);
    bool isActive = RainSplashHash01(seed ^ 0x68bc21ebu) <= intensity &&
        surfacePositionDepth.w > 0.0 && normalLengthSquared > 1.0e-6 &&
        normal.y > 0.72 &&
        viewDistance < surfaceWeatherFrame.precipitation.x &&
        !RainSplashHasCover(surfacePositionDepth.xyz, rainDirection);

    // 球面 billboard 在俯视时仍正对相机。旧的圆柱 billboard 始终以地表
    // 法线作为竖轴，观察高度增加后会被压成横线，并错误缩短贴图水花。
    vec3 viewDirection = normalize(cameraPosition.xyz - surfacePositionDepth.xyz);
    vec3 tangent = cross(normal, viewDirection);
    if (dot(tangent, tangent) <= 1.0e-6)
    {
        vec3 fallbackAxis = abs(normal.y) < 0.999
            ? vec3(0.0, 1.0, 0.0)
            : vec3(1.0, 0.0, 0.0);
        tangent = cross(normal, fallbackAxis);
    }
    tangent = normalize(tangent);
    vec3 billboardUp = normalize(cross(viewDirection, tangent));
    vec2 corner = corners[gl_VertexIndex];
    float normalizedHeight = corner.y * 0.5 + 0.5;
    float lifeScale = sin(clamp(age, 0.0, 1.0) * 3.14159265359);
    float radius = max(surfaceWeatherFrame.precipitation.z, 0.01) * mix(0.72, 1.08, lifeScale);
    float height = max(surfaceWeatherFrame.precipitation.z, 0.01) * 1.65;
    vec3 worldPosition = surfacePositionDepth.xyz + normal * 0.006 +
        tangent * corner.x * radius + billboardUp * normalizedHeight * height;
    gl_Position = isActive
        ? VPMatrix * vec4(worldPosition, 1.0)
        : vec4(2.0, 2.0, 2.0, 1.0);
    outUV = corner * 0.5 + 0.5;
    outAge = age;
    // 雨量已经通过 isActive 控制水花数量；结构覆盖率不应再次乘雨量。
    outAlpha = isActive ? 1.0 : 0.0;
    outCrownWeight = smoothstep(0.04, 0.28,
        surfaceWeatherFrame.state.z + surfaceWeatherFrame.state.w);
    outPositionWS = worldPosition;
    outTangentWS = tangent;
    outBillboardUpWS = billboardUp;
}
