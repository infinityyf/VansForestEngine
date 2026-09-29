#ifndef WEATHER_FRAME_DATA_GLSL
#define WEATHER_FRAME_DATA_GLSL

layout(set = 0, binding = 41, std140) uniform SurfaceWeatherFrameData
{
    vec4 state; // x=enabled, y=intensity, z=filmWetness, w=puddleFill
    vec4 motion; // xy=wind direction, z=wind speed, w=fall speed
    vec4 precipitation; // x=max distance, y=splash lifetime, z=splash radius
    vec4 surface; // x=ripple scale, y=ripple strength, z=weather time
    vec4 puddleField0; // x=scale, y=detail, z=threshold, w=softness
    vec4 puddleField1; // x=strength, y=seed
    vec4 groundResponse; // x=wet albedo, y=wet roughness, z=puddle roughness, w=puddle F0
} surfaceWeatherFrame;

#endif
