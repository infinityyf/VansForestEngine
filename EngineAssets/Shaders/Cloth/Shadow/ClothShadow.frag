#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/VansDrawSubmission.glsl"
#include "../ClothCoverage.glsl"
layout(set = 4, binding = 6) uniform sampler2D clothOpacity;
layout(location = 1) in vec2 fragUV;
void main()
{
    ClothMaterialPayload cloth = GetClothMaterialPayload(VansGetDrawData().materialIndex);
    float coverage = cloth.flags.x != 0u ? texture(clothOpacity, fragUV).r : 1.0;
    if (!ClothSurfaceVisible(cloth, gl_FrontFacing, coverage)) discard;
}
