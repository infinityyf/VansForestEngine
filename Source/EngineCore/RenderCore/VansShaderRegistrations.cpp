#include "VansShaderRegistry.h"

// ---------------------------------------------------------------------------
// RegisterEngineShaders — declares every built-in engine shader in one place.
//
// Rules:
//   - RegisterForType is called exactly ONCE per VansMaterialType value.
//     A second call for the same type is silently ignored (warning logged).
//   - RegisterNamed is used for shaders that have no material-type binding
//     and are referenced by name only (e.g. PunctualShadow).
// ---------------------------------------------------------------------------
void RegisterEngineShaders()
{
    auto& reg = VansGraphics::VansShaderRegistry::Get();

    // ── VAN_PBR ──────────────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_PBR, {
        "Unlit",
        "EngineAssets/Shaders/UnLit",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    // ── VAN_COAT ─────────────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_COAT, {
        "Coat",
        "EngineAssets/Shaders/Coat",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    // ── VAN_SKIN ─────────────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_SKIN, {
        "Skin",
        "EngineAssets/Shaders/UnlitSkin",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        12, false
    });

    // ── VAN_TRANSPARENT ───────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_TRANSPARENT, {
        "UnlitTransparentSimpleColor",
        "EngineAssets/Shaders/UnlitTransparent/SimpleColor",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        8, true
    });

    // ── VAN_DEFERRED ──────────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_DEFERRED, {
        "Deferred",
        "EngineAssets/Shaders/Deferred",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    // ── VAN_POST_PROCESS ──────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_POST_PROCESS, {
        "Postprocess",
        "EngineAssets/Shaders/PostProcess",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE,
        0, false
    });

    // ── VAN_SKY_BOX ───────────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_SKY_BOX, {
        "SkyBox",
        "EngineAssets/Shaders/SkyBox",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        0, false
    });

    // ── VAN_SCREEN_SPACE_AO ───────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_SCREEN_SPACE_AO, {
        "SSAO",
        "EngineAssets/Shaders/ScreenSpaceFeature/SSAO",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    // ── VAN_SHAODW ────────────────────────────────────────────────────────────
    reg.RegisterForType(VansGraphics::VAN_SHAODW, {
        "Shadow",
        "EngineAssets/Shaders/Shadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        12, false
    });

    // ── Named-only shaders (no material-type binding) ─────────────────────────
    // PunctualShadow is referenced explicitly in LoadScene(); not auto-bound
    // to any material type.
    reg.RegisterNamed({
        "PunctualShadow",
        "EngineAssets/Shaders/PunctualShadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        16, false
    });
}
