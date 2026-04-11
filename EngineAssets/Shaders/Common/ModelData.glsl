#if !defined(ModelCBBind)
    #define ModelCBBind 2
#endif
// layout(set=ModelCBBind, binding=0) uniform    ModelUniformBuffer 
// {
//     mat4 ModelMatrix;
//     mat4 NormalMatrix;
//     vec4 Position;
//     vec4 Scale;
// };
struct TransformData
{
    mat4 ModelMatrix;
    mat4 NormalMatrix;
    vec4 Position;
    vec4 Scale;
    mat4 PrevModelMatrix;
};
layout(std430, set = ModelCBBind, binding = 0) readonly buffer ObjectModelBuffer 
{
    TransformData transforms[];
} ModelBuffer;
