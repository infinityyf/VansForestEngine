#ifndef VANS_ANIMATION_SKINNING_GLSL
#define VANS_ANIMATION_SKINNING_GLSL

layout(std430, set = 3, binding = 0) readonly buffer VansAnimationBoneIDBuffer
{
    ivec4 boneIDs[];
} vansAnimationBoneIDs;

layout(std430, set = 3, binding = 1) readonly buffer VansAnimationBoneMatrixBuffer
{
    mat4 boneMatrices[];
} vansAnimationBones;

layout(std430, set = 3, binding = 2) readonly buffer VansAnimationBoneWeightBuffer
{
    vec4 weights[];
} vansAnimationWeights;

mat4 VansBuildSkinMatrix()
{
    ivec4 ids = vansAnimationBoneIDs.boneIDs[gl_VertexIndex];
    vec4 weights = vansAnimationWeights.weights[gl_VertexIndex];
    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;
    for (int influence = 0; influence < 4; ++influence)
    {
        if (ids[influence] >= 0)
        {
            skinMatrix += weights[influence] * vansAnimationBones.boneMatrices[ids[influence]];
            totalWeight += weights[influence];
        }
    }
    return totalWeight > 0.0001 ? skinMatrix : mat4(1.0);
}

void VansApplyAnimationSkinning(inout vec4 position, inout vec3 normal,
                                inout vec3 tangent, inout vec3 bitangent)
{
    mat4 skinMatrix = VansBuildSkinMatrix();
    position = skinMatrix * position;
    mat3 skinNormal = mat3(skinMatrix);
    normal = skinNormal * normal;
    tangent = skinNormal * tangent;
    bitangent = skinNormal * bitangent;
}

void VansApplyAnimationSkinningPosition(inout vec4 position)
{
    position = VansBuildSkinMatrix() * position;
}

#endif
