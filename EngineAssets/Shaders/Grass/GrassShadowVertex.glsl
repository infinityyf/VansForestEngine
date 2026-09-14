#include "../Lights/LightsData.glsl"
#include "GrassGeometry.glsl"
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 frag_uv;
layout(push_constant) uniform GrassShadowPC
{
    uint boneCount;
    uint subBladeCount;
    int shadowIndex;
    uint visibleOffset;
} pc;
void main()
{
    // 使用距离筛选后的独立列表，主相机不可见的近处植被仍可投影。
    uint instanceIndex = instanceRemap[pc.visibleOffset + gl_InstanceIndex / pc.subBladeCount];
    uint subBladeIndex = gl_InstanceIndex % pc.subBladeCount;
    vec3 worldPosition = grassWorldPosition(inPosition, instanceIndex, subBladeIndex,
        pc.boneCount, boneWeights[gl_VertexIndex]);
    vec4 clip = uDirectionLight.shadowMatrix[pc.shadowIndex] * vec4(worldPosition, 1.0);
    clip.z = clip.z * 0.5 + clip.w * 0.5;
    gl_Position = clip;
    frag_uv = inUV;
}
