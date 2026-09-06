#ifndef VANS_PARTICIPATING_MEDIA_COMMON_GLSL
#define VANS_PARTICIPATING_MEDIA_COMMON_GLSL

// 不同采样结构最终都转换为单位长度源项与 RGB 消光。区间传输统一服从
// Beer-Lambert 积分，不能由介质类型或颜色 Alpha 决定合成顺序。
struct ParticipatingMediaSample
{
    vec3 sourcePerMeter;
    vec3 extinctionPerMeter;
};

ParticipatingMediaSample EmptyParticipatingMediaSample()
{
    ParticipatingMediaSample sampleValue;
    sampleValue.sourcePerMeter = vec3(0.0);
    sampleValue.extinctionPerMeter = vec3(0.0);
    return sampleValue;
}

void AddParticipatingMediaSample(inout ParticipatingMediaSample destination,
    vec3 sourcePerMeter, vec3 extinctionPerMeter)
{
    destination.sourcePerMeter += max(sourcePerMeter, vec3(0.0));
    destination.extinctionPerMeter += max(extinctionPerMeter, vec3(0.0));
}

vec3 ParticipatingMediaSegmentScattering(
    ParticipatingMediaSample medium, float segmentLengthMeters)
{
    float lengthMeters = max(segmentLengthMeters, 0.0);
    vec3 extinction = max(medium.extinctionPerMeter, vec3(0.0));
    vec3 segmentOpticalDepth = extinction * lengthMeters;
    vec3 segmentTransmittance = exp(-min(segmentOpticalDepth, vec3(80.0)));
    vec3 integratedLength;
    for (int channel = 0; channel < 3; ++channel)
    {
        integratedLength[channel] = extinction[channel] > 1.0e-8
            ? (1.0 - segmentTransmittance[channel]) / extinction[channel]
            : lengthMeters;
    }
    return max(medium.sourcePerMeter, vec3(0.0)) * integratedLength;
}

void IntegrateParticipatingMediaStep(
    ParticipatingMediaSample medium,
    float segmentLengthMeters,
    inout vec3 accumulatedScattering,
    inout vec3 accumulatedOpticalDepth)
{
    vec3 accumulatedTransmittance =
        exp(-min(accumulatedOpticalDepth, vec3(80.0)));
    accumulatedScattering += accumulatedTransmittance *
        ParticipatingMediaSegmentScattering(medium, segmentLengthMeters);
    accumulatedOpticalDepth += max(medium.extinctionPerMeter, vec3(0.0)) *
        max(segmentLengthMeters, 0.0);
}

void ComposeParticipatingMediaInterval(
    inout vec3 accumulatedScattering,
    inout vec3 accumulatedOpticalDepth,
    vec3 intervalScattering,
    vec3 intervalOpticalDepth)
{
    accumulatedScattering +=
        exp(-min(accumulatedOpticalDepth, vec3(80.0))) *
        max(intervalScattering, vec3(0.0));
    accumulatedOpticalDepth += max(intervalOpticalDepth, vec3(0.0));
}

float ParticipatingMediaFroxelSliceCoordinate(
    float viewDepthMeters,
    float nearViewDepthMeters,
    float farViewDepthMeters,
    float slicePower)
{
    float normalizedDistance = clamp(
        (viewDepthMeters - nearViewDepthMeters) /
        max(farViewDepthMeters - nearViewDepthMeters, 1.0e-5),
        0.0, 1.0);
    return pow(normalizedDistance, 1.0 / max(slicePower, 1.0e-4));
}

// RGB 是局部雾 Froxel 的单位长度源项，A 是标量消光。距离范围外返回
// 真空，不能依赖采样器 clamp 把边界介质延伸到远处。
vec4 SampleParticipatingMediaFroxel(
    sampler3D froxelTexture,
    vec2 screenUv,
    float viewDepthMeters,
    vec4 lightingAndDepthRange,
    vec4 gridAndSliceParameters)
{
    float nearViewDepth = lightingAndDepthRange.z;
    float farViewDepth = lightingAndDepthRange.w;
    if (viewDepthMeters < nearViewDepth || viewDepthMeters > farViewDepth)
        return vec4(0.0);
    float slice = ParticipatingMediaFroxelSliceCoordinate(
        viewDepthMeters, nearViewDepth, farViewDepth,
        gridAndSliceParameters.z);
    return texture(froxelTexture,
        vec3(clamp(screenUv, vec2(0.0), vec2(1.0)), slice));
}

#endif
