#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in float inLinearDepth;
layout(location = 2) in vec3 inWorldNormal;
layout(location = 3) flat in int inLodLevel;
layout(location = 4) in vec2 inWorldXZ;

layout(set = 1, binding = 0) uniform WaterSurfaceParams
{
    mat4 waterVPMatrix;
    mat4 waterViewMatrix;
    vec4 waterCameraPosition;
    ivec4 geometryParams;
    vec4 geometryScale;
    vec4 spectrumScale;
    vec4 windAndChop;
    ivec4 simulationParams;
    vec4 waveParticleParams0;
    vec4 waveParticleParams1;
    vec4 waveParticleParams2;
    vec4 waveParticleParams3;
    vec4 flowMapWorld;
    vec4 flowMapParams;
    vec4 flowMapFallback;
} params;

layout(location = 0) out vec4 outWaterNormal;
layout(location = 1) out vec4 outWaterPosDepth;

void main()
{
    vec3 finalNormal = normalize(inWorldNormal);
    // Alpha is an explicit, unfiltered coverage bit.  The optical passes use
    // it together with the depth sentinel to classify shoreline pixels.
    outWaterNormal = vec4(finalNormal, 1.0);
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
