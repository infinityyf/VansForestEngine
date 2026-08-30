#ifndef PBR_WATER_PARAMS_GLSL
#define PBR_WATER_PARAMS_GLSL

#if !defined(PBR_WATER_PARAMS_SET) || !defined(PBR_WATER_PARAMS_BINDING) || !defined(PBR_WATER_PARAMS_INSTANCE)
    #error "Define PBR_WATER_PARAMS_SET, PBR_WATER_PARAMS_BINDING and PBR_WATER_PARAMS_INSTANCE before including pbr_water_params.glsl"
#endif

layout(set = PBR_WATER_PARAMS_SET, binding = PBR_WATER_PARAMS_BINDING) uniform PBRWaterParams
{
    vec4 absorptionCoeff;
    vec4 scatteringCoeff;
    vec4 cameraPosition;
    vec4 mainLightDir;
    vec4 mainLightColor;
    vec4 surfaceParams;
    vec4 refractionParams;
    vec4 volumeParams;
    vec4 thinSSSParams;
    vec4 backlitParams;
    vec4 filterParams;
    ivec4 effectFlags;
    vec4 colorMipParams0;
    vec4 colorMipParams1;
    vec4 shadowParams;
    vec4 shadowVolumeParams;
    mat4 invViewProjMatrix;
    mat4 viewMatrix;
    mat4 projMatrix;
} PBR_WATER_PARAMS_INSTANCE;

#endif
