#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"
#include "../../Common/AnimationSkinning.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 normalWS;
layout(location = 2) out vec3 tangentWS;
layout(location = 3) out vec3 bitangentWS;
layout(location = 4) out vec3 positionWS;

layout(push_constant) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
    int passUser0;
} materialConst;

void main()
{
    mat4 modelMatrix = ModelBuffer.transforms[materialConst.objectIndex].ModelMatrix;
    mat4 normalMatrix = ModelBuffer.transforms[materialConst.objectIndex].NormalMatrix;

    vec4 skinnedPosition = position;
    vec3 skinnedNormal = normal;
    vec3 skinnedTangent = tangent;
    vec3 skinnedBitangent = bitangent;
    if (materialConst.animationEnabled != 0)
        VansApplyAnimationSkinning(skinnedPosition, skinnedNormal, skinnedTangent, skinnedBitangent);

    vec4 worldPos = modelMatrix * skinnedPosition;
    gl_Position = VPMatrix * worldPos;

    mat3 nrm = mat3(normalMatrix);
    normalWS = normalize(nrm * skinnedNormal);
    tangentWS = normalize(nrm * skinnedTangent);
    bitangentWS = normalize(nrm * skinnedBitangent);
    positionWS = worldPos.xyz;
    fragUV = uv;
}
