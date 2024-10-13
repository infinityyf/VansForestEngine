#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Lights/LightsData.glsl"
#include "../BRDF/BRDFData.glsl"
#include "../Common/CameraData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_input;
layout( set=1, binding=0 ) uniform sampler2D mainTexture;
layout( location = 0 ) out vec4 frag_color;
void main() 
{ 
    //材质属性
    BRDFData brdfData;
    brdfData.normal = normal_input;
    brdfData.albedo = texture( mainTexture, frag_uv ).rgb;
    brdfData.roughness = roughness;
    brdfData.metallic = metallic;
    brdfData.ao = ao;
    brdfData.fresnel0 = vec3(0.04);

    //计算光照
    LightResult lightResult;
    DirectBRDF(brdfData,-GetDirectionLight(0).direction.rgb, -cameraDirection.rgb,lightResult.directDiffuse,lightResult.directSpecular);

    AmbientBRDF(brdfData,-cameraDirection.rgb, lightResult.ambientDiffuse, lightResult.ambientSpecular);

    frag_color.rgb = lightResult.directDiffuse * GetDirectionLight(0).color.rgb + lightResult.directSpecular;// texture( mainTexture, frag_uv0 );
    frag_color.rgb = lightResult.ambientDiffuse + lightResult.ambientSpecular;
    frag_color.a = 1;
}