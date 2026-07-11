#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float lightDepth;

layout(set = 4, binding = 1) uniform sampler2D hairAlpha;
layout(set = 4, binding = 8) uniform HairParamsBlock
{
    vec4 absorption;
    vec4 roughnessScale;
    vec4 shiftParams;
    vec4 coverageParams;
} hairParams;

layout(location = 0) out vec4 outDeepOpacity;

vec4 ComputeFourSliceWeights(float s)
{
    float x = clamp(s, 0.0, 0.9999) * 4.0;
    int idx = int(floor(x));
    if (idx == 0) return vec4(1.0, 0.0, 0.0, 0.0);
    if (idx == 1) return vec4(0.0, 1.0, 0.0, 0.0);
    if (idx == 2) return vec4(0.0, 0.0, 1.0, 0.0);
    return vec4(0.0, 0.0, 0.0, 1.0);
}

void main()
{
    float shadowCutoff = hairParams.coverageParams.z;
    float coverageScale = hairParams.coverageParams.y;
    float shadowDensity = hairParams.coverageParams.w;

    float alpha = texture(hairAlpha, fragUV).r;
    float coverage = clamp((alpha - shadowCutoff) * coverageScale, 0.0, 1.0);
    if (coverage <= 0.0001)
        discard;

    float density = coverage * shadowDensity;
    outDeepOpacity = density * ComputeFourSliceWeights(lightDepth);
}
