#ifndef BRDF_GRASS_GLSL
#define BRDF_GRASS_GLSL
// 入射方向偏移不依赖 N·L，背光与侧光不会丢失草间的接触阴影。
float GrassContactShadow(vec3 position,vec3 L,float maxDistance,vec4 rayParams,float strength)
{
    float distance=min(maxDistance,rayParams.x);
    if(distance<=0.02) return 1.0;
    vec3 originSS;
    if(!HiZ_ProjectToScreenChecked(ViewMatrix,ProjectionMatrix,position,originSS)) return 1.0;
    float bias=max(rayParams.z*0.25,0.005);
    float thickness=max(min(rayParams.y,0.08),0.01);
    int steps=clamp(int(rayParams.w),8,32);
    float visibility=1.0;
    float jitter=RandomInterLeaved(originSS.xy*uSSS.screenSize.xy);
    for(int i=0;i<steps;++i)
    {
        float t=(float(i)+0.5+jitter*0.5)/float(steps);
        float travel=mix(max(bias*2.0,0.02),distance,t);
        vec3 sampleSS;
        if(!HiZ_ProjectToScreenChecked(ViewMatrix,ProjectionMatrix,position+L*travel,sampleSS)) break;
        if(any(lessThan(sampleSS.xy,vec2(0))) || any(greaterThanEqual(sampleSS.xy,vec2(1)))) break;
        ivec2 size=textureSize(screenSpaceShadowHZB,0);
        float depth=texelFetch(screenSpaceShadowHZB,clamp(ivec2(sampleSS.xy*vec2(size)),ivec2(0),size-1),0).r;
        // 先排除接收叶片，再继续搜索其它草片，不能在第一个自相交处结束整条射线。
        if(depth<=0.0 || abs(depth-originSS.z)<max(bias,0.01)) continue;
        float separation=sampleSS.z-depth;
        if(separation>0.003 && separation<thickness)
        {
            float weight=(1.0-smoothstep(0.003,thickness,separation))*(1.0-smoothstep(0.65,1.0,t));
            visibility=min(visibility,1.0-weight);
        }
    }
    return mix(1.0,visibility,ScreenSpaceContactEdgeFade(originSS.xy)*clamp(strength,0.0,1.0));
}
// 草叶的反射与透射共享能量预算。N 始终是可见侧法线，L 指向光源。
// 透射角分布参考 UE TwoSidedFoliage；屏幕空间可见度在调用处独立处理。
#include "GrassBSDF.glsl"
void AddGrassLight(BRDFData b,vec3 L,vec3 irradiance,float visibility,float transmission,vec4 energy,inout LightResult result)
{
    vec3 diffuse,specular;
    GrassBSDF(b,L,transmission,energy,diffuse,specular);
    result.directDiffuse+=diffuse*irradiance*visibility;
    result.directSpecular+=specular*irradiance*visibility;
}
void CalculateDirectLight_Grass(BRDFData b,float transmission,vec4 energy,float sunVisibility,
    sampler2DShadow shadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT],inout LightResult result)
{
    AddGrassLight(b,normalize(uDirectionLight.direction.xyz),
        uDirectionLight.color.rgb*uDirectionLight.intensity*SampleSurfaceLightCookie(0,b.positionWS),sunVisibility,transmission,energy,result);
    TileLightHeader tile=GetFragTileLightHeader();
    for(uint k=0u;k<tile.pointCount;++k)
    {
        uint i=tileLightIndices[tile.pointOffset+k];
        PointLightData light=GetPointLight(int(i));
        vec3 delta=light.position.xyz-b.positionWS;
        float distance=length(delta);
        if(distance>=light.radius || distance<1e-5) continue;
        vec3 L=delta/distance;
        // 偏移到入射光一侧，让背光叶片也得到一致的阴影接收点。
        vec3 shadowN=dot(b.normal,L)<0.0?-b.normal:b.normal;
        float shadow=SamplePointShadowMapBRDF(b.positionWS,shadowN,L,shadowMap,int(i));
        if(IsPointShadowFallbackSelected(tile,i)) shadow=BlendPunctualShadowFallback(shadow,
            GrassContactShadow(b.positionWS,L,distance,uSSS.punctualRayParams,uSSS.fadeParams.w),0u,int(i));
        float falloff=pow(1.0-distance/light.radius,2.0);
        if(light.iesProfileIndex>=0.0) falloff*=SampleIESProfile(int(light.iesProfileIndex),L,vec3(0,-1,0));
        AddGrassLight(b,L,light.color.rgb*light.intensity*falloff*SampleSurfaceLightCookie(1+int(i),b.positionWS),shadow,transmission,energy,result);
    }
    for(uint k=0u;k<tile.spotCount;++k)
    {
        uint i=tileLightIndices[tile.spotOffset+k];
        SpotLightData light=GetSpotLight(int(i));
        vec3 delta=light.position.xyz-b.positionWS;
        float distance=length(delta);
        if(distance>=light.radius || distance<1e-5) continue;
        vec3 L=delta/distance;
        float cone=clamp((dot(normalize(light.direction.xyz),L)-cos(light.outerConeAngle))/max(cos(light.innerConeAngle)-cos(light.outerConeAngle),1e-5),0.0,1.0);
        if(cone<=0.0) continue;
        vec3 shadowN=dot(b.normal,L)<0.0?-b.normal:b.normal;
        float shadow=SampleSpotShadowMapBRDF(b.positionWS,shadowN,L,shadowMap,int(i));
        if(IsSpotShadowFallbackSelected(tile,i)) shadow=BlendPunctualShadowFallback(shadow,
            GrassContactShadow(b.positionWS,L,distance,uSSS.punctualRayParams,uSSS.fadeParams.w),1u,int(i));
        float falloff=pow(1.0-distance/light.radius,2.0)*cone;
        if(light.iesProfileIndex>=0.0) falloff*=SampleIESProfile(int(light.iesProfileIndex),L,light.direction.xyz)*light.iesIntensityScale;
        AddGrassLight(b,L,light.color.rgb*light.intensity*falloff*SampleSurfaceLightCookie(65+int(i),b.positionWS),shadow,transmission,energy,result);
    }
    for(uint k=0u;k<tile.rectCount;++k)
    {
        uint i=tileLightIndices[tile.rectOffset+k];
        RectLightData light=GetRectLight(int(i));
        light.color_twoSided.rgb*=SampleSurfaceLightCookie(129+int(i),b.positionWS);
        vec3 d,s,back,unused;
        EvaluateRectLightLTC(light,b.normal,b.viewDirection,b.positionWS,b.roughness,
            b.albedo*(1.0-transmission),vec3(0.04),d,s);
        EvaluateRectLightLTC(light,-b.normal,-b.viewDirection,b.positionWS,1.0,
            b.albedo*transmission*0.96,vec3(0),back,unused);
        vec3 delta=light.position_halfW.xyz-b.positionWS;
        float distance=max(length(delta),1e-5);
        vec3 L=delta/distance,shadowN=dot(b.normal,L)<0.0?-b.normal:b.normal;
        float shadow=SampleRectShadowMapBRDF(b.positionWS,shadowN,L,shadowMap,int(i));
        if(IsRectShadowFallbackSelected(tile,i)) shadow=BlendPunctualShadowFallback(shadow,
            GrassContactShadow(b.positionWS,L,distance,uSSS.punctualRayParams,uSSS.fadeParams.w),2u,int(i));
        result.directDiffuse+=(d+back)*shadow*GrassDiffuseEnergyScale(energy,transmission);
        result.directSpecular+=s*shadow;
    }
}
void AmbientBRDF_Grass(BRDFData b,vec3 backIrradiance,float transmission,float backAO,
    vec4 energy,float indirectDiffuseStrength,inout LightResult result)
{
    // 两路输入都是 E/pi；不能再乘一次 pi，也不能把背面光混进正面缓存。
    // wrap 透射瓣会覆盖少量正入射半球；分别积分，不能全放入背面。
    vec2 weights=GrassDiffuseHemisphereWeights(energy,transmission);
    result.ambientDiffuse=b.albedo*(b.indirectDiffuse*b.ao*weights.x+
        backIrradiance*backAO*weights.y)*indirectDiffuseStrength;
    vec3 reflection=reflect(-b.viewDirection,b.normal);
    ReflectionProbeSample probe=SampleReflectionProbes(b.positionWS,b.normal,reflection,b.roughness);
    vec3 environment=probe.specular;
    if(probe.coverage<1.0)
    {
        vec3 skySpecular = SampleSkySpecularCube(PreConvSpecularEnvironment,reflection,
            GetMipLevelFromRoughness(b.roughness));
        #ifdef AMBIENT_SKY_CACHE_ENABLED
            AmbientSkyTransmittanceSample skyVisibility = SampleAmbientSkyTransmittance(b.positionWS, reflection, b.roughness);
            skySpecular *= skyVisibility.visibility;
        #endif
        environment=mix(skySpecular,probe.specular,probe.coverage);
    }
    float ssrWeight=clamp(b.indirectSpecular.a*(1.0-smoothstep(reflectionProbeLightingParams.x,reflectionProbeLightingParams.y,b.roughness)),0.0,1.0);
    environment=mix(environment,b.indirectSpecular.rgb,ssrWeight);
    float NoV=max(dot(b.normal,b.viewDirection),0.0);
    float specularOcclusion=clamp(pow(NoV+b.ao,exp2(-16.0*b.roughness-1.0))-1.0+b.ao,0.0,1.0);
    result.ambientSpecular=environment*energy.x*specularOcclusion;
}
#endif
