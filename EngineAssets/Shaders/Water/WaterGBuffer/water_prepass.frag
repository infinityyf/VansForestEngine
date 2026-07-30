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
    vec4 surfaceOptics;
    vec4 scatteringCoeff;
    vec4 absorptionCoeff;
} params;

layout(location = 0) out vec4 outWaterNormal;
layout(location = 1) out vec4 outWaterScatterThickness;
layout(location = 2) out vec4 outWaterAbsorptionFoam;
layout(location = 3) out vec4 outWaterPosDepth;

void main()
{
    vec3 finalNormal = normalize(inWorldNormal);
    float roughness = clamp(params.surfaceOptics.x, 0.002, 0.3);
    float foam = 0.0;
    outWaterNormal = vec4(finalNormal, roughness);
    outWaterScatterThickness = vec4(max(params.scatteringCoeff.rgb, vec3(0.0)), 0.0);
    outWaterAbsorptionFoam = vec4(max(params.absorptionCoeff.rgb, vec3(0.0)), foam);
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
