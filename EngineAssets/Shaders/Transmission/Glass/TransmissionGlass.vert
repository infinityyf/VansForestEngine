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
layout(location = 1) out vec3 positionWS;
layout(location = 2) out vec3 normalWS;
layout(location = 3) out vec3 tangentWS;
layout(location = 4) out vec3 bitangentWS;
layout(location = 5) out vec4 clipPos;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    mat4 model = ModelBuffer.transforms[drawData.transformIndex].ModelMatrix;
    mat4 normalMatrix = ModelBuffer.transforms[drawData.transformIndex].NormalMatrix;

    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangentFrame.xyz;
    surface.bitangent = VansBuildBitangent(normal, tangentFrame.xyz, tangentFrame.w);
    VansApplyVertexDeformation(surface, drawData.vertexFeatureMask);

    vec4 world = model * surface.position;
    positionWS = world.xyz;
    normalWS = normalize(mat3(normalMatrix) * surface.normal);
    tangentWS = normalize(mat3(model) * surface.tangent);
    bitangentWS = normalize(mat3(model) * surface.bitangent);
    fragUV = uv;
    clipPos = VPMatrix * world;
    gl_Position = clipPos;
}
