#version 450
#extension GL_GOOGLE_include_directive : require

// TileLight：先引入 CameraData（提供 ScreenParams），再定义 TILE_LIGHT，再引入 TileLightData
#include "../Common/CameraData.glsl"
#define TILE_LIGHT
#include "../Common/TileLightData.glsl"

#include "../Lights/LightsData.glsl"
#include "../BRDF/BRDFData.glsl"
// 面光源发光贴图数组：最多 32 层，每层 256×256，完整 mip（在 RectLightLTC.glsl 引入前声明）。
#define RECT_LIGHT_EMISSIVE_ENABLED
layout( set = 1, binding = 15 ) uniform sampler2DArray rectLightEmissive;
#include "../Lighting/RectLightLTC.glsl"
#include "../BRDF/BRDFSkin.glsl"
#include "../BRDF/BRDFCloth.glsl"
#include "../BRDF/BRDFHair.glsl"
#include "../BRDF/BRDFSubsurface.glsl"
#include "../BRDF/BRDFVegetation.glsl"
#include "../Common/CameraData.glsl"

layout(set = 1, binding = 0) uniform sampler2D normalInput;
layout(set = 1, binding = 1) uniform sampler2D gbufferInput0;
layout(set = 1, binding = 2) uniform sampler2D gbufferInput1;
layout(set = 1, binding = 3) uniform sampler2D gbufferInput2;
layout(set = 1, binding = 4) uniform sampler2D depthInput;

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
layout( set = 1, binding = 14 ) uniform sampler3D giVisibility;

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

// GI probe volume parameters (must match GIPointLight.comp / GIVisibility.comp)
#define GI_ORIGIN   vec3(-20.0, -14.0, -20.0)
#define GI_SIZE     vec3(40.0, 40.0, 40.0)

// Sample the GI probe visibility volume (SH L0+L1, 4 coefficients).
// Returns vec4 of SH coefficients encoding directional sky visibility.
// Use EvalGIVisibility(shVis, direction) to get scalar visibility for a specific direction.
// Outside the GI volume: returns full-sky SH (L0 = 1.0 / Y00, L1 = 0).
vec4 SampleGIVisibilitySH(vec3 positionWS)
{
    const vec4 k_fullSky = vec4(3.5449077, 0.0, 0.0, 0.0); // EvalGIVisibility = 1.0

    vec3 uvw = (positionWS - GI_ORIGIN) / GI_SIZE;

    // Signed distance to the nearest volume face in UVW space; negative = outside
    vec3 edgeDist = min(uvw, vec3(1.0) - uvw);
    float boundaryDist = min(min(edgeDist.x, edgeDist.y), edgeDist.z);

    if (boundaryDist < 0.0)
        return k_fullSky;

    // Smoothly fade to full-sky within the outer 5% of the volume to avoid hard edges
    const float kFadeMargin = 0.05;
    float fade = smoothstep(0.0, kFadeMargin, boundaryDist);

    return mix(k_fullSky, texture(giVisibility, uvw), fade);
}

void main() 
{
    vec4 normalData = texture(normalInput, fragTexCoord);
    vec4 gbufferData0 = texture(gbufferInput0, fragTexCoord);
    vec4 gbufferData1 = texture(gbufferInput1, fragTexCoord);
    vec4 gbufferData2 = texture(gbufferInput2, fragTexCoord);
    vec4 depthData = texture(depthInput, fragTexCoord);

    vec3 normal = normalData.xyz;
    vec3 color = gbufferData0.xyz;
    float roughness = gbufferData0.w;
    float metallic = gbufferData1.x;
    float ao = gbufferData1.y;
    float materialID = gbufferData1.z;
    vec3 position_world = gbufferData2.xyz;
    float depth = depthData.x;
    float linearDepth = gbufferData2.w;


    //获取ssao：这里先使用原始半分辨率结果的安全采样，避免深度加权上采样把 AO 错误压黑。
    ivec2 ssaoSize = imageSize(ssao);
    ivec2 ssaoCoord = clamp(ivec2(fragTexCoord * vec2(ssaoSize)), ivec2(0), ssaoSize - 1);
    float ssaoValue = imageLoad(ssao, ssaoCoord).r;

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
    
    // indirect diffuse — SSGI temporal pass already accumulates and aligns to
    // current frame UV via motion vectors; sample at fragTexCoord.
    // GBuffer / SSR / Fog are produced from current-frame inputs after the RenderPass split.
    // and blurring edges that the temporal pass already correctly resolved.
    brdfData.indirectDiffuse = texture(ssgi, fragTexCoord).rgb;
    //b : 计算球谐
    //brdfData.indirectDiffuse = SampleSHColor(normal);
    //c : 计算动态GI，探针球谐
    //brdfData.indirectDiffuse = CalculateSHDiffuse(position_world, normal);

    brdfData.indirectSpecular = imageLoad(ssr,ivec2(fragTexCoord * ScreenParams.xy)).rgba;
    
    //计算光照
    LightResult lightResult;
    lightResult.directDiffuse = vec3(0);
    lightResult.directSpecular = vec3(0);
    lightResult.ambientDiffuse = vec3(0);
    lightResult.ambientSpecular = vec3(0);

    // Sample GI probe directional sky visibility (SH L0+L1)
    vec4 giVisSH = SampleGIVisibilitySH(position_world);

    int matID = int(round(materialID));
    if (matID == MATERIAL_ID_SKIN)
    {
        // --- Skin BRDF path ---
        // Curvature was stored in normalInput.w by UnlitSkin.frag
        float curvature = normalData.w;
        CalculateDirectLight_Skin(brdfData, curvature, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Skin(brdfData, viewDirection, giVisSH, lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_CLOTH)
    {
        // --- Cloth BRDF path ---
        // brdfData.roughness holds sheenRoughness (written into outGBuffer0.w by Cloth.frag)
        // Direct lighting uses the per-light cloth light loop
        CalculateDirectLight_Cloth(brdfData, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        // Ambient: ClothBRDFLUT .b channel used as the specular environment term
        AmbientBRDF_Cloth(brdfData, viewDirection, giVisSH,
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
        hair.shift             = normalData.w * 2.0 - 1.0;
        hair.flowBend          = 0.0;   // flow already baked into GBuffer tangent; set >0 to amplify shift

        // Decode hair fiber tangent from octahedral encoding in GBuffer1.x / .w
        vec2 octT = vec2(metallic, gbufferData1.w) * 2.0 - 1.0;
        hair.tangentWS = OctDecodeHair(octT);

        CalculateDirectLight_Hair(brdfData, hair, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Hair(brdfData, hair, viewDirection, giVisSH,
                         lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_SUBSURFACE)
    {
        // --- Subsurface Scattering BRDF path ---
        // Thickness was stored in normalInput.w by Subsurface.frag
        float thickness = normalData.w;
        // SubsurfacePower was packed into GBuffer1.x as (power / 50.0)
        float subsurfacePowerPacked = metallic;  // reuses the metallic slot
        float subsurfacePower = subsurfacePowerPacked * 50.0;

        SubsurfaceParams sss;
        sss.thickness      = thickness;
        sss.subsurfacePower = subsurfacePower;
        sss.subsurfaceColor = vec3(1.0, 0.2, 0.1); // warm reddish scatter tint (default)

        CalculateDirectLight_Subsurface(brdfData, sss, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Subsurface(brdfData, sss, viewDirection, giVisSH,
                               lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_GRASS)
    {
        // --- Vegetation / Grass BRDF path ---
        // Translucency was stored in normalInput.w by Grass.frag
        float translucency = normalData.w;

        // Grass AO — match the default PBR path's aggressive power curve
        brdfData.ao = pow(min(ao, ssaoValue), 2.0);

        VegetationParams veg;
        veg.translucency   = translucency;
        veg.scatterWidth   = 0.45;    // wrap diffuse width
        veg.sssDistortion  = 0.3;     // normal distortion (unused in new scatter)
        veg.sssAmbient     = 0.05;    // very low constant backlight
        veg.sssPower        = 14.0;   // high exponent = narrow forward-scatter cone

        CalculateDirectLight_Vegetation(brdfData, veg, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF_Vegetation(brdfData, viewDirection, giVisSH,
                               lightResult.ambientDiffuse, lightResult.ambientSpecular);
        lightResult.ambientSpecular = vec3(0.0); // grass blades: no ambient specular
    }
    else if (matID == MATERIAL_ID_EMISSIVE)
    {
        // --- Emissive 直通路径 ---
        // GBuffer0.w (roughness 插槽) 存储了发光强度，直接输出 albedo × intensity
        // 跳过全部 BRDF / 直接光照 / 阴影 / 环境光计算
        float emissiveIntensity   = roughness;             // GBuffer0.w = intensity
        lightResult.directDiffuse = color.rgb * emissiveIntensity;
        // directSpecular / ambientDiffuse / ambientSpecular 保持 vec3(0)
    }
    else
    {
        // --- Default PBR path ---
        CalculateDirectLight(brdfData, cascadeShadowMap, linearDepth, punctualShadowMap, lightResult);
        AmbientBRDF(brdfData, viewDirection, giVisSH, lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }

    outColor.rgb = lightResult.directDiffuse + lightResult.directSpecular;
    outColor.rgb += lightResult.ambientDiffuse + lightResult.ambientSpecular;
    //outColor.rgb = lightResult.ambientSpecular;
    //混合雾效  fogResult: rgb = in-scatter, a = opacity (1 - transmittance)
    // fogResult 由当前帧 GBuffer / 体积雾流程生成，Deferred 合成时直接按当前 UV 采样。
    vec4 fogData = texture(fogResult, fragTexCoord);
    float fogOpacity = fogData.a;
    outColor.rgb = outColor.rgb * (1.0 - fogOpacity) + fogData.rgb;
    //outColor.rgb = vec3(brdfData.ao,brdfData.ao,brdfData.ao);
    //outColor.rgb = lightResult.ambientSpecular;
    //outColor.rgb = CalculateSHDiffuse(position_world, normal);
    outColor.a = 1;
}