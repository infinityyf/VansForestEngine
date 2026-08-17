#version 450

// Final composite order:
// Bloom -> Exposure -> White Balance -> Tone Mapping -> Color Grading.

layout(set = 1, binding = 0) uniform sampler2D colorInput;
layout(set = 1, binding = 1) uniform sampler2D bloomResult;
layout(set = 1, binding = 2) uniform sampler2D exposureEV;
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
    float  m_DebugPassthrough;

	float  m_TimelineFadeColorR;
	float  m_TimelineFadeColorG;
	float  m_TimelineFadeColorB;
	float  m_TimelineFadeOpacity;

    int    m_EnableDOF;
    int    m_EnableAutoExposure;
    float  _pad9;
    float  _pad10;
} uPP;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

float Luminance(vec3 rgb)
{
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ReinhardExtended(vec3 x, float whitePoint)
{
    float wp2 = whitePoint * whitePoint;
    return x * (1.0 + x / wp2) / (1.0 + x);
}

vec3 RGBtoHSV(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 HSVtoRGB(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 WhiteBalance(vec3 rgb, float temperature, float tint)
{
    rgb.r += temperature * 0.1;
    rgb.b -= temperature * 0.1;
    rgb.g += tint * 0.05;
    return max(rgb, vec3(0.0));
}

void main()
{
    vec2 uv;
    uv.x = fragTexCoord.x * 0.5 + 0.5;
    uv.y = -fragTexCoord.y * 0.5 + 0.5;

    vec2 colorTexel = 1.0 / vec2(textureSize(colorInput, 0));
    vec2 colorUV = clamp(uv, colorTexel * 0.5, vec2(1.0) - colorTexel * 0.5);
    vec3 hdr = texture(colorInput, colorUV).rgb;
    if (uPP.m_DebugPassthrough > 0.5)
    {
        vec3 debugColor = hdr;
        if (any(isnan(debugColor)) || any(isinf(debugColor)))
            debugColor = vec3(1.0, 0.0, 1.0);
        else
            debugColor = debugColor / (vec3(1.0) + max(debugColor, vec3(0.0)));
        outColor = vec4(clamp(debugColor, 0.0, 1.0), 1.0);
        return;
    }
    vec2 bloomTexel = 1.0 / vec2(textureSize(bloomResult, 0));
    vec2 bloomUV = clamp(uv, bloomTexel * 0.5, vec2(1.0) - bloomTexel * 0.5);
    vec3 bloom = texture(bloomResult, bloomUV).rgb;
    hdr += bloom * uPP.m_BloomIntensity;

	float currentEV = uPP.m_EnableAutoExposure != 0
        ? texelFetch(exposureEV, ivec2(0, 0), 0).r
        : 0.0;
	if (isnan(currentEV) || isinf(currentEV))
		currentEV = 0.0;
	hdr *= exp2(currentEV + uPP.m_ExposureCompensation);

    if (uPP.m_EnableColorGrading != 0)
        hdr = WhiteBalance(hdr, uPP.m_Temperature, uPP.m_Tint);

    vec3 ldr;
    if (uPP.m_ToneMapperType == 0)
        ldr = clamp(hdr, 0.0, 1.0);
    else if (uPP.m_ToneMapperType == 1)
        ldr = ACESFilm(hdr);
    else
        ldr = ReinhardExtended(hdr, uPP.m_WhitePoint);

    if (uPP.m_EnableColorGrading != 0)
    {
        ldr = clamp((ldr - 0.5) * uPP.m_Contrast + 0.5, 0.0, 1.0);
        float lum = Luminance(ldr);
        ldr = max(mix(vec3(lum), ldr, uPP.m_Saturation), vec3(0.0));

        if (abs(uPP.m_HueShift) > 0.001)
        {
            vec3 hsv = RGBtoHSV(ldr);
            hsv.x = fract(hsv.x + uPP.m_HueShift);
            ldr = HSVtoRGB(hsv);
        }
    }

	vec3 fadeColor = vec3(
		uPP.m_TimelineFadeColorR,
		uPP.m_TimelineFadeColorG,
		uPP.m_TimelineFadeColorB);
	ldr = mix(ldr, fadeColor, clamp(uPP.m_TimelineFadeOpacity, 0.0, 1.0));

	outColor = vec4(clamp(ldr, 0.0, 1.0), 1.0);
}
