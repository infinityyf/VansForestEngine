#ifndef GRASS_BSDF_GLSL
#define GRASS_BSDF_GLSL
#include "BRDFData.glsl"
#include "GrassEnergy.glsl"
void GrassBSDF(BRDFData b,vec3 L,float transmission,vec4 energy,out vec3 diffuse,out vec3 specular)
{
    float NoL=max(dot(b.normal,L),0.0);
    float NoV=max(dot(b.normal,b.viewDirection),1e-4);
    vec3 sum=L+b.viewDirection;
    vec3 H=dot(sum,sum)>1e-8?normalize(sum):b.normal;
    vec3 F=FresnelSchlick(max(dot(b.viewDirection,H),0.0),vec3(0.04));
    float D=D_GGX(max(dot(b.normal,H),0.0),b.roughness);
    float alpha2=pow(b.roughness,4.0);
    float visibility=0.5/max(NoL*sqrt(NoV*NoV*(1.0-alpha2)+alpha2)+
        NoV*sqrt(NoL*NoL*(1.0-alpha2)+alpha2),1e-6);
    specular=D*visibility*F*NoL;
    const float wrap=0.5;
    float backWrap=clamp((-dot(b.normal,L)+wrap)/((1.0+wrap)*(1.0+wrap)),0.0,1.0);
    float x=max(-dot(b.viewDirection,L),0.0);
    const float a2=0.36;
    float denominator=(x*a2-x)*x+1.0;
    float scatter=a2/(PI*denominator*denominator);
    diffuse=b.albedo*((1.0-F)*(1.0-transmission)*NoL*INV_PI+
        0.96*transmission*backWrap*scatter)*GrassDiffuseEnergyScale(energy,transmission);
}
#endif
