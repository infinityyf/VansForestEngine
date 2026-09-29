#version 450
#extension GL_GOOGLE_include_directive : require
#include "../HairVisibilityData.glsl"
layout(set=1, binding=10) uniform sampler2D opaqueDepth;
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint layer = hairLayerDepths[HairPixelBase(pixel, uint(textureSize(opaqueDepth, 0).x)) + HAIR_LAYER_COUNT - 1u];
    // Empty/partially occupied pixels retain opaque occlusion. Never modify main depth.
    float hairDepth = layer == 0xffffffffu ? 1.0 : uintBitsToFloat(layer);
    gl_FragDepth = min(hairDepth, texelFetch(opaqueDepth, pixel, 0).r);
}
