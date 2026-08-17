#ifndef VANS_SKELETAL_SKINNING_GLSL
#define VANS_SKELETAL_SKINNING_GLSL

layout(std430, set = 3, binding = 0) readonly buffer VansSkinningBoneIDBuffer
{
    ivec4 boneIDs[];
} vansSkinningBoneIDs;

layout(std430, set = 3, binding = 1) readonly buffer VansSkinningCurrentBoneMatrixBuffer
{
    mat4 boneMatrices[];
} vansSkinningCurrentBones;

layout(std430, set = 3, binding = 2) readonly buffer VansSkinningBoneWeightBuffer
{
    vec4 weights[];
} vansSkinningWeights;

layout(std430, set = 3, binding = 3) readonly buffer VansSkinningPreviousBoneMatrixBuffer
{
    mat4 boneMatrices[];
} vansSkinningPreviousBones;

mat4 VansBuildCurrentSkinMatrix()
{
    ivec4 ids = vansSkinningBoneIDs.boneIDs[gl_VertexIndex];
    vec4 weights = vansSkinningWeights.weights[gl_VertexIndex];

    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;
    for (int influence = 0; influence < 4; ++influence)
    {
        if (ids[influence] >= 0)
        {
            skinMatrix += weights[influence] * vansSkinningCurrentBones.boneMatrices[ids[influence]];
            totalWeight += weights[influence];
        }
    }

    return totalWeight > 0.0001 ? skinMatrix : mat4(1.0);
}

mat4 VansBuildPreviousSkinMatrix()
{
    ivec4 ids = vansSkinningBoneIDs.boneIDs[gl_VertexIndex];
    vec4 weights = vansSkinningWeights.weights[gl_VertexIndex];

    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;
    for (int influence = 0; influence < 4; ++influence)
    {
        if (ids[influence] >= 0)
        {
            skinMatrix += weights[influence] * vansSkinningPreviousBones.boneMatrices[ids[influence]];
            totalWeight += weights[influence];
        }
    }

    return totalWeight > 0.0001 ? skinMatrix : mat4(1.0);
}

#endif
