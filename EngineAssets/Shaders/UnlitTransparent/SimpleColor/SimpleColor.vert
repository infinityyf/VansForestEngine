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
layout( location = 2 ) out vec3 position_world;

void main() 
{
    VansDrawData drawData = VansGetDrawData();
    int objectIndex   = drawData.transformIndex;
    mat4 ModelMatrix  = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 NormalMatrix = ModelBuffer.transforms[objectIndex].NormalMatrix;

    // 透明镜片和其他蒙皮子网格使用与不透明部分相同的骨骼变形。
    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangentFrame.xyz;
    surface.bitangent = VansBuildBitangent(normal, tangentFrame.xyz, tangentFrame.w);
    VansApplyVertexDeformation(surface, drawData.vertexFeatureMask);

    gl_Position    = VPMatrix * ModelMatrix * surface.position;
    normal_ws      = normalize(mat3(NormalMatrix) * surface.normal);
    frag_uv        = uv;
    position_world = (ModelMatrix * surface.position).xyz;
}
