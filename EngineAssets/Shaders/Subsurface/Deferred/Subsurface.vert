#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Common/VertexDeformation.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 normal;
layout( location = 3 ) in vec4 tangentFrame;

layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout( location = 2 ) out vec3 tangent_ws;
layout( location = 3 ) out vec3 bitangent_ws;
layout( location = 4 ) out vec3 position_world;
layout( location = 5 ) out vec4 motion_current_clip;
layout( location = 6 ) out vec4 motion_previous_clip;

void main() 
{
    VansDrawData drawData = VansGetDrawData();
    int objectIndex = drawData.transformIndex;
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 PreviousModelMatrix = ModelBuffer.transforms[objectIndex].PrevModelMatrix;
    mat4 NormalMatrix = ModelBuffer.transforms[objectIndex].NormalMatrix;

    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangentFrame.xyz;
    surface.bitangent = VansBuildBitangent(normal, tangentFrame.xyz, tangentFrame.w);
    VansApplyVertexDeformation(surface, drawData.vertexFeatureMask);

    vec4 previousLocalPosition = position;
    VansApplyPreviousVertexPositionDeformation(previousLocalPosition, drawData.vertexFeatureMask);
    vec4 worldPosition = ModelMatrix * surface.position;
    gl_Position = VPMatrix * worldPosition;
    motion_current_clip = UnjitteredVPMatrix * worldPosition;
    motion_previous_clip = LastUnjitteredVPMatrix * PreviousModelMatrix * previousLocalPosition;
    mat3 normalMatrix = mat3(NormalMatrix);
    normal_ws    = normalize(normalMatrix * surface.normal);
    tangent_ws   = normalize(normalMatrix * surface.tangent);
    bitangent_ws = normalize(normalMatrix * surface.bitangent);

    frag_uv = uv;
    position_world = worldPosition.xyz;
}
