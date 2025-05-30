#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/Atmosphere.glsl"

 layout( location = 0 ) in vec3 direction;
 layout( location = 0 ) out vec4 frag_color;
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
   
    vec3 viewPosition = cameraPosition.xyz + vec3(0,planetRadius + initSeaLevel,0);
    frag_color = vec4(SingleScatter(param, viewPosition),1);

 }