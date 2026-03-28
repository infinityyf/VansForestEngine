#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Lights/LightsData.glsl"
#include "../BRDF/BRDFData.glsl"
#include "../BRDF/BRDFSkin.glsl"
#include "../BRDF/BRDFCloth.glsl"
#include "../BRDF/BRDFHair.glsl"
#include "../BRDF/BRDFSubsurface.glsl"
#include "../Common/CameraData.glsl"

layout(set = 1, binding = 0, input_attachment_index = 0) uniform subpassInput normalInput;
layout(set = 1, binding = 1, input_attachment_index = 1) uniform subpassInput gbufferInput0;
layout(set = 1, binding = 2, input_attachment_index = 2) uniform subpassInput gbufferInput1;
layout(set = 1, binding = 3, input_attachment_index = 3) uniform subpassInput gbufferInput2;
layout(set = 1, binding = 4, input_attachment_index = 4) uniform subpassInput depthInput;

layout(set = 1, binding = 5, rgba32f ) uniform image2D ssao;
layout(set = 1, binding = 6) uniform sampler2D ssgi;
layout(set = 1, binding = 7, rgba32f ) uniform image2D ssr;
layout(set = 1, binding = 8) uniform sampler2DArray cascadeShadowMap;
layout(set = 1, binding = 9) uniform sampler2D punctualShadowMap;
// R通道球谐
layout( set = 1, binding = 10 ) uniform sampler3D SHRCoeff;
// B通道球谐
layout( set = 1, binding = 11 ) uniform sampler3D SHGCoeff;
// B通道球谐
layout( set = 1, binding = 12 ) uniform sampler3D SHBCoeff;
layout( set = 1, binding = 13 ) uniform sampler2D fogResult;

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

vec3 CalculateSHDiffuse(vec3 position_world, vec3 normal)
{
    vec3 inDirectDiffuse = vec3(0);
    vec3 uvw = (position_world + normal * 0.5 * 0.5 - vec3(-20,-20 + 6,-20)) / vec3(40,40,40);

    vec4 rCoeff = texture(SHRCoeff, uvw);
    vec4 gCoeff = texture(SHGCoeff, uvw);
    vec4 bCoeff = texture(SHBCoeff, uvw);
    vec3 tempDiffuse = vec3(0);
    for(int i = 0; i < 4; i++)
    {
        float basis = SHBasis(i, normal);
        tempDiffuse.r += rCoeff[i] * basis / PI;
        tempDiffuse.g += gCoeff[i] * basis / PI;
        tempDiffuse.b += bCoeff[i] * basis / PI;
    }

    // inDirectDiffuse = (sumW > 1e-6) ? (inDirectDiffuse / sumW) : vec3(0.0);
    inDirectDiffuse = max(tempDiffuse, vec3(0.0));
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
    float linearDepth = subpassLoad(gbufferInput2).w;


    //获取ssao
    float ssaoValue = imageLoad(ssao,ivec2(fragTexCoord * ScreenParams.xy / 2)).r;//texture(ssao, fragTexCoord).r;

    vec3 viewDirection = normalize(cameraPosition.xyz - position_world);

    //材质属性
    BRDFData brdfData;
    brdfData.normal = normal;
    brdfData.albedo = color.rgb;
    brdfData.roughness = roughness;
    brdfData.metallic = metallic;
    brdfData.ao = min(ao, ssaoValue);
    brdfData.ao = pow(brdfData.ao, 2.0);
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
    brdfData.indirectDiffuse = texture(ssgi, lastFrameUV).rgb;
    //b : 计算球谐
    //brdfData.indirectDiffuse = SampleSHColor(normal);
    //c : 计算动态GI，探针球谐
    //brdfData.indirectDiffuse = CalculateSHDiffuse(position_world, normal);

    brdfData.indirectSpecular = imageLoad(ssr,ivec2(lastFrameUV * ScreenParams.xy)).rgba;
    
    //计算光照
    LightResult lightResult;
    lightResult.directDiffuse = vec3(0);
    lightResult.directSpecular = vec3(0);
    lightResult.ambientDiffuse = vec3(0);
    lightResult.ambientSpecular = vec3(0);

    int matID = int(round(materialID));
    if (matID == MATERIAL_ID_SKIN)
    {
        // --- Skin BRDF path ---
        // Curvature was stored in normalInput.w by UnlitSkin.frag
        float curvature = subpassLoad(normalInput).w;
        CalculateDirectLight_Skin(brdfData, curvature, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Skin(brdfData, viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_CLOTH)
    {
        // --- Cloth BRDF path ---
        // brdfData.roughness holds sheenRoughness (written into outGBuffer0.w by Cloth.frag)
        // Direct lighting uses the per-light cloth light loop
        CalculateDirectLight_Cloth(brdfData, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        // Ambient: ClothBRDFLUT .b channel used as the specular environment term
        AmbientBRDF_Cloth(brdfData, viewDirection,
                          lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_HAIR)
    {
        // --- Hair BRDF path (Marschner R / TT / TRT) ---
        // Hair uses softer AO: the global pow(2.0) is too aggressive for
        // thin translucent card geometry.  Re-apply with gentler exponent.
        brdfData.ao = pow(min(ao, ssaoValue), 1.0);

        HairBRDFParams hair;
        hair.roughness         = brdfData.roughness;
        hair.specularStrength  = 1.0;   // constant (not stored in GBuffer)
        hair.scatter           = 0.35;  // constant (not stored in GBuffer)
        hair.shift             = subpassLoad(normalInput).w * 2.0 - 1.0;
        hair.flowBend          = 0.0;   // flow already baked into GBuffer tangent; set >0 to amplify shift

        // Decode hair fiber tangent from octahedral encoding in GBuffer1.x / .w
        vec2 octT = vec2(metallic, subpassLoad(gbufferInput1).w) * 2.0 - 1.0;
        hair.tangentWS = OctDecodeHair(octT);

        CalculateDirectLight_Hair(brdfData, hair, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Hair(brdfData, hair, viewDirection,
                         lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_SUBSURFACE)
    {
        // --- Subsurface Scattering BRDF path ---
        // Thickness was stored in normalInput.w by Subsurface.frag
        float thickness = subpassLoad(normalInput).w;
        // SubsurfacePower was packed into GBuffer1.x as (power / 50.0)
        float subsurfacePowerPacked = metallic;  // reuses the metallic slot
        float subsurfacePower = subsurfacePowerPacked * 50.0;

        SubsurfaceParams sss;
        sss.thickness      = thickness;
        sss.subsurfacePower = subsurfacePower;
        sss.subsurfaceColor = vec3(1.0, 0.2, 0.1); // warm reddish scatter tint (default)

        CalculateDirectLight_Subsurface(brdfData, sss, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Subsurface(brdfData, sss, viewDirection,
                               lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else
    {
        // --- Default PBR path ---
        CalculateDirectLight(brdfData, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF(brdfData, viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }

    outColor.rgb = lightResult.directDiffuse + lightResult.directSpecular;
    outColor.rgb += lightResult.ambientDiffuse + lightResult.ambientSpecular;
    //outColor.rgb = lightResult.ambientSpecular;
    //混合雾效  fogResult: rgb = in-scatter, a = opacity (1 - transmittance)
    vec4 fogData = texture(fogResult, fragTexCoord);
    float fogOpacity = fogData.a;
    outColor.rgb = outColor.rgb * (1.0 - fogOpacity) + fogData.rgb;
    //outColor.rgb = fogData.rgb * fogOpacity;
    //outColor.rgb = brdfData.indirectDiffuse;
    //outColor.rgb = CalculateSHDiffuse(position_world, normal);
    outColor.a = 1;
}