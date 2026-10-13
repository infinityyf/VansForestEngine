#ifndef GI_INSTANCE_MATERIAL_GLSL
#define GI_INSTANCE_MATERIAL_GLSL
// 与 GIInstanceMaterialGPU 对应；身份和裁剪阈值均按 TLAS 实例寻址。
struct GIInstanceMaterial
{
    uint packedTextureIndex;
    float alphaCutoff;
    uint clothMaterialIndex;
    uint padding;
    vec4 clothDiffuseRoughness;
};
#ifndef GI_INSTANCE_MATERIAL_SET
#define GI_INSTANCE_MATERIAL_SET 0
#endif
layout(set = GI_INSTANCE_MATERIAL_SET, binding = 7, std430) readonly buffer InstanceMaterialBuffer
{
    GIInstanceMaterial materials[];
} instanceMaterialData;
#define GI_TEXTURE_INDEX_MASK 0x0FFFFFFFu
#define GI_CLOTH_FLAG 0x20000000u
#define GI_CLOTH_SINGLE_SIDED_FLAG 0x10000000u
#define GI_PURE_EMISSIVE_FLAG 0x40000000u
#define GI_PBR_EMISSIVE_FLAG 0x80000000u

bool GIRejectMaterialFace(GIInstanceMaterial material, bool frontFace)
{
    return material.packedTextureIndex != 0xffffffffu && !frontFace &&
        (material.packedTextureIndex & (GI_CLOTH_FLAG | GI_CLOTH_SINGLE_SIDED_FLAG)) ==
        (GI_CLOTH_FLAG | GI_CLOTH_SINGLE_SIDED_FLAG);
}
uint GICoverageTextureIndex(GIInstanceMaterial material)
{
    return (material.packedTextureIndex & GI_TEXTURE_INDEX_MASK) +
        ((material.packedTextureIndex & GI_CLOTH_FLAG) != 0u ? 2u : 0u);
}
bool GIRejectMaterialCoverage(GIInstanceMaterial material, vec4 sampleValue)
{
    float coverage = (material.packedTextureIndex & GI_CLOTH_FLAG) != 0u ? sampleValue.r : sampleValue.a;
    return coverage < material.alphaCutoff;
}
#endif
