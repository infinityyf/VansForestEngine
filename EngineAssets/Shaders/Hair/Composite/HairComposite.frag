#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 1, binding = 0) uniform sampler2D hairColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 hair = texture(hairColor, fragUV);
    if (hair.a <= 0.0001)
        discard;

    outColor = hair;
}
