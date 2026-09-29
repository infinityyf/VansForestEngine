#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#define VANS_SURFACE_COOKIES
#include "../Common/CameraData.glsl"
#define LightCBBind 0
#include "../Lights/LightsData.glsl"
#include "../Atmosphere/AtmosphereMediaComposition.glsl"
#include "HairIndirectLighting.glsl"
#include "../BRDF/BRDFHair.glsl"
#define TILE_LIGHT
#include "../Common/TileLightData.glsl"
#include "HairMaterial.glsl"
#define IES_PROFILE_SET 1
#define IES_PROFILE_BINDING 4
#include "../Lights/IESProfile.glsl"
layout(location=0) in vec2 fragUV;
layout(location=1) in vec3 normalWS;
layout(location=2) in vec3 tangentWS;
layout(location=3) in vec3 bitangentWS;
layout(location=4) in vec3 positionWS;
layout(set=1,binding=2) uniform sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT];
layout(set=1,binding=3) uniform sampler2DArray cascadeShadowMap;
layout(location=0) out vec4 outHairColor;
layout(early_fragment_tests) in;
void main()
{
    float alpha=HairCoverage(fragUV);
    if(alpha<=0.0001) discard;
    vec3 N=HairSafeNormalize(normalWS,vec3(0,1,0));
    vec3 fallback=HairSafeNormalize(cross(abs(N.y)<0.9?vec3(0,1,0):vec3(1,0,0),N),vec3(1,0,0));
    vec3 T=HairSafeNormalize(tangentWS-N*dot(N,tangentWS),fallback);
    float handedness=dot(cross(N,T),bitangentWS)<0.0?-1.0:1.0;
    vec3 B=cross(N,T)*handedness;
    if(hairParams.scattering.w>0.0)
    {
        vec3 nTS=texture(hairNormal,fragUV).xyz*2.0-1.0;
        nTS.xy*=hairParams.scattering.w;
        N=HairSafeNormalize(mat3(T,B,N)*nTS,N);
    }
    if (hairParams.coverage.z > 0.0)
    {
        vec2 flow=texture(hairFlow,fragUV).rg*2.0-1.0;
        vec3 flowT=HairSafeNormalize(T*flow.x+B*flow.y,T);
        T=HairSafeNormalize(mix(T,flowT,hairParams.coverage.z),T);
    }
    T=HairSafeNormalize(T-N*dot(N,T),fallback);
    HairData hair;
    hair.tangentWS=T;
    hair.viewDirWS=HairSafeNormalize(cameraPosition.xyz-positionWS,N);
    hair.albedo=texture(hairAlbedo,fragUV).rgb;
    hair.longitudinalRoughness=clamp(hairParams.scattering.x*texture(hairRoughness,fragUV).r,0.035,1.0);
    hair.azimuthalRoughness=hairParams.scattering.y;
    hair.cuticleTilt=hairParams.scattering.z;
    PrepareHairScattering(hair);
    vec3 L=HairSafeNormalize(uDirectionLight.direction.xyz,N);
    float viewDepth=abs((ViewMatrix*vec4(positionWS,1.0)).z);
    vec3 direct=vec3(0.0);
    if(uDirectionLight.intensity>0.0)
    {
        float visibility=SampleSurfaceLightCookie(0,positionWS)*SampleCascadeShadow(positionWS,N,cascadeShadowMap,viewDepth);
        if(visibility>0.0)
            direct=EvaluateHairScattering(hair,L)*uDirectionLight.color.rgb*uDirectionLight.intensity*visibility;
    }
    // 共享列表仅做屏幕投影裁剪，不依赖不透明深度。面光列表容量小于总数，单独遍历以免漏光。
    TileLightHeader tile=GetFragTileLightHeader();
    for(uint item=0u;item<tile.pointCount;++item)
    {
        uint i=tileLightIndices[tile.pointOffset+item];
        PointLightData light=GetPointLight(int(i));
        if(light.intensity<=0.0) continue;
        vec3 delta=light.position.xyz-positionWS; float d=length(delta);
        if(d>=light.radius||d<1e-5) continue;
        L=delta/d;
        float attenuation=pow(1.0-d/max(light.radius,1e-5),2.0);
        if(light.iesProfileIndex>=0.0) attenuation*=SampleIESProfile(int(light.iesProfileIndex),L,vec3(0,-1,0));
        float shadow=SamplePointShadowMapBRDF(positionWS,N,L,punctualShadowMap,int(i));
        if(shadow<=0.0||attenuation<=0.0) continue;
        direct+=EvaluateHairScattering(hair,L)*light.color.rgb*light.intensity*attenuation*shadow*SampleSurfaceLightCookie(1+int(i),positionWS);
    }
    for(uint item=0u;item<tile.spotCount;++item)
    {
        uint i=tileLightIndices[tile.spotOffset+item];
        SpotLightData light=GetSpotLight(int(i));
        if(light.intensity<=0.0) continue;
        vec3 delta=light.position.xyz-positionWS; float d=length(delta);
        if(d>=light.radius||d<1e-5) continue;
        L=delta/d;
        float cone=smoothstep(cos(light.outerConeAngle),cos(light.innerConeAngle),dot(normalize(light.direction.xyz),L));
        if(cone<=0.0) continue;
        float attenuation=pow(1.0-d/max(light.radius,1e-5),2.0)*cone;
        if(light.iesProfileIndex>=0.0) attenuation*=SampleIESProfile(int(light.iesProfileIndex),L,light.direction.xyz)*max(light.iesIntensityScale,0.0);
        float shadow=SampleSpotShadowMapBRDF(positionWS,N,L,punctualShadowMap,int(i));
        if(shadow<=0.0||attenuation<=0.0) continue;
        direct+=EvaluateHairScattering(hair,L)*light.color.rgb*light.intensity*attenuation*shadow*SampleSurfaceLightCookie(65+int(i),positionWS);
    }
    for(uint i=0u;i<GetRectLightCount();++i)
    {
        RectLightData light=GetRectLight(int(i));
        if(light.up_intensity.w<=0.0) continue;
        float centerDistance=length(light.position_halfW.xyz-positionWS);
        if(centerDistance>=light.right_range.w) continue;
        float area=4.0*light.position_halfW.w*light.normal_halfH.w;
        for(int q=0;q<4;++q)
        {
            vec2 offset=vec2((q&1)==0?-0.577350269:0.577350269,(q&2)==0?-0.577350269:0.577350269);
            vec3 P=light.position_halfW.xyz+light.right_range.xyz*offset.x*light.position_halfW.w+light.up_intensity.xyz*offset.y*light.normal_halfH.w;
            vec3 delta=P-positionWS; float d=max(length(delta),1e-4); L=delta/d;
            float emitterCos=dot(light.normal_halfH.xyz,-L);
            emitterCos=light.color_twoSided.w>0.5?abs(emitterCos):max(emitterCos,0.0);
            float weight=area*0.25*emitterCos/(d*d)*pow(max(1.0-centerDistance/light.right_range.w,0.0),max(light.attenuationExp,1.0));
            if(weight<=0.0) continue;
            float shadow=SampleRectShadowMapBRDF(positionWS,N,L,punctualShadowMap,int(i));
            if(shadow<=0.0) continue;
            direct+=EvaluateHairScattering(hair,L)*light.color_twoSided.rgb*light.up_intensity.w*weight*shadow*SampleSurfaceLightCookie(129+int(i),positionWS);
        }
    }
    float ao=hairParams.occlusion.x>0.0?mix(1.0,texture(hairAO,fragUV).r,hairParams.occlusion.x):1.0;
    vec3 environment=EvaluateHairEnvironment(hair, positionWS, N);
    vec2 screenUV=gl_FragCoord.xy/ScreenParams.xy;
    vec3 radiance=CompositeAtmosphereSurfaceRadiance(screenUV,positionWS,max(direct+environment*ao,vec3(0.0)));
    // ShortCut 近层加权颜色；总覆盖率由所有片元累计，不会因近层容量截断。
    outHairColor=vec4(radiance*alpha,alpha);
}
