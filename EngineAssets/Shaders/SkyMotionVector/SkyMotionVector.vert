#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/CameraData.glsl"

layout(location = 0) in f16vec4 position;

layout(location = 0) out vec4 vCurrentClipPos;
layout(location = 1) out vec4 vPreviousClipPos;

void main()
{
    // 天空是无穷远方向，w=0 可从 View/VP 变换中消除相机平移。
    vec4 directionWS = vec4(vec3(position), 0.0);

    // 当前颜色覆盖使用 jittered 矩阵；速度本身使用无抖动的前后帧 VP。
    vec4 rasterClip = ProjectionMatrix * ViewMatrix * directionWS;
    gl_Position = rasterClip.xyzz;
    vCurrentClipPos = UnjitteredVPMatrix * directionWS;
    vPreviousClipPos = LastUnjitteredVPMatrix * directionWS;
}
