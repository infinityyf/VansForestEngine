#version 450

// ---------------------------------------------------------------------------
// MotionVector fragment shader
// Outputs a screen-space motion vector (UV delta) encoding how each pixel
// moved from the previous frame to the current frame.
//
// Convention (matches SSGITemporal / FSR consumers):
//   motionVector = currentUV − previousUV
//   previousUV   = currentUV − motionVector
// ---------------------------------------------------------------------------

layout( location = 0 ) in vec4 vCurrentClipPos;
layout( location = 1 ) in vec4 vPreviousClipPos;

layout( location = 0 ) out vec4 outMotionVector;

void main()
{
    // Perspective divide → NDC  [-1, 1]
    vec2 currentNDC  = vCurrentClipPos.xy  / vCurrentClipPos.w;
    vec2 previousNDC = vPreviousClipPos.xy / vPreviousClipPos.w;

    // Vulkan clip-space Y points downward; flip so that UV.y increases upward
    // (consistent with texture sampling conventions used by SSR / SSGI).
    currentNDC.y  = -currentNDC.y;
    previousNDC.y = -previousNDC.y;

    // NDC → UV  [0, 1]
    vec2 currentUV  = currentNDC  * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;

    // Motion vector in UV space
    vec2 motionVector = currentUV - previousUV;

    outMotionVector = vec4(motionVector, 0.0, 1.0);
}
