#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../BRDF/BRDFData.glsl"

// ── 顶点着色器输入 ─────────────────────────────────────────────────────────
layout( location = 0 ) in vec2  frag_uv;
layout( location = 1 ) in vec3  normal_ws;
layout( location = 2 ) in vec3  tangent_ws;
layout( location = 3 ) in vec3  bitangent_ws;
layout( location = 4 ) in vec3  position_world;
layout( location = 5 ) in vec3  position_local;   // OBB 模型空间坐标

// ── Set 0, binding 50: Bindless PBR 纹理阵列 ──────────────────────────────
layout( set = 0, binding = 50 ) uniform sampler2D globalPBRTextures[];

// ── Set 1, binding 0: GBuffer2 重建世界坐标和深度 ──────────────────────────
// GBuffer2 = (worldPos.xyz, -linearDepth)
layout( set = 1, binding = 0 ) uniform sampler2D gBuffer2Sampler;

// ── Push Constants ─────────────────────────────────────────────────────────
layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// ── MRT 输出（3 个颜色附件，与 DecalRenderPass 附件顺序一致） ──────────────
// 附件 0: Normal
layout( location = 0 ) out vec4 outNormal;
// 附件 1: GBuffer0 (albedo.rgb, roughness)
layout( location = 1 ) out vec4 outGBuffer0;
// 附件 2: GBuffer1 (metallic, AO, *, *)
// colorWriteMask 在 pipeline 中已设置为 R+G 仅写，B (materialID) 和 A 不变
layout( location = 2 ) out vec4 outGBuffer1;

void main()
{
    // ── 1. 从 GBuffer2 重建被遮挡表面的世界坐标 ──────────────────────────────
    vec2 screenUV = gl_FragCoord.xy * ScreenParams.zw;
    vec4 gbuf2    = texture(gBuffer2Sampler, screenUV);

    // w <= 0 表示空像素（清除值 0）或天空，丢弃
    if (gbuf2.w <= 0.0)
        discard;

    vec3 surfaceWS = gbuf2.xyz;

    // ── 2. OBB 越界测试 ──────────────────────────────────────────────────────
    int objectIndex  = materialConst.objectIndex;
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 invModel    = inverse(ModelMatrix);
    vec4 localPos    = invModel * vec4(surfaceWS, 1.0);

    // cube.obj 顶点范围 [-1, 1]^3；超出则不在 OBB 内，丢弃
    if (any(greaterThan(abs(localPos.xyz), vec3(1.0))))
        discard;

    // ── 3. XZ 平面投影 → UV ────────────────────────────────────────────────
    vec2 decal_uv = localPos.xz * 0.5 + 0.5;

    // ── 4. 采样贴花 PBR 纹理 ────────────────────────────────────────────────
    int mi = nonuniformEXT(materialConst.materialIndex);
    MaterialPayload matData = materialDataBuffer.materials[mi];

    vec4 albedoSample    = texture(globalPBRTextures[mi * 5 + 0], decal_uv);
    vec3 normalSample    = texture(globalPBRTextures[mi * 5 + 1], decal_uv).rgb;
    float metallicSample = texture(globalPBRTextures[mi * 5 + 2], decal_uv).r;
    float roughSample    = texture(globalPBRTextures[mi * 5 + 3], decal_uv).r;
    float aoSample       = texture(globalPBRTextures[mi * 5 + 4], decal_uv).r;

    // 贴花 alpha：由 albedo 纹理的 alpha 通道控制覆盖强度
    float alpha = albedoSample.a * matData.albedo.a;
    //if (alpha < 0.01)
        //discard;

    vec3 albedo    = matData.albedo.rgb * albedoSample.rgb;
    float metallic = matData.metallic   * metallicSample;
    float roughness= matData.roughness  * roughSample;
    float ao       = matData.ao         * aoSample;

    // 法线贴图：使用 GBuffer 中的世界空间法线作为基准 + 贴花切线空间法线
    // 贴花在 XZ 投影，因此固定使用 Y 轴朝上的 TBN
    vec3 T = normalize(vec3(ModelMatrix[0].xyz));  // OBB +X 轴
    vec3 B = normalize(vec3(ModelMatrix[2].xyz));  // OBB +Z 轴
    vec3 N = normalize(vec3(ModelMatrix[1].xyz));  // OBB +Y 轴（朝上）
    mat3 TBN       = mat3(T, B, N);
    normalSample   = normalSample * 2.0 - 1.0;
    vec3 normal    = normalize(TBN * normalSample);

    outNormal   = vec4(normal,   alpha);
    outGBuffer0 = vec4(albedo,   roughness);
    outGBuffer1 = vec4(metallic, ao, 0.0, alpha);
}
