#ifndef GI_RECEIVER_GEOMETRY_GLSL
#define GI_RECEIVER_GEOMETRY_GLSL
// 接收点查询共享实际 BLAS 布局，兼容有/无切线及 float 顶点。
#ifndef GI_RECEIVER_MESH_SET
#define GI_RECEIVER_MESH_SET 2
#endif
layout(set=GI_RECEIVER_MESH_SET,binding=3,std430) readonly buffer ReceiverVertices { uint words[]; } receiverVertices[];
layout(set=GI_RECEIVER_MESH_SET,binding=4,std430) readonly buffer ReceiverIndices { uint16_t words[]; } receiverIndices[];
layout(set=GI_RECEIVER_MESH_SET,binding=5,std430) readonly buffer ReceiverInstances { uint instances[]; } receiverInstances;
struct ReceiverMeshLayout { uvec4 offsets; uvec4 formats; };
layout(set=GI_RECEIVER_MESH_SET,binding=14,std430) readonly buffer ReceiverMeshLayouts { ReceiverMeshLayout meshes[]; } receiverMeshLayouts;

uvec3 ReceiverTriangle(uint mesh)
{
    uvec3 result;
    for(uint i=0u;i<3u;++i)
    {
        uint index=uint(gl_PrimitiveID)*3u+i;
        bool halfIndex=receiverMeshLayouts.meshes[mesh].formats.y!=0u;
        // 16 位末尾索引可能只剩两个字节，不能通过越界的 32 位读取得它。
        uint low=uint(receiverIndices[nonuniformEXT(mesh)].words[halfIndex?index:index*2u]);
        result[i]=halfIndex?low:(low|(uint(receiverIndices[nonuniformEXT(mesh)].words[index*2u+1u])<<16u));
    }
    return result;
}
vec3 ReceiverAttribute(uint mesh,uint vertex,uint attributeIndex)
{
    ReceiverMeshLayout meshLayout=receiverMeshLayouts.meshes[mesh];
    uint offset=meshLayout.offsets[attributeIndex+1u];
    if(offset==0xffffffffu || meshLayout.offsets.x==0u) return vec3(0.0);
    bool full=(meshLayout.formats.x&(1u<<attributeIndex))!=0u;
    uint address=vertex*meshLayout.offsets.x+offset;
    vec3 result=vec3(0.0);
    uint count=attributeIndex==1u?2u:3u;
    for(uint i=0u;i<count;++i)
    {
        uint byteOffset=address+i*(full?4u:2u);
        uint word=receiverVertices[nonuniformEXT(mesh)].words[byteOffset/4u];
        result[i]=full?uintBitsToFloat(word):unpackHalf2x16(word)[(byteOffset&2u)/2u];
    }
    return result;
}
vec3 ReceiverInterpolatedAttribute(uint mesh,uvec3 triangle,vec3 bary,uint attributeIndex)
{
    return ReceiverAttribute(mesh,triangle.x,attributeIndex)*bary.x+
        ReceiverAttribute(mesh,triangle.y,attributeIndex)*bary.y+
        ReceiverAttribute(mesh,triangle.z,attributeIndex)*bary.z;
}
#endif
