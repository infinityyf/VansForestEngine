#version 450

layout(set = 1, binding = 0, input_attachment_index = 0) uniform subpassInput normalInput;
layout(set = 1, binding = 1, input_attachment_index = 1) uniform subpassInput gbufferInput0;
layout(set = 1, binding = 2, input_attachment_index = 2) uniform subpassInput gbufferInput1;

layout(location = 0) out vec4 outColor;

void main() 
{
    vec3 normal = subpassLoad(normalInput).xyz;
    vec4 color = subpassLoad(gbufferInput0);
    float metallic = subpassLoad(gbufferInput1).x;

    // Output the processed color
    outColor = vec4(color.rgb + normal,metallic);
    //outColor = vec4(1.0,0.0,0.0,1.0);
}