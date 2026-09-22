#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../Common/VansDrawSubmission.glsl"
#include "../BRDF/BRDFData.glsl"
#include "../Decal/DecalResponse.glsl"
#include "../Decal/DecalReceiverFilter.glsl"

layout(set=0,binding=50) uniform sampler2D globalPBRTextures[];
layout(set=1,binding=0) uniform sampler2D gBuffer2Sampler;
layout(set=1,binding=1) uniform sampler2D gBuffer1Sampler;
layout(set=1,binding=2) uniform sampler2D normalSampler;
layout(location=0) flat in vec4 roadOriginDepth;
layout(location=1) flat in vec3 roadEdge1;
layout(location=2) flat in vec3 roadEdge2;
layout(location=3) flat in vec4 roadUV01;
layout(location=4) flat in vec2 roadUV2;
layout(location=0) out vec4 outDecalColor;
layout(location=1) out vec4 outDecalNormal;
layout(location=2) out vec4 outDecalRoughness;

void main()
{
    VansDrawData drawData=VansGetDrawData();
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 surface=texelFetch(gBuffer2Sampler,pixel,0);
    // 在任何分支丢弃之前计算接收位置导数。
    vec3 dpdx=dFdx(surface.xyz),dpdy=dFdy(surface.xyz);
    if (surface.w<=0.0) discard;
    vec4 receiver=texelFetch(gBuffer1Sampler,pixel,0);
    int receiverID=DecodeDecalReceiverMaterialID(receiver.z);
    float receiverGroup=ModelBuffer.transforms[drawData.transformIndex].Position.w;
    if (!DecalReceiverMatches(receiverID,receiver.w,receiverGroup)) discard;
    vec3 response=DecalReceiverResponse(receiverID);
    if (all(equal(response,vec3(0.0)))) discard;

    // 代理仅限定光栅范围。沿世界 -Y 投影时，地表 XZ 决定原道路的重心坐标。
    // PCG 代理顶点已经在世界空间，节点保持单位变换。
    vec3 local=surface.xyz-roadOriginDepth.xyz;
    float determinantXZ=roadEdge1.x*roadEdge2.z-roadEdge1.z*roadEdge2.x;
    if (abs(determinantXZ)<1e-8) discard;
    vec2 bary=vec2(local.x*roadEdge2.z-local.z*roadEdge2.x,
        roadEdge1.x*local.z-roadEdge1.z*local.x)/determinantXZ;
    if (any(lessThan(bary,vec2(0.0))) || bary.x+bary.y>1.0) discard;
    float below=bary.x*roadEdge1.y+bary.y*roadEdge2.y-local.y;
    if (below<0.0 || below>roadOriginDepth.w) discard;
    vec2 duv1=roadUV01.zw-roadUV01.xy,duv2=roadUV2-roadUV01.xy;
    vec2 uv=roadUV01.xy+bary.x*duv1+bary.y*duv2;

    // 从地表导数解析计算道路 UV 梯度，避免棱柱边缘的代理 UV 导数。
    vec2 dbdx=vec2(dpdx.x*roadEdge2.z-dpdx.z*roadEdge2.x,
        roadEdge1.x*dpdx.z-roadEdge1.z*dpdx.x)/determinantXZ;
    vec2 dbdy=vec2(dpdy.x*roadEdge2.z-dpdy.z*roadEdge2.x,
        roadEdge1.x*dpdy.z-roadEdge1.z*dpdy.x)/determinantXZ;
    vec2 uvDx=dbdx.x*duv1+dbdx.y*duv2;
    vec2 uvDy=dbdy.x*duv1+dbdy.y*duv2;
    int materialIndex=int(drawData.materialIndex);
    MaterialPayload material=materialDataBuffer.materials[materialIndex];
    vec3 albedo=material.albedo.rgb*textureGrad(globalPBRTextures[nonuniformEXT(materialIndex*5)],uv,uvDx,uvDy).rgb;
    vec3 sampledNormal=textureGrad(globalPBRTextures[nonuniformEXT(materialIndex*5+1)],uv,uvDx,uvDy).xyz*2.0-1.0;
    float roughness=material.roughness*textureGrad(globalPBRTextures[nonuniformEXT(materialIndex*5+3)],uv,uvDx,uvDy).r;

    // 道路 UV 的 U/V 朝向来自原三角形；法线基贴合实际接收地表。
    float determinantUV=duv1.x*duv2.y-duv1.y*duv2.x;
    if (abs(determinantUV)<1e-8) discard;
    vec3 directionU=(roadEdge1*duv2.y-roadEdge2*duv1.y)/determinantUV;
    vec3 directionV=(roadEdge2*duv1.x-roadEdge1*duv2.x)/determinantUV;
    vec3 normal=DecalSafeNormal(texelFetch(normalSampler,pixel,0).xyz,vec3(0,1,0));
    vec3 tangent=DecalSafeNormal(directionU-normal*dot(directionU,normal),vec3(1,0,0));
    vec3 bitangent=cross(normal,tangent);
    if (dot(bitangent,directionV)<0.0) bitangent=-bitangent;
    vec3 normalWS=DecalSafeNormal(mat3(tangent,bitangent,normal)*sampledNormal,normal);
    // V 是道路里程，可超过 1；只在道路左右边缘衰减。
    float edge=smoothstep(0.0,0.06,uv.x)*(1.0-smoothstep(0.94,1.0,uv.x));
    vec3 coverage=vec3(edge)*response;
    outDecalColor=vec4(clamp(albedo,0.0,1.0),coverage.x);
    outDecalNormal=vec4(normalWS,coverage.y);
    outDecalRoughness=vec4(clamp(roughness,0.045,1.0),0.0,0.0,coverage.z);
}
