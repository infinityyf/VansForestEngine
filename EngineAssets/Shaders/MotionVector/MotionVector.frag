#version 450

// Outputs current-to-previous screen-space motion in UV units. FSR converts
// this engine convention at the API boundary with a negative vector scale.

layout(location = 0) in vec4 vCurrentClipPos;
layout(location = 1) in vec4 vPreviousClipPos;

layout(location = 0) out vec2 outMotionVector;

void main()
{
    // Perspective divide to NDC [-1, 1].
    vec2 currentNDC = vCurrentClipPos.xy / vCurrentClipPos.w;
    vec2 previousNDC = vPreviousClipPos.xy / vPreviousClipPos.w;

    // Match the texture sampling convention used by the temporal consumers.
    currentNDC.y = -currentNDC.y;
    previousNDC.y = -previousNDC.y;

    vec2 currentUV = currentNDC * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;
    outMotionVector = currentUV - previousUV;
}
