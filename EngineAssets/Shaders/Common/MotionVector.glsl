#ifndef VANS_MOTION_VECTOR_GLSL
#define VANS_MOTION_VECTOR_GLSL

// Motion vectors use top-left texture UV coordinates and encode the displacement
// from the previous frame to the current frame. Keep every producer on this one
// conversion path so geometry, terrain, and sky remain interchangeable for TAA,
// DLSS, and FSR.
vec2 VansMotionVectorFromClip(vec4 currentClip, vec4 previousClip)
{
    if (abs(currentClip.w) < 1.0e-6 || abs(previousClip.w) < 1.0e-6)
        return vec2(0.0);

    vec2 currentNDC = currentClip.xy / currentClip.w;
    vec2 previousNDC = previousClip.xy / previousClip.w;
    currentNDC.y = -currentNDC.y;
    previousNDC.y = -previousNDC.y;

    vec2 motion = (currentNDC - previousNDC) * 0.5;
    return any(isnan(motion)) || any(isinf(motion)) ? vec2(0.0) : motion;
}

#endif
