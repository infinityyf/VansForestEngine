#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../BRDF/BRDFData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_input;
layout( location = 2 ) in vec3 position_world;
layout( set=2, binding=0 ) uniform sampler2D mainTexture;


//输出到MRT
layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outGBuffer0;
layout (location = 2) out vec4 outGBuffer1;

void main() 
{ 
    vec3 normal = normal_input;
    vec3 albedo = texture( mainTexture, frag_uv ).rgb;
    float roughness = roughness;
    float metallic = metallic;
    float ao = ao;
    vec3 fresnel0 = vec3(0.04);
    
    outNormal = vec4(normalize(normal), 1.0);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(metallic, ao, 0, 1.0);
}