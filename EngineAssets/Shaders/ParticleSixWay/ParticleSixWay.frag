#version 450
#extension GL_GOOGLE_include_directive : require

#define TILE_LIGHT
#include "../Common/CameraData.glsl"
#include "../Common/TileLightData.glsl"
#include "../Lights/LightsData.glsl"

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragBillboardRight;
layout(location = 4) in vec3 fragBillboardUp;
layout(location = 5) in vec3 fragBillboardForward;

// Unity/EmberGen style six-way packing:
// positive RGB = right, top, back; A = alpha
// negative RGB = left, bottom, front; A = emissive
layout(set = 1, binding = 0) uniform sampler2D positiveAxesTex;
layout(set = 1, binding = 1) uniform sampler2D negativeAxesTex;
layout(set = 1, binding = 2) uniform sampler2DArray cascadeShadowMap;
layout(set = 1, binding = 3) uniform sampler2DShadow punctualShadowMap;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ParticleSixWayPushConst
{
    vec4 spriteSheetParams;
    vec4 sixWayParams0; // x: direct, y: GI/ambient, z: emissive, w: absorption
    vec4 sixWayParams1; // x: remapMin, y: remapMax, z: alphaCutoff, w: debugMode
    vec4 mainLightDirAndPad;
    vec4 mainLightColor;
} pushConst;

struct SixWaySample
{
    float right;
    float top;
    float back;
    float left;
    float bottom;
    float front;
};

float RemapLightmap(float value)
{
    float lo = pushConst.sixWayParams1.x;
    float hi = max(pushConst.sixWayParams1.y, lo + 1e-4);
    return clamp((value - lo) / (hi - lo), 0.0, 1.0);
}

SixWaySample DecodeSixWay(vec3 positiveAxes, vec3 negativeAxes)
{
    SixWaySample s;
    s.right = RemapLightmap(positiveAxes.r);
    s.top = RemapLightmap(positiveAxes.g);
    s.back = RemapLightmap(positiveAxes.b);
    s.left = RemapLightmap(negativeAxes.r);
    s.bottom = RemapLightmap(negativeAxes.g);
    s.front = RemapLightmap(negativeAxes.b);
    return s;
}

float EvalSixWay(vec3 lightDirWorld, SixWaySample s)
{
    vec3 localDir = normalize(vec3(
        dot(lightDirWorld, normalize(fragBillboardRight)),
        dot(lightDirWorld, normalize(fragBillboardUp)),
        dot(lightDirWorld, normalize(fragBillboardForward))));

    vec3 p = max(localDir, vec3(0.0));
    vec3 n = max(-localDir, vec3(0.0));
    float weightSum = p.x + p.y + p.z + n.x + n.y + n.z + 1e-4;

    float value = 0.0;
    value += s.right * p.x;
    value += s.top * p.y;
    value += s.back * p.z;
    value += s.left * n.x;
    value += s.bottom * n.y;
    value += s.front * n.z;
    return value / weightSum;
}

float DistanceAttenuation(float distanceToLight, float radius)
{
    float attenuation = 1.0 - clamp(distanceToLight / max(radius, 1e-4), 0.0, 1.0);
    return attenuation * attenuation;
}

float SpotConeAttenuation(SpotLightData light, vec3 lightDirToLight)
{
    float coneAngle = dot(normalize(light.direction.xyz), normalize(lightDirToLight));
    float outerCone = cos(light.outerConeAngle);
    if (coneAngle < outerCone)
        return 0.0;

    float innerCone = cos(light.innerConeAngle);
    return clamp((coneAngle - outerCone) / max(innerCone - outerCone, 1e-4), 0.0, 1.0);
}

vec3 ParticleShadowNormal(vec3 lightDir)
{
    return normalize(lightDir + normalize(fragBillboardForward) * 0.15);
}

vec3 EvalEnvironmentGI(SixWaySample s)
{
    float ambientSixWay = (s.right + s.top + s.back + s.left + s.bottom + s.front) / 6.0;
    vec3 skyDiffuse = texture(PreConvDiffuseEnvironment, normalize(fragBillboardUp)).rgb;
    vec3 shAmbient = vec3(shCoefficients[0], shCoefficients[9], shCoefficients[18]) * 0.282095;
    return max(skyDiffuse + shAmbient, vec3(0.0)) * ambientSixWay * pushConst.sixWayParams0.y;
}

void AccumulatePointLight(uint lightIndex, SixWaySample sixWay, inout vec3 result)
{
    PointLightData light = GetPointLight(int(lightIndex));
    vec3 toLight = light.position.xyz - fragWorldPos;
    float distanceToLight = length(toLight);
    if (distanceToLight > light.radius || light.radius <= 0.001)
        return;

    vec3 lightDir = toLight / max(distanceToLight, 1e-4);
    float attenuation = DistanceAttenuation(distanceToLight, light.radius);
    uint shadowIndex = light.shadowMetaIndex;
    if (shadowIndex != INVALID_SHADOW_INDEX)
        attenuation *= SamplePointShadowMapBRDF(
            fragWorldPos, ParticleShadowNormal(lightDir), lightDir, punctualShadowMap, int(lightIndex));

    float sixWayValue = EvalSixWay(lightDir, sixWay);
    result += sixWayValue * light.color.rgb * light.intensity * attenuation * pushConst.sixWayParams0.x;
}

void AccumulateSpotLight(uint lightIndex, SixWaySample sixWay, inout vec3 result)
{
    SpotLightData light = GetSpotLight(int(lightIndex));
    vec3 toLight = light.position.xyz - fragWorldPos;
    float distanceToLight = length(toLight);
    if (distanceToLight > light.radius || light.radius <= 0.001)
        return;

    vec3 lightDir = toLight / max(distanceToLight, 1e-4);
    float attenuation = DistanceAttenuation(distanceToLight, light.radius) *
        SpotConeAttenuation(light, lightDir);
    if (attenuation <= 0.0)
        return;

    uint shadowIndex = light.shadowMetaIndex;
    if (shadowIndex != INVALID_SHADOW_INDEX)
        attenuation *= SampleSpotShadowMapBRDF(
            fragWorldPos, ParticleShadowNormal(lightDir), lightDir, punctualShadowMap, int(lightIndex));

    float sixWayValue = EvalSixWay(lightDir, sixWay);
    result += sixWayValue * light.color.rgb * light.intensity * attenuation * pushConst.sixWayParams0.x;
}

vec3 EvalSixWayLighting(SixWaySample sixWay)
{
    vec3 result = vec3(0.0);
    float viewDepth = -(ViewMatrix * vec4(fragWorldPos, 1.0)).z;

    vec3 mainLightDir = normalize(uDirectionLight.direction.xyz);
    float mainShadow = SampleCascadeShadow(
        fragWorldPos, ParticleShadowNormal(mainLightDir), cascadeShadowMap, viewDepth);
    float mainSixWay = EvalSixWay(mainLightDir, sixWay);
    result += mainSixWay * uDirectionLight.color.rgb * uDirectionLight.intensity *
        mainShadow * pushConst.sixWayParams0.x;

    TileLightHeader tile = GetFragTileLightHeader();
    for (uint slot = 0u; slot < tile.pointCount; ++slot)
        AccumulatePointLight(tileLightIndices[tile.pointOffset + slot], sixWay, result);
    for (uint slot = 0u; slot < tile.spotCount; ++slot)
        AccumulateSpotLight(tileLightIndices[tile.spotOffset + slot], sixWay, result);

    result += EvalEnvironmentGI(sixWay);
    return result;
}

void main()
{
    vec4 lmA = texture(positiveAxesTex, fragUV);
    vec4 lmB = texture(negativeAxesTex, fragUV);

    float alpha = lmA.a * fragColor.a;
    if (alpha < pushConst.sixWayParams1.z)
        discard;

    SixWaySample sixWay = DecodeSixWay(lmA.rgb, lmB.rgb);
    vec3 lighting = EvalSixWayLighting(sixWay);

    vec3 tint = max(fragColor.rgb, vec3(0.0));
    float absorption = clamp(pushConst.sixWayParams0.w, 0.0, 1.0);
    vec3 litColor = mix(lighting * tint, 1.0 - exp(-lighting * max(tint, vec3(0.001))), absorption);

    vec3 emissive = lmB.a * tint * pushConst.sixWayParams0.z;
    outColor = vec4(litColor + emissive, alpha);
}
