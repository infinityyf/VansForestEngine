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
};

struct LightResult
{
    vec3 directDiffuse;
    vec3 directSpecular;
    vec3 ambientDiffuse;
    vec3 ambientSpecular;
};

#define MAX_DIRECTION_LIGHTS 4
#define MAX_POINT_LIGHTS 10
#define MAX_SPOT_LIGHTS 10

#if !defined(LightCBBind)
    #define LightCBBind 3
#endif
layout(set=LightCBBind, binding=0) uniform LightsData
{
    DirectionLightData uDirectionLights[MAX_DIRECTION_LIGHTS];
    PointLightData uPointLights[MAX_POINT_LIGHTS];
    SpotLightData uSpotLights[MAX_SPOT_LIGHTS];
};


DirectionLightData GetDirectionLight(int index)
{
    return uDirectionLights[index];
}

PointLightData GetPointLight(int index)
{
    return uPointLights[index];
}

SpotLightData GetSpotLight(int index)
{
    return uSpotLights[index];
}

float SampleShadowMap(vec3 position_world, sampler2D shadowMap)
{
    vec4 clipCoord = GetDirectionLight(0).shadowMatrix * vec4(position_world, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    vec2 shadowUV = clipCoord.xy * 0.5 + 0.5;
    float shadowMapDepth = texture(shadowMap, shadowUV).r;
    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}
