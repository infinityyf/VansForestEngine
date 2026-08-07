#ifndef TREE_LEAF_DATA_GLSL
#define TREE_LEAF_DATA_GLSL

#if !defined(TreeLeafDataSetBind)
    #define TreeLeafDataSetBind 0
#endif

#if !defined(TreeLeafDataBinding)
    #define TreeLeafDataBinding 17
#endif

struct TreeLeafMaterialPayload
{
    vec4 subsurfaceColorAndStrength; // rgb = artist tint, a = transmission scale
    vec4 scattering;                 // x = wrap, y = scatter roughness, z = specular scale, w = alpha clip
    vec4 reserved;
};

layout(set = TreeLeafDataSetBind, binding = TreeLeafDataBinding, std430) readonly buffer TreeLeafMaterialData
{
    TreeLeafMaterialPayload treeLeafMaterials[];
} treeLeafMaterialDataBuffer;

TreeLeafMaterialPayload DefaultTreeLeafMaterialPayload()
{
    TreeLeafMaterialPayload payload;
    payload.subsurfaceColorAndStrength = vec4(0.70, 1.00, 0.32, 0.75);
    payload.scattering = vec4(0.50, 0.60, 0.45, 0.50);
    payload.reserved = vec4(0.0);
    return payload;
}

TreeLeafMaterialPayload GetTreeLeafMaterialPayload(int materialIndex)
{
    if (materialIndex < 0)
        return DefaultTreeLeafMaterialPayload();
    return treeLeafMaterialDataBuffer.treeLeafMaterials[materialIndex];
}

#endif // TREE_LEAF_DATA_GLSL
