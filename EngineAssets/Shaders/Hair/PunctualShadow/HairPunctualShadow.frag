#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragUV;
layout(set = 4, binding = 1) uniform sampler2D hairAlpha;
layout(set = 4, binding = 8) uniform HairParamsBlock
{
    vec4 absorption;
    vec4 roughnessScale;
    vec4 shiftParams;
    vec4 coverageParams;
} hairParams;
void main()
{
    float shadowCutoff = hairParams.coverageParams.z;
    float coverageScale = hairParams.coverageParams.y;
    float alpha = texture(hairAlpha, fragUV).r;
    float coverage = clamp((alpha - shadowCutoff) * coverageScale, 0.0, 1.0);
    if (coverage <= 0.0001)
        discard;
}
