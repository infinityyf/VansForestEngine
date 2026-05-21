#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Lights/LightsData.glsl"

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragBillboardRight;
layout(location = 4) in vec3 fragBillboardUp;
layout(location = 5) in vec3 fragBillboardForward;

// Unity Six-Way 标准打包：
// Positive: R=Right, G=Top,    B=Back,  A=Alpha
// Negative: R=Left,  G=Bottom, B=Front, A=Emissive
layout(set = 1, binding = 0) uniform sampler2D positiveAxesTex;
layout(set = 1, binding = 1) uniform sampler2D negativeAxesTex;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ParticleSixWayPushConst
{
    vec4 spriteSheetParams;     // x: columns, y: rows
    vec4 sixWayParams0;         // x: direct, y: ambient, z: emissive, w: absorption
    vec4 sixWayParams1;         // x: remapMin, y: remapMax, z: alphaCutoff, w: debugMode
    vec4 mainLightDirAndPad;    // 预留
    vec4 mainLightColor;        // 预留
} pushConst;

float RemapLightmap(float value)
{
    float lo = pushConst.sixWayParams1.x;
    float hi = max(pushConst.sixWayParams1.y, lo + 1e-4);
    return clamp((value - lo) / (hi - lo), 0.0, 1.0);
}

float EvalSixWay(vec3 lightDirWorld, vec3 lmPositive, vec3 lmNegative)
{
    vec3 localDir = normalize(vec3(
        dot(lightDirWorld, normalize(fragBillboardRight)),
        dot(lightDirWorld, normalize(fragBillboardUp)),
        dot(lightDirWorld, normalize(fragBillboardForward))
    ));

    vec3 p = max(localDir, vec3(0.0));
    vec3 n = max(-localDir, vec3(0.0));

    float rightValue  = RemapLightmap(lmPositive.r);
    float topValue    = RemapLightmap(lmPositive.g);
    float backValue   = RemapLightmap(lmPositive.b);
    float leftValue   = RemapLightmap(lmNegative.r);
    float bottomValue = RemapLightmap(lmNegative.g);
    float frontValue  = RemapLightmap(lmNegative.b);

    float weightSum = p.x + p.y + p.z + n.x + n.y + n.z + 1e-4;
    float value = 0.0;
    value += rightValue  * p.x;
    value += topValue    * p.y;
    value += frontValue  * p.z;
    value += leftValue   * n.x;
    value += bottomValue * n.y;
    value += backValue   * n.z;
    return value / weightSum;
}

vec3 EvalSixWayLighting(vec3 lmPositive, vec3 lmNegative)
{
    vec3 result = vec3(0.0);

    vec3 mainLightDir = normalize(uDirectionLight.direction.xyz);
    float mainSixWay = EvalSixWay(mainLightDir, lmPositive, lmNegative);
    result += mainSixWay * uDirectionLight.color.rgb * uDirectionLight.intensity * pushConst.sixWayParams0.x;

    uint pointCount = min(uPointLightCount, 8u);
    for (uint i = 0u; i < pointCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 toLight = pointLight.position.xyz - fragWorldPos;
        float distanceToLight = length(toLight);
        if (distanceToLight > pointLight.radius || pointLight.radius <= 0.001)
            continue;

        vec3 lightDir = toLight / max(distanceToLight, 1e-4);
        float attenuation = 1.0 - clamp(distanceToLight / pointLight.radius, 0.0, 1.0);
        attenuation *= attenuation;

        float sixWay = EvalSixWay(lightDir, lmPositive, lmNegative);
        result += sixWay * pointLight.color.rgb * pointLight.intensity * attenuation * pushConst.sixWayParams0.x;
    }

    float ambientSixWay = (
        RemapLightmap(lmPositive.r) + RemapLightmap(lmPositive.g) + RemapLightmap(lmPositive.b) +
        RemapLightmap(lmNegative.r) + RemapLightmap(lmNegative.g) + RemapLightmap(lmNegative.b)) / 6.0;
    result += vec3(ambientSixWay * pushConst.sixWayParams0.y);

    return result;
}

void main()
{
    vec4 lmA = texture(positiveAxesTex, fragUV);
    vec4 lmB = texture(negativeAxesTex, fragUV);

    float alpha = lmA.a * fragColor.a;
    if (alpha < pushConst.sixWayParams1.z)
        discard;

    vec3 lighting = EvalSixWayLighting(lmA.rgb, lmB.rgb);

    vec3 tint = max(fragColor.rgb, vec3(0.0));
    float absorption = clamp(pushConst.sixWayParams0.w, 0.0, 1.0);
    vec3 litColor = mix(lighting * tint, 1.0 - exp(-lighting * max(tint, vec3(0.001))), absorption);

    vec3 emissive = lmB.a * tint * pushConst.sixWayParams0.z;
    outColor = vec4(litColor + emissive, alpha);
}
