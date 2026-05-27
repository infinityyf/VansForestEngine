#version 450

// ============================================================
// PostProcess Final Composite
// 操作顺序：Bloom 合并 → 曝光应用 → ACES 色调映射 →
//           色彩分级（对比度/饱和度/色温） → 暗角 → 胶片颗粒 → 抖动
// ============================================================

layout(set = 1, binding = 0, input_attachment_index = 0) uniform subpassInput colorInput;
layout(set = 1, binding = 1) uniform sampler2D bloomResult;     // Bloom 最终 MIP（半分辨率）
layout(set = 1, binding = 2) uniform sampler2D exposureEV;      // 1x1 当前 EV 值（自动曝光）
layout(set = 1, binding = 3) uniform PostProcessParams
{
    // Exposure
    float  m_ExposureCompensation;
    float  _pad0;
    float  _pad1;
    float  _pad2;

    // Bloom
    float  m_BloomIntensity;
    float  m_BloomScatter;
    float  _pad3;
    float  _pad4;

    // Tone Mapping
    int    m_ToneMapperType;
    float  m_WhitePoint;
    float  _pad5;
    float  _pad6;

    // Color Grading
    int    m_EnableColorGrading;
    float  m_Contrast;
    float  m_Saturation;
    float  m_HueShift;

    float  m_Temperature;
    float  m_Tint;
    float  _pad7;
    float  _pad8;

    // Vignette
    int    m_EnableVignette;
    float  m_VignetteIntensity;
    float  m_VignetteSmoothness;
    float  _pad9;

    // Film Grain
    int    m_EnableFilmGrain;
    float  m_FilmGrainIntensity;
    float  m_Time;
    float  _pad10;

    // Dithering
    int    m_EnableDithering;
    float  _pad11;
    float  _pad12;
    float  _pad13;
} uPP;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// -------- 亮度辅助 --------
float Luminance(vec3 rgb) { return dot(rgb, vec3(0.2126, 0.7152, 0.0722)); }

// -------- ACES Filmic 色调映射（Hill ACES approximate） --------
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// -------- Reinhard 扩展（白点） --------
vec3 ReinhardExtended(vec3 x, float whitePoint)
{
    float wp2 = whitePoint * whitePoint;
    return x * (1.0 + x / wp2) / (1.0 + x);
}

// -------- RGB ↔ HSV --------
vec3 RGBtoHSV(vec3 c)
{
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 HSVtoRGB(vec3 c)
{
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// -------- 简单色温偏移（红蓝通道） --------
vec3 WhiteBalance(vec3 rgb, float temperature, float tint)
{
    // temperature > 0 偏暖（增红减蓝），< 0 偏冷
    rgb.r += temperature * 0.1;
    rgb.b -= temperature * 0.1;
    // tint > 0 偏绿，< 0 偏品红
    rgb.g += tint * 0.05;
    return max(rgb, vec3(0.0));
}

// -------- Hash 随机（film grain 用） --------
float Hash(vec2 p)
{
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

void main()
{
    // 获取屏幕 UV：PostProcess.vert 将 clip-space XY 直接传入（未做 [0,1] 转换）
    // 这里手动换算，并翻转 Y 轴与 Deferred 顶点着色器保持一致
    vec2 uv;
    uv.x =  fragTexCoord.x * 0.5 + 0.5;
    uv.y = -fragTexCoord.y * 0.5 + 0.5;

    // 读取 HDR 颜色（subpassLoad，无 UV 插值）
    vec3 hdr = subpassLoad(colorInput).rgb;

    // ---- 1. Bloom 混合（HDR 空间） ----
    vec3 bloom = texture(bloomResult, uv).rgb;
    hdr += bloom * uPP.m_BloomIntensity;

    // ---- 2. 曝光应用（EV → 线性乘数） ----
    float currentEV   = texelFetch(exposureEV, ivec2(0, 0), 0).r;
    float totalEV     = currentEV + uPP.m_ExposureCompensation;
    float exposureMul = pow(2.0, totalEV);
    hdr *= exposureMul;

    // ---- 3. 色温白平衡（HDR 空间） ----
    if (uPP.m_EnableColorGrading != 0)
    {
        hdr = WhiteBalance(hdr, uPP.m_Temperature, uPP.m_Tint);
    }

    // ---- 4. 色调映射（HDR → LDR） ----
    vec3 ldr;
    if (uPP.m_ToneMapperType == 0)
    {
        ldr = clamp(hdr, 0.0, 1.0);
    }
    else if (uPP.m_ToneMapperType == 1)
    {
        ldr = ACESFilm(hdr);
    }
    else
    {
        ldr = ReinhardExtended(hdr, uPP.m_WhitePoint);
    }

    // ---- 5. 色彩分级 (LDR 空间) ----
    if (uPP.m_EnableColorGrading != 0)
    {
        // 对比度（围绕 0.5 拉伸）
        ldr = clamp((ldr - 0.5) * uPP.m_Contrast + 0.5, 0.0, 1.0);

        // 饱和度（混合向灰度）
        float lum = Luminance(ldr);
        ldr = mix(vec3(lum), ldr, uPP.m_Saturation);
        ldr = max(ldr, vec3(0.0));

        // 色相偏移
        if (abs(uPP.m_HueShift) > 0.001)
        {
            vec3 hsv = RGBtoHSV(ldr);
            hsv.x = fract(hsv.x + uPP.m_HueShift);
            ldr = HSVtoRGB(hsv);
        }
    }

    // ---- 6. 暗角 ----
    if (uPP.m_EnableVignette != 0)
    {
        vec2 centered = uv * 2.0 - 1.0;
        float dist = length(centered);
        float vignette = smoothstep(
            1.0 - uPP.m_VignetteSmoothness,
            1.0 + uPP.m_VignetteSmoothness,
            dist);
        ldr *= 1.0 - vignette * uPP.m_VignetteIntensity;
    }

    // ---- 7. 胶片颗粒 ----
    if (uPP.m_EnableFilmGrain != 0)
    {
        float grain = Hash(uv + vec2(uPP.m_Time * 0.1, uPP.m_Time * 0.07)) - 0.5;
        ldr += grain * uPP.m_FilmGrainIntensity;
    }

    // ---- 8. 抖动（8-bit 量化噪声减少色阶带） ----
    if (uPP.m_EnableDithering != 0)
    {
        float dither = Hash(uv * vec2(1920.0, 1080.0) + 0.5) - 0.5;
        ldr += dither * (1.0 / 255.0);
    }

    outColor = vec4(clamp(ldr, 0.0, 1.0), 1.0);
}
