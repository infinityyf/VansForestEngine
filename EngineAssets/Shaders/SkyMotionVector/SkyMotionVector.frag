#version 450

layout(location = 0) in vec4 vCurrentClipPos;
layout(location = 1) in vec4 vPreviousClipPos;

layout(location = 0) out vec2 outMotionVector;

void main()
{
    if (abs(vCurrentClipPos.w) < 1.0e-6 || abs(vPreviousClipPos.w) < 1.0e-6)
    {
        outMotionVector = vec2(0.0);
        return;
    }

    vec2 currentNDC = vCurrentClipPos.xy / vCurrentClipPos.w;
    vec2 previousNDC = vPreviousClipPos.xy / vPreviousClipPos.w;

    // 与普通 MotionVector pass 的左上角纹理 UV 约定保持一致。
    currentNDC.y = -currentNDC.y;
    previousNDC.y = -previousNDC.y;

    vec2 motion = (currentNDC - previousNDC) * 0.5;
    outMotionVector = any(isnan(motion)) || any(isinf(motion))
        ? vec2(0.0)
        : motion;
}
