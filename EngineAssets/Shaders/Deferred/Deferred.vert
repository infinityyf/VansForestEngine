#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Pass through the vertex position to the clip space
    gl_Position = vec4(inPosition, 1.0);
    // Calculate texture coordinates from vertex position
    fragTexCoord = inPosition.xy * 0.5 + 0.5;
    fragTexCoord.y = 1.0 - fragTexCoord.y;
}