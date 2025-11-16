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
    vec4 softShadowParams;
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

        // outside shadow map -> treat as lit
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;


    ivec2 sz = textureSize(shadowMap, 0);
    vec2 texelSize = 1.0 / vec2(sz);

    float visibility = 0.0;
    int count = 0;
    float sampleCountInverse = 1.0 / float(DISK_SAMPLE_COUNT);
    float blockSearchRadius = 5.0;

    //pcf radius
    float receiverDepth = clipCoord.z;
    float avgBlockerDepth = 0;
    // noise in range [-0.5, 0.5]
    float frameIndex = softShadowParams.x;
    float sampleJitterAngle = RandomInterLeavedWithScale(shadowUV * vec2(sz), frameIndex) * 2.0 * PI;
    vec2 jitter = vec2(sin(sampleJitterAngle), cos(sampleJitterAngle));

    //计算遮挡物距离
    for(int i = 0; i < DISK_SAMPLE_COUNT; ++i)
    {
        float sampleDistNorm = 0;
        vec2 offset = ComputeFibonacciSpiralDiskSampleClumped(i, sampleCountInverse, sampleDistNorm);
        //增加Temporal Jitter
        offset = vec2(offset.x * jitter.y + offset.y * jitter.x, offset.x * -jitter.x + offset.y * jitter.y);

        //搜索半径需要动态调整
        vec2 sampleCoord = shadowUV + offset * texelSize *blockSearchRadius;
        sampleCoord = clamp(sampleCoord, vec2(0.0), vec2(1.0));

        float texDepth = texture(shadowMap, sampleCoord).r;
        if(texDepth < receiverDepth)
        {
            avgBlockerDepth += texDepth;
            count++;
        }
    }
    if(count == 0)
    {
        avgBlockerDepth = receiverDepth;
    }
    else
    {
        avgBlockerDepth /= float(count);
    }

    //计算模糊半径
    float radius = (receiverDepth - avgBlockerDepth) / 0.05;
    radius = mix(1.0, 8.0, clamp(radius,0,1));

    
    for(int i = 0; i < DISK_SAMPLE_COUNT; ++i)
    {
        float sampleDistNorm = 0;
        vec2 offset = ComputeFibonacciSpiralDiskSampleClumped(i, sampleCountInverse, sampleDistNorm);
        //增加Temporal Jitter
        offset = vec2(offset.x * jitter.y + offset.y * jitter.x, offset.x * -jitter.x + offset.y * jitter.y);

        //搜索半径需要动态调整
        vec2 sampleCoord = shadowUV + offset * texelSize * radius;
        sampleCoord = clamp(sampleCoord, vec2(0.0), vec2(1.0));

        float texDepth = texture(shadowMap, sampleCoord).r;
        visibility += (texDepth < clipCoord.z) ? 0.0 : 1.0;
        count++;
    }

    return visibility / float(count);
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

void CalculateDirectDiffuse(vec3 positionWS, vec3 normalWS, sampler2D shadowMap, sampler2D punctualShadowMap, float sampleRadius,  inout vec3 diffuseResult)
{
    diffuseResult = vec3(0);

    vec3 T, B;
    BuildTBN(normalWS, T, B);

    int sampleCount = 8;
    uint n = uint(sampleCount);
    float invN = 1.0 / float(sampleCount);
    float rotAngle = 0;
    float c = cos(rotAngle), s = sin(rotAngle);

    // Limit effective area for stability
    float radius = min(sampleRadius, 10.0); // arbitrary max
    for (uint i = 0u; i < n; ++i)
    {
        vec2 d2 = DiskSample(i, n);              // in [0,1] radius
        d2 = vec2(d2.x * c - d2.y * s, d2.x * s + d2.y * c); // rotate
        vec3 samplePos = positionWS + (T * d2.x + B * d2.y) * radius;
        float ndl = max(dot(normalWS, uDirectionLight.direction.xyz), 0.0) / PI;
        float shadowV = SampleDirectionShadowMap(samplePos, shadowMap);
        diffuseResult += ndl * uDirectionLight.color.rgb * uDirectionLight.intensity * shadowV;

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
            float shadowValue = SamplePointShadowMap(positionWS, punctualShadowMap, int(pointLight.shadowIndex));
            attenuation = min(attenuation, shadowValue);

            float ndl = max(dot(normalWS, lightDirection), 0.0) / PI; //

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

            // // 计算阴影
            // float shadowValue = SampleSpotShadowMap(positionWS, punctualShadowMap, int(spotLight.shadowIndex));
            // attenuation = min(attenuation, shadowValue);

            
            float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
            if (coneAngle < cos(spotLight.outerConeAngle)) continue;

            float innerConeAngle = cos(spotLight.innerConeAngle);
            float outerConeAngle = cos(spotLight.outerConeAngle);
            float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

            float ndl = max(dot(normalWS, lightDirection), 0.0) / PI;//
            vec3 diffuse = vec3(0);
            diffuse = ndl * spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

            diffuseResult += diffuse;
        }
    }
    diffuseResult *= invN;
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
