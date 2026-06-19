#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout(location = 0) in f16vec3 inPosition;
layout(location = 1) in f16vec2 inUV;
layout(location = 2) in f16vec3 inNormal;
layout(location = 3) in f16vec3 inTangent;
layout(location = 4) in f16vec3 inBitangent;

layout(set = 1, binding = 0) uniform CaptureCamera
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 position;
    vec4 giVolumeMin;
    vec4 giVolumeSizeAndBias;
} captureCamera;

layout(push_constant) uniform CaptureDraw
{
    mat4 model;
    vec4 albedo;
    vec4 emissive;
    vec4 params;
} drawData;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 worldTangent;
layout(location = 4) out vec3 worldBitangent;

void main()
{
    vec4 world = drawData.model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(drawData.model)));
    worldPosition = world.xyz;
    worldNormal = normalize(normalMatrix * vec3(inNormal));
    worldTangent = normalize(mat3(drawData.model) * vec3(inTangent));
    worldBitangent = normalize(mat3(drawData.model) * vec3(inBitangent));
    fragUV = vec2(inUV);
    gl_Position = captureCamera.viewProjection * world;
}
