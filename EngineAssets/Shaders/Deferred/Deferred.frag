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
layout(set = 2, binding = 1) uniform sampler2D shadowMap;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

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
    brdfData.ao = ssaoValue;
    brdfData.fresnel0 = vec3(0.04);
    brdfData.viewDirection = viewDirection;
    brdfData.positionWS = position_world;
    

    //计算光照
    LightResult lightResult;
    CalculateDirectLight(brdfData,shadowMap, lightResult);


    AmbientBRDF(brdfData,viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);

    outColor.rgb = lightResult.directDiffuse + lightResult.directSpecular;
    outColor.rgb += lightResult.ambientDiffuse  + lightResult.ambientSpecular;
    outColor.a = 1;
}