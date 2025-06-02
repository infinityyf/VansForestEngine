struct DirectionLightData
{
    vec4 direction;
    vec4 color;
    float intensity;
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

