//可以使用pushconstance频繁修改数据，但是大小有限，同一个shader只能使用一个
#if !defined(CameraCBBind)
    #define CameraCBBind 0
#endif
//caerma set
layout(set=CameraCBBind, binding=0) uniform    CameraUniformBuffer 
{
    vec4 cameraPosition;
    vec4 cameraDirection;
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
};
