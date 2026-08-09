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
#include "../EngineCore/AudioCore/VansAudioWaveform.h"
#include "../EngineCore/RenderCore/VansPostProcessProfile.h"
#include "../EngineCore/RenderCore/GICore/VansGISettings.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVideoThumbnail.h"
#include "../EngineCore/RenderCore/ShadowCore/VansPunctualShadowManager.h"
#include "../EngineCore/RuntimeCore/VansPackageManifest.h"
#include "../EngineCore/RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include "../EngineCore/SceneCore/VansSceneCameraMediaComponentReader.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeComponentKey.h"
#include "../EngineCore/SceneCore/VansSceneRenderSettingsConfigReader.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePropertyRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePlayer.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePreAnimatedState.h"
#include "../EngineCore/TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../EngineCore/TimelineCore/VansTimelineDependencyBuilder.h"
#include "../EngineCore/TimelineCore/VansTimelineCompiler.h"
#include "../EngineCore/TimelineCore/VansTimelineSerialization.h"
#include "../EngineCore/TimelineCore/VansTimelineValidator.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineEditService.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineCommandMap.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineEditorStateStore.h"
#include "../EngineCore/EditorCore/VansAssetDocumentRegistry.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AnimationCore/VansAnimationClip.h"
#include "../EngineCore/AnimationCore/VansAnimationController.h"
#include "../EngineCore/AnimationCore/VansAnimGraph.h"
#include "../EngineCore/AnimationCore/VansAnimatorIO.h"
#include "../EngineCore/AnimationCore/VansAnimatorRuntimeCompiler.h"
#include "../EngineCore/AnimationCore/VansAnimationSampler.h"
#include "../EngineCore/AnimationCore/VansPoseMath.h"
#include "../EngineCore/AnimationCore/VansPosePayloadMixer.h"
#include "../EngineCore/AnimationCore/VansAnimationLayer.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationAuthoringBridge.h"
#include "../EngineCore/ParticleCore/VansParticleRuntime.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/ScriptCore/VansTransform.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
thread_local bool g_TrackHeapAllocations = false;
thread_local std::size_t g_TrackedHeapAllocationCount = 0;

void RecordHeapAllocation() noexcept
{
    if (g_TrackHeapAllocations)
        ++g_TrackedHeapAllocationCount;
}
}

void* operator new(std::size_t size)
{
    if (void* pointer = std::malloc(size == 0 ? 1 : size))
    {
        RecordHeapAllocation();
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
#if defined(_WIN32)
    if (void* pointer = _aligned_malloc(size == 0 ? 1 : size,
        static_cast<std::size_t>(alignment)))
#else
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) == 0)
#endif
    {
        RecordHeapAllocation();
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

void operator delete[](void* pointer, std::align_val_t alignment) noexcept
{
    ::operator delete(pointer, alignment);
}

void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(pointer, alignment);
}

namespace fs = std::filesystem;

namespace
{
std::size_t CountHeapAllocations(const std::function<void()>& operation)
{
    g_TrackedHeapAllocationCount = 0;
    g_TrackHeapAllocations = true;
    operation();
    g_TrackHeapAllocations = false;
    return g_TrackedHeapAllocationCount;
}

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

bool InstallTestBaseLayer(VansGraphics::VansAnimationController& controller,
                          std::unique_ptr<VansGraphics::VansAnimGraph> graph,
                          std::string& error)
{
    using namespace VansGraphics;
    VansAnimationLayerGraphSetup setup;
    setup.definition.id = "layer-base";
    setup.definition.name = "Base";
    setup.definition.graphId = "graph-base";
    setup.definition.kind = VansAnimationLayerKind::Base;
    setup.definition.rootMotion = VansLayerRootMotionMode::Base;
    setup.definition.nodeTracks = VansLayerNodeTrackMode::Override;
    setup.graph = std::move(graph);
    std::vector<VansAnimationLayerGraphSetup> layers;
    layers.push_back(std::move(setup));
    return controller.SetLayerStack(std::move(layers), error);
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
    if (!Expect(authoring.cookedArtifacts == 0, "Authoring scan implicitly cooked an artifact"))
        return false;
    if (!Expect(Vans::VansAssetDatabase::Classify("Probe.vtimeline") == Vans::VansAssetType::Timeline,
        "Timeline asset extension is not classified canonically"))
        return false;
    return Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Timeline) == "TimelineImporter",
        "Timeline assets are not owned by the canonical Timeline importer");
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
	frame.updateTimelinesPostScript = [&](double) { trace.push_back("timeline-post"); };
	frame.updateCameraScripts = [&] { trace.push_back("camera"); };
	frame.updateTimelinesCamera = [&](double) { trace.push_back("timeline-camera"); };
	Vans::VansRuntimeFrameScheduler::RunGameplay(frame);

	const std::vector<std::string> expected{ "physics", "scripts", "cct", "timeline-post", "camera", "timeline-camera" };
    if (!Expect(trace == expected, "Gameplay frame callback order changed"))
        return false;

    trace.clear();
    frame.sceneReady = false;
    Vans::VansRuntimeFrameScheduler::RunGameplay(frame);
    return Expect(trace.empty(), "Gameplay callbacks ran without a ready scene");
}

bool TestTimelinePropertyRegistryContract()
{
	Vans::VansTimelinePropertyRegistry registry;
	Vans::VansTimelineKeyValue stored = 2.0f;
	std::string error;
	Vans::VansTimelineRuntimePropertyDescriptor descriptor;
	descriptor.descriptorId = "Test.Float";
	descriptor.componentTypeId = 77;
	descriptor.valueType = Vans::VansTimelineChannelType::Float;
	descriptor.read = [&](const Vans::VansResolvedTimelineTarget&, Vans::VansTimelineKeyValue& value, std::string&)
	{
		value = stored;
		return true;
	};
	descriptor.write = [&](const Vans::VansResolvedTimelineTarget&, const Vans::VansTimelineKeyValue& value, std::string&)
	{
		stored = value;
		return true;
	};
	if (!Expect(registry.Register(descriptor, error), error.c_str()))
		return false;
	if (!Expect(!registry.Register(descriptor, error),
		"Timeline Property registry accepted a duplicate descriptor ID"))
		return false;

	Vans::VansTimelinePropertyOutput output;
	output.componentTypeId = 77;
	output.descriptorId = "Test.Float";
	output.valueType = Vans::VansTimelineChannelType::Float;
	output.value = 3.0f;
	Vans::VansTimelineRestoreCallback restore;
	if (!Expect(registry.Apply(Vans::VansTimelineBlendMode::Additive, {}, output, restore, error), error.c_str()))
		return false;
	if (!ExpectNear(std::get<float>(stored), 5.0f, 0.0001f,
		"Timeline Property additive blend produced the wrong value"))
		return false;
	if (!Expect(static_cast<bool>(restore), "Timeline Property adapter did not provide restore state"))
		return false;
	restore();
	if (!ExpectNear(std::get<float>(stored), 2.0f, 0.0001f,
		"Timeline Property restore did not reinstate the pre-animated value"))
		return false;

	output.valueType = Vans::VansTimelineChannelType::Double;
	if (!Expect(!registry.Apply(Vans::VansTimelineBlendMode::Override, {}, output, restore, error),
		"Timeline Property registry accepted an authored descriptor value type mismatch"))
		return false;
	output.valueType = Vans::VansTimelineChannelType::Float;
	output.componentTypeId = 78;
	return Expect(!registry.Apply(Vans::VansTimelineBlendMode::Override, {}, output, restore, error),
		"Timeline Property registry accepted an authored component type mismatch");
}

std::shared_ptr<const Vans::VansCompiledTimeline> CompileTimelineEventProbe(
	const Vans::VansTimelineEventTrackConfig& eventConfig,
	std::string& error)
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 100;
	asset.playbackRange = { 0, 100 };
	asset.workRange = { 0, 100 };
	Vans::VansTimelineTrack track;
	track.id = "event-track";
	track.type = Vans::VansTimelineTrackType::EventSignal;
	track.name = "Events";
	track.config = eventConfig;
	Vans::VansTimelineSection section;
	section.id = "event-section";
	section.startTick = 0;
	section.durationTicks = 100;
	Vans::VansTimelineChannel channel;
	channel.id = "event-channel";
	channel.name = "Signals";
	channel.type = Vans::VansTimelineChannelType::EventPayload;
	channel.keys.push_back({ "event-start", 0, Vans::VansTimelineEventPayload{ "Probe", {} },
		Vans::VansTimelineInterpolation::Constant });
	channel.keys.push_back({ "event-middle", 50, Vans::VansTimelineEventPayload{ "Probe", {} },
		Vans::VansTimelineInterpolation::Constant });
	section.channels.push_back(std::move(channel));
	track.sections.push_back(std::move(section));
	asset.tracks.push_back(std::move(track));
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset);
	if (!compiled)
	{
		error = compiled.diagnostics.empty() ? "Timeline Event probe compilation failed" : compiled.diagnostics.front().message;
		return {};
	}
	return compiled.timeline;
}

bool TestTimelineEventPolicyContract()
{
	Vans::VansTimelineEventTrackConfig config;
	config.signalId = "probe.signal";
	config.firePolicy = "Both";
	config.seekPolicy = "Crossed";
	config.loopPolicy = "EveryLoop";
	std::string error;
	const auto timeline = CompileTimelineEventProbe(config, error);
	if (!Expect(static_cast<bool>(timeline), error.c_str()))
		return false;

	Vans::VansTimelineBindingResolver bindings;
	std::vector<Vans::VansTimelineEvaluationOutput> outputs;
	Vans::VansTimelineDiagnostics diagnostics;
	Vans::VansTimelineEvaluator::Evaluate(*timeline, Vans::VansTimelineEvaluationPhase::PostScript,
		{ { 100, 0, Vans::VansTimelineEvaluationReason::LoopWrap,
			Vans::VansTimelineSeekPolicy::AllEdges, 1, 1 } }, {}, bindings, "probe", outputs, diagnostics);
	if (!Expect(outputs.size() == 1 && std::get<Vans::VansTimelineEventOutput>(outputs.front().value).tick == 0,
		"Timeline loop wrap traversed and re-fired interior Event keys"))
		return false;

	Vans::VansTimelineEventTrackConfig onceConfig = config;
	onceConfig.seekPolicy = "ExactTick";
	onceConfig.oncePerPlayback = true;
	const auto onceTimeline = CompileTimelineEventProbe(onceConfig, error);
	if (!Expect(static_cast<bool>(onceTimeline), error.c_str()))
		return false;
	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle owner = world.CreateEntity({ "timeline-owner", "Timeline Owner" });
	Vans::VansRuntimeTimelineComponent component;
	component.instance.updateMode = Vans::VansTimelineUpdateMode::Manual;
	Vans::VansTimelinePlayer player;
	if (!Expect(player.Load(onceTimeline, component, &world, owner, "event-player", error), error.c_str()))
		return false;
	player.Play();
	player.SeekTicks(50, Vans::VansTimelineSeekPolicy::ExactTick, Vans::VansTimelineEvaluationReason::Scrub);
	player.UpdatePostScript(0.0, outputs);
	if (!Expect(outputs.size() == 1, "Timeline once-per-playback Event did not fire on its first exact seek"))
		return false;
	player.SeekTicks(50, Vans::VansTimelineSeekPolicy::ExactTick, Vans::VansTimelineEvaluationReason::Scrub);
	player.UpdatePostScript(0.0, outputs);
	if (!Expect(outputs.empty(), "Timeline once-per-playback Event fired more than once"))
		return false;
	player.Stop();
	player.Play();
	player.SeekTicks(50, Vans::VansTimelineSeekPolicy::ExactTick, Vans::VansTimelineEvaluationReason::Scrub);
	player.UpdatePostScript(0.0, outputs);
	return Expect(outputs.size() == 1, "Timeline once-per-playback Event did not reset for a new playback");
}

bool TestTimelineManualRuntimeControlContract()
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 60000;
	asset.playbackRange = { 0, 60000 };
	asset.workRange = { 0, 60000 };
	const Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(asset);
	if (!Expect(static_cast<bool>(compiled), "Timeline manual-control probe did not compile"))
		return false;

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle owner = world.CreateEntity({ "manual-timeline-owner", "Manual Timeline Owner" });
	Vans::VansRuntimeTimelineComponent component;
	component.assetGuid = "manual-timeline-asset";
	component.instance.playOn = Vans::VansTimelinePlayOn::Manual;
	const Vans::VansComponentHandle handle = world.AddComponent(
		owner,
		Vans::VansRuntimeComponentType_Timeline,
		component,
		"manual-timeline-component");

	Vans::VansTimelineRuntimeSystem runtime;
	runtime.RegisterWorld(&world);
	runtime.SetAssetLoader([timeline = compiled.timeline](
		const Vans::VansRuntimeTimelineComponent&,
		std::shared_ptr<const Vans::VansCompiledTimeline>& loaded,
		std::string& error)
	{
		error.clear();
		loaded = timeline;
		return true;
	});
	runtime.SyncTimelineComponents();

	Vans::VansTimelinePlayerState state{};
	Vans::VansTimelineTick tick = -1;
	if (!Expect(runtime.GetComponentState(handle, state, tick) &&
		state == Vans::VansTimelinePlayerState::Stopped && tick == 0,
		"Manual Timeline component did not initialize in Stopped state"))
		return false;
	if (!Expect(runtime.PlayComponent(handle, true), "Manual Timeline play command failed"))
		return false;
	runtime.UpdatePostScript(0.25);
	if (!Expect(runtime.GetComponentState(handle, state, tick) &&
		state == Vans::VansTimelinePlayerState::Playing && tick > 0,
		"Manual Timeline did not advance after play"))
		return false;
	if (!Expect(runtime.PauseComponent(handle) &&
		runtime.GetComponentState(handle, state, tick) && state == Vans::VansTimelinePlayerState::Paused,
		"Manual Timeline pause command failed"))
		return false;
	if (!Expect(runtime.ResumeComponent(handle) &&
		runtime.GetComponentState(handle, state, tick) && state == Vans::VansTimelinePlayerState::Playing,
		"Manual Timeline resume command failed"))
		return false;
	if (!Expect(runtime.PlayComponent(handle, true) &&
		runtime.GetComponentState(handle, state, tick) && tick == 0,
		"Manual Timeline restart did not return to the playback start"))
		return false;
	if (!Expect(runtime.StopComponent(handle) &&
		runtime.GetComponentState(handle, state, tick) &&
		state == Vans::VansTimelinePlayerState::Stopped && tick == 0,
		"Manual Timeline stop command failed"))
		return false;
	return Expect(!runtime.PlayComponent({}, true),
		"Manual Timeline accepted an invalid component handle");
}

bool TestTimelineNestedPlayerLifetimeContract()
{
	Vans::VansTimelineAsset childAsset;
	childAsset.durationTicks = 60000;
	childAsset.playbackRange = { 0, 60000 };
	childAsset.workRange = { 0, 60000 };
	const Vans::VansTimelineCompileResult childCompiled = Vans::VansTimelineCompiler::Compile(childAsset);
	if (!Expect(static_cast<bool>(childCompiled), "Nested Timeline child probe did not compile"))
		return false;

	Vans::VansTimelineAsset rootAsset;
	rootAsset.durationTicks = 60000;
	rootAsset.playbackRange = { 0, 60000 };
	rootAsset.workRange = { 0, 60000 };
	Vans::VansTimelineTrack childTrack;
	childTrack.id = "nested-player-track";
	childTrack.name = "Nested Player";
	childTrack.type = Vans::VansTimelineTrackType::SubTimeline;
	childTrack.config = Vans::VansTimelineSubTimelineTrackConfig{};
	Vans::VansTimelineSection childSection;
	childSection.id = "nested-player-section";
	childSection.durationTicks = 60000;
	childSection.assetGuid = "nested-player-child";
	childSection.config = childTrack.config;
	childTrack.sections.push_back(std::move(childSection));
	rootAsset.tracks.push_back(std::move(childTrack));

	Vans::VansTimelineCompileOptions compileOptions;
	compileOptions.dependencyLoader = [childAsset](
		const Vans::VansTimelineAssetReference& reference,
		Vans::VansTimelineAsset& loaded,
		std::string& identity,
		std::string& error)
	{
		if (reference.assetGuid != "nested-player-child")
		{
			error = "Unexpected nested Timeline dependency";
			return false;
		}
		loaded = childAsset;
		identity = reference.assetGuid;
		return true;
	};
	const Vans::VansTimelineCompileResult rootCompiled =
		Vans::VansTimelineCompiler::Compile(rootAsset, compileOptions);
	if (!Expect(static_cast<bool>(rootCompiled), "Nested Timeline root probe did not compile"))
		return false;

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle owner = world.CreateEntity({ "nested-player-owner", "Nested Player Owner" });
	Vans::VansRuntimeTimelineComponent component;
	component.assetGuid = "nested-player-root";
	component.instance.playOn = Vans::VansTimelinePlayOn::Manual;
	const Vans::VansComponentHandle handle = world.AddComponent(
		owner,
		Vans::VansRuntimeComponentType_Timeline,
		component,
		"nested-player-component");

	int rootLoads = 0;
	int childLoads = 0;
	Vans::VansTimelineRuntimeSystem runtime;
	runtime.RegisterWorld(&world);
	runtime.SetAssetLoader([&](
		const Vans::VansRuntimeTimelineComponent& requested,
		std::shared_ptr<const Vans::VansCompiledTimeline>& loaded,
		std::string& error)
	{
		error.clear();
		if (requested.assetGuid == "nested-player-root")
		{
			++rootLoads;
			loaded = rootCompiled.timeline;
			return true;
		}
		if (requested.assetGuid == "nested-player-child")
		{
			++childLoads;
			loaded = childCompiled.timeline;
			return true;
		}
		error = "Unknown nested Timeline runtime asset";
		return false;
	});
	runtime.SyncTimelineComponents();
	if (!Expect(runtime.PlayComponent(handle, true), "Nested Timeline root probe could not play"))
		return false;
	for (int frame = 0; frame < 5; ++frame)
		runtime.UpdatePostScript(1.0 / 60.0);
	if (rootLoads != 1 || childLoads != 1)
		std::cerr << "[TimelineNestedLifetime] rootLoads=" << rootLoads
			<< " childLoads=" << childLoads << '\n';
	return Expect(rootLoads == 1 && childLoads == 1,
		"Nested Timeline player was destroyed and reloaded between active frames");
}

Vans::VansTimelineKeyValue TimelineProbeValue(Vans::VansTimelineChannelType type)
{
	switch (type)
	{
	case Vans::VansTimelineChannelType::Bool: return true;
	case Vans::VansTimelineChannelType::Int32: return std::int32_t{ 7 };
	case Vans::VansTimelineChannelType::Int64: return std::int64_t{ 9 };
	case Vans::VansTimelineChannelType::Float: return 1.25f;
	case Vans::VansTimelineChannelType::Double: return 2.5;
	case Vans::VansTimelineChannelType::Enum: return std::string{ "Open" };
	case Vans::VansTimelineChannelType::String: return std::string{ "Timeline" };
	case Vans::VansTimelineChannelType::Vec2: return Vans::VansTimelineVec2{ { 1.0, 2.0 } };
	case Vans::VansTimelineChannelType::Vec3: return Vans::VansTimelineVec3{ { 1.0, 2.0, 3.0 } };
	case Vans::VansTimelineChannelType::Vec4: return Vans::VansTimelineVec4{ { 1.0, 2.0, 3.0, 4.0 } };
	case Vans::VansTimelineChannelType::Quaternion: return Vans::VansTimelineQuaternion{ { 0.0, 0.0, 0.0, 1.0 } };
	case Vans::VansTimelineChannelType::ColorLinear: return Vans::VansTimelineColorLinear{ { 0.1, 0.2, 0.3, 1.0 } };
	case Vans::VansTimelineChannelType::ColorSrgb: return Vans::VansTimelineColorSrgb{ { 0.4, 0.5, 0.6, 1.0 } };
	case Vans::VansTimelineChannelType::ObjectReference:
		return Vans::VansTimelineObjectReference{ "object-guid", "Assets/Object.asset", "Probe" };
	case Vans::VansTimelineChannelType::EventPayload:
		return Vans::VansTimelineEventPayload{ "Probe.Payload", Vans::VansSerializedValue::Object({
			{ "enabled", Vans::VansSerializedValue::Bool(true) } }) };
	}
	return {};
}

bool TestTimelineCanonicalRoundTripContract()
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 120000;
	asset.playbackRange = { 100, 110000 };
	asset.workRange = { -6000, 120000 };
	asset.metadata.displayName = "All Timeline Tracks";
	asset.metadata.tags = { "contract", "canonical" };
	Vans::VansTimelineBinding primary;
	primary.id = "binding-primary";
	primary.displayName = "Primary";
	primary.targetGuid = "entity-primary";
	asset.bindings.push_back(primary);
	Vans::VansTimelineBinding secondary = primary;
	secondary.id = "binding-secondary";
	secondary.displayName = "Secondary";
	secondary.targetGuid = "entity-secondary";
	asset.bindings.push_back(secondary);
	asset.groups.push_back({ "group-main", {}, "Main" });
	asset.markers.push_back({ "marker-fence", 60000, "Fence", { 1.0f, 0.5f, 0.2f, 1.0f }, "Contract", true });

	constexpr int trackCount = static_cast<int>(Vans::VansTimelineTrackType::Custom) + 1;
	for (int index = 0; index < trackCount; ++index)
	{
		const auto type = static_cast<Vans::VansTimelineTrackType>(index);
		Vans::VansTimelineTrack track;
		track.id = "track-" + std::to_string(index);
		track.type = type;
		track.name = Vans::VansTimelineSerialization::TrackTypeName(type);
		track.bindingId = "binding-primary";
		track.groupId = "group-main";
		track.order = index;
		track.priority = index - 4;
		track.config = Vans::VansTimelineEditService::DefaultTrackConfig(type);
		if (auto* constraint = std::get_if<Vans::VansTimelineConstraintTrackConfig>(&track.config))
		{
			constraint->sourceBindingId = "binding-primary";
			constraint->targetBindingId = "binding-secondary";
		}
		if (auto* camera = std::get_if<Vans::VansTimelineCameraCutTrackConfig>(&track.config))
		{
			camera->cameraBindingId = "binding-primary";
			camera->targetCameraBindingId = "binding-secondary";
			camera->priority = 17;
		}
		if (auto* custom = std::get_if<Vans::VansTimelineCustomTrackConfig>(&track.config))
			custom->customTypeId = "contract.custom";

		Vans::VansTimelineSection section;
		section.id = "section-" + std::to_string(index);
		section.name = track.name;
		section.startTick = index * 100;
		section.durationTicks = 1000;
		section.sourceInTick = 10;
		section.sourceOutTick = 1010;
		section.playRate = 1.25;
		section.loopMode = Vans::VansTimelineLoopMode::PingPong;
		section.loopCount = 2;
		section.config = track.config;
		if (index == 0)
		{
			constexpr int channelCount = static_cast<int>(Vans::VansTimelineChannelType::EventPayload) + 1;
			for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex)
			{
				const auto channelType = static_cast<Vans::VansTimelineChannelType>(channelIndex);
				Vans::VansTimelineChannel channel;
				channel.id = "channel-" + std::to_string(channelIndex);
				channel.name = "Channel " + std::to_string(channelIndex);
				channel.type = channelType;
				channel.preExtrapolation = Vans::VansTimelineExtrapolation::Hold;
				Vans::VansTimelineKey key;
				key.id = "key-" + std::to_string(channelIndex);
				key.tick = 42;
				key.value = TimelineProbeValue(channelType);
				key.interpolation = channelType == Vans::VansTimelineChannelType::Quaternion
					? Vans::VansTimelineInterpolation::Slerp : Vans::VansTimelineInterpolation::Constant;
				channel.keys.push_back(std::move(key));
				section.channels.push_back(std::move(channel));
			}
		}
		track.sections.push_back(std::move(section));
		asset.tracks.push_back(std::move(track));
	}

	const auto encoded = Vans::VansTimelineSerialization::Encode(asset);
	std::string forbiddenPath;
	if (!Expect(!Vans::VansTimelineSerialization::ContainsForbiddenFormatField(encoded, forbiddenPath),
		"Canonical Timeline serialization emitted a forbidden format-version field"))
		return false;
	Vans::VansTimelineAsset decoded;
	std::string error;
	if (!Expect(Vans::VansTimelineSerialization::Decode(encoded, decoded, error), error.c_str()))
		return false;
	if (!Expect(Vans::VansTimelineSerialization::Encode(decoded) == encoded,
		"Timeline all-track/all-channel JSON roundtrip changed canonical author data"))
		return false;
	for (int index = 0; index < trackCount; ++index)
	{
		const auto type = static_cast<Vans::VansTimelineTrackType>(index);
		Vans::VansTimelineTrackType parsed{};
		if (!Expect(Vans::VansTimelineSerialization::TryParseTrackType(
			Vans::VansTimelineSerialization::TrackTypeName(type), parsed) && parsed == type,
			"Timeline built-in track type name did not roundtrip"))
			return false;
	}
	auto forbidden = encoded;
	forbidden["tracks"][0]["config"]["schemaVersion"] = 1;
	return Expect(!Vans::VansTimelineSerialization::Decode(forbidden, decoded, error),
		"Timeline decoder accepted a forbidden schemaVersion field");
}

bool TestTimelineCameraShakeEvaluationContract()
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 60000;
	asset.playbackRange = { 0, 60000 };
	asset.workRange = { 0, 60000 };

	Vans::VansTimelineBinding cameraBinding;
	cameraBinding.id = "binding-camera";
	cameraBinding.displayName = "Camera";
	cameraBinding.targetGuid = "camera-entity";
	asset.bindings.push_back(cameraBinding);

	Vans::VansTimelineTrack cutTrack;
	cutTrack.id = "camera-cut";
	cutTrack.type = Vans::VansTimelineTrackType::CameraCut;
	cutTrack.name = "Camera Cut";
	cutTrack.priority = 100;
	Vans::VansTimelineCameraCutTrackConfig cutConfig;
	cutConfig.cameraBindingId = cameraBinding.id;
	cutConfig.targetCameraBindingId = cameraBinding.id;
	cutConfig.priority = 100;
	cutTrack.config = cutConfig;
	Vans::VansTimelineSection cutSection;
	cutSection.id = "camera-cut-section";
	cutSection.durationTicks = 30000;
	cutTrack.sections.push_back(cutSection);
	asset.tracks.push_back(cutTrack);

	Vans::VansTimelineTrack shakeTrack;
	shakeTrack.id = "camera-shake";
	shakeTrack.type = Vans::VansTimelineTrackType::CameraShake;
	shakeTrack.name = "Camera Shake";
	shakeTrack.bindingId = cameraBinding.id;
	shakeTrack.priority = 230;
	Vans::VansTimelineCameraShakeTrackConfig shakeConfig;
	shakeConfig.space = "CameraLocal";
	shakeConfig.amplitudeScale = 1.0;
	shakeConfig.priority = 80;
	shakeTrack.config = shakeConfig;
	Vans::VansTimelineSection shakeSection;
	shakeSection.id = "camera-shake-section";
	shakeSection.durationTicks = 30000;
	Vans::VansTimelineChannel positionChannel;
	positionChannel.id = "camera-shake-position";
	positionChannel.name = "positionOffset";
	positionChannel.type = Vans::VansTimelineChannelType::Vec3;
	positionChannel.keys.push_back({ "camera-shake-position-key", 0,
		Vans::VansTimelineVec3{ { 0.12, -0.03, 0.02 } }, Vans::VansTimelineInterpolation::Constant });
	Vans::VansTimelineChannel rotationChannel;
	rotationChannel.id = "camera-shake-rotation";
	rotationChannel.name = "rotationOffset";
	rotationChannel.type = Vans::VansTimelineChannelType::Vec3;
	rotationChannel.keys.push_back({ "camera-shake-rotation-key", 0,
		Vans::VansTimelineVec3{ { 1.0, -2.0, 0.5 } }, Vans::VansTimelineInterpolation::Constant });
	shakeSection.channels.push_back(std::move(positionChannel));
	shakeSection.channels.push_back(std::move(rotationChannel));
	shakeTrack.sections.push_back(std::move(shakeSection));
	asset.tracks.push_back(std::move(shakeTrack));

	const Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(asset);
	if (!Expect(static_cast<bool>(compiled), "Camera Shake Timeline probe did not compile"))
		return false;

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle cameraEntity = world.CreateEntity({ "camera-entity", "Camera" });
	Vans::VansTimelineBindingResolver bindings;
	bindings.BindWorld(&world, cameraEntity);
	std::vector<Vans::VansTimelineEvaluationOutput> outputs;
	Vans::VansTimelineDiagnostics diagnostics;
	Vans::VansTimelineEvaluator::Evaluate(*compiled.timeline, Vans::VansTimelineEvaluationPhase::Camera,
		{ { 0, 5000, Vans::VansTimelineEvaluationReason::Playback,
			Vans::VansTimelineSeekPolicy::ContinuousOnly, 1, 0 } }, {}, bindings, "camera-shake-probe",
		outputs, diagnostics);
	if (!Expect(!Vans::VansTimelineValidator::HasErrors(diagnostics),
		"Camera Shake Timeline probe produced binding diagnostics"))
		return false;
	if (!Expect(outputs.size() == 2, "Camera phase did not output CameraCut and CameraShake"))
		return false;

	const Vans::VansTimelineEvaluationOutput* cutOutput = nullptr;
	const Vans::VansTimelineEvaluationOutput* shakeOutput = nullptr;
	for (const auto& output : outputs)
	{
		if (output.trackType == Vans::VansTimelineTrackType::CameraCut)
			cutOutput = &output;
		else if (output.trackType == Vans::VansTimelineTrackType::CameraShake)
			shakeOutput = &output;
	}
	if (!Expect(cutOutput && shakeOutput, "Camera phase omitted cut or shake outputs"))
		return false;
	if (!Expect(shakeOutput->priority > cutOutput->priority,
		"Camera Shake priority should apply after CameraCut"))
		return false;
	const auto* shake = std::get_if<Vans::VansTimelineCameraShakeOutput>(&shakeOutput->value);
	if (!Expect(shake && shake->active, "Camera Shake output payload was not active"))
		return false;
	if (!ExpectNear(static_cast<float>(shake->positionOffset.value[0]), 0.12f, 0.0001f,
		"Camera Shake position offset did not sample"))
		return false;
	return ExpectNear(static_cast<float>(shake->rotationOffset.value[1]), -2.0f, 0.0001f,
		"Camera Shake rotation offset did not sample");
}

bool TestTimelineCapabilityAndGuidContract()
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 1000;
	asset.playbackRange = { 0, 1000 };
	asset.workRange = { 0, 1000 };
	Vans::VansTimelineBinding binding;
	binding.id = "binding";
	binding.displayName = "Target";
	binding.targetGuid = "entity";
	asset.bindings.push_back(binding);

	Vans::VansTimelineTrack media;
	media.id = "media";
	media.type = Vans::VansTimelineTrackType::Media;
	media.name = "Media";
	media.bindingId = binding.id;
	Vans::VansTimelineMediaTrackConfig mediaConfig;
	mediaConfig.targetKind = "MaterialSlot";
	mediaConfig.materialSlot = "0";
	mediaConfig.outputAudio = true;
	media.config = mediaConfig;
	asset.tracks.push_back(media);

	Vans::VansTimelineTrack camera;
	camera.id = "camera";
	camera.type = Vans::VansTimelineTrackType::CameraCut;
	camera.name = "Camera";
	camera.bindingId = binding.id;
	Vans::VansTimelineCameraCutTrackConfig cameraConfig;
	cameraConfig.cameraBindingId = binding.id;
	cameraConfig.aspectPolicy = "Letterbox";
	camera.config = cameraConfig;
	asset.tracks.push_back(camera);

	Vans::VansTimelineTrack spawn;
	spawn.id = "spawn";
	spawn.type = Vans::VansTimelineTrackType::Spawnable;
	spawn.name = "Spawn";
	Vans::VansTimelineSpawnableTrackConfig spawnConfig;
	spawnConfig.spawnTemplateGuid = "spawn-guid";
	spawnConfig.spawnTemplatePath = "Assets/Spawn.template";
	spawn.config = spawnConfig;
	asset.tracks.push_back(spawn);

	Vans::VansTimelineValidationContext editorContext;
	editorContext.runtimeValidation = false;
	const auto editorDiagnostics = Vans::VansTimelineValidator::Validate(asset, editorContext);
	if (!Expect(!Vans::VansTimelineValidator::HasErrors(editorDiagnostics),
		"Data-and-Editor Timeline capabilities could not be authored and saved"))
		return false;
	const auto runtimeDiagnostics = Vans::VansTimelineValidator::Validate(asset);
	if (!Expect(Vans::VansTimelineValidator::HasErrors(runtimeDiagnostics),
		"Runtime validation enabled unsupported Timeline capabilities"))
		return false;

	Vans::VansTimelineAsset pathOnly;
	pathOnly.durationTicks = 1000;
	pathOnly.playbackRange = { 0, 1000 };
	pathOnly.workRange = { 0, 1000 };
	pathOnly.bindings.push_back(binding);
	Vans::VansTimelineTrack animation;
	animation.id = "animation";
	animation.type = Vans::VansTimelineTrackType::AnimationClip;
	animation.name = "Animation";
	animation.bindingId = binding.id;
	animation.config = Vans::VansTimelineAnimationTrackConfig{};
	Vans::VansTimelineSection section;
	section.id = "clip";
	section.durationTicks = 100;
	section.assetPath = "Assets/PathOnly.vclip";
	animation.sections.push_back(section);
	pathOnly.tracks.push_back(animation);
	return Expect(Vans::VansTimelineValidator::HasErrors(Vans::VansTimelineValidator::Validate(pathOnly)),
		"Runtime Timeline validation accepted an asset path fallback without an indexed GUID");
}

bool TestTimelineDependencyClosureContract()
{
	auto makeAsset = []
	{
		Vans::VansTimelineAsset value;
		value.durationTicks = 1000;
		value.playbackRange = { 0, 1000 };
		value.workRange = { 0, 1000 };
		return value;
	};
	Vans::VansTimelineAsset root = makeAsset();
	Vans::VansTimelineTrack nested;
	nested.id = "nested-track";
	nested.type = Vans::VansTimelineTrackType::SubTimeline;
	nested.name = "Nested";
	nested.config = Vans::VansTimelineSubTimelineTrackConfig{};
	Vans::VansTimelineSection nestedSection;
	nestedSection.id = "nested-section";
	nestedSection.durationTicks = 500;
	nestedSection.assetGuid = "child-guid";
	nested.sections.push_back(nestedSection);
	root.tracks.push_back(nested);

	Vans::VansTimelineAsset child = makeAsset();
	Vans::VansTimelineBinding audioBinding;
	audioBinding.id = "audio-binding";
	audioBinding.displayName = "Audio";
	audioBinding.targetGuid = "audio-entity";
	child.bindings.push_back(audioBinding);
	Vans::VansTimelineTrack audio;
	audio.id = "audio-track";
	audio.type = Vans::VansTimelineTrackType::Audio;
	audio.name = "Audio";
	audio.bindingId = audioBinding.id;
	audio.config = Vans::VansTimelineAudioTrackConfig{};
	Vans::VansTimelineSection audioSection;
	audioSection.id = "audio-section";
	audioSection.durationTicks = 500;
	audioSection.assetGuid = "audio-guid";
	audio.sections.push_back(audioSection);
	child.tracks.push_back(audio);

	auto loader = [&](const Vans::VansTimelineAssetReference& reference, Vans::VansTimelineAsset& loaded,
		std::string& identity, std::string& error)
	{
		if (reference.assetGuid == "child-guid") { loaded = child; identity = "child"; return true; }
		if (reference.assetGuid == "root-guid") { loaded = root; identity = "root"; return true; }
		error = "Unknown Timeline dependency";
		return false;
	};
	Vans::VansTimelineDependencyClosure closure;
	Vans::VansTimelineDiagnostics diagnostics;
	if (!Expect(Vans::VansTimelineDependencyBuilder::BuildClosure(root, loader, closure, diagnostics),
		"Timeline recursive dependency closure failed"))
		return false;
	const bool hasChild = std::any_of(closure.transitive.begin(), closure.transitive.end(), [](const auto& reference)
	{
		return reference.kind == Vans::VansTimelineAssetReferenceKind::Timeline && reference.assetGuid == "child-guid";
	});
	const bool hasAudio = std::any_of(closure.transitive.begin(), closure.transitive.end(), [](const auto& reference)
	{
		return reference.kind == Vans::VansTimelineAssetReferenceKind::Audio && reference.assetGuid == "audio-guid";
	});
	if (!Expect(hasChild && hasAudio, "Timeline dependency closure omitted nested Timeline assets"))
		return false;

	Vans::VansTimelineTrack cycle = nested;
	cycle.id = "cycle-track";
	cycle.sections.front().id = "cycle-section";
	cycle.sections.front().assetGuid = "root-guid";
	child.tracks.push_back(cycle);
	return Expect(!Vans::VansTimelineDependencyBuilder::BuildClosure(root, loader, closure, diagnostics),
		"Timeline dependency builder accepted a direct recursive cycle");
}

bool TestTimelineScenePackageDependencyContract()
{
	TemporaryDirectory temporary;
	const fs::path assetsRoot = temporary.path / "Assets";
	const fs::path audioRoot = assetsRoot / "Audio";
	const fs::path timelineRoot = assetsRoot / "Timelines";
	const fs::path scenesRoot = temporary.path / "Scenes";
	fs::create_directories(audioRoot);
	fs::create_directories(timelineRoot);
	fs::create_directories(scenesRoot);

	const fs::path audioPath = audioRoot / "TimelineProbe.wav";
	{
		std::ofstream audioFile(audioPath, std::ios::binary);
		audioFile << "timeline-audio-dependency-probe";
	}

	Vans::VansAssetDatabase database(assetsRoot, temporary.path / "Library" / "Artifacts");
	const auto firstScan = database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	if (!Expect(static_cast<bool>(firstScan), "Timeline package probe could not index its Audio asset"))
		return false;
	const auto audioRecord = database.Find(audioPath);
	if (!Expect(audioRecord && audioRecord->type == Vans::VansAssetType::Audio,
		"Timeline package probe Audio GUID was not indexed"))
		return false;

	auto makeTimeline = []
	{
		Vans::VansTimelineAsset value;
		value.durationTicks = 60000;
		value.playbackRange = { 0, 60000 };
		value.workRange = { 0, 60000 };
		return value;
	};

	Vans::VansTimelineAsset child = makeTimeline();
	Vans::VansTimelineBinding audioBinding;
	audioBinding.id = "audio-binding";
	audioBinding.displayName = "Audio Target";
	audioBinding.targetGuid = "timeline-audio-owner";
	child.bindings.push_back(audioBinding);
	Vans::VansTimelineTrack audioTrack;
	audioTrack.id = "audio-track";
	audioTrack.type = Vans::VansTimelineTrackType::Audio;
	audioTrack.name = "Audio";
	audioTrack.bindingId = audioBinding.id;
	audioTrack.config = Vans::VansTimelineAudioTrackConfig{};
	Vans::VansTimelineSection audioSection;
	audioSection.id = "audio-section";
	audioSection.durationTicks = 30000;
	audioSection.assetGuid = audioRecord->guid.ToString();
	audioSection.config = audioTrack.config;
	audioTrack.sections.push_back(std::move(audioSection));
	child.tracks.push_back(std::move(audioTrack));

	std::string error;
	const fs::path childPath = timelineRoot / "Child.vtimeline";
	if (!Expect(Vans::VansTimelineSerialization::SaveAtomic(childPath, child, error), error.c_str()))
		return false;
	if (!Expect(database.RegisterOrRefresh(childPath, Vans::VansAssetOperationPolicy::Authoring(), error),
		error.c_str()))
		return false;
	const auto childRecord = database.Find(childPath);
	if (!Expect(childRecord && childRecord->type == Vans::VansAssetType::Timeline,
		"Child Timeline was not indexed"))
		return false;

	Vans::VansTimelineAsset root = makeTimeline();
	Vans::VansTimelineTrack subTimelineTrack;
	subTimelineTrack.id = "subtimeline-track";
	subTimelineTrack.type = Vans::VansTimelineTrackType::SubTimeline;
	subTimelineTrack.name = "Nested Timeline";
	subTimelineTrack.config = Vans::VansTimelineSubTimelineTrackConfig{};
	Vans::VansTimelineSection subTimelineSection;
	subTimelineSection.id = "subtimeline-section";
	subTimelineSection.durationTicks = 60000;
	subTimelineSection.assetGuid = childRecord->guid.ToString();
	subTimelineSection.config = subTimelineTrack.config;
	subTimelineTrack.sections.push_back(std::move(subTimelineSection));
	root.tracks.push_back(std::move(subTimelineTrack));

	const fs::path rootPath = timelineRoot / "Root.vtimeline";
	if (!Expect(Vans::VansTimelineSerialization::SaveAtomic(rootPath, root, error), error.c_str()))
		return false;
	if (!Expect(database.RegisterOrRefresh(rootPath, Vans::VansAssetOperationPolicy::Authoring(), error),
		error.c_str()))
		return false;
	const auto rootRecord = database.Find(rootPath);
	if (!Expect(rootRecord && rootRecord->type == Vans::VansAssetType::Timeline,
		"Root Timeline was not indexed"))
		return false;

	const fs::path scenePath = scenesRoot / "TimelinePackageProbe.json";
	const nlohmann::json scene = {
		{ "schemaVersion", Vans::VansSceneSchemaVersion },
		{ "sceneGuid", "bb930f1e-1760-45f6-ab71-a2eaad20f799" },
		{ "settings", nlohmann::json::object() },
		{ "entities", nlohmann::json::array({ {
			{ "id", "673cc9db-c17f-4af6-ab11-87db9e6f923c" },
			{ "name", "Timeline Owner" },
			{ "parent", nullptr },
			{ "components", nlohmann::json::array({ {
				{ "id", "28e5060b-5630-458e-ac0b-786069ace2dd" },
				{ "type", "Transform" },
				{ "version", 1 },
				{ "enabled", true },
				{ "data", {
					{ "position", { 0.0, 0.0, 0.0 } },
					{ "rotation", { 0.0, 0.0, 0.0, 1.0 } },
					{ "scale", { 1.0, 1.0, 1.0 } }
				} }
			}, {
				{ "id", "bd5c2cf4-8119-4425-a6cd-c42eaf754e41" },
				{ "type", "Timeline" },
				{ "version", 1 },
				{ "enabled", true },
				{ "data", {
					{ "timeline", { { "guid", rootRecord->guid.ToString() } } }
				} }
			} }) }
		} }) }
	};
	{
		std::ofstream sceneFile(scenePath, std::ios::binary);
		sceneFile << scene.dump(2);
	}

	const Vans::VansSceneAssetDependencyBuildResult dependencies =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(database, scenePath, {});
	if (!Expect(dependencies.success,
		dependencies.errors.empty() ? "Timeline scene package dependency closure failed"
			: dependencies.errors.front().c_str()))
		return false;
	if (!Expect(dependencies.requiredAssets.find(rootRecord->guid.ToString()) != dependencies.requiredAssets.end()
		&& dependencies.requiredAssets.find(childRecord->guid.ToString()) != dependencies.requiredAssets.end()
		&& dependencies.requiredAssets.find(audioRecord->guid.ToString()) != dependencies.requiredAssets.end(),
		"Timeline scene package dependency closure omitted a transitive asset"))
		return false;
	return Expect(std::any_of(dependencies.resourcePlan.audios.begin(), dependencies.resourcePlan.audios.end(),
		[&](const Vans::VansSceneAudioResourceRequest& request)
		{
			return request.assetGuid == audioRecord->guid.ToString();
		}), "Timeline Audio dependency was not projected into the packaged resource plan");
}

bool TestTimelinePreAnimatedStackContract()
{
	int value = 0;
	Vans::VansTimelinePreAnimatedState state;
	state.Capture("writer-a", "entity:property", [&] { value = 0; });
	value = 10;
	state.Capture("writer-b", "entity:property", [&] { value = 10; });
	value = 20;
	state.ReleaseWriter("writer-a");
	if (!Expect(value == 20, "Timeline released a covered pre-animated writer too early"))
		return false;
	state.ReleaseWriter("writer-b");
	if (!Expect(value == 0 && state.PropertyCount() == 0,
		"Timeline pre-animated writer stack did not restore the original value"))
		return false;

	state.Capture("writer-a", "entity:property", [&] { value = 0; });
	value = 10;
	state.Capture("writer-b", "entity:property", [&] { value = 10; });
	value = 20;
	state.ReleaseWriter("writer-b");
	if (!Expect(value == 10, "Timeline pre-animated stack did not reveal the previous writer"))
		return false;
	state.ReleaseWriter("writer-a");
	return Expect(value == 0, "Timeline pre-animated stack did not restore after the final writer");
}

bool TestTimelineIsolatedPreviewOwnerContract()
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 1000;
	asset.playbackRange = { 0, 1000 };
	asset.workRange = { 0, 1000 };
	const auto compiled = Vans::VansTimelineCompiler::Compile(asset);
	if (!Expect(static_cast<bool>(compiled), "Timeline isolated preview probe did not compile"))
		return false;

	Vans::VansRuntimeWorld world;
	Vans::VansTimelineRuntimeSystem runtime;
	runtime.RegisterWorld(&world);
	std::string error;
	if (!Expect(runtime.StartPreview("offline-preview-owner", compiled.timeline, {}, false, false, error), error.c_str()))
		return false;
	if (!Expect(world.Entities().CollectAliveEntities().size() == 1,
		"Timeline offline preview did not create exactly one isolated owner"))
		return false;
	if (!Expect(runtime.ConfigurePreview("offline-preview-owner", 1.0, -1, true),
		"Timeline preview rejected reverse loop playback settings"))
		return false;
	if (!Expect(runtime.SeekPreview("offline-preview-owner", 100,
		Vans::VansTimelineSeekPolicy::ContinuousOnly) && runtime.PlayPreview("offline-preview-owner"),
		"Timeline reverse preview could not seek and play"))
		return false;
	runtime.UpdateRuntimePostScript(0.005);
	Vans::VansTimelinePlayer* reversePreview = runtime.FindPreview("offline-preview-owner");
	if (!Expect(reversePreview && reversePreview->CurrentTick() == 100,
		"Runtime Timeline update advanced an editor-only preview"))
		return false;
	runtime.UpdatePreviewsPostScript(0.005);
	runtime.UpdatePreviewsPostScript(0.005);
	if (!Expect(reversePreview && reversePreview->State() == Vans::VansTimelinePlayerState::Playing &&
		reversePreview->CurrentTick() > 100,
		"Timeline reverse preview did not loop across the playback range"))
		return false;
	if (!Expect(runtime.StopPreview("offline-preview-owner") && world.Entities().CollectAliveEntities().empty(),
		"Timeline offline preview owner was not cleaned up on stop"))
		return false;

	const Vans::VansEntityHandle explicitOwner = world.CreateEntity({ "explicit-preview-owner", "Explicit Preview Owner" });
	if (!Expect(runtime.StartPreview("instance-preview", compiled.timeline, explicitOwner, false, false, error), error.c_str()))
		return false;
	if (!Expect(runtime.StopPreview("instance-preview") && world.IsAlive(explicitOwner),
		"Timeline instance preview destroyed its explicit scene owner"))
		return false;
	Vans::VansRuntimeTimelineComponent component;
	component.instance.updateMode = Vans::VansTimelineUpdateMode::Manual;
	Vans::VansTimelinePlayer reloadPlayer;
	if (!Expect(reloadPlayer.Load(compiled.timeline, component, &world, explicitOwner,
		"timeline-reload-contract", error), error.c_str()))
		return false;
	reloadPlayer.SeekTicks(500, Vans::VansTimelineSeekPolicy::ContinuousOnly,
		Vans::VansTimelineEvaluationReason::Jump);
	reloadPlayer.Play();
	Vans::VansTimelineAsset replacementAsset = asset;
	replacementAsset.durationTicks = 300;
	replacementAsset.playbackRange = { 100, 300 };
	replacementAsset.workRange = { 100, 300 };
	const auto replacement = Vans::VansTimelineCompiler::Compile(replacementAsset);
	if (!Expect(static_cast<bool>(replacement) &&
		reloadPlayer.Reload(replacement.timeline, error) && reloadPlayer.CurrentTick() == 200 &&
		reloadPlayer.State() == Vans::VansTimelinePlayerState::Playing,
		"Timeline hot reload did not preserve normalized time and playback state"))
		return false;
	if (!Expect(!reloadPlayer.Reload({}, error) && reloadPlayer.CurrentTick() == 200,
		"Timeline hot reload failure did not keep the last valid compiled asset"))
		return false;
	world.DestroyEntity(explicitOwner);
	return true;
}

bool TestAnimationV2TimelineShowcaseContract()
{
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 5 && !fs::exists(workspace / "AnimationV2Project"); ++depth)
	{
		if (!workspace.has_parent_path() || workspace.parent_path() == workspace) break;
		workspace = workspace.parent_path();
	}
	const fs::path projectRoot = workspace / "AnimationV2Project";
	if (!fs::exists(projectRoot)) return true;

	const fs::path timelinePath = projectRoot / "Assets" / "Cinematics" / "AnimationV2Showcase.vtimeline";
	const fs::path scenePath = projectRoot / "Scenes" / "MainScene.json";
	Vans::VansTimelineAsset timeline;
	std::string error;
	if (!Expect(Vans::VansTimelineSerialization::Load(timelinePath, timeline, error), error.c_str()))
		return false;
	const auto cameraCutTrack = std::find_if(timeline.tracks.begin(), timeline.tracks.end(), [](const auto& track)
	{
		return track.id == "track-camera-cut";
	});
	if (!Expect(cameraCutTrack != timeline.tracks.end(), "AnimationV2 Timeline fixture is missing its CameraCut track"))
		return false;
	const auto* cameraCutConfig = std::get_if<Vans::VansTimelineCameraCutTrackConfig>(&cameraCutTrack->config);
	if (!Expect(cameraCutConfig && cameraCutConfig->cameraBindingId == "binding-cinematic-camera" &&
		cameraCutConfig->targetCameraBindingId == "binding-main-camera",
		"AnimationV2 CameraCut must transfer a virtual camera into MainCamera"))
		return false;
	for (const auto& section : cameraCutTrack->sections)
	{
		const auto* sectionConfig = std::get_if<Vans::VansTimelineCameraCutTrackConfig>(
			std::holds_alternative<std::monostate>(section.config) ? &cameraCutTrack->config : &section.config);
		if (!Expect(sectionConfig && sectionConfig->cameraBindingId == "binding-cinematic-camera" &&
			sectionConfig->targetCameraBindingId == "binding-main-camera",
			"AnimationV2 CameraCut section is not sourced from the virtual camera and targeted at MainCamera"))
			return false;
	}

	Vans::VansAssetGuid timelineGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"ec3c5abd-8482-4906-b82b-8111922abb18", timelineGuid),
		"AnimationV2 Timeline fixture GUID is invalid"))
		return false;
	Vans::VansAssetRecord editorTimelineRecord;
	editorTimelineRecord.guid = timelineGuid;
	editorTimelineRecord.type = Vans::VansAssetType::Timeline;
	editorTimelineRecord.sourcePath = timelinePath;
	const std::vector<Vans::VansAssetRecord> editorTimelineRecords{ editorTimelineRecord };
	const Vans::VansResolvedAsset editorTimeline = Vans::VansAssetResolver(
		Vans::VansAssetAccessMode::Editor, editorTimelineRecords).Resolve(
			timelineGuid.ToString(), Vans::VansAssetType::Timeline);
	if (!Expect(editorTimeline.valid && editorTimeline.readPath == timelinePath,
		"AnimationV2 Editor Play Timeline incorrectly required a packaged cache artifact"))
		return false;
	if (!Expect(!Vans::VansAssetResolver(
		Vans::VansAssetAccessMode::Package, editorTimelineRecords).Resolve(
			timelineGuid.ToString(), Vans::VansAssetType::Timeline).valid,
		"AnimationV2 packaged Timeline accepted a source-only asset record"))
		return false;

	std::unordered_map<std::string, fs::path> timelinePaths;
	std::error_code iteratorError;
	for (fs::recursive_directory_iterator iterator(projectRoot / "Assets", iteratorError), end;
		!iteratorError && iterator != end; iterator.increment(iteratorError))
	{
		if (!iterator->is_regular_file() || iterator->path().extension() != ".meta") continue;
		const std::string filename = iterator->path().filename().string();
		if (filename.size() <= 15 || filename.substr(filename.size() - 15) != ".vtimeline.meta") continue;
		std::ifstream metaFile(iterator->path());
		nlohmann::json meta;
		try { metaFile >> meta; }
		catch (...) { continue; }
		const std::string guid = meta.value("guid", "");
		if (guid.empty()) continue;
		fs::path source = iterator->path();
		source.replace_extension();
		timelinePaths.emplace(guid, std::move(source));
	}

	Vans::VansTimelineCompileOptions options;
	options.validation.supportsPropertyDescriptor = [](std::uint16_t, const std::string&,
		Vans::VansTimelineChannelType) { return true; };
	options.dependencyLoader = [&](const Vans::VansTimelineAssetReference& reference,
		Vans::VansTimelineAsset& nested, std::string& identity, std::string& nestedError)
	{
		const auto found = timelinePaths.find(reference.assetGuid);
		if (found == timelinePaths.end())
		{
			nestedError = "AnimationV2 SubTimeline GUID is not indexed";
			return false;
		}
		identity = reference.assetGuid;
		return Vans::VansTimelineSerialization::Load(found->second, nested, nestedError);
	};
	const Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(timeline, options);
	if (!compiled)
	{
		std::string diagnostics;
		for (const auto& diagnostic : compiled.diagnostics)
		{
			if (!diagnostics.empty()) diagnostics += " | ";
			diagnostics += diagnostic.objectId + ":" + diagnostic.propertyPath + ":" + diagnostic.message;
		}
		return Expect(false, diagnostics.c_str());
	}

	std::ifstream sceneFile(scenePath);
	nlohmann::json sceneJson;
	try { sceneFile >> sceneJson; }
	catch (const std::exception& exception) { return Expect(false, exception.what()); }
	std::vector<std::string> realCameraEntities;
	bool virtualCameraFound = false;
	bool virtualCameraHasTransform = false;
	bool virtualCameraHasCamera = false;
	std::function<void(const nlohmann::json&)> inspectSceneNode = [&](const nlohmann::json& node)
	{
		if (node.is_array())
		{
			for (const auto& item : node) inspectSceneNode(item);
			return;
		}
		if (!node.is_object()) return;
		const std::string name = node.value("name", "");
		const auto components = node.find("components");
		if (!name.empty() && components != node.end() && components->is_array())
		{
			bool hasCamera = false;
			bool hasTransform = false;
			for (const auto& component : *components)
			{
				const std::string type = component.value("type", "");
				hasCamera = hasCamera || type == "Camera";
				hasTransform = hasTransform || type == "Transform";
			}
			if (hasCamera) realCameraEntities.push_back(name);
			if (name == "TimelineVirtualCamera")
			{
				virtualCameraFound = true;
				virtualCameraHasTransform = hasTransform;
				virtualCameraHasCamera = hasCamera;
			}
		}
		for (const auto& item : node.items())
			inspectSceneNode(item.value());
	};
	inspectSceneNode(sceneJson);
	if (!Expect(realCameraEntities.size() == 1 && realCameraEntities.front() == "MainCamera",
		"AnimationV2 Timeline fixture must keep MainCamera as the only real Camera"))
		return false;
	if (!Expect(virtualCameraFound && virtualCameraHasTransform && !virtualCameraHasCamera,
		"AnimationV2 Timeline virtual camera must be a Transform-only proxy"))
		return false;
	Vans::VansSceneContentBuildPlan plan;
	if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
		Vans::DecodeSerializedValueJson(sceneJson), projectRoot.string(), plan, error), error.c_str()))
		return false;
	const auto object = std::find_if(plan.objects.objects.begin(), plan.objects.objects.end(),
		[](const auto& candidate) { return candidate.entityGuid == "328c9fc0-940b-42df-b5fb-0f2e910b348e"; });
	if (!Expect(object != plan.objects.objects.end() && object->timeline && object->timeline->valid,
		"AnimationV2 MainCamera Timeline component was not projected"))
		return false;
	const auto componentGuid = object->componentGuids.find("timeline");
	if (!Expect(componentGuid != object->componentGuids.end() &&
		componentGuid->second == "422a3285-d3c8-4c55-9313-67787ea9b4c0" &&
		object->timeline->timelineAssetGuid == "ec3c5abd-8482-4906-b82b-8111922abb18",
		"AnimationV2 Timeline stable component or asset GUID was not projected canonically"))
		return false;

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle owner = world.CreateEntity(
		{ object->entityGuid, object->name, {}, true });
	Vans::VansRuntimeTimelineComponent component;
	component.assetGuid = object->timeline->timelineAssetGuid;
	component.instance = object->timeline->instance;
	const Vans::VansComponentHandle handle = world.AddComponent(owner,
		Vans::VansRuntimeComponentType_Timeline, component, componentGuid->second, true);
	Vans::VansTimelineRuntimeSystem runtime;
	runtime.RegisterWorld(&world);
	runtime.SetAssetLoader([&](const Vans::VansRuntimeTimelineComponent& requested,
		std::shared_ptr<const Vans::VansCompiledTimeline>& loaded, std::string& loadError)
	{
		if (requested.assetGuid != component.assetGuid)
		{
			loadError = "Unexpected AnimationV2 Timeline asset GUID";
			return false;
		}
		loaded = compiled.timeline;
		return true;
	});
	return Expect(world.FindComponentByGuid(componentGuid->second,
		Vans::VansRuntimeComponentType_Timeline) == handle && runtime.PlayComponent(handle, true),
		"AnimationV2 Timeline could not be resolved and started by stable component GUID");
}

bool TestTimelineEditorTransactionContract()
{
	TemporaryDirectory temporary;
	const fs::path path = temporary.path / "Transaction.vtimeline";
	Vans::VansTimelineAsset initial;
	initial.durationTicks = 1000;
	initial.playbackRange = { 0, 1000 };
	initial.workRange = { 0, 1000 };
	std::string error;
	if (!Expect(Vans::VansTimelineSerialization::SaveAtomic(path, initial, error), error.c_str()))
		return false;
	Vans::VansAssetDocumentRegistry::Get().Clear();
	Vans::VansTimelineEditService edit;
	if (!Expect(static_cast<bool>(edit.Open(path)), "Timeline edit service could not open a canonical asset"))
		return false;
	Vans::VansTimelineBinding binding;
	binding.id = "edit-binding";
	binding.displayName = "Edit Binding";
	binding.targetGuid = "edit-entity";
	if (!Expect(static_cast<bool>(edit.AddBinding(binding)), "Timeline edit service could not add a binding"))
		return false;
	const auto trackResult = edit.AddTrack(Vans::VansTimelineTrackType::Transform, binding.id);
	if (!Expect(static_cast<bool>(trackResult), "Timeline edit service could not add a Transform track"))
		return false;
	Vans::VansTimelineSection section;
	section.id = "edit-section";
	section.startTick = 100;
	section.durationTicks = 400;
	if (!Expect(static_cast<bool>(edit.AddSection(trackResult.objectId, section)), "Timeline edit service could not add a section"))
		return false;
	Vans::VansTimelineKey key;
	key.id = "edit-key";
	key.tick = 10;
	key.value = Vans::VansTimelineVec3{ { 1.0, 2.0, 3.0 } };
	if (!Expect(static_cast<bool>(edit.AddKey(trackResult.objectId, section.id, 0, key)), "Timeline edit service could not add a key"))
		return false;

	const auto beforeInteraction = edit.Document()->sourceDocument.CurrentStateId();
	if (!Expect(static_cast<bool>(edit.BeginInteraction()), "Timeline interaction did not begin"))
		return false;
	if (!Expect(edit.MoveSection(trackResult.objectId, section.id, 200) &&
		edit.MoveSection(trackResult.objectId, section.id, 300), "Timeline drag mutation failed"))
		return false;
	if (!Expect(edit.Document()->sourceDocument.CurrentStateId() == beforeInteraction,
		"Timeline drag added undo history before pointer release"))
		return false;
	if (!Expect(static_cast<bool>(edit.CommitInteraction()), "Timeline interaction did not commit"))
		return false;
	const auto afterInteraction = edit.Document()->sourceDocument.CurrentStateId();
	if (!Expect(afterInteraction == beforeInteraction + 1,
		"Timeline drag did not commit as exactly one undoable transaction"))
		return false;
	if (!Expect(edit.Undo() && edit.Asset().tracks.front().sections.front().startTick == 100,
		"Timeline Undo did not restore the pre-drag section"))
		return false;
	if (!Expect(edit.Redo() && edit.Asset().tracks.front().sections.front().startTick == 300,
		"Timeline Redo did not restore the committed drag"))
		return false;

	if (!Expect(edit.BeginInteraction() && edit.MoveKey(trackResult.objectId, section.id, 0, key.id, 99) &&
		edit.CancelInteraction(), "Timeline cancelled interaction failed"))
		return false;
	if (!Expect(edit.Asset().tracks.front().sections.front().channels.front().keys.front().tick == 10,
		"Timeline cancelled interaction changed author data"))
		return false;
	if (!Expect(static_cast<bool>(edit.SetKeyValue(trackResult.objectId, section.id, 0, key.id,
		Vans::VansTimelineVec3{ { 9.0, 8.0, 7.0 } })), "Timeline Auto Key value edit failed"))
		return false;
	const auto* editedValue = std::get_if<Vans::VansTimelineVec3>(
		&edit.Asset().tracks.front().sections.front().channels.front().keys.front().value);
	if (!Expect(editedValue && editedValue->value[0] == 9.0,
		"Timeline Auto Key value edit did not update the selected key"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Auto Key value edit was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.SetKeyCurve(trackResult.objectId, section.id, 0, key.id,
		Vans::VansTimelineInterpolation::Cubic, Vans::VansTimelineTangentMode::Weighted,
		0.25, 0.5, 1.5, 2.0)), "Timeline curve tangent edit failed"))
		return false;
	const auto& curvedKey = edit.Asset().tracks.front().sections.front().channels.front().keys.front();
	if (!Expect(curvedKey.interpolation == Vans::VansTimelineInterpolation::Cubic &&
		curvedKey.tangentMode == Vans::VansTimelineTangentMode::Weighted &&
		std::abs(curvedKey.leaveTangent - 0.5) < 0.0001,
		"Timeline curve tangent edit did not persist typed curve data"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline curve tangent edit was not undoable"))
		return false;
	Vans::VansTimelineKey duplicate = key;
	duplicate.id = "duplicate-key";
	if (!Expect(!edit.AddKey(trackResult.objectId, section.id, 0, duplicate),
		"Timeline edit service accepted two keys at the same tick"))
		return false;
	if (!Expect(edit.Asset().tracks.front().sections.front().channels.front().keys.size() == 1,
		"Timeline failed mutation was not rolled back"))
		return false;
	Vans::VansTimelineKey secondKey = key;
	secondKey.id = "edit-key-second";
	secondKey.tick = 20;
	if (!Expect(static_cast<bool>(edit.AddKey(trackResult.objectId, section.id, 0, secondKey)),
		"Timeline edit service could not add a second selected key"))
		return false;
	const std::unordered_set<Vans::VansTimelineId> selectedKeys{ key.id, secondKey.id };
	if (!Expect(static_cast<bool>(edit.MoveKeysBy(selectedKeys, 5)),
		"Timeline multi-key time move failed"))
		return false;
	const auto& movedKeys = edit.Asset().tracks.front().sections.front().channels.front().keys;
	if (!Expect(movedKeys.size() == 2 && movedKeys[0].tick == 15 && movedKeys[1].tick == 25,
		"Timeline multi-key move did not preserve relative timing"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline multi-key move was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.DuplicateKeys(selectedKeys, 30)) &&
		edit.Asset().tracks.front().sections.front().channels.front().keys.size() == 4,
		"Timeline multi-key duplicate did not create canonical key copies"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline multi-key duplicate was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.ScaleKeys(selectedKeys, 2.0, 1.0)),
		"Timeline multi-key scale failed"))
		return false;
	const auto& scaledKeys = edit.Asset().tracks.front().sections.front().channels.front().keys;
	if (!Expect(scaledKeys.size() == 2 && scaledKeys[0].tick == 5 && scaledKeys[1].tick == 25,
		"Timeline multi-key scale did not preserve its selection-center pivot"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline multi-key scale was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.SetKeysCurveMode(selectedKeys,
		Vans::VansTimelineInterpolation::Bezier, Vans::VansTimelineTangentMode::Weighted)),
		"Timeline batch interpolation/tangent edit failed"))
		return false;
	const auto& batchCurveKeys = edit.Asset().tracks.front().sections.front().channels.front().keys;
	if (!Expect(std::all_of(batchCurveKeys.begin(), batchCurveKeys.end(), [](const auto& item)
	{
		return item.interpolation == Vans::VansTimelineInterpolation::Bezier &&
			item.tangentMode == Vans::VansTimelineTangentMode::Weighted;
	}), "Timeline batch interpolation/tangent edit did not update every selected key"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline batch interpolation/tangent edit was not undoable"))
		return false;
	auto bufferedKeys = edit.Asset().tracks.front().sections.front().channels.front().keys;
	bufferedKeys.front().tick = 30;
	if (!Expect(static_cast<bool>(edit.ReplaceChannelKeys(
		trackResult.objectId, section.id, 0, bufferedKeys)),
		"Timeline buffered curve restore failed"))
		return false;
	if (!Expect(edit.Asset().tracks.front().sections.front().channels.front().keys.front().tick == 20,
		"Timeline buffered curve restore was not normalized by time"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()) &&
		edit.Asset().tracks.front().sections.front().channels.front().keys.front().tick == 10,
		"Timeline buffered curve restore was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.SlipSection(trackResult.objectId, section.id, 25)) &&
		edit.Asset().tracks.front().sections.front().sourceInTick == 25,
		"Timeline Slip edit did not offset source time"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Slip edit was not undoable"))
		return false;
	Vans::VansTimelineSection trailing;
	trailing.id = "edit-section-trailing";
	trailing.startTick = 800;
	trailing.durationTicks = 100;
	if (!Expect(static_cast<bool>(edit.AddSection(trackResult.objectId, trailing)),
		"Timeline edit service could not add a trailing section"))
		return false;
	if (!Expect(static_cast<bool>(edit.BeginInteraction()) &&
		edit.ScaleSection(trackResult.objectId, section.id, 600) && edit.CommitInteraction(),
		"Timeline Scale interaction failed"))
		return false;
	const auto& scaled = edit.Asset().tracks.front().sections.front();
	if (!Expect(scaled.durationTicks == 600 && scaled.channels.front().keys.front().tick == 15,
		"Timeline Scale did not scale section keys from the interaction baseline"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Scale was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.LoopExtendSection(trackResult.objectId, section.id, 600)) &&
		edit.Asset().tracks.front().sections.front().loopMode == Vans::VansTimelineLoopMode::Loop &&
		edit.Asset().tracks.front().sections.front().loopCount == 2,
		"Timeline Loop Extend did not author a finite loop range"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Loop Extend was not undoable"))
		return false;
	const std::unordered_set<Vans::VansTimelineId> multiSections{ section.id, trailing.id };
	if (!Expect(static_cast<bool>(edit.MoveSectionsBy(multiSections, -50)),
		"Timeline multi-section move failed"))
		return false;
	if (!Expect(edit.Asset().tracks.front().sections[0].startTick == 250 &&
		edit.Asset().tracks.front().sections[1].startTick == 750,
		"Timeline multi-section move did not preserve relative offsets"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline multi-section move was not undoable"))
		return false;
	Vans::VansTimelineGroup group;
	group.name = "Contract Group";
	const auto groupResult = edit.AddGroup(group);
	if (!Expect(static_cast<bool>(groupResult) &&
		static_cast<bool>(edit.MoveTrack(trackResult.objectId, groupResult.objectId)) &&
		edit.Asset().tracks.front().groupId == groupResult.objectId,
		"Timeline Outliner group move failed"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Outliner group move was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.RippleMoveSection(trackResult.objectId, section.id, 200)),
		"Timeline Ripple Move failed"))
		return false;
	const auto& rippleSections = edit.Asset().tracks.front().sections;
	const auto movedTrailing = std::find_if(rippleSections.begin(), rippleSections.end(), [](const auto& item)
	{
		return item.id == "edit-section-trailing";
	});
	if (!Expect(movedTrailing != rippleSections.end() && movedTrailing->startTick == 700,
		"Timeline Ripple Move did not shift following sections"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Ripple Move was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.SplitSection(trackResult.objectId, section.id, 500)) &&
		edit.Asset().tracks.front().sections.size() == 3,
		"Timeline Split did not create a second canonical section"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Split was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.DuplicateSection(trackResult.objectId, section.id, 50)) &&
		edit.Asset().tracks.front().sections.size() == 3,
		"Timeline Duplicate did not regenerate section identities"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline Duplicate was not undoable"))
		return false;
	Vans::VansTimelineMarker marker;
	marker.id = "edit-marker";
	marker.tick = 250;
	marker.label = "Contract Marker";
	if (!Expect(static_cast<bool>(edit.AddMarker(marker)) &&
		static_cast<bool>(edit.MoveMarker(marker.id, 375)) &&
		edit.Asset().markers.size() == 1 && edit.Asset().markers.front().tick == 375,
		"Timeline Marker create and move transaction failed"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()) && edit.Asset().markers.front().tick == 250,
		"Timeline Marker move was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.RemoveObject(marker.id)) && edit.Asset().markers.empty(),
		"Timeline Marker delete failed"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()) && edit.Asset().markers.size() == 1,
		"Timeline Marker delete was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.SetPlaybackRange(50, 900)) &&
		edit.Asset().playbackRange.startTick == 50 && edit.Asset().playbackRange.endTick == 900,
		"Timeline playback range command did not commit canonical author data"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()) && edit.Asset().playbackRange.startTick == 0 &&
		edit.Asset().playbackRange.endTick == 1000,
		"Timeline playback range command was not undoable"))
		return false;
	if (!Expect(static_cast<bool>(edit.RenameObject(trackResult.objectId, "Renamed Transform")) &&
		edit.Asset().tracks.front().name == "Renamed Transform",
		"Timeline rename command did not update the selected object"))
		return false;
	if (!Expect(static_cast<bool>(edit.Undo()), "Timeline rename command was not undoable"))
		return false;
	Vans::VansTimelineCommandMap commandMap;
	if (!Expect(commandMap.GetBinding(Vans::VansTimelineCommand::Rename).chord.key == ImGuiKey_F2 &&
		commandMap.GetBinding(Vans::VansTimelineCommand::SetPlaybackStart).chord.key == ImGuiKey_LeftBracket &&
		commandMap.GetBinding(Vans::VansTimelineCommand::SetSelectionEnd).chord.key == ImGuiKey_O,
		"Timeline Editor command map does not expose the documented default commands"))
		return false;
	if (!Expect(edit.RevertToSaved() && edit.Asset().tracks.empty() && edit.Asset().bindings.empty(),
		"Timeline Discard did not restore the last saved asset state"))
		return false;
	Vans::VansAssetDocumentRegistry::Get().Clear();
	return true;
}

bool TestTimelineEditorStateAndRecoveryContract()
{
	TemporaryDirectory temporary;
	const fs::path sourcePath = temporary.path / "Recovery.vtimeline";
	Vans::VansTimelineAsset source;
	source.durationTicks = 1000;
	source.playbackRange = { 0, 1000 };
	source.workRange = { 0, 1000 };
	std::string error;
	if (!Expect(Vans::VansTimelineSerialization::SaveAtomic(sourcePath, source, error), error.c_str()))
		return false;
	Vans::VansTimelineEditorUserState saved;
	saved.playhead = 321;
	saved.viewStart = -200;
	saved.pixelsPerTick = 0.75;
	saved.snapKeys = false;
	saved.snapRanges = false;
	saved.autoKeyMode = 2;
	saved.timeDisplayMode = 2;
	saved.reversePlayback = true;
	saved.loopPlaybackRange = true;
	saved.rowHeight = 38.0f;
	saved.curveValueZoom = 2.5;
	saved.curveValuePan = -3.0;
	saved.showWaveforms = false;
	saved.showThumbnails = false;
	saved.search = "camera";
	saved.mutedTracks.insert("muted-track");
	saved.pinnedTracks.insert("pinned-track");
	if (!Expect(Vans::VansTimelineEditorStateStore::SaveUserState(sourcePath, saved, error), error.c_str()))
		return false;
	Vans::VansTimelineEditorUserState loaded;
	if (!Expect(Vans::VansTimelineEditorStateStore::LoadUserState(sourcePath, loaded, error), error.c_str()))
		return false;
	if (!Expect(loaded.playhead == 321 && loaded.viewStart == -200 &&
		std::abs(loaded.pixelsPerTick - 0.75) < 0.0001 && !loaded.snapKeys && !loaded.snapRanges &&
		loaded.autoKeyMode == 2 && loaded.timeDisplayMode == 2 && loaded.search == "camera" &&
		loaded.reversePlayback && loaded.loopPlaybackRange && !loaded.showWaveforms && !loaded.showThumbnails &&
		std::abs(loaded.rowHeight - 38.0f) < 0.001f &&
		std::abs(loaded.curveValueZoom - 2.5) < 0.0001 && std::abs(loaded.curveValuePan + 3.0) < 0.0001 &&
		loaded.mutedTracks.find("muted-track") != loaded.mutedTracks.end() &&
		loaded.pinnedTracks.find("pinned-track") != loaded.pinnedTracks.end(),
		"Timeline Editor user state did not roundtrip outside the author asset"))
		return false;

	fs::last_write_time(sourcePath,
		fs::file_time_type::clock::now() - std::chrono::seconds(2));
	Vans::VansTimelineAsset recovery = source;
	recovery.metadata.displayName = "Recovered";
	if (!Expect(Vans::VansTimelineEditorStateStore::SaveRecovery(sourcePath, recovery, error), error.c_str()))
		return false;
	Vans::VansTimelineAsset recovered;
	bool available = false;
	if (!Expect(Vans::VansTimelineEditorStateStore::LoadRecoveryIfNewer(
		sourcePath, recovered, available, error), error.c_str()))
		return false;
	const bool valid = available && recovered.metadata.displayName == "Recovered";
	Vans::VansTimelineEditorStateStore::RemoveRecovery(sourcePath);
	Vans::VansTimelineEditorStateStore::RemoveUserState(sourcePath);
	return Expect(valid, "Timeline Editor did not expose a newer recovery snapshot");
}

bool TestTimelineTimeContract()
{
	const Vans::VansTimelineTimebase ntsc{ 60000, 30000, 1001 };
	const auto oneMinute = Vans::VansTimelineTime::FrameToTick(1798, ntsc);
	if (!Expect(Vans::VansTimelineTime::FormatTimecode(oneMinute, ntsc, true) == "00:00:59;28",
		"Timeline 29.97 drop-frame timecode conversion changed"))
		return false;
	const auto pingPong = Vans::VansTimelineSectionTimeMapper::Map(
		150, 0, 400, 10, 110, 1.0, false, Vans::VansTimelineLoopMode::PingPong, 4);
	if (!Expect(pingPong.active && pingPong.loopIndex == 1 && pingPong.reversed && pingPong.localTick == 59,
		"Timeline ping-pong section mapping produced the wrong local tick"))
		return false;
	const auto reverse = Vans::VansTimelineSectionTimeMapper::Map(
		0, 0, 100, 10, 110, 1.0, true, Vans::VansTimelineLoopMode::None, 1);
	return Expect(reverse.active && reverse.reversed && reverse.localTick == 109,
		"Timeline reverse section mapping did not start at the source-out boundary");
}

bool TestTimelineWaveformContract()
{
	TemporaryDirectory temporary;
	const fs::path path = temporary.path / "TimelineWaveform.wav";
	constexpr std::uint32_t sampleRate = 8000;
	constexpr std::uint32_t sampleCount = 800;
	constexpr std::uint32_t dataBytes = sampleCount * 2;
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!Expect(static_cast<bool>(stream), "Timeline waveform fixture could not be created")) return false;
	const auto writeU16 = [&](std::uint16_t value)
	{
		const char bytes[2]{ static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu) };
		stream.write(bytes, sizeof(bytes));
	};
	const auto writeU32 = [&](std::uint32_t value)
	{
		const char bytes[4]{ static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu),
			static_cast<char>((value >> 16u) & 0xffu), static_cast<char>((value >> 24u) & 0xffu) };
		stream.write(bytes, sizeof(bytes));
	};
	stream.write("RIFF", 4); writeU32(36 + dataBytes); stream.write("WAVE", 4);
	stream.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1); writeU32(sampleRate);
	writeU32(sampleRate * 2); writeU16(2); writeU16(16);
	stream.write("data", 4); writeU32(dataBytes);
	for (std::uint32_t index = 0; index < sampleCount; ++index)
	{
		const double phase = static_cast<double>(index) * 440.0 * 6.283185307179586 / sampleRate;
		const auto sample = static_cast<std::int16_t>(std::sin(phase) * 28000.0);
		writeU16(static_cast<std::uint16_t>(sample));
	}
	stream.close();

	VansEngine::VansAudioWaveform waveform;
	std::string error;
	if (!Expect(VansEngine::VansAudioWaveformBuilder::Build(path, 64, waveform, error), error.c_str()))
		return false;
	const float minimum = *std::min_element(waveform.minima.begin(), waveform.minima.end());
	const float maximum = *std::max_element(waveform.maxima.begin(), waveform.maxima.end());
	return Expect(waveform.minima.size() == 64 && waveform.maxima.size() == 64 &&
		waveform.durationSeconds > 0.09 && minimum < -0.7f && maximum > 0.7f,
		"Timeline Audio waveform analysis did not preserve duration and signed peaks");
}

bool TestTimelineVideoThumbnailContract()
{
	TemporaryDirectory temporary;
	const fs::path path = temporary.path / "TimelineThumbnail.ppm";
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!Expect(static_cast<bool>(stream), "Timeline thumbnail fixture could not be created"))
		return false;
	stream << "P6\n16 9\n255\n";
	for (int y = 0; y < 9; ++y)
		for (int x = 0; x < 16; ++x)
		{
			const unsigned char pixel[3]{
				static_cast<unsigned char>(x < 8 ? 240 : 20),
				static_cast<unsigned char>(y < 5 ? 40 : 220),
				static_cast<unsigned char>(30)
			};
			stream.write(reinterpret_cast<const char*>(pixel), sizeof(pixel));
		}
	stream.close();

	VansGraphics::VansVideoThumbnail thumbnail;
	std::string error;
	if (!Expect(VansGraphics::VansVideoThumbnailBuilder::Build(
		path, 64, 36, thumbnail, error), error.c_str()))
		return false;
	if (!Expect(thumbnail.width == 64 && thumbnail.height == 36 &&
		thumbnail.rgba.size() == 64u * 36u * 4u,
		"Timeline video thumbnail did not preserve the requested aspect and RGBA payload"))
		return false;
	const std::size_t leftTop = 0;
	const std::size_t rightBottom = (static_cast<std::size_t>(35) * 64 + 63) * 4;
	return Expect(thumbnail.rgba[leftTop] > thumbnail.rgba[leftTop + 1] &&
		thumbnail.rgba[rightBottom + 1] > thumbnail.rgba[rightBottom],
		"Timeline video thumbnail did not decode source frame colors");
}

bool TestTimelineScaleContract()
{
	Vans::VansTimelineAsset asset;
	asset.durationTicks = 100;
	asset.playbackRange = { 0, 100 };
	asset.workRange = { 0, 100 };
	asset.bindings.push_back({ "perf-binding", "Performance Target",
		Vans::VansTimelineBindingKind::SceneEntity, "timeline-perf-entity" });
	asset.tracks.reserve(1000);
	for (int trackIndex = 0; trackIndex < 1000; ++trackIndex)
	{
		Vans::VansTimelineTrack track;
		track.id = "perf-track-" + std::to_string(trackIndex);
		track.type = Vans::VansTimelineTrackType::Transform;
		track.name = "Transform " + std::to_string(trackIndex);
		track.bindingId = "perf-binding";
		track.order = trackIndex;
		track.config = Vans::VansTimelineTransformTrackConfig{};
		Vans::VansTimelineSection section;
		section.id = "perf-section-" + std::to_string(trackIndex);
		section.name = track.name;
		section.durationTicks = 100;
		section.config = track.config;
		Vans::VansTimelineChannel channel;
		channel.id = "perf-channel-" + std::to_string(trackIndex);
		channel.name = "Position";
		channel.type = Vans::VansTimelineChannelType::Vec3;
		channel.keys.reserve(100);
		for (int keyIndex = 0; keyIndex < 100; ++keyIndex)
		{
			Vans::VansTimelineKey key;
			key.id = "perf-key-" + std::to_string(trackIndex) + "-" + std::to_string(keyIndex);
			key.tick = keyIndex;
			key.value = Vans::VansTimelineVec3{ { static_cast<double>(keyIndex),
				static_cast<double>(trackIndex), 0.0 } };
			key.interpolation = Vans::VansTimelineInterpolation::Linear;
			channel.keys.push_back(std::move(key));
		}
		section.channels.push_back(std::move(channel));
		track.sections.push_back(std::move(section));
		asset.tracks.push_back(std::move(track));
	}

	const auto compileStart = std::chrono::steady_clock::now();
	Vans::VansTimelineCompileResult compiled = Vans::VansTimelineCompiler::Compile(asset, {});
	const double compileMilliseconds = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - compileStart).count();
	if (!Expect(static_cast<bool>(compiled), compiled.diagnostics.empty()
		? "Timeline 100000-Key asset did not compile" : compiled.diagnostics.front().message.c_str()))
		return false;

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle owner = world.CreateEntity({ "timeline-perf-entity", "Timeline Performance" });
	Vans::VansRuntimeTimelineComponent component;
	component.instance.updateMode = Vans::VansTimelineUpdateMode::Manual;
	Vans::VansTimelinePlayer player;
	std::string error;
	if (!Expect(player.Load(compiled.timeline, component, &world, owner, "timeline-performance", error), error.c_str()))
		return false;
	player.Play();
	std::vector<Vans::VansTimelineEvaluationOutput> outputs;
	outputs.reserve(1000);
	const auto evaluateStart = std::chrono::steady_clock::now();
	for (int iteration = 0; iteration < 20; ++iteration)
	{
		outputs.clear();
		player.SeekTicks(iteration * 4 + 1, Vans::VansTimelineSeekPolicy::ContinuousOnly,
			Vans::VansTimelineEvaluationReason::Scrub);
		player.UpdatePostScript(0.0, outputs);
		if (!Expect(outputs.size() == 1000,
			"Timeline scale evaluation did not produce every active continuous track"))
			return false;
	}
	const double averageEvaluationMilliseconds = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - evaluateStart).count() / 20.0;
#ifdef NDEBUG
	constexpr double evaluationBudgetMilliseconds = 25.0;
#else
	constexpr double evaluationBudgetMilliseconds = 250.0;
#endif
	std::cout << "[TimelineScale] tracks=1000 keys=100000 compileMs=" << compileMilliseconds
		<< " averageEvaluateMs=" << averageEvaluationMilliseconds << '\n';
	return Expect(averageEvaluationMilliseconds <= evaluationBudgetMilliseconds,
		"Timeline 1000-track/100000-Key evaluation exceeded the configuration budget");
}

struct RuntimeWorldTestComponent
{
	int value = 0;
};

bool TestRuntimeWorldEntityLifetimeContract()
{
	Vans::VansRuntimeWorld world;
	Vans::VansEntityHandle parent = world.CreateEntity({ "parent-guid", "Parent" });
	Vans::VansEntityHandle child = world.CreateEntity({ "child-guid", "Child", parent });

	if (!Expect(world.IsAlive(parent) && world.IsAlive(child),
		"Runtime world did not create live entities"))
		return false;
	if (!Expect(world.Entities().FindByGuid("child-guid") == child,
		"Runtime world guid index did not resolve child entity"))
		return false;
	if (!Expect(world.Entities().FindByName("Parent") == parent,
		"Runtime world name index did not resolve parent entity"))
		return false;
	if (!Expect(world.SetEntityName(parent, "RenamedParent"),
		"Runtime world failed to rename entity"))
		return false;
	if (!Expect(
		world.Entities().FindByName("Parent") == Vans::VansEntityHandle{} &&
			world.Entities().FindByName("RenamedParent") == parent &&
			world.Entities().Get(parent)->name == "RenamedParent",
		"Runtime world rename did not update name index and entity record"))
		return false;
	if (!Expect(world.Entities().Get(parent)->children.size() == 1,
		"Runtime world parent did not track child entity"))
		return false;
	Vans::VansEntityHandle inactive = world.CreateEntity({ "inactive-guid", "Inactive", {}, false });
	if (!Expect(
		world.IsAlive(inactive) &&
		!world.Entities().IsHierarchyActive(inactive) &&
		world.Entities().Get(inactive)->selfActive == false,
		"Runtime world create entity did not preserve inactive authoring state"))
		return false;
	const std::vector<Vans::VansEntityHandle> aliveBeforeDestroy =
		world.Entities().CollectAliveEntities();
	if (!Expect(
		aliveBeforeDestroy.size() == 3 &&
		std::find(aliveBeforeDestroy.begin(), aliveBeforeDestroy.end(), parent) != aliveBeforeDestroy.end() &&
		std::find(aliveBeforeDestroy.begin(), aliveBeforeDestroy.end(), child) != aliveBeforeDestroy.end() &&
		std::find(aliveBeforeDestroy.begin(), aliveBeforeDestroy.end(), inactive) != aliveBeforeDestroy.end(),
		"Runtime world alive entity enumeration did not include all live entities"))
		return false;

	const std::uint32_t oldGeneration = parent.generation;
	if (!Expect(world.DestroyEntity(parent), "Runtime world failed to destroy parent entity"))
		return false;
	if (!Expect(!world.IsAlive(parent) && !world.IsAlive(child),
		"Runtime world default destroy did not destroy child subtree"))
		return false;
	const std::vector<Vans::VansEntityHandle> aliveAfterDestroy =
		world.Entities().CollectAliveEntities();
	if (!Expect(
		aliveAfterDestroy.size() == 1 &&
		aliveAfterDestroy[0] == inactive,
		"Runtime world alive entity enumeration retained destroyed entities"))
		return false;

	Vans::VansEntityHandle reused = world.CreateEntity({ "reused-guid", "Reused" });
	return Expect(reused.index == parent.index && reused.generation != oldGeneration,
		"Runtime world entity handle generation did not advance after slot reuse");
}

bool TestRuntimeWorldParentEditContract()
{
	Vans::VansRuntimeWorld world;
	Vans::VansEntityHandle parent = world.CreateEntity({ "parent-guid", "Parent" });
	Vans::VansEntityHandle child = world.CreateEntity({ "child-guid", "Child" });

	if (!Expect(world.SetParent(child, parent), "Runtime world failed to apply parent edit"))
		return false;
	const Vans::VansEntityRecord* parentRecord = world.Entities().Get(parent);
	const Vans::VansEntityRecord* childRecord = world.Entities().Get(child);
	if (!Expect(parentRecord && parentRecord->children.size() == 1 && parentRecord->children[0] == child,
		"Runtime world parent edit did not update parent child list"))
		return false;
	if (!Expect(childRecord && childRecord->parent == parent,
		"Runtime world parent edit did not update child parent handle"))
		return false;

	auto& storage = world.RegisterStorage<RuntimeWorldTestComponent>(11);
	const Vans::VansComponentHandle parentComponent =
		storage.Add(parent, RuntimeWorldTestComponent{ 1 }, "parent-component-guid", true, true);
	const Vans::VansComponentHandle childComponent =
		storage.Add(child, RuntimeWorldTestComponent{ 2 }, "child-component-guid", true, true);
	const std::vector<Vans::VansComponentHandle> subtreeComponents =
		world.CollectComponentsInSubtree(parent);
	if (!Expect(
		std::find(subtreeComponents.begin(), subtreeComponents.end(), parentComponent) != subtreeComponents.end() &&
			std::find(subtreeComponents.begin(), subtreeComponents.end(), childComponent) != subtreeComponents.end(),
		"Runtime world subtree component collection did not include parent and child components"))
		return false;

	if (!Expect(world.SetEntityActive(parent, false),
		"Runtime world failed to deactivate parent entity"))
		return false;
	if (!Expect(!world.Entities().IsHierarchyActive(child),
		"Runtime world parent active state did not propagate to child"))
		return false;
	if (!Expect(
		!world.IsComponentEffectivelyEnabled(parentComponent) &&
			!world.IsComponentEffectivelyEnabled(childComponent),
		"Runtime world parent active state did not propagate to subtree components"))
		return false;

	if (!Expect(world.SetParent(child, {}),
		"Runtime world failed to clear parent edit"))
		return false;
	childRecord = world.Entities().Get(child);
	if (!Expect(childRecord && !childRecord->parent.IsValid(),
		"Runtime world clear parent edit left child parent handle valid"))
		return false;
	if (!Expect(world.Entities().IsHierarchyActive(child),
		"Runtime world clear parent edit did not detach child hierarchy active state"))
		return false;
	if (!Expect(world.IsComponentEffectivelyEnabled(childComponent),
		"Runtime world clear parent edit did not restore child component effective enabled"))
		return false;

	return true;
}

bool TestRuntimeWorldComponentEnabledContract()
{
	Vans::VansRuntimeWorld world;
	Vans::VansEntityHandle entity = world.CreateEntity({ "entity-guid", "Entity" });
	auto& storage = world.RegisterStorage<RuntimeWorldTestComponent>(1);
	Vans::VansComponentHandle component =
		storage.Add(entity, RuntimeWorldTestComponent{ 7 }, "component-guid", false, true);

	if (!Expect(storage.Contains(component), "Runtime component storage did not retain component handle"))
		return false;
	if (!Expect(!world.IsComponentEffectivelyEnabled(component),
		"Disabled runtime component became effective enabled"))
		return false;

	if (!Expect(world.SetComponentEnabled(component, true),
		"Runtime world failed to enable component"))
		return false;
	if (!Expect(world.IsComponentSelfEnabled(component) && world.IsComponentEffectivelyEnabled(component),
		"Enabled runtime component did not become effective enabled"))
		return false;

	if (!Expect(world.SetEntityActive(entity, false),
		"Runtime world failed to deactivate entity"))
		return false;
	if (!Expect(world.IsComponentSelfEnabled(component) && !world.IsComponentEffectivelyEnabled(component),
		"Entity active change overwrote component self enabled state"))
		return false;

	if (!Expect(world.SetEntityActive(entity, true),
		"Runtime world failed to reactivate entity"))
		return false;
	if (!Expect(world.IsComponentSelfEnabled(component) && world.IsComponentEffectivelyEnabled(component),
		"Runtime component did not recover effective enabled after entity reactivation"))
		return false;

	const Vans::VansComponentHandle removed = component;
	Vans::VansComponentHandle survivor =
		storage.Add(entity, RuntimeWorldTestComponent{ 8 }, "component-guid-survivor", true, true);
	if (!Expect(world.RemoveComponent(component), "Runtime world failed to remove component"))
		return false;
	if (!Expect(!storage.Contains(removed),
		"Runtime component storage allowed stale handle after remove"))
		return false;
	if (!Expect(world.FindComponentByGuid("component-guid").IsValid() == false,
		"Runtime component guid index retained removed component"))
		return false;
	if (!Expect(world.FindComponentByGuid("component-guid-survivor") == survivor,
		"Runtime component guid index did not update moved component after remove"))
		return false;

	Vans::VansComponentHandle added =
		storage.Add(entity, RuntimeWorldTestComponent{ 9 }, "component-guid-2", true, true);
	return Expect(added.index == removed.index && added.generation != removed.generation,
		"Runtime component handle generation did not advance after slot reuse");
}

bool TestRuntimeWorldComponentLifetimeContract()
{
	Vans::VansRuntimeWorld world;
	Vans::VansEntityHandle parent = world.CreateEntity({ "parent-guid", "Parent" });
	Vans::VansEntityHandle child = world.CreateEntity({ "child-guid", "Child", parent });

	auto* parentRenderNode =
		reinterpret_cast<VansGraphics::VansRenderNode*>(static_cast<std::uintptr_t>(0x1234));
	Vans::VansRuntimeRenderComponent parentRenderComponent{ parentRenderNode };
	parentRenderComponent.renderNodes.push_back(parentRenderNode);
	Vans::VansComponentHandle parentComponent = world.AddComponent(
		parent,
		Vans::VansRuntimeComponentType_Render,
		parentRenderComponent,
		"parent-render-guid",
		true);
	auto* childAudioNode =
		reinterpret_cast<VansEngine::VansAudioNode*>(static_cast<std::uintptr_t>(0x1111));
	auto* childAudioBinding =
		reinterpret_cast<VansEngine::VansAudioSourceBinding*>(static_cast<std::uintptr_t>(0x2222));
	Vans::VansRuntimeAudioComponent childAudioComponent;
	childAudioComponent.audioNode = childAudioNode;
	childAudioComponent.sourceBinding = childAudioBinding;
	childAudioComponent.sourceName = "child-audio";
	Vans::VansComponentHandle childComponent = world.AddComponent(
		child,
		Vans::VansRuntimeComponentType_Audio,
		childAudioComponent,
		"child-audio-guid",
		true);

	const auto* renderStorage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeRenderComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Render));
	const auto* audioStorage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeAudioComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Audio));
	if (!Expect(renderStorage && renderStorage->Contains(parentComponent),
		"Runtime world did not register parent runtime component"))
		return false;
	const Vans::VansRuntimeRenderComponent* storedRenderComponent = renderStorage->Get(parentComponent);
	if (!Expect(
		storedRenderComponent &&
			storedRenderComponent->renderNode == parentRenderNode &&
			storedRenderComponent->renderNodes.size() == 1 &&
			storedRenderComponent->renderNodes[0] == parentRenderNode,
		"Runtime world did not store parent render component data"))
		return false;
	if (!Expect(audioStorage && audioStorage->Contains(childComponent),
		"Runtime world did not register child runtime component"))
		return false;
	const Vans::VansRuntimeAudioComponent* storedAudioComponent = audioStorage->Get(childComponent);
	if (!Expect(
		storedAudioComponent &&
			storedAudioComponent->audioNode == childAudioNode &&
			storedAudioComponent->sourceBinding == childAudioBinding,
		"Runtime world did not store child audio component data"))
		return false;
	if (!Expect(world.FindComponentByGuid("parent-render-guid", Vans::VansRuntimeComponentType_Render) == parentComponent,
		"Runtime world did not resolve component by stable guid and type"))
		return false;
	if (!Expect(world.FindComponentByGuid("child-audio-guid") == childComponent,
		"Runtime world did not resolve component by stable guid across storages"))
		return false;

	if (!Expect(world.DestroyEntity(parent),
		"Runtime world failed to destroy entity with runtime components"))
		return false;
	if (!Expect(!renderStorage->Contains(parentComponent),
		"Runtime world left destroyed entity component alive"))
		return false;
	if (!Expect(!audioStorage->Contains(childComponent),
		"Runtime world left destroyed child component alive"))
		return false;
	return Expect(!world.FindComponentByGuid("child-audio-guid").IsValid(),
		"Runtime world left destroyed component guid indexed");
}

bool TestRuntimeComponentKeyCanonicalizationContract()
{
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("Transform") == "transform",
		"Transform did not canonicalize to transform runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("ModelRenderer") == "render",
		"ModelRenderer did not canonicalize to render runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("Animator") == "animation",
		"Animator did not canonicalize to animation runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("CharacterController") == "charController",
		"CharacterController did not canonicalize to character controller runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("charController") == "charController",
		"Canonical character controller runtime component key was not idempotent"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("UIController") == "ui",
		"UIController did not canonicalize to UI runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("LuaScript") == "script",
		"LuaScript did not canonicalize to script runtime component key"))
		return false;
	return Expect(Vans::VansRuntimeComponentTypeIdForKey("transform") == Vans::VansRuntimeComponentType_Transform,
		"Transform runtime component key did not resolve to transform type id");
}

bool TestRuntimeWorldCommandBufferContract()
{
	Vans::VansRuntimeWorld world;
	world.Commands().CreateEntity({ "queued-guid", "Queued" });
	if (!Expect(world.Commands().PendingCount() == 1,
		"Runtime world command buffer did not queue create command"))
		return false;

	world.FlushCommands();
	Vans::VansEntityHandle queued = world.Entities().FindByGuid("queued-guid");
	if (!Expect(world.IsAlive(queued),
		"Runtime world command buffer did not flush create command"))
		return false;

	Vans::VansEntityHandle parent = world.CreateEntity({ "queued-parent-guid", "QueuedParent" });
	world.Commands().SetParent(queued, parent);
	world.FlushCommands();
	const Vans::VansEntityRecord* parentRecord = world.Entities().Get(parent);
	const Vans::VansEntityRecord* childRecord = world.Entities().Get(queued);
	if (!Expect(parentRecord &&
		std::find(parentRecord->children.begin(), parentRecord->children.end(), queued) != parentRecord->children.end() &&
		childRecord &&
		childRecord->parent == parent,
		"Runtime world command buffer did not flush parent command"))
		return false;

	world.Commands().SetEntityActive(queued, false);
	world.FlushCommands();
	if (!Expect(!world.Entities().IsHierarchyActive(queued),
		"Runtime world command buffer did not flush active command"))
		return false;

	if (!Expect(world.CollectComponentsOwnedBy(queued).empty(),
		"Runtime world collect owned components returned entries before component add"))
		return false;

	world.Commands().SetEntityName(queued, "QueuedRenamed");
	world.FlushCommands();
	if (!Expect(
		world.Entities().FindByName("Queued") == Vans::VansEntityHandle{} &&
			world.Entities().FindByName("QueuedRenamed") == queued,
		"Runtime world command buffer did not flush entity name command"))
		return false;

	world.Commands().AddTransformComponent(
		queued,
		"queued-transform-guid",
		42,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle component = world.FindComponentByGuid(
		"queued-transform-guid",
		Vans::VansRuntimeComponentType_Transform);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeTransformComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Transform));
	if (!Expect(storage && world.IsComponentSelfEnabled(component),
		"Runtime world command buffer did not flush component add command"))
		return false;
	const Vans::VansRuntimeTransformComponent* transformComponent = storage->Get(component);
	if (!Expect(transformComponent && transformComponent->transformStoreId == 42,
		"Runtime world command buffer did not store transform component data"))
		return false;
	const std::vector<Vans::VansComponentHandle> queuedComponents =
		world.CollectComponentsOwnedBy(queued);
	if (!Expect(
		std::find(queuedComponents.begin(), queuedComponents.end(), component) != queuedComponents.end(),
		"Runtime world collect owned components did not include transform component"))
		return false;

	world.Commands().SetComponentEnabled(component, false);
	world.FlushCommands();
	if (!Expect(!world.IsComponentSelfEnabled(component),
		"Runtime world command buffer did not flush component enabled command"))
		return false;

	world.Commands().RemoveComponent(component);
	world.FlushCommands();
	if (!Expect(!storage->Contains(component),
		"Runtime world command buffer did not flush component remove command"))
		return false;
	if (!Expect(!world.FindComponentByGuid("queued-transform-guid").IsValid(),
		"Runtime world command buffer retained removed component guid"))
		return false;

	auto* renderNode =
		reinterpret_cast<VansGraphics::VansRenderNode*>(static_cast<std::uintptr_t>(0x5678));
	world.Commands().AddRenderComponent(
		queued,
		"queued-render-guid",
		renderNode,
		std::vector<VansGraphics::VansRenderNode*>{ renderNode },
		true);
	world.FlushCommands();
	Vans::VansComponentHandle renderComponent = world.FindComponentByGuid(
		"queued-render-guid",
		Vans::VansRuntimeComponentType_Render);
	auto* renderStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeRenderComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Render));
	const Vans::VansRuntimeRenderComponent* renderComponentData =
		renderStorage ? renderStorage->Get(renderComponent) : nullptr;
	if (!Expect(
		renderComponentData &&
			renderComponentData->renderNode == renderNode &&
			renderComponentData->renderNodes.size() == 1 &&
			renderComponentData->renderNodes[0] == renderNode,
		"Runtime world command buffer did not store render component data"))
		return false;

	auto* physicsNode =
		reinterpret_cast<VansEngine::VansPhysicsNode*>(static_cast<std::uintptr_t>(0x1357));
	world.Commands().AddPhysicsComponent(
		queued,
		"queued-physics-guid",
		physicsNode,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle physicsComponent = world.FindComponentByGuid(
		"queued-physics-guid",
		Vans::VansRuntimeComponentType_Physics);
	auto* physicsStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimePhysicsComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Physics));
	const Vans::VansRuntimePhysicsComponent* physicsComponentData =
		physicsStorage ? physicsStorage->Get(physicsComponent) : nullptr;
	if (!Expect(physicsComponentData && physicsComponentData->physicsNode == physicsNode,
		"Runtime world command buffer did not store physics component data"))
		return false;

	auto* clothNode =
		reinterpret_cast<VansEngine::VansClothNode*>(static_cast<std::uintptr_t>(0x2468));
	world.Commands().AddClothComponent(
		queued,
		"queued-cloth-guid",
		clothNode,
		"Profiles/cloth.profile",
		true);
	world.FlushCommands();
	Vans::VansComponentHandle clothComponent = world.FindComponentByGuid(
		"queued-cloth-guid",
		Vans::VansRuntimeComponentType_Cloth);
	auto* clothStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeClothComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Cloth));
	const Vans::VansRuntimeClothComponent* clothComponentData =
		clothStorage ? clothStorage->Get(clothComponent) : nullptr;
	if (!Expect(
		clothComponentData &&
			clothComponentData->clothNode == clothNode &&
			clothComponentData->profilePath == "Profiles/cloth.profile",
		"Runtime world command buffer did not store cloth component data"))
		return false;

	auto* controllerNode =
		reinterpret_cast<VansEngine::VansCharacterControllerNode*>(static_cast<std::uintptr_t>(0x3579));
	world.Commands().AddCharacterControllerComponent(
		queued,
		"queued-controller-guid",
		controllerNode,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle controllerComponent = world.FindComponentByGuid(
		"queued-controller-guid",
		Vans::VansRuntimeComponentType_CharacterController);
	auto* controllerStorage =
		static_cast<Vans::VansComponentStorage<Vans::VansRuntimeCharacterControllerComponent>*>(
			world.FindStorage(Vans::VansRuntimeComponentType_CharacterController));
	const Vans::VansRuntimeCharacterControllerComponent* controllerComponentData =
		controllerStorage ? controllerStorage->Get(controllerComponent) : nullptr;
	if (!Expect(controllerComponentData && controllerComponentData->controllerNode == controllerNode,
		"Runtime world command buffer did not store character controller component data"))
		return false;

	auto* vehicle =
		reinterpret_cast<VansEngine::VansPhysicsVehicle*>(static_cast<std::uintptr_t>(0x468A));
	world.Commands().AddVehicleComponent(
		queued,
		"queued-vehicle-guid",
		vehicle,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle vehicleComponent = world.FindComponentByGuid(
		"queued-vehicle-guid",
		Vans::VansRuntimeComponentType_Vehicle);
	auto* vehicleStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeVehicleComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Vehicle));
	const Vans::VansRuntimeVehicleComponent* vehicleComponentData =
		vehicleStorage ? vehicleStorage->Get(vehicleComponent) : nullptr;
	if (!Expect(vehicleComponentData && vehicleComponentData->vehicle == vehicle,
		"Runtime world command buffer did not store vehicle component data"))
		return false;

	auto* animationNode =
		reinterpret_cast<VansGraphics::VansAnimationNode*>(static_cast<std::uintptr_t>(0x579B));
	world.Commands().AddAnimationComponent(
		queued,
		"queued-animation-guid",
		animationNode,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle animationComponent = world.FindComponentByGuid(
		"queued-animation-guid",
		Vans::VansRuntimeComponentType_Animation);
	auto* animationStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Animation));
	const Vans::VansRuntimeAnimationComponent* animationComponentData =
		animationStorage ? animationStorage->Get(animationComponent) : nullptr;
	if (!Expect(animationComponentData && animationComponentData->animationNode == animationNode,
		"Runtime world command buffer did not store animation component data"))
		return false;

	world.Commands().AddRagdollComponent(
		queued,
		"queued-ragdoll-guid",
		animationNode,
		2,
		"Profiles/hero.ragdoll",
		"Hero",
		12,
		11,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle ragdollComponent = world.FindComponentByGuid(
		"queued-ragdoll-guid",
		Vans::VansRuntimeComponentType_Ragdoll);
	auto* ragdollStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeRagdollComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Ragdoll));
	const Vans::VansRuntimeRagdollComponent* ragdollComponentData =
		ragdollStorage ? ragdollStorage->Get(ragdollComponent) : nullptr;
	if (!Expect(
		ragdollComponentData &&
			ragdollComponentData->animationNode == animationNode &&
			ragdollComponentData->initialDriveMode == 2 &&
			ragdollComponentData->profilePath == "Profiles/hero.ragdoll" &&
			ragdollComponentData->profileName == "Hero" &&
			ragdollComponentData->configuredBodyCount == 12 &&
			ragdollComponentData->configuredJointCount == 11,
		"Runtime world command buffer did not store ragdoll component data"))
		return false;

	auto* audioNode =
		reinterpret_cast<VansEngine::VansAudioNode*>(static_cast<std::uintptr_t>(0x5ACE));
	auto* audioBinding =
		reinterpret_cast<VansEngine::VansAudioSourceBinding*>(static_cast<std::uintptr_t>(0x5ACF));
	VansEngine::AudioOcclusionSettings occlusionSettings;
	occlusionSettings.enabled = true;
	occlusionSettings.material = "wood";
	VansEngine::AudioOcclusionState occlusionState;
	occlusionState.gain = 0.7f;
	occlusionState.highFrequencyGain = 0.4f;
	occlusionState.lastBlocked = true;
	VansEngine::AudioConeSettings coneSettings;
	coneSettings.enabled = true;
	coneSettings.innerAngleDegrees = 45.0f;
	coneSettings.outerAngleDegrees = 120.0f;
	coneSettings.outerGain = 0.25f;
	world.Commands().AddAudioComponent(
		queued,
		"queued-audio-guid",
		audioNode,
		audioBinding,
		"ambience",
		coneSettings,
		true,
		true,
		1.0f,
		2.0f,
		3.0f,
		occlusionSettings,
		occlusionState,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle audioComponent = world.FindComponentByGuid(
		"queued-audio-guid",
		Vans::VansRuntimeComponentType_Audio);
	auto* audioStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAudioComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Audio));
	const Vans::VansRuntimeAudioComponent* audioComponentData =
		audioStorage ? audioStorage->Get(audioComponent) : nullptr;
	if (!Expect(
			audioComponentData &&
			audioComponentData->audioNode == audioNode &&
			audioComponentData->sourceBinding == audioBinding &&
			audioComponentData->sourceName == "ambience" &&
			audioComponentData->coneSettings.enabled &&
			audioComponentData->coneSettings.innerAngleDegrees == 45.0f &&
			audioComponentData->coneSettings.outerAngleDegrees == 120.0f &&
			audioComponentData->coneSettings.outerGain == 0.25f &&
			audioComponentData->dopplerEnabled &&
			audioComponentData->hasLastAudioPosition &&
			audioComponentData->lastAudioPositionX == 1.0f &&
			audioComponentData->lastAudioPositionY == 2.0f &&
			audioComponentData->lastAudioPositionZ == 3.0f &&
			audioComponentData->occlusionSettings.enabled &&
			audioComponentData->occlusionSettings.material == "wood" &&
			audioComponentData->occlusionState.gain == 0.7f &&
			audioComponentData->occlusionState.highFrequencyGain == 0.4f &&
			audioComponentData->occlusionState.lastBlocked,
		"Runtime world command buffer did not store audio component data"))
		return false;

	Vans::VansRuntimeAudioReverbZoneComponent reverbZone;
	reverbZone.shape = "box";
	reverbZone.preset = "cave";
	reverbZone.presetAssetGuid = "preset-guid";
	reverbZone.overridePresetParameters = true;
	reverbZone.radius = 9.0f;
	reverbZone.halfExtentX = 4.0f;
	reverbZone.halfExtentY = 5.0f;
	reverbZone.halfExtentZ = 6.0f;
	reverbZone.fadeDistance = 1.5f;
	reverbZone.wetGain = 0.75f;
	reverbZone.priority = 2;
	world.Commands().AddAudioReverbZoneComponent(
		queued,
		Vans::VansRuntimeComponentType_AudioReverbZone,
		"queued-reverb-guid",
		reverbZone,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle reverbComponent = world.FindComponentByGuid(
		"queued-reverb-guid",
		Vans::VansRuntimeComponentType_AudioReverbZone);
	auto* reverbStorage =
		static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAudioReverbZoneComponent>*>(
			world.FindStorage(Vans::VansRuntimeComponentType_AudioReverbZone));
	const Vans::VansRuntimeAudioReverbZoneComponent* reverbComponentData =
		reverbStorage ? reverbStorage->Get(reverbComponent) : nullptr;
	if (!Expect(
		reverbComponentData &&
			reverbComponentData->shape == "box" &&
			reverbComponentData->preset == "cave" &&
			reverbComponentData->presetAssetGuid == "preset-guid" &&
			reverbComponentData->overridePresetParameters &&
			reverbComponentData->radius == 9.0f &&
			reverbComponentData->halfExtentX == 4.0f &&
			reverbComponentData->halfExtentY == 5.0f &&
			reverbComponentData->halfExtentZ == 6.0f &&
			reverbComponentData->fadeDistance == 1.5f &&
			reverbComponentData->wetGain == 0.75f &&
			reverbComponentData->priority == 2,
		"Runtime world command buffer did not store audio reverb component data"))
		return false;

	auto* videoTexture =
		reinterpret_cast<VansGraphics::VansVideoTexture*>(static_cast<std::uintptr_t>(0x2345));
	auto* videoManager =
		reinterpret_cast<VansGraphics::VansVideoManager*>(static_cast<std::uintptr_t>(0x3456));
	world.Commands().AddVideoComponent(
		queued,
		"queued-video-guid",
		videoTexture,
		videoManager,
		7,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle videoComponent = world.FindComponentByGuid(
		"queued-video-guid",
		Vans::VansRuntimeComponentType_Video);
	auto* videoStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeVideoComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Video));
	const Vans::VansRuntimeVideoComponent* videoComponentData =
		videoStorage ? videoStorage->Get(videoComponent) : nullptr;
	if (!Expect(
		videoComponentData &&
			videoComponentData->videoTexture == videoTexture &&
			videoComponentData->videoManager == videoManager &&
			videoComponentData->bindlessFirstSlot == 7,
		"Runtime world command buffer did not store video component data"))
		return false;

	auto* particleRuntime =
		reinterpret_cast<VansGraphics::VansParticleRuntime*>(static_cast<std::uintptr_t>(0x4567));
	auto* particleRenderNode =
		reinterpret_cast<VansGraphics::VansParticleRenderNode*>(static_cast<std::uintptr_t>(0x6789));
	world.Commands().AddParticleComponent(
		queued,
		"queued-particle-guid",
		particleRuntime,
		particleRenderNode,
		true,
		false,
		4.25f,
		true,
		1.5f,
		2.5f,
		3.5f,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle particleComponent = world.FindComponentByGuid(
		"queued-particle-guid",
		Vans::VansRuntimeComponentType_Particle);
	auto* particleStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeParticleComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Particle));
	const Vans::VansRuntimeParticleComponent* particleComponentData =
		particleStorage ? particleStorage->Get(particleComponent) : nullptr;
	if (!Expect(
		particleComponentData &&
			particleComponentData->runtime == particleRuntime &&
			particleComponentData->renderNode == particleRenderNode &&
			particleComponentData->playOnAwake &&
			!particleComponentData->isPlaying &&
			particleComponentData->playTime == 4.25f &&
			particleComponentData->hasWorldPositionOverride &&
			particleComponentData->worldPositionOverrideX == 1.5f &&
			particleComponentData->worldPositionOverrideY == 2.5f &&
			particleComponentData->worldPositionOverrideZ == 3.5f,
		"Runtime world command buffer did not store particle component data"))
		return false;

	auto* camera =
		reinterpret_cast<VansGraphics::VansCamera*>(static_cast<std::uintptr_t>(0x789A));
	world.Commands().AddCameraComponent(
		queued,
		"queued-camera-guid",
		camera,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle cameraComponent = world.FindComponentByGuid(
		"queued-camera-guid",
		Vans::VansRuntimeComponentType_Camera);
	auto* cameraStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeCameraComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Camera));
	const Vans::VansRuntimeCameraComponent* cameraComponentData =
		cameraStorage ? cameraStorage->Get(cameraComponent) : nullptr;
	if (!Expect(cameraComponentData && cameraComponentData->camera == camera,
		"Runtime world command buffer did not store camera component data"))
		return false;

	auto* lightManager =
		reinterpret_cast<VansGraphics::VansLightManager*>(static_cast<std::uintptr_t>(0x89AB));
	world.Commands().AddLightComponent(
		queued,
		Vans::VansRuntimeComponentType_PointLight,
		"queued-point-light-guid",
		lightManager,
		3,
		Vans::VansRuntimeLightKind::Point,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle lightComponent = world.FindComponentByGuid(
		"queued-point-light-guid",
		Vans::VansRuntimeComponentType_PointLight);
	auto* lightStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeLightComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_PointLight));
	const Vans::VansRuntimeLightComponent* lightComponentData =
		lightStorage ? lightStorage->Get(lightComponent) : nullptr;
	if (!Expect(
		lightComponentData &&
			lightComponentData->lightManager == lightManager &&
			lightComponentData->lightIndex == 3 &&
			lightComponentData->kind == Vans::VansRuntimeLightKind::Point,
		"Runtime world command buffer did not store light component data"))
		return false;

	Vans::VansRuntimeUIComponent uiComponentData;
	uiComponentData.autoOpenScreens = { "MainMenu", "Hud" };
	uiComponentData.preloadScreens = { "Inventory" };
	uiComponentData.openScreens = { 10, 11 };
	world.Commands().AddUIComponent(
		queued,
		"queued-ui-guid",
		uiComponentData,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle uiComponent = world.FindComponentByGuid(
		"queued-ui-guid",
		Vans::VansRuntimeComponentType_UI);
	auto* uiStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeUIComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_UI));
	const Vans::VansRuntimeUIComponent* storedUIComponent =
		uiStorage ? uiStorage->Get(uiComponent) : nullptr;
	if (!Expect(
		storedUIComponent &&
			storedUIComponent->autoOpenScreens.size() == 2 &&
			storedUIComponent->autoOpenScreens[0] == "MainMenu" &&
			storedUIComponent->preloadScreens.size() == 1 &&
			storedUIComponent->preloadScreens[0] == "Inventory" &&
			storedUIComponent->openScreens.size() == 2 &&
			storedUIComponent->openScreens[1] == 11,
		"Runtime world command buffer did not store UI component data"))
		return false;

	Vans::VansRuntimeScriptComponent scriptComponentData;
	scriptComponentData.scriptPath = "Scripts/player.lua";
	scriptComponentData.entryName = "Player";
	scriptComponentData.enableRequested = true;
	scriptComponentData.state = Vans::VansRuntimeScriptState::Unloaded;
	scriptComponentData.isValid = false;
	scriptComponentData.hasStarted = false;
	Vans::VansRuntimeScriptFieldValue scriptField;
	scriptField.type = Vans::VansRuntimeScriptFieldType::Float;
	scriptField.floatValue = 4.5;
	scriptComponentData.serializedFields.emplace("speed", scriptField);
	world.Commands().AddScriptComponent(
		queued,
		"queued-script-guid",
		scriptComponentData,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle componentOwnedByDestroyedEntity = world.FindComponentByGuid(
		"queued-script-guid",
		Vans::VansRuntimeComponentType_Script);
	auto* scriptStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeScriptComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Script));
	if (!Expect(scriptStorage && scriptStorage->Contains(componentOwnedByDestroyedEntity),
		"Runtime world command buffer did not add destroy-owned component"))
		return false;
	const Vans::VansRuntimeScriptComponent* storedScriptComponent =
		scriptStorage->Get(componentOwnedByDestroyedEntity);
	if (!Expect(
		storedScriptComponent &&
			storedScriptComponent->scriptPath == "Scripts/player.lua" &&
			storedScriptComponent->entryName == "Player" &&
			storedScriptComponent->serializedFields.count("speed") == 1 &&
			storedScriptComponent->serializedFields.at("speed").floatValue == 4.5,
		"Runtime world command buffer did not store script component data"))
		return false;

	world.Commands().DestroyEntity(queued);
	world.FlushCommands();
	if (!Expect(!world.IsAlive(queued),
		"Runtime world command buffer did not flush destroy command"))
		return false;
	return Expect(!scriptStorage->Contains(componentOwnedByDestroyedEntity) &&
		!world.FindComponentByGuid("queued-script-guid").IsValid(),
		"Runtime world command buffer destroy did not clear owned components");
}

struct ScriptComponentEnabledProbe final : public VansScriptComponent
{
	int enableCount = 0;
	int disableCount = 0;
	int destroyCount = 0;

protected:
	void OnEnable() override { ++enableCount; }
	void OnDisable() override { ++disableCount; }
	void OnDestroy() override { ++destroyCount; }
};

bool TestScriptObjectActiveDoesNotOverwriteComponentEnabledContract()
{
	VansScriptObject object;
	auto* component = new ScriptComponentEnabledProbe();
	component->SetEnabled(false);
	object.AddComponent(component);

	if (!Expect(!component->IsEnabled() && !component->IsEffectivelyEnabled(),
		"Disabled component became effective enabled when added to active object"))
		return false;

	object.SetActive(false);
	if (!Expect(!component->IsEnabled() && !component->IsEffectivelyEnabled(),
		"Object deactivation overwrote component self enabled state"))
		return false;

	object.SetActive(true);
	if (!Expect(!component->IsEnabled() && !component->IsEffectivelyEnabled(),
		"Object reactivation enabled a component that was disabled by itself"))
		return false;

	component->SetEnabled(true);
	return Expect(component->IsEnabled() && component->IsEffectivelyEnabled(),
		"Component did not become effective enabled after self enable on active object");
}

bool TestScriptComponentDestroyIsIdempotentContract()
{
	ScriptComponentEnabledProbe component;
	component.Destroy();
	component.Destroy();

	if (!Expect(!component.IsEnabled() && !component.IsEffectivelyEnabled(),
		"Destroyed component remained enabled"))
		return false;
	if (!Expect(component.disableCount == 1,
		"Destroyed component disabled more than once"))
		return false;
	return Expect(component.destroyCount == 1,
		"Destroyed component ran OnDestroy more than once");
}

bool TestScriptComponentRuntimeEnabledMirrorHasNoBackendCallbacksContract()
{
	ScriptComponentEnabledProbe component;
	component.SetEnabled(false);
	if (!Expect(component.disableCount == 1 && component.enableCount == 0,
		"Probe did not enter the expected disabled baseline"))
		return false;

	component.MirrorRuntimeEnabledState(true, true);
	if (!Expect(component.IsEnabled() && component.IsEffectivelyEnabled(),
		"Runtime enabled mirror did not update wrapper visible state"))
		return false;
	if (!Expect(component.enableCount == 0 && component.disableCount == 1,
		"Runtime enabled mirror triggered backend enable/disable callbacks"))
		return false;

	component.MirrorRuntimeEnabledState(false, false);
	if (!Expect(!component.IsEnabled() && !component.IsEffectivelyEnabled(),
		"Runtime disabled mirror did not update wrapper visible state"))
		return false;
	return Expect(component.enableCount == 0 && component.disableCount == 1,
		"Runtime disabled mirror triggered backend enable/disable callbacks");
}

bool TestScriptParticleRuntimeEnabledMirrorContract()
{
	VansScriptParticleComponent component;
	component.m_Runtime = std::make_unique<VansGraphics::VansParticleRuntime>();

	component.MirrorRuntimeEnabledState(true, true);
	if (!Expect(component.IsEnabled() && component.IsEffectivelyEnabled(),
		"Particle runtime mirror did not update enabled state"))
		return false;
	if (!Expect(component.m_IsPlaying && component.m_Runtime->m_IsPlaying,
		"Particle runtime mirror did not start playback state"))
		return false;

	component.MirrorRuntimeEnabledState(true, false);
	if (!Expect(component.IsEnabled() && !component.IsEffectivelyEnabled(),
		"Particle runtime mirror did not preserve self enabled state while disabling effective state"))
		return false;
	return Expect(!component.m_IsPlaying && !component.m_Runtime->m_IsPlaying,
		"Particle runtime mirror did not pause playback state");
}

bool TestScriptUIRuntimeOpenScreensMirrorContract()
{
	VansScriptUIComponent component;
	component.MirrorRuntimeOpenScreens({ 17, 23 });
	if (!Expect(component.m_OpenScreens.size() == 2 &&
		component.m_OpenScreens[0] == 17 &&
		component.m_OpenScreens[1] == 23,
		"UI runtime mirror did not copy open screen handles"))
		return false;

	component.MirrorRuntimeEnabledState(false, false);
	if (!Expect(!component.IsEnabled() && !component.IsEffectivelyEnabled(),
		"UI runtime mirror did not update enabled facade state"))
		return false;

	component.MirrorRuntimeOpenScreens({});
	return Expect(component.m_OpenScreens.empty(),
		"UI runtime mirror did not clear open screen handles");
}

bool TestScriptObjectOwnedTransformReleaseContract()
{
	const std::uint32_t transformID = VansGraphics::VansTransformStore::AllocateTransform();
	const std::size_t queueSizeAfterAllocate = VansGraphics::VansTransformStore::FreeTransformIndices.size();

	auto* object = new VansScriptObject();
	object->m_TransformID = transformID;
	object->m_OwnsTransform = true;

	if (!Expect(object->ReleaseOwnedTransform() == transformID,
		"Script object did not release its owned transform id"))
	{
		delete object;
		VansGraphics::VansTransformStore::FreeTransform(transformID);
		return false;
	}

	delete object;
	if (!Expect(VansGraphics::VansTransformStore::FreeTransformIndices.size() == queueSizeAfterAllocate,
		"Script object destructor freed a transform after ownership release"))
	{
		VansGraphics::VansTransformStore::FreeTransform(transformID);
		return false;
	}

	VansGraphics::VansTransformStore::FreeTransform(transformID);
	return Expect(
		VansGraphics::VansTransformStore::FreeTransformIndices.size() == queueSizeAfterAllocate + 1,
		"Released transform was not freed exactly once by the caller");
}

bool TestScriptLightIndexRebindFacadeContract()
{
	VansScriptRectLightComponent rectLight;
	rectLight.m_LightIndex = 7;
	VansScriptComponent* base = &rectLight;
	base->RebindSceneLightIndex(VansScriptLightIndexKind::Point, 7, 3);
	if (!Expect(rectLight.m_LightIndex == 7,
		"Rect light component responded to the wrong scene light index kind"))
		return false;
	base->RebindSceneLightIndex(VansScriptLightIndexKind::Rect, 7, 3);
	return Expect(rectLight.m_LightIndex == 3,
		"Rect light component did not rebind its scene light index");
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

std::string BuildMinimalVClipHeader(bool includeNodeTransformChannels)
{
    std::string header =
        R"({"clipName":"ConfiguredSkeletalOnly","duration":0.0,"ticksPerSecond":60.0,"boneCount":0,)"
        R"("globalInverseTransform":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"bones":[])";
    if (includeNodeTransformChannels)
        header += R"(,"nodeTransformChannels":[])";
    header += "}";
    return header;
}

bool WriteMinimalVClip(const fs::path& path, bool includeNodeTransformChannels)
{
    const std::string header = BuildMinimalVClipHeader(includeNodeTransformChannels);
    const char magic[6] = {'V', 'C', 'L', 'I', 'P', '\0'};
    const std::uint32_t version = 1;
    const std::uint32_t headerSize = static_cast<std::uint32_t>(header.size());
    const std::uint64_t payloadSize = 0;

    std::ofstream out(path, std::ios::binary);
    out.write(magic, sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&headerSize), sizeof(headerSize));
    out.write(reinterpret_cast<const char*>(&payloadSize), sizeof(payloadSize));
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    return static_cast<bool>(out);
}

bool WriteVClipMeta(const fs::path& clipPath, bool hasNodeTransformChannels)
{
    std::ofstream out(clipPath.string() + ".meta", std::ios::binary);
    out << R"({"guid":"contract-test-vclip","importer":"AnimationClipImporter","version":1,"settings":{"nodeTransformChannels":)"
        << (hasNodeTransformChannels ? "true" : "false")
        << R"(},"subAssets":{}})";
    return static_cast<bool>(out);
}

bool TestAnimationClipNodeTransformChannelConfigContract()
{
    using namespace VansGraphics;

    TemporaryDirectory temporary;
    VansAnimationClip clip;
    Skeleton skeleton;

    const fs::path unconfiguredClip = temporary.path / "unconfigured_skeletal_only.vclip";
    if (!Expect(WriteMinimalVClip(unconfiguredClip, false),
        "Failed to write unconfigured skeletal-only vclip fixture"))
        return false;
    if (!Expect(!VansAnimationClipIO::Load(unconfiguredClip.string(), clip, skeleton),
        "Skeletal-only vclip without nodeTransformChannels resource config loaded unexpectedly"))
        return false;

    const fs::path configuredClip = temporary.path / "configured_skeletal_only.vclip";
    if (!Expect(WriteMinimalVClip(configuredClip, false) && WriteVClipMeta(configuredClip, false),
        "Failed to write configured skeletal-only vclip fixture"))
        return false;
    if (!Expect(VansAnimationClipIO::Load(configuredClip.string(), clip, skeleton),
        "Skeletal-only vclip with nodeTransformChannels=false config did not load"))
        return false;
    if (!Expect(clip.nodeTransformChannels.empty(),
        "Skeletal-only vclip config created unexpected node transform channels"))
        return false;

    const fs::path currentClip = temporary.path / "current_empty_node_channels.vclip";
    if (!Expect(WriteMinimalVClip(currentClip, true),
        "Failed to write current-format empty node transform channel fixture"))
        return false;
    if (!Expect(VansAnimationClipIO::Load(currentClip.string(), clip, skeleton),
        "Current-format vclip with explicit nodeTransformChannels did not load"))
        return false;

    return true;
}

bool TestAnimationStateMachineRestartSamplesStartPoseContract()
{
    using namespace VansGraphics;

    VansAnimationClip clip;
    clip.clipName = "Break";
    clip.duration = 1.0f;

    NodeTransformChannel channel;
    channel.nodeName = "Shard";
    channel.nodePath = "Glass/Shard";
    channel.keyframes.push_back({ 0.0f, glm::vec3(0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
    channel.keyframes.push_back({ 1.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
    clip.nodeTransformChannels.push_back(channel);

    auto stateMachine = std::make_unique<AnimGraphStateMachineNode>();
    stateMachine->m_DefaultStateName = "Break";

    AnimatorState state;
    state.name = "Break";
    state.clipName = "Break";
    state.loop = false;
    stateMachine->m_States.push_back(state);

    auto graph = std::make_unique<VansAnimGraph>();
    const int stateMachineId = graph->AddNode(std::move(stateMachine));
    const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    graph->AddLink(stateMachineId, 0, outputId, 0);

    VansAnimationController controller;
    controller.AddClip("Break", clip);
    std::string layerError;
    if (!Expect(InstallTestBaseLayer(controller, std::move(graph), layerError), layerError.c_str()))
        return false;

    Skeleton skeleton;
    controller.Play();
    controller.Update(0.0f, skeleton);
    controller.Update(2.0f, skeleton);
    const auto& endSample = controller.GetSampledNodeTransforms();
    if (!Expect(!endSample.empty() && std::fabs(endSample[0].modelTransform[3].x - 10.0f) <= 0.0001f,
        "State-machine clip did not reach its non-loop end pose"))
        return false;

    controller.Stop();
    controller.Play();
    controller.Update(0.0f, skeleton);
    const auto& restartSample = controller.GetSampledNodeTransforms();
    return Expect(!restartSample.empty() && std::fabs(restartSample[0].modelTransform[3].x) <= 0.0001f,
        "State-machine clip restart did not resample the start pose");
}

bool TestAnimatorCanonicalFormatContract()
{
    using namespace VansGraphics;

    TemporaryDirectory temporary;
    AnimatorAssetData asset;
    asset.name = "CanonicalAnimator";
    asset.editor.previewModelGuid = "33333333-3333-4333-8333-333333333333";
    asset.editor.previewModelPathHint = "Assets/Models/Hero.fbx";
    AnimatorParameter zeta;
    zeta.name = "Zeta";
    zeta.type = AnimatorParamType::Bool;
    AnimatorParameter alpha;
    alpha.name = "Alpha";
    alpha.type = AnimatorParamType::Float;
    asset.parameters = { zeta, alpha };

    auto graph = std::make_unique<VansAnimGraph>();
    auto clipNode = std::make_unique<AnimGraphClipNode>();
    clipNode->m_ClipName = "Idle";
    const int clipId = graph->AddNode(std::move(clipNode));
    const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(clipId > 0 && outputId > 0 && graph->AddLink(clipId, 0, outputId, 0) > 0,
        "Failed to build canonical animator graph fixture"))
        return false;
    asset.clipRefs = {
        { "Walk", "11111111-1111-4111-8111-111111111111", "Animation/Walk.vclip" },
        { "Idle", "22222222-2222-4222-8222-222222222222", "Animation/Idle.vclip" }
    };
    AnimatorGraphAsset graphAsset;
    graphAsset.id = "graph-base";
    graphAsset.name = "Base Graph";
    graphAsset.graph = std::move(graph);
    asset.graphs.push_back(std::move(graphAsset));
    VansAnimationLayerDefinition baseLayer;
    baseLayer.id = "layer-base";
    baseLayer.name = "Base";
    baseLayer.graphId = "graph-base";
    baseLayer.kind = VansAnimationLayerKind::Base;
    baseLayer.rootMotion = VansLayerRootMotionMode::Base;
    baseLayer.nodeTracks = VansLayerNodeTrackMode::Override;
    asset.layers.push_back(baseLayer);

    const fs::path firstPath = temporary.path / "canonical_first.vanimator";
    const fs::path secondPath = temporary.path / "canonical_second.vanimator";
    std::string saveError;
    if (!Expect(VansAnimatorIO::Save(firstPath.string(), asset, saveError)
        && VansAnimatorIO::Save(secondPath.string(), asset, saveError),
        "Canonical animator save failed"))
        return false;

    std::ifstream firstFile(firstPath, std::ios::binary);
    std::ifstream secondFile(secondPath, std::ios::binary);
    const std::string firstBytes((std::istreambuf_iterator<char>(firstFile)), std::istreambuf_iterator<char>());
    const std::string secondBytes((std::istreambuf_iterator<char>(secondFile)), std::istreambuf_iterator<char>());
    if (!Expect(firstBytes == secondBytes, "Canonical animator save is not byte-stable"))
        return false;

    const nlohmann::json root = nlohmann::json::parse(firstBytes);
    if (!Expect(!root.contains("version") && !root.contains("schemaVersion")
        && !root.contains("formatVersion"),
        "Canonical animator contains a generation field"))
        return false;
    if (!Expect(!root.contains("defaultState") && !root.contains("states")
        && !root.contains("transitions"),
        "Canonical animator contains a duplicate top-level state machine"))
        return false;
    if (!Expect(root["parameters"][0]["name"] == "Alpha"
        && root["parameters"][1]["name"] == "Zeta",
        "Animator parameters were not saved deterministically"))
        return false;
    if (!Expect(root["clips"][0]["name"] == "Idle" && root["clips"][1]["name"] == "Walk",
        "Animator clip references were not saved deterministically"))
        return false;
    if (!Expect(root["clips"][0].contains("asset")
        && root["clips"][0]["asset"]["guid"] == "22222222-2222-4222-8222-222222222222"
        && root["clips"][0]["asset"]["pathHint"] == "Animation/Idle.vclip"
        && !root["clips"][0].contains("path"),
        "Animator Clip reference is not strict GUID + pathHint schema"))
        return false;
    if (!Expect(root["editor"]["previewModel"]["guid"]
            == "33333333-3333-4333-8333-333333333333"
        && root["editor"]["previewModel"]["pathHint"] == "Assets/Models/Hero.fbx",
        "Animator preview model editor settings were not saved canonically"))
        return false;

    AnimatorAssetData loaded;
    if (!Expect(VansAnimatorIO::Load(firstPath.string(), loaded)
        && loaded.graphs.size() == 1 && loaded.layers.size() == 1
        && loaded.FindGraph("graph-base")
        && loaded.editor.previewModelGuid == "33333333-3333-4333-8333-333333333333",
        "Canonical animator did not load"))
        return false;
    AnimatorAssetData decodedDocument;
    std::string decodeError;
    if (!Expect(VansAnimatorIO::DeserializeFromJsonObject(root, decodedDocument, decodeError)
        && decodedDocument.clipRefs.size() == 2
        && decodedDocument.clipRefs[0].assetGuid == "22222222-2222-4222-8222-222222222222",
        "Canonical Animator document snapshot did not decode through the shared schema"))
        return false;

    std::string peekName;
    uint32_t stateCount = 99;
    uint32_t parameterCount = 0;
    if (!Expect(VansAnimatorIO::Peek(firstPath.string(), peekName, stateCount, parameterCount)
        && peekName == "CanonicalAnimator" && stateCount == 0 && parameterCount == 2,
        "Canonical animator metadata peek changed"))
        return false;

    nlohmann::json forbidden = root;
    forbidden["version"] = 1;
    const fs::path forbiddenPath = temporary.path / "forbidden_generation_field.vanimator";
    {
        std::ofstream output(forbiddenPath, std::ios::binary);
        output << forbidden.dump(2);
    }
    loaded.name = "stale";
    loaded.parameters.push_back({});
    if (!Expect(!VansAnimatorIO::Load(forbiddenPath.string(), loaded)
        && loaded.name.empty() && loaded.parameters.empty() && loaded.graphs.empty() && loaded.layers.empty(),
        "Animator loader accepted a generation field or retained stale output"))
        return false;

    nlohmann::json invalidStateMachine = root;
    invalidStateMachine["graphs"][0]["graph"]["nodes"].push_back({
        { "id", 99 }, { "type", "StateMachine" }, { "name", "Invalid State Machine" },
        { "posX", 0.0f }, { "posY", 0.0f },
        { "properties", { { "defaultState", "Missing" }, { "states", nlohmann::json::array() },
            { "transitions", nlohmann::json::array() } } }
    });
    if (!Expect(!VansAnimatorIO::DeserializeFromJsonObject(
        invalidStateMachine, decodedDocument, decodeError),
        "Animator accepted an invalid embedded State Machine definition"))
        return false;

    nlohmann::json unboundSlot = root;
    unboundSlot["graphs"][0]["graph"]["nodes"].push_back({
        { "id", 100 }, { "type", "Slot" }, { "name", "Unbound Slot" },
        { "posX", 0.0f }, { "posY", 0.0f },
        { "properties", { { "slotId", "slot-unbound" }, { "enableFallbackInput", false } } }
    });
    if (!Expect(!VansAnimatorIO::DeserializeFromJsonObject(unboundSlot, decodedDocument, decodeError),
        "Animator accepted a Slot node without a matching Slot definition"))
        return false;

    nlohmann::json missingGraph = root;
    missingGraph.erase("graphs");
    const fs::path missingGraphPath = temporary.path / "missing_graph.vanimator";
    {
        std::ofstream output(missingGraphPath, std::ios::binary);
        output << missingGraph.dump(2);
    }
    return Expect(!VansAnimatorIO::Load(missingGraphPath.string(), loaded),
        "Animator loader accepted an asset without Graph definitions");
}

bool TestAnimationGraphLinkValidationContract()
{
    using namespace VansGraphics;

    VansAnimGraph graph;
    const int firstId = graph.AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::SpeedScale));
    const int secondId = graph.AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::SpeedScale));
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(firstId > 0 && secondId > 0 && outputId > 0,
        "Failed to build graph validation fixture"))
        return false;
    if (!Expect(graph.AddLink(firstId, 0, secondId, 0) > 0,
        "Valid animation graph link was rejected"))
        return false;
    if (!Expect(graph.AddLink(secondId, 0, firstId, 0) < 0,
        "Animation graph cycle was accepted"))
        return false;
    if (!Expect(graph.AddLink(9999, 0, outputId, 0) < 0,
        "Animation graph link from a missing node was accepted"))
        return false;
    if (!Expect(graph.AddLink(secondId, 99, outputId, 0) < 0,
        "Animation graph link from a missing pin was accepted"))
        return false;
    if (!Expect(graph.AddLink(secondId, 0, outputId, 0) > 0,
        "Valid output link was rejected"))
        return false;

    nlohmann::json serialized;
    graph.SerializeToJsonObject(serialized);
    serialized["nodes"][0]["type"] = "UnknownNode";
    return Expect(!VansAnimGraph::DeserializeFromJsonObject(serialized),
        "Animation graph deserializer accepted an unknown node type");
}

bool TestAnimationPoseMathContract()
{
    using namespace VansGraphics;

    VansBoneTransform first;
    first.translation = glm::vec3(0.0f);
    first.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    first.scale = glm::vec3(1.0f);

    VansBoneTransform second;
    second.translation = glm::vec3(10.0f, 2.0f, -4.0f);
    second.rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    second.scale = glm::vec3(3.0f, 2.0f, 0.5f);

    const glm::mat4 blendedMatrix = VansPoseMath::BlendTransforms(
        VansPoseMath::Compose(first), VansPoseMath::Compose(second), 0.5f);
    VansBoneTransform blended;
    if (!Expect(VansPoseMath::TryDecompose(blendedMatrix, blended),
        "TRS blend produced a non-decomposable transform"))
        return false;
    if (!ExpectNear(blended.translation.x, 5.0f, 0.0001f,
        "TRS blend translation changed"))
        return false;
    if (!ExpectNear(blended.scale.x, 2.0f, 0.0001f,
        "TRS blend scale changed"))
        return false;
    if (!ExpectNear(glm::length(blended.rotation), 1.0f, 0.0001f,
        "TRS blend rotation is not normalized"))
        return false;

    VansBoneTransform base;
    base.translation = glm::vec3(1.0f, 0.0f, 0.0f);
    base.scale = glm::vec3(2.0f);
    VansBoneTransform additive;
    additive.translation = glm::vec3(2.0f, 0.0f, 0.0f);
    additive.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    additive.scale = glm::vec3(1.5f);

    VansBoneTransform additiveResult;
    if (!Expect(VansPoseMath::TryDecompose(
        VansPoseMath::ApplyAdditiveTransform(
            VansPoseMath::Compose(base), VansPoseMath::Compose(additive), 0.5f),
        additiveResult),
        "TRS additive blend produced a non-decomposable transform"))
        return false;
    if (!ExpectNear(additiveResult.translation.x, 2.0f, 0.0001f,
        "TRS additive translation changed"))
        return false;
    if (!ExpectNear(additiveResult.scale.x, 2.5f, 0.0001f,
        "TRS additive scale ratio changed"))
        return false;

    glm::mat4 invalid(1.0f);
    invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
    const glm::mat4 fallback = VansPoseMath::BlendTransforms(
        VansPoseMath::Compose(first), invalid, 0.25f);
    return Expect(std::isfinite(fallback[0][0]) && std::fabs(fallback[3].x) <= 0.0001f,
        "Invalid TRS input did not use the deterministic endpoint fallback");
}

bool TestAnimationGraphSharedSubgraphCacheContract()
{
    using namespace VansGraphics;

    class CountingPoseNode final : public VansAnimGraphNode
    {
    public:
        explicit CountingPoseNode(int& evaluations)
            : m_Evaluations(evaluations)
        {
            m_Type = AnimGraphNodeType::Clip;
            m_Name = "CountingPose";
        }

        std::vector<AnimGraphPin> GetPins() const override
        {
            return { { 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output } };
        }

        AnimGraphPose Evaluate(const AnimGraphContext&, VansAnimGraphInstance&) const override
        {
            ++m_Evaluations;
            AnimGraphPose pose;
            pose.localPose.push_back(VansBoneTransform{});
            pose.valid = true;
            return pose;
        }

    private:
        int& m_Evaluations;
    };

    int evaluations = 0;
    VansAnimGraph graph;
    const int sourceId = graph.AddNode(std::make_unique<CountingPoseNode>(evaluations));
    auto blend = std::make_unique<AnimGraphBlendNode>();
    blend->m_UseParam = false;
    blend->m_FixedAlpha = 0.5f;
    const int blendId = graph.AddNode(std::move(blend));
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(graph.AddLink(sourceId, 0, blendId, 0) > 0
        && graph.AddLink(sourceId, 0, blendId, 1) > 0
        && graph.AddLink(blendId, 0, outputId, 0) > 0,
        "Failed to build shared-subgraph cache fixture"))
        return false;

    VansAnimGraphInstance instance(graph);
    const AnimGraphPose result = instance.Evaluate({});
    if (!Expect(result.valid && evaluations == 1,
        "Shared animation subgraph was evaluated more than once in one frame"))
        return false;
    instance.Evaluate({});
    return Expect(evaluations == 2,
        "Animation graph evaluation cache was not reset for the next frame");
}

bool TestAnimationProjectAnimatorAssetsCanonicalContract()
{
    using namespace VansGraphics;

    fs::path workspace = fs::current_path();
    for (int depth = 0; depth < 5 && !fs::exists(workspace / "AnimationV2Project"); ++depth)
    {
        if (!workspace.has_parent_path() || workspace.parent_path() == workspace)
            break;
        workspace = workspace.parent_path();
    }
    if (!fs::exists(workspace / "AnimationV2Project"))
        return true;

    const std::vector<std::string> projectNames = {
        "AnimationV2Project", "DemoHallProject", "TestV2Project", "SponzaProject"
    };
    for (const std::string& projectName : projectNames)
    {
        const fs::path projectRoot = workspace / projectName;
        if (!fs::exists(projectRoot))
            continue;

        std::size_t animatorCount = 0;
        std::error_code error;
        for (fs::recursive_directory_iterator iterator(projectRoot, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            if (!iterator->is_regular_file() || iterator->path().extension() != ".vanimator")
                continue;
            ++animatorCount;
            AnimatorAssetData asset;
            if (!Expect(VansAnimatorIO::Load(iterator->path().string(), asset),
                "Project Animator asset failed canonical loading"))
                return false;
            for (const AnimatorGraphAsset& graph : asset.graphs)
            {
                VansAnimGraphInstance instance(*graph.graph);
                if (!Expect(instance.IsCompiled(),
                    "Project Animator Graph failed execution-plan compilation"))
                    return false;
            }
            if (!Expect(!asset.layers.empty()
                && asset.layers.front().kind == VansAnimationLayerKind::Base,
                "Project Animator is missing its canonical Base Layer"))
                return false;
            if (iterator->path().string().find("\\Builds\\") == std::string::npos)
            {
                Vans::VansAssetGuid previewGuid;
                const fs::path previewPath = projectRoot / asset.editor.previewModelPathHint;
                if (!Expect(Vans::VansAssetGuid::TryParse(asset.editor.previewModelGuid, previewGuid)
                    && !asset.editor.previewModelPathHint.empty()
                    && fs::exists(previewPath) && fs::exists(previewPath.string() + ".meta"),
                    "Project Animator preview model is missing or not project-local"))
                    return false;
                nlohmann::json previewMeta;
                std::ifstream previewMetaInput(previewPath.string() + ".meta");
                previewMetaInput >> previewMeta;
                if (!Expect(previewMeta.value("guid", "") == asset.editor.previewModelGuid,
                    "Project Animator preview model GUID/pathHint pair does not resolve locally"))
                    return false;
            }
        }
        if (!Expect(!error, "Failed while scanning project Animator assets"))
            return false;
        if (!Expect(animatorCount > 0, "No project Animator assets were validated"))
            return false;

        Vans::VansAssetDatabase database(projectRoot / "Assets", projectRoot / "Library" / "Artifacts");
        const Vans::VansAssetOperationPolicy readOnly = Vans::VansAssetOperationPolicy::ReadOnly();
        error.clear();
        for (fs::recursive_directory_iterator iterator(projectRoot / "Assets", error), end;
             !error && iterator != end; iterator.increment(error))
        {
            if (!iterator->is_regular_file())
                continue;
            const Vans::VansAssetType type = Vans::VansAssetDatabase::Classify(iterator->path());
            const bool animationAsset = type == Vans::VansAssetType::AnimationClip
                || type == Vans::VansAssetType::AnimatorController
                || type == Vans::VansAssetType::BoneMask;
            const bool timelineAsset = type == Vans::VansAssetType::Timeline
                || type == Vans::VansAssetType::Audio
                || type == Vans::VansAssetType::Video
                || type == Vans::VansAssetType::Particle;
            const bool retargetSourceModel = type == Vans::VansAssetType::Model
                && iterator->path().filename() == "SKM_UEFN_Mannequin.fbx";
            if (!animationAsset && !timelineAsset && !retargetSourceModel)
                continue;
            std::string registerError;
            if (!Expect(database.RegisterOrRefresh(iterator->path(), readOnly, registerError),
                "Animation or Timeline dependency asset failed read-only registration"))
                return false;
        }
        if (!Expect(!error, "Failed while registering project animation and Timeline assets"))
            return false;

        for (const Vans::VansAssetRecord& record : database.All())
        {
			if (record.type == Vans::VansAssetType::AnimationClip)
			{
				VansAnimationClipInfo clipInfo;
				if (!Expect(VansAnimationClipIO::Peek(record.sourcePath.string(), clipInfo),
					"Project Animation Clip is not self-contained canonical form"))
					return false;
				continue;
			}
            if (record.type == Vans::VansAssetType::Timeline)
            {
                Vans::VansTimelineAsset timeline;
                std::string timelineError;
                if (!Expect(Vans::VansTimelineSerialization::Load(
                    record.sourcePath, timeline, timelineError),
                    "Project Timeline failed canonical loading"))
                    return false;
                Vans::VansTimelineValidationContext validation;
                validation.runtimeValidation = false;
                const Vans::VansTimelineDiagnostics diagnostics =
                    Vans::VansTimelineValidator::Validate(timeline, validation);
                for (const Vans::VansTimelineDiagnostic& diagnostic : diagnostics)
                {
                    if (diagnostic.severity != Vans::VansTimelineDiagnosticSeverity::Error)
                        continue;
                    std::cerr << "[ProjectTimeline] " << record.sourcePath.string() << " :: "
                        << diagnostic.objectId << "." << diagnostic.propertyPath << " :: "
                        << diagnostic.message << std::endl;
                }
                if (!Expect(!Vans::VansTimelineValidator::HasErrors(diagnostics),
                    "Project Timeline failed authoring validation"))
                    return false;
                continue;
            }
            if (record.type != Vans::VansAssetType::AnimatorController)
                continue;
            AnimatorAssetData animator;
            if (!Expect(VansAnimatorIO::Load(record.sourcePath.string(), animator),
                "Registered Animator failed canonical loading"))
                return false;
            for (const AnimatorClipRef& clip : animator.clipRefs)
            {
                Vans::VansAssetGuid guid;
                const auto dependency = Vans::VansAssetGuid::TryParse(clip.assetGuid, guid)
                    ? database.Find(guid) : std::optional<Vans::VansAssetRecord>{};
                if (!Expect(dependency && dependency->type == Vans::VansAssetType::AnimationClip,
                    "Animator Clip GUID does not resolve inside its project"))
                    return false;
            }
            for (const VansAnimationLayerDefinition& layer : animator.layers)
            {
                if (layer.kind != VansAnimationLayerKind::Overlay)
                    continue;
                Vans::VansAssetGuid guid;
                const auto dependency = Vans::VansAssetGuid::TryParse(layer.maskGuid, guid)
                    ? database.Find(guid) : std::optional<Vans::VansAssetRecord>{};
                if (!Expect(dependency && dependency->type == Vans::VansAssetType::BoneMask,
                    "Animator Bone Mask GUID does not resolve inside its project"))
                    return false;
            }
        }

        const fs::path scenesRoot = projectRoot / "Scenes";
        if (!fs::exists(scenesRoot))
            continue;
        error.clear();
        for (fs::recursive_directory_iterator iterator(scenesRoot, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            if (!iterator->is_regular_file() || iterator->path().extension() != ".json")
                continue;
            const Vans::VansSceneAssetDependencyBuildResult dependencies =
                Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(database, iterator->path(), {});
            if (!Expect(dependencies.success,
                "Project scene animation or Timeline dependency closure is incomplete or contains path/cross-project references"))
                return false;
        }
        if (!Expect(!error, "Failed while validating project scene animation dependencies"))
            return false;
    }

    const fs::path layeredAnimatorPath = workspace / "AnimationV2Project" / "Assets"
        / "MotionMatchDataBase" / "UEFN_Mannequin.vanimator";
    AnimatorAssetData layeredAnimator;
    if (!Expect(VansAnimatorIO::Load(layeredAnimatorPath.string(), layeredAnimator)
        && layeredAnimator.layers.size() == 2
        && layeredAnimator.layers[1].kind == VansAnimationLayerKind::Overlay
        && layeredAnimator.layers[1].maskGuid == "7f7ccc4a-58c6-450a-9ffe-2c1ed7a5b029"
        && layeredAnimator.layers[1].useWeightParameter
        && layeredAnimator.layers[1].weightParameter == "UpperBodyWeight"
        && layeredAnimator.slots.size() == 1
        && layeredAnimator.slots[0].layerId == layeredAnimator.layers[1].id,
        "AnimationV2 layered Motion Matching/Retarget fixture is incomplete"))
        return false;
    VansBoneMaskAsset projectMask;
    std::string maskError;
    if (!Expect(VansBoneMaskStorage::Load(
        workspace / "AnimationV2Project" / "Assets" / "MotionMatchDataBase"
            / "UEFN_UpperBody.vbonemask",
        projectMask, maskError)
        && projectMask.previewSkeletonGuid == layeredAnimator.editor.previewModelGuid
        && projectMask.branchRules.size() == 1
        && projectMask.branchRules.front().rootBone == "spine_01",
        "AnimationV2 layered fixture Bone Mask is not canonical or skeleton-bound"))
        return false;
    return true;
}

bool TestAnimationAuthoringBoundaryContract()
{
    fs::path sourceFile = fs::path(__FILE__);
    if (sourceFile.is_relative())
        sourceFile = fs::absolute(sourceFile);
    const fs::path engineRoot = sourceFile.parent_path().parent_path().parent_path();
    const fs::path editorRoot = engineRoot / "Source" / "EngineCore" / "EditorCore";
    std::error_code scanError;
    for (fs::recursive_directory_iterator iterator(editorRoot, scanError), end;
         !scanError && iterator != end; iterator.increment(scanError))
    {
        if (!iterator->is_regular_file())
            continue;
        const std::string extension = iterator->path().extension().string();
        if (extension != ".cpp" && extension != ".h" && extension != ".hpp")
            continue;
        std::ifstream input(iterator->path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        if (!Expect(text.find("AnimationCore/") == std::string::npos
            && text.find("AnimationCore\\") == std::string::npos,
            "EditorCore directly depends on AnimationCore instead of IEngineEditorAPI DTOs"))
            return false;
    }
    if (!Expect(!scanError, "Unable to scan EditorCore animation architecture boundary"))
        return false;

    fs::path workspace = engineRoot.parent_path().parent_path();
    const fs::path animatorPath = workspace / "AnimationV2Project" / "Assets"
        / "MotionMatchDataBase" / "UEFN_Mannequin.vanimator";
    const fs::path maskPath = workspace / "AnimationV2Project" / "Assets"
        / "MotionMatchDataBase" / "UEFN_UpperBody.vbonemask";
    if (!fs::exists(animatorPath) || !fs::exists(maskPath))
        return true;

    auto readText = [](const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    };
    const std::string animatorText = readText(animatorPath);
    auto animator = Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(animatorText);
    if (!Expect(animator.success && animator.document,
        "Public Animator authoring DTO failed to decode the AnimationV2 fixture"))
        return false;
    const auto encodedAnimator = Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(*animator.document);
    if (!Expect(encodedAnimator.success,
        "Public Animator authoring DTO failed canonical re-encoding"))
        return false;
    const nlohmann::json originalAnimator = nlohmann::json::parse(animatorText);
    VansGraphics::AnimatorAssetData nativeAnimator;
    std::string nativeError;
    if (!Expect(VansGraphics::VansAnimatorIO::DeserializeFromJsonObject(
        originalAnimator, nativeAnimator, nativeError), nativeError.c_str()))
        return false;
    nlohmann::json nativeCanonicalAnimator;
    if (!Expect(VansGraphics::VansAnimatorIO::SerializeToJsonObject(
        nativeAnimator, nativeCanonicalAnimator, nativeError), nativeError.c_str()))
        return false;
    const nlohmann::json roundTrippedAnimator = nlohmann::json::parse(encodedAnimator.canonicalJson);
    if (nativeCanonicalAnimator != roundTrippedAnimator)
    {
        std::function<bool(const nlohmann::json&, const nlohmann::json&, const std::string&)> reportDifference;
        reportDifference = [&](const auto& before, const auto& after, const std::string& path)
        {
            if (before.type() != after.type())
            {
                std::cerr << "[ForestContractTests] Animator DTO type mismatch at " << path << '\n';
                return true;
            }
            if (before.is_object())
            {
                for (auto item = before.begin(); item != before.end(); ++item)
                {
                    if (!after.contains(item.key()))
                    {
                        std::cerr << "[ForestContractTests] Animator DTO dropped " << path << '/' << item.key() << '\n';
                        return true;
                    }
                    if (reportDifference(item.value(), after.at(item.key()), path + '/' + item.key())) return true;
                }
                for (auto item = after.begin(); item != after.end(); ++item)
                    if (!before.contains(item.key()))
                    {
                        std::cerr << "[ForestContractTests] Animator DTO added " << path << '/' << item.key() << '\n';
                        return true;
                    }
                return false;
            }
            if (before.is_array())
            {
                if (before.size() != after.size())
                {
                    std::cerr << "[ForestContractTests] Animator DTO array-size mismatch at " << path << '\n';
                    return true;
                }
                for (std::size_t index = 0; index < before.size(); ++index)
                    if (reportDifference(before[index], after[index], path + '/' + std::to_string(index))) return true;
                return false;
            }
            if (before != after)
            {
                std::cerr << "[ForestContractTests] Animator DTO value mismatch at " << path
                          << ": " << before.dump() << " != " << after.dump() << '\n';
                return true;
            }
            return false;
        };
        reportDifference(nativeCanonicalAnimator, roundTrippedAnimator, "$");
        return Expect(false, "Animator DTO bridge changed canonical authoring data during round trip");
    }

    const std::string maskText = readText(maskPath);
    auto mask = Vans::EditorAPI::AnimationAuthoringBridge::DecodeBoneMask(maskText);
    if (!Expect(mask.success, "Public Bone Mask authoring DTO failed to decode the AnimationV2 fixture"))
        return false;
    const auto encodedMask = Vans::EditorAPI::AnimationAuthoringBridge::EncodeBoneMask(mask.document);
    if (!Expect(encodedMask.success
        && nlohmann::json::parse(maskText) == nlohmann::json::parse(encodedMask.canonicalJson),
        "Bone Mask DTO bridge changed canonical authoring data during round trip"))
        return false;

    Vans::EditorAPI::AssetSkeletonSnapshot skeleton;
    skeleton.available = true;
    skeleton.bones = {
        { "root", -1, {} }, { "pelvis", 0, {} },
        { "spine_01", 1, {} }, { "spine_02", 2, {} }
    };
    const auto compiled = Vans::EditorAPI::AnimationAuthoringBridge::CompileBoneMask(mask.document, skeleton);
    return Expect(compiled.valid && compiled.weights.size() == skeleton.bones.size()
        && compiled.weights[2] > 0.99f && compiled.weights[3] > 0.99f,
        "Bone Mask DTO compilation did not preserve branch-rule semantics");
}

bool TestAnimationGraphAdvancesOnlyActiveNodesContract()
{
    using namespace VansGraphics;

    auto activeClip = std::make_unique<AnimGraphClipNode>();
    activeClip->m_ClipName = "Active";
    auto inactiveClip = std::make_unique<AnimGraphClipNode>();
    inactiveClip->m_ClipName = "Inactive";
    auto condition = std::make_unique<AnimGraphIfConditionNode>();
    condition->m_ParamName = "ChooseActive";
    condition->m_CompareOp = CompareOp::Equal;
    condition->m_BoolVal = true;

    VansAnimGraph graph;
    const int activeId = graph.AddNode(std::move(activeClip));
    const int inactiveId = graph.AddNode(std::move(inactiveClip));
    const int conditionId = graph.AddNode(std::move(condition));
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(graph.AddLink(activeId, 0, conditionId, 0) > 0
        && graph.AddLink(inactiveId, 0, conditionId, 1) > 0
        && graph.AddLink(conditionId, 0, outputId, 0) > 0,
        "Failed to build active-node advancement fixture"))
        return false;

    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].localTransform = glm::mat4(1.0f);
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips["Active"] = BuildContractClip("Active", 0.0f, 0.0f);
    clips["Inactive"] = BuildContractClip("Inactive", 0.0f, 0.0f);
    std::unordered_map<std::string, AnimatorParameter> parameters;
    parameters["ChooseActive"] = { "ChooseActive", AnimatorParamType::Bool };
    parameters["ChooseActive"].boolVal = true;

    AnimGraphContext context;
    context.skeleton = &skeleton;
    context.clips = &clips;
    context.parameters = &parameters;

    VansAnimGraphInstance instance(graph);
    instance.AdvanceTime(0.25f, context);
    instance.Evaluate(context);
    instance.AdvanceTime(0.25f, context);
    if (!ExpectNear(instance.GetClipTime(activeId), 0.25f, 0.0001f,
        "Active animation graph node did not advance"))
        return false;
    return ExpectNear(instance.GetClipTime(inactiveId), 0.0f, 0.0001f,
        "Inactive animation graph node advanced unexpectedly");
}

bool TestAnimationGraphDefinitionInstanceIsolationContract()
{
    using namespace VansGraphics;

    auto clip = std::make_unique<AnimGraphClipNode>();
    clip->m_ClipName = "Walk";
    const int unusedBeforeAdd = clip->GetNodeId();
    VansAnimGraph definition;
    const int clipId = definition.AddNode(std::move(clip));
    const int outputId = definition.AddNode(
        VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(unusedBeforeAdd < 0 && clipId > 0 && outputId > 0
        && definition.AddLink(clipId, 0, outputId, 0) > 0,
        "Failed to build Definition/Instance isolation fixture"))
        return false;

    std::vector<int> executionPlan;
    std::string compileError;
    if (!Expect(definition.BuildExecutionPlan(executionPlan, compileError)
        && executionPlan.size() == 2
        && executionPlan.front() == clipId
        && executionPlan.back() == outputId,
        "Animation graph execution plan is not deterministic source-to-output order"))
        return false;

    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].localTransform = glm::mat4(1.0f);
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips["Walk"] = BuildContractClip("Walk", 0.0f, 1.0f);
    std::unordered_map<std::string, AnimatorParameter> parameters;
    AnimGraphContext context;
    context.skeleton = &skeleton;
    context.clips = &clips;
    context.parameters = &parameters;

    VansAnimGraphInstance first(definition);
    VansAnimGraphInstance second(definition);
    if (!Expect(first.IsCompiled() && second.IsCompiled(),
        "Two instances could not compile the same graph definition"))
        return false;
    first.Evaluate(context);
    second.Evaluate(context);
    first.AdvanceTime(0.25f, context);
    second.AdvanceTime(0.75f, context);
    if (!ExpectNear(first.GetClipTime(clipId), 0.25f, 0.0001f,
        "First animation graph instance lost its independent time"))
        return false;
    if (!ExpectNear(second.GetClipTime(clipId), 0.75f, 0.0001f,
        "Second animation graph instance was contaminated by the first"))
        return false;

    first.Reset();
    if (!ExpectNear(first.GetClipTime(clipId), 0.0f, 0.0001f,
        "Reset did not clear only the target graph instance"))
        return false;
    return ExpectNear(second.GetClipTime(clipId), 0.75f, 0.0001f,
        "Reset of one graph instance changed another instance");
}

bool TestAnimationPayloadIntervalSamplingContract()
{
    using namespace VansGraphics;

    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].id = 0;
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[0].localTransform = glm::mat4(1.0f);
    skeleton.boneNameToIndex["root"] = 0;

    VansAnimationClip clip;
    clip.stableId = 1001;
    clip.clipName = "PayloadContract";
    clip.duration = 1.0f;
    clip.boneKeyframes.resize(1);
    clip.boneKeyframes[0] = {
        { 0.0f, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) },
        { 1.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) }
    };
    clip.curves.push_back({ 2001, "Stride", { { 0.0f, 0.0f }, { 1.0f, 1.0f } } });
    clip.events.push_back({ 3001, 0.05f, "Early", std::int64_t(7) });
    clip.events.push_back({ 3002, 0.95f, "Late", std::string("payload") });
    clip.syncGroupName = "Locomotion";
    clip.syncMarkers.push_back({ 4001, 0.0f, "Left" });
    clip.syncMarkers.push_back({ 4002, 0.5f, "Right" });

    auto sample = [&](float previous, float current)
    {
        VansAnimationSampleRequest request;
        request.previousTime = previous;
        request.currentTime = current;
        request.loop = true;
        request.sourceNodeId = 77;
        VansPosePayload payload;
        VansAnimationSampler::Sample(clip, skeleton, request, payload);
        return payload;
    };

    const VansPosePayload wrapped = sample(0.9f, 1.1f);
    if (!Expect(wrapped.rootMotion.valid
        && std::fabs(wrapped.rootMotion.translation.x - 2.0f) <= 0.0001f,
        "Root motion loop wrap did not produce a continuous interval delta"))
        return false;
    if (!Expect(wrapped.events.size() == 2
        && wrapped.events[0].name == "Late" && wrapped.events[0].loopIndex == 0
        && wrapped.events[1].name == "Early" && wrapped.events[1].loopIndex == 1,
        "Loop-wrap events were not emitted exactly once in traversal order"))
        return false;
    if (!Expect(wrapped.events[0].sourceNodeId == 77 && wrapped.events[0].clipId == 1001,
        "Animation event payload lost its stable source identity"))
        return false;

    const VansPosePayload multipleWraps = sample(0.25f, 2.25f);
    if (!Expect(std::fabs(multipleWraps.rootMotion.translation.x - 20.0f) <= 0.0001f,
        "Root motion did not accumulate multiple loop traversals"))
        return false;
    const VansPosePayload reverse = sample(1.1f, 0.9f);
    if (!Expect(std::fabs(reverse.rootMotion.translation.x + 2.0f) <= 0.0001f,
        "Reverse playback did not invert root motion"))
        return false;
    if (!Expect(reverse.events.size() == 2
        && reverse.events[0].name == "Early" && reverse.events[1].name == "Late",
        "Reverse playback events were not emitted in reverse traversal order"))
        return false;

    const VansPosePayload middle = sample(0.25f, 0.5f);
    if (!Expect(middle.curves.size() == 1 && middle.curves[0].present
        && std::fabs(middle.curves[0].value - 0.5f) <= 0.0001f,
        "Animation curve was not sampled with presence semantics"))
        return false;
    if (!Expect(middle.sync.valid && middle.sync.markerId == 4002
        && middle.sync.nextMarkerId == 4001,
        "Animation sync marker interval was not sampled"))
        return false;

    VansPosePayload noCurves = middle;
    noCurves.curves.clear();
    noCurves.events.clear();
    const VansPosePayload blended = VansPosePayloadMixer::BlendOverride(middle, noCurves, 1.0f);
    if (!Expect(blended.curves.size() == 1 && blended.curves[0].present
        && std::fabs(blended.curves[0].value - 0.5f) <= 0.0001f,
        "Missing curve input was incorrectly treated as a zero-valued curve"))
        return false;

    const VansPosePayload deduplicated = VansPosePayloadMixer::BlendOverride(wrapped, wrapped, 0.5f);
    return Expect(deduplicated.events.size() == wrapped.events.size(),
        "Shared payload event occurrences were duplicated by blending");
}

bool TestAnimationClipPayloadMetadataRoundTripContract()
{
    using namespace VansGraphics;

    TemporaryDirectory temporary;
    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].id = 0;
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[0].localTransform = glm::mat4(1.0f);
    skeleton.boneNameToIndex["root"] = 0;
    skeleton.BuildTopologicalOrder();

    VansAnimationClip source;
    source.stableId = 901;
    source.clipName = "Metadata";
    source.duration = 1.0f;
    source.boneKeyframes.resize(1);
    source.boneKeyframes[0].push_back({ 0.0f, glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
    source.curves.push_back({ 902, "Aim", { { 0.0f, 0.25f }, { 1.0f, 0.75f } } });
    source.events.push_back({ 903, 0.4f, "Commit", glm::vec3(1.0f, 2.0f, 3.0f) });
    source.syncGroupName = "Action";
    source.syncMarkers.push_back({ 904, 0.2f, "Begin" });
    source.rootMotion.enabled = true;
    source.rootMotion.boneName = "root";
    source.rootMotion.extractTranslation = true;
    source.rootMotion.extractRotation = false;
    source.rootMotion.extractScale = false;

    const fs::path path = temporary.path / "payload_metadata.vclip";
    if (!Expect(VansAnimationClipIO::Save(path.string(), source, skeleton),
        "Animation clip payload metadata save failed"))
        return false;

    VansAnimationClip loaded;
    Skeleton loadedSkeleton;
    if (!Expect(VansAnimationClipIO::Load(path.string(), loaded, loadedSkeleton),
        "Animation clip payload metadata load failed"))
        return false;
    if (!Expect(loaded.stableId == 901 && loaded.curves.size() == 1
        && loaded.curves[0].id == 902 && loaded.events.size() == 1
        && loaded.events[0].id == 903 && loaded.syncMarkers.size() == 1
        && loaded.syncMarkers[0].id == 904,
        "Animation clip payload metadata identities changed during round trip"))
        return false;
    if (!Expect(std::holds_alternative<glm::vec3>(loaded.events[0].payload)
        && std::get<glm::vec3>(loaded.events[0].payload) == glm::vec3(1.0f, 2.0f, 3.0f),
        "Animation event data payload changed during round trip"))
        return false;
    if (!Expect(loaded.syncGroupName == "Action" && loaded.rootMotion.boneName == "root"
        && loaded.rootMotion.extractTranslation && !loaded.rootMotion.extractRotation,
        "Animation sync or root-motion metadata changed during round trip"))
        return false;

    VansAnimationClipInfo info;
    return Expect(VansAnimationClipIO::Peek(path.string(), info)
        && info.curveCount == 1 && info.eventCount == 1 && info.syncMarkerCount == 1,
        "Animation clip payload metadata peek counts changed");
}

bool TestAnimationSpeedScaleContract()
{
    using namespace VansGraphics;

    auto clipNode = std::make_unique<AnimGraphClipNode>();
    clipNode->m_ClipName = "Walk";
    auto speedNode = std::make_unique<AnimGraphSpeedScaleNode>();
    speedNode->m_FixedSpeed = 2.0f;
    speedNode->m_UseParam = false;
    VansAnimGraph graph;
    const int clipId = graph.AddNode(std::move(clipNode));
    const int speedId = graph.AddNode(std::move(speedNode));
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(graph.AddLink(clipId, 0, speedId, 0) > 0
        && graph.AddLink(speedId, 0, outputId, 0) > 0,
        "Failed to build SpeedScale graph fixture"))
        return false;

    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[0].localTransform = glm::mat4(1.0f);
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips["Walk"] = BuildContractClip("Walk", 0.0f, 1.0f);
    std::unordered_map<std::string, AnimatorParameter> parameters;
    AnimGraphContext context;
    context.skeleton = &skeleton;
    context.clips = &clips;
    context.parameters = &parameters;

    VansAnimGraphInstance instance(graph);
    if (!Expect(instance.IsCompiled(), "Valid SpeedScale graph did not compile"))
        return false;
    instance.Evaluate(context);
    instance.AdvanceTime(0.25f, context);
    if (!ExpectNear(instance.GetClipTime(clipId), 0.5f, 0.0001f,
        "SpeedScale did not propagate to the active clip clock"))
        return false;

    auto sharedClip = std::make_unique<AnimGraphClipNode>();
    sharedClip->m_ClipName = "Walk";
    auto conflictingSpeed = std::make_unique<AnimGraphSpeedScaleNode>();
    auto blend = std::make_unique<AnimGraphBlendNode>();
    blend->m_UseParam = false;
    VansAnimGraph conflicting;
    const int sharedClipId = conflicting.AddNode(std::move(sharedClip));
    const int conflictingSpeedId = conflicting.AddNode(std::move(conflictingSpeed));
    const int blendId = conflicting.AddNode(std::move(blend));
    const int conflictingOutputId = conflicting.AddNode(
        VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    if (!Expect(conflicting.AddLink(sharedClipId, 0, conflictingSpeedId, 0) > 0
        && conflicting.AddLink(conflictingSpeedId, 0, blendId, 0) > 0
        && conflicting.AddLink(sharedClipId, 0, blendId, 1) > 0
        && conflicting.AddLink(blendId, 0, conflictingOutputId, 0) > 0,
        "Failed to build conflicting SpeedScale fixture"))
        return false;
    VansAnimGraphInstance rejected(conflicting);
    return Expect(!rejected.IsCompiled()
        && rejected.GetCompileError().find("conflicting SpeedScale paths") != std::string::npos,
        "Graph compiler accepted ambiguous playback speeds for one stateful source");
}

VansGraphics::Skeleton BuildLayerContractSkeleton()
{
    using namespace VansGraphics;
    Skeleton skeleton;
    skeleton.bones.resize(3);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].id = 0;
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[0].children = { 1 };
    skeleton.bones[1].name = "spine";
    skeleton.bones[1].id = 1;
    skeleton.bones[1].parentIndex = 0;
    skeleton.bones[1].children = { 2 };
    skeleton.bones[2].name = "arm";
    skeleton.bones[2].id = 2;
    skeleton.bones[2].parentIndex = 1;
    for (size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        skeleton.bones[index].localTransform = glm::mat4(1.0f);
        skeleton.bones[index].offsetMatrix = glm::mat4(1.0f);
        skeleton.boneNameToIndex[skeleton.bones[index].name] = static_cast<int>(index);
    }
    skeleton.BuildTopologicalOrder();
    return skeleton;
}

bool TestBoneMaskCompilationAndStorageContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildLayerContractSkeleton();
    VansBoneMaskAsset mask;
    mask.id = "mask-upper";
    mask.name = "Upper";
    mask.previewSkeletonGuid = "44444444-4444-4444-8444-444444444444";
    mask.previewSkeletonPathHint = "Assets/Models/Hero.fbx";
    mask.editorExpandedBones = { "root", "spine" };
    VansBoneMaskBranchRule include;
    include.id = "include-spine";
    include.mode = VansBoneMaskRuleMode::Include;
    include.rootBone = "spine";
    include.rootWeight = 0.25f;
    include.endWeight = 1.0f;
    include.falloff = VansBoneMaskFalloff::Linear;
    mask.branchRules.push_back(include);
    VansBoneMaskBranchRule exclude;
    exclude.id = "exclude-arm";
    exclude.mode = VansBoneMaskRuleMode::Exclude;
    exclude.rootBone = "arm";
    exclude.falloff = VansBoneMaskFalloff::Constant;
    mask.branchRules.push_back(exclude);
    mask.explicitWeights["arm"] = 0.75f;

    const VansCompiledBoneMask compiled = VansBoneMaskCompiler::Compile(mask, skeleton);
    if (!Expect(compiled.valid && compiled.weights.size() == 3,
        "Bone Mask did not compile for a compatible skeleton"))
        return false;
    if (!ExpectNear(compiled.weights[0], 0.0f, 0.0001f,
        "Bone Mask changed a bone outside its branch"))
        return false;
    if (!ExpectNear(compiled.weights[1], 0.25f, 0.0001f,
        "Bone Mask root falloff weight changed"))
        return false;
    if (!ExpectNear(compiled.weights[2], 0.75f, 0.0001f,
        "Explicit Bone Mask weight did not override include/exclude rules"))
        return false;

    VansBoneMaskAsset invalid = mask;
    invalid.branchRules.front().rootBone = "missing";
    if (!Expect(!VansBoneMaskCompiler::Compile(invalid, skeleton).valid,
        "Missing Include root did not invalidate Bone Mask compilation"))
        return false;

    TemporaryDirectory temporary;
    const fs::path path = temporary.path / "upper.vbonemask";
    std::string error;
    if (!Expect(VansBoneMaskStorage::SaveAtomic(path, mask, error), error.c_str()))
        return false;
    VansBoneMaskAsset loaded;
    if (!Expect(VansBoneMaskStorage::Load(path, loaded, error), error.c_str()))
        return false;
    if (!Expect(loaded.id == mask.id && loaded.branchRules.size() == 2
        && loaded.explicitWeights["arm"] == 0.75f
        && loaded.previewSkeletonGuid == mask.previewSkeletonGuid
        && loaded.editorExpandedBones == mask.editorExpandedBones,
        "Bone Mask canonical storage changed authored rules"))
        return false;

    nlohmann::json document;
    VansBoneMaskAsset decodedDocument;
    if (!Expect(VansBoneMaskStorage::SerializeToJsonObject(mask, document, error)
        && VansBoneMaskStorage::DeserializeFromJsonObject(document, decodedDocument, error)
        && document["editor"]["expandedBones"].size() == 2
        && decodedDocument.previewSkeletonGuid == mask.previewSkeletonGuid,
        "Bone Mask editor document did not use the canonical storage codec"))
        return false;

    VansBoneMaskAsset invalidWeight = mask;
    invalidWeight.explicitWeights["arm"] = 1.25f;
    if (!Expect(!VansBoneMaskStorage::SerializeToJsonObject(invalidWeight, document, error),
        "Bone Mask storage silently clamped an invalid authored weight"))
        return false;

    nlohmann::json invalidDocument;
    if (!Expect(VansBoneMaskStorage::SerializeToJsonObject(mask, invalidDocument, error), error.c_str()))
        return false;
    invalidDocument["previewSkeleton"]["unexpected"] = true;
    if (!Expect(!VansBoneMaskStorage::DeserializeFromJsonObject(invalidDocument, decodedDocument, error),
        "Bone Mask document accepted an unknown nested field"))
        return false;

    nlohmann::json forbidden;
    {
        std::ifstream input(path);
        input >> forbidden;
    }
    forbidden["version"] = 1;
    const fs::path forbiddenPath = temporary.path / "forbidden.vbonemask";
    {
        std::ofstream output(forbiddenPath);
        output << forbidden.dump(2);
    }
    return Expect(!VansBoneMaskStorage::Load(forbiddenPath, loaded, error),
        "Bone Mask loader accepted a forbidden generation field");
}

bool TestAnimatorRuntimeCompilerContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildLayerContractSkeleton();
    TemporaryDirectory temporary;
    auto makeClip = [](const std::string& name, float spineValue, float armValue)
    {
        VansAnimationClip clip;
        clip.clipName = name;
        clip.duration = 1.0f;
        clip.boneKeyframes.resize(3);
        const float values[] = { 0.0f, spineValue, armValue };
        for (int index = 0; index < 3; ++index)
        {
            clip.boneKeyframes[index].push_back({ 0.0f, glm::vec3(values[index], 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
            clip.boneKeyframes[index].push_back({ 1.0f, glm::vec3(values[index], 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        }
        return clip;
    };
    const fs::path basePath = temporary.path / "base.vclip";
    const fs::path overlayPath = temporary.path / "overlay.vclip";
    if (!Expect(VansAnimationClipIO::Save(basePath.string(), makeClip("Base", 0.0f, 0.0f), skeleton)
        && VansAnimationClipIO::Save(overlayPath.string(), makeClip("Overlay", 10.0f, 20.0f), skeleton),
        "Runtime compiler fixture clips failed to save"))
        return false;

    VansBoneMaskAsset mask;
    mask.id = "mask-upper";
    mask.name = "Upper";
    VansBoneMaskBranchRule maskRule;
    maskRule.id = "include-spine";
    maskRule.rootBone = "spine";
    maskRule.rootWeight = 1.0f;
    maskRule.endWeight = 1.0f;
    maskRule.falloff = VansBoneMaskFalloff::Constant;
    mask.branchRules.push_back(maskRule);
    const fs::path maskPath = temporary.path / "upper.vbonemask";
    std::string error;
    if (!Expect(VansBoneMaskStorage::SaveAtomic(maskPath, mask, error), error.c_str()))
        return false;

    AnimatorAssetData asset;
    asset.name = "RuntimeCompiler";
    AnimatorParameter upperWeight;
    upperWeight.name = "UpperBodyWeight";
    upperWeight.type = AnimatorParamType::Float;
    upperWeight.floatVal = 0.0f;
    asset.parameters.push_back(upperWeight);
    asset.clipRefs = {
        { "Base", "55555555-5555-4555-8555-555555555555", "base.vclip" },
        { "Overlay", "66666666-6666-4666-8666-666666666666", "overlay.vclip" }
    };

    auto makeClipGraph = [](const std::string& clipName, const std::string& slotId, int& slotNodeId)
    {
        auto graph = std::make_unique<VansAnimGraph>();
        auto clipNode = std::make_unique<AnimGraphClipNode>();
        clipNode->m_ClipName = clipName;
        const int clipNodeId = graph->AddNode(std::move(clipNode));
        int outputSourceId = clipNodeId;
        if (!slotId.empty())
        {
            auto slotNode = std::make_unique<AnimGraphSlotNode>();
            slotNode->m_SlotId = slotId;
            slotNode->m_EnableFallbackInput = true;
            slotNodeId = graph->AddNode(std::move(slotNode));
            graph->AddLink(clipNodeId, 0, slotNodeId, 0);
            outputSourceId = slotNodeId;
        }
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(outputSourceId, 0, outputId, 0);
        return graph;
    };
    int unusedSlotNodeId = -1;
    AnimatorGraphAsset baseGraph;
    baseGraph.id = "graph-base";
    baseGraph.name = "Base";
    baseGraph.graph = makeClipGraph("Base", {}, unusedSlotNodeId);
    asset.graphs.push_back(std::move(baseGraph));
    int slotNodeId = -1;
    AnimatorGraphAsset overlayGraph;
    overlayGraph.id = "graph-upper";
    overlayGraph.name = "Upper";
    overlayGraph.graph = makeClipGraph("Overlay", "slot-upper", slotNodeId);
    asset.graphs.push_back(std::move(overlayGraph));

    VansAnimationLayerDefinition baseLayer;
    baseLayer.id = "layer-base";
    baseLayer.name = "Base";
    baseLayer.graphId = "graph-base";
    baseLayer.kind = VansAnimationLayerKind::Base;
    baseLayer.rootMotion = VansLayerRootMotionMode::Base;
    asset.layers.push_back(baseLayer);
    VansAnimationLayerDefinition overlayLayer;
    overlayLayer.id = "layer-upper";
    overlayLayer.name = "Upper";
    overlayLayer.graphId = "graph-upper";
    overlayLayer.kind = VansAnimationLayerKind::Overlay;
    overlayLayer.maskGuid = "77777777-7777-4777-8777-777777777777";
    overlayLayer.maskPathHint = "upper.vbonemask";
    overlayLayer.useWeightParameter = true;
    overlayLayer.weightParameter = "UpperBodyWeight";
    overlayLayer.rootMotion = VansLayerRootMotionMode::Ignore;
    asset.layers.push_back(overlayLayer);
    VansAnimationSlotDefinition slot;
    slot.id = "slot-upper";
    slot.name = "Upper";
    slot.layerId = "layer-upper";
    slot.slotNodeId = slotNodeId;
    asset.slots.push_back(slot);

    const auto clipResolver = [&](const AnimatorClipRef& reference, fs::path& path, std::string& resolveError)
    {
        if (reference.assetGuid == "55555555-5555-4555-8555-555555555555") path = basePath;
        else if (reference.assetGuid == "66666666-6666-4666-8666-666666666666") path = overlayPath;
        else { resolveError = "Unexpected Clip GUID"; return false; }
        return true;
    };
    const auto maskResolver = [&](const VansAnimationLayerDefinition& layer, fs::path& path, std::string& resolveError)
    {
        if (layer.maskGuid != "77777777-7777-4777-8777-777777777777")
        {
            resolveError = "Unexpected Bone Mask GUID";
            return false;
        }
        path = maskPath;
        return true;
    };
    auto controller = VansAnimatorRuntimeCompiler::Compile(asset, skeleton,
        clipResolver, maskResolver, {}, error);
    if (!Expect(controller && controller->GetLayerCount() == 2, error.c_str()))
        return false;
    controller->SetFloat("UpperBodyWeight", 1.0f);
    controller->Play();
    controller->Update(0.0f, skeleton);
    if (!ExpectNear(controller->GetCachedGlobalTransform(1)[3].x, 10.0f, 0.0001f,
        "Unified runtime compiler did not apply the Overlay Layer"))
        return false;
    VansSlotPlayRequest request;
    request.clipName = "Overlay";
    const VansSlotPlaybackHandle handle = controller->PlaySlot("slot-upper", request);
    controller->Update(0.05f, skeleton);
    if (!Expect(handle && controller->IsSlotActive("slot-upper"),
        "Unified runtime compiler did not configure the authored Slot"))
        return false;

    if (!Expect(!VansAnimatorRuntimeCompiler::Compile(
        asset, skeleton, {}, maskResolver, {}, error),
        "Full-graph runtime compilation accepted a missing Clip resolver"))
        return false;

    bool clipResolverInvoked = false;
    bool maskResolverInvoked = false;
    VansAnimatorRuntimeCompileOptions targetOptions;
    targetOptions.mode = VansAnimatorRuntimeCompileMode::ExternalPoseTarget;
    auto targetController = VansAnimatorRuntimeCompiler::Compile(
        asset,
        skeleton,
        [&](const AnimatorClipRef&, fs::path&, std::string& resolveError)
        {
            clipResolverInvoked = true;
            resolveError = "External-pose target must not resolve source Clips";
            return false;
        },
        [&](const VansAnimationLayerDefinition&, fs::path&, std::string& resolveError)
        {
            maskResolverInvoked = true;
            resolveError = "External-pose target must not resolve source Bone Masks";
            return false;
        },
        targetOptions,
        error);
    if (!Expect(targetController && !clipResolverInvoked && !maskResolverInvoked,
        error.empty() ? "External-pose target resolved source Layer assets" : error.c_str()))
        return false;
    if (!Expect(targetController->GetClipNames().empty() && !targetController->HasLayerStack()
        && ExpectNear(targetController->GetFloat("UpperBodyWeight"), 0.0f, 0.0001f,
            "External-pose target did not preserve authored parameters"),
        "External-pose target retained the source Clip/Layer runtime"))
        return false;

    std::vector<glm::mat4> externalPose(skeleton.bones.size(), glm::mat4(1.0f));
    return Expect(targetController->SubmitExternalModelPose(
        externalPose, skeleton, 0.016f, VansExternalPoseEvaluationMode::TargetPostProcess)
        && targetController->GetCachedGlobalTransforms().size() == skeleton.bones.size(),
        "External-pose target did not accept a retargeted model-space pose");
}

bool TestAnimationLayerStackRuntimeContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildLayerContractSkeleton();
    auto buildClip = [&](const std::string& name, float root, float spine, float arm)
    {
        VansAnimationClip clip;
        clip.clipName = name;
        clip.duration = 1.0f;
        clip.boneKeyframes.resize(3);
        const float values[] = { root, spine, arm };
        for (int index = 0; index < 3; ++index)
        {
            clip.boneKeyframes[index].push_back({ 0.0f, glm::vec3(values[index], 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
            clip.boneKeyframes[index].push_back({ 1.0f, glm::vec3(values[index], 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        }
        return clip;
    };
    auto buildGraph = [](const std::string& clipName)
    {
        auto graph = std::make_unique<VansAnimGraph>();
        auto clip = std::make_unique<AnimGraphClipNode>();
        clip->m_ClipName = clipName;
        const int clipId = graph->AddNode(std::move(clip));
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(clipId, 0, outputId, 0);
        return graph;
    };

    VansBoneMaskAsset upperMask;
    upperMask.id = "mask-upper";
    upperMask.name = "Upper";
    VansBoneMaskBranchRule rule;
    rule.id = "upper-branch";
    rule.rootBone = "spine";
    rule.rootWeight = 0.25f;
    rule.endWeight = 0.75f;
    upperMask.branchRules.push_back(rule);

    VansAnimationLayerGraphSetup base;
    base.definition.id = "layer-base";
    base.definition.name = "Base";
    base.definition.graphId = "graph-base";
    base.definition.kind = VansAnimationLayerKind::Base;
    base.definition.rootMotion = VansLayerRootMotionMode::Base;
    base.definition.nodeTracks = VansLayerNodeTrackMode::Override;
    base.graph = buildGraph("Base");

    VansAnimationLayerGraphSetup overlay;
    overlay.definition.id = "layer-upper";
    overlay.definition.name = "Upper";
    overlay.definition.graphId = "graph-upper";
    overlay.definition.kind = VansAnimationLayerKind::Overlay;
    overlay.definition.blendMode = VansLayerBlendMode::Override;
    overlay.definition.fixedWeight = 1.0f;
    overlay.definition.rootMotion = VansLayerRootMotionMode::Ignore;
    overlay.definition.events = VansLayerEventMode::ActiveOnly;
    overlay.graph = buildGraph("Overlay");
    overlay.mask = upperMask;

    VansAnimationController controller;
    controller.AddClip("Base", buildClip("Base", 0.0f, 0.0f, 0.0f));
    controller.AddClip("Overlay", buildClip("Overlay", 100.0f, 10.0f, 20.0f));
    std::vector<VansAnimationLayerGraphSetup> layers;
    layers.push_back(std::move(base));
    layers.push_back(std::move(overlay));
    std::string error;
    if (!Expect(controller.SetLayerStack(std::move(layers), error), error.c_str()))
        return false;
    controller.Play();
    controller.Update(0.0f, skeleton);

    if (!ExpectNear(controller.GetCachedGlobalTransform(0)[3].x, 0.0f, 0.0001f,
        "Partial Layer unexpectedly changed the root bone"))
        return false;
    if (!ExpectNear(controller.GetCachedGlobalTransform(1)[3].x, 2.5f, 0.0001f,
        "Layer root falloff did not affect the spine"))
        return false;
    if (!ExpectNear(controller.GetCachedGlobalTransform(2)[3].x, 17.5f, 0.0001f,
        "Layer descendant weight or hierarchy composition changed"))
        return false;

    for (int frame = 0; frame < 8; ++frame)
        controller.Update(1.0f / 60.0f, skeleton);
    if (!Expect(controller.GetLastFrameScratchAllocations() == 0
        && controller.GetLastFrameScratchAllocatedBytes() == 0,
        "Stable Base + Overlay evaluation requested new frame-pool memory"))
        return false;
    const std::size_t layeredHeapAllocations = CountHeapAllocations(
        [&]() { controller.Update(1.0f / 60.0f, skeleton); });
    if (layeredHeapAllocations != 0)
    {
        std::cerr << "[ForestContractTests] Base + Overlay stable-frame allocations: "
            << layeredHeapAllocations << '\n';
    }
    if (!Expect(layeredHeapAllocations == 0,
        "Stable Base + Overlay Animation Update performed a heap allocation"))
        return false;

    VansAnimationLayerGraphSetup invalidBase;
    invalidBase.definition.id = "not-base";
    invalidBase.definition.name = "OverlayFirst";
    invalidBase.definition.graphId = "invalid-graph";
    invalidBase.definition.kind = VansAnimationLayerKind::Overlay;
    invalidBase.graph = buildGraph("Base");
    invalidBase.mask = upperMask;
    std::vector<VansAnimationLayerGraphSetup> invalidLayers;
    invalidLayers.push_back(std::move(invalidBase));
    return Expect(!controller.SetLayerStack(std::move(invalidLayers), error),
        "Layer Stack accepted an Animator without exactly one Base layer");
}

bool TestAnimationSlotRuntimeContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildLayerContractSkeleton();
    auto makeClip = [](const std::string& name, float armValue)
    {
        VansAnimationClip clip;
        clip.clipName = name;
        clip.duration = 1.0f;
        clip.boneKeyframes.resize(3);
        for (int index = 0; index < 3; ++index)
        {
            const float value = index == 2 ? armValue : 0.0f;
            clip.boneKeyframes[index].push_back({ 0.0f, glm::vec3(value, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
            clip.boneKeyframes[index].push_back({ 1.0f, glm::vec3(value, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        }
        return clip;
    };
    auto makeBaseGraph = []
    {
        auto graph = std::make_unique<VansAnimGraph>();
        auto clip = std::make_unique<AnimGraphClipNode>();
        clip->m_ClipName = "Base";
        const int clipId = graph->AddNode(std::move(clip));
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(clipId, 0, outputId, 0);
        return graph;
    };
    auto slotGraph = std::make_unique<VansAnimGraph>();
    auto slotNode = std::make_unique<AnimGraphSlotNode>();
    slotNode->m_SlotId = "slot-upper";
    slotNode->m_EnableFallbackInput = false;
    const int slotNodeId = slotGraph->AddNode(std::move(slotNode));
    const int slotOutputId = slotGraph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    slotGraph->AddLink(slotNodeId, 0, slotOutputId, 0);

    VansBoneMaskAsset mask;
    mask.id = "mask-upper";
    mask.name = "Upper";
    VansBoneMaskBranchRule rule;
    rule.id = "upper";
    rule.rootBone = "spine";
    rule.rootWeight = 1.0f;
    rule.endWeight = 1.0f;
    mask.branchRules.push_back(rule);

    VansAnimationLayerGraphSetup base;
    base.definition.id = "layer-base";
    base.definition.name = "Base";
    base.definition.graphId = "graph-base";
    base.definition.kind = VansAnimationLayerKind::Base;
    base.definition.rootMotion = VansLayerRootMotionMode::Base;
    base.graph = makeBaseGraph();
    VansAnimationLayerGraphSetup overlay;
    overlay.definition.id = "layer-upper";
    overlay.definition.name = "Upper";
    overlay.definition.graphId = "graph-slot";
    overlay.definition.kind = VansAnimationLayerKind::Overlay;
    overlay.definition.events = VansLayerEventMode::ActiveOnly;
    overlay.graph = std::move(slotGraph);
    overlay.mask = mask;

    VansAnimationController controller;
    controller.AddClip("Base", makeClip("Base", 0.0f));
    controller.AddClip("Fire", makeClip("Fire", 20.0f));
    controller.AddClip("Reload", makeClip("Reload", 30.0f));
    std::vector<VansAnimationLayerGraphSetup> layers;
    layers.push_back(std::move(base));
    layers.push_back(std::move(overlay));
    std::string error;
    if (!Expect(controller.SetLayerStack(std::move(layers), error), error.c_str()))
        return false;

    VansAnimationSlotDefinition slot;
    slot.id = "slot-upper";
    slot.name = "Upper";
    slot.layerId = "layer-upper";
    slot.slotNodeId = slotNodeId;
    slot.defaultBlendIn = 0.0f;
    slot.defaultBlendOut = 0.2f;
    if (!Expect(controller.SetSlots({ slot }, error), error.c_str()))
        return false;
    controller.Play();
    VansSlotPlayRequest fire;
    fire.clipName = "Fire";
    fire.priority = 1;
    const VansSlotPlaybackHandle fireHandle = controller.PlaySlot("slot-upper", fire);
    if (!Expect(static_cast<bool>(fireHandle), "Slot did not return an exact playback handle"))
        return false;
    controller.Update(0.0f, skeleton);
    if (!ExpectNear(controller.GetCachedGlobalTransform(2)[3].x, 20.0f, 0.0001f,
        "Slot Graph node did not feed the owning Layer"))
        return false;
    if (!Expect(controller.GetSlotStatus(fireHandle).state == VansSlotPlaybackState::Playing,
        "Slot handle did not report its exact active request"))
        return false;
    for (int frame = 0; frame < 8; ++frame)
        controller.Update(1.0f / 60.0f, skeleton);
    if (!Expect(controller.GetLastFrameScratchAllocations() == 0
        && controller.GetLastFrameScratchAllocatedBytes() == 0,
        "Stable Layer + Slot evaluation requested new frame-pool memory"))
        return false;
    const std::size_t slotHeapAllocations = CountHeapAllocations(
        [&]() { controller.Update(1.0f / 60.0f, skeleton); });
    if (slotHeapAllocations != 0)
    {
        std::cerr << "[ForestContractTests] Layer + Slot stable-frame allocations: "
            << slotHeapAllocations << '\n';
    }
    if (!Expect(slotHeapAllocations == 0,
        "Stable Layer + Slot Animation Update performed a heap allocation"))
        return false;

    VansSlotPlayRequest reload;
    reload.clipName = "Reload";
    reload.priority = 2;
    reload.blendIn = 0.0f;
    const VansSlotPlaybackHandle reloadHandle = controller.PlaySlot("slot-upper", reload);
    if (!Expect(static_cast<bool>(reloadHandle)
        && controller.GetSlotStatus(fireHandle).state == VansSlotPlaybackState::BlendingOut,
        "Higher-priority Slot request did not interrupt through blend-out"))
        return false;
    int interruptedEvents = 0;
    for (const VansSlotLifecycleEvent& event : controller.GetSlotLifecycleEvents())
        if (event.handle == fireHandle && event.type == VansSlotLifecycleEventType::Interrupted)
            ++interruptedEvents;
    if (!Expect(interruptedEvents == 1, "Interrupted Slot lifecycle event did not fire exactly once"))
        return false;

    VansAnimationSlotRuntime queueRuntime;
    slot.concurrency = VansSlotConcurrency::Queue;
    slot.maxQueueDepth = 1;
    if (!Expect(queueRuntime.Configure({ slot }, error), error.c_str()))
        return false;
    VansSlotPlayRequest queuedA = fire;
    queuedA.priority = 0;
    VansSlotPlayRequest queuedB = reload;
    queuedB.priority = 0;
    const auto first = queueRuntime.Play("slot-upper", queuedA);
    const auto second = queueRuntime.Play("slot-upper", queuedB);
    if (!Expect(queueRuntime.GetStatus(second).state == VansSlotPlaybackState::Queued,
        "Queue Slot did not preserve arrival order"))
        return false;
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips.emplace("Fire", makeClip("Fire", 20.0f));
    clips.emplace("Reload", makeClip("Reload", 30.0f));
    std::unordered_map<std::string, VansPosePayload> payloads;
    queueRuntime.Update(1.1f, clips, skeleton, payloads);
    queueRuntime.Update(0.0f, clips, skeleton, payloads);
    return Expect(queueRuntime.GetStatus(first).state == VansSlotPlaybackState::Completed
        && queueRuntime.GetStatus(second).state != VansSlotPlaybackState::Queued,
        "Queued Slot request did not start after the prior request completed");
}

bool TestAnimationHotReloadStateTransferContract()
{
    using namespace VansGraphics;
    const Skeleton skeleton = BuildLayerContractSkeleton();
    auto makeClip = [](const std::string& name, float armValue)
    {
        VansAnimationClip clip;
        clip.clipName = name;
        clip.duration = 1.0f;
        clip.boneKeyframes.resize(3);
        for (int bone = 0; bone < 3; ++bone)
        {
            const float value = bone == 2 ? armValue : 0.0f;
            clip.boneKeyframes[bone] = {
                { 0.0f, glm::vec3(value, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) },
                { 1.0f, glm::vec3(value, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) }
            };
        }
        return clip;
    };
    auto makeController = [&](bool compatibleState) -> std::unique_ptr<VansAnimationController>
    {
        auto controller = std::make_unique<VansAnimationController>();
        controller->AddParameter("Weight", AnimatorParamType::Float);
        controller->AddParameter("Mode", AnimatorParamType::Int);
        controller->AddClip("Base", makeClip("Base", 0.0f));
        controller->AddClip("Action", makeClip("Action", 10.0f));
        controller->AddClip("Fire", makeClip("Fire", 20.0f));

        auto baseGraph = std::make_unique<VansAnimGraph>();
        auto stateMachine = std::make_unique<AnimGraphStateMachineNode>();
        AnimatorState idle;
        idle.name = "Idle";
        idle.clipName = "Base";
        AnimatorState action;
        action.name = compatibleState ? "Action" : "Replacement";
        action.clipName = "Action";
        stateMachine->m_States = { idle, action };
        stateMachine->m_DefaultStateName = "Idle";
        const int stateMachineId = baseGraph->AddNode(std::move(stateMachine));
        const int baseOutput = baseGraph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        baseGraph->AddLink(stateMachineId, 0, baseOutput, 0);

        auto slotGraph = std::make_unique<VansAnimGraph>();
        auto slotNode = std::make_unique<AnimGraphSlotNode>();
        slotNode->m_SlotId = "slot-upper";
        slotNode->m_EnableFallbackInput = false;
        const int slotNodeId = slotGraph->AddNode(std::move(slotNode));
        const int slotOutput = slotGraph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        slotGraph->AddLink(slotNodeId, 0, slotOutput, 0);

        VansAnimationLayerGraphSetup base;
        base.definition.id = "layer-base";
        base.definition.name = "Base";
        base.definition.graphId = "graph-base";
        base.definition.kind = VansAnimationLayerKind::Base;
        base.graph = std::move(baseGraph);
        VansAnimationLayerGraphSetup overlay;
        overlay.definition.id = "layer-upper";
        overlay.definition.name = "Upper";
        overlay.definition.graphId = "graph-upper";
        overlay.definition.kind = VansAnimationLayerKind::Overlay;
        overlay.graph = std::move(slotGraph);
        VansBoneMaskAsset mask;
        mask.id = "mask-upper";
        mask.name = "Upper";
        VansBoneMaskBranchRule branch;
        branch.id = "upper";
        branch.rootBone = "spine";
        branch.rootWeight = 1.0f;
        branch.endWeight = 1.0f;
        mask.branchRules.push_back(branch);
        overlay.mask = std::move(mask);
        std::vector<VansAnimationLayerGraphSetup> layers;
        layers.push_back(std::move(base));
        layers.push_back(std::move(overlay));
        std::string error;
        if (!controller->SetLayerStack(std::move(layers), error))
            return nullptr;
        VansAnimationSlotDefinition slot;
        slot.id = "slot-upper";
        slot.name = "Upper";
        slot.layerId = "layer-upper";
        slot.slotNodeId = slotNodeId;
        slot.defaultBlendIn = 0.0f;
        slot.defaultBlendOut = 0.1f;
        if (!controller->SetSlots({ slot }, error))
            return nullptr;
        controller->Update(0.0f, skeleton);
        return controller;
    };

    auto previous = makeController(true);
    auto replacement = makeController(true);
    if (!Expect(previous && replacement, "Failed to create hot reload contract controllers"))
        return false;
    previous->SetFloat("Weight", 0.73f);
    previous->SetInt("Mode", 4);
    previous->Play("Action");
    previous->Update(0.35f, skeleton);
    VansSlotPlayRequest fire;
    fire.clipName = "Fire";
    const VansSlotPlaybackHandle handle = previous->PlaySlot("slot-upper", fire);
    previous->Update(0.1f, skeleton);
    const float previousTime = previous->GetCurrentPlayTime();

    std::string diagnostic;
    if (!Expect(replacement->TransferRuntimeStateFrom(*previous, skeleton, diagnostic),
        "Compatible Animator hot reload unexpectedly reset runtime state"))
        return false;
    replacement->Update(0.0f, skeleton);
    if (!ExpectNear(replacement->GetFloat("Weight"), 0.73f, 0.0001f,
        "Animator hot reload did not preserve a compatible parameter value")
        || !Expect(replacement->GetInt("Mode") == 4,
            "Animator hot reload did not preserve a compatible int parameter")
        || !Expect(replacement->GetCurrentStateName() == "Action",
            "Animator hot reload did not preserve the State Machine state")
        || !ExpectNear(replacement->GetCurrentPlayTime(), previousTime, 0.0001f,
            "Animator hot reload did not preserve State Machine playback time")
        || !Expect(replacement->GetPlaybackState() == AnimationState::Playing,
            "Animator hot reload did not preserve playback state")
        || !Expect(replacement->GetSlotStatus(handle).state == VansSlotPlaybackState::Playing,
            "Animator hot reload did not preserve a valid Slot request"))
        return false;

    auto incompatible = makeController(false);
    if (!Expect(incompatible != nullptr, "Failed to create incompatible hot reload controller"))
        return false;
    if (!Expect(!incompatible->TransferRuntimeStateFrom(*previous, skeleton, diagnostic),
        "Animator hot reload reported incompatible State IDs as fully compatible"))
        return false;
    incompatible->Update(0.0f, skeleton);
    if (!Expect(incompatible->GetCurrentStateName() == "Idle",
        "Incompatible Animator hot reload did not perform a controlled State Machine reset"))
        return false;

    VansAnimationSlotRuntime removedSlotRuntime;
    if (!Expect(removedSlotRuntime.Configure({}, diagnostic), diagnostic.c_str()))
        return false;
    VansAnimationSlotRuntime activeSlotRuntime;
    VansAnimationSlotDefinition slot;
    slot.id = "slot-upper";
    slot.name = "Upper";
    slot.layerId = "layer-upper";
    slot.slotNodeId = 1;
    if (!Expect(activeSlotRuntime.Configure({ slot }, diagnostic), diagnostic.c_str()))
        return false;
    const VansSlotPlaybackHandle removedHandle = activeSlotRuntime.Play("slot-upper", fire);
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips.emplace("Fire", makeClip("Fire", 20.0f));
    removedSlotRuntime.TransferRuntimeStateFrom(activeSlotRuntime, clips);
    std::unordered_map<std::string, VansPosePayload> payloads;
    removedSlotRuntime.Update(0.0f, clips, skeleton, payloads);
    const bool interruptedByReload = std::any_of(
        removedSlotRuntime.GetLifecycleEvents().begin(), removedSlotRuntime.GetLifecycleEvents().end(),
        [&](const VansSlotLifecycleEvent& event)
        {
            return event.handle == removedHandle
                && event.type == VansSlotLifecycleEventType::InterruptedByReload;
        });
    return Expect(interruptedByReload,
        "Removed Slot binding did not publish InterruptedByReload during state transfer");
}

bool TestAnimationMarkerSyncLayerContract()
{
    using namespace VansGraphics;
    const Skeleton skeleton = BuildLayerContractSkeleton();
    auto makeClip = [](const std::string& name, float duration, float armEnd,
                       float markerA, float markerB)
    {
        VansAnimationClip clip;
        clip.clipName = name;
        clip.duration = duration;
        clip.syncGroupName = "Locomotion";
        clip.syncMarkers = {
            { VansAnimationStableId("A"), markerA, "A" },
            { VansAnimationStableId("B"), markerB, "B" }
        };
        clip.boneKeyframes.resize(3);
        for (int bone = 0; bone < 3; ++bone)
        {
            const float end = bone == 2 ? armEnd : 0.0f;
            clip.boneKeyframes[bone].push_back({ 0.0f, glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
            clip.boneKeyframes[bone].push_back({ duration, glm::vec3(end, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        }
        return clip;
    };
    auto makeGraph = [](const std::string& clipName)
    {
        auto graph = std::make_unique<VansAnimGraph>();
        auto clip = std::make_unique<AnimGraphClipNode>();
        clip->m_ClipName = clipName;
        const int clipId = graph->AddNode(std::move(clip));
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(clipId, 0, outputId, 0);
        return graph;
    };

    VansBoneMaskAsset armMask;
    armMask.id = "mask-arm";
    armMask.name = "Arm";
    armMask.explicitWeights["arm"] = 1.0f;
    VansAnimationLayerGraphSetup leader;
    leader.definition.id = "layer-leader";
    leader.definition.name = "Leader";
    leader.definition.graphId = "graph-leader";
    leader.definition.kind = VansAnimationLayerKind::Base;
    leader.definition.rootMotion = VansLayerRootMotionMode::Base;
    leader.graph = makeGraph("Leader");
    VansAnimationLayerGraphSetup follower;
    follower.definition.id = "layer-follower";
    follower.definition.name = "Follower";
    follower.definition.graphId = "graph-follower";
    follower.definition.kind = VansAnimationLayerKind::Overlay;
    follower.definition.sync = VansLayerSyncMode::MarkerSync;
    follower.definition.syncLeaderLayerId = "layer-leader";
    follower.graph = makeGraph("Follower");
    follower.mask = armMask;

    VansAnimationController controller;
    controller.AddClip("Leader", makeClip("Leader", 2.0f, 0.0f, 0.25f, 1.25f));
    controller.AddClip("Follower", makeClip("Follower", 4.0f, 40.0f, 1.0f, 3.0f));
    std::vector<VansAnimationLayerGraphSetup> layers;
    layers.push_back(std::move(leader));
    layers.push_back(std::move(follower));
    std::string error;
    if (!Expect(controller.SetLayerStack(std::move(layers), error), error.c_str()))
        return false;
    controller.Play();
    controller.Update(0.0f, skeleton);
    controller.Update(0.75f, skeleton);
    return ExpectNear(controller.GetCachedGlobalTransform(2)[3].x, 20.0f, 0.001f,
        "Marker-synced Layer did not align the follower marker phase");
}

bool TestAnimationTargetPostProcessContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildLayerContractSkeleton();
    auto makePostProcessGraph = []
    {
        auto graph = std::make_unique<VansAnimGraph>();
        const int inputId = graph->AddNode(
            VansAnimGraph::CreateNodeByType(AnimGraphNodeType::TargetPoseInput));
        auto lookAt = std::make_unique<AnimGraphLookAtNode>();
        lookAt->m_BoneNames = { "arm" };
        lookAt->m_BoneWeights = { 1.0f };
        lookAt->m_UseFixedTarget = true;
        lookAt->m_FixedTargetPos = glm::vec3(1.0f, 0.0f, 0.0f);
        lookAt->m_FixedWeight = 1.0f;
        const int lookAtId = graph->AddNode(std::move(lookAt));
        const int outputId = graph->AddNode(
            VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(inputId, 0, lookAtId, 0);
        graph->AddLink(lookAtId, 0, outputId, 0);
        return graph;
    };
    auto aimedForward = [](const VansAnimationController& controller)
    {
        return glm::normalize(glm::vec3(controller.GetCachedGlobalTransform(2)
            * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    };

    std::string error;
    VansAnimationController externalController;
    if (!Expect(externalController.SetTargetPostProcessGraph(makePostProcessGraph(), error), error.c_str()))
        return false;
    std::vector<glm::mat4> externalModelPose(3, glm::mat4(1.0f));
    if (!Expect(externalController.SubmitExternalModelPose(
        externalModelPose, skeleton, 0.016f,
        VansExternalPoseEvaluationMode::TargetPostProcess),
        "Retarget-style pose did not enter Target Post Process"))
        return false;
    if (!Expect(glm::dot(aimedForward(externalController), glm::vec3(1.0f, 0.0f, 0.0f)) > 0.95f,
        "Target Post Process did not modify the retargeted target-skeleton pose"))
        return false;
    for (int frame = 0; frame < 8; ++frame)
        externalController.SubmitExternalModelPose(externalModelPose, skeleton, 1.0f / 60.0f,
            VansExternalPoseEvaluationMode::TargetPostProcess);
    const std::size_t externalPostProcessAllocations = CountHeapAllocations([&]()
    {
        externalController.SubmitExternalModelPose(externalModelPose, skeleton, 1.0f / 60.0f,
            VansExternalPoseEvaluationMode::TargetPostProcess);
    });
    if (externalPostProcessAllocations != 0)
    {
        std::cerr << "[ForestContractTests] Retarget-style Target Post Process stable-frame allocations: "
            << externalPostProcessAllocations << '\n';
        std::cerr << "[ForestContractTests] Frame-pool allocations: "
            << externalController.GetLastFrameScratchAllocations() << '\n';
    }
    if (!Expect(externalPostProcessAllocations == 0,
        "Stable Retarget-style Target Post Process performed a heap allocation"))
        return false;

    VansAnimationController directController;
    if (!Expect(directController.SubmitExternalModelPose(
        externalModelPose, skeleton, 0.016f,
        VansExternalPoseEvaluationMode::DirectFinalPose),
        "Direct external pose submission failed"))
        return false;
    if (!Expect(std::fabs(glm::dot(aimedForward(directController), glm::vec3(1.0f, 0.0f, 0.0f))) < 0.001f,
        "Ragdoll direct-pose mode unexpectedly ran animation post processing"))
        return false;

    VansAnimationClip clip;
    clip.clipName = "Base";
    clip.duration = 1.0f;
    clip.boneKeyframes.resize(3);
    for (auto& track : clip.boneKeyframes)
    {
        track.push_back({ 0.0f, glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        track.push_back({ 1.0f, glm::vec3(0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
    }
    auto baseGraph = std::make_unique<VansAnimGraph>();
    auto clipNode = std::make_unique<AnimGraphClipNode>();
    clipNode->m_ClipName = "Base";
    const int clipId = baseGraph->AddNode(std::move(clipNode));
    const int outputId = baseGraph->AddNode(
        VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
    baseGraph->AddLink(clipId, 0, outputId, 0);

    VansAnimationController layeredController;
    layeredController.AddClip("Base", clip);
    if (!Expect(InstallTestBaseLayer(layeredController, std::move(baseGraph), error), error.c_str())
        || !Expect(layeredController.SetTargetPostProcessGraph(makePostProcessGraph(), error), error.c_str()))
        return false;
    layeredController.Play();
    layeredController.Update(0.0f, skeleton);
    if (!Expect(glm::dot(aimedForward(layeredController), glm::vec3(1.0f, 0.0f, 0.0f)) > 0.95f,
        "Layer-composed pose skipped Target Post Process"))
        return false;
    for (int frame = 0; frame < 8; ++frame)
        layeredController.Update(1.0f / 60.0f, skeleton);
    if (!Expect(CountHeapAllocations(
        [&]() { layeredController.Update(1.0f / 60.0f, skeleton); }) == 0,
        "Stable Layer + Target Post Process Update performed a heap allocation"))
        return false;

    auto invalidPostProcess = makePostProcessGraph();
    auto forbiddenClip = std::make_unique<AnimGraphClipNode>();
    forbiddenClip->m_ClipName = "Base";
    invalidPostProcess->AddNode(std::move(forbiddenClip));
    return Expect(!layeredController.SetTargetPostProcessGraph(std::move(invalidPostProcess), error),
        "Target Post Process accepted a playback pose source");
}

bool TestAnimationSyncedGraphStateContract()
{
    using namespace VansGraphics;

    const Skeleton skeleton = BuildLayerContractSkeleton();
    auto makeClip = [](const std::string& name, float armValue, float duration)
    {
        VansAnimationClip clip;
        clip.clipName = name;
        clip.duration = duration;
        clip.boneKeyframes.resize(3);
        for (int bone = 0; bone < 3; ++bone)
        {
            const float value = bone == 2 ? armValue : 0.0f;
            clip.boneKeyframes[bone].push_back({ 0.0f, glm::vec3(value, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
            clip.boneKeyframes[bone].push_back({ duration, glm::vec3(value, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
        }
        return clip;
    };
    auto makeStateGraph = [](const std::string& idleClip, const std::string& runClip,
                             bool ownsTransitions)
    {
        auto stateMachine = std::make_unique<AnimGraphStateMachineNode>();
        stateMachine->m_DefaultStateName = "Idle";
        AnimatorState idle;
        idle.name = "Idle";
        idle.clipName = idleClip;
        AnimatorState run;
        run.name = "Run";
        run.clipName = runClip;
        stateMachine->m_States = { idle, run };
        if (ownsTransitions)
        {
            AnimatorTransition transition;
            transition.fromState = "Idle";
            transition.toState = "Run";
            transition.blendDuration = 0.5f;
            TransitionCondition condition;
            condition.paramName = "Go";
            condition.op = CompareOp::Equal;
            condition.boolVal = true;
            transition.conditions.push_back(condition);
            stateMachine->m_Transitions.push_back(transition);
        }
        auto graph = std::make_unique<VansAnimGraph>();
        const int stateMachineId = graph->AddNode(std::move(stateMachine));
        const int outputId = graph->AddNode(
            VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(stateMachineId, 0, outputId, 0);
        return graph;
    };

    VansAnimationLayerGraphSetup leader;
    leader.definition.id = "layer-leader";
    leader.definition.name = "Leader";
    leader.definition.graphId = "graph-leader";
    leader.definition.kind = VansAnimationLayerKind::Base;
    leader.graph = makeStateGraph("BaseIdle", "BaseRun", true);

    VansBoneMaskAsset armMask;
    armMask.id = "mask-arm";
    armMask.name = "Arm";
    armMask.explicitWeights["arm"] = 1.0f;
    VansAnimationLayerGraphSetup follower;
    follower.definition.id = "layer-follower";
    follower.definition.name = "Follower";
    follower.definition.graphId = "graph-follower";
    follower.definition.kind = VansAnimationLayerKind::Overlay;
    follower.definition.sync = VansLayerSyncMode::SyncedGraph;
    follower.definition.syncLeaderLayerId = "layer-leader";
    follower.graph = makeStateGraph("UpperIdle", "UpperRun", false);
    follower.mask = armMask;

    VansAnimationController controller;
    controller.AddParameter("Go", AnimatorParamType::Bool);
    controller.AddClip("BaseIdle", makeClip("BaseIdle", 0.0f, 1.0f));
    controller.AddClip("BaseRun", makeClip("BaseRun", 0.0f, 2.0f));
    controller.AddClip("UpperIdle", makeClip("UpperIdle", 10.0f, 3.0f));
    controller.AddClip("UpperRun", makeClip("UpperRun", 30.0f, 4.0f));
    std::vector<VansAnimationLayerGraphSetup> layers;
    layers.push_back(std::move(leader));
    layers.push_back(std::move(follower));
    std::string error;
    if (!Expect(controller.SetLayerStack(std::move(layers), error), error.c_str()))
        return false;
    controller.Play();
    controller.Update(0.0f, skeleton);
    controller.SetBool("Go", true);
    controller.Update(0.25f, skeleton);
    return ExpectNear(controller.GetCachedGlobalTransform(2)[3].x, 20.0f, 0.001f,
        "Synced Graph follower did not share leader state and transition progress");
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

bool TestGIProbeUpdateScheduleContract()
{
	using namespace VansGraphics;
	GIProbeRegionDesc desc;
	desc.gridDimensions = glm::uvec3(80u, 80u, 80u);
	desc.overrideGridDimensions = true;
	desc.raysPerProbe = 256u;
	desc.spatialUpdateDivisor = 2u;
	desc.directionUpdateSlices = 16u;
	const GIResolvedRegion region = ResolveGIRegion(desc);

	const GIProbeUpdateBatch first = BuildGIProbeUpdateBatch(region, 0u);
	if (!Expect(first.spatialPhaseCount == 8u, "GI scheduler changed the 2x2x2 spatial phase count"))
		return false;
	if (!Expect(first.raysPerActiveProbe == 16u, "GI scheduler changed the 256/16 ray batch size"))
		return false;
	if (!Expect(first.fullUpdateFrameCount == 128u, "GI scheduler changed the 128-frame complete update period"))
		return false;

	std::array<uint32_t, 8> spatialPhaseVisits{};
	std::array<uint32_t, 16> directionSliceVisits{};
	for (uint64_t frame = 0; frame < 128u; ++frame)
	{
		const GIProbeUpdateBatch batch = BuildGIProbeUpdateBatch(region, frame);
		if (!Expect(batch.activeRayCount == batch.activeProbeCount * 16u,
			"GI scheduler exceeded the fixed active-probe ray budget"))
			return false;
		++spatialPhaseVisits[batch.spatialPhase];
		++directionSliceVisits[batch.directionSlice];
	}

	for (uint32_t visits : spatialPhaseVisits)
	{
		if (!Expect(visits == 16u, "GI scheduler did not visit every spatial phase for every direction slice"))
			return false;
	}
	for (uint32_t visits : directionSliceVisits)
	{
		if (!Expect(visits == 8u, "GI scheduler did not visit every direction slice for every spatial phase"))
			return false;
	}

	const GIProbeUpdateBatch nextCycle = BuildGIProbeUpdateBatch(region, 128u);
	if (!Expect(nextCycle.cycleIndex == 1u && nextCycle.spatialPhase == 0u && nextCycle.directionSlice == 0u,
		"GI scheduler did not begin the next cycle at frame 128"))
		return false;

	GIProbeRegionDesc oddDesc = desc;
	oddDesc.gridDimensions = glm::uvec3(5u, 3u, 1u);
	const GIResolvedRegion oddRegion = ResolveGIRegion(oddDesc);
	const GIProbeUpdateBatch oddBatch = BuildGIProbeUpdateBatch(oddRegion, 1u);
	if (!Expect(oddBatch.spatialPhaseCount == 4u,
		"GI scheduler created empty phases for a degenerate grid axis"))
		return false;
	constexpr std::array<uint64_t, 4> expectedOddPhaseProbeCounts{ 6u, 4u, 3u, 2u };
	uint64_t oddVisitedProbes = 0u;
	for (uint64_t phase = 0u; phase < oddBatch.spatialPhaseCount; ++phase)
	{
		const GIProbeUpdateBatch phaseBatch = BuildGIProbeUpdateBatch(oddRegion, phase);
		if (!Expect(phaseBatch.activeProbeCount == expectedOddPhaseProbeCounts[phase],
			"GI scheduler over-counted active probes for an odd-sized grid phase"))
			return false;
		oddVisitedProbes += phaseBatch.activeProbeCount;
	}
	if (!Expect(oddVisitedProbes == oddRegion.probeCount,
		"GI scheduler did not cover every odd-sized grid probe exactly once per spatial cycle"))
		return false;

	VansGISettings temporalSettings;
	temporalSettings.irradianceHysteresis = 2.0f;
	temporalSettings.distanceHysteresis = -1.0f;
	temporalSettings.distanceSharpness = 2.0f;
	temporalSettings.brightnessChangeThreshold = -4.0f;
	NormalizeGISettings(temporalSettings);
	if (!Expect(temporalSettings.irradianceHysteresis == 0.999f &&
		temporalSettings.distanceHysteresis == 0.0f &&
		temporalSettings.distanceSharpness == 8.0f &&
		temporalSettings.brightnessChangeThreshold == 0.001f,
		"GI temporal stability parameters were not normalized to their canonical ranges"))
		return false;

	VansGISettings orderedSettings;
	orderedSettings.regions = { GIProbeRegionDesc{}, GIProbeRegionDesc{}, GIProbeRegionDesc{} };
	orderedSettings.regions[0].stableId = 11u;
	orderedSettings.regions[1].stableId = 22u;
	orderedSettings.regions[2].stableId = 33u;
	orderedSettings.selectedRegionIndex = 2u;
	const std::vector<const GIProbeRegionDesc*> orderedRegions = BuildActiveGIRegionOrder(orderedSettings);
	if (!Expect(orderedRegions.size() == 3u && orderedRegions[0]->stableId == 33u &&
		orderedRegions[1]->stableId == 11u && orderedRegions[2]->stableId == 22u,
		"GI region order does not place the selected region first for matching atlas and SSGI bindings"))
		return false;
	orderedSettings.regions[2].enabled = false;
	const std::vector<const GIProbeRegionDesc*> disabledSelectedRegions = BuildActiveGIRegionOrder(orderedSettings);
	return Expect(disabledSelectedRegions.size() == 2u && disabledSelectedRegions[0]->stableId == 11u,
		"GI region order retained a disabled selected region in the atlas/SSGI binding order");
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
	if (!TestTimelinePropertyRegistryContract())
		return 58;
	if (!TestTimelineEventPolicyContract())
		return 59;
	if (!TestTimelineManualRuntimeControlContract())
		return 72;
	if (!TestTimelineNestedPlayerLifetimeContract())
		return 73;
	if (!TestTimelineCanonicalRoundTripContract())
		return 60;
	if (!TestTimelineCameraShakeEvaluationContract())
		return 92;
	if (!TestTimelineCapabilityAndGuidContract())
		return 61;
	if (!TestTimelineDependencyClosureContract())
		return 62;
	if (!TestTimelineScenePackageDependencyContract())
		return 67;
	if (!TestTimelinePreAnimatedStackContract())
		return 63;
	if (!TestTimelineIsolatedPreviewOwnerContract())
		return 68;
	if (!TestAnimationV2TimelineShowcaseContract())
		return 74;
	if (!TestTimelineEditorTransactionContract())
		return 64;
	if (!TestTimelineEditorStateAndRecoveryContract())
		return 66;
	if (!TestTimelineTimeContract())
		return 65;
	if (!TestTimelineWaveformContract())
		return 69;
	if (!TestTimelineVideoThumbnailContract())
		return 71;
	if (!TestTimelineScaleContract())
		return 70;
	if (!TestRuntimeWorldEntityLifetimeContract())
        return 23;
    if (!TestRuntimeWorldParentEditContract())
        return 27;
    if (!TestRuntimeWorldComponentEnabledContract())
        return 24;
    if (!TestRuntimeWorldComponentLifetimeContract())
        return 29;
    if (!TestRuntimeComponentKeyCanonicalizationContract())
        return 30;
    if (!TestRuntimeWorldCommandBufferContract())
        return 25;
    if (!TestScriptObjectActiveDoesNotOverwriteComponentEnabledContract())
        return 26;
    if (!TestScriptComponentDestroyIsIdempotentContract())
        return 28;
    if (!TestScriptComponentRuntimeEnabledMirrorHasNoBackendCallbacksContract())
        return 35;
    if (!TestScriptParticleRuntimeEnabledMirrorContract())
        return 36;
    if (!TestScriptUIRuntimeOpenScreensMirrorContract())
        return 37;
    if (!TestScriptObjectOwnedTransformReleaseContract())
        return 33;
    if (!TestScriptLightIndexRebindFacadeContract())
        return 34;
    if (!TestMotionMatchingAutoBuildLocomotionMetadataContract())
        return 31;
    if (!TestAnimationClipNodeTransformChannelConfigContract())
        return 32;
    if (!TestAnimationStateMachineRestartSamplesStartPoseContract())
        return 38;
    if (!TestAnimatorCanonicalFormatContract())
        return 39;
    if (!TestAnimationGraphLinkValidationContract())
        return 40;
    if (!TestAnimationPoseMathContract())
        return 41;
    if (!TestAnimationGraphSharedSubgraphCacheContract())
        return 42;
    if (!TestAnimationProjectAnimatorAssetsCanonicalContract())
        return 43;
	if (!TestAnimationAuthoringBoundaryContract())
		return 57;
    if (!TestAnimationGraphAdvancesOnlyActiveNodesContract())
        return 44;
	if (!TestAnimationGraphDefinitionInstanceIsolationContract())
		return 45;
	if (!TestAnimationPayloadIntervalSamplingContract())
		return 46;
	if (!TestAnimationClipPayloadMetadataRoundTripContract())
		return 47;
	if (!TestAnimationSpeedScaleContract())
		return 48;
	if (!TestBoneMaskCompilationAndStorageContract())
		return 49;
	if (!TestAnimatorRuntimeCompilerContract())
		return 55;
	if (!TestAnimationLayerStackRuntimeContract())
		return 50;
	if (!TestAnimationSlotRuntimeContract())
		return 51;
	if (!TestAnimationHotReloadStateTransferContract())
		return 56;
	if (!TestAnimationMarkerSyncLayerContract())
		return 52;
	if (!TestAnimationTargetPostProcessContract())
		return 53;
	if (!TestAnimationSyncedGraphStateContract())
		return 54;
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
	if (!TestGIProbeUpdateScheduleContract())
		return 75;
    std::cout << "Forest contract tests passed\n";
    return 0;
}
