#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"

// ── Quad 顶点（binding 0，per-vertex）────────────────────────────────────
layout(location = 0) in vec2 inLocalPos;
layout(location = 1) in vec2 inUV;

// ── 实例数据（binding 1，per-instance）──────────────────────────────────
layout(location = 2) in vec3  instWorldPos;
layout(location = 3) in float instSize;
layout(location = 4) in vec4  instColor;
layout(location = 5) in float instRotation;
layout(location = 6) in float instFrameIndex;
layout(location = 7) in vec2  instPadding;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec3 fragBillboardRight;
layout(location = 4) out vec3 fragBillboardUp;
layout(location = 5) out vec3 fragBillboardForward;

layout(push_constant) uniform ParticleSixWayPushConst
{
    vec4 spriteSheetParams;     // x: columns, y: rows
    vec4 sixWayParams0;         // x: direct, y: ambient, z: emissive, w: absorption
    vec4 sixWayParams1;         // x: remapMin, y: remapMax, z: alphaCutoff, w: debugMode
    vec4 mainLightDirAndPad;    // 预留
    vec4 mainLightColor;        // 预留
} pushConst;

void main()
{
    vec3 camRight = normalize(vec3(ViewMatrix[0][0], ViewMatrix[1][0], ViewMatrix[2][0]));
    vec3 camUp    = normalize(vec3(ViewMatrix[0][1], ViewMatrix[1][1], ViewMatrix[2][1]));
    vec3 camForward = normalize(-vec3(ViewMatrix[0][2], ViewMatrix[1][2], ViewMatrix[2][2]));

    float cosR = cos(instRotation);
    float sinR = sin(instRotation);
    vec2 rotLocal = vec2(
        inLocalPos.x * cosR - inLocalPos.y * sinR,
        inLocalPos.x * sinR + inLocalPos.y * cosR
    );

    vec3 worldPos = instWorldPos
        + camRight * rotLocal.x * instSize
        + camUp    * rotLocal.y * instSize;

    gl_Position = VPMatrix * vec4(worldPos, 1.0);

    float cols = max(pushConst.spriteSheetParams.x, 1.0);
    float rows = max(pushConst.spriteSheetParams.y, 1.0);
    float totalFrames = cols * rows;
    int frame = int(mod(instFrameIndex, totalFrames));
    float col = float(frame % int(cols));
    float row = float(frame / int(cols));
    vec2 frameSize = vec2(1.0 / cols, 1.0 / rows);
    fragUV = (inUV + vec2(col, row)) * frameSize;

    fragColor = instColor;
    fragWorldPos = worldPos;
    fragBillboardRight = camRight;
    fragBillboardUp = camUp;
    fragBillboardForward = camForward;
}
