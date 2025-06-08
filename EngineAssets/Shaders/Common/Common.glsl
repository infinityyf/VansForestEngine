#define PI 3.1415936
#define TWO_PI 6.28318530718
#define SSAO_SAMPLE_COUNT 32
#define SSAO_RADIUS 0.5
#define SSAO_DEPTH_THRESHOLD 0.6
#define SSAO_DEPHT_BIAS 0.02

float RandomInterLeaved (vec2 uv) 
{
    return fract(sin(dot(uv.xy,vec2(12.9898,78.233)))*43758.5453123);
}

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

float LinearizeDepth(float depth, float near, float far)
{
    float z = depth;
    return (2.0 * near * far) / (far + near - z * (far - near));
}