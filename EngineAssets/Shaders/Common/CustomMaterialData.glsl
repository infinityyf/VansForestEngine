#ifndef CUSTOM_MATERIAL_DATA_INCLUDED
#define CUSTOM_MATERIAL_DATA_INCLUDED

#if !defined(CustomMaterialDataSetBind)
    #define CustomMaterialDataSetBind 0
#endif

#if !defined(CustomMaterialDataBinding)
    #define CustomMaterialDataBinding 15
#endif

struct CustomMaterialPayload
{
    vec4 values[8];
    ivec4 textureIndices;
};

layout(set = CustomMaterialDataSetBind, binding = CustomMaterialDataBinding, std430) readonly buffer CustomMaterialData
{
    CustomMaterialPayload materials[];
} customMaterialDataBuffer;

#endif
