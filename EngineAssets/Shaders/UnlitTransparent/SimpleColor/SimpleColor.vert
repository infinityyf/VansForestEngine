#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 normal;
layout( location = 3 ) in vec3 tangent;
layout( location = 4 ) in vec3 bitangent;

layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout( location = 2 ) out vec3 position_world;

layout( push_constant ) uniform TransparentPushConsts
{
    int objectIndex;
} pushConst;

void main() 
{
    int objectIndex   = pushConst.objectIndex;
    mat4 ModelMatrix  = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 NormalMatrix = ModelBuffer.transforms[objectIndex].NormalMatrix;

    gl_Position    = VPMatrix * ModelMatrix * position;
    normal_ws      = normalize(mat3(NormalMatrix) * normal);
    frag_uv        = uv;
    position_world = (ModelMatrix * position).xyz;
}
