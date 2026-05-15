#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"

// ── Quad 顶点（binding 0，per-vertex） ────────────────────────────────────
layout(location = 0) in vec2 inLocalPos;   // 局部位置 [-0.5, 0.5]
layout(location = 1) in vec2 inUV;         // 基础 UV

// ── 实例数据（binding 1，per-instance） ──────────────────────────────────
// 与 VansParticleInstanceData 保持一一对应（48 字节 stride）
layout(location = 2) in vec3  instWorldPos;    // m_WorldPosition
layout(location = 3) in float instSize;        // m_Size
layout(location = 4) in vec4  instColor;       // m_Color (rgba)
layout(location = 5) in float instRotation;    // m_Rotation（弧度）
layout(location = 6) in float instFrameIndex;  // m_FrameIndex（精灵动画帧）
layout(location = 7) in vec2  instPadding;     // m_Padding（保留）

// ── 输出到片元着色器 ─────────────────────────────────────────────────────
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

// ── Push Constants（精灵表列/行数） ─────────────────────────────────────
layout(push_constant) uniform ParticlePushConst
{
    // 精灵动画表参数：(列数, 行数, 未用, 未用)
    vec4 spriteSheetParams;
} pushConst;

void main()
{
    // 相机的右向量和上向量（世界空间 Billboard）
    vec3 camRight = vec3(ViewMatrix[0][0], ViewMatrix[1][0], ViewMatrix[2][0]);
    vec3 camUp    = vec3(ViewMatrix[0][1], ViewMatrix[1][1], ViewMatrix[2][1]);

    // 旋转局部坐标（绕相机朝向旋转）
    float cosR = cos(instRotation);
    float sinR = sin(instRotation);
    vec2  rotLocal = vec2(
        inLocalPos.x * cosR - inLocalPos.y * sinR,
        inLocalPos.x * sinR + inLocalPos.y * cosR
    );

    // 世界空间 Billboard 扩展
    vec3 worldPos = instWorldPos
        + camRight * rotLocal.x * instSize
        + camUp    * rotLocal.y * instSize;

    gl_Position = VPMatrix * vec4(worldPos, 1.0);

    // ── 精灵动画 UV 偏移 ─────────────────────────────────────────────────
    float cols      = max(pushConst.spriteSheetParams.x, 1.0);
    float rows      = max(pushConst.spriteSheetParams.y, 1.0);
    float totalFrames = cols * rows;
    int   frame     = int(mod(instFrameIndex, totalFrames));
    float col       = float(frame  % int(cols));
    float row       = float(frame  / int(cols));

    vec2 frameSize  = vec2(1.0 / cols, 1.0 / rows);
    fragUV = (inUV + vec2(col, row)) * frameSize;

    fragColor = instColor;
}
