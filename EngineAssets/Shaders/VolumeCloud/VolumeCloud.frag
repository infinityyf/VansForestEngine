#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/Atmosphere.glsl"

// #define LightCBBind 2
// #include "../Lights/LightsData.glsl"

// layout(set=1, binding=0) uniform  VolumeCloudUniformBuffer
// {
//     float cloudMinHeight;
//     float cloudMaxHeight;
//     float cloudScatterCoeff;
//     float cloudAbsorbCoeff;
//     float cloudPhaseG;
// };
#define cloudMinHeight 1500
#define cloudMaxHeight 2100
#define cloudScatterCoeff 0.002f
#define cloudAbsorbCoeff 0.01f
#define cloudPhaseG 0.76f

layout( location = 0 ) in vec3 direction;
layout( location = 0 ) out vec4 frag_color;

// vec3 VolumeSingleScatter(vec3 viewDirection,vec3 start_position)
// {
//     vec3 color = vec3(0,0,0);
//     int sample_count = 128;
//     int sub_sample_count = 16;
//     float distanceCloudMin = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMinHeight, start_position, viewDirection);
//     float distanceCloudMax = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMaxHeight, start_position, viewDirection);
//     float distanceVolume = distanceCloudMax - distanceCloudMin;

//     float step = distanceVolume / sample_count;
//     vec3 pScatterPosition = start_position + viewDirection * distanceCloudMin;

//     float cos_theta = dot(sunDirection.xyz, viewDirection);

//     vec3 transmittance = vec3(1.0);
//     for(int i=0; i < sample_count; i++)
//     {
//         float density = 0.1;
//         float distanceSun = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMaxHeight, pScatterPosition, sunDirection.xyz);
//         float stepToSun = distanceSun / sub_sample_count;
//         vec3 pToSun = pScatterPosition;

//         //当前点的光源输入
//         vec3 sunTransmittance = vec3(1.0);
//         //累计每个采样点的散射结果，并且计算到光源的散射结果
//         for(int j=0; j < sub_sample_count; j++)
//         {
//             //当前点的浓度
//             sunTransmittance *= exp(-stepToSun * (cloudAbsorbCoeff * density));
//             pToSun += sunDirection.xyz * stepToSun;
//         }
//         transmittance *= exp(-step * (cloudAbsorbCoeff * density));

//         vec3 scattering = cloudScatterCoeff * step * sunTransmittance * HGPhase(cos_theta,cloudPhaseG);
//         color += transmittance * scattering;
//         pScatterPosition += viewDirection * step;
//     }
//     return color;
// }

#define cloudSpeed 0.02
#define cloudDensity 0.03


#define volumetricCloudSteps 12			//Primary ray-march steps (was 16).
#define volumetricLightSteps 8			//Higher is a better result with rendering of volumetric light.

#define cloudShadowingSteps 6			//Sun-visibility march steps (was 12).
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

// Temporal interleaved gradient noise — less structured than Bayer, good temporal distribution
float interleavedGradientNoise(vec2 pixel, float frame)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715)) + frame * 0.61803398875));
}



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

float rayleighPhase(float x){
	return 0.375 * (1.0 + x*x);
}

float hgPhase(float x, float g)
{
    float g2 = g*g;
	return 0.25 * ((1.0 - g2) * pow(1.0 + g2 - 2.0*g*x, -1.5));
}

float miePhaseSky(float x, float depth)
{
 	return hgPhase(x, exp2(-0.000003 * depth));
}

float powder(float od)
{
	return 1.0 - exp2(-od * 2.0);
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
    
    // 3 octaves instead of 4 — the 16x octave caused high-frequency noise / aliasing
	float noise = Get3DNoise(cloudCoord) * 0.625;
    	  noise += Get3DNoise(cloudCoord * 2.0 + movement) * 0.25;
    	  noise += Get3DNoise(cloudCoord * 7.0 - movement) * 0.125;
    
    const float top = 0.004;
    const float bottom = 0.01;
    
    float horizonHeight = p.y - cloudMinHeight;
    float cloudLayerThickness = max(float(cloudMaxHeight - cloudMinHeight), 1.0);
    float normalizedH = clamp(horizonHeight / cloudLayerThickness, 0.0, 1.0);
    // Parabolic vertical shaping: thins at top and bottom of cloud layer
    float verticalShape = normalizedH * (1.0 - normalizedH) * 4.0;
    float treshHold = (1.0 - exp2(-bottom * horizonHeight)) * exp2(-top * horizonHeight);
    treshHold *= clamp(verticalShape, 0.0, 1.0);
    
    // Wider smoothstep band reduces harsh on/off transitions that look noisy
    float clouds = smoothstep(0.48, 0.62, noise);
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

        if (transmittance * rSteps > 3.5)
            break;
    }
    
    return exp2(-transmittance * rSteps);
}

float phase2Lobes(float x)
{
    const float m = 0.6;
    const float gm = 0.8;
    
	float lobe1 = hgPhase(x, 0.8 * gm);
    float lobe2 = hgPhase(x, -0.5 * gm);
    
    return mix(lobe2, lobe1, m);
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
    
    if (viewDirection.y < 0.0)
    {
       return vec3(0);
    }
    
    float bottomSphere = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMinHeight, viewPosition, viewDirection);
    float topSphere = RayIntersectSphere(vec3(0, 0, 0), planetRadius + cloudMaxHeight, viewPosition, viewDirection);

    if (bottomSphere < 0.0 || topSphere < 0.0 || topSphere <= bottomSphere)
        return atomsphereColor;
    
    vec3 startPosition = viewPosition + viewDirection * bottomSphere;
    vec3 endPosition = viewPosition + viewDirection * topSphere;
    
    vec3 increment = (endPosition - startPosition) * iSteps;
    vec3 cloudPosition = increment * dither + startPosition;
    
    float stepLength = length(increment);
    
    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;
    
    float lDotW = dot(sunDirection.xyz, viewDirection);
    float phase = phase2Lobes(lDotW);
    
    // Minimal ambient sky contribution to fill shadow areas
    vec3 skyLight = sunColor * 0.08;
    
    for (int i = 0; i < steps; i++, cloudPosition += increment)
    {
        float opticalDepth = getClouds(cloudPosition) * stepLength;
        
        if (opticalDepth <= 0.0)
            continue;
        
		scattering += getVolumetricCloudsScattering(opticalDepth, phase, cloudPosition, sunColor, skyLight) * transmittance;
        transmittance *= exp2(-opticalDepth);

        // Early exit when cloud is opaque enough — saves remaining iterations
        if (transmittance < 0.03)
            break;
    }
    
    return mix(atomsphereColor * transmittance + scattering, atomsphereColor, clamp((length(startPosition) - planetRadius - initSeaLevel) * 0.00001, 0.0, 1.0));
}

void main() 
{
    //从当前相机方向发出射线，得到云层此时的积分厚度
    vec3 viewPosition = cameraPosition.xyz + vec3(0,planetRadius + initSeaLevel,0);
    vec3 viewDirection = normalize(direction);
    // Temporal IGN dither — varies per frame, much less visible pattern than static Bayer
    float dither = interleavedGradientNoise(gl_FragCoord.xy, FrameIndex);

    vec3 lightAbsorb = calcAtomsphereSunAbsorbLight(sunDirection.xyz);
    
    vec3 color = calculateVolumetricClouds(viewPosition,viewDirection, vec3(0.0f), dither, lightAbsorb);

    //都积分128次，每次进行一次单向散射，并考虑powder effect
    frag_color = vec4(color,1);
}