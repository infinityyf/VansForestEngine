#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"
#include "../Atmosphere/AtmosphereCommon.glsl"
#include "../Atmosphere/AtmosphereMediaComposition.glsl"

// ── 从顶点着色器接收 ─────────────────────────────────────────────────────
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;

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
    vec4 color = texSample * fragColor;
    vec2 screenUv = gl_FragCoord.xy / max(ScreenParams.xy, vec2(1.0));
    color.rgb = CompositeAtmosphereSurfaceRadiance(
        screenUv, fragWorldPos, color.rgb);
    outColor = color;
}
