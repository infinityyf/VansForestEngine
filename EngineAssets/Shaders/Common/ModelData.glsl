#if !defined(ModelCBBind)
    #define ModelCBBind 1
#endif
layout(set=ModelCBBind, binding=0) uniform    ModelUniformBuffer 
{
    mat4 ModelMatrix;
    vec4 Position;
    vec4 Scale;
};
