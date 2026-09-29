#ifndef VANS_HAIR_MATERIAL_GLSL
#define VANS_HAIR_MATERIAL_GLSL
layout(set=4, binding=0) uniform sampler2D hairAlbedo;
layout(set=4, binding=1) uniform sampler2D hairAlpha;
layout(set=4, binding=2) uniform sampler2D hairNormal;
layout(set=4, binding=3) uniform sampler2D hairRoughness;
layout(set=4, binding=4) uniform sampler2D hairAO;
layout(set=4, binding=5) uniform sampler2D hairFlow;
#include "../Common/VansDrawSubmission.glsl"
struct HairParams
{
    vec4 scattering; // 纵向粗糙度、方位粗糙度、毛鳞片倾角（弧度）、法线强度
    vec4 coverage;   // cutoff、scale、flowStrength、castShadows
    vec4 occlusion;  // AO 强度，其余为 ABI 填充
};
layout(std430, set=4, binding=6) readonly buffer HairMaterialBuffer { HairParams hairMaterialParams[]; };
#define hairParams hairMaterialParams[VansGetDrawData().materialIndex]
float HairCoverage(vec2 uv)
{
    return clamp((texture(hairAlpha, uv).r - hairParams.coverage.x) * hairParams.coverage.y, 0.0, 1.0);
}
vec3 HairSafeNormalize(vec3 v, vec3 fallback)
{
    float len2 = dot(v,v);
    return len2 > 1e-10 ? v * inversesqrt(len2) : fallback;
}
// 阴影使用确定性 alpha test；覆盖率的连续积分由可见性/光学厚度路径负责。
bool HairCastsShadow(vec2 uv)
{
    return hairParams.coverage.w > 0.5 && HairCoverage(uv) >= 0.5;
}
#endif
