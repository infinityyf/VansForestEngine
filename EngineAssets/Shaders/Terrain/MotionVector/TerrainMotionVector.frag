#version 450

layout(location = 0) in vec4 vCurrentClipPos;
layout(location = 1) in vec4 vPreviousClipPos;

layout(location = 0) out vec4 outMotionVector;

void main()
{
    vec2 currentNDC = vCurrentClipPos.xy / vCurrentClipPos.w;
    vec2 previousNDC = vPreviousClipPos.xy / vPreviousClipPos.w;

    // Vulkan clip-space Y is inverted relative to UV-space.
    currentNDC.y = -currentNDC.y;
    previousNDC.y = -previousNDC.y;

    vec2 currentUV = currentNDC * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;

    outMotionVector = vec4(currentUV - previousUV, 0.0, 1.0);
}
