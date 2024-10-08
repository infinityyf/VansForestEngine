//可以使用pushconstance频繁修改数据，但是大小有限，同一个shader只能使用一个
//caerma set
layout(set=0, binding=0) uniform    CameraUniformBuffer 
{
    mat4 ModelMatrix;
    vec4 cameraPosition;
    vec4 cameraDirection;
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
};
