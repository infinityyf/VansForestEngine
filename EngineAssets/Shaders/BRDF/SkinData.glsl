#ifndef SKIN_DATA_INCLUDED
#define SKIN_DATA_INCLUDED

#if !defined(SkinDataSetBind)
    #define SkinDataSetBind 0
#endif

#if !defined(SkinDataBinding)
    #define SkinDataBinding 18
#endif

struct SkinMaterialPayload
{
    vec4 scatterColorAmount;       // rgb = scatter tint, a = base scatter amount
    vec4 roughnessNormalSpecular;  // x = roughness, y = normal strength, z = specular scale, w = transmission scale
    vec4 lobeIOR;                  // x = primary roughness scale, y = secondary roughness scale, z = IOR, w = primary lobe weight
    vec4 profileControls;          // x = diffusion radius, y = thinness, z = optical depth, w = ambient scatter
    vec4 profileShape;             // rgb = per-channel diffusion radius scale, w = boundary color bleed
    vec4 profileLUT;               // x = profile LUT layer, -1 = legacy 2D LUT fallback, y = authored thinness weight
    vec4 debugControls;            // x = debug view mode, yzw reserved
};

layout(set = SkinDataSetBind, binding = SkinDataBinding, std430) readonly buffer SkinMaterialData
{
    SkinMaterialPayload skinMaterials[];
} skinMaterialDataBuffer;

SkinMaterialPayload DefaultSkinMaterialPayload()
{
    SkinMaterialPayload payload;
    payload.scatterColorAmount = vec4(1.0, 0.34, 0.22, 0.65);
    payload.roughnessNormalSpecular = vec4(0.62, 0.35, 1.0, 1.0);
    payload.lobeIOR = vec4(0.75, 1.75, 1.4, 0.72);
    payload.profileControls = vec4(1.0, 1.0, 1.0, 0.35);
    payload.profileShape = vec4(1.0);
    payload.profileLUT = vec4(-1.0, 0.0, 0.0, 0.0);
    payload.debugControls = vec4(0.0);
    return payload;
}

// Skin stores a small aux value in the material-ID fraction; round(id) stays stable.
const float SKIN_GBUFFER_AUX_RANGE = 0.49;

float PackSkinMaterialIDWithThinness(float materialID, float thinness, float thinnessWeight)
{
    return materialID + clamp(thinness, 0.0, 1.0) *
        clamp(thinnessWeight, 0.0, 1.0) * SKIN_GBUFFER_AUX_RANGE;
}

float UnpackSkinThinnessFromMaterialID(float encodedMaterialID, float materialID)
{
    return clamp((encodedMaterialID - materialID) / SKIN_GBUFFER_AUX_RANGE, 0.0, 1.0);
}

SkinMaterialPayload GetSkinMaterialPayload(int materialIndex)
{
    if (materialIndex < 0 || materialIndex >= skinMaterialDataBuffer.skinMaterials.length())
        return DefaultSkinMaterialPayload();
    return skinMaterialDataBuffer.skinMaterials[materialIndex];
}

#endif // SKIN_DATA_INCLUDED
