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
    vec4 microSlopeParams;
    vec4 microDomainParams;
} params;
layout(set = 1, binding = 3) uniform sampler2DArray microSlopeMap;

layout(location = 0) out vec4 outWaterNormal;
layout(location = 1) out vec4 outWaterPosDepth;

vec2 RotatePosition(vec2 p, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// If q = R * worldXZ, gradients transform back with R^T.
vec2 RotateGradientToWorld(vec2 gradient, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * gradient.x + s * gradient.y,
               -s * gradient.x + c * gradient.y);
}

vec2 SampleSlopeField(int layer, float coverage, float angle)
{
    vec2 spectralXZ = RotatePosition(inWorldXZ, angle);
    vec2 localGradient = texture(microSlopeMap,
        vec3(spectralXZ / coverage, float(layer))).xy;
    return RotateGradientToWorld(localGradient, angle);
}

vec3 ApplyMicroSlope(vec3 macroNormal)
{
    if (params.simulationParams.z == 0 || params.microSlopeParams.x <= 0.0)
        return normalize(macroNormal);

    float pixelFootprint = max(length(dFdx(inWorldXZ)), length(dFdy(inWorldXZ)));
    float bandMinimums[2] = float[2](params.microSlopeParams.y, params.microSlopeParams.z);
    float primaryCoverage = params.microDomainParams.x;
    float secondaryCoverage = params.microDomainParams.y;
    float decorrelationAngle = params.microDomainParams.z;

    vec2 heightGradient = vec2(0.0);
    const float pairVarianceNormalization = 0.70710678118;
    for (int band = 0; band < 2; ++band)
    {
        float nyquistWeight = smoothstep(
            2.0 * pixelFootprint,
            4.0 * pixelFootprint,
            bandMinimums[band]);
        int firstLayer = band * 2;
        vec2 pairedGradient =
            SampleSlopeField(firstLayer, primaryCoverage, decorrelationAngle)
          + SampleSlopeField(firstLayer + 1, secondaryCoverage, -decorrelationAngle);
        heightGradient += pairedGradient * (pairVarianceNormalization * nyquistWeight);
    }

    vec2 normalSlope = -heightGradient * params.microSlopeParams.x;
    vec3 detailTS = normalize(vec3(normalSlope.x, 1.0, normalSlope.y));

    vec3 N = normalize(macroNormal);
    vec3 tangent = normalize(abs(N.y) > 0.05 ? vec3(1.0, -N.x / N.y, 0.0) : vec3(1.0, 0.0, 0.0));
    // T x N points along world +Z for a flat water surface.  The previous
    // N x T order inverted the spectral Z slope.
    vec3 bitangent = normalize(cross(tangent, N));
    return normalize(tangent * detailTS.x + N * detailTS.y + bitangent * detailTS.z);
}

void main()
{
    vec3 finalNormal = ApplyMicroSlope(inWorldNormal);
    // Alpha is an explicit, unfiltered coverage bit.  The optical passes use
    // it together with the depth sentinel to classify shoreline pixels.
    outWaterNormal = vec4(finalNormal, 1.0);
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
