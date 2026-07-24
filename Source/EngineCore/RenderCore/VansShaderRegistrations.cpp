#include "VansShaderManager.h"
#include "VansMaterial.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "WaterCore/VansWaterGeometryClipmap.h"
#include "WaterCore/VansWaterFFT.h"
#include "ReflectionProbeCore/VansReflectionProbeSystem.h"
#include "RayTracingCore/VansRayTracing.h"

// ---------------------------------------------------------------------------
// RegisterEngineShaders declares every built-in engine shader in one place.
//
// Two-step registration:
//   Step 1: RegisterShader: one entry per unique shader (keyed by name).
//   Step 2: RegisterMaterialPasses: maps each material type to { pass, shader }.
// ---------------------------------------------------------------------------
void RegisterEngineShaders()
{
    auto& reg = VansGraphics::VansShaderManager::Get();

    // -----------------------------------------------------------------------
    // Step 1: Register all shaders by name (one entry per unique shader)
    // -----------------------------------------------------------------------

    reg.RegisterGraphicsShader("Unlit", {
        "Unlit",
        "EngineAssets/Shaders/UnLit/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 4
    });

    reg.RegisterGraphicsShader("Shadow", {
        "Shadow",
        "EngineAssets/Shaders/Shadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::VansDrawPushConstant), false
    });

    reg.RegisterGraphicsShader("PunctualShadow", {
        "PunctualShadow",
        "EngineAssets/Shaders/PunctualShadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(int) * 5, false
    });

    reg.RegisterGraphicsShader("Skin", {
        "Skin",
        "EngineAssets/Shaders/UnlitSkin/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 4
    });

    reg.RegisterGraphicsShader("Cloth", {
        "Cloth",
        "EngineAssets/Shaders/Cloth/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 4
    });

    reg.RegisterGraphicsShader("HairVisibility", {
        "HairVisibility",
        "EngineAssets/Shaders/Hair/Visibility",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 0
    });

    reg.RegisterGraphicsShader("HairShadow", {
        "HairShadow",
        "EngineAssets/Shaders/Hair/Shadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(int) * 4, false
    });

    reg.RegisterGraphicsShader("HairPunctualShadow", {
        "HairPunctualShadow",
        "EngineAssets/Shaders/Hair/PunctualShadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(int) * 5, false
    });

    reg.RegisterGraphicsShader("HairDeepOpacity", {
        "HairDeepOpacity",
        "EngineAssets/Shaders/Hair/DeepOpacity",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        sizeof(int) * 4, false, false, 1, true
    });

    reg.RegisterGraphicsShader("HairLighting", {
        "HairLighting",
        "EngineAssets/Shaders/Hair/Lighting",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterGraphicsShader("HairComposite", {
        "HairComposite",
        "EngineAssets/Shaders/Hair/Composite",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false, false, -1, false, 0, true
    });

    reg.RegisterGraphicsShader("Coat", {
        "Coat",
        "EngineAssets/Shaders/Coat",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false
    });

    reg.RegisterGraphicsShader("TransparentSimpleColor", {
        "TransparentSimpleColor",
        "EngineAssets/Shaders/UnlitTransparent/SimpleColor",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        8, true
    });

    VansGraphics::VansShaderEntry transmissionGlass = {
        "TransmissionGlass",
        "EngineAssets/Shaders/Transmission/Glass",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false
    };
    transmissionGlass.enablePremultipliedAlphaBlend = true;
    reg.RegisterGraphicsShader("TransmissionGlass", transmissionGlass);

    reg.RegisterGraphicsShader("Deferred", {
        "Deferred",
        "EngineAssets/Shaders/Deferred",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterGraphicsShader("Postprocess", {
        "Postprocess",
        "EngineAssets/Shaders/PostProcess",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterGraphicsShader("SkyBox", {
        "SkyBox",
        "EngineAssets/Shaders/SkyBox",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterGraphicsShader("SSAO", {
        "SSAO",
        "EngineAssets/Shaders/ScreenSpaceFeature/SSAO",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_NEVER, VK_CULL_MODE_NONE,
        0, false
    });

    reg.RegisterGraphicsShader("Subsurface", {
        "Subsurface",
        "EngineAssets/Shaders/Subsurface/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 4
    });

    reg.RegisterGraphicsShader("GrassGBuffer", {
        "GrassGBuffer",
        "EngineAssets/Shaders/Grass/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::GrassDrawPushConstants), false, false, 4  // P1: LOD distance parameters, 8 bytes.
    });

    reg.RegisterGraphicsShader("TreeGBuffer", {
        "TreeGBuffer",
        "EngineAssets/Shaders/Tree/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::TreeDrawPushConstants), false, false, 4
    });

    reg.RegisterGraphicsShader("TreeShadow", {
        "TreeShadow",
        "EngineAssets/Shaders/Tree/Shadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::TreeShadowPushConstants), false
    });

    reg.RegisterGraphicsShader("TreePunctualShadow", {
        "TreePunctualShadow",
        "EngineAssets/Shaders/Tree/PunctualShadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::TreePunctualShadowPushConstants), false
    });

    reg.RegisterGraphicsShader("MotionVector", {
        "MotionVector",
        "EngineAssets/Shaders/MotionVector",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false
    });

    reg.RegisterGraphicsShader("Emissive", {
        "Emissive",
        "EngineAssets/Shaders/Emissive/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 4
    });

    // Particle billboard shader.
    // - Depth test enabled and depth write disabled for transparent pass.
    // - CULL_NONE keeps billboards visible from both sides.
    // - Alpha blend enabled for additive/overlay blending.
    // - Push constant: 16 bytes, vec4 spriteSheetParams.
    reg.RegisterGraphicsShader("Particle", {
        "Particle",
        "EngineAssets/Shaders/Particle",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        16, true
    });

    // Six-way smoke lighting particle shader.
    // - Keeps the regular transparent particle render state.
    // - Set 1 binds positive/negative axis lightmaps.
    // - Push constant: 80 bytes, flipbook plus six-way lighting and reserved main-light data.
    reg.RegisterGraphicsShader("ParticleSixWay", {
        "ParticleSixWay",
        "EngineAssets/Shaders/ParticleSixWay",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        80, true
    });

    // Screen-space decal shader.
    // - CULL_FRONT rasterizes only the cube back faces.
    // - GREATER_OR_EQUAL accepts scene geometry inside the decal volume.
    // - depthWrite = FALSE avoids writing decal volume depth.

    reg.RegisterGraphicsShader("Decal", {
        "Decal",
        "EngineAssets/Shaders/Decal",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_CULL_MODE_FRONT_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false, true
    });

    // -----------------------------------------------------------------------


    VansGraphics::VansShaderEntry waterGBufferShader{
        "WaterGBuffer", "EngineAssets/Shaders/Water/WaterGBuffer",
        VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(VansGraphics::WaterPatchPushConstant), false
    };
    waterGBufferShader.colorAttachmentCount = 2;
    reg.RegisterGraphicsShader("WaterGBuffer", std::move(waterGBufferShader));

    VansGraphics::VansShaderEntry waterCompositeShader{
        "WaterComposite", "EngineAssets/Shaders/Water/WaterComposite",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE,
        0, false
    };
    waterCompositeShader.colorAttachmentCount = 1;
    reg.RegisterGraphicsShader("WaterComposite", std::move(waterCompositeShader));
    reg.RegisterGraphicsShader("Terrain", {
        "Terrain", "EngineAssets/Shaders/Terrain/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        0, false, false, 4
    });

    reg.RegisterGraphicsShader("PBREmissive", {
        "PBREmissive",
        "EngineAssets/Shaders/Emissive/Deferred",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(VansGraphics::VansDrawPushConstant), false, false, 4
    });
    reg.RegisterGraphicsShader("TerrainShadow", {
        "TerrainShadow", "EngineAssets/Shaders/Terrain/Shadow",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        sizeof(int), false
    });
    reg.RegisterGraphicsShader("TerrainMotionVector", {
        "TerrainMotionVector", "EngineAssets/Shaders/Terrain/MotionVector",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
        0, false
    });
    VansGraphics::VansShaderEntry terrainTessShader{
        "TerrainTess", "EngineAssets/Shaders/Terrain/DeferredTess",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_CULL_MODE_NONE,
        0, false
    };
    terrainTessShader.colorAttachmentCount = 4;
    terrainTessShader.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    terrainTessShader.patchControlPoints = 3;
    reg.RegisterGraphicsShader("TerrainTess", std::move(terrainTessShader));

    VansGraphics::VansShaderEntry reflectionProbeCaptureShader{
        "ReflectionProbeCapture", "EngineAssets/Shaders/ReflectionProbeCapture",
        VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
        sizeof(float) * (16 + 4 + 4 + 4), false
    };
    reflectionProbeCaptureShader.colorAttachmentCount = 1;
    reflectionProbeCaptureShader.frontFace = VK_FRONT_FACE_CLOCKWISE;
    reg.RegisterGraphicsShader("ReflectionProbeCapture", std::move(reflectionProbeCaptureShader));

    VansGraphics::VansShaderEntry reflectionProbeCaptureSkyShader{
        "ReflectionProbeCaptureSky", "EngineAssets/Shaders/ReflectionProbeCaptureSky",
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE,
        0, false
    };
    reflectionProbeCaptureSkyShader.colorAttachmentCount = 1;
    reg.RegisterGraphicsShader("ReflectionProbeCaptureSky", std::move(reflectionProbeCaptureSkyShader));
    reg.RegisterComputeShader("PreConDiffuseEnvironment", "EngineAssets/Shaders/PreConDiffuseEnvironment");
    reg.RegisterComputeShader("PreConSpecularEnvironment", "EngineAssets/Shaders/PreConSpecularEnvironment");
    reg.RegisterComputeShader("SSGI", "EngineAssets/Shaders/SSGI");
    reg.RegisterComputeShader("SSGITemporal", "EngineAssets/Shaders/SSGITemporal");
    reg.RegisterComputeShader("HIZ", "EngineAssets/Shaders/HIZ");
    reg.RegisterComputeShader("HIZSeed", "EngineAssets/Shaders/HIZ_SEED");
    reg.RegisterComputeShader("ScreenSpaceShadow", "EngineAssets/Shaders/ScreenSpaceShadow");
    reg.RegisterComputeShader("SSRTrace", "EngineAssets/Shaders/SSR_TRACE");
    reg.RegisterComputeShader("SSRResolve", "EngineAssets/Shaders/SSR_RESOLVE");
    reg.RegisterComputeShader("SSRTemporalAA", "EngineAssets/Shaders/SSR_TEMPORALAA");
    reg.RegisterComputeShader("BilateralFilter", "EngineAssets/Shaders/BilateralFilter", sizeof(VansGraphics::VansMaterialManager::BilateralFilterPushConst));
    reg.RegisterComputeShader("VolumetricFog", "EngineAssets/Shaders/VolumetricFog");
    reg.RegisterComputeShader("FogLightInjection", "EngineAssets/Shaders/FogLightInjection");
    reg.RegisterComputeShader("FogRayMarch", "EngineAssets/Shaders/FogRayMarch");
    reg.RegisterComputeShader("CloudRayMarch", "EngineAssets/Shaders/Cloud");
    reg.RegisterComputeShader("TileLightBuild", "EngineAssets/Shaders/TileLight");
    reg.RegisterComputeShader("PunctualShadowDebug", "EngineAssets/Shaders/PunctualShadowDebug");
    reg.RegisterComputeShader("ExposureLuminance", "EngineAssets/Shaders/PostProcess/ExposureLuminance");
    reg.RegisterComputeShader("ExposureAdapt", "EngineAssets/Shaders/PostProcess/ExposureAdapt");
    reg.RegisterComputeShader("BloomPrefilter", "EngineAssets/Shaders/PostProcess/BloomPrefilter");
    reg.RegisterComputeShader("BloomDownsample", "EngineAssets/Shaders/PostProcess/BloomDownsample");
    reg.RegisterComputeShader("BloomUpsample", "EngineAssets/Shaders/PostProcess/BloomUpsample");
    reg.RegisterComputeShader("GIPointLight", "EngineAssets/Shaders/GIPointLight", sizeof(VansGraphics::RayTracingPushConstant));
    reg.RegisterComputeShader("GIVisibilityUpdate", "EngineAssets/Shaders/GIVisibilityUpdate", sizeof(VansGraphics::RayTracingPushConstant));
    reg.RegisterComputeShader("GIRTPreview", "EngineAssets/Shaders/GIRTPreview", sizeof(VansGraphics::GIRTPreviewPushConstant));
    reg.RegisterComputeShader("ReflectionProbePrefilter", "EngineAssets/Shaders/ReflectionProbePrefilter", sizeof(VansGraphics::VansReflectionProbeSystem::PrefilterPushConstants));
    reg.RegisterComputeShader("GrassBoneSim", "EngineAssets/Shaders/GrassBoneSim", sizeof(VansGraphics::GrassSimPushConstants));
    reg.RegisterComputeShader("GrassCull", "EngineAssets/Shaders/GrassCull", sizeof(VansGraphics::GrassCullPushConstants));
    reg.RegisterComputeShader("TreeCull", "EngineAssets/Shaders/TreeCull", sizeof(VansGraphics::TreeCullPushConstants));
    reg.RegisterComputeShader("WaterWave", "EngineAssets/Shaders/Water/WaterWave");
    reg.RegisterComputeShader("WaterWaveParticle", "EngineAssets/Shaders/Water/WaveParticle");
    reg.RegisterComputeShader("WaterFlowMap", "EngineAssets/Shaders/Water/FlowMap");
    reg.RegisterComputeShader("WaterSSR", "EngineAssets/Shaders/Water/SSR");
    reg.RegisterComputeShader("WaterRefraction", "EngineAssets/Shaders/Water/Refraction");
    reg.RegisterComputeShader("WaterCaustics", "EngineAssets/Shaders/Water/Caustics");
    reg.RegisterComputeShader("WaterThickness", "EngineAssets/Shaders/Water/SSS");
    reg.RegisterComputeShader("WaterSSSScatter", "EngineAssets/Shaders/Water/SSSScatter");
    reg.RegisterComputeShader("WaterFFTInit", "EngineAssets/Shaders/Water/FFT/Init");
    reg.RegisterComputeShader("WaterFFTEvolve", "EngineAssets/Shaders/Water/FFT/Evolve");
    reg.RegisterComputeShader("WaterFFTIter", "EngineAssets/Shaders/Water/FFT/Iter", sizeof(VansGraphics::VansWaterFFT::IterPushConstants));
    reg.RegisterComputeShader("WaterFFTExtract", "EngineAssets/Shaders/Water/FFT/Extract");
    reg.RegisterComputeShader("WaterFFTExtractSlope", "EngineAssets/Shaders/Water/FFT/ExtractSlope");
    reg.RegisterRayTracingShader("RayTracingTest", "EngineAssets/Shaders/RayTracingTest", sizeof(VansGraphics::RayTracingPushConstant));

    // Step 2: Register material type to { pass name, shader name } mappings.


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
        { VansGraphics::VansPass::HAIR_VISIBILITY,  "HairVisibility" },
        { VansGraphics::VansPass::SHADOW,           "HairShadow"     },
        { VansGraphics::VansPass::HAIR_SHADOW,      "HairShadow"     },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "HairPunctualShadow" },
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

    reg.RegisterMaterialPasses(VansGraphics::VAN_PBR_TRANSMISSION, {
        { VansGraphics::VansPass::FORWARD_TRANSPARENT, "TransmissionGlass" },
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

    // Emissive: only participates in GBuffer, without shadow or velocity passes.
    reg.RegisterMaterialPasses(VansGraphics::VAN_EMISSIVE, {
        { VansGraphics::VansPass::GBUFFER,          "Emissive"       },
    });

    reg.RegisterMaterialPasses(VansGraphics::VAN_PBR_EMISSIVE, {
        { VansGraphics::VansPass::GBUFFER,          "PBREmissive"    },
        { VansGraphics::VansPass::SHADOW,           "Shadow"         },
        { VansGraphics::VansPass::PUNCTUAL_SHADOW,  "PunctualShadow" },
        { VansGraphics::VansPass::VELOCITY,         "MotionVector"   },
    });

    // Decal: only participates in DecalGBuffer, without shadow or depth writes.
    reg.RegisterMaterialPasses(VansGraphics::VAN_DECAL, {
        { VansGraphics::VansPass::DECAL_GBUFFER,    "Decal"          },
    });
}
