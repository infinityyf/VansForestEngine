#version 450

// 顶点缓冲仍为半精度；固定功能读取转换为 float，无需 StorageInputOutput16。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangentFrame;

layout(set = 1, binding = 0) uniform CaptureCamera
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 position;
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
    vec3 tangent = vec3(inTangentFrame.xyz);
    vec3 bitangent = cross(vec3(inNormal), tangent) * (float(inTangentFrame.w) < 0.0 ? -1.0 : 1.0);
    worldTangent = normalize(mat3(drawData.model) * tangent);
    worldBitangent = normalize(mat3(drawData.model) * bitangent);
    fragUV = vec2(inUV);
    gl_Position = captureCamera.viewProjection * world;
}
