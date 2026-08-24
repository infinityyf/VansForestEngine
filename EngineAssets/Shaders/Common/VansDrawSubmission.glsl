#ifndef VANS_DRAW_SUBMISSION_GLSL
#define VANS_DRAW_SUBMISSION_GLSL

// Must match VansDrawInstanceDataGPU. The vertex stage resolves gl_InstanceIndex
// once and forwards the record unchanged to the fragment stage.
struct VansDrawData
{
    int transformIndex;
    int materialIndex;
    uint vertexFeatureMask;
    int passUser0;
};

VansDrawData VansDecodeDrawData(uvec4 rawData)
{
    VansDrawData data;
    data.transformIndex = int(rawData.x);
    data.materialIndex = int(rawData.y);
    data.vertexFeatureMask = rawData.z;
    data.passUser0 = int(rawData.w);
    return data;
}

#if defined(GL_VERTEX_SHADER)

layout(std430, set = 2, binding = 1) readonly buffer VansDrawInstanceDataBuffer
{
    uvec4 records[];
} vansDrawInstanceData;

layout(location = 15) flat out uvec4 vansDrawDataPayload;

VansDrawData VansGetDrawData()
{
    vansDrawDataPayload = vansDrawInstanceData.records[gl_InstanceIndex];
    return VansDecodeDrawData(vansDrawDataPayload);
}

#elif defined(GL_FRAGMENT_SHADER)

layout(location = 15) flat in uvec4 vansDrawDataPayload;

VansDrawData VansGetDrawData()
{
    return VansDecodeDrawData(vansDrawDataPayload);
}

#else
#error VansDrawSubmission.glsl is only valid in vertex and fragment shaders
#endif

#endif
