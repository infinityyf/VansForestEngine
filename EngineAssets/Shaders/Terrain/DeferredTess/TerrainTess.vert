#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in uint instanceEdgeFlags;
layout(location = 6) in vec2 instanceMorphRange;

layout(location = 0) out vec2 vsOutLocalXZ;
layout(location = 1) out vec2 vsOutOffset;
layout(location = 2) out float vsOutScale;
layout(location = 3) flat out uint vsOutEdgeFlags;
layout(location = 4) out vec2 vsOutMorphRange;

void main()
{
    gl_Position = vec4(vec3(inPos), 1.0);
    vsOutLocalXZ = vec2(inPos.xz);
    vsOutOffset = instanceOffset;
    vsOutScale = instanceScale;
    vsOutEdgeFlags = instanceEdgeFlags;
    vsOutMorphRange = instanceMorphRange;
}
