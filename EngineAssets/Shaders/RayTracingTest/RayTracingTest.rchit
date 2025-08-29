#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

//用于性能优化，提示编译器，不同线程组会访问不同索引的资源
#extension GL_EXT_nonuniform_qualifier : require

#include "../Common/Common.glsl"

//vertex data
struct Vertex 
{
    vec4 position;
    vec4 normal;
};

layout(location = 0) rayPayloadInEXT RayTracePayload prd;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 3, std430) buffer VertexBuffers {
    Vertex vertices[];
} vertexBuffers[];

layout(set = 0, binding = 4, std430) buffer IndexBuffers {
    uint indices[];
} indexBuffers[];

layout(set = 0, binding = 5, std430) buffer InstanceDataBuffer {
    uint instances[];
} instanceData;

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

    prd.positionHit = vec4(position,1);
    if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
    {
        worldNormal = -worldNormal;
    }
    prd.normalHit = vec4(worldNormal,0);
}