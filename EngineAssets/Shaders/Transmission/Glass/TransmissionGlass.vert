#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"
#include "../../Common/VertexDeformation.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 positionWS;
layout(location = 2) out vec3 normalWS;
layout(location = 3) out vec3 tangentWS;
layout(location = 4) out vec3 bitangentWS;
layout(location = 5) out vec4 clipPos;

layout(push_constant) uniform DrawPushConsts
{
    int materialIndex;
    int transformIndex;
    uint vertexFeatureMask;
    int passUser0;
} pc;

void main()
{
    mat4 model = ModelBuffer.transforms[pc.transformIndex].ModelMatrix;
    mat4 normalMatrix = ModelBuffer.transforms[pc.transformIndex].NormalMatrix;

    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangent;
    surface.bitangent = bitangent;
    VansApplyVertexDeformation(surface, pc.vertexFeatureMask);

    vec4 world = model * surface.position;
    positionWS = world.xyz;
    normalWS = normalize(mat3(normalMatrix) * surface.normal);
    tangentWS = normalize(mat3(model) * surface.tangent);
    bitangentWS = normalize(mat3(model) * surface.bitangent);
    fragUV = uv;
    clipPos = VPMatrix * world;
    gl_Position = clipPos;
}
