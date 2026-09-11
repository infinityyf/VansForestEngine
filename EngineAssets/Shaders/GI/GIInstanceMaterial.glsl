#ifndef GI_INSTANCE_MATERIAL_GLSL
#define GI_INSTANCE_MATERIAL_GLSL
// 与 GIInstanceMaterialGPU 对应；身份和裁剪阈值均按 TLAS 实例寻址。
struct GIInstanceMaterial
{
    uint packedTextureIndex;
    float alphaCutoff;
};
#ifndef GI_INSTANCE_MATERIAL_SET
#define GI_INSTANCE_MATERIAL_SET 0
#endif
layout(set = GI_INSTANCE_MATERIAL_SET, binding = 7, std430) readonly buffer InstanceMaterialBuffer
{
    GIInstanceMaterial materials[];
} instanceMaterialData;
#define GI_TEXTURE_INDEX_MASK 0x3FFFFFFFu
#define GI_PURE_EMISSIVE_FLAG 0x40000000u
#define GI_PBR_EMISSIVE_FLAG 0x80000000u
#endif
