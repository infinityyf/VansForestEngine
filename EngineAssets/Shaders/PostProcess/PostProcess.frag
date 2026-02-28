#version 450

layout(set = 1, binding = 0, input_attachment_index = 0) uniform subpassInput colorInput;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample the input attachment
    vec4 color = subpassLoad(colorInput);

    // //linear to sRGB
    // //remap to LDR
    // color.rgb = color.rgb / (color.rgb + vec3(1.0));
    // color.rgb = pow(color.rgb, vec3(1.0/2.2));  

    // // Apply a grayscale effect
    // float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    // vec4 grayscaleColor = vec4(vec3(gray), color.a);

    // Output the processed color
    outColor = color;
    //outColor = vec4(1.0,0.0,0.0,1.0);
}