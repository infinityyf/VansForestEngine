#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/Atmosphere.glsl"

 layout( location = 0 ) in vec3 direction;
 layout( location = 0 ) out vec4 frag_color;
 layout( set = 1, binding = 1 ) uniform sampler2D fogResult;
// 1/4 分辨率体积云结果（由 CloudRayMarch.comp 计算，RGB=内散射，A=透射率）
layout(set = 1, binding = 2) uniform sampler2D cloudBuffer;
layout(set = 1, binding = 3) uniform sampler2D moonAlbedo;

vec3 EvaluateMoonAlbedo(vec3 viewDir, vec3 moonDir)
{
    vec3 diskNormal = normalize(moonDir);
    vec3 tangent = normalize(cross(abs(diskNormal.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), diskNormal));
    vec3 bitangent = cross(diskNormal, tangent);
    float radius = max(moonDiskDirectionAngularRadius.w, 1e-5);
    vec2 local = vec2(dot(viewDir, tangent), dot(viewDir, bitangent)) / sin(radius);
    vec2 uv = local * 0.5 + 0.5;
    float mask = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    vec3 albedo = texture(moonAlbedo, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
    return mix(vec3(1.0), albedo, mask);
}

float EvaluateCelestialOcclusion(float cloudTransmittance, float fogOpacity, float occlusionStrength)
{
    float strength = max(occlusionStrength, 1.0);
    float cloudT = clamp(cloudTransmittance, 0.0, 1.0);
    float fogT = clamp(1.0 - fogOpacity, 0.0, 1.0);
    return pow(cloudT, strength) * pow(fogT, strength);
}

 void main() 
 {
    AtmosphereParam param;
    param.planetRadius = planetRadius;
    param.atmosphereWidth = atmosphereWidth;
    param.rayleighScalarHeight = rayleighScalarHeight;
    param.mieScalarHeight = mieScalarHeight;
    param.mieAnisotropy = mieAnisotropy;
    param.ozoneLevelCenterHeight = ozoneLevelCenterHeight;
    param.ozoneLevelWidth = ozoneLevelWidth;
    param.sunLuminance = sunLuminance;
    param.sunDirection = sunDirection.xyz;
    param.viewDirection = normalize(direction);
   
    vec3 viewPosition = cameraPosition.xyz + vec3(0, planetRadius + initSeaLevel, 0);
    vec3 skyColor = SingleScatter(param, viewPosition);
    vec3 sunDiskColor = EvaluateCelestialDisk(
        param,
        viewPosition,
        param.viewDirection,
        sunDiskDirectionAngularRadius.xyz,
        sunDiskDirectionAngularRadius.w,
        sunDiskParams.x,
        sunDiskRadianceEnabled.rgb,
        sunDiskRadianceEnabled.w);

    vec3 moonDir = normalize(moonDiskDirectionAngularRadius.xyz);
    float moonPhase = clamp(moonDiskParams.y, 0.0, 1.0);
    vec3 moonRadiance = moonDiskRadianceEnabled.rgb * moonPhase * EvaluateMoonAlbedo(param.viewDirection, moonDir);
    vec3 moonDiskColor = EvaluateCelestialDisk(
        param,
        viewPosition,
        param.viewDirection,
        moonDir,
        moonDiskDirectionAngularRadius.w,
        moonDiskParams.x,
        moonRadiance,
        moonDiskRadianceEnabled.w);

    vec2 uv = gl_FragCoord.xy / ScreenParams.xy;

    // 合成体积云结果（RGB=内散射，A=透射率），由 CloudRayMarch.comp 以 1/4 分辨率预计算
    vec4 cloudData = texture(cloudBuffer, uv);
    vec3 color = skyColor * cloudData.a + cloudData.rgb;

    // 叠加体积雾（在云层之上，fogResult 由当前帧 VolumetricFog Compute 生成）
    vec4 fogData = texture(fogResult, uv);
    float fogOpacity = fogData.a;
    color = color * (1.0 - fogOpacity) + fogData.rgb;
    color += sunDiskColor * EvaluateCelestialOcclusion(cloudData.a, fogOpacity, sunDiskParams.z);
    color += moonDiskColor * EvaluateCelestialOcclusion(cloudData.a, fogOpacity, moonDiskParams.z);

    frag_color = vec4(color, 1);
 }
