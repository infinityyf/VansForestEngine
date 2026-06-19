#version 450

layout(location = 0) in vec2 ndcPosition;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 5) uniform samplerCube PreConvSpecularEnvironment;

layout(set = 1, binding = 0) uniform CaptureCamera
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 position;
} captureCamera;

void main()
{
    vec4 world = captureCamera.inverseViewProjection * vec4(ndcPosition, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / max(world.w, 1e-5) - captureCamera.position.xyz);
    vec3 sky = textureLod(PreConvSpecularEnvironment, direction, 0.0).rgb;
    outColor = vec4(max(sky, vec3(0.0)), 1.0);
}
