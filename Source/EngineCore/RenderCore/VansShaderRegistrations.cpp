#include "VansShaderRegistry.h"

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
        24, false
    });

    // ══════════════════════════════════════════════════════════════════════════
    // Step 2: Register material type → { pass name → shader name } mappings
    // ══════════════════════════════════════════════════════════════════════════

    reg.RegisterMaterialPasses(VansGraphics::VAN_PBR, {
        { VansGraphics::VansPass::GBUFFER,          "Unlit"          },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_COAT, {
        { VansGraphics::VansPass::GBUFFER,          "Coat"           },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_SKIN, {
        { VansGraphics::VansPass::GBUFFER,          "Skin"           },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_CLOTH, {
        { VansGraphics::VansPass::GBUFFER,          "Cloth"          },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_HAIR, {
        { VansGraphics::VansPass::GBUFFER,          "Hair"           },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_SUBSURFACE, {
        { VansGraphics::VansPass::GBUFFER,          "Subsurface"     },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_GRASS, {
        { VansGraphics::VansPass::GBUFFER,          "GrassGBuffer"   },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
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
}
