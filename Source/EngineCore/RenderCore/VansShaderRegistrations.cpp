#include "VansShaderRegistry.h"
#include "VegetationCore/VansVegetationSystem.h"

// ---------------------------------------------------------------------------
// RegisterEngineShaders — declares every built-in engine shader in one place.
//
// Two-step registration:
//   Step 1: RegisterShader — one entry per unique shader (keyed by name).
//   Step 2: RegisterMaterialPasses — maps each material type to { pass → shader }.
//
// Legacy RegisterForType/RegisterNamed calls are kept temporarily until all
// callers have been migrated to the new pass-based API.
// ---------------------------------------------------------------------------
void RegisterEngineShaders()
{
    auto& reg = VansGraphics::VansShaderRegistry::Get();

    // ══════════════════════════════════════════════════════════════════════════
    // Step 1: Register all shaders by name (one entry per unique shader)
    // ══════════════════════════════════════════════════════════════════════════

    reg.RegisterShader("Unlit", {
        "Unlit",
        "EngineAssets/Shaders/UnLit",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    reg.RegisterShader("Shadow", {
        "Shadow",
        "EngineAssets/Shaders/Shadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        12, false
    });

    reg.RegisterShader("PunctualShadow", {
        "PunctualShadow",
        "EngineAssets/Shaders/PunctualShadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        16, false
    });

    reg.RegisterShader("Skin", {
        "Skin",
        "EngineAssets/Shaders/UnlitSkin",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    reg.RegisterShader("Cloth", {
        "Cloth",
        "EngineAssets/Shaders/Cloth",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        12, false
    });

    reg.RegisterShader("Hair", {
        "Hair",
        "EngineAssets/Shaders/Hair",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        12, false
    });

    reg.RegisterShader("Coat", {
        "Coat",
        "EngineAssets/Shaders/Coat",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    reg.RegisterShader("TransparentSimpleColor", {
        "TransparentSimpleColor",
        "EngineAssets/Shaders/UnlitTransparent/SimpleColor",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        8, true
    });

    reg.RegisterShader("Deferred", {
        "Deferred",
        "EngineAssets/Shaders/Deferred",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterShader("Postprocess", {
        "Postprocess",
        "EngineAssets/Shaders/PostProcess",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterShader("SkyBox", {
        "SkyBox",
        "EngineAssets/Shaders/SkyBox",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterShader("SSAO", {
        "SSAO",
        "EngineAssets/Shaders/ScreenSpaceFeature/SSAO",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterShader("Subsurface", {
        "Subsurface",
        "EngineAssets/Shaders/Subsurface",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    reg.RegisterShader("GrassGBuffer", {
        "GrassGBuffer",
        "EngineAssets/Shaders/Grass",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::GrassDrawPushConstants), false  // P1: 增加 LOD 距离参数，48 字节
    });

    reg.RegisterShader("MotionVector", {
        "MotionVector",
        "EngineAssets/Shaders/MotionVector",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        8, false
    });

    reg.RegisterShader("Emissive", {
        "Emissive",
        "EngineAssets/Shaders/Emissive",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    // 粒子 Billboard 着色器：
    // - 深度测试开启、深度写入关闭（透明 Pass）
    // - CULL_NONE：Billboard 双面可见
    // - Alpha Blend 开启（支持加法/叠加混合）
    // - Push Constant 16 字节（vec4 spriteSheetParams：精灵动画列/行数）
    reg.RegisterShader("Particle", {
        "Particle",
        "EngineAssets/Shaders/Particle",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        16, true
    });

    // 贴花着色器：正确的 screen-space decal 方案
    // - CULL_FRONT：只光栅化背面（cube 远离相机的面）
    // - GREATER_OR_EQUAL：场景深度 <= 背面深度 → 场景几何在 cube 内部 → 通过
    //   （天空/cube后方几何的深度 > 背面深度 → 拒绝，正确）
    // - depthWrite = FALSE：不写入深度
    reg.RegisterShader("Decal", {
        "Decal",
        "EngineAssets/Shaders/Decal",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_CULL_MODE_FRONT_BIT,
        12, false, true
    });

    // ══════════════════════════════════════════════════════════════════════════
    // Step 2: Register material type → { pass name → shader name } mappings
    // ══════════════════════════════════════════════════════════════════════════

    reg.RegisterMaterialPasses(VansGraphics::VAN_PBR, {
        { VansGraphics::VansPass::GBUFFER,          "Unlit"          },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
        { VansGraphics::VansPass::VELOCITY,         "MotionVector"   },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_COAT, {
        { VansGraphics::VansPass::GBUFFER,          "Coat"           },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
        { VansGraphics::VansPass::VELOCITY,         "MotionVector"   },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_SKIN, {
        { VansGraphics::VansPass::GBUFFER,          "Skin"           },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
        { VansGraphics::VansPass::VELOCITY,         "MotionVector"   },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_CLOTH, {
        { VansGraphics::VansPass::GBUFFER,          "Cloth"          },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_HAIR, {
        { VansGraphics::VansPass::GBUFFER,          "Hair"           },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::VELOCITY,         "MotionVector"   },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_SUBSURFACE, {
        { VansGraphics::VansPass::GBUFFER,          "Subsurface"     },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
        { VansGraphics::VansPass::VELOCITY,         "MotionVector"   },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_GRASS, {
        { VansGraphics::VansPass::GBUFFER,          "GrassGBuffer"   },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_TRANSPARENT, {
        { VansGraphics::VansPass::FORWARD_TRANSPARENT, "TransparentSimpleColor" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_DEFERRED, {
        { VansGraphics::VansPass::DEFERRED,         "Deferred"       },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_POST_PROCESS, {
        { VansGraphics::VansPass::POST_PROCESS,     "Postprocess"    },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_SKY_BOX, {
        { VansGraphics::VansPass::SKY_BOX,          "SkyBox"         },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_SCREEN_SPACE_AO, {
        { VansGraphics::VansPass::SCREEN_SPACE,     "SSAO"           },
    });

    // 自发光：仅参与 GBuffer pass，不投射阴影，不参与 Velocity pass
    reg.RegisterMaterialPasses(VansGraphics::VAN_EMISSIVE, {
        { VansGraphics::VansPass::GBUFFER,          "Emissive"       },
    });

    // 贴花：仅参与 DecalGBuffer pass，不投射阴影，不写深度
    reg.RegisterMaterialPasses(VansGraphics::VAN_DECAL, {
        { VansGraphics::VansPass::DECAL_GBUFFER,    "Decal"          },
    });
}
