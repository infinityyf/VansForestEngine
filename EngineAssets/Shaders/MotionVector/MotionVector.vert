#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"

// ---------------------------------------------------------------------------
// MotionVector vertex shader
//
// Descriptor sets (same layout as shadow pass):
//   Set 0 — Global  (CameraData UBO: VPMatrix, LastVPMatrix, …)
//   Set 1 — EmptyPass (unused placeholder)
//   Set 2 — Object  (Transform SSBO: ModelMatrix per object)
//
// Computes current-frame and previous-frame clip-space positions so the
// fragment shader can output a per-pixel screen-space motion vector.
//
// Currently captures CAMERA MOTION only (LastVPMatrix vs VPMatrix).
// Object / skeletal motion will be added when a PrevModelMatrix buffer
// is available on the CPU side — at that point, prevWorldPos should use
// prevModel * position and be projected with LastVPMatrix.
// ---------------------------------------------------------------------------

layout( location = 0 ) in f16vec4 position;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
} materialConst;

// Clip-space positions passed to the fragment shader.
// Full vec4 so the fragment shader can do the perspective divide.
layout( location = 0 ) out vec4 vCurrentClipPos;
layout( location = 1 ) out vec4 vPreviousClipPos;

void main()
{
    int objectIndex = materialConst.objectIndex;

    mat4 currentModel = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 prevModel    = ModelBuffer.transforms[objectIndex].PrevModelMatrix;

    // Current-frame world position
    vec4 worldPos = currentModel * position;

    // Current-frame clip position (unjittered，保证静止时 motionVec 精确为零)
    vec4 currentClip = UnjitteredVPMatrix * worldPos;

    // Previous-frame world position (using last frame's model matrix)
    vec4 prevWorldPos = prevModel * position;

    // Previous-frame clip position (unjittered last frame VP + last frame model)
    vec4 previousClip = LastUnjitteredVPMatrix * prevWorldPos;

    // gl_Position 仍用 jittered VPMatrix 保持 TAA 抖动对 GBuffer 的作用
    gl_Position      = VPMatrix * worldPos;
    vCurrentClipPos  = currentClip;
    vPreviousClipPos = previousClip;
}
