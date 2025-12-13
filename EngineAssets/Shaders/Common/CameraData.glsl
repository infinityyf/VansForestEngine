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
    mat4 LastViewMatrix;
    mat4 LastProjectionMatrix;
    mat4 LastPrevViewMatrix;
    mat4 LastPrevProjectionMatrix;
    mat4 InverseViewMatrix;
    mat4 InverseProjectionMatrix;
    vec4 ScreenParams;
    vec4 FrameParams;
    vec4 CameraParams; // x: near, y: far, z: fov, w: aspect
};

#define FrameIndex FrameParams.x
#define FrameTime FrameParams.y

#define NearPlane CameraParams.x
#define FarPlane CameraParams.y
#define Fov CameraParams.z
#define Aspect CameraParams.w
