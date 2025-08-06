#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../BRDF/BRDFData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;
layout( set=2, binding=0 ) uniform sampler2D baseColor;
layout( set=2, binding=1 ) uniform sampler2D normalMap;
layout( set=2, binding=2 ) uniform sampler2D metalMap;
layout( set=2, binding=3 ) uniform sampler2D roughnessMap;
layout( set=2, binding=4 ) uniform sampler2D aoMap;

//输出到MRT
layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outGBuffer0;
layout (location = 2) out vec4 outGBuffer1;
layout (location = 3) out vec4 outGBuffer2;

void main() 
{ 
    vec3 normal_sample = texture(normalMap, frag_uv).rgb;
    normal_sample.rg = normal_sample.rg * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normal_sample);

    vec3 albedo = albedo.rgb * texture( baseColor, frag_uv ).rgb;
    float roughness = roughness * texture( roughnessMap, frag_uv ).r;
    float metallic = metallic * texture( metalMap, frag_uv ).r;
    float ao = ao * texture( aoMap, frag_uv ).r;
    vec3 fresnel0 = vec3(0.04);
    
    outNormal = vec4(normal, 1.0);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(metallic, ao, 0, 1.0);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    outGBuffer2 = vec4(position_world, -linearDepth);
}