#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 normal;
layout( location = 3 ) in vec3 tangent;
layout( location = 4 ) in vec3 bitangent;

// 插值到 fragment shader 的世界空间数据
layout( location = 0 ) out vec2  frag_uv;
layout( location = 1 ) out vec3  normal_ws;
layout( location = 2 ) out vec3  tangent_ws;
layout( location = 3 ) out vec3  bitangent_ws;
layout( location = 4 ) out vec3  position_world;
// 贴花专用：OBB 局部坐标（用于 fragment shader 中的越界测试和 UV 推导）
layout( location = 5 ) out vec3  position_local;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

void main()
{
    int objectIndex = materialConst.objectIndex;
    mat4 ModelMatrix  = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 NormalMatrix = ModelBuffer.transforms[objectIndex].NormalMatrix;

    // 将顶点变换到世界空间，再投影到裁剪空间
    vec4 worldPos = ModelMatrix * position;
    gl_Position   = VPMatrix * worldPos;

    mat3 normalMatrix = mat3(NormalMatrix);

    // 安全 normalize：防止零向量产生 NaN
    vec3 n_ws  = normalMatrix * normal;
    vec3 t_ws  = normalMatrix * tangent;
    vec3 bt_ws = normalMatrix * bitangent;

    float nl = dot(n_ws,  n_ws);
    float tl = dot(t_ws,  t_ws);
    float bl = dot(bt_ws, bt_ws);
    normal_ws    = nl  > 1e-8 ? n_ws  * inversesqrt(nl)  : vec3(0.0, 1.0, 0.0);
    tangent_ws   = tl  > 1e-8 ? t_ws  * inversesqrt(tl)  : vec3(1.0, 0.0, 0.0);
    bitangent_ws = bl  > 1e-8 ? bt_ws * inversesqrt(bl)  : vec3(0.0, 0.0, 1.0);

    position_world = worldPos.xyz;

    // OBB 局部坐标：UV 的 [0,1] 正好对应模型空间的 [-0.5, 0.5]（标准化立方体）
    // 使用原始 position.xyz（模型空间）直接作为局部坐标
    position_local = position.xyz;

    frag_uv = uv;
}
