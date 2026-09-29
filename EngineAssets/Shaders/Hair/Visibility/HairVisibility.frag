#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/CameraData.glsl"
#include "../HairMaterial.glsl"
#include "../HairVisibilityData.glsl"
layout(location=0) in vec2 fragUV;
layout(location=0) out float outOpticalDepth;
layout(early_fragment_tests) in;
void main()
{
    float alpha = HairCoverage(fragUV);
    if (alpha <= 0.0001) discard;
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint base = HairPixelBase(pixel, uint(ScreenParams.x));
    uint depth = floatBitsToUint(gl_FragCoord.z);
    for (uint i=0u; i<HAIR_LAYER_COUNT; ++i)
    {
        depth = max(depth, atomicMin(hairLayerDepths[base+i], depth));
        // 空槽之后没有需要继续向后插入的有效深度。
        if (depth == 0xffffffffu) break;
    }
    // R32_SFLOAT 附件以 ONE + ONE 混合累加，禁止恢复同像素 CAS 重试。
    outOpticalDepth = -log(max(1.0-alpha, 1e-7));
}
