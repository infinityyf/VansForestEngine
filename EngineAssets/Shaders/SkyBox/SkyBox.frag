#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/Atmosphere.glsl"

 layout( location = 0 ) in vec3 direction;
 layout( location = 0 ) out vec4 frag_color;
 layout( set = 1, binding = 1 ) uniform sampler2D fogResult;
// 1/4 分辨率体积云结果（由 CloudRayMarch.comp 计算，RGB=内散射，A=透射率）
layout(set = 1, binding = 2) uniform sampler2D cloudBuffer;

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

    vec2 uv = gl_FragCoord.xy / ScreenParams.xy;

    // 合成体积云结果（RGB=内散射，A=透射率），由 CloudRayMarch.comp 以 1/4 分辨率预计算
    vec4 cloudData = texture(cloudBuffer, uv);
    vec3 color = skyColor * cloudData.a + cloudData.rgb;

    // 叠加体积雾（在云层之上，fogResult 由当前帧 VolumetricFog Compute 生成）
    vec4 fogData = texture(fogResult, uv);
    float fogOpacity = fogData.a;
    color = color * (1.0 - fogOpacity) + fogData.rgb;

    frag_color = vec4(color, 1);
 }