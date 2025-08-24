#version 450
#extension GL_GOOGLE_include_directive : require
#define CameraCBBind 0
#define LightCBBind 3
#define PBRLutSetBind 4

#include "../Lights/LightsData.glsl"
#include "../BRDF/BRDFData.glsl"
#include "../Common/CameraData.glsl"

layout(set = 1, binding = 0, input_attachment_index = 0) uniform subpassInput normalInput;
layout(set = 1, binding = 1, input_attachment_index = 1) uniform subpassInput gbufferInput0;
layout(set = 1, binding = 2, input_attachment_index = 2) uniform subpassInput gbufferInput1;
layout(set = 1, binding = 3, input_attachment_index = 3) uniform subpassInput gbufferInput2;
layout(set = 1, binding = 4, input_attachment_index = 4) uniform subpassInput depthInput;

layout(set = 2, binding = 0, rgba32f ) uniform image2D ssao;
layout(set = 2, binding = 1, rgba32f ) uniform image2D ssgi;
layout(set = 2, binding = 2, rgba32f ) uniform image2D ssr;
layout(set = 2, binding = 3) uniform sampler2D shadowMap;
layout(set = 2, binding = 4) uniform sampler2D punctualShadowMap;
// R通道球谐
layout( set = 2, binding = 5 ) uniform sampler3D SHRCoeff;
// B通道球谐
layout( set = 2, binding = 6 ) uniform sampler3D SHGCoeff;
// B通道球谐
layout( set = 2, binding = 7 ) uniform sampler3D SHBCoeff;


layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

vec3 SampleSHColor(vec3 dir) 
{
    vec3 v = normalize(dir);
    vec3 color = vec3(0.0);

    for(int i = 0; i < 9; i++) 
    {
        float basis = SHBasis(i, v);
        color += vec3(shCoefficients[i * 3 + 0],shCoefficients[i * 3 + 1],shCoefficients[i * 3 + 2]) * basis;
    }
    return color;
}

vec3 CalculateSHDiffuse(vec3 uvw, vec3 normal)
{
    vec3 inDirectDiffuse = vec3(0);
    //获取球谐系数还原颜色
    vec4 rCoeff = texture(SHRCoeff, uvw);
    vec4 gCoeff = texture(SHGCoeff, uvw); 
    vec4 bCoeff = texture(SHBCoeff, uvw);
    for(int i = 0; i < 4; i++) 
    {
        float basis = SHBasis(i, normal);
        inDirectDiffuse.r += rCoeff[i] * basis;
        inDirectDiffuse.g += gCoeff[i] * basis;
        inDirectDiffuse.b += bCoeff[i] * basis;
    }
    return inDirectDiffuse;

}

void main() 
{
    vec3 normal = subpassLoad(normalInput).xyz;
    vec3 color = subpassLoad(gbufferInput0).xyz;
    float roughness = subpassLoad(gbufferInput0).w;
    float metallic = subpassLoad(gbufferInput1).x;
    float ao = subpassLoad(gbufferInput1).y;
    float materialID = subpassLoad(gbufferInput1).z;
    vec3 position_world = subpassLoad(gbufferInput2).xyz;
    float depth = subpassLoad(depthInput).x;

    //获取ssao
    float ssaoValue = imageLoad(ssao,ivec2(fragTexCoord * ScreenParams.xy)).r;//texture(ssao, fragTexCoord).r;

    vec3 viewDirection = normalize(cameraPosition.xyz - position_world);

    //材质属性
    BRDFData brdfData;
    brdfData.normal = normal;
    brdfData.albedo = color.rgb;
    brdfData.roughness = roughness;
    brdfData.metallic = metallic;
    brdfData.ao = min(ao, ssaoValue);
    brdfData.fresnel0 = vec3(0.04);
    brdfData.viewDirection = viewDirection;
    brdfData.positionWS = position_world;
    
    //remap to last frame screen space
    vec4 lastFrameClip = LastProjectionMatrix * LastViewMatrix * vec4(position_world, 1.0);
    lastFrameClip /= lastFrameClip.w;
    lastFrameClip.y = -lastFrameClip.y; // flip y for screen space
    vec2 lastFrameUV = (lastFrameClip.xy + 1.0) * 0.5;
    //indirect diffuse
    //a : brdfData.indirectDiffuse = texture(PreConvDiffuseEnvironment, normal).rgb;
    //gi 使用半分辨率
    //brdfData.indirectDiffuse = imageLoad(ssgi,ivec2(lastFrameUV * ScreenParams.xy / 4)).rgb;
    //b : 计算球谐
    //brdfData.indirectDiffuse = SampleSHColor(normal);
    //c : 计算动态GI，探针球谐
    vec3 probeUVW = (position_world - vec3(-20,-20,-20)) / vec3(40,40,40);
    brdfData.indirectDiffuse = CalculateSHDiffuse(probeUVW, normal);

    brdfData.indirectSpecular = imageLoad(ssr,ivec2(lastFrameUV * ScreenParams.xy / 2)).rgba;
    

    //计算光照
    LightResult lightResult;
    CalculateDirectLight(brdfData,shadowMap,punctualShadowMap, lightResult);


    AmbientBRDF(brdfData,viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);

    outColor.rgb = lightResult.directDiffuse + lightResult.directSpecular;
    outColor.rgb += lightResult.ambientDiffuse + lightResult.ambientSpecular;
    outColor.rgb = brdfData.indirectDiffuse;
    outColor.a = 1;
}