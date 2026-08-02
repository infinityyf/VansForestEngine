#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AssetCore/VansAssetResolver.h"
#include "../EngineCore/AudioCore/VansAudioAttenuation.h"
#include "../EngineCore/AudioCore/VansAudioBus.h"
#include "../EngineCore/AudioCore/VansAudioBusSnapshotAsset.h"
#include "../EngineCore/AudioCore/VansAudioDuckingRulesAsset.h"
#include "../EngineCore/AudioCore/VansAudioDirectionality.h"
#include "../EngineCore/AudioCore/VansAudioManager.h"
#include "../EngineCore/AudioCore/VansAudioMixConfig.h"
#include "../EngineCore/AudioCore/VansAudioOcclusion.h"
#include "../EngineCore/AudioCore/Storage/VansAudioBusSnapshotAssetStorage.h"
#include "../EngineCore/AudioCore/Storage/VansAudioDuckingRulesAssetStorage.h"
#include "../EngineCore/AudioCore/VansAudioReverbPresetAsset.h"
#include "../EngineCore/AudioCore/Storage/VansAudioReverbPresetAssetStorage.h"
#include "../EngineCore/AudioCore/VansAudioReverbEnvironment.h"
#include "../EngineCore/AudioCore/VansAudioSourceBinding.h"
#include "../EngineCore/AudioCore/VansAudioPreviewPlayer.h"
#include "../EngineCore/AudioCore/VansAudioVirtualization.h"
#include "../EngineCore/RenderCore/VansPostProcessProfile.h"
#include "../EngineCore/RenderCore/ShadowCore/VansPunctualShadowManager.h"
#include "../EngineCore/RuntimeCore/VansPackageManifest.h"
#include "../EngineCore/RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include "../EngineCore/SceneCore/VansSceneCameraMediaComponentReader.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/SceneCore/VansSceneRenderSettingsConfigReader.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineCore/AnimationCore/VansAnimationController.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
struct TemporaryDirectory
{
    fs::path path;

    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("ForestContractTests." + std::to_string(nonce));
        fs::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

bool Expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "[ForestContractTests] " << message << '\n';
    return false;
}

bool ExpectNear(float actual, float expected, float tolerance, const char* message)
{
    return Expect(std::fabs(actual - expected) <= tolerance, message);
}

bool TestPackageManifestRoundTrip()
{
    TemporaryDirectory temporary;
    Vans::VansPackageManifest expected;
    expected.generatedAt = "2026-07-31T00:00:00Z";
    expected.scene = "Scenes/MainScene.json";
    expected.resourcePlan = "Library/Package/ResourcePlan.json";
    expected.resourcePlanReport = "Library/Package/ResourcePlanReport.json";
	expected.shaderArtifacts = "Library/Artifacts/Shaders";
    expected.copiedFileCount = 42;

    std::string error;
    const fs::path manifestPath = temporary.path / "ForestPackage.json";
    if (!Expect(Vans::VansPackageManifestIO::Save(manifestPath, expected, error), error.c_str()))
        return false;

	{
		std::ifstream manifestFile(manifestPath, std::ios::binary);
		const std::string manifestText(
			(std::istreambuf_iterator<char>(manifestFile)),
			std::istreambuf_iterator<char>());
		if (!Expect(manifestText.find("\"version\"") == std::string::npos,
			"Package manifest introduced a versioned compatibility field"))
			return false;
	}

    const Vans::VansPackageManifestLoadResult loaded = Vans::VansPackageManifestIO::Load(manifestPath);
    if (!Expect(static_cast<bool>(loaded), loaded.error.c_str()))
        return false;
    if (!Expect(loaded.manifest.scene == expected.scene, "Manifest scene did not round-trip"))
        return false;
    if (!Expect(loaded.manifest.resourcePlan == expected.resourcePlan, "Manifest resource plan did not round-trip"))
        return false;
	if (!Expect(loaded.manifest.shaderArtifacts == expected.shaderArtifacts,
		"Manifest shader artifact root did not round-trip"))
		return false;
    if (!Expect(loaded.manifest.copiedFileCount == expected.copiedFileCount, "Manifest file count did not round-trip"))
        return false;

    Vans::VansPackageManifest invalid = expected;
    invalid.scene = "../Outside.json";
    return Expect(!Vans::VansPackageManifestIO::Validate(invalid, error), "Manifest accepted a parent traversal path");
}

bool TestAssetPolicies()
{
    TemporaryDirectory temporary;
    const fs::path assetsRoot = temporary.path / "Assets";
    const fs::path artifactRoot = temporary.path / "Library" / "Artifacts";
    fs::create_directories(assetsRoot);
    const fs::path texturePath = assetsRoot / "PolicyProbe.png";
    {
        std::ofstream file(texturePath, std::ios::binary);
        file << "policy-probe";
    }

    Vans::VansAssetDatabase database(assetsRoot, artifactRoot);
    const Vans::VansAssetScanResult readOnly = database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
    const fs::path metaPath = Vans::VansAssetMeta::MetaPathFor(texturePath);
    if (!Expect(!readOnly.errors.empty(), "Read-only scan did not report the missing meta"))
        return false;
    if (!Expect(!fs::exists(metaPath), "Read-only scan wrote a meta file"))
        return false;

    const Vans::VansAssetScanResult authoring = database.Scan(Vans::VansAssetOperationPolicy::Authoring());
    if (!Expect(static_cast<bool>(authoring), "Authoring scan failed"))
        return false;
    if (!Expect(authoring.generatedMeta == 1, "Authoring scan did not report one generated meta"))
        return false;
    if (!Expect(fs::exists(metaPath), "Authoring scan did not create the missing meta"))
        return false;
    return Expect(authoring.cookedArtifacts == 0, "Authoring scan implicitly cooked an artifact");
}

bool TestGameplayFrameOrder()
{
    std::vector<std::string> trace;
    Vans::VansRuntimeGameplayFrame frame;
    frame.sceneReady = true;
    frame.simulationRunning = true;
    frame.gameplayActive = true;
    frame.syncPhysicsTransforms = [&] { trace.push_back("physics"); };
    frame.updateNonCameraScripts = [&] { trace.push_back("scripts"); };
    frame.flushCharacterControllerTransforms = [&] { trace.push_back("cct"); };
    frame.updateCameraScripts = [&] { trace.push_back("camera"); };
    Vans::VansRuntimeFrameScheduler::RunGameplay(frame);

    const std::vector<std::string> expected{ "physics", "scripts", "cct", "camera" };
    if (!Expect(trace == expected, "Gameplay frame callback order changed"))
        return false;

    trace.clear();
    frame.sceneReady = false;
    Vans::VansRuntimeFrameScheduler::RunGameplay(frame);
    return Expect(trace.empty(), "Gameplay callbacks ran without a ready scene");
}

VansGraphics::Skeleton BuildContractHumanoidSkeleton()
{
    using namespace VansGraphics;

    Skeleton skeleton;
    skeleton.bones.resize(5);
    auto setBone = [&](int index, const char* name, int parent, const glm::vec3& localPosition)
    {
        BoneInfo& bone = skeleton.bones[index];
        bone.id = index;
        bone.name = name;
        bone.parentIndex = parent;
        bone.localTransform = glm::translate(glm::mat4(1.0f), localPosition);
        skeleton.boneNameToIndex[name] = index;
        if (parent >= 0)
            skeleton.bones[parent].children.push_back(index);
    };

    setBone(0, "root", -1, glm::vec3(0.0f, 0.0f, 0.0f));
    setBone(1, "pelvis", 0, glm::vec3(0.0f, 0.0f, 1.0f));
    setBone(2, "foot_l", 0, glm::vec3(-0.25f, 0.0f, 0.0f));
    setBone(3, "foot_r", 0, glm::vec3(0.25f, 0.0f, 0.0f));
    setBone(4, "head", 1, glm::vec3(0.0f, 0.0f, 0.8f));
    skeleton.BuildTopologicalOrder();
    return skeleton;
}

VansGraphics::VansAnimationClip BuildContractClip(const std::string& name,
                                                  float rootEndY,
                                                  float leftFootPhase)
{
    using namespace VansGraphics;

    VansAnimationClip clip;
    clip.clipName = name;
    clip.duration = 1.0f;
    clip.ticksPerSecond = 30.0f;
    clip.boneKeyframes.resize(5);

    auto addKeys = [&](int bone, const glm::vec3& start, const glm::vec3& end)
    {
        clip.boneKeyframes[bone].push_back({ 0.0f, start, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        clip.boneKeyframes[bone].push_back({ 1.0f, end, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
    };

    addKeys(0, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, rootEndY, 0.0f));
    addKeys(1, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    addKeys(2, glm::vec3(-0.25f, 0.0f, 0.0f), glm::vec3(-0.25f, leftFootPhase, 0.0f));
    addKeys(3, glm::vec3(0.25f, 0.0f, 0.0f), glm::vec3(0.25f, -leftFootPhase, 0.0f));
    addKeys(4, glm::vec3(0.0f, 0.0f, 0.8f), glm::vec3(0.0f, 0.0f, 0.8f));
    return clip;
}

bool TestMotionMatchingAutoBuildLocomotionMetadataContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildContractHumanoidSkeleton();
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips.emplace("Idle_Stand", BuildContractClip("Idle_Stand", 0.0f, 0.0f));
    clips.emplace("WalkStart_F", BuildContractClip("WalkStart_F", -0.35f, 0.10f));
    clips.emplace("Walk_F", BuildContractClip("Walk_F", -0.70f, 0.20f));

    std::unordered_map<std::string, AnimatorParameter> parameters;
    parameters["Speed"] = { "Speed", AnimatorParamType::Float };
    parameters["Direction"] = { "Direction", AnimatorParamType::Float };
    parameters["MoveState"] = { "MoveState", AnimatorParamType::Int };
    parameters["UseMotionMatching"] = { "UseMotionMatching", AnimatorParamType::Bool };
    parameters["UseMotionMatching"].boolVal = true;

    MotionMatchingSettings settings;
    settings.enabled = true;
    settings.autoBuild = true;
    settings.externallyDriven = false;
    settings.searchThrottle = 0.01f;
    settings.minSwitchInterval = 0.0f;
    settings.desiredSpeedScale = 1.0f;
    settings.includeClipTokens = { "Idle", "Walk" };
    settings.rig.root = "root";
    settings.rig.trajectoryRoot = "root";
    settings.rig.pelvis = "pelvis";
    settings.rig.leftFoot = "foot_l";
    settings.rig.rightFoot = "foot_r";
    settings.rig.head = "head";
    settings.rig.forwardAxis = glm::vec3(0.0f, -1.0f, 0.0f);

    VansMotionMatchingRuntime runtime;
    runtime.Configure(settings);
    std::vector<glm::mat4> localTransforms;

    runtime.Update(0.033f, skeleton, clips, parameters, glm::mat4(1.0f), localTransforms);
    const MotionMatchingDebugData& idleDebug = runtime.GetDebugData();
    if (!Expect(idleDebug.databaseReady, "Motion matching auto database did not build"))
        return false;
    if (!Expect(idleDebug.activeClip == "Idle_Stand", "Idle query did not remain on Idle_Stand"))
        return false;

    parameters["Speed"].floatVal = 0.6f;
    parameters["Direction"].floatVal = 0.0f;
    parameters["MoveState"].intVal = 1;
    for (int i = 0; i < 8; ++i)
        runtime.Update(0.033f, skeleton, clips, parameters, glm::mat4(1.0f), localTransforms);

    const MotionMatchingDebugData& walkDebug = runtime.GetDebugData();
    if (!Expect(walkDebug.usedThisFrame, "Motion matching did not run for walk query"))
        return false;
    const bool selectedWalk =
        walkDebug.activeClip.find("Walk") != std::string::npos ||
        walkDebug.selectedClip.find("Walk") != std::string::npos;
    if (!Expect(selectedWalk, "Walk query did not select a Walk clip from auto-built metadata"))
        return false;
    return Expect(walkDebug.switches > 0, "Walk query did not switch away from idle");
}

bool TestAudioDistanceAttenuationContract()
{
    using namespace VansEngine;
    AudioAttenuationSettings settings;
    settings.referenceDistance = 1.0f;
    settings.maxDistance = 11.0f;
    settings.rolloff = 1.0f;

    settings.mode = AudioAttenuationMode::Linear;
    if (!ExpectNear(ComputeDistanceGain(1.0f, settings), 1.0f, 0.0001f,
        "Linear attenuation changed at reference distance"))
        return false;
    if (!ExpectNear(ComputeDistanceGain(6.0f, settings), 0.5f, 0.0001f,
        "Linear attenuation changed at half range"))
        return false;
    if (!ExpectNear(ComputeDistanceGain(11.0f, settings), 0.0f, 0.0001f,
        "Linear attenuation did not clamp at max distance"))
        return false;

    settings.mode = AudioAttenuationMode::Inverse;
    if (!ExpectNear(ComputeDistanceGain(6.0f, settings), 1.0f / 6.0f, 0.0001f,
        "Inverse attenuation formula changed"))
        return false;

    settings.mode = AudioAttenuationMode::Exponential;
    settings.rolloff = 2.0f;
    if (!ExpectNear(ComputeDistanceGain(2.0f, settings), 0.25f, 0.0001f,
        "Exponential attenuation formula changed"))
        return false;

    settings.maxDistance = 0.1f;
    return ExpectNear(ComputeDistanceGain(0.5f, settings), 1.0f, 0.0001f,
        "Attenuation settings normalization changed near reference distance");
}

bool TestAudioBusContract()
{
    using namespace VansEngine;

    if (!Expect(NormalizeAudioBusName(" sfx ") == "SFX",
        "Audio bus name normalization changed"))
        return false;
    if (!Expect(NormalizeAudioBusName("") == "SFX",
        "Empty audio bus name should default to SFX"))
        return false;

    AudioBusState master;
    master.gain = 0.5f;
    AudioBusState music;
    music.gain = 0.8f;
    if (!ExpectNear(ComputeAudioBusEffectiveGain(master, music, false, false), 0.4f, 0.0001f,
        "Audio bus gain multiplication changed"))
        return false;

    music.muted = true;
    if (!ExpectNear(ComputeAudioBusEffectiveGain(master, music, false, false), 0.0f, 0.0001f,
        "Muted audio bus should produce zero gain"))
        return false;

    music.muted = false;
    if (!ExpectNear(ComputeAudioBusEffectiveGain(master, music, true, false), 0.0f, 0.0001f,
        "Non-solo audio bus should be muted while another bus is soloed"))
        return false;

    music.soloed = true;
    if (!ExpectNear(ComputeAudioBusEffectiveGain(master, music, true, false), 0.4f, 0.0001f,
        "Soloed audio bus should keep its effective gain"))
        return false;

    SetAudioBusDuckingGainImmediate(music, 0.5f);
    if (!ExpectNear(ComputeAudioBusEffectiveGain(master, music, false, false), 0.2f, 0.0001f,
        "Audio bus ducking gain should multiply the base bus gain"))
        return false;
    StartAudioBusDuckingFade(music, 1.0f, 0.5f);
    TickAudioBusFade(music, 0.5f);
    if (!ExpectNear(music.duckingGain, 1.0f, 0.0001f,
        "Audio bus ducking fade should restore to neutral gain"))
        return false;

    AudioBusState fadingBus;
    SetAudioBusGainImmediate(fadingBus, 1.0f);
    StartAudioBusGainFade(fadingBus, 0.25f, 1.0f);
    TickAudioBusFade(fadingBus, 0.5f);
    if (!ExpectNear(fadingBus.gain, 0.625f, 0.0001f,
        "Audio bus fade interpolation changed"))
        return false;
    TickAudioBusFade(fadingBus, 0.5f);
    if (!ExpectNear(fadingBus.gain, 0.25f, 0.0001f,
        "Audio bus fade should land exactly on target"))
        return false;
    if (!Expect(!IsAudioBusFading(fadingBus),
        "Audio bus fade should finish after reaching its duration"))
        return false;

    VansAudioManager manager;
    manager.SetBusGain("Master", 0.75f);
    manager.SetBusGain("Music", 0.5f);
    manager.SetBusMuted("SFX", true);
    AudioBusSnapshot duckingSnapshot;
    duckingSnapshot.fadeSeconds = 0.3f;
    duckingSnapshot.buses.push_back(AudioBusSnapshotEntry{ "Music", 0.25f });
    manager.ApplyBusSnapshot(duckingSnapshot);
    const AudioBusState musicState = manager.GetBusState("Music");
    if (!ExpectNear(musicState.gain, 0.5f, 0.0001f,
        "Audio bus snapshot should not hard-cut current gain when fade is non-zero"))
        return false;
    if (!ExpectNear(musicState.targetGain, 0.25f, 0.0001f,
        "Audio bus snapshot target gain changed"))
        return false;
    if (!Expect(IsAudioBusFading(musicState),
        "Audio bus snapshot should start a fade"))
        return false;

    AudioDuckingRule duckingRule;
    duckingRule.triggerBusName = "Voice";
    duckingRule.targetBusName = "Music";
    duckingRule.targetGain = 0.4f;
    duckingRule.attackSeconds = 0.0f;
    duckingRule.releaseSeconds = 0.0f;
    manager.AddDuckingRule(duckingRule);
    manager.UpdateDucking({ "Voice" });
    if (!ExpectNear(manager.GetBusState("Music").duckingGain, 0.4f, 0.0001f,
        "Audio ducking rule did not apply target ducking gain"))
        return false;
    if (!ExpectNear(manager.GetEffectiveBusGain("Music"), 0.15f, 0.0001f,
        "Audio ducking rule should multiply effective bus gain"))
        return false;
    manager.UpdateDucking({});
    if (!ExpectNear(manager.GetBusState("Music").duckingGain, 1.0f, 0.0001f,
        "Audio ducking rule did not release back to neutral gain"))
        return false;

    const std::vector<AudioBusDebugEntry> snapshot = manager.GetBusDebugSnapshot();

    if (!Expect(snapshot.size() == 4,
        "Audio bus debug snapshot should expose configured buses"))
        return false;
    if (!Expect(snapshot[0].name == "Master",
        "Audio bus debug snapshot should keep Master first"))
        return false;

    const auto musicEntry = std::find_if(snapshot.begin(), snapshot.end(),
        [](const AudioBusDebugEntry& entry)
        {
            return entry.name == "Music";
        });
    if (!Expect(musicEntry != snapshot.end(),
        "Audio bus debug snapshot lost Music bus"))
        return false;
    if (!ExpectNear(musicEntry->state.duckingGain, 1.0f, 0.0001f,
        "Audio bus debug snapshot should expose released ducking gain"))
        return false;

    return ExpectNear(musicEntry->effectiveGain, 0.375f, 0.0001f,
        "Audio bus debug snapshot effective gain changed");
}

bool TestAudioMixConfigContract()
{
    using namespace VansEngine;

    TemporaryDirectory temporary;
    const fs::path mixPath = temporary.path / "AudioMix.json";
    std::ofstream mixFile(mixPath, std::ios::binary);
    mixFile << R"json({
        "displayName": "Contract Mix",
        "defaultSnapshot": "Gameplay",
        "buses": {
            "Master": { "gain": 0.9, "lowpassHighFrequencyGain": 1.0 },
            "Ambient": { "gain": 0.8, "lowpassHighFrequencyGain": 0.5 },
            "SFX": { "gain": 1.0, "lowpassHighFrequencyGain": 1.0 }
        },
        "snapshots": {
            "Gameplay": {
                "fadeSeconds": 0.0,
                "buses": {
                    "Ambient": { "gain": 0.4, "lowpassHighFrequencyGain": 0.3 }
                }
            },
            "UIFocus": {
                "fadeSeconds": 0.0,
                "buses": [
                    { "bus": "SFX", "gain": 0.25, "lowpassHighFrequencyGain": 0.2 }
                ]
            }
        },
        "ducking": [
            {
                "triggerBus": "SFX",
                "targetBus": "Ambient",
                "targetGain": 0.55,
                "attackSeconds": 0.0,
                "releaseSeconds": 0.0
            }
        ]
    })json";
    mixFile.close();

    AudioMixConfig config;
    std::string error;
    if (!Expect(VansAudioMixConfigStorage::Load(mixPath, config, error), error.c_str()))
        return false;
    if (!Expect(config.displayName == "Contract Mix", "Audio mix display name did not read"))
        return false;
    if (!Expect(config.buses.size() == 3, "Audio mix bus list did not read"))
        return false;
    if (!Expect(config.snapshots.size() == 2, "Audio mix snapshots did not read"))
        return false;
    if (!Expect(config.duckingRules.size() == 1, "Audio mix ducking rules did not read"))
        return false;
    if (!ExpectNear(config.duckingRules[0].targetGain, 0.55f, 0.0001f,
        "Audio mix ducking target gain did not read"))
        return false;

    VansAudioManager manager;
    manager.ApplyMixConfig(config);
    AudioBusState ambientState = manager.GetBusState("Ambient");
    if (!ExpectNear(ambientState.gain, 0.4f, 0.0001f,
        "Audio mix default snapshot did not apply ambient gain"))
        return false;
    if (!ExpectNear(ambientState.lowpassHighFrequencyGain, 0.3f, 0.0001f,
        "Audio mix default snapshot did not apply ambient lowpass"))
        return false;

    if (!Expect(manager.ApplyNamedBusSnapshot("UIFocus"),
        "Named audio mix snapshot did not apply"))
        return false;
    AudioBusState sfxState = manager.GetBusState("SFX");
    if (!ExpectNear(sfxState.gain, 0.25f, 0.0001f,
        "Named audio mix snapshot did not apply SFX gain"))
        return false;
    if (!ExpectNear(sfxState.lowpassHighFrequencyGain, 0.2f, 0.0001f,
        "Named audio mix snapshot did not apply SFX lowpass"))
        return false;

    manager.UpdateDucking({ "SFX" });
    ambientState = manager.GetBusState("Ambient");
    if (!ExpectNear(ambientState.duckingGain, 0.55f, 0.0001f,
        "Audio mix ducking rule did not activate"))
        return false;
    return ExpectNear(manager.GetEffectiveBusGain("Ambient"), 0.198f, 0.0001f,
        "Audio mix ducking did not multiply master, bus, and ducking gain");
}

bool TestAudioOcclusionContract()
{
    using namespace VansEngine;

    AudioOcclusionSettings settings;
    settings.blockedGain = 0.25f;
    settings.blockedHighFrequencyGain = 0.2f;
    settings.attackSeconds = 0.05f;
    settings.releaseSeconds = 0.2f;
    settings.queryIntervalSeconds = 0.0f;
    settings.maxQueriesPerFrame = 0;
    settings.Normalize();

    if (!ExpectNear(settings.queryIntervalSeconds, 0.016f, 0.0001f,
        "Audio occlusion query interval did not normalize"))
        return false;
    if (!Expect(settings.maxQueriesPerFrame == 1,
        "Audio occlusion query budget did not normalize"))
        return false;
    if (!Expect(NormalizeAudioOcclusionMaterialName("Concrete") == std::string("stone"),
        "Audio occlusion material aliases should normalize"))
        return false;

    AudioOcclusionSettings materialSettings;
    materialSettings.material = "wood";
    materialSettings.materialThickness = 2.0f;
    const AudioOcclusionSettings resolvedMaterial =
        ResolveAudioOcclusionMaterialSettings(materialSettings);
    if (!Expect(resolvedMaterial.blockedGain < GetAudioOcclusionMaterialProfile("wood").blockedGain,
        "Audio occlusion material thickness should increase attenuation"))
        return false;
    AudioOcclusionSettings customSettings;
    customSettings.blockedGain = 0.33f;
    customSettings.blockedHighFrequencyGain = 0.22f;
    customSettings.material = "custom";
    const AudioOcclusionSettings resolvedCustom =
        ResolveAudioOcclusionMaterialSettings(customSettings);
    if (!ExpectNear(resolvedCustom.blockedGain, 0.33f, 0.0001f,
        "Custom audio occlusion material should preserve explicit gain"))
        return false;

    AudioOcclusionState state;
    state = UpdateAudioOcclusionState(state, settings, true, 0.016f);
    if (!Expect(state.gain < 1.0f && state.gain > settings.blockedGain,
        "Audio occlusion attack should smooth toward blocked gain"))
        return false;
    if (!Expect(state.highFrequencyGain < 1.0f && state.highFrequencyGain > settings.blockedHighFrequencyGain,
        "Audio occlusion attack should smooth high frequencies"))
        return false;

    const float blockedGain = state.gain;
    state = UpdateAudioOcclusionState(state, settings, false, 0.016f);
    return Expect(state.gain > blockedGain,
        "Audio occlusion release should smooth back toward full gain");
}

bool TestAudioDirectionalityContract()
{
    using namespace VansEngine;

    AudioConeSettings disabled;
    disabled.enabled = false;
    disabled.innerAngleDegrees = 20.0f;
    disabled.outerAngleDegrees = 40.0f;
    disabled.outerGain = 0.2f;
    disabled.Normalize();
    if (!ExpectNear(disabled.innerAngleDegrees, 360.0f, 0.0001f,
        "Disabled audio cone should normalize to omnidirectional inner angle"))
        return false;
    if (!ExpectNear(disabled.outerGain, 1.0f, 0.0001f,
        "Disabled audio cone should normalize to full outer gain"))
        return false;

    AudioConeSettings enabled;
    enabled.enabled = true;
    enabled.innerAngleDegrees = 120.0f;
    enabled.outerAngleDegrees = 60.0f;
    enabled.outerGain = -1.0f;
    enabled.Normalize();
    if (!ExpectNear(enabled.outerAngleDegrees, 120.0f, 0.0001f,
        "Audio cone outer angle should clamp above inner angle"))
        return false;
    return ExpectNear(enabled.outerGain, 0.0f, 0.0001f,
        "Audio cone outer gain should clamp to [0, 1]");
}

bool TestAudioComponentOcclusionReadContract()
{
    using Value = Vans::VansSerializedValue;

    const Value audioNode = Value::Object({
        { "source", Value::String("DoorLoop") },
        { "occlusionEnabled", Value::Bool(true) },
        { "occlusionGain", Value::Float(0.3) },
        { "occlusionHighFrequencyGain", Value::Float(0.25) },
        { "occlusionAttack", Value::Float(0.04) },
        { "occlusionRelease", Value::Float(0.2) },
        { "occlusionQueryInterval", Value::Float(0.1) },
        { "occlusionMaxDistance", Value::Float(40.0) },
        { "occlusionMaxQueriesPerFrame", Value::Int(8) },
        { "coneEnabled", Value::Bool(true) },
        { "coneInnerAngle", Value::Float(70.0) },
        { "coneOuterAngle", Value::Float(120.0) },
        { "coneOuterGain", Value::Float(0.4) },
        { "dopplerEnabled", Value::Bool(true) }
    });

    const std::optional<Vans::VansSceneAudioComponentConfig> config =
        Vans::VansSceneCameraMediaComponentReader::ReadAudio(
            audioNode,
            [](const Vans::VansSerializedValue& source)
            {
                return source.kind == Vans::VansSerializedValue::Kind::String
                    ? source.stringValue
                    : std::string{};
            });
    if (!Expect(config.has_value(), "Audio component with occlusion settings was not read"))
        return false;
    if (!Expect(config->sourceName == "DoorLoop", "Audio component source did not read"))
        return false;
    if (!Expect(config->occlusionEnabled, "Audio component occlusion enabled did not read"))
        return false;
    if (!ExpectNear(config->occlusionGain, 0.3f, 0.0001f,
        "Audio component occlusion gain did not read"))
        return false;
    if (!ExpectNear(config->occlusionHighFrequencyGain, 0.25f, 0.0001f,
        "Audio component occlusion high frequency gain did not read"))
        return false;
    if (!Expect(config->occlusionMaxQueriesPerFrame == 8,
        "Audio component occlusion query budget did not read"))
        return false;
    if (!Expect(config->coneEnabled, "Audio component cone enabled did not read"))
        return false;
    if (!ExpectNear(config->coneOuterGain, 0.4f, 0.0001f,
        "Audio component cone outer gain did not read"))
        return false;
    return Expect(config->dopplerEnabled,
        "Audio component doppler enabled did not read");
}

bool TestAudioSourceBindingNullObjectContract()
{
    VansEngine::VansAudioSourceBinding binding;
    binding.Play();
    binding.Pause();
    binding.Stop();
    binding.Resume();
    binding.SetVolume(0.5f);
    binding.SetPitch(1.2f);
    binding.SetLoop(true);
    binding.SetSpatial(true);
    binding.SetPosition(1.0f, 2.0f, 3.0f);
    binding.UpdateDistanceGain(0.0f, 0.0f, 0.0f);
    binding.SetBusName("Music");
    binding.SetReverbSend(0.5f);
    binding.SetOcclusion(0.5f, 0.25f);
    binding.SetVelocity(1.0f, 0.0f, 0.0f);
    binding.SetDirection(0.0f, 0.0f, 1.0f);
    binding.SetCone(VansEngine::AudioConeSettings{});

    if (!Expect(!binding.IsBound(), "Unbound audio source binding reported itself as bound"))
        return false;
    if (!Expect(!binding.IsPlaying() && !binding.IsPaused(),
        "Unbound audio source binding reported playback state"))
        return false;
    if (!Expect(binding.GetBusName().empty(), "Unbound audio source binding should expose an empty bus"))
        return false;
    return Expect(binding.GetFilePath().empty(), "Unbound audio source binding should expose an empty file path");
}

bool TestAudioPreviewSettingsContract()
{
    VansEngine::VansAudioPreviewSettings settings;
    if (!Expect(settings.streaming, "Audio preview should default to streaming playback"))
        return false;
    if (!Expect(settings.bus == "Preview", "Audio preview should use the Preview bus by default"))
        return false;
    if (!ExpectNear(settings.pitch, 1.0f, 0.0001f, "Audio preview default pitch changed"))
        return false;
    return ExpectNear(settings.reverbSend, 0.0f, 0.0001f,
        "Audio preview should default to dry playback");
}

bool TestAudioVoiceVirtualizationContract()
{
    using namespace VansEngine;

    std::vector<AudioVoiceCandidate> candidates;
    candidates.push_back(AudioVoiceCandidate{
        0, true, true, true, true, true, 30.0f, 100.0f, 1.0f });
    candidates.push_back(AudioVoiceCandidate{
        1, true, true, true, true, true, 4.0f, 100.0f, 0.8f });
    candidates.push_back(AudioVoiceCandidate{
        2, true, true, true, true, false, 0.0f, 100.0f, 0.25f });
    candidates.push_back(AudioVoiceCandidate{
        3, true, true, true, true, true, 2.0f, 100.0f, 0.0f });

    AudioVoiceBudgetSettings settings;
    settings.maxActiveVoices = 2;
    const AudioVoiceSelection selection = SelectAudioVoices(candidates, settings);

    if (!Expect(selection.active.size() == candidates.size(),
        "Audio voice selection did not preserve candidate count"))
        return false;
    if (!Expect(selection.activeCount == 2,
        "Audio voice selection did not honor active voice budget"))
        return false;
    if (!Expect(selection.active[2],
        "Non-spatial audio voice should keep high priority inside budget"))
        return false;
    if (!Expect(selection.active[1],
        "Near spatial audio voice should be selected before far spatial audio voice"))
        return false;
    if (!Expect(!selection.active[0],
        "Far spatial audio voice should be virtualized when over budget"))
        return false;
    return Expect(selection.virtualizedCount == 1,
        "Audio voice virtualization count changed");
}

bool TestAudioReverbEnvironmentContract()
{
    using namespace VansEngine;

    AudioReverbZoneState sphere;
    sphere.shape = AudioReverbZoneShape::Sphere;
    sphere.preset = AudioReverbPreset::Hall;
    sphere.radius = 10.0f;
    sphere.fadeDistance = 4.0f;
    sphere.wetGain = 0.8f;
    sphere.priority = 1;

    if (!ExpectNear(ComputeReverbZoneBlend(0.0f, 0.0f, 0.0f, sphere), 1.0f, 0.0001f,
        "Sphere reverb zone changed inside blend"))
        return false;
    if (!ExpectNear(ComputeReverbZoneBlend(12.0f, 0.0f, 0.0f, sphere), 0.5f, 0.0001f,
        "Sphere reverb zone changed fade blend"))
        return false;
    if (!Expect(!EvaluateReverbZone(14.0f, 0.0f, 0.0f, sphere).affectsListener,
        "Sphere reverb zone affects listener outside fade"))
        return false;
    const AudioReverbZoneEvaluation sphereInsideEval =
        EvaluateReverbZone(0.0f, 0.0f, 0.0f, sphere);
    if (!Expect(sphereInsideEval.preset == AudioReverbPreset::Hall,
        "Reverb zone evaluation lost preset"))
        return false;
    if (!Expect(AudioReverbPresetFromString("under_water") == AudioReverbPreset::Underwater,
        "Reverb preset normalization changed"))
        return false;
    if (!Expect(AudioReverbPresetToString(AudioReverbPreset::Cave) == std::string("cave"),
        "Reverb preset serialization changed"))
        return false;
    if (!Expect(GetAudioReverbPresetParameters(AudioReverbPreset::Hall).decayTime >
        GetAudioReverbPresetParameters(AudioReverbPreset::Room).decayTime,
        "Hall preset should decay longer than room preset"))
        return false;

    AudioReverbZoneState box;
    box.shape = AudioReverbZoneShape::Box;
    box.halfExtentX = 2.0f;
    box.halfExtentY = 3.0f;
    box.halfExtentZ = 4.0f;
    box.fadeDistance = 2.0f;
    box.wetGain = 0.4f;
    box.priority = 5;

    if (!ExpectNear(ComputeReverbZoneBlend(1.5f, 0.0f, 0.0f, box), 1.0f, 0.0001f,
        "Box reverb zone changed inside blend"))
        return false;
    if (!ExpectNear(ComputeReverbZoneBlend(3.0f, 0.0f, 0.0f, box), 0.5f, 0.0001f,
        "Box reverb zone changed fade blend"))
        return false;
    AudioReverbZoneState rotatedBox = box;
    rotatedBox.rightX = 0.0f;
    rotatedBox.rightY = 1.0f;
    rotatedBox.rightZ = 0.0f;
    rotatedBox.upX = -1.0f;
    rotatedBox.upY = 0.0f;
    rotatedBox.upZ = 0.0f;
    if (!ExpectNear(ComputeReverbZoneBlend(0.0f, 3.0f, 0.0f, rotatedBox), 0.5f, 0.0001f,
        "Rotated box reverb zone should evaluate in local axes"))
        return false;

    const AudioReverbZoneEvaluation sphereEval = EvaluateReverbZone(0.0f, 0.0f, 0.0f, sphere);
    const AudioReverbZoneEvaluation boxEval = EvaluateReverbZone(0.0f, 0.0f, 0.0f, box);
    if (!Expect(ShouldSelectReverbZoneCandidate(sphereEval, boxEval),
        "Higher-priority reverb zone was not selected"))
        return false;

    AudioReverbZoneState cave = sphere;
    cave.preset = AudioReverbPreset::Cave;
    cave.priority = 5;
    cave.wetGain = 0.25f;
    const AudioReverbEnvironmentEvaluation mixedEnvironment =
        EvaluateReverbEnvironment({
            sphereEval,
            boxEval,
            EvaluateReverbZone(0.0f, 0.0f, 0.0f, cave)
        });
    if (!Expect(mixedEnvironment.affectsListener && mixedEnvironment.contributingZoneCount == 2,
        "Reverb environment should mix only the highest-priority affecting zones"))
        return false;
    if (!ExpectNear(mixedEnvironment.wetGain, 0.65f, 0.0001f,
        "Reverb environment wet gain should sum same-priority zones"))
        return false;
    if (!Expect(mixedEnvironment.presetParameters.decayTime >
        GetAudioReverbPresetParameters(AudioReverbPreset::Room).decayTime,
        "Mixed reverb environment should blend preset parameters"))
        return false;

    const float smoothed = ComputeSmoothedReverbWetGain(0.0f, 1.0f, 0.1f);
    return Expect(smoothed > 0.0f && smoothed < 1.0f,
        "Reverb wet smoothing should advance without snapping");
}

bool TestAudioReverbPresetAssetContract()
{
    using Value = Vans::VansSerializedValue;

    if (!Expect(Vans::VansAssetDatabase::Classify("Hall.vreverb") == Vans::VansAssetType::AudioReverbPreset,
        "Audio reverb preset asset extension was not classified"))
        return false;
    if (!Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::AudioReverbPreset) ==
        "AudioReverbPresetImporter",
        "Audio reverb preset importer token changed"))
        return false;

    const Value root = Value::Object({
        { "guid", Value::String("preset-guid") },
        { "displayName", Value::String("Long Hall") },
        { "preset", Value::String("hall") },
        { "parameters", Value::Object({
            { "density", Value::Float(2.0) },
            { "diffusion", Value::Float(0.55) },
            { "gain", Value::Float(0.4) },
            { "gainHF", Value::Float(-1.0) },
            { "decayTime", Value::Float(25.0) }
        }) }
    });

    Vans::VansAudioReverbPresetAsset asset;
    std::string error;
    if (!Expect(Vans::ReadAudioReverbPresetAsset(root, asset, error), error.c_str()))
        return false;
    if (!Expect(asset.displayName == "Long Hall", "Audio reverb preset display name did not read"))
        return false;
    if (!ExpectNear(asset.parameters.density, 1.0f, 0.0001f,
        "Audio reverb preset density did not clamp"))
        return false;
    if (!ExpectNear(asset.parameters.diffusion, 0.55f, 0.0001f,
        "Audio reverb preset diffusion did not read"))
        return false;
    if (!ExpectNear(asset.parameters.gainHF, 0.0f, 0.0001f,
        "Audio reverb preset gainHF did not clamp"))
        return false;
    if (!ExpectNear(asset.parameters.decayTime, 20.0f, 0.0001f,
        "Audio reverb preset decay time did not clamp"))
        return false;

    const Value written = Vans::WriteAudioReverbPresetAssetRoot(asset);
    const Value* parameters = Vans::FindObjectField(written, "parameters");
    if (!Expect(parameters && parameters->kind == Value::Kind::Object,
        "Audio reverb preset writer omitted parameters"))
        return false;

    TemporaryDirectory temporary;
    const fs::path assetPath = temporary.path / "LongHall.vreverb";
    if (!Expect(Vans::VansAudioReverbPresetAssetStorage::SaveAtomic(assetPath, asset, error), error.c_str()))
        return false;

    Vans::VansAudioReverbPresetAsset loaded;
    if (!Expect(Vans::VansAudioReverbPresetAssetStorage::Load(assetPath, loaded, error), error.c_str()))
        return false;
    return ExpectNear(loaded.parameters.decayTime, asset.parameters.decayTime, 0.0001f,
        "Audio reverb preset storage did not round-trip decay time");
}

bool TestAudioBusSnapshotAssetContract()
{
    using Value = Vans::VansSerializedValue;

    if (!Expect(Vans::VansAssetDatabase::Classify("DialogueDuck.vaudiosnapshot") == Vans::VansAssetType::AudioBusSnapshot,
        "Audio bus snapshot asset extension was not classified"))
        return false;
    if (!Expect(Vans::VansAssetDatabase::Classify("Legacy.vbusnapshot") == Vans::VansAssetType::AudioBusSnapshot,
        "Audio bus snapshot alias extension was not classified"))
        return false;
    if (!Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::AudioBusSnapshot) ==
        "AudioBusSnapshotImporter",
        "Audio bus snapshot importer token changed"))
        return false;

    const Value root = Value::Object({
        { "guid", Value::String("snapshot-guid") },
        { "displayName", Value::String("Dialogue Duck") },
        { "fadeSeconds", Value::Float(0.35) },
        { "buses", Value::Array({
            Value::Object({
                { "bus", Value::String("music") },
                { "gain", Value::Float(0.25) },
                { "muted", Value::Bool(false) }
            }),
            Value::Object({
                { "name", Value::String("SFX") },
                { "gain", Value::Float(9.0) }
            })
        }) }
    });

    Vans::VansAudioBusSnapshotAsset asset;
    std::string error;
    if (!Expect(Vans::ReadAudioBusSnapshotAsset(root, asset, error), error.c_str()))
        return false;
    if (!Expect(asset.displayName == "Dialogue Duck", "Audio bus snapshot display name did not read"))
        return false;
    if (!Expect(asset.snapshot.buses.size() == 2, "Audio bus snapshot bus entries did not read"))
        return false;
    if (!Expect(asset.snapshot.buses[0].busName == "Music", "Audio bus snapshot bus name did not normalize"))
        return false;
    if (!Expect(asset.snapshot.buses[0].overrideMuted && !asset.snapshot.buses[0].muted,
        "Audio bus snapshot muted override did not read"))
        return false;
    if (!ExpectNear(asset.snapshot.buses[1].gain, 4.0f, 0.0001f,
        "Audio bus snapshot gain did not clamp"))
        return false;
    if (!ExpectNear(asset.snapshot.fadeSeconds, 0.35f, 0.0001f,
        "Audio bus snapshot fade time did not read"))
        return false;

    const Value written = Vans::WriteAudioBusSnapshotAssetRoot(asset);
    const Value* buses = Vans::FindObjectField(written, "buses");
    if (!Expect(buses && buses->kind == Value::Kind::Array && buses->arrayItems.size() == 2,
        "Audio bus snapshot writer omitted bus entries"))
        return false;

    TemporaryDirectory temporary;
    const fs::path assetPath = temporary.path / "DialogueDuck.vaudiosnapshot";
    if (!Expect(Vans::VansAudioBusSnapshotAssetStorage::SaveAtomic(assetPath, asset, error), error.c_str()))
        return false;

    Vans::VansAudioBusSnapshotAsset loaded;
    if (!Expect(Vans::VansAudioBusSnapshotAssetStorage::Load(assetPath, loaded, error), error.c_str()))
        return false;
    return ExpectNear(loaded.snapshot.buses[0].gain, asset.snapshot.buses[0].gain, 0.0001f,
        "Audio bus snapshot storage did not round-trip bus gain");
}

bool TestAudioDuckingRulesAssetContract()
{
    using Value = Vans::VansSerializedValue;

    if (!Expect(Vans::VansAssetDatabase::Classify("DialogueDuck.vducking") == Vans::VansAssetType::AudioDuckingRules,
        "Audio ducking rules asset extension was not classified"))
        return false;
    if (!Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::AudioDuckingRules) ==
        "AudioDuckingRulesImporter",
        "Audio ducking rules importer token changed"))
        return false;

    const Value root = Value::Object({
        { "guid", Value::String("ducking-guid") },
        { "displayName", Value::String("Dialogue Ducking") },
        { "rules", Value::Array({
            Value::Object({
                { "trigger", Value::String("voice") },
                { "target", Value::String("music") },
                { "gain", Value::Float(-1.0) },
                { "attack", Value::Float(0.08) },
                { "release", Value::Float(99.0) },
                { "enabled", Value::Bool(true) }
            }),
            Value::Object({
                { "trigger_bus", Value::String("SFX") },
                { "target_bus", Value::String("Ambient") },
                { "target_gain", Value::Float(0.6) }
            })
        }) }
    });

    Vans::VansAudioDuckingRulesAsset asset;
    std::string error;
    if (!Expect(Vans::ReadAudioDuckingRulesAsset(root, asset, error), error.c_str()))
        return false;
    if (!Expect(asset.displayName == "Dialogue Ducking", "Audio ducking rules display name did not read"))
        return false;
    if (!Expect(asset.rules.size() == 2, "Audio ducking rules entries did not read"))
        return false;
    if (!Expect(asset.rules[0].triggerBusName == "Voice" && asset.rules[0].targetBusName == "Music",
        "Audio ducking rule bus names did not normalize"))
        return false;
    if (!ExpectNear(asset.rules[0].targetGain, 0.0f, 0.0001f,
        "Audio ducking rule target gain did not clamp"))
        return false;
    if (!ExpectNear(asset.rules[0].releaseSeconds, 10.0f, 0.0001f,
        "Audio ducking rule release time did not clamp"))
        return false;
    if (!ExpectNear(asset.rules[1].targetGain, 0.6f, 0.0001f,
        "Audio ducking rule target_gain alias did not read"))
        return false;

    const Value written = Vans::WriteAudioDuckingRulesAssetRoot(asset);
    const Value* rules = Vans::FindObjectField(written, "rules");
    if (!Expect(rules && rules->kind == Value::Kind::Array && rules->arrayItems.size() == 2,
        "Audio ducking rules writer omitted rule entries"))
        return false;

    TemporaryDirectory temporary;
    const fs::path assetPath = temporary.path / "DialogueDuck.vducking";
    if (!Expect(Vans::VansAudioDuckingRulesAssetStorage::SaveAtomic(assetPath, asset, error), error.c_str()))
        return false;

    Vans::VansAudioDuckingRulesAsset loaded;
    if (!Expect(Vans::VansAudioDuckingRulesAssetStorage::Load(assetPath, loaded, error), error.c_str()))
        return false;
    return ExpectNear(loaded.rules[1].targetGain, asset.rules[1].targetGain, 0.0001f,
        "Audio ducking rules storage did not round-trip target gain");
}

bool TestIndexedAssetResolutionContract()
{
    TemporaryDirectory temporary;
    const fs::path sourcePath = temporary.path / "fullscreen.obj";
    const fs::path artifactPath = temporary.path / "fullscreen.vmesh";
    {
        std::ofstream source(sourcePath, std::ios::binary);
        source << "source";
    }

    Vans::VansAssetGuid guid;
    if (!Expect(Vans::VansAssetGuid::TryParse(
        "2c86c128-f3f0-4dbd-9e4e-0f0f0a61c9d1", guid), "Test guid is invalid"))
        return false;

    Vans::VansAssetRecord record;
    record.guid = guid;
    record.type = Vans::VansAssetType::Model;
    record.sourcePath = sourcePath;
    record.artifactPath = artifactPath;
    record.artifactFormat = Vans::VansAssetArtifactFormat::Imported;
    std::vector<Vans::VansAssetRecord> records{ record };

    Vans::VansAssetResolver editor(Vans::VansAssetAccessMode::Editor, records);
    const Vans::VansResolvedAsset editorSource = editor.Resolve(guid.ToString(), Vans::VansAssetType::Model);
    if (!Expect(editorSource.valid && editorSource.readPath == sourcePath,
        "Editor resolver did not use the indexed source when cache was absent"))
        return false;

    Vans::VansAssetResolver packageWithoutCache(Vans::VansAssetAccessMode::Package, records);
    if (!Expect(!packageWithoutCache.Resolve(guid.ToString(), Vans::VansAssetType::Model).valid,
        "Package resolver accepted an indexed source without a cache artifact"))
        return false;

    {
        std::ofstream artifact(artifactPath, std::ios::binary);
        artifact << "cache";
    }
    Vans::VansAssetResolver package(Vans::VansAssetAccessMode::Package, records);
    const Vans::VansResolvedAsset packaged = package.Resolve(guid.ToString(), Vans::VansAssetType::Model);
    if (!Expect(packaged.valid && packaged.readPath == artifactPath,
        "Package resolver did not read the indexed cache artifact"))
        return false;
    return Expect(!package.Resolve("missing-guid", Vans::VansAssetType::Model).valid,
        "Package resolver accepted a resource missing from the index");
}

bool TestPackagedAudioResourcePlanRoundTrip()
{
    TemporaryDirectory temporary;
    Vans::VansPackagedResourcePlan plan;
    Vans::VansSceneAudioResourceRequest audio;
    audio.name = "CaveDrip";
    audio.assetGuid = "audio-guid";
    audio.path = "Assets/Audio/cave_drip.ogg";
    audio.playMode = "streaming";
    audio.loop = true;
    audio.autoPlay = true;
    audio.volume = 0.8f;
    audio.pitch = 1.1f;
    audio.spatial = true;
    audio.referenceDistance = 2.0f;
    audio.maxDistance = 40.0f;
    audio.rolloff = 1.5f;
    audio.attenuationMode = "inverse";
    audio.reverbSend = 0.35f;
    audio.bus = "Music";
    plan.resourcePlan.audios.push_back(audio);

    std::string error;
    const fs::path planPath = temporary.path / "ResourcePlan.json";
    if (!Expect(Vans::VansPackagedResourcePlanIO::Save(planPath, plan, temporary.path, error), error.c_str()))
        return false;

    Vans::VansPackagedResourcePlan loaded;
    if (!Expect(Vans::VansPackagedResourcePlanIO::Load(planPath, temporary.path, loaded, error), error.c_str()))
        return false;
    if (!Expect(loaded.resourcePlan.audios.size() == 1, "Packaged audio resource count changed"))
        return false;

    const Vans::VansSceneAudioResourceRequest& roundTrip = loaded.resourcePlan.audios.front();
    if (!Expect(roundTrip.attenuationMode == "inverse", "Packaged audio attenuation mode did not round-trip"))
        return false;
    if (!ExpectNear(roundTrip.reverbSend, 0.35f, 0.0001f,
        "Packaged audio reverb send did not round-trip"))
        return false;
    return Expect(roundTrip.bus == "Music", "Packaged audio bus did not round-trip");
}

bool TestAudioReverbZoneRuntimeProjection()
{
    using Value = Vans::VansSerializedValue;
    const Value sceneRoot = Value::Object({
        { "schemaVersion", Value::Int(Vans::VansSceneSchemaVersion) },
        { "settings", Value::Object({}) },
        { "entities", Value::Array({
            Value::Object({
                { "id", Value::String("zone-entity") },
                { "name", Value::String("Reverb Zone") },
                { "components", Value::Array({
                    Value::Object({
                        { "id", Value::String("transform-id") },
                        { "type", Value::String("Transform") },
                        { "enabled", Value::Bool(true) },
                        { "data", Value::Object({
                            { "position", Value::Array({ Value::Float(1.0), Value::Float(2.0), Value::Float(3.0) }) },
                            { "rotation", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0), Value::Float(1.0) }) },
                            { "scale", Value::Array({ Value::Float(1.0), Value::Float(1.0), Value::Float(1.0) }) }
                        }) }
                    }),
                    Value::Object({
                        { "id", Value::String("zone-component") },
                        { "type", Value::String("AudioReverbZone") },
                        { "enabled", Value::Bool(true) },
                        { "data", Value::Object({
                            { "shape", Value::String("box") },
                            { "preset", Value::String("hall") },
                            { "radius", Value::Float(12.0) },
                            { "halfExtents", Value::Array({ Value::Float(5.0), Value::Float(6.0), Value::Float(7.0) }) },
                            { "fadeDistance", Value::Float(3.0) },
                            { "wetGain", Value::Float(0.7) },
                            { "priority", Value::Int(4) }
                        }) }
                    })
                }) }
            }),
            Value::Object({
                { "id", Value::String("volume-entity") },
                { "name", Value::String("Audio Volume") },
                { "components", Value::Array({
                    Value::Object({
                        { "id", Value::String("volume-transform") },
                        { "type", Value::String("Transform") },
                        { "enabled", Value::Bool(true) },
                        { "data", Value::Object({
                            { "position", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0) }) },
                            { "rotation", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0), Value::Float(1.0) }) },
                            { "scale", Value::Array({ Value::Float(1.0), Value::Float(1.0), Value::Float(1.0) }) }
                        }) }
                    }),
                    Value::Object({
                        { "id", Value::String("volume-component") },
                        { "type", Value::String("AudioVolume") },
                        { "enabled", Value::Bool(true) },
                        { "data", Value::Object({
                            { "shape", Value::String("sphere") },
                            { "preset", Value::String("room") },
                            { "radius", Value::Float(9.0) },
                            { "fadeDistance", Value::Float(1.5) },
                            { "wetGain", Value::Float(0.5) },
                            { "priority", Value::Int(6) },
                            { "overridePresetParameters", Value::Bool(true) },
                            { "density", Value::Float(0.5) },
                            { "diffusion", Value::Float(0.6) },
                            { "gain", Value::Float(0.7) },
                            { "gainHF", Value::Float(0.2) },
                            { "decayTime", Value::Float(6.5) }
                        }) }
                    })
                }) }
            })
        }) }
    });

    Vans::VansSceneContentBuildPlan plan;
    std::string error;
    if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
        sceneRoot,
        "",
        plan,
        error), error.c_str()))
    {
        return false;
    }
    if (!Expect(plan.objects.objects.size() == 2, "Audio reverb zone/volume entities were not projected"))
        return false;

    const auto& object = plan.objects.objects.front();
    if (!Expect(object.audioReverbZone.has_value(), "AudioReverbZone component was not projected"))
        return false;
    if (!Expect(object.audioReverbZone->shape == "box",
        "AudioReverbZone shape did not project"))
        return false;
    if (!Expect(object.audioReverbZone->preset == "hall",
        "AudioReverbZone preset did not project"))
        return false;
    if (!ExpectNear(object.audioReverbZone->radius, 12.0f, 0.0001f,
        "AudioReverbZone radius did not project"))
        return false;
    if (!ExpectNear(object.audioReverbZone->halfExtents[0], 5.0f, 0.0001f,
        "AudioReverbZone half extent X did not project"))
        return false;
    if (!ExpectNear(object.audioReverbZone->halfExtents[1], 6.0f, 0.0001f,
        "AudioReverbZone half extent Y did not project"))
        return false;
    if (!ExpectNear(object.audioReverbZone->halfExtents[2], 7.0f, 0.0001f,
        "AudioReverbZone half extent Z did not project"))
        return false;
    if (!ExpectNear(object.audioReverbZone->fadeDistance, 3.0f, 0.0001f,
        "AudioReverbZone fade distance did not project"))
        return false;
    if (!ExpectNear(object.audioReverbZone->wetGain, 0.7f, 0.0001f,
        "AudioReverbZone wet gain did not project"))
        return false;
    if (!Expect(object.audioReverbZone->priority == 4,
        "AudioReverbZone priority did not project"))
        return false;

    const auto& volumeObject = plan.objects.objects.back();
    if (!Expect(volumeObject.audioReverbZone.has_value(),
        "AudioVolume component was not projected"))
        return false;
    if (!Expect(volumeObject.audioReverbZone->componentType == "AudioVolume",
        "AudioVolume component type did not project"))
        return false;
    if (!Expect(volumeObject.audioReverbZone->preset == "room",
        "AudioVolume preset did not project"))
        return false;
    if (!ExpectNear(volumeObject.audioReverbZone->radius, 9.0f, 0.0001f,
        "AudioVolume radius did not project"))
        return false;
    if (!Expect(volumeObject.audioReverbZone->priority == 6,
        "AudioVolume priority did not project"))
        return false;
    if (!Expect(volumeObject.audioReverbZone->overridePresetParameters,
        "AudioVolume custom reverb parameters were not marked as overrides"))
        return false;
    if (!ExpectNear(volumeObject.audioReverbZone->presetParameters.decayTime, 6.5f, 0.0001f,
        "AudioVolume custom reverb decay time did not project"))
        return false;
    return ExpectNear(volumeObject.audioReverbZone->presetParameters.gainHF, 0.2f, 0.0001f,
        "AudioVolume custom reverb gainHF did not project");
}

bool TestExposureParameterContract()
{
	VansGraphics::VansPostProcessProfile profile;
	profile.m_MinEV100 = 8.0f;
	profile.m_MaxEV100 = -4.0f;
	profile.m_AdaptationSpeedUp = -1.0f;
	profile.m_AdaptationSpeedDown = 2.0f;
	profile.m_EnableAutoExposure = true;

	const VansGraphics::VansExposureAdaptParamsGPU params =
		profile.ToExposureAdaptParams(10.0f);
	if (!Expect(params.m_MinEV100 == -4.0f && params.m_MaxEV100 == 8.0f,
		"Exposure EV bounds were not normalized"))
		return false;
	if (!Expect(params.m_AdaptationSpeedUp == 0.0f && params.m_AdaptationSpeedDown == 2.0f,
		"Exposure adaptation speeds were not sanitized"))
		return false;
	if (!Expect(params.m_DeltaTime == 0.25f,
		"Exposure delta time was not bounded after a long frame"))
		return false;
	return Expect(params.m_EnableAutoExposure == 1,
		"Exposure enable state was not uploaded");
}

bool TestPostProcessSceneSettingsProjection()
{
	using Value = Vans::VansSerializedValue;
	const Value sceneSettings = Value::Object({
		{ "postProcess", Value::Object({
			{ "exposure", Value::Object({
				{ "enableAutoExposure", Value::Bool(false) },
				{ "exposureCompensation", Value::Float(1.25) },
				{ "minEV100", Value::Float(-4.0) },
				{ "maxEV100", Value::Float(12.0) }
			}) },
			{ "bloom", Value::Object({
				{ "enable", Value::Bool(true) },
				{ "intensity", Value::Float(0.75) }
			}) },
			{ "toneMapping", Value::Object({
				{ "type", Value::Int(2) },
				{ "whitePoint", Value::Float(8.0) }
			}) },
			{ "colorGrading", Value::Object({
				{ "enable", Value::Bool(true) },
				{ "saturation", Value::Float(1.4) }
			}) }
		}) }
	});

	const Vans::VansSceneRenderSettingsConfig config =
		Vans::VansSceneRenderSettingsConfigReader::Read(sceneSettings);
	if (!Expect(config.postProcess.has_value(), "Post-process scene settings were not projected"))
		return false;
	const Vans::VansScenePostProcessSettingsConfig& postProcess = *config.postProcess;
	if (!Expect(postProcess.enableAutoExposure == false &&
		postProcess.exposureCompensation == 1.25f &&
		postProcess.minEV100 == -4.0f && postProcess.maxEV100 == 12.0f,
		"Exposure scene settings were not projected"))
		return false;
	if (!Expect(postProcess.enableBloom == true && postProcess.bloomIntensity == 0.75f,
		"Bloom scene settings were not projected"))
		return false;
	if (!Expect(postProcess.toneMapperType == 2 && postProcess.whitePoint == 8.0f,
		"Tone-mapping scene settings were not projected"))
		return false;
	return Expect(postProcess.enableColorGrading == true && postProcess.saturation == 1.4f,
		"Color-grading scene settings were not projected");
}

bool TestDualPunctualShadowAtlasOwnership()
{
	using namespace VansGraphics;
	VansPunctualShadowManager manager(256, 128, 2);
	VansPunctualShadowBudget budget = manager.GetBudget();
	budget.maxDirtyTexelsPerFrame = 8ull * 128ull * 128ull;
	manager.SetBudget(budget);

	VansPunctualShadowCameraData camera;
	camera.position = glm::vec3(0.0f);
	std::vector<VansPunctualShadowLightInput> lights(8);
	for (uint32_t index = 0; index < lights.size(); ++index)
	{
		auto& light = lights[index];
		light.stableLightId = index + 1u;
		light.type = VansPunctualShadowLightType::Spot;
		light.gpuLightIndex = index;
		light.position = glm::vec3(static_cast<float>(index), 0.0f, 2.0f);
		light.intensity = 10.0f;
		light.radius = 10.0f;
		light.settings.castShadows = true;
		light.settings.resolution = VansShadowResolution::R128;
		light.settings.maxShadowDistance = 100.0f;
	}

	manager.PrepareFrame(camera, lights, 1);
	if (!Expect(manager.GetStatistics().residentLights == lights.size(),
		"Dual punctual Atlas did not make all eight 128px spot lights resident"))
		return false;
	if (!Expect(manager.HasRenderJobs(0) && manager.HasRenderJobs(1),
		"Dual punctual Atlas did not schedule work on both layers"))
		return false;
	for (const VansPunctualShadowRenderJob& job : manager.GetRenderJobs())
	{
		if (!Expect(job.shadowMetaIndex < manager.GetGPUShadowData().size() &&
			job.shadowViewIndex < manager.GetGPUShadowViews().size(),
			"Punctual render job was published with an invalid metadata/view index"))
			return false;
		const uint32_t ownerAtlas =
			(manager.GetGPUShadowData()[job.shadowMetaIndex].ownerKey >> 16u) & 0x3u;
		if (!Expect(ownerAtlas == job.atlasIndex,
			"Punctual render job Atlas does not match its sampling metadata"))
			return false;
	}

	const auto firstSnapshot = manager.CaptureDebugSnapshot();
	std::vector<uint32_t> atlasByStableId(lights.size() + 1u, VANS_INVALID_SHADOW_INDEX);
	for (const auto& light : firstSnapshot.lights)
	{
		if (!light.activeBlocks[0].IsValid())
			return Expect(false, "Resident punctual light has no valid Atlas block");
		atlasByStableId[light.stableLightId] = light.activeBlocks[0].atlasIndex;
	}

	std::reverse(lights.begin(), lights.end());
	for (uint32_t index = 0; index < lights.size(); ++index)
		lights[index].gpuLightIndex = index;
	manager.PrepareFrame(camera, lights, 2);

	const auto& metadata = manager.GetGPUShadowData();
	for (const auto& light : lights)
	{
		const uint32_t metaIndex = manager.GetShadowMetaIndex(light.stableLightId);
		if (!Expect(metaIndex < metadata.size(), "Reordered punctual light lost its metadata binding"))
			return false;
		const uint32_t ownerKey = metadata[metaIndex].ownerKey;
		const uint32_t ownerType = (ownerKey >> 8u) & 0x3u;
		const uint32_t ownerLightIndex = ownerKey & 0xffu;
		const uint32_t ownerAtlas = (ownerKey >> 16u) & 0x3u;
		if (!Expect(ownerType == static_cast<uint32_t>(light.type) &&
			ownerLightIndex == light.gpuLightIndex &&
			ownerAtlas == atlasByStableId[light.stableLightId],
			"Reordered punctual light metadata points at the wrong owner or Atlas"))
			return false;
	}

	const uint32_t duplicatedStableId = lights[0].stableLightId;
	lights[1].stableLightId = duplicatedStableId;
	manager.PrepareFrame(camera, lights, 3);
	return Expect(manager.GetShadowMetaIndex(duplicatedStableId) == VANS_INVALID_SHADOW_INDEX,
		"Duplicate stable light IDs were allowed to alias one shadow metadata entry");
}
}

int main()
{
    if (!TestPackageManifestRoundTrip())
        return 1;
    if (!TestAssetPolicies())
        return 2;
    if (!TestGameplayFrameOrder())
        return 3;
    if (!TestMotionMatchingAutoBuildLocomotionMetadataContract())
        return 31;
    if (!TestAudioDistanceAttenuationContract())
        return 4;
    if (!TestAudioBusContract())
        return 5;
    if (!TestAudioMixConfigContract())
        return 6;
    if (!TestAudioOcclusionContract())
        return 7;
    if (!TestAudioDirectionalityContract())
        return 8;
    if (!TestAudioComponentOcclusionReadContract())
        return 9;
    if (!TestAudioSourceBindingNullObjectContract())
        return 10;
    if (!TestAudioPreviewSettingsContract())
        return 11;
    if (!TestAudioVoiceVirtualizationContract())
        return 12;
    if (!TestAudioReverbEnvironmentContract())
        return 13;
    if (!TestAudioReverbPresetAssetContract())
        return 14;
    if (!TestAudioBusSnapshotAssetContract())
        return 15;
    if (!TestAudioDuckingRulesAssetContract())
        return 16;
    if (!TestIndexedAssetResolutionContract())
        return 17;
    if (!TestPackagedAudioResourcePlanRoundTrip())
        return 18;
    if (!TestAudioReverbZoneRuntimeProjection())
        return 19;
	if (!TestExposureParameterContract())
		return 20;
	if (!TestPostProcessSceneSettingsProjection())
		return 21;
	if (!TestDualPunctualShadowAtlasOwnership())
		return 22;
    std::cout << "Forest contract tests passed\n";
    return 0;
}
