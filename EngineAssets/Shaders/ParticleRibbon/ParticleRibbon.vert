#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 inUV;
layout(location=0) out vec4 fragColor;
layout(location=1) out vec2 fragUV;
layout(location=2) out vec3 fragWorldPos;
layout(push_constant) uniform RibbonParameters { vec4 parameters; } ribbon;
void main()
{
    gl_Position = VPMatrix*vec4(inPosition,1);
    fragColor = inColor; fragUV = inUV; fragWorldPos = inPosition;
}
