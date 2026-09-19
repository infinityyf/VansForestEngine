#ifndef GRASS_ENERGY_GLSL
#define GRASS_ENERGY_GLSL
// 与草专用直接 BSDF 相同的 GGX、Fresnel、wrap 散射瓣的离线半球积分。
// R=镜面方向反照率；G=漫反射；B/A=透射瓣的正/反入射半球。
layout(set=0,binding=40) uniform sampler2D grassEnergyLUT;
vec4 GrassIntegratedEnergy(float NoV,float roughness)
{
    vec2 size=vec2(textureSize(grassEnergyLUT,0));
    float v=pow(clamp((NoV-1e-4)/(1.0-1e-4),0.0,1.0),1.0/3.0);
    vec2 coordinate=vec2(v,clamp((roughness-0.045)/0.955,0.0,1.0));
    return textureLod(grassEnergyLUT,(coordinate*(size-1.0)+0.5)/size,0.0);
}
float GrassDiffuseEnergyScale(vec4 energy,float transmission)
{
    float diffuse=energy.y*(1.0-transmission)+(energy.z+energy.w)*transmission;
    return min(1.0,max(1.0-energy.x,0.0)/max(diffuse,1e-6));
}
vec2 GrassDiffuseHemisphereWeights(vec4 energy,float transmission)
{
    return vec2(energy.y*(1.0-transmission)+energy.z*transmission,
        energy.w*transmission)*GrassDiffuseEnergyScale(energy,transmission);
}
#endif
