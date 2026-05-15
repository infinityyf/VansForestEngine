#version 450
#extension GL_GOOGLE_include_directive : require

// ── 从顶点着色器接收 ─────────────────────────────────────────────────────
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;

// ── 粒子纹理（Set 1, binding 0） ─────────────────────────────────────────
layout(set = 1, binding = 0) uniform sampler2D particleTex;

// ── 输出 ─────────────────────────────────────────────────────────────────
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texSample = texture(particleTex, fragUV);

    // 丢弃全透明片元（避免深度写入开销）
    if (texSample.a < 0.004)
        discard;

    // 实例颜色与纹理相乘（支持颜色渐变模块调制）
    outColor = texSample * fragColor;
}
