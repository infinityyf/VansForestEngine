#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"
#include "../../Common/DrawPushConstants.glsl"
#include "../../Common/VertexDeformation.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 normal;
layout( location = 3 ) in vec3 tangent;
layout( location = 4 ) in vec3 bitangent;

layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout( location = 2 ) out vec3 tangent_ws;
layout( location = 3 ) out vec3 bitangent_ws;
layout( location = 4 ) out vec3 position_world;

void main() 
{
    int objectIndex = materialConst.objectIndex;
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 NormalMatrix = ModelBuffer.transforms[objectIndex].NormalMatrix;

    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangent;
    surface.bitangent = bitangent;
    VansApplyVertexDeformation(surface, materialConst.vertexFeatureMask);

    gl_Position = VPMatrix * ModelMatrix * surface.position;
    mat3 normalMatrix = mat3(NormalMatrix);
    normal_ws    = normalize(normalMatrix * surface.normal);
    tangent_ws   = normalize(normalMatrix * surface.tangent);
    bitangent_ws = normalize(normalMatrix * surface.bitangent);

    frag_uv= uv;
    position_world = (ModelMatrix * surface.position).xyz;
}
