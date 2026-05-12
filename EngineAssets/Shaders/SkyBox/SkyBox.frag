#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/Atmosphere.glsl"

 layout( location = 0 ) in vec3 direction;
 layout( location = 0 ) out vec4 frag_color;
 layout( set = 1, binding = 1 ) uniform sampler2D fogResult;

#define cloudMinHeight 1500
#define cloudMaxHeight 2100
#define cloudScatterCoeff 0.002f
#define cloudAbsorbCoeff 0.01f
#define cloudPhaseG 0.76f
#define cloudSpeed 0.02
#define cloudDensity 0.03


#define volumetricCloudSteps 16			//Higher is a better result with rendering of clouds.
#define volumetricLightSteps 8			//Higher is a better result with rendering of volumetric light.

#define cloudShadowingSteps 12			//Higher is a better result with shading on clouds.
#define volumetricLightShadowSteps 4	//Higher is a better result with shading on volumetric light from clouds

#define rayleighCoeff (vec3(0.27, 0.5, 1.0) * 1e-5)	//Not really correct
#define mieCoeff vec3(0.5e-6)						//Not really correct

const float sunBrightness = 3.0;


float bayer2(vec2 a){
    a = floor(a);
    return fract( dot(a, vec2(.5, a.y * .75)) );
}

#define bayer4(a)   (bayer2( .5*(a))*.25+bayer2(a))
#define bayer8(a)   (bayer4( .5*(a))*.25+bayer2(a))
#define bayer16(a)  (bayer8( .5*(a))*.25+bayer2(a))
#define bayer32(a)  (bayer16(.5*(a))*.25+bayer2(a))
#define bayer64(a)  (bayer32(.5*(a))*.25+bayer2(a))
#define bayer128(a) (bayer64(.5*(a))*.25+bayer2(a))



const float pi = acos(-1.0);
const float rPi = 1.0 / pi;
const float hPi = pi * 0.5;
const float tau = pi * 2.0;
const float rLOG2 = 1.0 / log(2.0);


const vec3 totalCoeff = rayleighCoeff + mieCoeff;

vec3 scatter(vec3 coeff, float depth){
	return coeff * depth;
}

vec3 absorb(vec3 coeff, float depth){
	return exp2(scatter(coeff, -depth));
}

float calcParticleThickness(float depth){
   	
    depth = depth * 2.0;
    depth = max(depth + 0.01, 0.01);
    depth = 1.0 / depth;
    
	return 100000.0 * depth;   
}

float calcParticleThicknessH(float depth){
   	
    depth = depth * 2.0 + 0.1;
    depth = max(depth + 0.01, 0.01);
    depth = 1.0 / depth;
    
	return 100000.0 * depth;   
}

float calcParticleThicknessConst(const float depth){
    
	return 100000.0 / max(depth * 2.0 - 0.01, 0.01);   
}

float calculateScatterIntergral(float opticalDepth, float coeff){
    float a = -coeff * rLOG2;
    float b = -1.0 / coeff;
    float c =  1.0 / coeff;

    return exp2(a * opticalDepth) * b + c;
}

vec3 calculateScatterIntergral(float opticalDepth, vec3 coeff){
    vec3 a = -coeff * rLOG2;
    vec3 b = -1.0 / coeff;
    vec3 c =  1.0 / coeff;

    return exp2(a * opticalDepth) * b + c;
}

vec3 calcAtomsphereSunAbsorbLight(vec3 sunDirection)
{
    float lDotU = dot(sunDirection, vec3(0.0, 1.0, 0.0));
    float opticalDepthLight = calcParticleThickness(lDotU);
    vec3 absorbLight = absorb(totalCoeff, opticalDepthLight);
    return absorbLight;
}


float fade5(float t) { return t*t*t*(t*(t*6.0 - 15.0) + 10.0); }

float hash3(vec3 p) {
    // cheap hash without sin
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float Get3DNoise(vec3 pos)
{
    vec3 ip = floor(pos);
    vec3 fp = fract(pos);

    float n000 = hash3(ip + vec3(0,0,0));
    float n100 = hash3(ip + vec3(1,0,0));
    float n010 = hash3(ip + vec3(0,1,0));
    float n110 = hash3(ip + vec3(1,1,0));
    float n001 = hash3(ip + vec3(0,0,1));
    float n101 = hash3(ip + vec3(1,0,1));
    float n011 = hash3(ip + vec3(0,1,1));
    float n111 = hash3(ip + vec3(1,1,1));

    vec3 u = vec3(fade5(fp.x), fade5(fp.y), fade5(fp.z));

    float x00 = mix(n000, n100, u.x);
    float x10 = mix(n010, n110, u.x);
    float x01 = mix(n001, n101, u.x);
    float x11 = mix(n011, n111, u.x);

    float y0 = mix(x00, x10, u.y);
    float y1 = mix(x01, x11, u.y);

    return mix(y0, y1, u.z);
}

float getClouds(vec3 p)
{
    p = vec3(p.x, length(p) - planetRadius - initSeaLevel, p.z);
    
    if (p.y < cloudMinHeight || p.y > cloudMaxHeight)
        return 0.0;
    
    float time = 0;
    vec3 movement = vec3(time, 0.0, time);
    
    vec3 cloudCoord = (p * 0.001) + movement;
    
	float noise = Get3DNoise(cloudCoord) * 0.5;
    	  noise += Get3DNoise(cloudCoord * 2.0 + movement) * 0.25;
    	  noise += Get3DNoise(cloudCoord * 7.0 - movement) * 0.125;
    	  noise += Get3DNoise((cloudCoord + movement) * 16.0) * 0.0625;
    
    const float top = 0.004;
    const float bottom = 0.01;
    
    float horizonHeight = p.y - cloudMinHeight;
    float treshHold = (1.0 - exp2(-bottom * horizonHeight)) * exp2(-top * horizonHeight);
    
    float clouds = smoothstep(0.55, 0.6, noise);
          clouds *= treshHold;
    
    return clouds * cloudDensity;
}

float getSunVisibility(vec3 p)
{
	const int steps = cloudShadowingSteps;
    const float rSteps = (cloudMaxHeight - cloudMinHeight) / float(steps);
    
    vec3 increment = sunDirection.xyz * rSteps;
    vec3 position = increment * 0.5 + p;
    
    float transmittance = 0.0;
    
    for (int i = 0; i < steps; i++, position += increment)
    {
		transmittance += getClouds(position);
    }
    
    return exp2(-transmittance * rSteps);
}



vec3 getVolumetricCloudsScattering(float opticalDepth, float phase, vec3 p, vec3 sunColor, vec3 skyLight)
{
    float intergal = calculateScatterIntergral(opticalDepth, 1.11);
    
    float beersPowder = powder(opticalDepth * log(2.0));
    
	vec3 sunlighting = (sunColor * getSunVisibility(p) * beersPowder) * phase * hPi * sunBrightness;
    vec3 skylighting = skyLight * 0.25 * rPi;
    
    return (sunlighting + skylighting) * intergal * pi;
}


vec3 calculateVolumetricClouds(vec3 viewPosition, vec3 viewDirection, vec3 atomsphereColor, float dither, vec3 sunColor)
{
	const int steps = volumetricCloudSteps;
    const float iSteps = 1.0 / float(steps);
    
   //  if (viewDirection.y < 0.0)
   //  {
   //     return vec3(0);
   //  }
    
    float bottomSphere = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMinHeight, viewPosition, viewDirection);
    float topSphere = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMaxHeight, viewPosition, viewDirection);
    
    vec3 startPosition = viewPosition + viewDirection * bottomSphere;
    vec3 endPosition = viewPosition + viewDirection * topSphere;
    
    vec3 increment = (endPosition - startPosition) * iSteps;
    vec3 cloudPosition = increment * dither + startPosition;
    
    float stepLength = length(increment);
    
    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;
    
    float lDotW = dot(sunDirection.xyz, viewDirection);
    float phase = phase2Lobes(lDotW);
    
    vec3 skyLight = vec3(0);
    
    for (int i = 0; i < steps; i++, cloudPosition += increment)
    {
        float opticalDepth = getClouds(cloudPosition) * stepLength;
        
        if (opticalDepth <= 0.0)
            continue;
        
		scattering += getVolumetricCloudsScattering(opticalDepth, phase, cloudPosition, sunColor, skyLight) * transmittance;
        transmittance *= exp2(-opticalDepth);
    }
    
    return mix(atomsphereColor * transmittance + scattering, atomsphereColor, clamp((length(startPosition) - planetRadius - initSeaLevel) * 0.00001, 0.0, 1.0));
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
   
    vec3 viewPosition = cameraPosition.xyz + vec3(0,planetRadius + initSeaLevel,0);

    vec3 skyColor = SingleScatter(param, viewPosition);

    vec3 viewDirection = param.viewDirection;
    float dither = bayer64(gl_FragCoord.xy);

    vec3 lightAbsorb = calcAtomsphereSunAbsorbLight(sunDirection.xyz);
    
    vec3 color = calculateVolumetricClouds(viewPosition,viewDirection, skyColor, dither, lightAbsorb);

    vec2 uv = gl_FragCoord.xy / ScreenParams.xy;

    // fogResult 由当前帧体积雾流程生成，天空合成也使用当前屏幕 UV。
    vec4 fogData = texture(fogResult, uv);
    float fogOpacity = fogData.a;
    color = color * (1.0 - fogOpacity) + fogData.rgb;

    //都积分128次，每次进行一次单向散射，并考虑powder effect
    frag_color = vec4(color,1);
 }