#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Common/VertexDeformation.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tangentFrame;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 normalWS;
layout(location = 2) out vec3 tangentWS;
layout(location = 3) out vec3 bitangentWS;
layout(location = 4) out vec3 positionWS;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    mat4 modelMatrix = ModelBuffer.transforms[drawData.transformIndex].ModelMatrix;
    mat4 normalMatrix = ModelBuffer.transforms[drawData.transformIndex].NormalMatrix;

    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangentFrame.xyz;
    surface.bitangent = VansBuildBitangent(normal, tangentFrame.xyz, tangentFrame.w);
    VansApplyVertexDeformation(surface, drawData.vertexFeatureMask);

    vec4 worldPos = modelMatrix * surface.position;
    gl_Position = VPMatrix * worldPos;

    mat3 nrm = mat3(normalMatrix);
    normalWS = normalize(nrm * surface.normal);
    tangentWS = normalize(nrm * surface.tangent);
    bitangentWS = normalize(nrm * surface.bitangent);
    positionWS = worldPos.xyz;
    fragUV = uv;
}
