#ifndef COMMON_GLSL_INCLUDED
#define COMMON_GLSL_INCLUDED

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#define PI 3.1415936
#define TWO_PI 6.28318530718
#define FOUR_PI 12.566370614359172
#define HALF_PI 1.57079632679
#define SH_SAMPLE_COUNT 1024
#define SSAO_SAMPLE_COUNT 32
#define SSAO_RADIUS 2.0
#define SSAO_DEPTH_THRESHOLD 1.2
#define SSAO_DEPHT_BIAS 0.04

// ---------------------------------------------------------------------------
// Material IDs for deferred shading (stored in G-Buffer)
// ID 0 = skip / no shading
// ---------------------------------------------------------------------------
#define MATERIAL_ID_SKIP        0
#define MATERIAL_ID_PBR         1
#define MATERIAL_ID_TERRAIN     2
#define MATERIAL_ID_SKIN        3
#define MATERIAL_ID_REFRACTION  4
#define MATERIAL_ID_CLOTH       5
#define MATERIAL_ID_HAIR        6
#define MATERIAL_ID_SUBSURFACE  7
#define MATERIAL_ID_GRASS       8
#define MATERIAL_ID_EMISSIVE    9   // 自发光：albedo × intensity 直通，无 BRDF

#define DEPTH_BIAS 0.001
// 旧的 NDC 空间固定偏置已废弃，保留用于方向光（正交投影不存在透视放大问题）
// 点光源/聚光灯改用世界空间法线偏置，见 LightsData.glsl
#define PUNCTUAL_NORMAL_OFFSET_BASE  0.02   // 世界空间法线偏置基值（米），与光源距离无关
#define PUNCTUAL_SLOPE_BIAS_SCALE    1.5    // slope = tan(θ) 放大系数
#define PUNCTUAL_SLOPE_BIAS_MAX      4.0    // tan(θ) 上界，防止 grazing 发散
#define ESM_C 80.0

// Cascade Shadow Map constants
#define CASCADE_COUNT 4
#define CASCADE_BLEND_BAND 0.15
#define FOG_CASCADE_INDEX 1
#define RAYTRACING_CASCADE_INDEX 1

#define SSGI_MAX_COUNT 32
#define SSGI_MAX_STEP 4

#define SSR_DEPTH_TOLERANCE 0.01      // base depth tolerance (fraction of scene depth)
#define SSR_NUM_RESOLVER 24            // spatial resolve samples (was 9 — more needed for rough)
#define SSR_REFINE_STEPS 16            // binary-search refinement iterations
#define SSR_START_BIAS 0.05            // world-space bias along normal to avoid self-hit
#define SSR_RESOLVE_BASE_RADIUS 2.0    // min gather radius in pixels (tight for mirrors)
#define SSR_RESOLVE_MAX_RADIUS  20.0   // max gather radius (reached at roughness=1)
#define SSR_FIREFLY_CLAMP 4.0          // max luminance for firefly suppression in resolve
#define SSR_ROUGHNESS_FADE_START 0.4   // roughness below this: full SSR trust
#define SSR_ROUGHNESS_FADE_END   0.7   // roughness above this: SSR fully faded out



float pow2(float x)
{
    return x * x;
}

vec2 pow2(vec2 x)
{
    return x * x;
}

vec3 pow2(vec3 x)
{
    return x * x;
}

vec4 pow2(vec4 x)
{
    return x * x;
}

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

vec2 HashRandom(vec2 uv,float t)
{
    vec2 scale = vec2(2.083, 4.867);
    uv += t * scale;

    // First component (original hash)
    float n1 = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123);

    // Second component (different hash: offset + different constants)
    vec2 uv2 = uv + vec2(37.713, 17.127);
    float n2 = fract(sin(dot(uv2, vec2(4.1234, 95.873))) * 23421.631);

    return vec2(n1, n2);
}

// R2 quasi-random sequence (Roberts, 2018) — well-distributed 2D points
// over [0,1)².  Superior stratification compared to sin-hash for
// importance-sampled GGX in stochastic SSR.
vec2 R2Sequence(int index)
{
    const float g  = 1.32471795724; // plastic constant
    const float a1 = 1.0 / g;       // ≈ 0.7548776662
    const float a2 = 1.0 / (g * g); // ≈ 0.5698402910
    return fract(vec2(a1, a2) * float(index) + 0.5);
}

// Per-pixel deterministic seed — decorrelates the quasi-random sequence
// spatially so neighboring pixels sample different parts of the hemisphere.
uint PixelSeedHash(ivec2 p)
{
    uint seed = uint(p.x) * 1664525u + uint(p.y) * 1013904223u;
    seed ^= seed >> 16u;
    seed *= 0x45d9f3bu;
    seed ^= seed >> 16u;
    return seed;
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

mat3 GetTangentBasis(vec3 TangentZ) 
{
    vec3 UpVector = abs(TangentZ.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 TangentX = normalize(cross(UpVector, TangentZ));
    vec3 TangentY = normalize(cross(TangentZ, TangentX));
    return mat3(TangentX, TangentY, TangentZ);
}

vec3 TangentToWorld(vec3 Vec, vec3 TangentZ)
{
    return GetTangentBasis(TangentZ) * Vec;
}

float Luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Karis tonemap / inverse — compresses HDR before temporal blending so
// bright firefly pixels don't bias the neighbourhood statistics or the
// exponential moving average.  Standard technique from Karis (2014).
vec3 KarisTonemap(vec3 c)        { return c / (1.0 + Luminance(c)); }
vec3 KarisInverseTonemap(vec3 c) { return c / max(1.0 - Luminance(c), 1e-4); }

const ivec3 kFrameOffsets[8] = ivec3[](
    ivec3(0,0,0), // frame 0
    ivec3(1,0,0), // frame 1
    ivec3(0,1,0), // frame 2
    ivec3(1,1,0), // frame 3
    ivec3(0,0,1), // frame 4
    ivec3(1,0,1), // frame 5
    ivec3(0,1,1), // frame 6
    ivec3(1,1,1)  // frame 7
);

// 2阶球谐基函数 (实值形式)
// v: 单位方向向量 (x,y,z)
float SHBasis(int i, vec3 v) 
{
    switch(i) 
    {
        case 0:  return 0.282095;                                  // Y00
        case 1:  return 0.488603 * v.z;                            // Y1-1
        case 2:  return 0.488603 * v.y;                            // Y10
        case 3:  return 0.488603 * v.x;                            // Y11
        case 4:  return 1.092548 * v.x * v.y;                       // Y2-2
        case 5:  return 1.092548 * v.y * v.z;                       // Y2-1
        case 6:  return 0.315392 * (3.0 * v.z * v.z - 1.0);         // Y20
        case 7:  return 1.092548 * v.x * v.z;                       // Y21
        case 8:  return 0.546274 * (v.x * v.x - v.y * v.y);         // Y22
        default: return 0.0;
    }
}


// 从 GI 可见性球谐系数（L0+L1，4 系数）中沿指定方向求标量可见性 [0,1]
// shVis 由 GIVisibility.comp 写入: c = (4π/N) Σ v(d_i)*Y(d_i)
float EvalGIVisibility(vec4 shVis, vec3 dir)
{
    vec3 d = normalize(dir);
    float val = shVis.x * SHBasis(0, d)
              + shVis.y * SHBasis(1, d)
              + shVis.z * SHBasis(2, d)
              + shVis.w * SHBasis(3, d);
    return max(val, 0.0);
}

// 生成均匀分布的球面采样点 (使用黄金螺旋算法)
vec3 SampleSphere(int i, int sampleCount) 
{
    float y = 1.0 - (i + 0.5) / sampleCount * 2.0;  // y范围 [-1,1]
    float radius = sqrt(1.0 - y * y);                // 半径
    
    float theta = 6.28318530718 * i / 1.61803398875; // 黄金角
    
    float x = cos(theta) * radius;
    float z = sin(theta) * radius;
    
    return vec3(x, y, z);
}

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)  | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)  | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)  | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)  | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // 1/2^32
}

// // Low-discrepancy sphere sampler (Hammersley sequence)
// vec3 SampleSphere(int i, int sampleCount)
// {
//     float u = (float(i) + 0.5) / float(sampleCount); // stratified in [0,1]
//     float v = RadicalInverse_VdC(uint(i));           // quasi-random in [0,1]

//     float cosTheta = 1.0 - 2.0 * u;                  // y in [-1,1]
//     float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
//     float phi = TWO_PI * v;

//     return vec3(cos(phi) * sinTheta,
//                 cosTheta,
//                 sin(phi) * sinTheta);
// }

struct RayTracePayload
{
    vec4 positionHit;
    vec4 normalHit;
    vec4 albedoRoughness;
};


float rayleighPhase(float x)
{
	return 0.375 * (1.0 + x*x);
}

float hgPhase(float x, float g)
{
    float g2 = g*g;
	return 0.25 * ((1.0 - g2) * pow(max(1e-6, 1.0 + g2 - 2.0*g*x), -1.5));
}

float miePhaseSky(float x, float depth)
{
 	return hgPhase(x, exp2(-0.000003 * depth));
}

float powder(float od)
{
	return 1.0 - exp2(-od * 2.0);
}

float phase2Lobes(float x)
{
    const float m = 0.6;
    const float gm = 0.8;
    
	float lobe1 = hgPhase(x, 0.8 * gm);
    float lobe2 = hgPhase(x, -0.5 * gm);
    
    return mix(lobe2, lobe1, m);
}

#define DISK_SAMPLE_COUNT 64

const vec2 poissonDisk[16] =
    {
    vec2( -0.94201624, -0.39906216 ),
    vec2( 0.94558609, -0.76890725 ),
    vec2( -0.094184101, -0.92938870 ),
    vec2( 0.34495938, 0.29387760 ),
    vec2( -0.91588581, 0.45771432 ),
    vec2( -0.81544232, -0.87912464 ),
    vec2( -0.38277543, 0.27676845 ),
    vec2( 0.97484398, 0.75648379 ),
    vec2( 0.44323325, -0.97511554 ),
    vec2( 0.53742981, -0.47373420 ),
    vec2( -0.26496911, -0.41893023 ),
    vec2( 0.79197514, 0.19090188 ),
    vec2( -0.24188840, 0.99706507 ),
    vec2( -0.81409955, 0.91437590 ),
    vec2( 0.19984126, 0.78641367 ),
    vec2( 0.14383161, -0.14100790 )
    };

const vec2 fibonacciSpiralDirection[DISK_SAMPLE_COUNT] =
{
    vec2 (1, 0),
    vec2 (-0.7373688780783197, 0.6754902942615238),
    vec2 (0.08742572471695988, -0.9961710408648278),
    vec2 (0.6084388609788625, 0.793600751291696),
    vec2 (-0.9847134853154288, -0.174181950379311),
    vec2 (0.8437552948123969, -0.5367280526263233),
    vec2 (-0.25960430490148884, 0.9657150743757782),
    vec2 (-0.46090702471337114, -0.8874484292452536),
    vec2 (0.9393212963241182, 0.3430386308741014),
    vec2 (-0.924345556137805, 0.3815564084749356),
    vec2 (0.423845995047909, -0.9057342725556143),
    vec2 (0.29928386444487326, 0.9541641203078969),
    vec2 (-0.8652112097532296, -0.501407581232427),
    vec2 (0.9766757736281757, -0.21471942904125949),
    vec2 (-0.5751294291397363, 0.8180624302199686),
    vec2 (-0.12851068979899202, -0.9917081236973847),
    vec2 (0.764648995456044, 0.6444469828838233),
    vec2 (-0.9991460540072823, 0.04131782619737919),
    vec2 (0.7088294143034162, -0.7053799411794157),
    vec2 (-0.04619144594036213, 0.9989326054954552),
    vec2 (-0.6407091449636957, -0.7677836880006569),
    vec2 (0.9910694127331615, 0.1333469877603031),
    vec2 (-0.8208583369658855, 0.5711318504807807),
    vec2 (0.21948136924637865, -0.9756166914079191),
    vec2 (0.4971808749652937, 0.8676469198750981),
    vec2 (-0.952692777196691, -0.30393498034490235),
    vec2 (0.9077911335843911, -0.4194225289437443),
    vec2 (-0.38606108220444624, 0.9224732195609431),
    vec2 (-0.338452279474802, -0.9409835569861519),
    vec2 (0.8851894374032159, 0.4652307598491077),
    vec2 (-0.9669700052147743, 0.25489019011123065),
    vec2 (0.5408377383579945, -0.8411269468800827),
    vec2 (0.16937617250387435, 0.9855514761735877),
    vec2 (-0.7906231749427578, -0.6123030256690173),
    vec2 (0.9965856744766464, -0.08256508601054027),
    vec2 (-0.6790793464527829, 0.7340648753490806),
    vec2 (0.0048782771634473775, -0.9999881011351668),
    vec2 (0.6718851669348499, 0.7406553331023337),
    vec2 (-0.9957327006438772, -0.09228428288961682),
    vec2 (0.7965594417444921, -0.6045602168251754),
    vec2 (-0.17898358311978044, 0.9838520605119474),
    vec2 (-0.5326055939855515, -0.8463635632843003),
    vec2 (0.9644371617105072, 0.26431224169867934),
    vec2 (-0.8896863018294744, 0.4565723210368687),
    vec2 (0.34761681873279826, -0.9376366819478048),
    vec2 (0.3770426545691533, 0.9261958953890079),
    vec2 (-0.9036558571074695, -0.4282593745796637),
    vec2 (0.9556127564793071, -0.2946256262683552),
    vec2 (-0.50562235513749, 0.8627549095688868),
    vec2 (-0.2099523790012021, -0.9777116131824024),
    vec2 (0.8152470554454873, 0.5791133210240138),
    vec2 (-0.9923232342597708, 0.12367133357503751),
    vec2 (0.6481694844288681, -0.7614961060013474),
    vec2 (0.036443223183926, 0.9993357251114194),
    vec2 (-0.7019136816142636, -0.7122620188966349),
    vec2 (0.998695384655528, 0.05106396643179117),
    vec2 (-0.7709001090366207, 0.6369560596205411),
    vec2 (0.13818011236605823, -0.9904071165669719),
    vec2 (0.5671206801804437, 0.8236347091470047),
    vec2 (-0.9745343917253847, -0.22423808629319533),
    vec2 (0.8700619819701214, -0.49294233692210304),
    vec2 (-0.30857886328244405, 0.9511987621603146),
    vec2 (-0.4149890815356195, -0.9098263912451776),
    vec2 (0.9205789302157817, 0.3905565685566777)
};

//FibonacciSpiralDisk随机采样，Sample点更集中在中心，更适合给FindBlocker用
vec2 ComputeFibonacciSpiralDiskSampleClumped(int sampleIndex, float sampleCountInverse, out float sampleDistNorm)
{
    sampleDistNorm = float(sampleIndex) * sampleCountInverse;
    return fibonacciSpiralDirection[sampleIndex] * sampleDistNorm;
}

// Disk sample (low discrepancy) in [0,1] radius
vec2 DiskSample(uint i, uint n)
{
    float u = (float(i) + 0.5) / float(n);
    float r = sqrt(u);
    float theta = (float(i) * 2.39996323); // golden angle
    return r * vec2(cos(theta), sin(theta));
}

// Orthonormal basis from normal
void BuildTBN(in vec3 N, out vec3 T, out vec3 B)
{
    vec3 up = (abs(N.y) < 0.99) ? vec3(0,1,0) : vec3(1,0,0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// ============================================================================
// HiZ screen-space ray tracing
// Strategy (based on mip-chain acceleration):
//   Project ray start & end to screen space (UV + linear depth).
//   March in SS with step size scaled by current mip level.
//   Behind surface at mip > 0  → step back & descend to finer mip.
//   Behind surface at mip == 0 → thickness test to confirm hit.
//   In front of surface         → advance & climb to coarser mip.
// ============================================================================

struct HiZTraceResult {
    bool  hit;
    vec2  uv;
    float depth;
};

// Project world position to screen space: returns (uv.x, uv.y, linearDepth)
//   linearDepth = -viewPos.z (正米)，与 HIZ 存储格式一致
//   uv 已应用引擎使用的负高度 viewport Y 翻转（NDC Y=+1 → UV.y=0）
//   注意：uv 允许在 [0,1] 外，表示投影有效但在屏幕外；只有 clip.w <= 0 才非法。
bool HiZ_ProjectToScreenChecked(mat4 viewMat, mat4 projMat, vec3 ws, out vec3 screenPos)
{
    vec4 viewPos = viewMat * vec4(ws, 1.0);
    float linearDepth = -viewPos.z;
    vec4 clip = projMat * viewPos;
    if (clip.w <= 1e-6 || linearDepth <= 1e-6)
    {
        screenPos = vec3(-1.0);
        return false;
    }
    vec2 ndc = clip.xy / clip.w;
    vec2 uv = ndc * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    screenPos = vec3(uv, linearDepth);
    return true;
}

vec3 HiZ_ProjectToScreen(mat4 viewMat, mat4 projMat, vec3 ws)
{
    vec3 screenPos;
    if (!HiZ_ProjectToScreenChecked(viewMat, projMat, ws, screenPos))
        return vec3(-1.0);
    return screenPos;
}

// ============================================================================
// TraceHiZ_UV — 屏幕空间 HiZ 加速光线追踪（透视正确 per-cell traversal）
//
// 核心算法：
//   1) ray 在屏幕 UV 中是一条直线；沿 UV cell 边界推进。
//   2) view-space linear depth 不能在 UV 上线性插值，必须插值 1/z 再取倒数。
//   3) HIZ 是 min linear depth，cell 内若 ray 的 zFar < sceneMinZ，可安全跨过并升 mip。
//   4) 若可能相交，mip > 0 不推进只下降 mip；mip == 0 解 1/z 方程得到精确 hitUV。
//
// 必须 POINT SAMPLE：HZB sampler 默认是 LINEAR，textureLod 会破坏 min-depth 语义。
// ============================================================================
HiZTraceResult TraceHiZ_UV(
    sampler2D hiz,
    mat4 lastView,
    mat4 lastProj,
    vec3 startWS,
    vec3 dirWS,
    float maxDistWorld,
    float traceStride,           // 保留接口但当前未使用
    float thicknessThreshold     // 米，命中点前后允许的深度误差
) {
    dirWS = normalize(dirWS);

    // 若 ray 朝相机方向走，固定 maxDistWorld 端点可能越过相机后方，导致投影失败。
    // 这里把世界空间距离裁到近相机前方一点，保证 endSS 可投影；朝远处走则由调用方传入
    // 尽可能大的 maxDistWorld（建议 FarPlane）来覆盖屏幕边缘。
    vec4 startVS4 = lastView * vec4(startWS, 1.0);
    vec3 dirVS = (lastView * vec4(dirWS, 0.0)).xyz;
    float traceDistWorld = maxDistWorld;
    if (dirVS.z > 1e-6)
    {
        float distToNear = (-0.01 - startVS4.z) / dirVS.z;
        traceDistWorld = min(traceDistWorld, max(distToNear, 0.0));
    }

    vec3 startSS;
    vec3 endSS;
    if (!HiZ_ProjectToScreenChecked(lastView, lastProj, startWS, startSS))
        return HiZTraceResult(false, vec2(0.0), 0.0);
    if (traceDistWorld <= 1e-5)
        return HiZTraceResult(false, vec2(0.0), 0.0);
    if (!HiZ_ProjectToScreenChecked(lastView, lastProj, startWS + dirWS * traceDistWorld, endSS))
        return HiZTraceResult(false, vec2(0.0), 0.0);

    vec2 uv0 = startSS.xy;
    vec2 uv1 = endSS.xy;
    vec2 rayUV = uv1 - uv0;
    if (length(rayUV) < 1e-7)
        return HiZTraceResult(false, vec2(0.0), 0.0); // 几乎垂直于屏幕，退化

    // 计算从 uv0 沿 rayUV 到屏幕边缘的参数。trace 不应该因为 uv1 在屏幕外而提前失败，
    // 而应一直走到屏幕边缘、世界最大距离或最大迭代次数。
    float tScreenExit = 1.0;
    if (rayUV.x > 1e-8)
        tScreenExit = min(tScreenExit, (1.0 - uv0.x) / rayUV.x);
    else if (rayUV.x < -1e-8)
        tScreenExit = min(tScreenExit, (0.0 - uv0.x) / rayUV.x);

    if (rayUV.y > 1e-8)
        tScreenExit = min(tScreenExit, (1.0 - uv0.y) / rayUV.y);
    else if (rayUV.y < -1e-8)
        tScreenExit = min(tScreenExit, (0.0 - uv0.y) / rayUV.y);

    float tEnd = clamp(tScreenExit, 0.0, 1.0);

    ivec2 size0 = textureSize(hiz, 0);
    // 与 CPU 端 HIZMipCount = 1+floor(log2(min(w,h))) 对齐，最高 mip 索引
    int maxMip = int(floor(log2(float(min(size0.x, size0.y)))));
    // SSR 在平面低角度反射时，过粗的 mip 会把一大片屏幕区域压成同一个 min-depth，
    // 容易沿扫描方向形成稳定的横向断层。限制最高 mip，牺牲少量性能换取更连续的命中。
    maxMip = min(maxMip, 5);

    float maxPixelExtent = max(abs(rayUV.x) * float(size0.x), abs(rayUV.y) * float(size0.y));
    if (maxPixelExtent < 1.0)
        return HiZTraceResult(false, vec2(0.0), 0.0);

    float invZ0 = 1.0 / max(startSS.z, 1e-6);
    float invZ1 = 1.0 / max(endSS.z, 1e-6);
    float invZDelta = invZ1 - invZ0;

    // 起始 t：跳过 1~traceStride 个 mip0 像素以避免自相交。
    float t = max(traceStride, 1.0) / maxPixelExtent;
    // 低角度射线对 t 偏移非常敏感，较大的 epsilon 会周期性跳过细小 cell，表现为横向条纹。
    float tEpsilon = 0.05 / maxPixelExtent;

    int mipLevel = 0;
    const int kMaxIterations = 512;

    for (int iter = 0; iter < kMaxIterations; iter++)
    {
        if (t >= tEnd)
            return HiZTraceResult(false, vec2(0.0), 0.0);

        vec2 uv = uv0 + rayUV * t;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            return HiZTraceResult(false, vec2(0.0), 0.0);

        ivec2 mipSize  = textureSize(hiz, mipLevel);
        vec2  cellSize = 1.0 / vec2(mipSize);
        ivec2 cellIdx  = clamp(ivec2(uv * vec2(mipSize)), ivec2(0), mipSize - 1);

        // POINT SAMPLE — 必须用 texelFetch，绕过 LINEAR sampler
        float sceneMinZ = texelFetch(hiz, cellIdx, mipLevel).r;

        // 计算当前 mip cell 的退出边界对应的 ray 参数 tExit。
        vec2 cellMin = vec2(cellIdx) * cellSize;
        vec2 cellMax = cellMin + cellSize;
        vec2 boundary = vec2(rayUV.x >= 0.0 ? cellMax.x : cellMin.x,
                             rayUV.y >= 0.0 ? cellMax.y : cellMin.y);

        float tx = (abs(rayUV.x) > 1e-8) ? (boundary.x - uv.x) / rayUV.x : 1e9;
        float ty = (abs(rayUV.y) > 1e-8) ? (boundary.y - uv.y) / rayUV.y : 1e9;
        float dt = max(0.0, min(tx, ty));
        float tExit = min(t + dt, tEnd);

        // 透视正确线性深度：在屏幕空间应线性插值 1/z，而不是 z。
        float zEnter = 1.0 / max(invZ0 + invZDelta * t, 1e-8);
        float zExit  = 1.0 / max(invZ0 + invZDelta * tExit, 1e-8);
        float zNear = min(zEnter, zExit);
        float zFar  = max(zEnter, zExit);

        // min-depth HiZ：cell 内最近几何体在 sceneMinZ 处
        // 若 ray 整段都比该值更近（zFar < sceneMinZ）→ 不可能撞到任何几何体 → 安全跨过
        if (sceneMinZ >= 1e4 || zFar < sceneMinZ)
        {
            t = tExit + tEpsilon;
            mipLevel = min(mipLevel + 1, maxMip);
        }
        else
        {
            // 可能命中：cell 内 ray 深度区间跨过 sceneMinZ。
            if (mipLevel <= 0)
            {
                // 已在最细 mip：解 1/z 方程得到 ray 与当前像素线性深度的交点。
                if (zNear <= sceneMinZ + thicknessThreshold && sceneMinZ <= zFar + thicknessThreshold)
                {
                    float tHit = t;
                    if (abs(invZDelta) > 1e-8)
                    {
                        tHit = (1.0 / max(sceneMinZ, 1e-6) - invZ0) / invZDelta;
                    }

                    if (tHit >= t - tEpsilon && tHit <= tExit + tEpsilon)
                    {
                        vec2 hitUV = uv0 + rayUV * clamp(tHit, 0.0, 1.0);
                        if (hitUV.x >= 0.0 && hitUV.x <= 1.0 && hitUV.y >= 0.0 && hitUV.y <= 1.0)
                            return HiZTraceResult(true, hitUV, sceneMinZ);
                    }
                }

                // 当前像素内没有有效前向交点，跳过该 cell。
                t = tExit + tEpsilon;
                mipLevel = min(mipLevel + 1, maxMip);
            }
            else
            {
                // mip > 0：命中 coarse cell，不推进，只下降一级 mip 细化。
                mipLevel--;
            }
        }
    }

    return HiZTraceResult(false, vec2(0.0), 0.0);
}

// --- 新增：ReSTIR 蓄水池 Buffer ---
struct ReservoirData 
{
    vec4 state;   // x: w_sum (权重和), y: M (样本数), z: W (最终权重), w: sampleIndex (选中的索引)
    vec4 radiance; // rgb: radiance (选中的颜色), w: padding
};

#endif