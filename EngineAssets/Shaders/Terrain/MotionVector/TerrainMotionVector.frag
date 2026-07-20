#version 450

// 地形 Motion Vector 片元阶段。
// 输出 UV 空间速度：currentUV - previousUV。

layout( location = 0 ) in vec4 vCurrentClipPos;
layout( location = 1 ) in vec4 vPreviousClipPos;

layout( location = 0 ) out vec4 outMotionVector;

void main()
{
    // 透视除法得到 NDC，范围为 [-1, 1]。
    vec2 currentNDC  = vCurrentClipPos.xy  / vCurrentClipPos.w;
    vec2 previousNDC = vPreviousClipPos.xy / vPreviousClipPos.w;

    // Vulkan 裁剪空间 Y 方向与 UV 约定相反，这里翻转。
    currentNDC.y  = -currentNDC.y;
    previousNDC.y = -previousNDC.y;

    // NDC 转 UV，范围为 [0, 1]。
    vec2 currentUV  = currentNDC  * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;

    // UV 空间 motion vector。
    vec2 motionVector = currentUV - previousUV;

    outMotionVector = vec4(motionVector, 0.0, 1.0);
}
