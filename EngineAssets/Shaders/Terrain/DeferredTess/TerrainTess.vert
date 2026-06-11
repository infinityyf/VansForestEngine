#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

// Thin vertex shader for tessellation pipeline.
// Does NOT sample heightmap — passes local coords and instance data through to TCS.
// Heightmap displacement happens in the TES after tessellation.

// Vertex input (16x16 base patch, local coords 0..16)
layout(location = 0) in f16vec3 inPos;
layout(location = 1) in f16vec2 inUV;
layout(location = 2) in f16vec3 inNormal;

// Instance input
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags;

// Output to TCS (per-vertex, NOT world-space displaced)
layout(location = 0) out vec2 vsOutUV;
layout(location = 1) out vec2 vsOutLocalXZ;
layout(location = 2) out vec2 vsOutOffset;
layout(location = 3) out float vsOutScale;
layout(location = 4) out float vsOutLod;
layout(location = 5) out float vsOutStitchFlags;

void main() {
    // Output local-space position (w=1 ensures linear barycentric interpolation in TES)
    gl_Position = vec4(vec3(inPos), 1.0);

    vsOutUV           = vec2(inUV);
    vsOutLocalXZ      = vec2(inPos.xz);
    vsOutOffset       = instanceOffset;
    vsOutScale        = instanceScale;
    vsOutLod          = instanceLod;
    vsOutStitchFlags  = instanceStitchFlags;
}
