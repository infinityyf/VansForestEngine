#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"
#include "../../Common/MotionVector.glsl"
#include "../GrassDrawData.glsl"
#include "../GrassSurfaceNormal.glsl"

layout(location=0) in vec2 frag_uv;
layout(location=1) in vec3 normal_ws;
layout(location=2) in vec4 current_clip;
layout(location=3) in vec4 previous_clip;
layout(location=4) in vec3 position_world;
layout(set=4,binding=0) uniform sampler2D grassAlbedo;
layout(set=4,binding=1) uniform sampler2D grassNormal;
layout(set=4,binding=2) uniform sampler2D grassRoughness;
layout(set=4,binding=3) uniform sampler2D grassTranslucency;
layout(set=4,binding=4) uniform sampler2D grassAO;

layout(location=0) out vec4 outNormal;
layout(location=1) out vec4 outGBuffer0;
// Grass 专用通道：非金属、材质微结构 AO、材质类型、间接漫反射强度。
layout(location=2) out vec4 outGBuffer1;
layout(location=3) out vec4 outGBuffer2;
layout(location=4) out vec2 outMotionVector;

void main()
{
    vec4 color=texture(grassAlbedo,frag_uv,MaterialMipBias);
    // 导数在 discard 之前计算，保持 helper invocation 的切线和高光抗锯齿有效。
    vec3 dpdx=dFdx(position_world),dpdy=dFdy(position_world);
    mat3 frame=GrassTangentFrame(normal_ws,dpdx,dpdy,dFdx(frag_uv),dFdy(frag_uv));
    bool backFace=GrassIsBackFace(frame[2],dpdx,dpdy,cameraPosition.xyz-position_world);
    vec3 N=GrassMappedNormal(frame,texture(grassNormal,frag_uv,MaterialMipBias).rgb,pc.normalStrength,backFace);
    float roughness=clamp(texture(grassRoughness,frag_uv,MaterialMipBias).r,0.045,1.0);
    // 法线方差进入微表面宽度；保持远处成簇高光，避免窄亮点闪烁。
    float variance=0.5*(dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N)));
    roughness=sqrt(clamp(roughness*roughness+min(variance,0.18),0.002025,1.0));
    if(color.a<0.5) discard;
    float microAO=mix(1.0,clamp(texture(grassAO,frag_uv,MaterialMipBias).r,0.0,1.0),pc.aoStrength);
    float transmission=clamp(texture(grassTranslucency,frag_uv,MaterialMipBias).r*pc.transmissionStrength,0.0,1.0);
    outNormal=vec4(N,transmission);
    outGBuffer0=vec4(color.rgb,roughness);
    outGBuffer1=vec4(0.0,microAO,float(MATERIAL_ID_GRASS),pc.indirectDiffuseStrength);
    outGBuffer2=vec4(position_world,-(ViewMatrix*vec4(position_world,1.0)).z);
    outMotionVector=VansMotionVectorFromClip(current_clip,previous_clip);
}
