#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

//用于性能优化，提示编译器，不同线程组会访问不同索引的资源
#extension GL_EXT_nonuniform_qualifier : require

//取消padding
#extension GL_EXT_scalar_block_layout : require

#include "../Common/Common.glsl"

//vertex data
struct Vertex 
{
    vec4 position;
    vec2 uv;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
};

layout(location = 0) rayPayloadInEXT RayTracePayload prd;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 3, std430, scalar) buffer VertexBuffers {
    Vertex vertices[];
} vertexBuffers[];

layout(set = 0, binding = 4, std430) buffer IndexBuffers {
    uint indices[];
} indexBuffers[];

layout(set = 0, binding = 5, std430) buffer InstanceDataBuffer {
    uint instances[];
} instanceData;

layout(set = 0, binding = 7, std430) buffer InstanceToTextureIndexBuffer {
    uint indexs[];
} textureIndexData;


layout(set = 0, binding = 50) uniform sampler2D PBRTextures[]; 
#define ALBEDO_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_INDEX 2
#define ROUGHNESS_INDEX 3
#define AO_INDEX 4

void main()
{
    uint instanceID = gl_InstanceID;
    uint primitiveID = gl_PrimitiveID;

    uint modelIndex = instanceData.instances[instanceID];

    uint indexBase = primitiveID * 3;
    uint i0 = indexBuffers[modelIndex].indices[indexBase + 0];
    uint i1 = indexBuffers[modelIndex].indices[indexBase + 1];
    uint i2 = indexBuffers[modelIndex].indices[indexBase + 2];

    Vertex v0 = vertexBuffers[modelIndex].vertices[i0];
    Vertex v1 = vertexBuffers[modelIndex].vertices[i1];
    Vertex v2 = vertexBuffers[modelIndex].vertices[i2];

    vec3 position = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    //获取插值坐标
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 normal      = v0.normal.xyz * barycentrics.x + v1.normal.xyz * barycentrics.y + v2.normal.xyz * barycentrics.z;
    mat3 normalMat = transpose(mat3(gl_WorldToObjectEXT));
    vec3 worldNormal = normalize(normalMat * normal); // Transforming the normal to world space

    
    if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
    {
        worldNormal = -worldNormal;
    }
    prd.positionHit = vec4(position,1);
    prd.normalHit = vec4(worldNormal,1);

    //获取材质信息
    uint textureIndex = textureIndexData.indexs[modelIndex];
    vec2 uv = v0.uv * barycentrics.x + v1.uv* barycentrics.y + v2.uv * barycentrics.z;
    vec4 albedo = texture(PBRTextures[textureIndex + ALBEDO_INDEX], uv);
    float ropughness = texture(PBRTextures[textureIndex + ROUGHNESS_INDEX], uv).r;
    prd.albedoRoughness = vec4(albedo.rgb, ropughness);

}