#ifndef COMMON_GLSL_INCLUDED
#define COMMON_GLSL_INCLUDED

#define PI 3.1415936
#define TWO_PI 6.28318530718
#define SSAO_SAMPLE_COUNT 32
#define SSAO_RADIUS 2.0
#define SSAO_DEPTH_THRESHOLD 1.0
#define SSAO_DEPHT_BIAS 0.02

#define DEPTH_BIAS 0.001
#define ESM_C 80.0

#define SSGI_MAX_COUNT 32
#define SSGI_MAX_STEP 4

#define SSR_MAX_COUNT 64
#define SSR_MAX_STEP 4
#define SSR_NUM_RESOLVER 9

float RandomInterLeaved (vec2 uv) 
{
    return fract(sin(dot(uv.xy,vec2(12.9898,78.233)))*43758.5453123);
}

// float IGN(int2 pixelCoord, int frame)
// {
//     frame = frame % 64; // need to periodically reset frame to avoid numerical issues
//     float x = float(pixelX) + 5.588238 * float(frame);
//     float y = float(pixelY) + 5.588238 * float(frame);
//     return std::fmodf(52.9829189 * std::fmodf(0.06711056*float(x) + 0.00583715*float(y), 1.0), 1.0);
// }

float RandomInterLeavedWithScale(vec2 uv, float t)
{
    vec3 magic = vec3(0.06711056f, 0.00587752f,52.9829189f);
    vec2 scale = vec2(2.083f, 4.867f);
    uv += t * scale;
    return RandomInterLeaved(uv);
}

float RandomWithScale(vec2 uv, float t)
{
    vec3 magic = vec3(0.06711056f, 0.00587752f,52.9829189f);
    vec2 scale = vec2(2.083f, 4.867f);
    uv += t * scale;
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

vec2 HashRandom(vec2 p,float frameCount)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    vec3 frameMagicScale = vec3(2.083f, 4.867f,8.65);
    p3 += frameCount * frameMagicScale;
    return fract((p3.xx + p3.yz) * p3.zy);
}

float LinearizeDepth(float depth, float near, float far)
{
    float z = depth;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

vec4 ImportanceSampleGGX(vec2 random, float roughness) 
{
	float m = roughness * roughness;
	float m2 = m * m;

	float Phi = 2 * PI * random.x;
	float CosTheta = sqrt( (1 - random.y) / ( 1 + (m2 - 1) * random.y) );
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	vec3 H = vec3( SinTheta * cos(Phi), SinTheta * sin(Phi), CosTheta );
			
	float d = (CosTheta * m2 - CosTheta) * CosTheta + 1;
	float D = m2 / (PI * d * d);
			
	float PDF = D * CosTheta;
	return vec4(H, PDF);
}

#endif