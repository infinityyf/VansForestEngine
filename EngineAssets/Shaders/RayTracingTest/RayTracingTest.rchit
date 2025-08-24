#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

//用于性能优化，提示编译器，不同线程组会访问不同索引的资源
#extension GL_EXT_nonuniform_qualifier : require

#include "../Common/Common.glsl"

//vertex data
struct Vertex 
{
    vec3 position;
};

//instance data
struct InstanceData 
{
    mat4 modelMatrix;
    uint modelIndex;
};

layout(location = 0) rayPayloadInEXT RayTracePayload prd;

layout(set = 0, binding = 3, std430) buffer VertexBuffers {
    Vertex vertices[];
} vertexBuffers[];

layout(set = 0, binding = 4, std430) buffer IndexBuffers {
    uint indices[];
} indexBuffers[];

layout(set = 0, binding = 5, std430) buffer InstanceDataBuffer {
    InstanceData instances[];
} instanceData;

void main()
{
    // uint instanceID = gl_InstanceIDEXT;
    // uint primitiveID = gl_PrimitiveIDEXT;

    // InstanceData instance = instanceData.instances[instanceID];
    // uint modelIndex = instance.modelIndex;

    // uint indexBase = primitiveID * 3;
    // uint i0 = indexBuffers[nonuniformEXT(modelIndex)].indices[indexBase + 0];
    // uint i1 = indexBuffers[nonuniformEXT(modelIndex)].indices[indexBase + 1];
    // uint i2 = indexBuffers[nonuniformEXT(modelIndex)].indices[indexBase + 2];

    // Vertex v0 = vertexBuffers[nonuniformEXT(modelIndex)].vertices[i0];
    // Vertex v1 = vertexBuffers[nonuniformEXT(modelIndex)].vertices[i1];
    // Vertex v2 = vertexBuffers[nonuniformEXT(modelIndex)].vertices[i2];

    vec3 position = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;;
    prd.positionHit = vec4(position,1);
}