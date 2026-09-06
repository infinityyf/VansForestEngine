#ifndef LOCAL_FOG_FIELD_SAMPLING_GLSL
#define LOCAL_FOG_FIELD_SAMPLING_GLSL

#extension GL_EXT_nonuniform_qualifier : require

const uint LOCAL_FOG_FIELD_TEXTURE_DESCRIPTOR_COUNT = 193u;
const uint LOCAL_FOG_FIELD_SAMPLE_HANDLE_COUNT = 194u;

layout(set = 1, binding = 10) uniform sampler2D
    localFogFieldTextures2D[LOCAL_FOG_FIELD_TEXTURE_DESCRIPTOR_COUNT];

struct LocalFogFieldSampleMetadata
{
    uvec4 descriptorChannelsAndKind;
    vec4 resolutionMipAndZeroThreshold;
    vec4 decodeScaleBiasPadding;
};

layout(set = 1, binding = 11, std430) readonly buffer LocalFogFieldSampleMetadataBuffer
{
    LocalFogFieldSampleMetadata samples[];
} uLocalFogFieldSamples;

LocalFogFieldSampleMetadata GetLocalFogFieldSampleMetadata(uint sampleHandle)
{
    return uLocalFogFieldSamples.samples[min(
        sampleHandle, LOCAL_FOG_FIELD_SAMPLE_HANDLE_COUNT - 1u)];
}

vec2 BuildLocalFogLocalXZUv(vec3 localPosition, vec2 tiling, vec2 offset)
{
    return (localPosition.xz + vec2(0.5)) * tiling + offset;
}

float ComputeLocalFogFieldLod(
    uint sampleHandle,
    vec2 localXZFootprintMeters,
    vec2 dimensionsMetersXZ,
    vec2 tiling,
    float lodBias)
{
    LocalFogFieldSampleMetadata metadata =
        GetLocalFogFieldSampleMetadata(sampleHandle);
    vec2 resolution = max(metadata.resolutionMipAndZeroThreshold.xy, vec2(1.0));
    vec2 texelMeters = max(dimensionsMetersXZ, vec2(1.0e-4)) /
        max(resolution * max(tiling, vec2(1.0e-3)), vec2(1.0e-4));
    vec2 footprintRatio = max(localXZFootprintMeters, vec2(1.0e-5)) /
        texelMeters;
    float rho = max(footprintRatio.x, footprintRatio.y);
    float maximumLod = max(metadata.resolutionMipAndZeroThreshold.z - 1.0, 0.0);
    return clamp(log2(max(rho, 1.0)) + lodBias, 0.0, maximumLod);
}

float SampleLocalFogScalar2D(uint sampleHandle, vec2 uv, float lod)
{
    LocalFogFieldSampleMetadata metadata =
        GetLocalFogFieldSampleMetadata(sampleHandle);
    uint descriptorIndex = min(metadata.descriptorChannelsAndKind.x,
        LOCAL_FOG_FIELD_TEXTURE_DESCRIPTOR_COUNT - 1u);
    vec4 sampled = textureLod(
        localFogFieldTextures2D[nonuniformEXT(descriptorIndex)], uv, lod);
    float rawValue = sampled[int(min(metadata.descriptorChannelsAndKind.y, 3u))];
    return rawValue * metadata.decodeScaleBiasPadding.x +
        metadata.decodeScaleBiasPadding.y;
}

vec2 SampleLocalFogPlanarVector2D(uint sampleHandle, vec2 uv, float lod)
{
    LocalFogFieldSampleMetadata metadata =
        GetLocalFogFieldSampleMetadata(sampleHandle);
    uint descriptorIndex = min(metadata.descriptorChannelsAndKind.x,
        LOCAL_FOG_FIELD_TEXTURE_DESCRIPTOR_COUNT - 1u);
    vec4 sampled = textureLod(
        localFogFieldTextures2D[nonuniformEXT(descriptorIndex)], uv, lod);
    vec2 rawValue = vec2(
        sampled[int(min(metadata.descriptorChannelsAndKind.y, 3u))],
        sampled[int(min(metadata.descriptorChannelsAndKind.z, 3u))]);
    return rawValue * metadata.decodeScaleBiasPadding.x +
        metadata.decodeScaleBiasPadding.y;
}

float LocalFogFieldDecodeZeroThreshold(uint sampleHandle)
{
    return GetLocalFogFieldSampleMetadata(sampleHandle).
        resolutionMipAndZeroThreshold.w;
}

#endif
