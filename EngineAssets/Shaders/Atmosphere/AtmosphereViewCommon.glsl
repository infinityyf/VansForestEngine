#ifndef VANS_ATMOSPHERE_VIEW_COMMON_GLSL
#define VANS_ATMOSPHERE_VIEW_COMMON_GLSL

vec3 AtmosphereWorldDirectionFromUv(vec2 uv)
{
    vec2 ndc = uv * 2.0 - 1.0;
    // Compute image UV is top-to-bottom, while the projection matrix keeps
    // the engine's OpenGL-style clip-space Y convention.
    ndc.y *= -1.0;
    vec4 view = InverseProjectionMatrix * vec4(ndc, 1.0, 1.0);
    vec3 viewDirection = normalize(view.xyz / max(abs(view.w), 1.0e-6));
    return normalize((InverseViewMatrix * vec4(viewDirection, 0.0)).xyz);
}

// 主深度是天空/场景覆盖关系的唯一权威来源。GBuffer2 只由延迟材质写入，
// 不能代表写深度的 Forward Opaque，也不能用其 alpha 作为背景哨兵。
bool AtmosphereHasSurfaceDepth(float deviceDepth)
{
    // Depth is cleared to exactly 1.0 and opaque geometry uses LESS.  Comparing
    // against the clear value preserves valid geometry throughout the camera
    // range instead of silently turning very distant surfaces into sky.
    return deviceDepth < 1.0;
}

vec3 AtmosphereWorldPositionFromDeviceDepth(vec2 uv, float deviceDepth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y *= -1.0;
    vec4 viewPosition = InverseProjectionMatrix *
        vec4(ndc, deviceDepth, 1.0);
    viewPosition /= max(abs(viewPosition.w), 1.0e-6);
    return (InverseViewMatrix * vec4(viewPosition.xyz, 1.0)).xyz;
}

#endif
