#version 450
layout( location = 0 ) in vec2 fragTexCoord;
layout(set = 0, binding = 0) uniform sampler2D  normalInput;
layout(set = 0, binding = 1) uniform sampler2D  gbufferInput0;
layout(set = 0, binding = 2) uniform sampler2D  gbufferInput1;
layout(set = 0, binding = 3) uniform sampler2D  gbufferInput2;
layout(set = 0, binding = 4) uniform sampler2D  depthInput;
layout(set = 0, binding = 5, rgba32f ) uniform image2D outColor;

void main() 
{
    vec3 color = texture( normalInput, fragTexCoord ).rgb;
    imageStore(outColor, ivec2(fragTexCoord * 100), vec4(color, 1.0));
}