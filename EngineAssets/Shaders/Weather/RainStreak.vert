#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"
#include "WeatherFrameData.glsl"

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outPositionWS;

void main()
{
    const int segmentCount = 64;
    int segment = gl_VertexIndex / 6;
    int localVertex = gl_VertexIndex % 6;
    float u0 = float(segment) / float(segmentCount);
    float u1 = float(segment + 1) / float(segmentCount);
    float a0 = u0 * 6.28318530718;
    float a1 = u1 * 6.28318530718;

    float radius = max(surfaceWeatherFrame.precipitation.x, 1.0);
    float halfHeight = max(radius * 0.72, 8.0);
    vec2 windDirection = normalize(surfaceWeatherFrame.motion.xy);
    vec3 axisTilt = vec3(windDirection.x, 0.0, windDirection.y) *
        (surfaceWeatherFrame.motion.z / max(surfaceWeatherFrame.motion.w, 0.1)) * halfHeight;
    vec3 top = axisTilt + vec3(0.0, halfHeight, 0.0);
    vec3 bottom = -axisTilt + vec3(0.0, -halfHeight, 0.0);
    vec3 ring0 = vec3(cos(a0) * radius, 0.0, sin(a0) * radius);
    vec3 ring1 = vec3(cos(a1) * radius, 0.0, sin(a1) * radius);

    vec3 localPosition;
    if (localVertex == 0) { localPosition = top; outUV = vec2((u0 + u1) * 0.5, 0.0); }
    else if (localVertex == 1) { localPosition = ring0; outUV = vec2(u0, 0.5); }
    else if (localVertex == 2) { localPosition = ring1; outUV = vec2(u1, 0.5); }
    else if (localVertex == 3) { localPosition = bottom; outUV = vec2((u0 + u1) * 0.5, 1.0); }
    else if (localVertex == 4) { localPosition = ring1; outUV = vec2(u1, 0.5); }
    else { localPosition = ring0; outUV = vec2(u0, 0.5); }

    outPositionWS = cameraPosition.xyz + localPosition;
    gl_Position = VPMatrix * vec4(outPositionWS, 1.0);
}
