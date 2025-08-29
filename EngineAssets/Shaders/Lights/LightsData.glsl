#include "../Common/Common.glsl"
#include "../BRDF/BRDFData.glsl"

struct DirectionLightData
{
    vec4 direction;
    vec4 color;
    float intensity;
    mat4x4 shadowMatrix;
};

struct PointLightData
{
    vec4 position;
    vec4 color;
    float intensity;
    float radius;
    float shadowIndex;
    mat4x4 shadowMatrix[6];
};

struct SpotLightData
{
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float radius;
    float innerConeAngle;
    float outerConeAngle;
    mat4x4 shadowMatrix;
    float shadowIndex;
};

struct LightResult
{
    vec3 directDiffuse;
    vec3 directSpecular;
    vec3 ambientDiffuse;
    vec3 ambientSpecular;
};

#define MAX_DIRECTION_LIGHTS 1
#define MAX_POINT_LIGHTS 10
#define MAX_SPOT_LIGHTS 10

#if !defined(LightCBBind)
    #define LightCBBind 3
#endif
layout(set=LightCBBind, binding=0) uniform LightsData
{
    uint uPointLightCount;
    uint uSpotLightCount;
    uint uShadowAtlasSize;
    uint uShadowAtlasCount;
    DirectionLightData uDirectionLight;
    PointLightData uPointLights[MAX_POINT_LIGHTS];
    SpotLightData uSpotLights[MAX_SPOT_LIGHTS];
};

PointLightData GetPointLight(int index)
{
    return uPointLights[index];
}

SpotLightData GetSpotLight(int index)
{
    return uSpotLights[index];
}

int GetCubemapFaceIndex(vec3 dir)
{
    vec3 absDir = abs(dir);
    int face = 0;
    if (absDir.x > absDir.y && absDir.x > absDir.z)
        face = dir.x > 0.0 ? 0 : 1; // +X : -X
    else if (absDir.y > absDir.z)
        face = dir.y > 0.0 ? 2 : 3; // +Y : -Y
    else
        face = dir.z > 0.0 ? 4 : 5; // +Z : -Z
    return face;
}


float SampleDirectionShadowMap(vec3 position_world, sampler2D shadowMap)
{
    vec4 clipCoord = uDirectionLight.shadowMatrix * vec4(position_world, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    clipCoord.z -= DEPTH_BIAS;
    vec2 shadowUV = clipCoord.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;
    float shadowMapDepth = texture(shadowMap, shadowUV).r;
    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

float SampleDirectionShadowMap_ESM(vec3 position_world, sampler2D shadowMap)
{
    vec4 clipCoord = uDirectionLight.shadowMatrix * vec4(position_world, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    vec2 shadowUV = clipCoord.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    float receiverDepth = clipCoord.z - DEPTH_BIAS;
    float esmShadow = texture(shadowMap, shadowUV).r;

    // ESM 阴影公式
    float visibility = clamp(esmShadow * exp(-ESM_C * receiverDepth), 0.0, 1.0);
    // 可选：softness 调整
    // visibility = pow(visibility, softness);

    return visibility;
}

float SampleDirectionShadowMap_PCF_Noise(vec3 position_world, sampler2D shadowMap)
{
    vec4 clipCoord = uDirectionLight.shadowMatrix * vec4(position_world, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    clipCoord.z -= DEPTH_BIAS;
    vec2 shadowUV = clipCoord.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    float shadow = 0.0;
    float samples = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    // 随机偏移（可用屏幕坐标或shadowUV作为种子）
    float noise = RandomInterLeaved(shadowUV * 100.0);

    // 3x3 PCF + 随机扰动
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            // 每个采样点加一点噪声扰动
            vec2 offset = vec2(x, y) + noise;
            float shadowMapDepth = texture(shadowMap, shadowUV + offset * texelSize).r;
            shadow += shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
            samples += 1.0;
        }
    }
    return shadow / samples;
}

float SamplePointShadowMap(vec3 position_world, sampler2D shadowMap, int shadowIndex)
{
    vec3 direction = position_world - uPointLights[shadowIndex].position.xyz;

    //获取采样的方向
    int shadowDirectionIndex = GetCubemapFaceIndex(direction);

    ivec2 shadowOffset = ivec2((shadowIndex * 6 + shadowDirectionIndex) % uShadowAtlasCount, (shadowIndex * 6 + shadowDirectionIndex) / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uPointLights[shadowIndex].shadowMatrix[shadowDirectionIndex];
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord/=  clipCoord.w;
    clipCoord.xy  = clipCoord.xy * 0.5 + 0.5;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);

    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset,0).r;

    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

float SampleSpotShadowMap(vec3 position_world, sampler2D shadowMap, int shadowIndex)
{
    int pointLightCount = int(uPointLightCount);
    ivec2 shadowOffset = ivec2((pointLightCount * 6 + shadowIndex) % uShadowAtlasCount, (pointLightCount * 6 + shadowIndex) / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uSpotLights[shadowIndex].shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord/=  clipCoord.w;
    clipCoord.xy  = clipCoord.xy * 0.5 + 0.5;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);

    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset,0).r;

    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

void CalculateDirectDiffuse(vec3 positionWS, vec3 normalWS, sampler2D shadowMap, sampler2D punctualShadowMap,  inout vec3 diffuseResult)
{
    diffuseResult = vec3(0);

    float ndl = max(dot(normalWS, uDirectionLight.direction.xyz), 0.0) / PI; //
    diffuseResult = ndl * uDirectionLight.color.rgb * uDirectionLight.intensity;
    float shadowValue = SampleDirectionShadowMap(positionWS, shadowMap);
    diffuseResult *= shadowValue;

    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        // 计算阴影
        shadowValue = SamplePointShadowMap(positionWS, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        ndl = max(dot(normalWS, lightDirection), 0.0) / PI; //

        vec3 diffuse = vec3(0);
        diffuse = ndl * pointLight.color.rgb * pointLight.intensity * attenuation;
        diffuseResult += diffuse;
    }

    //聚光灯计算
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        // 计算阴影
        float shadowValue = SampleSpotShadowMap(positionWS, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        
        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        ndl = max(dot(normalWS, lightDirection), 0.0) / PI;//
        vec3 diffuse = vec3(0);
        diffuse = ndl * spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        diffuseResult += diffuse;
    }
}

void CalculateDirectLight(BRDFData brdfData, sampler2D shadowMap, sampler2D punctualShadowMap, inout LightResult lightResult)
{
    lightResult.directDiffuse = vec3(0);
    lightResult.directSpecular = vec3(0);

    //平行光计算
    vec3 diffuseResult = vec3(0);
    vec3 specularResult = vec3(0);
    DirectBRDF(brdfData, uDirectionLight.direction.rgb ,diffuseResult,specularResult);
    diffuseResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;

    float shadowValue = SampleDirectionShadowMap_PCF_Noise(brdfData.positionWS, shadowMap);
    diffuseResult *= shadowValue;
    specularResult *= shadowValue;

    lightResult.directDiffuse += diffuseResult;
    lightResult.directSpecular += specularResult;

    //点光源计算
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        // 计算阴影
        float shadowValue = SamplePointShadowMap(brdfData.positionWS, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        vec3 diffuseResult = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult *= pointLight.color.rgb * pointLight.intensity * attenuation;
        specularResult *= pointLight.color.rgb * pointLight.intensity * attenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }

    //聚光灯计算
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        // 计算阴影
        float shadowValue = SampleSpotShadowMap(brdfData.positionWS, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 diffuseResult = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        specularResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }
}
