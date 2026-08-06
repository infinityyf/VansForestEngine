#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"
#include "../../Common/DrawPushConstants.glsl"
#include "../../Common/VertexDeformation.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec3 normal_ws;
layout(location = 2) out vec3 tangent_ws;
layout(location = 3) out vec3 bitangent_ws;
layout(location = 4) out vec3 position_world;

void main()
{
    int objectIndex = materialConst.objectIndex;
    mat4 modelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 normalMatrix4 = ModelBuffer.transforms[objectIndex].NormalMatrix;

    VansVertexSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.tangent = tangent;
    surface.bitangent = bitangent;
    VansApplyVertexDeformation(surface, materialConst.vertexFeatureMask);

    gl_Position = VPMatrix * modelMatrix * surface.position;

    mat3 normalMatrix = mat3(normalMatrix4);
    vec3 n_ws = normalMatrix * surface.normal;
    vec3 t_ws = normalMatrix * surface.tangent;
    vec3 bt_ws = normalMatrix * surface.bitangent;
    float nl = dot(n_ws, n_ws);
    float tl = dot(t_ws, t_ws);
    float bl = dot(bt_ws, bt_ws);
    normal_ws = nl > 1e-8 ? n_ws * inversesqrt(nl) : vec3(0.0, 0.0, 1.0);
    tangent_ws = tl > 1e-8 ? t_ws * inversesqrt(tl) : vec3(1.0, 0.0, 0.0);
    bitangent_ws = bl > 1e-8 ? bt_ws * inversesqrt(bl) : cross(normal_ws, tangent_ws);

    frag_uv = uv;
    position_world = (modelMatrix * surface.position).xyz;
}
