#ifndef SKY_LIGHTING_INCLUDED
#define SKY_LIGHTING_INCLUDED

// 同一未缩放静态天空的 radiance / irradiance / specular 表示由源统一发布。
layout(set = 0, binding = 37, std140) uniform SkyLightingParameters
{
    vec4 skyLightingParameters; // x：本帧天光 radiance 缩放，所有入口只应用一次。
};
float GetSkyDiffuseCubeIntensity() { return max(skyLightingParameters.x, 0.0); }
float GetSkySpecularCubeIntensity() { return max(skyLightingParameters.x, 0.0); }
vec3 SampleSkyRadiance(samplerCube source, vec3 direction)
{
    return textureLod(source, normalize(direction), 0.0).rgb * GetSkyDiffuseCubeIntensity();
}
vec3 SampleSkyDiffuseIrradiance(samplerCube source, vec3 direction)
{
    return textureLod(source, normalize(direction), 0.0).rgb * GetSkyDiffuseCubeIntensity();
}
// 消费端的 diffuse lighting 契约是 E/π，反照率和材质因子仍由原有 BRDF 应用。
vec3 SampleSkyDiffuseCube(samplerCube source, vec3 direction)
{
    return SampleSkyDiffuseIrradiance(source, direction) * 0.31830988618379067154;
}
vec3 SampleSkySpecularCube(samplerCube source, vec3 direction, float lod)
{
    return textureLod(source, normalize(direction), lod).rgb * GetSkySpecularCubeIntensity();
}
#endif
