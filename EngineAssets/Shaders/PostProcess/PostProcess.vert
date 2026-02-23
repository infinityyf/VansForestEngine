#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout(location = 0) in f16vec3 inPosition;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Pass through the vertex position to the clip space
    gl_Position = vec4(inPosition, 1.0);
    // Calculate texture coordinates from vertex position
    fragTexCoord = inPosition.xy;
}