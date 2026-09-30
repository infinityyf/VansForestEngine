#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../Common/VansDrawSubmission.glsl"
#include "../Common/CustomMaterialData.glsl"
#include "../Decal/DecalResponse.glsl"
#include "../Decal/DecalReceiverFilter.glsl"

layout(set=0,binding=50) uniform sampler2D globalPBRTextures[];
layout(set=1,binding=0) uniform sampler2D gBuffer2Sampler;
layout(set=1,binding=1) uniform sampler2D gBuffer1Sampler;
layout(set=1,binding=2) uniform sampler2D normalSampler;
layout(location=0) flat in vec4 splineOriginDepth;
layout(location=1) flat in vec3 splineEdge1;
layout(location=2) flat in vec3 splineEdge2;
layout(location=3) flat in vec4 splineUV01;
layout(location=4) flat in vec2 splineUV2;
layout(location=5) flat in vec3 splineFadeRange;
layout(location=0) out vec4 outDecalColor;
layout(location=1) out vec4 outDecalNormal;
layout(location=2) out vec4 outDecalRoughness;

float SplineEdgeNoiseHash(vec2 p)
{
    return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);
}

float SplineEdgeNoise(vec2 p)
{
    vec2 cell=floor(p),fraction=fract(p);
    fraction=fraction*fraction*(3.0-2.0*fraction);
    return mix(mix(SplineEdgeNoiseHash(cell),SplineEdgeNoiseHash(cell+vec2(1,0)),fraction.x),
        mix(SplineEdgeNoiseHash(cell+vec2(0,1)),SplineEdgeNoiseHash(cell+vec2(1,1)),fraction.x),fraction.y);
}

void main()
{
    VansDrawData drawData=VansGetDrawData();
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 surface=texelFetch(gBuffer2Sampler,pixel,0);
    vec3 dpdx=dFdx(surface.xyz),dpdy=dFdy(surface.xyz);
    if (surface.w<=0.0) discard;
    vec4 receiver=texelFetch(gBuffer1Sampler,pixel,0);
    int receiverID=DecodeDecalReceiverMaterialID(receiver.z);
    vec3 response=DecalReceiverResponse(receiverID);
    if (all(equal(response,vec3(0.0)))) discard;
    float receiverGroup=ModelBuffer.transforms[drawData.transformIndex].Position.w;
    if (!DecalReceiverMatches(receiverID,receiver.w,receiverGroup)) discard;
    float minimumDot=ModelBuffer.transforms[drawData.transformIndex].Scale.w;
    if (!DecalNormalMatches(texelFetch(normalSampler,pixel,0).xyz,vec3(0.0,1.0,0.0),minimumDot)) discard;

    // 光栅棱柱仅限定覆盖范围；接收面的世界坐标决定样条带的 UV。
    vec3 local=surface.xyz-splineOriginDepth.xyz;
    float determinantXZ=splineEdge1.x*splineEdge2.z-splineEdge1.z*splineEdge2.x;
    if (abs(determinantXZ)<1e-8) discard;
    vec2 bary=vec2(local.x*splineEdge2.z-local.z*splineEdge2.x,
        splineEdge1.x*local.z-splineEdge1.z*local.x)/determinantXZ;
    if (any(lessThan(bary,vec2(0.0))) || bary.x+bary.y>1.0) discard;
    float below=bary.x*splineEdge1.y+bary.y*splineEdge2.y-local.y;
    if (below<0.0 || below>splineOriginDepth.w) discard;
    vec2 duv1=splineUV01.zw-splineUV01.xy,duv2=splineUV2-splineUV01.xy;
    vec2 uv=splineUV01.xy+bary.x*duv1+bary.y*duv2;
    vec2 dbdx=vec2(dpdx.x*splineEdge2.z-dpdx.z*splineEdge2.x,
        splineEdge1.x*dpdx.z-splineEdge1.z*dpdx.x)/determinantXZ;
    vec2 dbdy=vec2(dpdy.x*splineEdge2.z-dpdy.z*splineEdge2.x,
        splineEdge1.x*dpdy.z-splineEdge1.z*dpdy.x)/determinantXZ;
    vec2 uvDx=dbdx.x*duv1+dbdx.y*duv2;
    vec2 uvDy=dbdy.x*duv1+dbdy.y*duv2;
    vec2 splineUV=uv;

    CustomMaterialPayload material=customMaterialDataBuffer.materials[drawData.materialIndex];
    float edgeFade=1.0;
    float sideFadeFraction=material.values[3].y;
    if (sideFadeFraction>0.0)
        edgeFade=min(edgeFade,clamp(min(splineUV.x,1.0-splineUV.x)/sideFadeFraction,0.0,1.0));
    float endFadeMeters=material.values[3].z;
    if (endFadeMeters>0.0 && splineFadeRange.z>0.0 && splineFadeRange.y!=0.0)
    {
        float alongMeters=clamp((splineUV.y-splineFadeRange.x)*splineFadeRange.y,0.0,1.0)*splineFadeRange.z;
        edgeFade=min(edgeFade,clamp(min(alongMeters,splineFadeRange.z-alongMeters)/endFadeMeters,0.0,1.0));
    }
    // The optional noise texture changes only the partially transparent band.
    float noiseStrength=material.values[3].w;
    int noiseIndex=int(round(material.values[5].x));
    if (edgeFade>0.0 && edgeFade<1.0 && noiseStrength>0.0)
    {
        float noiseScale=material.values[4].x;
        vec2 noiseUV=surface.xz*noiseScale;
        float noise=noiseIndex>=0?
            textureGrad(globalPBRTextures[nonuniformEXT(noiseIndex)],noiseUV,
                dpdx.xz*noiseScale,dpdy.xz*noiseScale).r:
            SplineEdgeNoise(noiseUV);
        edgeFade=clamp(edgeFade+(noise-0.5)*noiseStrength*4.0*edgeFade*(1.0-edgeFade),0.0,1.0);
    }
    edgeFade=edgeFade*edgeFade*(3.0-2.0*edgeFade);
    if (material.values[2].y>=0.5)
    {
        // 路径素材的横轴沿道路：同步交换采样梯度和法线切线基。
        uv=uv.yx;uvDx=uvDx.yx;uvDy=uvDy.yx;
        duv1=duv1.yx;duv2=duv2.yx;
    }
    float crossScale=material.values[2].w;
    uv.y=uv.y*crossScale+material.values[3].x;uvDx.y*=crossScale;uvDy.y*=crossScale;
    duv1.y*=crossScale;duv2.y*=crossScale;
    vec4 colorSample=textureGrad(globalPBRTextures[nonuniformEXT(material.textureIndices.x)],uv,uvDx,uvDy);
    vec4 normalSample=textureGrad(globalPBRTextures[nonuniformEXT(material.textureIndices.y)],uv,uvDx,uvDy);
    vec4 roughnessSample=textureGrad(globalPBRTextures[nonuniformEXT(material.textureIndices.z)],uv,uvDx,uvDy);
    vec3 fourthSample=textureGrad(globalPBRTextures[nonuniformEXT(material.textureIndices.w)],uv,uvDx,uvDy).rgb;
    bool coverageIsAO=material.values[2].z>=0.5;
    vec3 coverageMask=coverageIsAO?vec3(1.0):fourthSample;
    vec3 coverage=DecalAttributeCoverage(vec3(colorSample.a,normalSample.a,roughnessSample.a),
        coverageMask,material.values[0].a,material.values[1].yzw,response)*edgeFade;
    if (all(lessThanEqual(coverage,vec3(0.0)))) discard;

    float determinantUV=duv1.x*duv2.y-duv1.y*duv2.x;
    if (abs(determinantUV)<1e-8) discard;
    vec3 directionU=(splineEdge1*duv2.y-splineEdge2*duv1.y)/determinantUV;
    vec3 directionV=(splineEdge2*duv1.x-splineEdge1*duv2.x)/determinantUV;
    vec3 geometricNormal=DecalSafeNormal(cross(dpdy,dpdx),vec3(0,1,0));
    if (geometricNormal.y<0.0) geometricNormal=-geometricNormal;
    vec3 tangent=DecalSafeNormal(directionU-geometricNormal*dot(directionU,geometricNormal),vec3(1,0,0));
    vec3 bitangent=cross(geometricNormal,tangent);
    if (dot(bitangent,directionV)<0.0) bitangent=-bitangent;
    vec3 normalWS=DecalSafeNormal(mat3(tangent,bitangent,geometricNormal)*(normalSample.rgb*2.0-1.0),geometricNormal);
    outDecalColor=vec4(clamp(material.values[0].rgb*colorSample.rgb,0.0,1.0),coverage.x);
    outDecalNormal=vec4(normalWS,coverage.y);
    // G 保存 AO，B 标记 AO 覆盖率；与其他贴花共用原有 RGBA 混合附件。
    outDecalRoughness=vec4(clamp(material.values[1].x*roughnessSample.r,0.0,1.0),
        coverageIsAO?fourthSample.r:0.0,coverageIsAO?1.0:0.0,coverage.z);
}
