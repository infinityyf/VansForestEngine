#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AssetCore/VansAssetObjectRepository.h"
#include "NavigationAIContractTests.h"
#include "PcgCoreContractTests.h"
#include "PcgAssetContractTests.h"
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/AssetCore/VansAssetResolver.h"
#include "../EngineCore/AssetCore/VansBuiltInAssetCatalog.h"
#include "../EngineCore/AssetCore/VansDerivedArtifactLayout.h"
#include "../EngineCore/AssetCore/VansMaterialAuthoringAsset.h"
#include "../EngineCore/AssetCore/VansShaderAuthoringAsset.h"
#include "../EngineCore/AssetCore/VansSkinProfile.h"
#include "../EngineCore/AssetCore/VansSkeletalMeshImportSettings.h"
#include "../EngineCore/AssetCore/Importers/VansTextureCooker.h"
#include "../EngineCore/AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../EngineCore/AssetCore/Serialization/VansAssetMetaJsonCodec.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Serialization/VansSkinProfileJsonCodec.h"
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
#include "../EngineCore/AudioCore/VansAudioSystem.h"
#include "../EngineCore/AudioCore/VansAudioPreviewPlayer.h"
#include "../EngineCore/AudioCore/VansAudioVirtualization.h"
#include "../EngineCore/MediaCore/VansMediaDecodeSession.h"
#include "../EngineCore/PhysicsCore/Storage/VansCollisionLayerStorage.h"
#include "../EngineCore/PhysicsCore/VansCollisionLayerConfig.h"
#include "../EngineCore/PhysicsCore/VansClothMeshPrep.h"
#include "../EngineCore/CameraCore/VansCameraCore.h"
#include "../EngineCore/RenderCore/VansPostProcessProfile.h"
#include "../EngineCore/RenderCore/Serialization/VansPostProcessProfileJsonCodec.h"
#include "../EngineCore/RenderCore/Storage/VansPostProcessProfileStorage.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VansDrawSubmission.h"
#include "../EngineCore/RenderCore/VansCameraControlArbiter.h"
#include "../EngineCore/RenderCore/Timeline/VansVirtualCameraParameterStore.h"
#include "../EngineCore/RenderCore/GICore/VansGISettings.h"
#include "../EngineCore/RenderCore/VulkanCore/VansFrameSubmitOrchestrator.h"
#include "../EngineCore/RenderCore/VulkanCore/VansRenderGraphVulkanSync.h"
#include "../EngineCore/RenderCore/VulkanCore/VansRenderPass.h"
#include "../EngineCore/RenderCore/VulkanCore/VansRenderPassCatalog.h"
#include "../EngineCore/RenderCore/FidelityFXCore/VansFSRTypes.h"
#include "../EngineCore/RenderCore/UpscalingCore/VansUpscalerHistoryState.h"
#include "../EngineCore/RenderCore/UpscalingCore/VansUpscalerManager.h"
#include "../EngineCore/RenderCore/UpscalingCore/VansUpscaleResolutionPolicy.h"
#include "../EngineCore/RenderCore/UpscalingCore/VansTemporalJitterSequence.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VansGraphicsDevice.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/RenderCore/VansMainCameraVisibility.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansRenderNode.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMainCameraVisibilityState.h"
#include "../EngineCore/RenderCore/VansRenderFrame.h"
#include "../EngineCore/RenderCore/VansRenderSystem.h"
#include "../EngineCore/RenderCore/WaterCore/VansWaterConfig.h"
#include "../EngineCore/RenderCore/TerrainCore/VansTerrainLod.h"
#include "../EngineCore/TerrainCore/VansTerrainAsset.h"
#include "../EngineCore/TerrainCore/VansTerrainBrush.h"
#include "../EngineCore/TerrainCore/Serialization/VansTerrainAssetCodec.h"
#include "../EngineCore/TerrainCore/Serialization/VansTerrainImageCodec.h"
#include "../EngineCore/AuthoringCore/Terrain/VansTerrainAuthoringSession.h"
#include "../EngineCore/TerrainCore/VansTerrainSurfaceQuery.h"
#include "../EngineCore/RenderCore/AtmosphereCore/VansAtmosphereMath.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneProjectResourceBuilder.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneAssembly.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneResourceArtifactPrewarmer.h"
#include "../EngineCore/RenderCore/VansTemporalProjection.h"
#include "../EngineCore/RenderCore/ShadowCore/VansPunctualShadowManager.h"
#include "../EngineCore/RenderCore/ShadowCore/VansPunctualShadowFrameState.h"
#include "../EngineCore/RenderCore/BRDFData/VansLight.h"
#include "../EngineCore/RuntimeCore/VansPackageManifest.h"
#include "../EngineCore/RuntimeCore/VansCharacterMotion.h"
#include "../EngineCore/RuntimeCore/VansCharacterLocomotionResolver.h"
#include "../EngineCore/RuntimeCore/VansCharacterTrajectoryGenerator.h"
#include "../EngineCore/RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../EngineCore/RuntimeCore/VansFramePhase.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/Util/VansProfiler.h"
#include "../EngineCore/Util/VansFileFingerprint.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/Transform/VansTransformGraph.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include "../EngineCore/SceneCore/VansSceneAnimationComponentReader.h"
#include "../EngineCore/SceneCore/VansSceneEnvironmentNodeConfigReader.h"
#include "../EngineCore/SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../EngineCore/SceneCore/Storage/VansVegetationConfigStorage.h"
#include "../EngineCore/SceneCore/VansSceneCameraMediaComponentReader.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/SceneCore/VansSceneEntityFactory.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/SceneCore/VansSceneRenderSettingsConfigReader.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/SceneCore/Storage/VansSceneFileStorage.h"
#include "../EngineCore/TimelineRuntime/VansTimelineApplierRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineClockRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineEvaluator.h"
#include "../EngineCore/TimelineRuntime/VansTimelinePreAnimatedState.h"
#include "../EngineCore/TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../EngineCore/TimelineCore/VansTimelineDependencyBuilder.h"
#include "../EngineCore/TimelineCore/VansTimelineCompiler.h"
#include "../EngineCore/TimelineCore/VansTimelineSerialization.h"
#include "../EngineCore/TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../EngineCore/TimelineCore/VansTimelineValidator.h"
#include "../EngineCore/Timeline/VansEngineTimelineRegistry.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineEditService.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineCommandMap.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentRegistry.h"
#include "../EngineCore/EditorCore/VansAssetDocumentTypeRegistry.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentEditService.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/EditorCore/VansEditorRuntimePreviewProjector.h"
#include "../EngineCore/EditorCore/VansEditorPropertyDescriptorRegistry.h"
#include "../EngineCore/EditorCore/Animation/VansAnimationRigSaveService.h"
#include "../EngineCore/EditorCore/VansEditorAssetSaveService.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AssetCore/Storage/VansMaterialAuthoringAssetStorage.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/AssetCore/Storage/VansSkinProfileStorage.h"
#include "../EngineCore/AssetCore/Storage/VansStagedFileTransaction.h"
#include "../EngineCore/AnimationCore/VansAnimationClip.h"
#include "../EngineCore/AnimationCore/VansAnimationController.h"
#include "../EngineCore/AnimationCore/VansAnimationNode.h"
#include "../EngineCore/AnimationCore/VansAnimGraph.h"
#include "../EngineCore/AnimationCore/VansAnimatorIO.h"
#include "../EngineCore/AnimationCore/VansAnimatorRuntimeCompiler.h"
#include "../EngineCore/AnimationCore/VansAnimatorValidator.h"
#include "../EngineCore/AnimationCore/VansAnimationSampler.h"
#include "../EngineCore/AnimationCore/VansPoseMath.h"
#include "../EngineCore/AnimationCore/VansPosePayloadMixer.h"
#include "../EngineCore/AnimationCore/VansAnimationLayer.h"
#include "../EngineCore/AnimationCore/VansSkinnedMeshLoader.h"
#include "../EngineCore/AnimationCore/Retargeting/VansRetargetProcessor.h"
#include "../EngineCore/AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansRetargetProfileStorage.h"
#include "../EngineCore/AnimationCore/Serialization/VansRetargetProfileJsonCodec.h"
#include "../EngineCore/AICore/Serialization/VansAIBehaviorJsonCodec.h"
#include "../EngineCore/PhysicsCore/Serialization/VansRagdollProfileJsonCodec.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansRootMotionYaw.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansTurnInPlaceWarping.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationAuthoringBridge.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationPreviewAdoptService.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationPreviewRigAuthoringService.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationPreviewParameterEditing.h"
#include "../EngineCore/EngineAPILayer/Private/RuntimeGeneratedMaterialAssetService.h"
#include "../EngineCore/EngineAPILayer/Private/VansLocalFogFieldPreviewService.h"
#include "../EngineCore/EngineAPILayer/Public/EngineDTOs.h"
#include "../EngineCore/ParticleCore/VansParticleRuntime.h"
#include "../EngineCore/ParticleCore/Serialization/VansParticleAssetJsonCodec.h"
#include "TimelineRefactorContractTests.h"
#include "GAFContractTests.h"
#include "ProceduralAnimationContractTests.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/ProjectSystem/VansEnginePaths.h"
#include "../EngineCore/ProjectSystem/VansProjectSettingsData.h"
#include "../EngineCore/ProjectSystem/Serialization/VansProjectConfigJsonCodec.h"
#include "../EngineCore/ProjectSystem/Serialization/VansProjectSettingsJsonCodec.h"
#include "../EngineCore/ProjectSystem/Storage/VansProjectSettingsStorage.h"
#include "../EngineCore/GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/ProjectSystem/Storage/VansProjectConfigStorage.h"
#include "../EngineCore/RuntimeUI/VansUIAssetResolver.h"
#include "../EngineCore/RuntimeUI/Public/VansUIEvents.h"
#include "../EngineCore/RuntimeUI/Public/VansUIRuntimeHandles.h"
#include "../EngineCore/ScriptCore/VansLuaUIBridge.h"
#include "../EngineCore/ScriptCore/VansLuaValueConverter.h"
#include "../EngineCore/ScriptCore/VansScriptComponentReader.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/ScriptCore/VansLuaScriptInspectorService.h"
#include "../EngineCore/SceneRuntime/Transform/VansTransformStore.h"

#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <new>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

namespace
{
Vans::VansSerializedValue BuildValidEnvironmentSettingsForTest();

class TestRenderSystemDevice final : public VansGraphics::VansGraphicsDevice
{
public:
	TestRenderSystemDevice()
	{
		m_RenderWidth = 1280;
		m_RenderHeight = 720;
	}
	~TestRenderSystemDevice() override
	{
		if (destructionCount)
			++(*destructionCount);
	}

	bool BeforeRendering() override
	{
		beforeThread = std::this_thread::get_id();
		++beforeCount;
		return beforeSucceeded;
	}
	void PrepareRenderingFrame() override
	{
		prepareThread = std::this_thread::get_id();
		prepareFrontendOrder = NextOrder();
	}
	VansGraphics::VansRenderSubmissionPrepareResult PrepareRenderSubmission(
		VansGraphics::VansRenderFrameSubmission& submission) override
	{
		prepareThread = std::this_thread::get_id();
		prepareBackendOrder = NextOrder();
		preparedFrameId = submission.Frame().FrameId();
		preparedWorkSerial = submission.WorkSerial();
		preparedMutationCount = submission.MutationsBeforeFrame().Size();
		if (forcedPrepareStatus != VansGraphics::VansRenderSubmissionPrepareStatus::Ready)
			return { forcedPrepareStatus, forcedPrepareError };
		if (!renderWorld.Apply(submission.MutationsBeforeFrame()))
		{
			return {
				VansGraphics::VansRenderSubmissionPrepareStatus::FatalProtocolViolation,
				"Test render mutation was rejected"
			};
		}
		return { VansGraphics::VansRenderSubmissionPrepareStatus::Ready, {} };
	}
	void Rendering() override
	{
		renderThread = std::this_thread::get_id();
		renderOrder = NextOrder();
		++renderCount;
		std::unique_lock<std::mutex> lock(renderGateMutex);
		if (blockRendering)
		{
			renderGateCondition.wait(lock, [this] { return releaseRendering; });
			blockRendering = false;
		}
	}
	void Present() override
	{
		presentThread = std::this_thread::get_id();
		presentOrder = NextOrder();
		++presentCount;
	}
	void AfterRendering() override
	{
		afterThread = std::this_thread::get_id();
		++afterCount;
	}
	bool WaitForIdle() override
	{
		waitThread = std::this_thread::get_id();
		++waitCount;
		return true;
	}
	void OnWindowResize(std::uint32_t width, std::uint32_t height) override
	{
		resizeThread = std::this_thread::get_id();
		++resizeCount;
		lastWidth = width;
		lastHeight = height;
	}
	void InitializeGpuProfiler() override { ++profilerInitializeCount; }
	void* GetNativeGraphicsDevice() override { return nullptr; }
	void* GetNativeCommandBuffer() override { return nullptr; }

	std::uint32_t beforeCount = 0;
	std::uint32_t* destructionCount = nullptr;
	bool beforeSucceeded = true;
	std::uint32_t renderCount = 0;
	std::uint32_t presentCount = 0;
	std::uint32_t afterCount = 0;
	std::uint32_t waitCount = 0;
	std::uint32_t resizeCount = 0;
	std::uint32_t profilerInitializeCount = 0;
	std::uint32_t lastWidth = 0;
	std::uint32_t lastHeight = 0;
	int* sequence = nullptr;
	int prepareFrontendOrder = 0;
	int prepareBackendOrder = 0;
	int renderOrder = 0;
	int presentOrder = 0;
	VansGraphics::VansRenderFrameId preparedFrameId;
	VansGraphics::VansRenderWorkSerial preparedWorkSerial;
	std::size_t preparedMutationCount = 0;
	VansGraphics::VansRenderWorld renderWorld;
	VansGraphics::VansRenderSubmissionPrepareStatus forcedPrepareStatus =
		VansGraphics::VansRenderSubmissionPrepareStatus::Ready;
	std::string forcedPrepareError;
	std::thread::id beforeThread;
	std::thread::id prepareThread;
	std::thread::id renderThread;
	std::thread::id presentThread;
	std::thread::id resizeThread;
	std::thread::id waitThread;
	std::thread::id afterThread;
	std::mutex renderGateMutex;
	std::condition_variable renderGateCondition;
	bool blockRendering = false;
	bool releaseRendering = false;

private:
	int NextOrder() { return sequence ? ++(*sequence) : 0; }
};

class TestRenderFrameSource final : public VansGraphics::IVansRenderFrameSource
{
public:
	std::optional<VansGraphics::VansRenderFrameSourceOutput> PrepareMainThreadRenderFrame(
		const VansGraphics::VansRenderFramePreparationContext& context) override
	{
		prepareOrder = sequence ? ++(*sequence) : 0;
		++prepareCount;
		lastFrameId = context.frameId;
		lastViewportWidth = context.view.viewportWidth;
		lastViewportHeight = context.view.viewportHeight;
		if (!prepareSucceeded)
			return std::nullopt;
		VansGraphics::VansRenderFrameSourceOutput output;
		if (useUpdatesAfterFirst && prepareCount > 1)
		{
			output.mutationsBeforeFrame.AddUpdate(
				VansGraphics::VansRenderProxyHandle{ 0u, 1u },
				VansGraphics::VansRenderProxyStaticData{ 5u, true });
		}
		else
		{
			output.mutationsBeforeFrame.AddCreate(
				VansGraphics::VansRenderProxyHandle{ 0u, 1u },
				VansGraphics::VansRenderProxyStaticData{ 5u, true });
		}
		return output;
	}

	std::uint32_t prepareCount = 0;
	bool prepareSucceeded = true;
	bool useUpdatesAfterFirst = false;
	int* sequence = nullptr;
	int prepareOrder = 0;
	VansGraphics::VansRenderFrameId lastFrameId;
	std::uint32_t lastViewportWidth = 0;
	std::uint32_t lastViewportHeight = 0;
};

class TestRenderThreadTransaction final
	: public VansGraphics::IVansRenderThreadTransaction
{
public:
	TestRenderThreadTransaction(
		std::thread::id& executedThread,
		std::uint32_t& executeCount)
		: m_ExecutedThread(executedThread), m_ExecuteCount(executeCount)
	{
	}

	bool Execute(VansGraphics::VansGraphicsDevice&) override
	{
		m_ExecutedThread = std::this_thread::get_id();
		++m_ExecuteCount;
		return true;
	}

private:
	std::thread::id& m_ExecutedThread;
	std::uint32_t& m_ExecuteCount;
};

bool TestRenderSystemLifecycleContract()
{
	// 引擎通过基类指针释放设备，必须执行具体后端的析构。
	std::uint32_t destructionCount = 0;
	{
		auto ownedDevice = std::make_unique<TestRenderSystemDevice>();
		ownedDevice->destructionCount = &destructionCount;
		std::unique_ptr<VansGraphics::VansGraphicsDevice> baseOwner = std::move(ownedDevice);
	}
	if (destructionCount != 1)
		return false;

	TestRenderSystemDevice device;
	TestRenderFrameSource frameSource;
	int frameSequence = 0;
	device.sequence = &frameSequence;
	frameSource.sequence = &frameSequence;
	VansGraphics::VansRenderSystem renderSystem(device, frameSource);
	const std::thread::id mainThread = std::this_thread::get_id();
	if (!renderSystem.InitializeFrameExecution() ||
		renderSystem.GetState() != VansGraphics::VansRenderSystemState::Running ||
		device.beforeCount != 1 || renderSystem.GetRenderWidth() != 1280 ||
		renderSystem.GetRenderHeight() != 720)
	{
		return false;
	}

	VansGraphics::VansCamera camera(&device);
	if (!renderSystem.BeginFrame(camera) ||
		!renderSystem.IsFrameOpen() ||
		frameSource.prepareOrder != 1 ||
		device.prepareFrontendOrder != 0 ||
		device.prepareBackendOrder != 0 ||
		device.renderOrder != 0 ||
		frameSource.prepareCount != 1 ||
		frameSource.lastViewportWidth != 1280 ||
		frameSource.lastViewportHeight != 720 ||
		device.renderWorld.Resolve({ 0u, 1u }) != nullptr)
	{
		return false;
	}
	const VansGraphics::VansRenderFrameSubmitResult submitResult = renderSystem.SubmitFrame();
	const auto frameOutcome = renderSystem.GetFrameOutcome(submitResult.frameId);
	if (!submitResult || renderSystem.IsFrameOpen() ||
		device.prepareFrontendOrder != 2 ||
		device.prepareBackendOrder != 3 ||
		device.renderOrder != 4 ||
		device.presentOrder != 5 || device.presentCount != 1 ||
		frameSource.lastFrameId != device.preparedFrameId ||
		device.preparedWorkSerial != VansGraphics::VansRenderWorkSerial(0) ||
		device.preparedMutationCount != 1 ||
		device.renderWorld.Resolve({ 0u, 1u }) == nullptr ||
		device.beforeThread == mainThread ||
		device.prepareThread != device.beforeThread ||
		device.renderThread != device.beforeThread ||
		device.presentThread != device.beforeThread ||
		!frameOutcome.has_value() ||
		frameOutcome->status != VansGraphics::VansRenderFrameStatus::PresentQueued ||
		frameOutcome->frameId != submitResult.frameId ||
		frameOutcome->workSerial != VansGraphics::VansRenderWorkSerial(0))
	{
		return false;
	}

	if (!renderSystem.RequestSurfaceResize(1600, 900) ||
		device.resizeCount != 1 || device.lastWidth != 1600 || device.lastHeight != 900 ||
		device.resizeThread != device.beforeThread)
	{
		return false;
	}

	std::thread::id transactionThread;
	std::uint32_t transactionCount = 0;
	if (!renderSystem.ExecuteRenderThreadTransaction(
			std::make_unique<TestRenderThreadTransaction>(
				transactionThread, transactionCount)) ||
		transactionCount != 1 || transactionThread != device.beforeThread ||
		transactionThread == mainThread || device.waitCount != 1)
	{
		return false;
	}

	renderSystem.InitializeGpuProfiler();
	if (device.profilerInitializeCount != 1)
		return false;

	// 同一 work generation 上的重复 idle 必须合并，避免连续触发
	// device-wide stall；Quiesce 复用同一条已完成 barrier。
	if (!renderSystem.WaitForIdle() || !renderSystem.WaitForIdle() ||
		device.waitCount != 2 || !renderSystem.Quiesce() ||
		renderSystem.GetState() != VansGraphics::VansRenderSystemState::Quiesced ||
		device.waitCount != 2 || device.waitThread != device.beforeThread)
	{
		return false;
	}

	renderSystem.ShutdownFrameExecution();
	if (renderSystem.GetState() != VansGraphics::VansRenderSystemState::Stopped ||
		device.afterCount != 1 || device.afterThread != device.beforeThread)
	{
		return false;
	}

	return renderSystem.WaitForIdle() && device.waitCount == 2;
}

bool TestProfilerSnapshotContract()
{
#if VANS_PROFILER_ENABLED
	auto& profiler = Vans::VansProfiler::Get();
	profiler.SetPaused(false);
	profiler.SetCaptureEnabled(true);
	profiler.BeginFrame();
	{
		Vans::VansCpuScopeTimer outer("ProfilerContract::Outer", Vans::ProfileCategory::Frame);
		{
			Vans::VansCpuScopeTimer inner("ProfilerContract::Inner", Vans::ProfileCategory::JobSystem);
		}
	}
	profiler.EndFrame();

	// The following non-blocking frame boundary publishes the completed snapshot.
	profiler.BeginFrame();
	const Vans::ProfileFrame& frame = profiler.GetTimeline();
	const Vans::ProfileEvent* outerEvent = nullptr;
	const Vans::ProfileEvent* innerEvent = nullptr;
	for (uint32_t eventIndex = 0u; eventIndex < frame.eventCount; ++eventIndex)
	{
		if (std::string(frame.events[eventIndex].name) == "ProfilerContract::Outer")
			outerEvent = &frame.events[eventIndex];
		else if (std::string(frame.events[eventIndex].name) == "ProfilerContract::Inner")
			innerEvent = &frame.events[eventIndex];
	}
	const bool snapshotValid = outerEvent != nullptr && innerEvent != nullptr &&
		innerEvent->parentEventId == outerEvent->eventId &&
		outerEvent->endUs >= outerEvent->startUs &&
		innerEvent->endUs >= innerEvent->startUs &&
		frame.droppedCpuEvents == 0u && !frame.overflow;
	const uint64_t frozenFrameIndex = frame.frameIndex;
	profiler.EndFrame();

	profiler.SetPaused(true);
	profiler.BeginFrame();
	profiler.EndFrame();
	const bool pauseValid = profiler.GetTimeline().frameIndex == frozenFrameIndex;

	profiler.SetPaused(false);
	profiler.SetCaptureEnabled(false);
	profiler.BeginFrame();
	profiler.EndFrame();
	return snapshotValid && pauseValid;
#else
	return true;
#endif
}

bool TestProfilerStableCaptureContract()
{
#if VANS_PROFILER_ENABLED
    using Capture = Vans::VansProfileCapture;
    Capture::Settings settings;
    settings.windowFrames = 4;
    settings.measurementFrames = 4;
    settings.minimumWindowSeconds = 2.0;
    Capture capture;
    capture.Start(settings, 10);
    auto frame = std::make_unique<Vans::ProfileFrame>();
    frame->frameIndex = 10;
    frame->gpuExpected = frame->gpuComplete = true;
    frame->eventCount = 1;
    frame->events[0].flags = Vans::ProfileEventFlagGpu;
    std::strcpy(frame->events[0].name, "Frame");
    frame->events[0].endUs = 8000.0;
    frame->events[1].flags = Vans::ProfileEventFlagGpu;
    std::strcpy(frame->events[1].name, "Intermittent");
    frame->events[1].endUs = 2000.0;
    double now = 0.0;
    const auto feed = [&](double cpu = 10000.0, double gpu = 8000.0)
    {
        frame->frameDurationUs = cpu;
        frame->gpuDurationUs = gpu;
        capture.Observe(*frame, now);
        ++frame->frameIndex;
        now += 1.0;
    };
    const auto check = [](bool condition, const char* label)
    {
        if (!condition)
            std::cerr << "Profiler stability contract failed: " << label << '\n';
        return condition;
    };
    feed(); feed(); feed(); feed(100000.0, 80000.0);
    if (!check(capture.GetStableWindows() == 0 && capture.GetRestartCount() == 1, "startup spike rejected"))
        return false;
    for (int i = 0; i < 12; ++i) feed();
    if (!check(capture.GetPhase() == Capture::Phase::Measuring && capture.GetSamples().empty(), "settling excluded"))
        return false;
    feed(); feed(); feed(); feed(100000.0);
    if (!check(capture.GetPhase() == Capture::Phase::Settling && capture.GetDiscardedMeasurementSamples() == 4,
        "whole measurement batch discarded on late CPU stall"))
        return false;
    for (int i = 0; i < 12; ++i) feed();
    // 同一帧的未完成结果和重复快照均不能提前增加样本。
    frame->gpuComplete = false;
    capture.Observe(*frame, now);
    if (!check(capture.GetSamples().empty(), "pending query ignored")) return false;
    frame->gpuComplete = true;
    for (int i = 0; i < 4; ++i)
    {
        frame->eventCount = i % 2 == 0 ? 2 : 1;
        feed();
        --frame->frameIndex;
        capture.Observe(*frame, now);
        ++frame->frameIndex;
        if (!check(capture.GetSamples().size() == static_cast<size_t>(i + 1), "duplicate ignored")) return false;
    }
    if (!check(capture.GetPhase() == Capture::Phase::Complete && capture.GetMissingFrames() == 0,
        "stable contiguous batch accepted")) return false;

    const auto reportDir = std::filesystem::temp_directory_path()
        / ("ForestProfilerCapture_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!check(capture.DumpJson(reportDir.string().c_str()), "report written")) return false;
    nlohmann::json report;
    { std::ifstream input(reportDir / "profile_capture.json"); input >> report; }
    std::filesystem::remove(reportDir / "profile_capture.json");
    std::filesystem::remove(reportDir);
    if (!check(report.at("accepted").get<bool>() && report.at("sampleCount") == 4
        && report.at("discardedMeasurementSamples") == 4
        && report.at("cpuFrameUs").at("median") == 10000.0
        && report.at("gpuScopesUs").at("GPU/Intermittent").at("mean") == 1000.0,
        "report and absent-scope accounting")) return false;

    // 直接父子扣减、同名多次调用、同名不同线程和缺席帧归零。
    settings.minimumWindowSeconds = 0.0;
    capture.Start(settings, frame->frameIndex);
    frame->eventCount = 1;
    for (int i = 0; i < 12; ++i) feed();
    frame->trackCount = 2;
    frame->tracks[0].trackId = 1;
    frame->tracks[1].trackId = 2;
    std::strcpy(frame->tracks[0].name, "Main");
    std::strcpy(frame->tracks[1].name, "Worker");
    const auto cpuEvent = [&](int index, uint32_t id, uint32_t parent, uint32_t track,
        const char* name, double start, double end, uint16_t flags = 0)
    {
        auto& event = frame->events[index];
        event = {};
        event.eventId = id;
        event.parentEventId = parent;
        event.trackId = track;
        std::strcpy(event.name, name);
        event.startUs = start;
        event.endUs = end;
        event.flags = flags;
    };
    cpuEvent(1, 10, 0, 1, "Parent", 0, 100);
    cpuEvent(2, 11, 10, 1, "Child", 10, 40);
    cpuEvent(3, 12, 10, 1, "Child", 50, 60);
    cpuEvent(4, 13, 11, 1, "Leaf", 20, 30);
    cpuEvent(5, 14, 10, 2, "Parent", 0, 20000, Vans::ProfileEventFlagWait);
    for (int i = 0; i < 4; ++i)
    {
        frame->eventCount = i % 2 == 0 ? 6 : 1;
        feed();
    }
    if (!check(capture.DumpJson(reportDir.string().c_str()), "CPU report written")) return false;
    { std::ifstream input(reportDir / "profile_capture.json"); input >> report; }
    std::filesystem::remove_all(reportDir);
    const auto findScope = [&](uint32_t track, const char* name) -> nlohmann::json
    {
        for (const auto& scope : report.at("cpuScopes"))
            if (scope.at("trackId") == track && scope.at("name") == name) return scope;
        return {};
    };
    if (!check(findScope(1, "Parent").at("inclusiveUs").at("mean") == 50.0
        && findScope(1, "Parent").at("exclusiveUs").at("mean") == 30.0
        && findScope(1, "Child").at("inclusiveUs").at("mean") == 20.0
        && findScope(1, "Child").at("exclusiveUs").at("mean") == 15.0
        && findScope(1, "Child").at("callsPerFrame").at("mean") == 1.0
        && findScope(2, "Parent").at("inclusiveUs").at("mean") == 10000.0
        && findScope(2, "Parent").at("wait") == true, "CPU hierarchy, tracks and absent samples")) return false;
    settings.minimumWindowSeconds = 2.0;

    capture.Start(settings, frame->frameIndex);
    frame->eventCount = 1;
    for (int i = 0; i < 12; ++i) feed();
    feed(); feed();
    frame->droppedGpuEvents = 1;
    feed();
    frame->droppedGpuEvents = 0;
    if (!check(capture.GetPhase() == Capture::Phase::Settling
        && capture.GetDiscardedMeasurementSamples() == 2, "dropped GPU event resets batch")) return false;
    feed();
    frame->frameIndex += 2;
    feed();
    if (!check(capture.GetMissingFrames() == 2 && capture.GetStableWindows() == 0, "missing frames reset window")) return false;
    for (int i = 0; i < 3; ++i) feed();
    for (int i = 0; i < 4; ++i) feed(10000.0, 12000.0);
    if (!check(capture.GetStableWindows() == 0, "GPU drift rejected independently")) return false;

    settings.minimumWindowSeconds = 10.0;
    capture.Start(settings, frame->frameIndex);
    for (int i = 0; i < 4; ++i) feed();
    if (!check(capture.GetStableWindows() == 0, "frame count alone cannot qualify")) return false;
    for (int i = 0; i < 7; ++i) feed();
    if (!check(capture.GetStableWindows() == 1, "wall duration also required")) return false;
    capture.Cancel();
    feed();
    if (!check(!capture.IsActive() && capture.GetStableWindows() == 1, "cancel freezes capture")) return false;
    const auto stats = Capture::Summarize({1, 2, 3, 4, 5});
    if (!check(stats.mean == 3.0 && stats.median == 3.0 && std::abs(stats.p95 - 4.8) < 1e-9
        && std::abs(stats.p99 - 4.96) < 1e-9, "percentiles")) return false;
    std::cout << "Profiler stability contract PASS: warmup spikes, batch rejection, missing/duplicate frames, wall duration, GPU scopes and percentiles\n";
#endif
    return true;
}

bool TestProfilerOutOfOrderCompletionContract()
{
#if VANS_PROFILER_ENABLED
    auto& profiler = Vans::VansProfiler::Get();
    Vans::VansProfileCapture::Settings settings;
    settings.windowFrames = 2;
    settings.stableWindows = 2;
    settings.measurementFrames = 2;
    settings.minimumWindowSeconds = 0.0;
    // 此用例验证组帧顺序；耗时门槛由上一个合成时序用例独立验证。
    settings.relativeDrift = settings.maximumP95Spread = settings.maximumSpikeRatio = 1000.0;
    profiler.SetCaptureEnabled(true);
    profiler.StartStableCapture(settings);
    const auto cpuFrame = [&]()
    {
        profiler.BeginFrame();
        const uint64_t index = profiler.GetActiveFrameIndex();
        profiler.MarkCurrentFrameHasRender();
        profiler.EndFrame();
        Vans::VansCpuProfiler::Get().BindCurrentThreadToFrame(index);
        Vans::VansCpuProfiler::Get().EndRenderFrame(index);
        return index;
    };
    const auto gpuFrame = [&](uint64_t index)
    {
        Vans::VansGpuResolvedFrame gpu;
        gpu.frameIndex = index;
        gpu.eventCount = gpu.laneEventCount[0] = 1;
        gpu.durationUs = 10.0;
        gpu.events[0].trackId = gpu.events[0].eventId = 1;
        gpu.events[0].flags = Vans::ProfileEventFlagGpu;
        gpu.events[0].endUs = 10.0;
        std::strcpy(gpu.events[0].name, "Reordered GPU");
        profiler.SubmitGpuFrame(gpu);
    };
    const uint64_t first = cpuFrame();
    const uint64_t second = cpuFrame();
    gpuFrame(second);
    const uint64_t third = cpuFrame();
    const bool waiting = profiler.GetStableCapture().GetNextFrameIndex() == first;
    gpuFrame(first);
    const uint64_t fourth = cpuFrame();
    const auto& capture = profiler.GetStableCapture();
    const bool recovered = capture.GetNextFrameIndex() == second + 1
        && capture.GetStableWindows() == 1 && capture.GetMissingFrames() == 0;
    profiler.CancelStableCapture();
    gpuFrame(third);
    gpuFrame(fourth);
    profiler.SetCaptureEnabled(false);
    profiler.BeginFrame();
    profiler.EndFrame();
    if (!waiting || !recovered)
    {
        std::cerr << "Profiler out-of-order completion contract failed: first=" << first
            << " second=" << second << " waiting=" << waiting << " recovered=" << recovered
            << " next=" << capture.GetNextFrameIndex() << " windows=" << capture.GetStableWindows()
            << " missing=" << capture.GetMissingFrames() << " restarts=" << capture.GetRestartCount() << '\n';
        return false;
    }
    std::cout << "Profiler out-of-order completion contract PASS: late CPU/GPU assembly retained before UI publication\n";
#endif
    return true;
}

bool TestRenderSystemStartupFailureContract()
{
	TestRenderSystemDevice device;
	device.beforeSucceeded = false;
	TestRenderFrameSource frameSource;
	{
		VansGraphics::VansRenderSystem renderSystem(device, frameSource);
		if (renderSystem.InitializeFrameExecution() || device.beforeCount != 1 ||
			device.afterCount != 1 || device.beforeThread != device.afterThread)
		{
			return false;
		}
	}
	return device.afterCount == 1;
}

bool TestRenderSystemDestructorFallbackContract()
{
	TestRenderSystemDevice device;
	TestRenderFrameSource frameSource;
	{
		VansGraphics::VansRenderSystem renderSystem(device, frameSource);
		if (!renderSystem.InitializeFrameExecution())
			return false;
		// 故意不调用 ShutdownFrameExecution：析构必须投递 Stop 并回收线程。
	}
	return device.beforeCount == 1 && device.afterCount == 1 &&
		device.beforeThread == device.afterThread;
}

bool TestRenderSystemPrepareFailureContract()
{
	using namespace VansGraphics;
	{
		TestRenderSystemDevice device;
		TestRenderFrameSource frameSource;
		VansRenderSystem renderSystem(device, frameSource);
		if (!renderSystem.InitializeFrameExecution())
			return false;
		VansCamera camera(&device);
		device.forcedPrepareStatus = VansRenderSubmissionPrepareStatus::RecoverableFailure;
		device.forcedPrepareError = "Injected recoverable prepare failure";
		if (!renderSystem.BeginFrame(camera) || !renderSystem.IsFrameOpen() ||
			device.renderCount != 0 || device.presentCount != 0)
		{
			return false;
		}
		const VansRenderFrameId failedFrame = renderSystem.GetCurrentFrameId();
		const VansRenderFrameSubmitResult failedSubmit = renderSystem.SubmitFrame();
		const auto failedOutcome = renderSystem.GetFrameOutcome(failedFrame);
		if (failedSubmit.status != VansRenderFrameSubmitStatus::BackendFrameFailed ||
			renderSystem.IsFrameOpen() ||
			renderSystem.GetState() != VansRenderSystemState::Running ||
			!failedOutcome.has_value() ||
			failedOutcome->status != VansRenderFrameStatus::RecoverableFailure ||
			failedOutcome->error != device.forcedPrepareError ||
			device.presentCount != 0)
		{
			return false;
		}

		device.forcedPrepareStatus = VansRenderSubmissionPrepareStatus::Ready;
		device.forcedPrepareError.clear();
		if (!renderSystem.BeginFrame(camera) || !renderSystem.SubmitFrame())
			return false;
		if (!renderSystem.Quiesce())
			return false;
		renderSystem.ShutdownFrameExecution();
		if (renderSystem.GetState() != VansRenderSystemState::Stopped)
			return false;
	}

	{
		TestRenderSystemDevice device;
		TestRenderFrameSource frameSource;
		VansRenderSystem renderSystem(device, frameSource);
		if (!renderSystem.InitializeFrameExecution())
			return false;
		VansCamera camera(&device);
		device.forcedPrepareStatus = VansRenderSubmissionPrepareStatus::FatalProtocolViolation;
		device.forcedPrepareError = "Injected mutation protocol violation";
		if (!renderSystem.BeginFrame(camera) || !renderSystem.IsFrameOpen())
			return false;
		const VansRenderFrameId failedFrame = renderSystem.GetCurrentFrameId();
		const VansRenderFrameSubmitResult failedSubmit = renderSystem.SubmitFrame();
		const auto failedOutcome = renderSystem.GetFrameOutcome(failedFrame);
		if (failedSubmit.status != VansRenderFrameSubmitStatus::BackendFrameFailed ||
			renderSystem.IsFrameOpen() ||
			renderSystem.GetState() != VansRenderSystemState::Fatal ||
			!failedOutcome.has_value() ||
			failedOutcome->status != VansRenderFrameStatus::FatalProtocolViolation ||
			failedOutcome->error != device.forcedPrepareError ||
			device.renderCount != 0 || device.presentCount != 0 ||
			renderSystem.BeginFrame(camera))
		{
			return false;
		}
		renderSystem.ShutdownFrameExecution();
		if (renderSystem.GetState() != VansRenderSystemState::Stopped)
			return false;
	}
	return true;
}

bool TestRenderSystemOneFrameLeadContract()
{
	using namespace VansGraphics;
	TestRenderSystemDevice device;
	TestRenderFrameSource frameSource;
	frameSource.useUpdatesAfterFirst = true;
	VansRenderSystem renderSystem(device, frameSource, true);
	if (!renderSystem.InitializeFrameExecution())
		return false;
	VansCamera camera(&device);
	{
		std::lock_guard<std::mutex> lock(device.renderGateMutex);
		device.blockRendering = true;
		device.releaseRendering = false;
	}
	if (!renderSystem.BeginFrame(camera))
		return false;
	const VansRenderFrameSubmitResult frame0 = renderSystem.SubmitFrame();
	if (!frame0 || renderSystem.GetFrameOutcome(frame0.frameId).has_value())
		return false;

	// Main can build N while RT is deliberately blocked in N-1.
	if (!renderSystem.BeginFrame(camera))
		return false;
	std::thread releaseThread([&device]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		{
			std::lock_guard<std::mutex> lock(device.renderGateMutex);
			device.releaseRendering = true;
		}
		device.renderGateCondition.notify_all();
	});
	const auto waitStarted = std::chrono::steady_clock::now();
	const VansRenderFrameSubmitResult frame1 = renderSystem.SubmitFrame();
	const auto waited = std::chrono::steady_clock::now() - waitStarted;
	releaseThread.join();
	if (!frame1 || waited < std::chrono::milliseconds(25) ||
		!renderSystem.GetFrameOutcome(frame0.frameId).has_value())
	{
		return false;
	}
	if (!renderSystem.Quiesce())
		return false;
	renderSystem.ShutdownFrameExecution();
	return renderSystem.GetState() == VansRenderSystemState::Stopped &&
		renderSystem.GetFrameOutcome(frame1.frameId).has_value();
}

bool TestRenderOutcomeLedgerContract()
{
	using namespace VansGraphics;
	VansRenderOutcomeLedger ledger(1, 2);
	if (!ledger.TryAcceptFrame(VansRenderWorkSerial(3), VansRenderFrameId(10)) ||
		ledger.TryAcceptFrame(VansRenderWorkSerial(4), VansRenderFrameId(11)) ||
		ledger.PendingFrameCount() != 1)
	{
		return false;
	}

	VansRenderFrameOutcome wrongOutcome;
	wrongOutcome.workSerial = VansRenderWorkSerial(99);
	wrongOutcome.frameId = VansRenderFrameId(10);
	wrongOutcome.status = VansRenderFrameStatus::PresentQueued;
	if (ledger.PublishOutcome(std::move(wrongOutcome)))
		return false;

	VansRenderFrameOutcome firstOutcome;
	firstOutcome.workSerial = VansRenderWorkSerial(3);
	firstOutcome.frameId = VansRenderFrameId(10);
	firstOutcome.status = VansRenderFrameStatus::SkippedMinimized;
	if (!ledger.PublishOutcome(std::move(firstOutcome)) ||
		!ledger.LeadCreditReleasedFor(VansRenderFrameId(10)) ||
		ledger.PendingFrameCount() != 0)
	{
		return false;
	}

	auto publishFrame = [&](std::uint64_t work, std::uint64_t frame,
		VansRenderFrameStatus status)
	{
		if (!ledger.TryAcceptFrame(VansRenderWorkSerial(work), VansRenderFrameId(frame)))
			return false;
		VansRenderFrameOutcome outcome;
		outcome.workSerial = VansRenderWorkSerial(work);
		outcome.frameId = VansRenderFrameId(frame);
		outcome.status = status;
		return ledger.PublishOutcome(std::move(outcome));
	};
	if (!publishFrame(4, 11, VansRenderFrameStatus::SubmittedWithoutPresent) ||
		!publishFrame(5, 12, VansRenderFrameStatus::RecoverableFailure) ||
		ledger.RetainedOutcomeCount() != 2 ||
		ledger.FindOutcome(VansRenderFrameId(10)).has_value())
	{
		return false;
	}

	const VansRenderOutcomeWaitResult evicted = ledger.WaitForOutcome(VansRenderFrameId(10));
	if (evicted.status != VansRenderOutcomeWaitStatus::OutcomeEvicted)
		return false;
	const VansRenderOutcomeWaitResult retained = ledger.WaitForOutcome(VansRenderFrameId(12));
	if (retained.status != VansRenderOutcomeWaitStatus::OutcomeAvailable ||
		!retained.outcome.has_value() ||
		retained.outcome->status != VansRenderFrameStatus::RecoverableFailure)
	{
		return false;
	}

	ledger.SignalStopped();
	return ledger.WaitForOutcome(VansRenderFrameId(99)).status ==
		VansRenderOutcomeWaitStatus::Stopped;
}

bool TestRenderFramePacketContract()
{
	using namespace VansGraphics;
	static_assert(!std::is_default_constructible_v<VansRenderFramePacket>);
	static_assert(!std::is_copy_constructible_v<VansRenderFramePacket>);
	static_assert(std::is_move_constructible_v<VansRenderFramePacket>);
	static_assert(!std::is_move_assignable_v<VansRenderFramePacket>);
	static_assert(!std::is_copy_constructible_v<VansRenderFrameSubmission>);
	static_assert(std::is_move_constructible_v<VansRenderFrameSubmission>);

	VansRenderViewSnapshot invalidView;
	VansRenderFrameBuilder invalidBuilder(
		VansRenderFrameId(4),
		VansLogicFrameId(7),
		VansSurfaceEpoch(2));
	if (invalidBuilder.SetView(invalidView))
		return false;

	VansRenderViewSnapshot view;
	view.viewportWidth = 1920;
	view.viewportHeight = 1080;
	view.nearClip = 0.25f;
	view.farClip = 5000.0f;
	view.historyReset = VansRenderViewHistoryReset::CameraCut;

	VansRenderFrameBuilder builder(
		VansRenderFrameId(11),
		VansLogicFrameId(19),
		VansSurfaceEpoch(3));
	VansRenderFrameTimingSnapshot timing;
	timing.elapsedSeconds = 12.5;
	timing.deltaSeconds = 1.0 / 60.0;
	timing.renderDeltaSeconds = 1.0 / 120.0;
	VansRenderSceneFrameSnapshot scene;
	scene.sceneReady = true;
	scene.sceneEpoch = 4;
	scene.mainCameraHiZCullSettings.enableTransparent = true;
	scene.mainCameraHiZCullSettings.depthBiasMeters = 1.25f;
	scene.light.prepared = true;
	scene.light.punctualShadowMapWidth = 4096;
	scene.light.frameSequence = 3.0f;
	scene.materials.prepared = true;
	scene.materials.pbr.elementStride = sizeof(VansBasePBRParam);
	scene.materials.cloth.elementStride = sizeof(VansClothGPUParam);
	scene.materials.treeLeaf.elementStride = sizeof(VansTreeLeafParamsGPU);
	scene.materials.skin.elementStride = sizeof(VansSkinGPUParam);
	scene.materials.custom.elementStride = sizeof(VansCustomMaterialPayload);
	scene.gi.prepared = true;
	scene.postProcess.prepared = true;
	VansPointLight pointLight{};
	pointLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	scene.light.pointLights.emplace_back(pointLight);
	VansRenderTransformFrameData transform;
	transform.proxy = { 7u, 3u };
	transform.position = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
	scene.transforms.emplace_back(transform);
	VansRenderMainCameraCullInput cullInput;
	cullInput.proxy = { 7u, 3u };
	cullInput.nodeName = "OwnedCullInput";
	cullInput.cullClass = VansMainCameraCullClass::Opaque;
	cullInput.hasBounds = false;
	scene.mainCameraCullInputs.emplace_back(std::move(cullInput));
	scene.punctualShadow.prepared = true;
	VansPunctualShadowLightInput shadowLight;
	shadowLight.stableLightId = 17;
	shadowLight.type = VansPunctualShadowLightType::Point;
	shadowLight.gpuLightIndex = 0;
	scene.punctualShadow.lights.emplace_back(shadowLight);
	VansRenderPunctualShadowCasterInput shadowCaster;
	shadowCaster.proxy = { 7u, 3u };
	shadowCaster.shadowCasterMask = 0x3u;
	scene.punctualShadow.casters.emplace_back(shadowCaster);
	scene.features.hasDecal = true;
	if (!builder.SetView(view) || builder.SetView(view) ||
		!builder.SetTiming(timing) || builder.SetTiming(timing) ||
		!builder.SetScene(scene) || builder.SetScene(scene))
		return false;

	auto packet = std::move(builder).Finalize();
	const bool packetValid = packet.has_value() &&
		packet->FrameId() == VansRenderFrameId(11) &&
		packet->SourceLogicFrameId() == VansLogicFrameId(19) &&
		packet->SurfaceEpoch() == VansSurfaceEpoch(3) &&
		packet->Timing().deltaSeconds == timing.deltaSeconds &&
		packet->Timing().renderDeltaSeconds == timing.renderDeltaSeconds &&
		packet->View().viewportWidth == 1920 &&
		packet->View().viewportHeight == 1080 &&
		packet->View().nearClip == 0.25f &&
		packet->View().farClip == 5000.0f &&
		packet->Scene().sceneReady &&
		packet->Scene().sceneEpoch == 4 &&
		packet->Scene().mainCameraHiZCullSettings.enableTransparent &&
		packet->Scene().mainCameraHiZCullSettings.depthBiasMeters == 1.25f &&
		packet->Scene().light.IsComplete() &&
		packet->Scene().light.pointLights.size() == 1 &&
		packet->Scene().light.pointLights.front().m_ShadowMetaIndex == VANS_INVALID_SHADOW_INDEX &&
		packet->Scene().transforms.size() == 1 &&
		packet->Scene().transforms.front().proxy == VansRenderProxyHandle{ 7u, 3u } &&
		packet->Scene().transforms.front().position == glm::vec4(1.0f, 2.0f, 3.0f, 1.0f) &&
		packet->Scene().mainCameraCullInputs.size() == 1 &&
		packet->Scene().mainCameraCullInputs.front().proxy == VansRenderProxyHandle{ 7u, 3u } &&
		packet->Scene().mainCameraCullInputs.front().nodeName == "OwnedCullInput" &&
		packet->Scene().punctualShadow.IsComplete() &&
		packet->Scene().punctualShadow.lights.size() == 1 &&
		packet->Scene().punctualShadow.lights.front().stableLightId == 17 &&
		packet->Scene().punctualShadow.casters.size() == 1 &&
		packet->Scene().punctualShadow.casters.front().proxy == VansRenderProxyHandle{ 7u, 3u } &&
		packet->Scene().punctualShadowJobs.empty() &&
		!packet->Scene().features.hasPunctualShadowJobs &&
		packet->Scene().features.hasDecal &&
		!VansRenderPassCatalog::IsPassEnabled(
			VansRenderPassCondition::HasPunctualShadowJobs,
			packet->Scene().features) &&
		packet->Timing().elapsedSeconds == 12.5 &&
		packet->Timing().deltaSeconds == 1.0 / 60.0 &&
		HasRenderViewHistoryReset(
			packet->View().historyReset,
			VansRenderViewHistoryReset::CameraCut);
	if (!packetValid)
		return false;

	const VansRenderTransformFrameData* originalTransformStorage =
		packet->Scene().transforms.data();
	VansRenderFrameSubmission submission(
		VansRenderWorkSerial(5),
		VansRenderMutationBatch{},
		std::move(*packet));
	VansRenderSceneFrameSnapshot consumedScene;
	return submission.ConsumeSceneForRendering(consumedScene) &&
		!submission.ConsumeSceneForRendering(consumedScene) &&
		consumedScene.transforms.size() == 1 &&
		consumedScene.transforms.data() == originalTransformStorage;
}

bool TestMainCameraVisibilityBackendOwnershipContract()
{
	using namespace VansGraphics;
	static_assert(VansMainCameraVisibilityState::kFrameSlotCount == 2);
	static_assert(!std::is_copy_constructible_v<VansMainCameraVisibilityState>);

	VansMainCameraVisibilityState backendState;
	VansRenderViewSnapshot view;
	view.view = glm::mat4(1.0f);
	view.projection = glm::mat4(1.0f);
	view.position = glm::vec3(0.0f);
	view.forward = glm::vec3(0.0f, 0.0f, -1.0f);
	view.viewportWidth = 1280;
	view.viewportHeight = 720;
	view.fieldOfViewRadians = glm::radians(60.0f);
	view.nearClip = 0.1f;
	view.farClip = 1000.0f;

	VansRenderSceneFrameSnapshot scene;
	scene.sceneReady = true;
	scene.mainCameraHiZCullSettings.depthBiasMeters = 0.75f;
	VansRenderMainCameraCullInput input;
	input.proxy = { 12u, 5u };
	input.nodeName = "BackendOwnedHiZCandidate";
	input.cullClass = VansMainCameraCullClass::Opaque;
	input.bounds = MakeRenderBoundsFromLocalAABB(
		glm::vec3(-0.25f),
		glm::vec3(0.25f),
		glm::mat4(1.0f));
	input.hasBounds = input.bounds.IsValid();
	scene.mainCameraCullInputs.push_back(input);

	VANS_SET_FRAME_PHASE(VansFramePhase::RenderThreadConsume);
	backendState.PrepareFrame(view, scene, 0, 10);
	VansVKBuffer* slotZeroObjectBuffer = &backendState.GetActiveCullObjectBuffer();
	if (!backendState.HasActiveCandidates() ||
		backendState.GetActiveCandidateCount() != 1 ||
		backendState.GetActiveFrameSlotIndex() != 0 ||
		backendState.GetActiveSettings().depthBiasMeters != 0.75f ||
		!backendState.ShouldDraw(input.proxy))
	{
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return false;
	}

	const VansMainCameraVisibilityDebugSnapshot firstSnapshot =
		backendState.GetDebugSnapshot();
	backendState.PrepareFrame(view, scene, 1, 11);
	VansVKBuffer* slotOneObjectBuffer = &backendState.GetActiveCullObjectBuffer();
	if (slotZeroObjectBuffer == slotOneObjectBuffer ||
		backendState.GetActiveFrameSlotIndex() != 1)
	{
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
		return false;
	}

	backendState.Reset();
	const VansMainCameraVisibilityDebugSnapshot resetSnapshot =
		backendState.GetDebugSnapshot();
	VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	return firstSnapshot.stats.enabled &&
		firstSnapshot.stats.candidateCount == 1 &&
		firstSnapshot.stats.preCullDrawCallCount == 1 &&
		firstSnapshot.stats.drawnDrawCallCount == 1 &&
		!resetSnapshot.stats.enabled &&
		resetSnapshot.stats.candidateCount == 0 &&
		resetSnapshot.stats.preCullDrawCallCount == 0 &&
		firstSnapshot.culledNodes.empty();
}

bool TestPunctualShadowBackendOwnershipContract()
{
	using namespace VansGraphics;
	VansPunctualShadowFrameState backendState;

	auto makeFrame = []()
	{
		VansRenderSceneFrameSnapshot scene;
		scene.sceneEpoch = 9;
		scene.sceneReady = true;
		scene.light.prepared = true;
		scene.light.punctualShadowMapWidth = 4096;
		VansSpotLight gpuLight{};
		gpuLight.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
		scene.light.spotLights.emplace_back(gpuLight);

		scene.punctualShadow.prepared = true;
		VansPunctualShadowLightInput light;
		light.stableLightId = 41;
		light.type = VansPunctualShadowLightType::Spot;
		light.gpuLightIndex = 0;
		light.position = glm::vec3(0.0f, 0.0f, -3.0f);
		light.direction = glm::vec3(0.0f, 0.0f, -1.0f);
		light.intensity = 10.0f;
		light.radius = 12.0f;
		light.settings.castShadows = true;
		light.settings.resolution = VansShadowResolution::R128;
		light.settings.maxShadowDistance = 100.0f;
		scene.punctualShadow.lights.emplace_back(light);

		VansRenderPunctualShadowCasterInput caster;
		caster.proxy = { 3u, 2u };
		caster.hasBounds = false;
		scene.punctualShadow.casters.emplace_back(caster);
		return scene;
	};

	VansRenderSceneFrameSnapshot first = makeFrame();
	if (!backendState.PrepareFrame(first, 1) ||
		first.punctualShadowJobs.empty() ||
		!first.features.hasPunctualShadowJobs ||
		first.light.spotLights.front().m_ShadowMetaIndex == VANS_INVALID_SHADOW_INDEX ||
		first.punctualShadowJobs.front().casterHandles.size() != 1 ||
		first.punctualShadowJobs.front().casterHandles.count(VansRenderProxyHandle{ 3u, 2u }) != 1)
	{
		return false;
	}
	if (backendState.FindCaster({3u, 2u}) == nullptr ||
		backendState.FindCaster({3u, 2u})->hasBounds ||
		backendState.FindCaster({3u, 1u}) != nullptr)
		return false;
	std::vector<std::uint8_t> packedLightBuffer;
	if (!VansLightManager::BuildRenderLightBufferPayload(
		first.light,
		backendState.GetGPUShadowData(),
		backendState.GetGPUShadowViews(),
		packedLightBuffer) ||
		packedLightBuffer.size() != VansLightManager::GetLightBufferPayloadSize())
	{
		return false;
	}
	std::array<std::uint32_t, 4> packedCounts{};
	std::memcpy(packedCounts.data(), packedLightBuffer.data(), sizeof(packedCounts));
	const std::size_t packedSpotOffset = sizeof(std::uint32_t) * 4 + sizeof(float) * 4 +
		sizeof(VansDirectionalLight) * VANS_MAX_DIRECTION_LIGHTS +
		sizeof(VansPointLight) * VANS_MAX_POINT_LIGHTS;
	VansSpotLight packedSpot{};
	std::memcpy(&packedSpot, packedLightBuffer.data() + packedSpotOffset, sizeof(packedSpot));
	if (packedCounts[0] != 0 || packedCounts[1] != 1 ||
		packedCounts[2] != 4096 ||
		packedCounts[3] != backendState.GetGPUShadowViews().size() ||
		packedSpot.m_ShadowMetaIndex != first.light.spotLights.front().m_ShadowMetaIndex)
	{
		return false;
	}
	if (backendState.CaptureDebugSnapshot().statistics.residentLights != 0)
		return false;

	backendState.NotifyRenderJobsSubmitted();
	VansRenderSceneFrameSnapshot second = makeFrame();
	if (!backendState.PrepareFrame(second, 2) ||
		backendState.CaptureDebugSnapshot().statistics.residentLights != 1)
	{
		return false;
	}

	VansRenderSceneFrameSnapshot unloaded;
	VansRenderSceneFrameSnapshot reused = makeFrame();
	reused.punctualShadow.casters.front().proxy = {3u, 3u};
	if (!backendState.PrepareFrame(reused, 3) || backendState.FindCaster({3u, 2u}) != nullptr ||
		backendState.FindCaster({3u, 3u}) == nullptr)
		return false;
	reused.punctualShadow.casters.clear();
	if (!backendState.PrepareFrame(reused, 4) || backendState.FindCaster({3u, 3u}) != nullptr)
		return false;
	unloaded.sceneEpoch = 10;
	if (!backendState.PrepareFrame(unloaded, 5) || backendState.FindCaster({3u, 2u}) != nullptr)
		return false;
	const VansPunctualShadowDebugSnapshot resetSnapshot =
		backendState.CaptureDebugSnapshot();
	return resetSnapshot.lights.empty() &&
		resetSnapshot.statistics.residentLights == 0;
}

bool TestRenderWorldContract()
{
	using namespace VansGraphics;
	VansRenderProxyHandleAllocator allocator;
	const VansRenderProxyHandle first = allocator.Allocate();
	const VansRenderProxyHandle second = allocator.Allocate();
	if (first != VansRenderProxyHandle{ 0u, 1u } ||
		second != VansRenderProxyHandle{ 1u, 1u } || allocator.ActiveCount() != 2)
	{
		return false;
	}
	VansRenderWorld identityOnlyWorld;
	VansRenderMutationBatch identityOnlyCreate;
	identityOnlyCreate.AddCreate(first, {});
	if (!identityOnlyWorld.Apply(identityOnlyCreate) ||
		identityOnlyWorld.Resolve(first) == nullptr ||
		identityOnlyWorld.Resolve(first)->transformSlot != VANS_INVALID_RENDER_TRANSFORM_SLOT)
	{
		return false;
	}

	VansRenderWorld world;
	VansRenderMutationBatch creates;
	creates.AddCreate(first, { 4u, true });
	creates.AddCreate(second, { 8u, false });
	if (!world.Apply(creates) || world.ActiveProxyCount() != 2 ||
		world.Resolve(first) == nullptr || world.Resolve(first)->transformSlot != 4 ||
		world.Resolve(second) == nullptr || world.Resolve(second)->enabled)
	{
		return false;
	}

	// 整批失败必须保持原状态，不能只应用失败命令之前的 update。
	VansRenderMutationBatch invalidBatch;
	invalidBatch.AddUpdate(first, { 99u, true });
	invalidBatch.AddDestroy({ second.index, second.generation + 1u });
	if (world.Apply(invalidBatch) || world.Resolve(first)->transformSlot != 4 ||
		world.RejectedMutationBatchCount() != 1)
	{
		return false;
	}

	VansRenderMutationBatch destroy;
	destroy.AddDestroy(first);
	if (!allocator.Release(first) || !world.Apply(destroy) || world.Resolve(first) != nullptr)
		return false;

	const VansRenderProxyHandle reused = allocator.Allocate();
	if (reused.index != first.index || reused.generation == first.generation)
		return false;
	if (MakeRenderProxyStableId(VansRenderProxyHandle{}) != 0 ||
		MakeRenderProxyStableId(first) == MakeRenderProxyStableId(reused) ||
		MakeRenderProxyStableId(reused) !=
			(static_cast<uint64_t>(reused.generation) << 32u | reused.index))
	{
		return false;
	}
	VansRenderMutationBatch replacement;
	replacement.AddCreate(reused, { 12u, true });
	if (!world.Apply(replacement) || world.Resolve(first) != nullptr ||
		world.Resolve(reused) == nullptr || world.Resolve(reused)->transformSlot != 12)
	{
		return false;
	}

	return allocator.ActiveCount() == 2 && world.ActiveProxyCount() == 2;
}

bool TestFramePhaseThreadLocalContract()
{
#ifdef _DEBUG
	g_CurrentFramePhase = VansFramePhase::GameLogic;
	bool workerObservedOwnPhase = false;
	bool physicsObservedOwnRole = false;
	std::thread worker([&workerObservedOwnPhase]
	{
		VANS_INIT_RENDER_THREAD();
		g_CurrentFramePhase = VansFramePhase::GPURecord;
		workerObservedOwnPhase =
			g_CurrentThreadRole == VansThreadRole::Render &&
			g_CurrentFramePhase == VansFramePhase::GPURecord;
	});
	worker.join();
	std::thread physics([&physicsObservedOwnRole]
	{
		VANS_INIT_PHYSICS_THREAD();
		physicsObservedOwnRole = g_CurrentThreadRole == VansThreadRole::Physics;
		VANS_CLEAR_THREAD_ROLE();
	});
	physics.join();
	return workerObservedOwnPhase && physicsObservedOwnRole &&
		g_CurrentThreadRole == VansThreadRole::Main &&
		g_CurrentFramePhase == VansFramePhase::GameLogic;
#else
	return true;
#endif
}

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

bool ExpectSerializedFloatField(
	const Vans::VansSerializedValue& object,
	const char* key,
	float expected,
	const char* message)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	if (!Expect(field != nullptr, message))
		return false;
	return ExpectNear(
		static_cast<float>(Vans::ReadSerializedNumber(*field, std::numeric_limits<double>::quiet_NaN())),
		expected,
		0.0001f,
		message);
}

bool ExpectSerializedIntField(
	const Vans::VansSerializedValue& object,
	const char* key,
	std::int64_t expected,
	const char* message)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	if (!Expect(field != nullptr, message))
		return false;
	if (!Expect(field->kind == Vans::VansSerializedValue::Kind::Int ||
		field->kind == Vans::VansSerializedValue::Kind::Float, message))
		return false;
	const std::int64_t actual = field->kind == Vans::VansSerializedValue::Kind::Int
		? field->intValue
		: static_cast<std::int64_t>(std::llround(field->floatValue));
	return Expect(actual == expected, message);
}

bool ExpectSerializedStringField(
	const Vans::VansSerializedValue& object,
	const char* key,
	const std::string& expected,
	const char* message)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	if (!Expect(field && field->kind == Vans::VansSerializedValue::Kind::String, message))
		return false;
	return Expect(field->stringValue == expected, message);
}

bool SerializedStringArrayContains(
	const Vans::VansSerializedValue& object,
	const char* key,
	const std::string& expected)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	if (!field || field->kind != Vans::VansSerializedValue::Kind::Array)
		return false;
	for (const Vans::VansSerializedValue& item : field->arrayItems)
	{
		if (item.kind == Vans::VansSerializedValue::Kind::String && item.stringValue == expected)
			return true;
	}
	return false;
}

bool ExpectSerializedVec3Field(
	const Vans::VansSerializedValue& object,
	const char* key,
	const glm::vec3& expected,
	const char* message)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	if (!Expect(field && field->kind == Vans::VansSerializedValue::Kind::Array &&
		field->arrayItems.size() >= 3, message))
		return false;
	const glm::vec3 actual(
		static_cast<float>(Vans::ReadSerializedNumber(field->arrayItems[0], std::numeric_limits<double>::quiet_NaN())),
		static_cast<float>(Vans::ReadSerializedNumber(field->arrayItems[1], std::numeric_limits<double>::quiet_NaN())),
		static_cast<float>(Vans::ReadSerializedNumber(field->arrayItems[2], std::numeric_limits<double>::quiet_NaN())));
	return ExpectNear(actual.r, expected.r, 0.0001f, message) &&
		ExpectNear(actual.g, expected.g, 0.0001f, message) &&
		ExpectNear(actual.b, expected.b, 0.0001f, message);
}

bool InstallTestBaseLayer(VansGraphics::VansAnimationController& controller,
                          std::unique_ptr<VansGraphics::VansAnimGraph> graph,
                          std::string& error)
{
    using namespace VansGraphics;
	VansAnimationLayerSetup setup;
    setup.definition.id = "layer-base";
    setup.definition.name = "Base";
    setup.definition.kind = VansAnimationLayerKind::Base;
    setup.definition.rootMotion = VansLayerRootMotionMode::Base;
    setup.definition.nodeTracks = VansLayerNodeTrackMode::Override;
	std::vector<VansAnimationLayerSetup> layers;
    layers.push_back(std::move(setup));
	VansAnimationGraphSetSetup graphSet;
	graphSet.definition.id = "graph-set-default";
	graphSet.definition.name = "Default";
	graphSet.definition.bindings.push_back({ "layer-base", "graph-base", true });
	VansAnimationGraphBindingSetup binding;
	binding.definition = graphSet.definition.bindings.front();
	binding.graph = std::move(graph);
	graphSet.bindings.push_back(std::move(binding));
	std::vector<VansAnimationGraphSetSetup> graphSets;
	graphSets.push_back(std::move(graphSet));
	return controller.SetAnimationGraphSets(
		std::move(layers), std::move(graphSets), "graph-set-default", {}, {}, error);
}

bool InstallTestGraphSet(
	VansGraphics::VansAnimationController& controller,
	std::vector<VansGraphics::VansAnimationLayerSetup> layers,
	std::vector<std::string> graphIds,
	std::vector<std::unique_ptr<VansGraphics::VansAnimGraph>> graphs,
	std::string& error)
{
	using namespace VansGraphics;
	if (layers.size() != graphIds.size() || layers.size() != graphs.size())
	{
		error = "Test Graph Set fixture sizes do not match";
		return false;
	}
	VansAnimationGraphSetSetup graphSet;
	graphSet.definition.id = "graph-set-default";
	graphSet.definition.name = "Default";
	for (std::size_t index = 0; index < layers.size(); ++index)
	{
		VansAnimationGraphBindingDefinition definition{
			layers[index].definition.id, graphIds[index], true };
		graphSet.definition.bindings.push_back(definition);
		VansAnimationGraphBindingSetup binding;
		binding.definition = std::move(definition);
		binding.graph = std::move(graphs[index]);
		graphSet.bindings.push_back(std::move(binding));
	}
	std::vector<VansAnimationGraphSetSetup> graphSets;
	graphSets.push_back(std::move(graphSet));
	return controller.SetAnimationGraphSets(
		std::move(layers), std::move(graphSets), "graph-set-default", {}, {}, error);
}

bool TestPackageManifestRoundTrip()
{
	VansGraphics::VansSceneResourceArtifactPrewarmResult inconsistentPrewarm;
	inconsistentPrewarm.errors.push_back("failure without a mirrored counter");
	if (!Expect(!inconsistentPrewarm.Succeeded(),
		"Scene resource prewarm ignored an explicit error when failure counters were zero"))
		return false;

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

	Vans::VansPackageManifest missingShaderRoot = expected;
	missingShaderRoot.shaderArtifacts.clear();
	if (!Expect(!Vans::VansPackageManifestIO::Validate(missingShaderRoot, error),
		"Manifest accepted a missing explicit shader artifact root"))
		return false;

    Vans::VansPackageManifest invalid = expected;
    invalid.scene = "../Outside.json";
    return Expect(!Vans::VansPackageManifestIO::Validate(invalid, error), "Manifest accepted a parent traversal path");
}

bool TestSkinProfileJsonRoundTrip()
{
	TemporaryDirectory temporary;
	Vans::VansSkinProfile expected;
	expected.name = "Cinematic Skin";
	expected.description = "Contract fixture";
	expected.basePreset = "cinematic";
	expected.scatterColor = glm::vec3(1.0f, 0.42f, 0.28f);
	expected.scatterAmount = 0.72f;
	expected.roughness = 0.58f;
	expected.normalStrength = 0.31f;
	expected.specularScale = 0.91f;
	expected.transmissionScale = 1.12f;
	expected.primaryRoughnessScale = 0.68f;
	expected.secondaryRoughnessScale = 1.82f;
	expected.skinIor = 1.43f;
	expected.specularLobeMix = 0.77f;
	expected.diffusionRadiusScale = 1.2f;
	expected.thinnessScale = 0.84f;
	expected.transmissionDepthScale = 1.35f;
	expected.ambientScatterScale = 0.46f;
	expected.scatterRadiusScale = glm::vec3(1.25f, 0.95f, 0.7f);
	expected.boundaryColorBleed = 1.2f;
	expected.profileLutLayer = 2;

	std::string error;
	const fs::path profilePath = temporary.path / "Cinematic.skinprofile";
	if (!Expect(Vans::VansSkinProfileStorage::SaveAtomic(profilePath, expected, error), error.c_str()))
		return false;

	Vans::VansSkinProfile loaded;
	if (!Expect(Vans::VansSkinProfileStorage::Load(profilePath, loaded, error), error.c_str()))
		return false;
	if (!Expect(loaded.name == expected.name && loaded.description == expected.description &&
		loaded.basePreset == expected.basePreset, "Skin profile identity fields did not round-trip"))
		return false;
	if (!ExpectNear(loaded.scatterColor.r, expected.scatterColor.r, 0.0001f,
		"Skin profile scatter color R did not round-trip") ||
		!ExpectNear(loaded.scatterColor.g, expected.scatterColor.g, 0.0001f,
			"Skin profile scatter color G did not round-trip") ||
		!ExpectNear(loaded.scatterColor.b, expected.scatterColor.b, 0.0001f,
			"Skin profile scatter color B did not round-trip"))
		return false;
	if (!ExpectNear(loaded.scatterAmount, expected.scatterAmount, 0.0001f,
		"Skin profile scatter amount did not round-trip") ||
		!ExpectNear(loaded.roughness, expected.roughness, 0.0001f,
			"Skin profile roughness did not round-trip") ||
		!ExpectNear(loaded.normalStrength, expected.normalStrength, 0.0001f,
			"Skin profile normal strength did not round-trip") ||
		!ExpectNear(loaded.specularScale, expected.specularScale, 0.0001f,
			"Skin profile specular scale did not round-trip") ||
		!ExpectNear(loaded.transmissionScale, expected.transmissionScale, 0.0001f,
			"Skin profile transmission scale did not round-trip"))
		return false;
	if (!ExpectNear(loaded.primaryRoughnessScale, expected.primaryRoughnessScale, 0.0001f,
		"Skin profile primary roughness scale did not round-trip") ||
		!ExpectNear(loaded.secondaryRoughnessScale, expected.secondaryRoughnessScale, 0.0001f,
			"Skin profile secondary roughness scale did not round-trip") ||
		!ExpectNear(loaded.skinIor, expected.skinIor, 0.0001f,
			"Skin profile IOR did not round-trip") ||
		!ExpectNear(loaded.specularLobeMix, expected.specularLobeMix, 0.0001f,
			"Skin profile lobe mix did not round-trip"))
		return false;
	if (!ExpectNear(loaded.diffusionRadiusScale, expected.diffusionRadiusScale, 0.0001f,
		"Skin profile diffusion radius scale did not round-trip") ||
		!ExpectNear(loaded.thinnessScale, expected.thinnessScale, 0.0001f,
			"Skin profile thinness scale did not round-trip") ||
		!ExpectNear(loaded.transmissionDepthScale, expected.transmissionDepthScale, 0.0001f,
			"Skin profile transmission depth scale did not round-trip") ||
		!ExpectNear(loaded.ambientScatterScale, expected.ambientScatterScale, 0.0001f,
			"Skin profile ambient scatter scale did not round-trip"))
		return false;
	return ExpectNear(loaded.scatterRadiusScale.r, expected.scatterRadiusScale.r, 0.0001f,
		"Skin profile scatter radius R did not round-trip") &&
		ExpectNear(loaded.scatterRadiusScale.g, expected.scatterRadiusScale.g, 0.0001f,
			"Skin profile scatter radius G did not round-trip") &&
		ExpectNear(loaded.scatterRadiusScale.b, expected.scatterRadiusScale.b, 0.0001f,
			"Skin profile scatter radius B did not round-trip") &&
		ExpectNear(loaded.boundaryColorBleed, expected.boundaryColorBleed, 0.0001f,
			"Skin profile boundary color bleed did not round-trip") &&
		Expect(loaded.profileLutLayer == expected.profileLutLayer,
			"Skin profile LUT layer did not round-trip");
}

bool TestSkinProfileJsonAliasDecode()
{
	Vans::SkinProfileJson root;
	root["name"] = "Alias Skin";
	root["scatterRadiusRGB"] = Vans::SkinProfileJson::array({ 0.9f, 0.8f, 0.7f });
	root["skinBoundaryBleed"] = 0.85f;
	root["skinProfileLutLayer"] = 2;
	root["scattering"]["profileScatterRadius"] = Vans::SkinProfileJson::array({ 1.4f, 1.1f, 0.8f });
	root["scattering"]["skinBoundaryBleed"] = 1.35f;
	root["scattering"]["skinLutLayer"] = 3;

	Vans::VansSkinProfile decoded;
	std::string error;
	if (!Expect(Vans::VansSkinProfileJsonCodec::Decode(root, "Alias.skinprofile", decoded, error), error.c_str()))
		return false;
	return ExpectNear(decoded.scatterRadiusScale.r, 1.4f, 0.0001f,
		"Skin profile alias decode did not prefer nested profile scatter radius R") &&
		ExpectNear(decoded.scatterRadiusScale.g, 1.1f, 0.0001f,
			"Skin profile alias decode did not prefer nested profile scatter radius G") &&
		ExpectNear(decoded.scatterRadiusScale.b, 0.8f, 0.0001f,
			"Skin profile alias decode did not prefer nested profile scatter radius B") &&
		ExpectNear(decoded.boundaryColorBleed, 1.35f, 0.0001f,
			"Skin profile alias decode did not prefer nested boundary bleed") &&
		Expect(decoded.profileLutLayer == 3,
			"Skin profile alias decode did not prefer nested LUT layer");
}

bool TestSkinProfileMaterialProjectionContract()
{
	using Value = Vans::VansSerializedValue;

	TemporaryDirectory temporary;
	const fs::path profilePath = temporary.path / "Hero.skinprofile";
	const fs::path materialPath = temporary.path / "HeroSkin.mat";

	Vans::VansAssetGuid profileGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"14141414-1414-4414-8414-141414141414",
		profileGuid), "Skin profile projection test GUID is invalid"))
		return false;
	Vans::VansAssetGuid materialGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"24242424-2424-4424-8424-242424242424",
		materialGuid), "Skin material projection test GUID is invalid"))
		return false;

	Vans::VansSkinProfile profile;
	profile.name = "Hero Skin Profile";
	profile.basePreset = "fair";
	profile.scatterColor = glm::vec3(0.92f, 0.38f, 0.27f);
	profile.scatterAmount = 0.73f;
	profile.roughness = 0.71f;
	profile.normalStrength = 0.29f;
	profile.specularScale = 1.17f;
	profile.transmissionScale = 1.26f;
	profile.primaryRoughnessScale = 0.64f;
	profile.secondaryRoughnessScale = 1.86f;
	profile.skinIor = 1.46f;
	profile.specularLobeMix = 0.81f;
	profile.diffusionRadiusScale = 1.33f;
	profile.thinnessScale = 0.77f;
	profile.transmissionDepthScale = 1.41f;
	profile.ambientScatterScale = 0.52f;
	profile.scatterRadiusScale = glm::vec3(1.31f, 1.08f, 0.82f);
	profile.boundaryColorBleed = 1.22f;
	profile.profileLutLayer = 3;

	std::string error;
	if (!Expect(Vans::VansSkinProfileStorage::SaveAtomic(profilePath, profile, error), error.c_str()))
		return false;

	Vans::VansMaterialAuthoringAsset material;
	material.guid = materialGuid.ToString();
	material.materialType = "skin";
	material.parameters = Value::Object({
		{ "skinProfile", Value::Object({
			{ "guid", Value::String(profileGuid.ToString()) }
		}) },
		{ "scatterColor", Value::Array({
			Value::Float(0.21), Value::Float(0.19), Value::Float(0.17) }) },
		{ "roughness", Value::Float(0.44) }
	});

	Vans::VansStagedFile materialStage;
	if (!Expect(Vans::VansMaterialAuthoringAssetStorage::StageWrite(
		materialPath, material, materialStage, error), error.c_str()))
		return false;
	Vans::VansStagedFileTransaction transaction;
	transaction.Add(std::move(materialStage));
	if (!Expect(transaction.Publish(error), error.c_str()))
		return false;

	Vans::VansAssetRecord profileRecord;
	profileRecord.guid = profileGuid;
	profileRecord.type = Vans::VansAssetType::SkinProfile;
	profileRecord.state = Vans::VansAssetState::CpuReady;
	profileRecord.sourceHash = 0x14141414u;
	profileRecord.sourcePath = profilePath;
	profileRecord.authoringPath = profilePath;

	Vans::VansAssetRecord materialRecord;
	materialRecord.guid = materialGuid;
	materialRecord.type = Vans::VansAssetType::Material;
	materialRecord.state = Vans::VansAssetState::CpuReady;
	materialRecord.sourceHash = 0x24242424u;
	materialRecord.sourcePath = materialPath;
	materialRecord.authoringPath = materialPath;

	struct ScopedPackagedAssets
	{
		~ScopedPackagedAssets()
		{
			Vans::VansProjectManager::Get().SetPackagedAssetRecords({});
		}
	};
	Vans::VansProjectManager::Get().CloseProject();
	Vans::VansProjectManager::Get().SetPackagedAssetRecords({ materialRecord, profileRecord });
	ScopedPackagedAssets scopedAssets;
	Vans::VansAssetObjectRepository& repository =
		Vans::VansProjectManager::Get().GetAssetObjectRepository();
	if (!Expect(
		repository.Publish<Vans::VansMaterialAuthoringAsset>(
			materialGuid,
			materialRecord.type,
			Vans::AssetObjectContentHash(materialRecord),
			std::make_shared<const Vans::VansMaterialAuthoringAsset>(material),
			{ profileGuid },
			error).IsValid() &&
		repository.Publish<Vans::VansSkinProfile>(
			profileGuid,
			profileRecord.type,
			Vans::AssetObjectContentHash(profileRecord),
			std::make_shared<const Vans::VansSkinProfile>(profile),
			{},
			error).IsValid(),
		"Could not publish material and skin profile memory objects"))
		return false;

	std::error_code removeError;
	fs::remove(materialPath, removeError);
	if (!Expect(!removeError && !fs::exists(materialPath),
		"Could not remove the material source before runtime projection"))
		return false;
	removeError.clear();
	fs::remove(profilePath, removeError);
	if (!Expect(!removeError && !fs::exists(profilePath),
		"Could not remove the skin profile source before runtime projection"))
		return false;

	Value sceneRoot = Value::Object({
		{ "schemaVersion", Value::Int(Vans::VansSceneSchemaVersion) },
		{ "sceneGuid", Value::String("31313131-3131-4313-8313-313131313131") },
		{ "name", Value::String("SkinProfileProjection") },
		{ "settings", Value::Object({
			{ "environment", BuildValidEnvironmentSettingsForTest() }
		}) },
		{ "entities", Value::Array({}) }
	});

	Vans::VansIOAudit::Reset();
	Vans::VansSceneContentBuildPlan plan;
	const bool projected = Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
		sceneRoot,
		temporary.path.string(),
		plan,
		error);
	if (!Expect(projected, error.c_str()))
		return false;
	if (!Expect(plan.materials.size() == 1u,
		"Skin profile material projection did not emit exactly one runtime material"))
		return false;

	const Value& runtimeMaterial = plan.materials.front().root;
	const Value* runtimeType = Vans::FindObjectField(runtimeMaterial, "type");
	if (!Expect(runtimeType && runtimeType->kind == Value::Kind::String &&
		runtimeType->stringValue == "skin", "Skin profile material projection changed material type"))
		return false;
	const Value* runtimeProfile = Vans::FindObjectField(runtimeMaterial, "skinProfile");
	if (!Expect(runtimeProfile && runtimeProfile->kind == Value::Kind::String &&
		runtimeProfile->stringValue == "fair", "Skin profile GUID reference did not collapse to base preset"))
		return false;
	if (!ExpectSerializedStringField(
		runtimeMaterial,
		"skinProfileAssetGuid",
		profileGuid.ToString(),
		"Skin profile asset GUID was not preserved for runtime profile updates"))
		return false;
	if (!Expect(SerializedStringArrayContains(runtimeMaterial, "skinProfileInheritedFields", "specularScale") &&
		SerializedStringArrayContains(runtimeMaterial, "skinProfileInheritedFields", "skinProfileLutLayer"),
		"Skin profile inherited fields did not record profile-owned parameters"))
		return false;
	if (!Expect(!SerializedStringArrayContains(runtimeMaterial, "skinProfileInheritedFields", "scatterColor") &&
		!SerializedStringArrayContains(runtimeMaterial, "skinProfileInheritedFields", "roughness") &&
		!SerializedStringArrayContains(runtimeMaterial, "skinProfileInheritedFields", "skinProfile"),
		"Skin profile inherited fields captured explicit material overrides"))
		return false;
	if (!ExpectSerializedVec3Field(runtimeMaterial, "scatterColor", glm::vec3(0.21f, 0.19f, 0.17f),
		"Explicit Skin material scatter color override was replaced by profile"))
		return false;
	if (!ExpectSerializedFloatField(runtimeMaterial, "roughness", 0.44f,
		"Explicit Skin material roughness override was replaced by profile"))
		return false;
	if (!ExpectSerializedFloatField(runtimeMaterial, "specularScale", 1.17f,
		"Skin profile specular scale was not projected into runtime material"))
		return false;
	if (!ExpectSerializedFloatField(runtimeMaterial, "skinIor", 1.46f,
		"Skin profile IOR was not projected into runtime material"))
		return false;
	if (!ExpectSerializedFloatField(runtimeMaterial, "boundaryColorBleed", 1.22f,
		"Skin profile boundary bleed was not projected into runtime material"))
		return false;
	if (!ExpectSerializedIntField(runtimeMaterial, "skinProfileLutLayer", 3,
		"Skin profile LUT layer was not projected into runtime material"))
		return false;
	if (!ExpectSerializedVec3Field(runtimeMaterial, "profileScatterRadius", glm::vec3(1.31f, 1.08f, 0.82f),
		"Skin profile RGB scatter radius was not projected into runtime material"))
		return false;
	const auto projectionIOEvents = Vans::VansIOAudit::Snapshot();
	return Expect(std::none_of(
		projectionIOEvents.begin(), projectionIOEvents.end(),
		[&](const Vans::VansIOEvent& event)
		{
			if (event.operation != Vans::VansIOOperation::Read)
				return false;
			const fs::path normalized = event.path.lexically_normal();
			return normalized == materialPath.lexically_normal() ||
				normalized == profilePath.lexically_normal();
		}),
		"Runtime material projection reread a deleted authoring asset instead of memory");
}

bool TestSkinProfileLUTGenerationContract()
{
	VansGraphics::VansBasePBRParam legacy;
	VansGraphics::VansSkinGPUParam palePayload;
	if (!Expect(VansGraphics::ResolveSkinProfilePresetPayload("pale", legacy, palePayload),
		"Skin profile preset alias did not resolve for LUT generation"))
		return false;
	if (!ExpectNear(palePayload.profileLUT.x, 1.0f, 0.0001f,
		"Skin profile preset alias did not map to the expected LUT layer"))
		return false;
	const auto paleFingerprint = VansGraphics::BuildSkinProfileLUTFingerprint(palePayload);
	VansGraphics::VansSkinGPUParam editedPalePayload = palePayload;
	editedPalePayload.profileControls.x += 0.125f;
	if (!Expect(VansGraphics::BuildSkinProfileLUTFingerprint(palePayload) == paleFingerprint,
		"Skin profile LUT fingerprint is not stable for identical payloads"))
		return false;
	if (!Expect(VansGraphics::BuildSkinProfileLUTFingerprint(editedPalePayload) != paleFingerprint,
		"Skin profile LUT fingerprint ignores diffusion radius changes"))
		return false;
	if (!Expect(VansGraphics::IsDynamicSkinProfileLUTLayer(VansGraphics::VANS_FIRST_DYNAMIC_SKIN_PROFILE_LUT_LAYER) &&
		!VansGraphics::IsDynamicSkinProfileLUTLayer(1),
		"Skin profile dynamic LUT layer range is unstable"))
		return false;

	constexpr int kTestLUTSize = 16;
	std::vector<uint8_t> neutralPixels;
	std::vector<uint8_t> darkPixels;
	if (!Expect(VansGraphics::GenerateBuiltInSkinProfileLUTLayer(
		0, kTestLUTSize, kTestLUTSize, neutralPixels),
		"Neutral skin profile LUT layer did not generate"))
		return false;
	if (!Expect(VansGraphics::GenerateBuiltInSkinProfileLUTLayer(
		3, kTestLUTSize, kTestLUTSize, darkPixels),
		"Dark skin profile LUT layer did not generate"))
		return false;
	if (!Expect(neutralPixels.size() == static_cast<size_t>(kTestLUTSize * kTestLUTSize * 4) &&
		darkPixels.size() == neutralPixels.size(), "Generated skin LUT size is unstable"))
		return false;
	if (!Expect(!VansGraphics::GenerateBuiltInSkinProfileLUTLayer(
		99, kTestLUTSize, kTestLUTSize, darkPixels),
		"Skin LUT generation accepted an invalid built-in layer"))
		return false;

	uint64_t neutralChecksum = 0;
	uint64_t darkChecksum = 0;
	for (size_t i = 0; i < neutralPixels.size(); ++i)
	{
		neutralChecksum += static_cast<uint64_t>(neutralPixels[i]) * static_cast<uint64_t>(i + 1u);
		darkChecksum += static_cast<uint64_t>(darkPixels[i]) * static_cast<uint64_t>(i + 1u);
		if ((i % 4u) == 3u && !Expect(neutralPixels[i] == 255u, "Generated skin LUT alpha is not opaque"))
			return false;
	}
	if (!Expect(neutralChecksum != darkChecksum, "Generated built-in skin LUT layers are identical"))
		return false;

	const size_t terminatorOffset =
		(static_cast<size_t>(kTestLUTSize - 1) * static_cast<size_t>(kTestLUTSize) +
			static_cast<size_t>((kTestLUTSize / 2) - 1)) * 4u;
	return Expect(neutralPixels[terminatorOffset] > neutralPixels[terminatorOffset + 2u],
		"Generated skin LUT did not preserve stronger red scatter near the terminator");
}

bool TestSkinDefaultTextureContract()
{
	using namespace Vans;

	struct ExpectedTexture
	{
		const char* alias;
		const char* guid;
		const char* sourcePath;
	};
	const ExpectedTexture expectedTextures[] = {
		{
			"defaultSkinCavity",
			"5f6d5237-1a6b-4b98-8b22-7cb738d88e09",
			"EngineAssets/Textures/Default/defaultSkinCavity.png"
		},
		{
			"defaultSkinMask",
			"b4b0a914-7e03-4b9a-92e9-fd6e73f32842",
			"EngineAssets/Textures/Default/defaultSkinMask.png"
		}
	};

	const auto& builtIns = VansBuiltInAssetCatalog::Entries();
	fs::path engineRoot;
	for (fs::path cursor = fs::current_path(); !cursor.empty() && engineRoot.empty();
		cursor = cursor.parent_path())
	{
		for (const fs::path& candidate : { cursor, cursor / "ForestEngine" })
		{
			if (fs::is_regular_file(candidate / expectedTextures[0].sourcePath))
			{
				engineRoot = candidate;
				break;
			}
		}
		if (cursor == cursor.root_path())
			break;
	}
	if (!Expect(!engineRoot.empty(), "Skin default textures are missing from EngineAssets"))
		return false;

	TemporaryDirectory artifactDirectory;
	VansAssetDatabase builtInDatabase(
		engineRoot / "EngineAssets", artifactDirectory.path / "Artifacts");
	for (const ExpectedTexture& expected : expectedTextures)
	{
		const auto entry = std::find_if(
			builtIns.begin(), builtIns.end(), [&](const VansBuiltInAssetEntry& candidate)
			{
				return candidate.runtimeAlias != nullptr &&
					std::string(candidate.runtimeAlias) == expected.alias;
			});
		if (!Expect(entry != builtIns.end() &&
			entry->type == VansAssetType::Texture &&
			std::string(entry->guid) == expected.guid &&
			std::string(entry->sourcePath) == expected.sourcePath &&
			VansBuiltInAssetCatalog::IsReservedRuntimeAlias(expected.alias),
			"Skin default texture is not a stable built-in asset contract"))
		{
			return false;
		}

		std::string assetError;
		if (!Expect(builtInDatabase.RegisterOrRefresh(
			engineRoot / expected.sourcePath,
			VansAssetOperationPolicy::ReadOnly(), assetError),
			assetError.empty() ? "Skin default texture could not be indexed" : assetError.c_str()))
		{
			return false;
		}
		const std::optional<VansAssetRecord> record =
			builtInDatabase.Find(engineRoot / expected.sourcePath);
		if (!Expect(record && record->type == VansAssetType::Texture &&
			record->guid.ToString() == expected.guid,
			"Skin default texture meta does not match its built-in catalog entry"))
		{
			return false;
		}
	}
	return true;
}

bool TestAssetPolicies()
{
    TemporaryDirectory temporary;
    const fs::path assetsRoot = temporary.path / "Assets";
    const fs::path artifactRoot = temporary.path / "Library" / "Artifacts";
	Vans::VansAssetGuid layoutGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"10101010-2020-3030-4040-505050505050", layoutGuid),
		"Artifact layout fixture guid is invalid"))
		return false;
	const Vans::VansDerivedArtifactLocation textureLayout =
		Vans::VansDerivedArtifactLayout::ImportedRuntimeCache(
			artifactRoot, Vans::VansAssetType::Texture, layoutGuid);
	const Vans::VansDerivedArtifactLocation meshLayout =
		Vans::VansDerivedArtifactLayout::ImportedRuntimeCache(
			artifactRoot, Vans::VansAssetType::Model, layoutGuid);
	if (!Expect(textureLayout && meshLayout &&
		textureLayout.path == artifactRoot / "Textures" / (layoutGuid.ToString() + ".vtex") &&
		meshLayout.path == artifactRoot / "Meshes" / (layoutGuid.ToString() + ".vmesh") &&
		!Vans::VansDerivedArtifactLayout::ImportedRuntimeCache(
			artifactRoot, Vans::VansAssetType::Material, layoutGuid) &&
		!Vans::VansDerivedArtifactLayout::ImportedRuntimeCache(
			{}, Vans::VansAssetType::Texture, layoutGuid),
		"Imported runtime artifact layout is not a single strict policy"))
		return false;
	const Vans::VansDerivedArtifactLocation shaderLayout =
		Vans::VansDerivedArtifactLayout::ProjectShaderCache(artifactRoot);
	const Vans::VansDerivedArtifactLocation builtInLayout =
		Vans::VansDerivedArtifactLayout::ProjectBuiltInArtifactRoot(artifactRoot);
	const Vans::VansDerivedArtifactLocation engineShaderLayout =
		Vans::VansDerivedArtifactLayout::EngineShaderCache(temporary.path / "Engine");
	const Vans::VansDerivedArtifactLocation gafLayout =
		Vans::VansDerivedArtifactLayout::ProjectGameplayCookedAsset(
			artifactRoot, layoutGuid, "Action.vaction");
	const Vans::VansDerivedArtifactLocation packagedSource =
		Vans::VansDerivedArtifactLayout::PackagedSourceCache(
			layoutGuid, "Action.vaction", false);
	if (!Expect(builtInLayout.path == artifactRoot / "Engine" &&
		!Vans::VansDerivedArtifactLayout::ProjectBuiltInArtifactRoot({}) &&
		shaderLayout.path == artifactRoot / "Shaders" &&
		engineShaderLayout.path == temporary.path / "Engine/Library/Artifacts/Shaders" &&
		gafLayout.path == artifactRoot / "GAF" / layoutGuid.ToString() /
			"Action.vaction.gafcooked" &&
		packagedSource.path == fs::path("Library/Artifacts/Resources") /
			layoutGuid.ToString() / "Action.vaction" &&
		Vans::VansDerivedArtifactLayout::PackagedMetadata(layoutGuid).path ==
			fs::path("Library/Artifacts/Metadata") / (layoutGuid.ToString() + ".meta") &&
		Vans::VansDerivedArtifactLayout::PackagedShaderArtifacts().path ==
			fs::path("Library/Artifacts/Shaders") &&
		Vans::VansDerivedArtifactLayout::PackagedResourcePlanReport().path ==
			fs::path("Library/Package/ResourcePlanReport.json") &&
		!Vans::VansDerivedArtifactLayout::ProjectGameplayCookedAsset(
			artifactRoot, layoutGuid, "../escape.vaction"),
		"Project and packaged artifact layouts are not strict single policies"))
		return false;
	Vans::VansShaderCompileRequest shaderRequest;
	shaderRequest.programId = "ArtifactPolicy";
	shaderRequest.sourceFolder = temporary.path / "Assets/Shaders/ArtifactPolicy";
	if (!Expect(Vans::VansShaderArtifactCache::ResolveArtifactRoot(shaderRequest).empty(),
		"Shader cache guessed an artifact root from sourceFolder or current directory"))
		return false;
	shaderRequest.artifactRoot = shaderLayout.path;
	if (!Expect(Vans::VansShaderArtifactCache::ResolveArtifactRoot(shaderRequest) ==
			shaderLayout.path.lexically_normal(),
		"Shader cache did not honor the explicit authoring artifact root"))
		return false;
	shaderRequest.artifactRoot.clear();
	const fs::path cookedShaderRoot = temporary.path / "Package/Content/Library/Artifacts/Shaders";
	Vans::VansShaderArtifactCache::ConfigureCookedRuntime(cookedShaderRoot);
	const bool cookedRootResolved = Vans::VansShaderArtifactCache::IsCookedOnlyMode() &&
		Vans::VansShaderArtifactCache::ResolveArtifactRoot(shaderRequest) ==
			cookedShaderRoot.lexically_normal();
	Vans::VansShaderArtifactCache::ResetRuntimeConfiguration();
	if (!Expect(cookedRootResolved && !Vans::VansShaderArtifactCache::IsCookedOnlyMode(),
		"Packaged shader root is not the only cooked-only mode owner"))
		return false;
    fs::create_directories(assetsRoot);
    const fs::path texturePath = assetsRoot / "PolicyProbe.tga";
    const auto writeProbeTexture = [&](std::uint8_t red)
    {
        const std::uint8_t tga[] = {
            0, 0, 2, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 1, 0, 1, 0,
            32, 0x28,
            0, 0, red, 255
        };
        std::ofstream file(texturePath, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(tga), sizeof(tga));
        return static_cast<bool>(file);
    };
    if (!Expect(writeProbeTexture(255), "Could not write the texture cache policy probe"))
        return false;

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
    const auto indexedTexture = database.Find(texturePath);
    if (!Expect(indexedTexture.has_value(), "Authoring scan did not index the texture cache probe"))
        return false;
    if (!Expect(indexedTexture->artifactPath.empty(), "Authoring scan unexpectedly resolved a texture artifact"))
        return false;

    const Vans::VansTextureArtifactEnsureResult firstEnsure =
        database.EnsureTextureArtifact(indexedTexture->guid);
    if (!Expect(firstEnsure.status == Vans::VansTextureArtifactEnsureStatus::Cooked,
        "First editor texture cache ensure did not cook the missing artifact"))
        return false;
    if (!Expect(firstEnsure.HasArtifact() && fs::is_regular_file(firstEnsure.artifactPath),
        "First editor texture cache ensure did not publish a readable artifact"))
        return false;
    const auto cookedRecord = database.Find(indexedTexture->guid);
    if (!Expect(cookedRecord && cookedRecord->artifactPath == firstEnsure.artifactPath,
        "Editor texture cache ensure did not refresh the live asset index"))
        return false;

    const Vans::VansTextureArtifactEnsureResult secondEnsure =
        database.EnsureTextureArtifact(indexedTexture->guid);
    if (!Expect(secondEnsure.status == Vans::VansTextureArtifactEnsureStatus::UpToDate &&
        secondEnsure.artifactPath == firstEnsure.artifactPath,
        "Second editor texture cache ensure did not reuse the current artifact"))
        return false;

    if (!Expect(writeProbeTexture(128), "Could not update the texture cache policy probe"))
        return false;
    std::error_code timestampError;
    const auto updatedTimestamp = fs::last_write_time(texturePath, timestampError) + std::chrono::seconds(2);
    if (!timestampError)
        fs::last_write_time(texturePath, updatedTimestamp, timestampError);
    if (!Expect(!timestampError, "Could not advance the texture cache policy probe timestamp"))
        return false;
    const Vans::VansTextureArtifactEnsureResult staleEnsure =
        database.EnsureTextureArtifact(indexedTexture->guid);
    if (!Expect(staleEnsure.status == Vans::VansTextureArtifactEnsureStatus::Cooked &&
        staleEnsure.artifactPath == firstEnsure.artifactPath,
        "Editor texture cache ensure did not rebuild the stale artifact"))
        return false;

    Vans::VansAssetDatabase reopenedDatabase(assetsRoot, artifactRoot);
    const Vans::VansAssetScanResult reopened =
        reopenedDatabase.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
    const auto reopenedRecord = reopenedDatabase.Find(indexedTexture->guid);
    if (!Expect(reopened && reopenedRecord && reopenedRecord->artifactPath == firstEnsure.artifactPath,
        "Subsequent editor scan did not prefer the generated texture artifact"))
        return false;
    const Vans::VansAssetTypeDescriptor* timelineType =
		Vans::VansAssetDatabase::Describe(Vans::VansAssetType::Timeline);
    if (!Expect(timelineType && timelineType->canonicalExtension == ".vtimeline" &&
		Vans::VansAssetDatabase::Classify("Probe.vtimeline") == Vans::VansAssetType::Timeline,
		"Timeline asset catalog and extension classification diverged"))
        return false;
	if (!Expect(Vans::VansAssetDatabase::Classify("Hero.skinprofile") == Vans::VansAssetType::SkinProfile,
		"Skin profile asset extension is not classified canonically"))
		return false;
	if (!Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::SkinProfile) == "SkinProfileImporter",
		"Skin profile assets are not owned by the canonical SkinProfile importer"))
		return false;
	if (!Expect(
		Vans::VansAssetDatabase::Classify("RTG_Source_To_Target.vretarget") ==
			Vans::VansAssetType::RetargetProfile &&
		Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::RetargetProfile) ==
			"RetargetProfileImporter",
		"Retarget profiles are not classified by their current canonical extension"))
		return false;
	if (!Expect(
		Vans::VansAssetDatabase::Classify("Hero.vragdoll") ==
			Vans::VansAssetType::RagdollProfile &&
		Vans::VansAssetDatabase::Classify("Hero.ragdoll") == Vans::VansAssetType::Unknown,
		"Ragdoll classification did not converge on the current .vragdoll extension"))
		return false;

	const auto registerModelCapabilityProbe = [&](const char* filename, bool skeletal)
		-> std::optional<Vans::VansAssetRecord>
	{
		const fs::path modelPath = assetsRoot / filename;
		{
			std::ofstream model(modelPath, std::ios::binary | std::ios::trunc);
			model << "asset capability probe";
		}
		Vans::VansAssetMeta meta;
		meta.guid = Vans::VansAssetGuid::New();
		meta.importer = Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Model);
		if (skeletal)
			meta.subAssets.emplace("bone:Root", Vans::VansSubAssetId::New());
		std::string metaError;
		if (!Vans::VansAssetMetaStorage::SaveAtomic(
			Vans::VansAssetMeta::MetaPathFor(modelPath), meta, metaError))
		{
			return std::nullopt;
		}
		std::string registerError;
		if (!database.RegisterOrRefresh(
			modelPath, Vans::VansAssetOperationPolicy::ReadOnly(), registerError))
		{
			return std::nullopt;
		}
		return database.Find(modelPath);
	};
	const auto skeletalModel = registerModelCapabilityProbe("SkeletalPreview.fbx", true);
	const auto staticModel = registerModelCapabilityProbe("StaticPreview.fbx", false);
	if (!Expect(skeletalModel && skeletalModel->hasSkeletalMesh,
		"Imported bone metadata did not expose the skeletal preview capability"))
		return false;
	if (!Expect(staticModel && !staticModel->hasSkeletalMesh,
		"Static model metadata was incorrectly exposed as a skeletal preview model"))
		return false;
    return Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Timeline) == "TimelineImporter",
        "Timeline assets are not owned by the canonical Timeline importer");
}

bool TestAssetTypeSerializationContract()
{
    constexpr Vans::VansAssetType types[] = {
        Vans::VansAssetType::Model,
        Vans::VansAssetType::Texture,
		Vans::VansAssetType::IESProfile,
        Vans::VansAssetType::Material,
        Vans::VansAssetType::Shader,
        Vans::VansAssetType::Audio,
        Vans::VansAssetType::Video,
        Vans::VansAssetType::Scene,
        Vans::VansAssetType::Particle,
        Vans::VansAssetType::AnimationClip,
        Vans::VansAssetType::AnimatorController,
        Vans::VansAssetType::AnimationRig,
		Vans::VansAssetType::RetargetProfile,
        Vans::VansAssetType::BoneMask,
        Vans::VansAssetType::Timeline,
		Vans::VansAssetType::NavigationMesh,
		Vans::VansAssetType::AIBehavior,
        Vans::VansAssetType::ActionDefinition,
        Vans::VansAssetType::ActionSet,
        Vans::VansAssetType::GameplayEffect,
        Vans::VansAssetType::GameplayCue,
        Vans::VansAssetType::AttributeSet,
        Vans::VansAssetType::TargetingPolicy,
        Vans::VansAssetType::GameplayTagTree,
        Vans::VansAssetType::PayloadSchema,
        Vans::VansAssetType::ActionGraph,
        Vans::VansAssetType::CameraRigProfile,
        Vans::VansAssetType::CameraShakeProfile,
        Vans::VansAssetType::GAFEditorLayout,
        Vans::VansAssetType::ClothProfile,
        Vans::VansAssetType::SkinProfile,
        Vans::VansAssetType::PostProcessProfile,
        Vans::VansAssetType::RagdollProfile,
        Vans::VansAssetType::AudioReverbPreset,
        Vans::VansAssetType::AudioBusSnapshot,
        Vans::VansAssetType::AudioDuckingRules,
		Vans::VansAssetType::UIScreen,
		Vans::VansAssetType::UIComponent,
		Vans::VansAssetType::UIThemeTokens,
		Vans::VansAssetType::UILocalization,
		Vans::VansAssetType::UIXaml,
		Vans::VansAssetType::VegetationConfig,
		Vans::VansAssetType::Terrain,
		Vans::VansAssetType::PlantType,
		Vans::VansAssetType::PcgMask,
		Vans::VansAssetType::PcgSpline,
		Vans::VansAssetType::DamageProfile,
		Vans::VansAssetType::Prefab
    };

    for (const Vans::VansAssetType type : types)
    {
        const std::string_view name = Vans::VansAssetDatabase::SerializedTypeName(type);
		const Vans::VansAssetTypeDescriptor* descriptor = Vans::VansAssetDatabase::Describe(type);
        if (!Expect(name != "unknown", "A registered asset type has no serialized name"))
            return false;
        if (!Expect(Vans::VansAssetDatabase::ParseSerializedType(name) == type,
            "An asset type did not round-trip through its serialized name"))
            return false;
		if (!Expect(descriptor && descriptor->type == type && descriptor->serializedName == name &&
			!descriptor->importer.empty(),
			"An asset type is missing its canonical catalog descriptor"))
			return false;
		if (!descriptor->canonicalExtension.empty() &&
			!Expect(Vans::VansAssetDatabase::Classify(
				std::filesystem::path(std::string("Probe") + std::string(descriptor->canonicalExtension))) == type,
				"An asset type canonical extension did not classify through the shared catalog"))
			return false;
    }

    if (!Expect(Vans::VansAssetDatabase::Classify(
		std::filesystem::path(L"\u4E2D\u6587\u8D44\u4EA7.vaction")) ==
		Vans::VansAssetType::ActionDefinition,
		"A non-ASCII asset filename did not preserve canonical extension classification"))
		return false;

    return Expect(
        Vans::VansAssetDatabase::SerializedTypeName(Vans::VansAssetType::Unknown) == "unknown" &&
        Vans::VansAssetDatabase::ParseSerializedType("unknown") == Vans::VansAssetType::Unknown &&
        Vans::VansAssetDatabase::ParseSerializedType("AnimationRig") == Vans::VansAssetType::Unknown,
        "Unknown or non-canonical asset type names were accepted");
}

bool TestSceneResourceArtifactPrewarmContract()
{
    TemporaryDirectory temporary;
    const fs::path assetsRoot = temporary.path / "Assets";
    const fs::path engineAssetsRoot = temporary.path / "EngineAssets";
    const fs::path artifactRoot = temporary.path / "Library" / "Artifacts";
    fs::create_directories(assetsRoot);
    fs::create_directories(engineAssetsRoot);

    const fs::path texturePath = assetsRoot / "PrewarmProbe.tga";
    const fs::path meshPath = assetsRoot / "PrewarmProbe.obj";
    const auto writeTexture = [&](std::uint8_t red)
    {
        const std::uint8_t tga[] = {
            0, 0, 2, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 1, 0, 1, 0,
            32, 0x28,
            0, 0, red, 255
        };
        std::ofstream file(texturePath, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(tga), sizeof(tga));
        return static_cast<bool>(file);
    };
    const auto writeMesh = [&](float extent)
    {
        std::ofstream file(meshPath, std::ios::binary | std::ios::trunc);
        file << "o PrewarmProbe\n"
            << "v 0 0 0\n"
            << "v " << extent << " 0 0\n"
            << "v 0 " << extent << " 0\n"
            << "vt 0 0\nvt 1 0\nvt 0 1\n"
            << "vn 0 0 1\n"
            << "f 1/1/1 2/2/1 3/3/1\n";
        return static_cast<bool>(file);
    };
    if (!Expect(writeTexture(255) && writeMesh(1.0f),
        "Could not write scene resource artifact prewarm probes"))
        return false;

    Vans::VansAssetDatabase database(assetsRoot, artifactRoot);
    Vans::VansAssetDatabase builtInDatabase(engineAssetsRoot, artifactRoot / "Engine");
    if (!Expect(database.Scan(Vans::VansAssetOperationPolicy::Authoring()) &&
        builtInDatabase.Scan(Vans::VansAssetOperationPolicy::ReadOnly()),
        "Could not index scene resource artifact prewarm probes"))
        return false;
    const auto textureRecord = database.Find(texturePath);
    const auto meshRecord = database.Find(meshPath);
    if (!Expect(textureRecord && meshRecord,
        "Scene resource artifact prewarm probes were not indexed"))
        return false;

    Vans::VansSceneResourceBuildPlan plan;
    plan.includeDefaultTextureSet = false;
    plan.loadRegisteredShaders = false;
    Vans::VansSceneTextureResourceRequest textureRequest;
    textureRequest.name = "PrewarmProbeTexture";
    textureRequest.assetGuid = textureRecord->guid.ToString();
    textureRequest.path = "Assets/PrewarmProbe.tga";
    plan.textures.push_back(textureRequest);
    Vans::VansSceneMeshResourceRequest meshRequest;
    meshRequest.name = "PrewarmProbeMesh";
    meshRequest.assetGuid = meshRecord->guid.ToString();
    meshRequest.path = "Assets/PrewarmProbe.obj";
    meshRequest.needTangent = false;
    meshRequest.supportRayTracing = false;
    plan.meshes.push_back(meshRequest);

    const auto first = VansGraphics::VansSceneResourceArtifactPrewarmer::Prewarm(
        temporary.path, database, builtInDatabase, plan);
    if (!Expect(first.meshCooked == 1 && first.textureCooked == 1 && first.Succeeded(),
        "First Editor resource prewarm did not cook mesh and texture artifacts"))
        return false;
    const auto firstTextureRecord = database.Find(textureRecord->guid);
    const auto firstMeshRecord = database.Find(meshRecord->guid);
    if (!Expect(firstTextureRecord && firstMeshRecord &&
        fs::is_regular_file(firstTextureRecord->artifactPath) &&
        fs::is_regular_file(firstMeshRecord->artifactPath),
        "Editor resource prewarm did not publish artifacts into the live index"))
        return false;

    const auto second = VansGraphics::VansSceneResourceArtifactPrewarmer::Prewarm(
        temporary.path, database, builtInDatabase, plan);
    if (!Expect(second.meshUpToDate == 1 && second.textureUpToDate == 1 &&
        second.meshCooked == 0 && second.textureCooked == 0,
        "Second Editor resource prewarm did not reuse current artifacts"))
        return false;

    if (!Expect(writeTexture(128) && writeMesh(2.0f),
        "Could not update scene resource artifact prewarm probes"))
        return false;
    std::error_code timestampError;
    for (const fs::path& path : { texturePath, meshPath })
    {
        const auto timestamp = fs::last_write_time(path, timestampError);
        if (timestampError) break;
        fs::last_write_time(path, timestamp + std::chrono::seconds(2), timestampError);
        if (timestampError) break;
    }
    if (!Expect(!timestampError, "Could not advance scene resource prewarm probe timestamps"))
        return false;

    const auto stale = VansGraphics::VansSceneResourceArtifactPrewarmer::Prewarm(
        temporary.path, database, builtInDatabase, plan);
    return Expect(stale.meshCooked == 1 && stale.textureCooked == 1 && stale.Succeeded(),
        "Editor resource prewarm did not rebuild stale mesh and texture artifacts");
}

bool TestSceneResourceFailurePropagationContract()
{
	static_assert(std::is_same_v<
		decltype(VansGraphics::VansSceneProjectResourceBuilder::LoadShadersFromRegistry(
			std::declval<VansGraphics::VansScene&>(),
			std::declval<const std::string&>(),
			std::declval<VkDevice&>())),
		bool>);
	static_assert(std::is_same_v<
		decltype(VansGraphics::VansSceneProjectResourceBuilder::RegisterShaders(
			std::declval<VansGraphics::VansScene&>(),
			std::declval<const std::vector<Vans::VansSceneShaderResourceRequest>&>(),
			std::declval<const Vans::VansSceneResourceLoadContext&>(),
			std::declval<VkDevice&>(),
			false)),
		bool>);

	TemporaryDirectory temporary;
	const fs::path shaderSource = temporary.path / "SceneBuildAtomicityProbe";
	{
		std::ofstream file(shaderSource, std::ios::binary | std::ios::trunc);
		file << "contract probe";
	}
	Vans::VansAssetRecord validRecord;
	validRecord.guid = Vans::VansAssetGuid::FromStableName(
		"ForestContractTests",
		"SceneBuildShaderAtomicity");
	validRecord.type = Vans::VansAssetType::Shader;
	validRecord.sourcePath = shaderSource;
	const Vans::VansSceneResourceLoadContext loadContext =
		Vans::VansSceneResourceLoadContext::ForEditor(
			temporary.path,
			temporary.path,
			{ validRecord });
	Vans::VansSceneShaderResourceRequest valid;
	valid.name = "SceneBuildAtomicityProbe";
	valid.assetGuid = validRecord.guid.ToString();
	Vans::VansSceneShaderResourceRequest unresolved;
	unresolved.name = "UnresolvedShader";
	unresolved.assetGuid = "00000000-0000-0000-0000-000000000001";

	VansGraphics::VansScene scene;
	VkDevice nativeDevice = VK_NULL_HANDLE;
	return Expect(
		!VansGraphics::VansSceneProjectResourceBuilder::RegisterShaders(
			scene,
			{ valid, unresolved },
			loadContext,
			nativeDevice,
			false) &&
			VansGraphics::VansShaderManager::Get().FindShaderEntry(valid.name) == nullptr,
		"Invalid shader batch did not fail atomically before registry publication");
}

class VansRuntimeFrameTracePort final :
	public Vans::IVansRuntimeFramePort,
	public Vans::IVansRuntimeFramePreviewPort
{
  public:
	explicit VansRuntimeFrameTracePort(std::vector<std::string>& trace)
		: m_Trace(trace)
	{
	}

	void SyncPhysicsTransforms(const Vans::VansRuntimeFrameContext&) override { Add("physics"); }
	void UpdateNonCameraScripts(const Vans::VansRuntimeFrameContext&) override { Add("scripts"); }
	void AdvanceCameraRuntime(const Vans::VansRuntimeFrameContext&) override { Add("camera-advance"); }
	void UpdateActionsEarly(const Vans::VansRuntimeFrameContext&) override { Add("actions-early"); }
	void UpdateAI(const Vans::VansRuntimeFrameContext&) override { Add("ai"); }
	void PrepareCharacterLocomotion(const Vans::VansRuntimeFrameContext&) override { Add("locomotion"); }
	void FlushCharacterControllerTransforms(const Vans::VansRuntimeFrameContext&) override { Add("cct"); }
	void UpdateTimelinesPostScript(const Vans::VansRuntimeFrameContext&) override { Add("timeline-post"); }
	void RunActionLateContinuation(const Vans::VansRuntimeFrameContext&) override { Add("actions-late"); }
	void BeginCameraControlFrame(const Vans::VansRuntimeFrameContext&) override { Add("camera-begin"); }
	void UpdateCameraScripts(const Vans::VansRuntimeFrameContext&) override { Add("camera"); }
	void CaptureCameraControlBase(const Vans::VansRuntimeFrameContext&) override { Add("camera-base"); }
	void UpdateTimelinesCamera(const Vans::VansRuntimeFrameContext&) override { Add("timeline-camera"); }
	void ResolveCameraControlFrame(const Vans::VansRuntimeFrameContext&) override { Add("camera-resolve"); }
	void UpdatePostScriptControllers(const Vans::VansRuntimeFrameContext&) override { Add("post-extra"); }
	void UpdateCameraControllers(const Vans::VansRuntimeFrameContext&) override { Add("camera-extra"); }

  private:
	void Add(const char* stage) { m_Trace.emplace_back(stage); }

	std::vector<std::string>& m_Trace;
};

bool TestGameplayFrameOrder()
{
    std::vector<std::string> trace;
	VansRuntimeFrameTracePort framePort(trace);
	Vans::VansRuntimeFramePolicy framePolicy{ true, true, true, true };
	const Vans::VansRuntimeFrameContext frameContext{ 1.0 / 60.0 };
	Vans::VansRuntimeFrameScheduler::RunGameplay(
		framePort, &framePort, framePolicy, frameContext);

	const std::vector<std::string> expected{ "camera-begin", "physics", "scripts", "camera-advance", "actions-early", "ai", "locomotion", "cct",
		"timeline-post", "post-extra", "actions-late", "camera", "camera-base", "timeline-camera",
		"camera-extra", "camera-resolve" };
    if (!Expect(trace == expected, "Gameplay frame callback order changed"))
        return false;

    trace.clear();
	framePolicy.m_IsSceneReady = false;
	Vans::VansRuntimeFrameScheduler::RunGameplay(
		framePort, &framePort, framePolicy, frameContext);
	if (!Expect(trace.empty(), "Gameplay callbacks ran without a ready scene")) return false;

	framePolicy = { true, false, false, true };
	Vans::VansRuntimeFrameScheduler::RunGameplay(
		framePort, &framePort, framePolicy, frameContext);
	const std::vector<std::string> editExpected{
		"camera-begin", "camera-advance", "post-extra", "actions-late",
		"camera-base", "camera-extra", "camera-resolve" };
	return Expect(trace == editExpected,
		"Edit camera control frame did not advance CameraRuntime before preview resolution");
}

Vans::VansEngineTimelineCatalog TimelineCatalog()
{
	const Vans::VansEngineTimelineCatalog catalog = Vans::VansGetEngineTimelineCatalog();
	if (!catalog) std::cerr << "[ForestContractTests] " << catalog.error << '\n';
	return catalog;
}

bool TestCameraControlArbiterContract()
{
	using namespace VansGraphics;
	VansCameraControlArbiter arbiter;
	VansCamera frameCamera(nullptr);
	if (!Expect(arbiter.IsBaseCameraWriteWindowOpen(),
		"Camera base write window was not open between frames")) return false;
	arbiter.BeginFrame(frameCamera);
	if (!Expect(arbiter.IsBaseCameraWriteWindowOpen(),
		"Camera base write window closed before CaptureBase")) return false;
	arbiter.CaptureBase(frameCamera);
	if (!Expect(!arbiter.IsBaseCameraWriteWindowOpen(),
		"Camera base write window remained open while contributions were resolving")) return false;
	arbiter.Resolve(frameCamera);
	if (!Expect(arbiter.IsBaseCameraWriteWindowOpen(),
		"Camera base write window did not reopen after Resolve")) return false;
	arbiter.Clear(&frameCamera);
	Vans::VansCameraViewSnapshot base;
	base.pose.position = { 10.0f, 0.0f, 0.0f };
	base.pose.rotationDegrees = { 0.0f, 90.0f, 0.0f };
	base.lens.fieldOfView = 60.0f;
	std::string error;
	Vans::VansCameraViewSnapshot clampedView = base;
	clampedView.lens.fieldOfView = 0.5f;
	clampedView.lens.nearClip = 0.01f;
	clampedView.lens.farClip = 0.02f;
	std::string clampDiagnostic;
	if (!Vans::VansClampCameraView(
		clampedView, Vans::VansCameraLensLimits{}, clampDiagnostic) ||
		clampDiagnostic.empty() ||
		std::abs(clampedView.lens.fieldOfView - 1.0f) > 0.001f ||
		std::abs(clampedView.lens.nearClip - 0.1f) > 0.001f ||
		std::abs(clampedView.lens.farClip - 0.101f) > 0.001f ||
		!Vans::VansValidateCameraView(
			clampedView, Vans::VansCameraLensLimits{}, error))
	{
		return Expect(false,
			"Camera lens Clamp/Validate semantics did not preserve the effective defaults");
	}
	if (!arbiter.Runtime().SetBaseView(Vans::VansCameraRuntime::MainView(), base, error))
		return Expect(false, error.c_str());
	const auto timelineDomain = VansCameraControlArbiter::TimelineDomain();
	const auto gameplayDomain = Vans::VansMakeStableId<Vans::VansCameraContributionDomainIdTag>(
		"CameraControl.ContractGameplay");
	const Vans::VansGenerationHandle sameHandle{ 7, 3 };
	Vans::VansCameraContributionRequest gameplay;
	gameplay.view = Vans::VansCameraRuntime::MainView();
	gameplay.owner = { gameplayDomain, sameHandle };
	gameplay.kind = Vans::VansCameraContributionKind::Lens;
	gameplay.value = base;
	gameplay.value.lens.fieldOfView = 50.0f;
	gameplay.channels = Vans::VansCameraChannel_FieldOfView;
	gameplay.order.priority = 10;
	if (!arbiter.Submit(gameplay, error)) return Expect(false, error.c_str());
	if (!Expect(!arbiter.IsUserLookSuppressed(),
		"camera contributions changed gameplay look behavior without opting in")) return false;
	Vans::VansCameraContributionRequest timeline = gameplay;
	timeline.owner = { timelineDomain, sameHandle };
	timeline.value.lens.fieldOfView = 40.0f;
	timeline.blendMode = Vans::VansCameraBlendMode::Weighted;
	timeline.order.priority = VansCameraControlArbiter::TimelinePriority;
	timeline.weight = 0.5f;
	timeline.suppressUserLook = true;
	if (!arbiter.Submit(timeline, error)) return Expect(false, error.c_str());
	if (!Expect(arbiter.IsUserLookSuppressed(),
		"Timeline camera contribution did not suppress gameplay look input")) return false;
	Vans::VansCameraContributionRequest shake;
	shake.view = Vans::VansCameraRuntime::MainView();
	shake.owner = { timelineDomain, { 8, 3 } };
	shake.kind = Vans::VansCameraContributionKind::PoseOffset;
	shake.value.pose.position = { 0.0f, 0.0f, 1.0f };
	shake.blendMode = Vans::VansCameraBlendMode::Additive;
	shake.space = Vans::VansCameraSpace::CameraLocal;
	shake.order.priority = VansCameraControlArbiter::TimelinePriority + 100;
	shake.channels = Vans::VansCameraChannel_Position;
	if (!arbiter.Submit(shake, error)) return Expect(false, error.c_str());
	Vans::VansCameraContributionRequest invalid = gameplay;
	invalid.owner.writer = { 9, 3 };
	invalid.order.priority = VansCameraControlArbiter::TimelinePriority;
	if (!Expect(!arbiter.Submit(invalid, error) && !error.empty(),
		"non-Timeline controller entered the reserved Timeline priority range")) return false;
	const Vans::VansResolvedCameraView resolved =
		arbiter.Runtime().ResolveView(Vans::VansCameraRuntime::MainView());
	if (!Expect(arbiter.Runtime().ContributionCount() == 3,
		"camera control owner domains collided on equal generation handles")) return false;
	if (!Expect(std::abs(resolved.snapshot.lens.fieldOfView - 45.0f) < 0.001f &&
		glm::length(resolved.snapshot.pose.position - glm::vec3(11.0f, 0.0f, 0.0f)) < 0.001f,
		"camera priority, weighting, or camera-local additive resolution is wrong")) return false;
	arbiter.ReleaseDomain(timelineDomain);
	if (!Expect(!arbiter.IsUserLookSuppressed(),
		"Timeline camera domain release did not restore gameplay look input")) return false;
	const Vans::VansResolvedCameraView gameplayOnly =
		arbiter.Runtime().ResolveView(Vans::VansCameraRuntime::MainView());
	if (!Expect(arbiter.Runtime().ContributionCount() == 1 &&
		std::abs(gameplayOnly.snapshot.lens.fieldOfView - 50.0f) < 0.001f,
		"camera control domain release removed another controller domain")) return false;

	VansVirtualCameraParameterStore virtualCameras;
	const Vans::VansEntityHandle virtualCamera{ 12, 4 };
	if (!Expect(virtualCameras.Set(virtualCamera, { 72.0f, 0.03f, 2000.0f }),
		"virtual camera parameter store rejected a valid Transform entity")) return false;
	const VansVirtualCameraParameters* parameters = virtualCameras.Find(virtualCamera);
	if (!Expect(parameters && parameters->fieldOfView == 72.0f &&
		parameters->nearClip == 0.03f && parameters->farClip == 2000.0f,
		"virtual camera parameters were not recorded without a Camera component")) return false;
	if (!Expect(virtualCameras.Remove(virtualCamera) && virtualCameras.Find(virtualCamera) == nullptr,
		"virtual camera parameter lifetime did not restore cleanly")) return false;

	Vans::VansCameraRuntime cameraRuntime;
	Vans::VansCameraViewSnapshot coreBase;
	coreBase.pose.position = { 1.0f, 2.0f, 3.0f };
	coreBase.pose.rotationDegrees = { 0.0f, 90.0f, 0.0f };
	coreBase.lens.fieldOfView = 60.0f;
	error.clear();
	if (!cameraRuntime.SetBaseView(Vans::VansCameraRuntime::MainView(), coreBase, error))
		return Expect(false, error.c_str());
	const auto actionDomain =
		Vans::VansMakeStableId<Vans::VansCameraContributionDomainIdTag>("Camera.GAF.Contract");
	Vans::VansCameraContributionRequest firstContribution;
	firstContribution.view = Vans::VansCameraRuntime::MainView();
	firstContribution.owner = { actionDomain, { 1, 1 } };
	firstContribution.kind = Vans::VansCameraContributionKind::Lens;
	firstContribution.value = coreBase;
	firstContribution.value.lens.fieldOfView = 50.0f;
	firstContribution.channels = Vans::VansCameraChannel_FieldOfView;
	firstContribution.order.layer = 2;
	firstContribution.order.hierarchicalBias = 3;
	firstContribution.order.priority = 4;
	const Vans::VansCameraContributionHandle firstContributionHandle =
		cameraRuntime.AddContribution(firstContribution, error);
	Vans::VansCameraContributionRequest secondContribution = firstContribution;
	secondContribution.owner.writer = { 2, 1 };
	secondContribution.value.lens.fieldOfView = 40.0f;
	secondContribution.blendMode = Vans::VansCameraBlendMode::Weighted;
	secondContribution.weight = 0.5f;
	secondContribution.order.priority = 5;
	const Vans::VansCameraContributionHandle secondContributionHandle =
		cameraRuntime.AddContribution(secondContribution, error);
	const Vans::VansResolvedCameraView coreResolved =
		cameraRuntime.ResolveView(Vans::VansCameraRuntime::MainView());
	if (!Expect(firstContributionHandle && secondContributionHandle &&
		std::abs(coreResolved.snapshot.lens.fieldOfView - 45.0f) < 0.001f &&
		coreResolved.appliedContributions.size() == 2 &&
		coreResolved.appliedContributions.front() == firstContributionHandle,
		"CameraCore did not apply its stable layer/bias/priority/sequence order")) return false;
	if (!cameraRuntime.ReleaseContribution(firstContributionHandle) ||
		cameraRuntime.ReleaseContribution(firstContributionHandle) ||
		cameraRuntime.UpdateContribution(firstContributionHandle, firstContribution, error))
		return Expect(false, "CameraCore accepted a stale contribution handle");
	firstContribution.owner.writer = { 3, 1 };
	const Vans::VansCameraContributionHandle reused =
		cameraRuntime.AddContribution(firstContribution, error);
	if (!Expect(reused && reused.value.index == firstContributionHandle.value.index &&
		reused.value.generation != firstContributionHandle.value.generation,
		"CameraCore did not advance generation when reusing a contribution slot")) return false;

	Vans::VansCameraShakeDefinition shakeDefinition;
	shakeDefinition.stableName = "Camera.Shake.StateOwnershipContract";
	shakeDefinition.id = Vans::VansMakeStableId<Vans::VansCameraShakeIdTag>(
		shakeDefinition.stableName);
	shakeDefinition.attackSeconds = 0.0f;
	shakeDefinition.sustainSeconds = 1.0f;
	shakeDefinition.releaseSeconds = 0.0f;
	const Vans::VansCameraShakeHandle shakeHandle =
		cameraRuntime.RegisterShake(shakeDefinition, error);
	Vans::VansCameraContributionRequest shakeRequest;
	shakeRequest.view = Vans::VansCameraRuntime::MainView();
	shakeRequest.owner = { actionDomain, { 4, 1 } };
	shakeRequest.kind = Vans::VansCameraContributionKind::Shake;
	shakeRequest.blendMode = Vans::VansCameraBlendMode::Additive;
	shakeRequest.channels = Vans::VansCameraChannel_Position |
		Vans::VansCameraChannel_Rotation;
	shakeRequest.shake = shakeHandle;
	const Vans::VansCameraContributionHandle shakeContribution =
		cameraRuntime.AddContribution(shakeRequest, error);
	cameraRuntime.Advance(0.6);
	Vans::VansCameraContributionRequest readShake;
	if (!Expect(shakeHandle && shakeContribution &&
		cameraRuntime.ReadContribution(shakeContribution, readShake) &&
		readShake.order.stableSequence != 0,
		"CameraCore did not expose the normalized author request by value")) return false;
	readShake.weight = 0.5f;
	if (!cameraRuntime.UpdateContribution(shakeContribution, readShake, error))
		return Expect(false, error.c_str());
	cameraRuntime.Advance(0.5);
	if (!Expect(cameraRuntime.ContributionCount() == 2 &&
		!cameraRuntime.ReadContribution(shakeContribution, readShake),
		"CameraCore author update reset or exposed private Shake elapsed state")) return false;

	Vans::VansCameraRigDefinition rig;
	rig.stableName = "Camera.Rig.Contract";
	rig.id = Vans::VansMakeStableId<Vans::VansCameraRigIdTag>(rig.stableName);
	rig.initialView = coreBase;
	rig.initialView.lens.fieldOfView = 75.0f;
	const Vans::VansCameraRigHandle rigHandle = cameraRuntime.RegisterRig(rig, error);
	const Vans::VansCameraViewId secondaryView =
		Vans::VansMakeStableId<Vans::VansCameraViewIdTag>("Camera.View.Secondary");
	if (!rigHandle || !cameraRuntime.BindViewRig(secondaryView, rigHandle, error))
		return Expect(false, error.c_str());
	if (!Expect(std::abs(cameraRuntime.ResolveView(secondaryView).snapshot.lens.fieldOfView - 75.0f) < 0.001f,
		"CameraCore view did not resolve its logical rig independently")) return false;

	Vans::VansCameraRigDefinition solvedRig;
	solvedRig.stableName = "Camera.Rig.SolverContract";
	solvedRig.id = Vans::VansMakeStableId<Vans::VansCameraRigIdTag>(solvedRig.stableName);
	solvedRig.follow.enabled = true;
	solvedRig.follow.mode = Vans::VansCameraFollowMode::SpringArm;
	solvedRig.follow.targetBinding = "Avatar";
	solvedRig.follow.localOffset = { 0.0f, 0.0f, -4.0f };
	solvedRig.lookAt.enabled = true;
	solvedRig.lookAt.targetBinding = "Avatar";
	solvedRig.collision.enabled = true;
	solvedRig.collision.radius = 0.25f;
	solvedRig.collision.minimumDistance = 0.5f;
	solvedRig.collision.padding = 0.25f;
	solvedRig.collision.recoverySeconds = 0.5f;
	const Vans::VansCameraRigHandle solvedRigHandle =
		cameraRuntime.RegisterRig(solvedRig, error);
	bool cameraBlocked = true;
	cameraRuntime.SetBindingResolver([](Vans::VansGenerationHandle context,
		std::string_view binding, Vans::VansCameraBindingSnapshot& target)
	{
		if (context != Vans::VansGenerationHandle{ 5, 2 } || binding != "Avatar")
			return false;
		target.pose.position = { 10.0f, 0.0f, 0.0f };
		target.pose.rotationDegrees = { 0.0f, 0.0f, 0.0f };
		return true;
	});
	cameraRuntime.SetCollisionResolver([&](const Vans::VansCameraCollisionQuery& query,
		Vans::VansCameraCollisionResult& hit)
	{
		if (query.bindingContext != Vans::VansGenerationHandle{ 5, 2 } ||
			std::abs(query.radius - 0.25f) > 0.001f) return false;
		hit.blocked = cameraBlocked;
		hit.distance = cameraBlocked ? 2.0f : glm::length(
			query.desiredPosition - query.origin);
		return true;
	});
	const Vans::VansCameraViewId solvedView =
		Vans::VansMakeStableId<Vans::VansCameraViewIdTag>("Camera.View.SolverContract");
	if (!solvedRigHandle || !cameraRuntime.BindViewRig(
		solvedView, solvedRigHandle, error, { 5, 2 })) return Expect(false, error.c_str());
	cameraRuntime.Advance(0.016);
	const Vans::VansResolvedCameraView obstructed = cameraRuntime.ResolveView(solvedView);
	if (!Expect(std::abs(glm::length(obstructed.snapshot.pose.position -
			glm::vec3(10.0f, 0.0f, 0.0f)) - 1.75f) < 0.001f &&
		std::abs(obstructed.snapshot.pose.rotationDegrees.y - 90.0f) < 0.001f,
		"CameraCore rig solver did not apply follow, look-at, and collision retraction"))
		return false;
	cameraBlocked = false;
	cameraRuntime.Advance(0.1);
	const float recoveringDistance = glm::length(
		cameraRuntime.ResolveView(solvedView).snapshot.pose.position -
		glm::vec3(10.0f, 0.0f, 0.0f));
	if (!Expect(recoveringDistance > 1.75f && recoveringDistance < 4.0f,
		"CameraCore collision recovery snapped instead of damping outward")) return false;
	for (int index = 0; index < 100; ++index) cameraRuntime.Advance(0.1);
	if (!Expect(std::abs(glm::length(cameraRuntime.ResolveView(solvedView).snapshot.pose.position -
			glm::vec3(10.0f, 0.0f, 0.0f)) - 4.0f) < 0.01f,
		"CameraCore collision recovery did not converge to the desired arm length")) return false;
	if (!Expect(cameraRuntime.ReleaseDomain(actionDomain) == 2 &&
		cameraRuntime.ContributionCount() == 0,
		"CameraCore domain cleanup did not release all persistent contributions")) return false;
	return Expect(cameraRuntime.UnregisterShake(shakeHandle) &&
		cameraRuntime.UnregisterRig(solvedRigHandle) &&
		cameraRuntime.UnregisterRig(rigHandle) &&
		!cameraRuntime.ResolveRig(rigHandle),
		"CameraCore rig generation lifetime is invalid");
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
	if (!Expect(world.SetEntityName(parent, "RenamedParent"),
		"Runtime world failed to rename entity"))
		return false;
	if (!Expect(world.Entities().Get(parent)->name == "RenamedParent",
		"Runtime world rename did not update the entity record"))
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

	auto* storage = world.RegisterStorage<RuntimeWorldTestComponent>(111);
	if (!Expect(storage != nullptr, "Runtime world failed to register test component storage"))
		return false;
	const Vans::VansComponentHandle parentComponent =
		storage->Add(parent, RuntimeWorldTestComponent{ 1 }, "parent-component-guid", true, true);
	const Vans::VansComponentHandle childComponent =
		storage->Add(child, RuntimeWorldTestComponent{ 2 }, "child-component-guid", true, true);
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

bool TestEmptySceneEntityFactoryContract()
{
    Vans::SceneEmptyEntityFactoryRequest request;
    request.entityName = "Empty Object";
    request.transformComponentGuid = "empty-transform-guid";
    const Vans::VansSerializedValue entity =
        Vans::VansSceneEntityFactory::BuildEmptyEntity(request, "empty-entity-guid");

    if (!Expect(Vans::ReadSerializedStringField(entity, "id") == "empty-entity-guid" &&
        Vans::ReadSerializedStringField(entity, "name") == "Empty Object",
        "Empty object factory did not preserve the requested entity identity"))
    {
        return false;
    }
    const Vans::VansSerializedValue* parent = Vans::FindObjectField(entity, "parent");
    if (!Expect(parent && parent->kind == Vans::VansSerializedValue::Kind::Null,
        "Root empty object factory did not serialize a null parent"))
    {
        return false;
    }
    const Vans::VansSerializedValue* components = Vans::FindObjectField(entity, "components");
    if (!Expect(components && components->kind == Vans::VansSerializedValue::Kind::Array &&
        components->arrayItems.size() == 1,
        "Empty object factory did not produce exactly one component"))
    {
        return false;
    }
    const Vans::VansSerializedValue& transform = components->arrayItems.front();
    if (!Expect(Vans::ReadSerializedStringField(transform, "id") == "empty-transform-guid" &&
        Vans::ReadSerializedStringField(transform, "type") == "Transform" &&
        Vans::ReadSerializedBoolField(transform, "enabled", false),
        "Empty object factory did not produce an enabled Transform component"))
    {
        return false;
    }

	const Vans::VansSerializedValue entities =
		Vans::VansSerializedValue::Array({ entity });
    Vans::VansSceneContentBuildPlan plan;
    std::string error;
	if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
		entities,
        "",
        plan,
        error), error.c_str()))
    {
        return false;
    }
    if (!Expect(plan.objects.objects.size() == 1 && plan.objects.objects.front().transform.has_value(),
		"Transform-only empty object did not project without a synthetic Scene document"))
    {
        return false;
    }
    const Vans::VansSceneObjectBuildConfig& object = plan.objects.objects.front();
    const Vans::VansSceneTransformConfig& projectedTransform = *object.transform;
    if (!Expect(object.entityGuid == "empty-entity-guid" &&
        object.name == "Empty Object" &&
        object.componentGuids.find("transform") != object.componentGuids.end() &&
        object.componentGuids.at("transform") == "empty-transform-guid" &&
        !object.render.has_value(),
        "Empty object runtime projection changed its identity or added a renderer"))
    {
        return false;
    }
    if (!ExpectNear(projectedTransform.position[0], 0.0f, 0.0001f,
        "Empty object default Transform position X changed") ||
        !ExpectNear(projectedTransform.position[1], 0.0f, 0.0001f,
        "Empty object default Transform position Y changed") ||
        !ExpectNear(projectedTransform.position[2], 0.0f, 0.0001f,
        "Empty object default Transform position Z changed") ||
        !ExpectNear(projectedTransform.scale[0], 1.0f, 0.0001f,
        "Empty object default Transform scale X changed") ||
        !ExpectNear(projectedTransform.scale[1], 1.0f, 0.0001f,
        "Empty object default Transform scale Y changed") ||
        !ExpectNear(projectedTransform.scale[2], 1.0f, 0.0001f,
        "Empty object default Transform scale Z changed"))
    {
        return false;
    }

    constexpr const char* animatorGuid = "44444444-4444-4444-8444-444444444444";
    constexpr const char* rigGuid = "55555555-5555-4555-8555-555555555555";
    const Vans::VansSerializedValue animationEntity = Vans::VansSerializedValue::Object({
        { "id", Vans::VansSerializedValue::String("animation-entity-guid") },
        { "name", Vans::VansSerializedValue::String("Animation Entity") },
        { "active", Vans::VansSerializedValue::Bool(true) },
        { "parent", Vans::VansSerializedValue::Null() },
        { "components", Vans::VansSerializedValue::Array({
            Vans::VansSerializedValue::Object({
                { "id", Vans::VansSerializedValue::String("animation-component-guid") },
                { "type", Vans::VansSerializedValue::String("Animation") },
                { "enabled", Vans::VansSerializedValue::Bool(true) },
                { "data", Vans::VansSerializedValue::Object({
                    { "name", Vans::VansSerializedValue::String("Animation Entity") },
                    { "mesh_group", Vans::VansSerializedValue::String("Character") },
                    { "animator", Vans::VansSerializedValue::Object({
                        { "guid", Vans::VansSerializedValue::String(animatorGuid) }
                    }) },
                    { "rig", Vans::VansSerializedValue::Object({
                        { "guid", Vans::VansSerializedValue::String(rigGuid) }
                    }) }
                }) }
            })
        }) }
    });
    Vans::VansSceneContentBuildPlan animationPlan;
    error.clear();
    if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
        Vans::VansSerializedValue::Array({ animationEntity }),
        "C:/PackageRoot",
        animationPlan,
        error), error.c_str()))
    {
        return false;
    }
    if (!Expect(animationPlan.objects.objects.size() == 1 &&
        animationPlan.objects.objects.front().animation.has_value() &&
        animationPlan.objects.objects.front().animation->animatorGuid == animatorGuid &&
        animationPlan.objects.objects.front().animation->rigGuid == rigGuid,
        "Runtime Scene projection rewrote Animation asset GUIDs into disk paths"))
    {
        return false;
    }

    constexpr const char* parentGuid = "11111111-1111-4111-8111-111111111111";
    constexpr const char* animationComponentGuid = "22222222-2222-4222-8222-222222222222";
    constexpr const char* boneGuid = "33333333-3333-4333-8333-333333333333";
    Vans::VansSceneParentReference entityParent;
    entityParent.kind = Vans::VansSceneParentKind::Entity;
    if (!Expect(Vans::VansAssetGuid::TryParse(parentGuid, entityParent.entityGuid),
        "Empty child test parent guid is invalid"))
    {
        return false;
    }
    request.parent = entityParent;
    const Vans::VansSerializedValue child =
        Vans::VansSceneEntityFactory::BuildEmptyEntity(request, "child-entity-guid");
    const Vans::VansSerializedValue* childParentValue = Vans::FindObjectField(child, "parent");
    Vans::VansSceneParentReference childParent;
    std::string parentError;
    if (!Expect(childParentValue
        && Vans::TryReadSceneParentReference(*childParentValue, childParent, parentError)
        && childParent.kind == Vans::VansSceneParentKind::Entity
        && childParent.entityGuid == entityParent.entityGuid,
        "Empty child factory did not preserve its canonical entity parent reference"))
    {
        return false;
    }

    Vans::VansSceneParentReference boneParent;
    boneParent.kind = Vans::VansSceneParentKind::Bone;
    if (!Expect(Vans::VansAssetGuid::TryParse(parentGuid, boneParent.entityGuid)
        && Vans::VansAssetGuid::TryParse(animationComponentGuid,
            boneParent.animationComponentGuid)
        && Vans::VansAssetGuid::TryParse(boneGuid, boneParent.anchorGuid),
        "Empty child test bone parent identity is invalid"))
    {
        return false;
    }
    request.parent = boneParent;
    const Vans::VansSerializedValue boneChild =
        Vans::VansSceneEntityFactory::BuildEmptyEntity(request, "bone-child-entity-guid");
    const Vans::VansSerializedValue* boneParentValue =
        Vans::FindObjectField(boneChild, "parent");
    Vans::VansSceneParentReference decodedBoneParent;
    parentError.clear();
    return Expect(boneParentValue
        && Vans::TryReadSceneParentReference(
            *boneParentValue, decodedBoneParent, parentError)
        && decodedBoneParent.kind == Vans::VansSceneParentKind::Bone
        && decodedBoneParent.entityGuid == boneParent.entityGuid
        && decodedBoneParent.animationComponentGuid == boneParent.animationComponentGuid
        && decodedBoneParent.anchorGuid == boneParent.anchorGuid,
        "Empty child factory did not preserve its canonical bone parent reference");
}

bool TestLocalVolumetricFogEntityFactoryContract()
{
	Vans::SceneLocalVolumetricFogEntityFactoryRequest request;
	request.entityName = "Ground Fog";
	request.transformComponentGuid = "ground-fog-transform";
	request.fogComponentGuid = "ground-fog-component";
	request.position = { -3.0f, 0.25f, -10.0f };
	request.scale = { 54.0f, 2.5f, 40.0f };
	request.settings.visibilityDistanceMeters = 150.0f;
	request.settings.edgeFadeDistanceMeters = 0.6f;
	request.settings.skyLightingScale = 0.7f;
	request.settings.receiveCloudShadows = true;
	request.settings.shapeMask.enabled = true;
	request.settings.shapeMask.source.assetGuid = "11111111-2222-4333-8444-555555555555";
	request.settings.shapeMask.source.channel = Vans::VansLocalFogTextureChannel::G;
	request.settings.shapeMask.mapping.tiling = { 2.0f, 3.0f };
	request.settings.detailNoise.enabled = true;
	request.settings.detailNoise.source.assetGuid = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
	request.settings.detailNoise.mapping.offset = { 0.25f, -0.5f };
	request.settings.flow.enabled = true;
	request.settings.flow.source.assetGuid = "99999999-8888-4777-8666-555555555555";
	request.settings.flow.source.xChannel = Vans::VansLocalFogTextureChannel::B;
	request.settings.flow.source.zChannel = Vans::VansLocalFogTextureChannel::A;
	request.settings.flow.fallbackDirectionLocalXZ = { 3.0f, 4.0f };
	request.settings.flow.speedMetersPerSecond = 2.0f;
	request.settings.flow.loopDistanceMeters = 5.0f;
	request.settings.flow.phaseOffset01 = 1.25f;
	const Vans::VansSerializedValue entity =
		Vans::VansSceneEntityFactory::BuildLocalVolumetricFogEntity(
			request, "ground-fog-entity");
	const Vans::VansSerializedValue* components =
		Vans::FindObjectField(entity, "components");
	if (!Expect(
		Vans::ReadSerializedStringField(entity, "id") == "ground-fog-entity" &&
		Vans::ReadSerializedStringField(entity, "name") == "Ground Fog" &&
		components && components->kind == Vans::VansSerializedValue::Kind::Array &&
		components->arrayItems.size() == 2,
		"Local fog factory did not produce the canonical entity structure"))
	{
		return false;
	}
	const Vans::VansSerializedValue& transform = components->arrayItems[0];
	const Vans::VansSerializedValue& fog = components->arrayItems[1];
	const Vans::VansSerializedValue* fogData = Vans::FindObjectField(fog, "data");
	const Vans::VansSerializedValue* visibility = fogData
		? Vans::FindObjectField(*fogData, "visibilityDistanceMeters") : nullptr;
	const Vans::VansSerializedValue* edgeFade = fogData
		? Vans::FindObjectField(*fogData, "edgeFadeDistanceMeters") : nullptr;
	const Vans::VansSerializedValue* shape = fogData
		? Vans::FindObjectField(*fogData, "shapeMask") : nullptr;
	const Vans::VansSerializedValue* detail = fogData
		? Vans::FindObjectField(*fogData, "detailNoise") : nullptr;
	const Vans::VansSerializedValue* flow = fogData
		? Vans::FindObjectField(*fogData, "flow") : nullptr;
	const Vans::VansSerializedValue* shapeSource = shape
		? Vans::FindObjectField(*shape, "source") : nullptr;
	const Vans::VansSerializedValue* shapeAsset = shapeSource
		? Vans::FindObjectField(*shapeSource, "asset") : nullptr;
	const Vans::VansSerializedValue* shapeMapping = shape
		? Vans::FindObjectField(*shape, "mapping") : nullptr;
	const Vans::VansSerializedValue* flowSource = flow
		? Vans::FindObjectField(*flow, "source") : nullptr;
	const Vans::VansSerializedValue* flowAsset = flowSource
		? Vans::FindObjectField(*flowSource, "asset") : nullptr;
	const Vans::VansSerializedValue* fallback = flow
		? Vans::FindObjectField(*flow, "fallbackDirectionLocalXZ") : nullptr;
	const Vans::VansSerializedValue* phase = flow
		? Vans::FindObjectField(*flow, "phaseOffset01") : nullptr;
	return Expect(
		Vans::ReadSerializedStringField(transform, "id") == "ground-fog-transform" &&
		Vans::ReadSerializedStringField(transform, "type") == "Transform" &&
		Vans::ReadSerializedStringField(fog, "id") == "ground-fog-component" &&
		Vans::ReadSerializedStringField(fog, "type") == "LocalVolumetricFog" &&
		Vans::ReadSerializedBoolField(fog, "enabled", false) &&
		visibility && std::abs(Vans::ReadSerializedNumber(*visibility) - 150.0) < 1.0e-6 &&
		edgeFade && std::abs(Vans::ReadSerializedNumber(*edgeFade) - 0.6) < 1.0e-6 &&
		Vans::ReadSerializedBoolField(*fogData, "receiveCloudShadows", false) &&
		shape && detail && flow && shapeSource && shapeAsset && shapeMapping &&
		flowSource && flowAsset &&
		Vans::ReadSerializedBoolField(*shape, "enabled", false) &&
		Vans::ReadSerializedStringField(*shapeSource, "channels") == "g" &&
		Vans::ReadSerializedStringField(*shapeAsset, "guid") ==
			"11111111-2222-4333-8444-555555555555" &&
		Vans::ReadSerializedStringField(*shapeMapping, "projection") == "localXZ" &&
		Vans::ReadSerializedStringField(*shapeMapping, "addressMode") ==
			"clampToBorderZero" &&
		Vans::ReadSerializedBoolField(*detail, "enabled", false) &&
		Vans::ReadSerializedStringField(*flowSource, "channels") == "ba" &&
		Vans::ReadSerializedStringField(*flowAsset, "guid") ==
			"99999999-8888-4777-8666-555555555555" &&
		fallback && fallback->kind == Vans::VansSerializedValue::Kind::Array &&
		fallback->arrayItems.size() == 2 &&
		std::abs(Vans::ReadSerializedNumber(fallback->arrayItems[0]) - 0.6) < 1.0e-6 &&
		std::abs(Vans::ReadSerializedNumber(fallback->arrayItems[1]) - 0.8) < 1.0e-6 &&
		phase && std::abs(Vans::ReadSerializedNumber(*phase) - 0.25) < 1.0e-6,
		"Local fog factory did not serialize the canonical Shape/Detail/Flow schema");
}

bool TestLocalVolumetricFogFieldPreviewContract()
{
	const std::uint8_t scalarPixel[] = { 10, 64, 128, 255 };
	Vans::EditorAPI::LocalFogFieldPreviewRequest scalarRequest;
	scalarRequest.channels = "b";
	scalarRequest.kind = Vans::EditorAPI::LocalFogFieldPreviewKind::Scalar;
	scalarRequest.sampleColumns = 1;
	scalarRequest.sampleRows = 100;
	const Vans::EditorAPI::LocalFogFieldPreviewSnapshot scalarPreview =
		Vans::EditorAPI::BuildLocalFogFieldPreviewFromRgba8(
			scalarPixel, sizeof(scalarPixel), 1, 1, scalarRequest);
	if (!Expect(scalarPreview.available && scalarPreview.sourceWidth == 1 &&
		scalarPreview.sourceHeight == 1 && scalarPreview.sampleColumns == 4 &&
		scalarPreview.sampleRows == 32 && scalarPreview.scalarSamples.size() == 128 &&
		std::all_of(scalarPreview.scalarSamples.begin(), scalarPreview.scalarSamples.end(),
			[](float value) { return std::abs(value - 128.0f / 255.0f) < 1.0e-6f; }),
		"Local Fog scalar preview did not select the requested channel or clamp its grid"))
	{
		return false;
	}

	const std::uint8_t axisPixel[] = { 255, 128, 0, 255 };
	Vans::EditorAPI::LocalFogFieldPreviewRequest flowRequest;
	flowRequest.channels = "rg";
	flowRequest.kind = Vans::EditorAPI::LocalFogFieldPreviewKind::FlowVector;
	flowRequest.sampleColumns = 4;
	flowRequest.sampleRows = 4;
	const Vans::EditorAPI::LocalFogFieldPreviewSnapshot axisPreview =
		Vans::EditorAPI::BuildLocalFogFieldPreviewFromRgba8(
			axisPixel, sizeof(axisPixel), 1, 1, flowRequest);
	if (!Expect(axisPreview.available && axisPreview.flowSamples.size() == 16 &&
		std::all_of(axisPreview.flowSamples.begin(), axisPreview.flowSamples.end(),
			[](const Vans::EditorAPI::Vec2& value)
			{
				return std::abs(value.x - 1.0f) < 1.0e-6f &&
					std::abs(value.y) < 1.0e-6f;
			}),
		"Local Fog flow preview did not apply the shader-equivalent 8-bit neutral dead zone"))
	{
		return false;
	}

	const std::uint8_t diagonalPixel[] = { 255, 255, 0, 255 };
	const Vans::EditorAPI::LocalFogFieldPreviewSnapshot diagonalPreview =
		Vans::EditorAPI::BuildLocalFogFieldPreviewFromRgba8(
			diagonalPixel, sizeof(diagonalPixel), 1, 1, flowRequest);
	const float diagonal = std::sqrt(0.5f);
	if (!Expect(diagonalPreview.available && !diagonalPreview.flowSamples.empty() &&
		std::abs(diagonalPreview.flowSamples.front().x - diagonal) < 1.0e-6f &&
		std::abs(diagonalPreview.flowSamples.front().y - diagonal) < 1.0e-6f,
		"Local Fog flow preview did not clamp decoded vector length like the shader"))
	{
		return false;
	}

	flowRequest.channels = "rr";
	const Vans::EditorAPI::LocalFogFieldPreviewSnapshot invalidPreview =
		Vans::EditorAPI::BuildLocalFogFieldPreviewFromRgba8(
			axisPixel, sizeof(axisPixel), 1, 1, flowRequest);
	return Expect(!invalidPreview.available && !invalidPreview.message.empty(),
		"Local Fog flow preview accepted duplicate vector channels");
}

bool TestLocalVolumetricFogRuntimePreviewProjectionContract()
{
	Vans::SceneLocalVolumetricFogEntityFactoryRequest request;
	request.fogComponentGuid = "live-local-fog-component";
	request.settings.visibilityDistanceMeters = 321.0f;
	request.settings.detailNoise.enabled = true;
	request.settings.detailNoise.source.assetGuid =
		"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
	request.settings.detailNoise.mapping.tiling = { 7.0f, 9.0f };
	request.settings.detailNoise.influence = 0.35f;
	request.settings.flow.enabled = true;
	request.settings.flow.fallbackDirectionLocalXZ = { 0.0f, 1.0f };
	request.settings.flow.speedMetersPerSecond = 4.5f;
	const Vans::VansSerializedValue entity =
		Vans::VansSceneEntityFactory::BuildLocalVolumetricFogEntity(
			request, "live-local-fog-entity");

	const Vans::EditorAPI::RuntimeEntityPreviewChange change =
		Vans::BuildRuntimeEntityPreviewChange(entity);
	if (!Expect(change.localVolumetricFogEdits.size() == 1,
		"Local Fog Inspector preview did not carry the current component schema"))
		return false;

	const Vans::EditorAPI::RuntimeLocalVolumetricFogEdit& edit =
		change.localVolumetricFogEdits.front();
	Vans::VansSceneLocalVolumetricFogComponentConfig projected;
	if (!Expect(edit.entityGuid == "live-local-fog-entity" &&
		edit.componentGuid == "live-local-fog-component" &&
		Vans::VansSceneRuntimeProjection::ProjectLocalVolumetricFogComponent(
			edit.component, projected),
		"Local Fog runtime preview could not reuse the canonical scene projection"))
		return false;

	return Expect(
		std::abs(projected.visibilityDistanceMeters - 321.0f) < 1.0e-6f &&
		projected.detailNoise.enabled &&
		projected.detailNoise.source.assetGuid ==
			"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee" &&
		std::abs(projected.detailNoise.mapping.tiling[0] - 7.0f) < 1.0e-6f &&
		std::abs(projected.detailNoise.mapping.tiling[1] - 9.0f) < 1.0e-6f &&
		std::abs(projected.detailNoise.influence - 0.35f) < 1.0e-6f &&
		projected.flow.enabled &&
		std::abs(projected.flow.speedMetersPerSecond - 4.5f) < 1.0e-6f,
		"Local Fog runtime preview lost Noise/Flow rendering parameters");
}

bool TestLocalVolumetricFogFieldDependencyContract()
{
	TemporaryDirectory temporary;
	const fs::path assetsRoot = temporary.path / "Assets";
	const fs::path artifactRoot = temporary.path / "Library" / "Artifacts";
	const fs::path texturePath = assetsRoot / "PackedLocalFogFields.tga";
	fs::create_directories(assetsRoot);
	const std::uint8_t tga[] = {
		0, 0, 2, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 2, 0, 2, 0,
		32, 0x28,
		128, 128, 255, 255, 128, 128, 255, 255,
		128, 128, 255, 255, 128, 128, 255, 255
	};
	{
		std::ofstream texture(texturePath, std::ios::binary | std::ios::trunc);
		texture.write(reinterpret_cast<const char*>(tga), sizeof(tga));
		if (!Expect(static_cast<bool>(texture),
			"Could not create the Local Fog field texture fixture"))
			return false;
	}

	Vans::VansAssetGuid textureGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"12345678-1234-4234-8234-123456789abc", textureGuid),
		"Could not parse the Local Fog field texture fixture GUID"))
		return false;
	Vans::VansAssetMeta meta;
	meta.guid = textureGuid;
	meta.importer = Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Texture);
	auto saveTextureMeta = [&](bool linear, int importChannel = 4,
		const char* precision = "low8")
	{
		meta.SetSerializedSettings(Vans::VansSerializedValue::Object({
			{ "colorSpace", Vans::VansSerializedValue::String(
				linear ? "linear" : "sRGB") },
			{ "sRGB", Vans::VansSerializedValue::Bool(!linear) },
			{ "useCompress", Vans::VansSerializedValue::Bool(false) },
			{ "needMip", Vans::VansSerializedValue::Bool(true) },
			{ "importChannel", Vans::VansSerializedValue::Int(importChannel) },
			{ "precision", Vans::VansSerializedValue::String(precision) }
		}));
		std::string error;
		return Vans::VansAssetMetaStorage::SaveAtomic(
			Vans::VansAssetMeta::MetaPathFor(texturePath), meta, error);
	};
	if (!Expect(saveTextureMeta(true),
		"Could not create valid Local Fog field texture metadata"))
		return false;

	Vans::VansAssetDatabase database(assetsRoot, artifactRoot);
	if (!Expect(static_cast<bool>(
		database.Scan(Vans::VansAssetOperationPolicy::ReadOnly())),
		"Could not index the Local Fog field texture fixture"))
		return false;
	Vans::VansAssetObjectRepository objectRepository;
	auto refreshObjectRepository = [&]()
	{
		objectRepository.Clear();
		const Vans::VansAssetScanResult scan =
			database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
		if (!scan) return false;
		return static_cast<bool>(Vans::VansAssetObjectBootstrapper::Publish(
			database.All(), objectRepository));
	};
	if (!Expect(refreshObjectRepository(),
		"Could not publish Local Fog field texture metadata to memory"))
		return false;

	auto buildSceneDocument = [&](Vans::VansLocalFogTextureAddressMode detailAddress)
	{
		Vans::VansSceneData scene;
		scene.sceneGuid = Vans::VansAssetGuid::New();
		Vans::VansSceneEntityData entity;
		entity.id = Vans::VansEntityGuid::New();
		entity.name = "Local Fog Field Dependency Probe";
		entity.components.push_back(Vans::VansSceneSchema::MakeTransform());
		Vans::VansSceneLocalVolumetricFogComponentConfig settings;
		settings.shapeMask.enabled = true;
		settings.shapeMask.source.assetGuid = textureGuid.ToString();
		settings.shapeMask.source.channel = Vans::VansLocalFogTextureChannel::R;
		settings.detailNoise.enabled = true;
		settings.detailNoise.source.assetGuid = textureGuid.ToString();
		settings.detailNoise.source.channel = Vans::VansLocalFogTextureChannel::G;
		settings.detailNoise.mapping.addressMode = detailAddress;
		settings.flow.enabled = true;
		settings.flow.source.assetGuid = textureGuid.ToString();
		settings.flow.source.xChannel = Vans::VansLocalFogTextureChannel::B;
		settings.flow.source.zChannel = Vans::VansLocalFogTextureChannel::A;
		settings.flow.speedMetersPerSecond = 1.0f;
		Vans::VansSceneComponentData fog;
		fog.id = Vans::VansComponentGuid::New();
		fog.type = "LocalVolumetricFog";
		fog.enabled = true;
		fog.data = Vans::VansSceneEntityFactory::BuildLocalVolumetricFogComponentData(
			settings);
		entity.components.push_back(std::move(fog));
		scene.entities.push_back(std::move(entity));
		return Vans::DecodeSerializedValueJson(
			Vans::VansSceneSchema::SerializeSceneJson(scene));
	};

	const fs::path sceneSourcePath = temporary.path / "Scenes" / "LocalFog.scene.json";
	Vans::VansIOAudit::Reset();
	const Vans::VansSceneAssetDependencyBuildResult valid =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			buildSceneDocument(Vans::VansLocalFogTextureAddressMode::Repeat),
			sceneSourcePath,
			{},
			objectRepository);
	const auto dependencyIO = Vans::VansIOAudit::Snapshot();
	if (!Expect(valid.success && valid.requiredTextures.size() == 1 &&
		valid.requiredTextures.find(textureGuid.ToString()) != valid.requiredTextures.end() &&
		valid.resourcePlan.textures.size() == 1 &&
		!valid.resourcePlan.textures.front().srgb &&
		!valid.resourcePlan.textures.front().useCompress &&
		valid.resourcePlan.textures.front().needMip,
		"Local Fog Shape/Detail/Flow did not deduplicate into one valid texture dependency"))
		return false;
	if (!Expect(std::none_of(
		dependencyIO.begin(), dependencyIO.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.domain == Vans::VansIODomain::Authoring;
		}),
		"Local Fog dependency planning touched authoring storage after memory bootstrap"))
		return false;

	const Vans::VansTextureArtifactEnsureResult ensured =
		database.EnsureTextureArtifact(textureGuid);
	Vans::VansCookedTextureData cookedTexture;
	std::string cookedTextureError;
	if (!Expect(ensured.status == Vans::VansTextureArtifactEnsureStatus::Cooked &&
		fs::is_regular_file(ensured.artifactPath) &&
		Vans::VansTextureCooker::LoadArtifact(
			ensured.artifactPath, cookedTexture, cookedTextureError) &&
		cookedTexture.format == Vans::VansCookedTextureFormat::RGBA8 &&
		cookedTexture.width == 2 && cookedTexture.height == 2 &&
		cookedTexture.mips.size() == 2 && cookedTexture.data.size() == 20,
		"Local Fog field texture did not produce a complete uncompressed RGBA8 .vtex artifact"))
	{
		return false;
	}
	const Vans::VansTextureArtifactEnsureResult current =
		database.EnsureTextureArtifact(textureGuid);
	if (!Expect(current.status == Vans::VansTextureArtifactEnsureStatus::UpToDate,
		"Local Fog field texture artifact was not reused after a current cook"))
	{
		return false;
	}
	const std::optional<Vans::VansAssetRecord> cookedRecord = database.Find(textureGuid);
	if (!Expect(cookedRecord &&
		cookedRecord->artifactFormat == Vans::VansAssetArtifactFormat::Imported &&
		cookedRecord->artifactPath == ensured.artifactPath,
		"Local Fog field texture artifact was not published into the asset index"))
	{
		return false;
	}
	Vans::VansAssetResolver packagedResolver(
		Vans::VansAssetAccessMode::Package, { *cookedRecord });
	const Vans::VansResolvedAsset packagedTexture = packagedResolver.Resolve(
		textureGuid.ToString(), Vans::VansAssetType::Texture);
	if (!Expect(packagedTexture.valid && packagedTexture.artifactAvailable &&
		packagedTexture.readPath == ensured.artifactPath,
		"Packaged Local Fog field texture did not resolve to its cooked-only artifact"))
	{
		return false;
	}
	const auto verifyCompactFieldArtifact = [&](const char* fileName,
		const char* guidText,
		int importChannel,
		Vans::VansCookedTextureFormat expectedFormat,
		std::size_t expectedBytes)
	{
		const fs::path compactTexturePath = assetsRoot / fileName;
		{
			std::ofstream texture(compactTexturePath, std::ios::binary | std::ios::trunc);
			const std::uint8_t compactTgaHeader[] = {
				0, 0, 2, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 3, 0, 3, 0,
				32, 0x28
			};
			const std::uint8_t compactPixel[] = { 128, 64, 255, 255 };
			texture.write(reinterpret_cast<const char*>(compactTgaHeader),
				sizeof(compactTgaHeader));
			for (int pixel = 0; pixel < 9; ++pixel)
				texture.write(reinterpret_cast<const char*>(compactPixel), sizeof(compactPixel));
			if (!texture)
				return false;
		}
		Vans::VansAssetMeta compactMeta;
		if (!Vans::VansAssetGuid::TryParse(guidText, compactMeta.guid))
			return false;
		compactMeta.importer = Vans::VansAssetDatabase::ImporterFor(
			Vans::VansAssetType::Texture);
		compactMeta.SetSerializedSettings(Vans::VansSerializedValue::Object({
			{ "colorSpace", Vans::VansSerializedValue::String("linear") },
			{ "sRGB", Vans::VansSerializedValue::Bool(false) },
			{ "useCompress", Vans::VansSerializedValue::Bool(false) },
			{ "needMip", Vans::VansSerializedValue::Bool(true) },
			{ "importChannel", Vans::VansSerializedValue::Int(importChannel) }
		}));
		const fs::path compactMetaPath = Vans::VansAssetMeta::MetaPathFor(compactTexturePath);
		std::string error;
		if (!Vans::VansAssetMetaStorage::SaveAtomic(compactMetaPath, compactMeta, error))
			return false;
		const Vans::VansTextureCookResult cook = Vans::VansTextureCooker::CookIfNeeded(
			compactTexturePath, compactMetaPath, compactMeta, artifactRoot);
		Vans::VansCookedTextureData loaded;
		return cook.status == Vans::VansTextureCookStatus::Cooked &&
			Vans::VansTextureCooker::LoadArtifact(cook.artifactPath, loaded, error) &&
			loaded.format == expectedFormat && loaded.mips.size() == 2 &&
			loaded.data.size() == expectedBytes &&
			std::all_of(loaded.mips.begin(), loaded.mips.end(),
				[](const Vans::VansCookedTextureMip& mip) { return mip.offset % 4u == 0u; });
	};
	if (!Expect(
		verifyCompactFieldArtifact(
			"ShapeR8.tga", "22345678-1234-4234-8234-123456789abc", 1,
			Vans::VansCookedTextureFormat::R8, 13) &&
		verifyCompactFieldArtifact(
			"FlowRG8.tga", "32345678-1234-4234-8234-123456789abc", 2,
			Vans::VansCookedTextureFormat::RG8, 22),
		"Local Fog compact R8/RG8 field textures did not round-trip through .vtex"))
	{
		return false;
	}

	const Vans::VansSceneAssetDependencyBuildResult invalidAddress =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			buildSceneDocument(Vans::VansLocalFogTextureAddressMode::ClampToEdge),
			sceneSourcePath,
			{},
			objectRepository);
	if (!Expect(!invalidAddress.success && !invalidAddress.errors.empty(),
		"Local Fog dependency validation accepted flowed non-repeat Detail Noise"))
		return false;

	if (!Expect(saveTextureMeta(true, 2),
		"Could not create the insufficient-channel Local Fog metadata"))
		return false;
	if (!Expect(refreshObjectRepository(),
		"Could not refresh insufficient-channel Local Fog metadata in memory"))
		return false;
	const Vans::VansSceneAssetDependencyBuildResult invalidChannels =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			buildSceneDocument(Vans::VansLocalFogTextureAddressMode::Repeat),
			sceneSourcePath,
			{},
			objectRepository);
	if (!Expect(!invalidChannels.success && !invalidChannels.errors.empty(),
		"Local Fog dependency validation accepted BA flow channels from an RG texture"))
		return false;

	if (!Expect(saveTextureMeta(true, 4, "high16"),
		"Could not create the unsupported-precision Local Fog metadata"))
		return false;
	if (!Expect(refreshObjectRepository(),
		"Could not refresh unsupported-precision Local Fog metadata in memory"))
		return false;
	const Vans::VansSceneAssetDependencyBuildResult invalidPrecision =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			buildSceneDocument(Vans::VansLocalFogTextureAddressMode::Repeat),
			sceneSourcePath,
			{},
			objectRepository);
	if (!Expect(!invalidPrecision.success && !invalidPrecision.errors.empty(),
		"Local Fog dependency validation accepted a non-cookable texture precision"))
		return false;

	if (!Expect(saveTextureMeta(false, 4),
		"Could not create invalid Local Fog field texture metadata"))
		return false;
	if (!Expect(refreshObjectRepository(),
		"Could not refresh sRGB Local Fog metadata in memory"))
		return false;
	const Vans::VansSceneAssetDependencyBuildResult invalidColorSpace =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			buildSceneDocument(Vans::VansLocalFogTextureAddressMode::Repeat),
			sceneSourcePath,
			{},
			objectRepository);
	return Expect(!invalidColorSpace.success && !invalidColorSpace.errors.empty(),
		"Local Fog dependency validation accepted an sRGB field texture");
}

bool TestTransformGraphAnchorContract()
{
	struct TransformLease
	{
		std::vector<std::uint32_t> ids;
		~TransformLease()
		{
			for (auto it = ids.rbegin(); it != ids.rend(); ++it)
				Vans::VansTransformStore::Release(*it);
		}
		std::uint32_t Allocate(const glm::vec3& position)
		{
			const std::uint32_t id = Vans::VansTransformStore::Allocate();
			ids.push_back(id);
			Vans::VansTransform transform = Vans::VansTransformStore::Read(id);
			transform.m_Position = position;
			transform.m_Rotation = glm::vec3(0.0f);
			transform.m_Scale = glm::vec3(1.0f);
			Vans::VansTransformStore::Write(id, transform);
			return id;
		}
	} transforms;

	class AnchorProvider final : public Vans::IVansTransformAnchorProvider
	{
	public:
		glm::mat4 model{ 1.0f };
		std::uint64_t revision = 1;
		bool ResolveModelSpaceTransform(
			const Vans::VansTransformAnchorHandle& handle,
			glm::mat4& outModelTransform,
			std::uint64_t& outPoseRevision) const override
		{
			if (handle.instanceId != 7 || handle.instanceGeneration != 3
				|| handle.anchorGuid != "test-bone-guid")
			{
				return false;
			}
			outModelTransform = model;
			outPoseRevision = revision;
			return true;
		}
	} provider;

	const std::uint32_t owner = transforms.Allocate(glm::vec3(10.0f, 0.0f, 0.0f));
	const std::uint32_t child = transforms.Allocate(glm::vec3(12.0f, 0.0f, 0.0f));
	const std::uint32_t attachment = transforms.Allocate(glm::vec3(25.0f, 5.0f, 0.0f));
	const std::uint32_t snappedAttachment = transforms.Allocate(glm::vec3(100.0f, 100.0f, 0.0f));
	const std::uint32_t profiledAttachment = transforms.Allocate(glm::vec3(-50.0f));
	Vans::VansTransformStore::ClearDirty();
	const std::uint64_t revisionBeforeRepeatedWrite =
		Vans::VansTransformStore::GetRevision();
	const std::uint64_t ownerRevisionBeforeRepeatedWrite =
		Vans::VansTransformStore::GetTransformRevision(owner);
	Vans::VansTransformStore::MarkDirty(owner);
	Vans::VansTransformStore::MarkDirty(owner);
	if (!Expect(Vans::VansTransformStore::GetRevision() ==
		revisionBeforeRepeatedWrite + 2u,
		"Transform revision did not advance for repeated same-frame writes") ||
		!Expect(Vans::VansTransformStore::GetTransformRevision(owner) >
			ownerRevisionBeforeRepeatedWrite,
			"Transform slot revision did not track its repeated same-frame writes"))
	{
		return false;
	}
	Vans::VansTransformStore::ClearDirty();
	Vans::VansTransformGraph graph(&provider);
	if (!Expect(graph.SetParent(child, owner, Vans::VansTransformReparentMode::KeepWorld)
		&& graph.Resolve(),
		"Transform graph could not create an entity parent link"))
	{
		return false;
	}
	if (!ExpectNear(Vans::VansTransformStore::Read(child).m_Position.x,
		12.0f, 0.0001f, "KeepWorld entity reparent changed child world position"))
	{
		return false;
	}
	Vans::VansTransform ownerTransform = Vans::VansTransformStore::Read(owner);
	ownerTransform.m_Position.x = 20.0f;
	Vans::VansTransformStore::Write(owner, ownerTransform);
	if (!Expect(graph.Resolve(), "Transform graph failed after its entity parent moved")
		|| !ExpectNear(Vans::VansTransformStore::Read(child).m_Position.x,
			22.0f, 0.0001f, "Entity child did not follow its parent transform"))
	{
		return false;
	}
	if (!Expect(!graph.SetParent(owner, child, Vans::VansTransformReparentMode::KeepLocal),
		"Transform graph accepted an entity hierarchy cycle"))
	{
		return false;
	}
	const std::uint32_t releasedParent = transforms.Allocate(glm::vec3(2.0f, 0.0f, 0.0f));
	const std::uint32_t orphanedChild = transforms.Allocate(glm::vec3(3.0f, 0.0f, 0.0f));
	if (!Expect(graph.SetParent(orphanedChild, releasedParent,
		Vans::VansTransformReparentMode::KeepWorld) && graph.Resolve(),
		"Transform graph could not create the released-parent probe"))
	{
		return false;
	}
	Vans::VansTransformStore::Release(releasedParent);
	transforms.ids.erase(std::remove(
		transforms.ids.begin(), transforms.ids.end(), releasedParent), transforms.ids.end());
	if (!Expect(!graph.Resolve() && !graph.HasParent(orphanedChild),
		"Transform graph retained or dereferenced a released parent Transform") ||
		!Expect(graph.GetLastError().find("released Transform storage") != std::string::npos,
			"Transform graph did not expose its released-parent failure"))
	{
		return false;
	}

	provider.model = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 5.0f, 0.0f));
	Vans::VansTransformAnchorHandle anchor;
	anchor.instanceId = 7;
	anchor.instanceGeneration = 3;
	anchor.kind = Vans::VansTransformAnchorKind::Bone;
	anchor.anchorGuid = "test-bone-guid";
	if (!Expect(graph.SetAnchor(attachment, owner, anchor,
		Vans::VansTransformReparentMode::KeepWorld) && graph.Resolve(),
		"Transform graph could not create a bone anchor link"))
	{
		return false;
	}
	const auto& attachedAtBind =
		Vans::VansTransformStore::Read(attachment).m_Position;
	if (!ExpectNear(attachedAtBind.x, 25.0f, 0.0001f,
		"KeepWorld bone attach changed attachment world X")
		|| !ExpectNear(attachedAtBind.y, 5.0f, 0.0001f,
			"KeepWorld bone attach changed attachment world Y"))
	{
		return false;
	}
	provider.model = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 8.0f, 0.0f));
	++provider.revision;
	if (!Expect(graph.Resolve(), "Transform graph failed after its animated bone moved"))
		return false;
	const auto& attachedAfterPose =
		Vans::VansTransformStore::Read(attachment).m_Position;
	if (!ExpectNear(attachedAfterPose.x, 25.0f, 0.0001f,
		"Bone attachment drifted in X after pose update")
		|| !ExpectNear(attachedAfterPose.y, 8.0f, 0.0001f,
			"Bone attachment did not follow the animated bone pose"))
	{
		return false;
	}
	if (!Expect(graph.SetAnchor(snappedAttachment, owner, anchor,
		Vans::VansTransformReparentMode::Snap) && graph.Resolve(),
		"Transform graph could not snap an entity to a bone anchor"))
	{
		return false;
	}
	const auto& snapped =
		Vans::VansTransformStore::Read(snappedAttachment).m_Position;
	if (!ExpectNear(snapped.x, 25.0f, 0.0001f,
		"Snap did not reset the attachment local X")
		|| !ExpectNear(snapped.y, 8.0f, 0.0001f,
			"Snap did not reset the attachment local Y"))
	{
		return false;
	}
	Vans::VansLocalTransform profileLocal;
	profileLocal.position = glm::vec3(2.0f, 3.0f, 4.0f);
	profileLocal.rotation = glm::angleAxis(
		glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	profileLocal.scale = glm::vec3(1.5f);
	if (!Expect(graph.SetAnchorWithLocalTransform(
		profiledAttachment, owner, anchor, profileLocal) && graph.Resolve(),
		"Transform graph could not atomically apply an attachment profile"))
	{
		return false;
	}
	Vans::VansLocalTransform resolvedProfileLocal;
	const auto& profiledWorld =
		Vans::VansTransformStore::Read(profiledAttachment).m_Position;
	if (!Expect(graph.TryGetLocalTransform(profiledAttachment, resolvedProfileLocal),
		"Transform graph did not expose the authored profile Local Transform")
		|| !ExpectNear(glm::length(resolvedProfileLocal.position - profileLocal.position),
			0.0f, 1.0e-5f, "Attachment profile local position changed")
		|| !ExpectNear(std::abs(glm::dot(
			resolvedProfileLocal.rotation, profileLocal.rotation)),
			1.0f, 1.0e-5f, "Attachment profile local rotation changed")
		|| !ExpectNear(glm::length(resolvedProfileLocal.scale - profileLocal.scale),
			0.0f, 1.0e-5f, "Attachment profile local scale changed")
		|| !ExpectNear(profiledWorld.x, 27.0f, 1.0e-4f,
			"Attachment profile world X is incorrect")
		|| !ExpectNear(profiledWorld.y, 11.0f, 1.0e-4f,
			"Attachment profile world Y is incorrect")
		|| !ExpectNear(profiledWorld.z, 4.0f, 1.0e-4f,
			"Attachment profile world Z is incorrect"))
	{
		return false;
	}
	return Expect(graph.ClearParent(snappedAttachment,
		Vans::VansTransformReparentMode::Snap),
		"Transform graph could not clear a snapped parent")
		&& ExpectNear(Vans::VansTransformStore::Read(
			snappedAttachment).m_Position.x, 0.0f, 0.0001f,
			"Snap detach did not reset world X")
		&& ExpectNear(Vans::VansTransformStore::Read(
			snappedAttachment).m_Position.y, 0.0f, 0.0001f,
			"Snap detach did not reset world Y");
}

bool TestRuntimeWorldComponentEnabledContract()
{
	Vans::VansRuntimeWorld world;
	Vans::VansEntityHandle entity = world.CreateEntity({ "entity-guid", "Entity" });
	auto* storage = world.RegisterStorage<RuntimeWorldTestComponent>(101);
	if (!Expect(storage != nullptr, "Runtime world failed to register enabled-state test storage"))
		return false;
	Vans::VansComponentHandle component =
		storage->Add(entity, RuntimeWorldTestComponent{ 7 }, "component-guid", false, true);

	if (!Expect(storage->Contains(component), "Runtime component storage did not retain component handle"))
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
		storage->Add(entity, RuntimeWorldTestComponent{ 8 }, "component-guid-survivor", true, true);
	if (!Expect(world.RemoveComponent(component), "Runtime world failed to remove component"))
		return false;
	if (!Expect(!storage->Contains(removed),
		"Runtime component storage allowed stale handle after remove"))
		return false;
	if (!Expect(world.FindComponentByGuid("component-guid").IsValid() == false,
		"Runtime component guid index retained removed component"))
		return false;
	if (!Expect(world.FindComponentByGuid("component-guid-survivor") == survivor,
		"Runtime component guid index did not update moved component after remove"))
		return false;

	Vans::VansComponentHandle added =
		storage->Add(entity, RuntimeWorldTestComponent{ 9 }, "component-guid-2", true, true);
	return Expect(added.index == removed.index && added.generation != removed.generation,
		"Runtime component handle generation did not advance after slot reuse");
}

bool TestRuntimeWorldComponentLifetimeContract()
{
	// 以未索引的 header 顺序作行为参照，覆盖任意位置删除及 slot/generation 复用。
	{
		Vans::VansComponentStorage<int> storage(100);
		std::vector<Vans::VansComponentHandle> handles;
		for (std::uint32_t i = 0; i < 257; ++i)
			handles.push_back(storage.Add({ i % 13, 1 + i % 3 }, static_cast<int>(i)));
		const auto verifyOwners = [&]()
		{
			for (std::uint32_t owner = 0; owner < 14; ++owner)
				for (std::uint32_t generation = 1; generation < 5; ++generation)
				{
					const Vans::VansEntityHandle entity{ owner, generation };
					std::vector<Vans::VansComponentHandle> expected, actual;
					for (const auto& header : storage.Headers())
						if (header.owner == entity) expected.push_back(header.self);
					storage.CollectOwnedBy(entity, actual);
					if (actual != expected || storage.FindFirstOwnedBy(entity) !=
						(expected.empty() ? Vans::VansComponentHandle{} : expected.front()))
						return false;
				}
			return true;
		};
		for (std::uint32_t i = 0; i < 180; ++i)
		{
			const auto removed = handles[(i * 37) % handles.size()];
			storage.Remove(removed);
			if (!Expect(!storage.Contains(removed), "Removed component handle remained live")) return false;
			storage.Add({ i % 13, 2 + i % 3 }, static_cast<int>(i));
			if (!Expect(verifyOwners(), "Owner index changed dense order or leaked another generation")) return false;
		}
		storage.RemoveOwnedBy({ 5, 2 });
		if (!Expect(verifyOwners() && !storage.FindFirstOwnedBy({ 5, 2 }).IsValid(),
			"Owner removal left stale components after dense compaction")) return false;
	}
	{
		Vans::VansRuntimeWorld mismatchWorld;
		const Vans::VansEntityHandle entity =
			mismatchWorld.CreateEntity({ "mismatch-entity-guid", "Mismatch" });
		const Vans::VansComponentHandle mismatched = mismatchWorld.AddComponent(
			entity,
			Vans::VansRuntimeComponentType_Render,
			Vans::VansRuntimeAudioComponent{},
			"mismatched-component-guid");
		if (!Expect(!mismatched.IsValid() &&
			mismatchWorld.FindStorage<Vans::VansRuntimeRenderComponent>(
				Vans::VansRuntimeComponentType_Render) == nullptr,
			"Runtime world accepted a C++ payload under the wrong known component type id"))
			return false;
	}
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
	childAudioComponent.assetGuid = "child-audio";
	Vans::VansComponentHandle childComponent = world.AddComponent(
		child,
		Vans::VansRuntimeComponentType_Audio,
		childAudioComponent,
		"child-audio-guid",
		true);

	const auto* renderStorage =
		world.FindStorage<Vans::VansRuntimeRenderComponent>(Vans::VansRuntimeComponentType_Render);
	const auto* audioStorage =
		world.FindStorage<Vans::VansRuntimeAudioComponent>(Vans::VansRuntimeComponentType_Audio);
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

bool TestRuntimeWorldClearInvalidatesHandlesContract()
{
	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle oldEntity =
		world.CreateEntity({ "old-entity-guid", "OldEntity" });
	const Vans::VansComponentHandle oldComponent = world.AddComponent(
		oldEntity, 101, RuntimeWorldTestComponent{ 1 }, "old-component-guid");
	auto* storage = world.FindStorage<RuntimeWorldTestComponent>(101);
	if (!Expect(storage && storage->Contains(oldComponent),
		"Runtime world clear precondition did not publish the component")) return false;

	world.Clear();
	if (!Expect(!world.IsAlive(oldEntity) && !storage->Contains(oldComponent) &&
		!world.FindComponentByGuid("old-component-guid").IsValid(),
		"Runtime world clear retained a live entity or component handle")) return false;

	const Vans::VansEntityHandle newEntity =
		world.CreateEntity({ "new-entity-guid", "NewEntity" });
	const Vans::VansComponentHandle newComponent = world.AddComponent(
		newEntity, 101, RuntimeWorldTestComponent{ 2 }, "new-component-guid");
	return Expect(newEntity.index == oldEntity.index &&
		newEntity.generation != oldEntity.generation &&
		newComponent.index == oldComponent.index &&
		newComponent.generation != oldComponent.generation &&
		!world.IsAlive(oldEntity) && !storage->Contains(oldComponent),
		"Runtime world clear allowed a stale handle to alias the next scene");
}

bool TestRuntimeComponentKeyCanonicalizationContract()
{
	const auto& descriptors = Vans::VansComponentTypeCatalog::All();
	if (!Expect(descriptors.size() == 27,
		"Component type catalog did not retain all registered component identities"))
		return false;
	for (std::size_t i = 0; i < descriptors.size(); ++i)
	{
		if (!Expect(!descriptors[i].authoringType.empty() && !descriptors[i].runtimeKey.empty(),
			"Component type catalog contains an empty authoring type or runtime key"))
			return false;
		for (std::size_t j = i + 1; j < descriptors.size(); ++j)
		{
			if (!Expect(descriptors[i].authoringType != descriptors[j].authoringType,
				"Component type catalog contains duplicate authoring identities"))
				return false;
		}
	}

	static constexpr std::array<std::string_view, 20> expectedInspectorOrder{
		"ModelRenderer", "LODGroup", "Physics", "Camera", "Animation",
		"CharacterController", "DirectionalLight", "PointLight", "SpotLight", "RectLight",
		"Audio", "AudioVolume", "AudioReverbZone", "LocalVolumetricFog", "Video", "Particle",
		"Cloth", "Vehicle", "ActionHost", "Script"
	};
	std::vector<std::string_view> inspectorTypes;
	std::size_t singletonCount = 0;
	std::size_t defaultFactoryCount = 0;
	for (const Vans::VansComponentTypeDescriptor& descriptor : descriptors)
	{
		if (descriptor.defaultDataFactory)
			++defaultFactoryCount;
		if (!descriptor.inspectorAddable)
			continue;
		if (!Expect(descriptor.defaultDataFactory != nullptr,
			"Inspector-addable component has no catalog default-data factory"))
			return false;
		if (!Expect(Vans::VansComponentTypeCatalog::CreateDefaultData(descriptor.authoringType).kind ==
			Vans::VansSerializedValue::Kind::Object,
			"Catalog default-data factory did not return an object"))
			return false;
		inspectorTypes.push_back(descriptor.authoringType);
		if (descriptor.singleton)
			++singletonCount;
	}
	if (!Expect(std::equal(
		inspectorTypes.begin(), inspectorTypes.end(),
		expectedInspectorOrder.begin(), expectedInspectorOrder.end()),
		"Component type catalog changed the Inspector Add Component order"))
		return false;
	if (!Expect(singletonCount == 4,
		"Component type catalog did not retain the four Inspector singleton constraints"))
		return false;
	if (!Expect(defaultFactoryCount == expectedInspectorOrder.size(),
		"Component type catalog does not own exactly the 20 Inspector default-data factories"))
		return false;

	std::size_t componentAssetReferenceRuleCount = 0;
	for (const Vans::VansComponentTypeDescriptor& descriptor : descriptors)
		componentAssetReferenceRuleCount += descriptor.assetReferenceRuleCount;
	if (!Expect(componentAssetReferenceRuleCount == 24,
		"Component type catalog does not own exactly the 24 component asset-reference rules"))
		return false;
	const auto pointIes = Vans::VansEditorPropertyDescriptorRegistry::Resolve(
		"PointLight", "data", "ies_profile_guid");
	const auto materialOverride = Vans::VansEditorPropertyDescriptorRegistry::Resolve(
		"ModelRenderer", "materialOverrides", "0");
	const auto actionReference = Vans::VansEditorPropertyDescriptorRegistry::Resolve(
		"ActionHost", "entries", "action");
	const auto globalShader = Vans::VansEditorPropertyDescriptorRegistry::Resolve(
		"Material", "data", "shader");
	const auto historicalDeadCustomTextures = Vans::VansEditorPropertyDescriptorRegistry::Resolve(
		"Material", "customTextures", "albedo");
	const auto unknownReference = Vans::VansEditorPropertyDescriptorRegistry::Resolve(
		"Unknown", "data", "source");
	if (!Expect(pointIes.IsDeclared() && pointIes.objectReferenceSlot.expectedAssetType ==
		Vans::EditorAPI::AssetType::IESProfile && pointIes.objectReferenceSlot.storagePolicy ==
		Vans::ObjectReferenceStoragePolicy::GuidString &&
		materialOverride.IsDeclared() && materialOverride.objectReferenceSlot.expectedAssetType ==
		Vans::EditorAPI::AssetType::Material &&
		actionReference.IsDeclared() && actionReference.objectReferenceSlot.expectedAssetType ==
		Vans::EditorAPI::AssetType::ActionDefinition &&
		globalShader.IsDeclared() && globalShader.objectReferenceSlot.expectedAssetType ==
		Vans::EditorAPI::AssetType::Shader &&
		!historicalDeadCustomTextures.IsObjectReference() && !unknownReference.IsObjectReference(),
		"Component or global asset-reference metadata changed its declared slot behavior"))
		return false;

	const Vans::VansSerializedValue modelDefaults =
		Vans::VansComponentTypeCatalog::CreateDefaultData("ModelRenderer");
	const Vans::VansSerializedValue* modelReference =
		Vans::FindObjectField(modelDefaults, "model");
	if (!Expect(modelReference &&
		Vans::ReadSerializedStringField(*modelReference, "guid").empty() &&
		Vans::ReadSerializedStringField(modelDefaults, "rayTracingMode") == "auto" &&
		Vans::ReadSerializedBoolField(modelDefaults, "castShadows", false),
		"ModelRenderer catalog defaults changed"))
		return false;

	const Vans::VansSerializedValue pointDefaults =
		Vans::VansComponentTypeCatalog::CreateDefaultData("PointLight");
	const Vans::VansSerializedValue spotDefaults =
		Vans::VansComponentTypeCatalog::CreateDefaultData("SpotLight");
	const Vans::VansSerializedValue rectDefaults =
		Vans::VansComponentTypeCatalog::CreateDefaultData("RectLight");
	if (!Expect(Vans::ReadSerializedStringField(pointDefaults, "shadowUpdateMode") == "EveryFrame" &&
		Vans::ReadSerializedStringField(spotDefaults, "shadowUpdateMode") == "OnChange" &&
		!Vans::ReadSerializedBoolField(rectDefaults, "castShadows", true),
		"Light catalog defaults changed their per-type shadow behavior"))
		return false;

	const Vans::VansSerializedValue actionHostDefaults =
		Vans::VansComponentTypeCatalog::CreateDefaultData("ActionHost");
	const Vans::VansSerializedValue unknownDefaults =
		Vans::VansComponentTypeCatalog::CreateDefaultData("UnknownComponent");
	if (!Expect(actionHostDefaults.kind == Vans::VansSerializedValue::Kind::Object &&
		!actionHostDefaults.objectFields.empty() &&
		unknownDefaults.kind == Vans::VansSerializedValue::Kind::Object &&
		unknownDefaults.objectFields.empty(),
		"Catalog default-data fallback or ActionHost domain factory changed"))
		return false;
	if (!Expect(Vans::VansComponentTypeCatalog::IsSceneAuthoringType("GameplayActionHost") &&
		!Vans::VansComponentTypeCatalog::IsSceneAuthoringType("gameplayactionhost") &&
		!Vans::VansComponentTypeCatalog::IsSceneAuthoringType("Ragdoll") &&
		!Vans::VansComponentTypeCatalog::IsSceneAuthoringType("render"),
		"Component type catalog did not preserve the strict Scene authoring whitelist"))
		return false;
	if (!Expect(Vans::VansComponentTypeCatalog::HasTrait(
		"DirectionalLight", Vans::VansComponentTypeTrait::Light) &&
		Vans::VansComponentTypeCatalog::HasTrait(
			"Audio", Vans::VansComponentTypeTrait::CameraMedia) &&
		Vans::VansComponentTypeCatalog::HasTrait(
			"Particle", Vans::VansComponentTypeTrait::Particle) &&
		Vans::VansComponentTypeCatalog::HasTrait(
			"MultiMeshRoot", Vans::VansComponentTypeTrait::MultiMeshRoot) &&
		Vans::VansComponentTypeCatalog::HasTrait(
			"Animation", Vans::VansComponentTypeTrait::Animation),
		"Component type catalog did not retain Scene projection classification traits"))
		return false;
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
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("NavigationAgent") == "navigation_agent",
		"NavigationAgent did not canonicalize to navigation agent runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("AIAgent") == "ai_agent",
		"AIAgent did not canonicalize to AI agent runtime component key"))
		return false;
	if (!Expect(Vans::CanonicalRuntimeComponentKeyForName("MultiMeshRoot") == "multimeshroot" &&
		Vans::CanonicalRuntimeComponentKeyForName("LocalVolumetricFog") == "localvolumetricfog",
		"Catalog changed an existing non-runtime authoring component metadata key"))
		return false;
	if (!Expect(Vans::VansRuntimeComponentTypeIdForKey("transform") == Vans::VansRuntimeComponentType_Transform,
		"Transform runtime component key did not resolve to transform type id"))
		return false;
	if (!Expect(Vans::VansRuntimeComponentTypeIdForKey("navigation_agent") ==
		Vans::VansRuntimeComponentType_NavigationAgent,
		"Navigation agent runtime component key did not resolve to navigation agent type id"))
		return false;
	if (!Expect(Vans::VansRuntimeComponentTypeIdForKey("multimeshroot") == Vans::VansInvalidComponentTypeId &&
		Vans::VansRuntimeComponentTypeIdForKey("localvolumetricfog") == Vans::VansInvalidComponentTypeId &&
		Vans::VansRuntimeComponentTypeIdForKey("RENDER") == Vans::VansInvalidComponentTypeId,
		"Runtime type-id lookup widened an intentionally unsupported or case-sensitive key"))
		return false;
	return Expect(Vans::VansRuntimeComponentTypeIdForKey("ai_agent") == Vans::VansRuntimeComponentType_AIAgent,
		"AI agent runtime component key did not resolve to AI agent type id");
}

bool TestRuntimeWorldCommandBufferContract()
{
	Vans::VansRuntimeWorld world;
	world.Commands().CreateEntity({ "queued-guid", "Queued" });

	const Vans::VansRuntimeCommandCommitResult createCommit =
		world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansEntityHandle queued = world.Entities().FindByGuid("queued-guid");
	if (!Expect(world.IsAlive(queued) &&
		createCommit.consumedCommandCount == 1 &&
		createCommit.hierarchyActivityChanged,
		"Runtime world typed commit did not publish exactly one create command"))
		return false;
	const Vans::VansRuntimeCommandCommitResult emptyCommit =
		world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	if (!Expect(emptyCommit.consumedCommandCount == 0 &&
		!emptyCommit.hierarchyActivityChanged,
		"Runtime world empty typed commit reported mutation"))
		return false;

	Vans::VansEntityHandle parent = world.CreateEntity({ "queued-parent-guid", "QueuedParent" });
	world.Commands().SetParent(queued, parent);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	const Vans::VansEntityRecord* parentRecord = world.Entities().Get(parent);
	const Vans::VansEntityRecord* childRecord = world.Entities().Get(queued);
	if (!Expect(parentRecord &&
		std::find(parentRecord->children.begin(), parentRecord->children.end(), queued) != parentRecord->children.end() &&
		childRecord &&
		childRecord->parent == parent,
		"Runtime world command buffer did not flush parent command"))
		return false;

	world.Commands().SetEntityActive(queued, false);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	if (!Expect(!world.Entities().IsHierarchyActive(queued),
		"Runtime world command buffer did not flush active command"))
		return false;

	if (!Expect(world.CollectComponentsOwnedBy(queued).empty(),
		"Runtime world collect owned components returned entries before component add"))
		return false;

	world.Commands().SetEntityName(queued, "QueuedRenamed");
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	if (!Expect(world.Entities().Get(queued) &&
		world.Entities().Get(queued)->name == "QueuedRenamed",
		"Runtime world command buffer did not flush entity name command"))
		return false;

	world.Commands().AddTransformComponent(
		queued,
		"queued-transform-guid",
		42,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle component = world.FindComponentByGuid(
		"queued-transform-guid",
		Vans::VansRuntimeComponentType_Transform);
	auto* storage = world.FindStorage<Vans::VansRuntimeTransformComponent>(
		Vans::VansRuntimeComponentType_Transform);
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	if (!Expect(!world.IsComponentSelfEnabled(component),
		"Runtime world command buffer did not flush component enabled command"))
		return false;

	world.Commands().RemoveComponent(component);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle renderComponent = world.FindComponentByGuid(
		"queued-render-guid",
		Vans::VansRuntimeComponentType_Render);
	auto* renderStorage = world.FindStorage<Vans::VansRuntimeRenderComponent>(
		Vans::VansRuntimeComponentType_Render);
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle physicsComponent = world.FindComponentByGuid(
		"queued-physics-guid",
		Vans::VansRuntimeComponentType_Physics);
	auto* physicsStorage = world.FindStorage<Vans::VansRuntimePhysicsComponent>(
		Vans::VansRuntimeComponentType_Physics);
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
		"0467883c-ddb2-4064-9de1-ba6fae26f90f",
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle clothComponent = world.FindComponentByGuid(
		"queued-cloth-guid",
		Vans::VansRuntimeComponentType_Cloth);
	auto* clothStorage = world.FindStorage<Vans::VansRuntimeClothComponent>(
		Vans::VansRuntimeComponentType_Cloth);
	const Vans::VansRuntimeClothComponent* clothComponentData =
		clothStorage ? clothStorage->Get(clothComponent) : nullptr;
	if (!Expect(
		clothComponentData &&
			clothComponentData->clothNode == clothNode &&
			clothComponentData->profileAssetGuid == "0467883c-ddb2-4064-9de1-ba6fae26f90f",
		"Runtime world command buffer did not store cloth component data"))
		return false;

	auto* controllerNode =
		reinterpret_cast<VansEngine::VansCharacterControllerNode*>(static_cast<std::uintptr_t>(0x3579));
	world.Commands().AddCharacterControllerComponent(
		queued,
		"queued-controller-guid",
		controllerNode,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle controllerComponent = world.FindComponentByGuid(
		"queued-controller-guid",
		Vans::VansRuntimeComponentType_CharacterController);
	auto* controllerStorage = world.FindStorage<Vans::VansRuntimeCharacterControllerComponent>(
		Vans::VansRuntimeComponentType_CharacterController);
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle vehicleComponent = world.FindComponentByGuid(
		"queued-vehicle-guid",
		Vans::VansRuntimeComponentType_Vehicle);
	auto* vehicleStorage = world.FindStorage<Vans::VansRuntimeVehicleComponent>(
		Vans::VansRuntimeComponentType_Vehicle);
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
		7,
		3,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle animationComponent = world.FindComponentByGuid(
		"queued-animation-guid",
		Vans::VansRuntimeComponentType_Animation);
	auto* animationStorage = world.FindStorage<Vans::VansRuntimeAnimationComponent>(
		Vans::VansRuntimeComponentType_Animation);
	const Vans::VansRuntimeAnimationComponent* animationComponentData =
		animationStorage ? animationStorage->Get(animationComponent) : nullptr;
	if (!Expect(animationComponentData && animationComponentData->animationNode == animationNode
		&& animationComponentData->skeletonInstanceId == 7
		&& animationComponentData->skeletonInstanceGeneration == 3,
		"Runtime world command buffer did not store animation component data"))
		return false;

	world.Commands().AddRagdollComponent(
		queued,
		"queued-ragdoll-guid",
		animationNode,
		2,
		"8f8f1201-1ed8-4f3e-8cc6-641c5b1d0001",
		"Hero",
		12,
		11,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle ragdollComponent = world.FindComponentByGuid(
		"queued-ragdoll-guid",
		Vans::VansRuntimeComponentType_Ragdoll);
	auto* ragdollStorage = world.FindStorage<Vans::VansRuntimeRagdollComponent>(
		Vans::VansRuntimeComponentType_Ragdoll);
	const Vans::VansRuntimeRagdollComponent* ragdollComponentData =
		ragdollStorage ? ragdollStorage->Get(ragdollComponent) : nullptr;
	if (!Expect(
		ragdollComponentData &&
			ragdollComponentData->animationNode == animationNode &&
			ragdollComponentData->initialDriveMode == 2 &&
			ragdollComponentData->profileAssetGuid == "8f8f1201-1ed8-4f3e-8cc6-641c5b1d0001" &&
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle audioComponent = world.FindComponentByGuid(
		"queued-audio-guid",
		Vans::VansRuntimeComponentType_Audio);
	auto* audioStorage = world.FindStorage<Vans::VansRuntimeAudioComponent>(
		Vans::VansRuntimeComponentType_Audio);
	const Vans::VansRuntimeAudioComponent* audioComponentData =
		audioStorage ? audioStorage->Get(audioComponent) : nullptr;
	if (!Expect(
			audioComponentData &&
			audioComponentData->audioNode == audioNode &&
			audioComponentData->sourceBinding == audioBinding &&
			audioComponentData->assetGuid == "ambience" &&
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
		Vans::VansRuntimeAudioEnvironmentKind::ReverbZone,
		"queued-reverb-guid",
		reverbZone,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle reverbComponent = world.FindComponentByGuid(
		"queued-reverb-guid",
		Vans::VansRuntimeComponentType_AudioReverbZone);
	auto* reverbStorage = world.FindStorage<Vans::VansRuntimeAudioReverbZoneComponent>(
		Vans::VansRuntimeComponentType_AudioReverbZone);
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
		"video-asset-guid",
		videoTexture,
		videoManager,
		7,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle videoComponent = world.FindComponentByGuid(
		"queued-video-guid",
		Vans::VansRuntimeComponentType_Video);
	auto* videoStorage = world.FindStorage<Vans::VansRuntimeVideoComponent>(
		Vans::VansRuntimeComponentType_Video);
	const Vans::VansRuntimeVideoComponent* videoComponentData =
		videoStorage ? videoStorage->Get(videoComponent) : nullptr;
	if (!Expect(
		videoComponentData &&
			videoComponentData->assetGuid == "video-asset-guid" &&
			videoComponentData->videoTexture == videoTexture &&
			videoComponentData->videoManager == videoManager &&
			videoComponentData->bindlessFirstSlot == 7,
		"Runtime world command buffer did not store video component data"))
		return false;

	const Vans::VansGenerationHandle particleRuntime{7, 9};
	world.Commands().AddParticleComponent(
		queued,
		"queued-particle-guid",
		"particle-asset-guid",
		particleRuntime,
		true,
		true,
		1.5f,
		2.5f,
		3.5f,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle particleComponent = world.FindComponentByGuid(
		"queued-particle-guid",
		Vans::VansRuntimeComponentType_Particle);
	auto* particleStorage = world.FindStorage<Vans::VansRuntimeParticleComponent>(
		Vans::VansRuntimeComponentType_Particle);
	const Vans::VansRuntimeParticleComponent* particleComponentData =
		particleStorage ? particleStorage->Get(particleComponent) : nullptr;
	if (!Expect(
		particleComponentData &&
			particleComponentData->assetGuid == "particle-asset-guid" &&
			particleComponentData->instance == particleRuntime &&
			particleComponentData->playOnAwake &&
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle cameraComponent = world.FindComponentByGuid(
		"queued-camera-guid",
		Vans::VansRuntimeComponentType_Camera);
	auto* cameraStorage = world.FindStorage<Vans::VansRuntimeCameraComponent>(
		Vans::VansRuntimeComponentType_Camera);
	const Vans::VansRuntimeCameraComponent* cameraComponentData =
		cameraStorage ? cameraStorage->Get(cameraComponent) : nullptr;
	if (!Expect(cameraComponentData && cameraComponentData->camera == camera,
		"Runtime world command buffer did not store camera component data"))
		return false;

	auto* lightManager =
		reinterpret_cast<VansGraphics::VansLightManager*>(static_cast<std::uintptr_t>(0x89AB));
	world.Commands().AddLightComponent(
		queued,
		"queued-point-light-guid",
		lightManager,
		3,
		Vans::VansRuntimeLightKind::Point,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle lightComponent = world.FindComponentByGuid(
		"queued-point-light-guid",
		Vans::VansRuntimeComponentType_PointLight);
	auto* lightStorage = world.FindStorage<Vans::VansRuntimeLightComponent>(
		Vans::VansRuntimeComponentType_PointLight);
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
	uiComponentData.autoOpenScreenAssetGuids = { "MainMenu", "Hud" };
	uiComponentData.preloadScreenAssetGuids = { "Inventory" };
	uiComponentData.openScreens = { 10, 11 };
	world.Commands().AddUIComponent(
		queued,
		"queued-ui-guid",
		uiComponentData,
		true);
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle uiComponent = world.FindComponentByGuid(
		"queued-ui-guid",
		Vans::VansRuntimeComponentType_UI);
	auto* uiStorage = world.FindStorage<Vans::VansRuntimeUIComponent>(
		Vans::VansRuntimeComponentType_UI);
	const Vans::VansRuntimeUIComponent* storedUIComponent =
		uiStorage ? uiStorage->Get(uiComponent) : nullptr;
	if (!Expect(
		storedUIComponent &&
			storedUIComponent->autoOpenScreenAssetGuids.size() == 2 &&
			storedUIComponent->autoOpenScreenAssetGuids[0] == "MainMenu" &&
			storedUIComponent->preloadScreenAssetGuids.size() == 1 &&
			storedUIComponent->preloadScreenAssetGuids[0] == "Inventory" &&
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	Vans::VansComponentHandle componentOwnedByDestroyedEntity = world.FindComponentByGuid(
		"queued-script-guid",
		Vans::VansRuntimeComponentType_Script);
	auto* scriptStorage = world.FindStorage<Vans::VansRuntimeScriptComponent>(
		Vans::VansRuntimeComponentType_Script);
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
	world.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
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
    VansGraphics::VansParticleManager manager;
    VansScriptParticleComponent component;
    component.m_Manager = &manager;
    component.m_Instance = manager.Create(std::make_shared<VansGraphics::VansParticleAsset>());
    component.Play(); manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    component.MirrorRuntimeEnabledState(true, false);
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    if (!Expect(component.IsPlaying() && !component.GetRuntime()->IsEffectivelyEnabled(),
        "Effective enablement overwrote explicit playback state")) return false;
    component.MirrorRuntimeEnabledState(true, true);
    manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    component.Pause(); manager.TickMainThread(0); manager.WaitForUpdateAndSwap();
    return Expect(!component.IsPlaying() && component.GetRuntime()->IsEffectivelyEnabled(),
        "Explicit pause overwrote component enablement");
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
	const std::uint32_t transformID = Vans::VansTransformStore::Allocate();
	const std::uint32_t generation = Vans::VansTransformStore::GetGeneration(transformID);

	auto* object = new VansScriptObject();
	object->m_TransformID = transformID;
	object->m_OwnsTransform = true;

	if (!Expect(object->ReleaseOwnedTransform() == transformID,
		"Script object did not release its owned transform id"))
	{
		delete object;
		Vans::VansTransformStore::Release(transformID);
		return false;
	}

	delete object;
	if (!Expect(Vans::VansTransformStore::IsAllocated(transformID) &&
		Vans::VansTransformStore::GetGeneration(transformID) == generation,
		"Script object destructor freed a transform after ownership release"))
	{
		Vans::VansTransformStore::Release(transformID);
		return false;
	}

	Vans::VansTransformStore::Release(transformID);
	const std::uint32_t releasedGeneration = Vans::VansTransformStore::GetGeneration(transformID);
	Vans::VansTransformStore::Release(transformID);
	return Expect(releasedGeneration != generation &&
		!Vans::VansTransformStore::IsAllocated(transformID) &&
		Vans::VansTransformStore::GetGeneration(transformID) == releasedGeneration,
		"Released transform was not recycled exactly once by the caller");
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

VansGraphics::VansAnimationClip BuildContractTurnClip(const std::string& name,
                                                      float rootYawDegrees)
{
    VansGraphics::VansAnimationClip clip = BuildContractClip(name, 0.0f, 0.0f);
    clip.boneKeyframes[0][1].rotation = glm::angleAxis(
        glm::radians(rootYawDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    return clip;
}

bool TestCharacterTrajectoryGeneratorContract()
{
	const glm::vec3 rootMotionStartPosition(12.0f, 0.0f, -7.0f);
	constexpr float rootMotionStartYaw = 90.0f;
	const Vans::VansRootMotionOwnerDelta rootMotionDelta =
		Vans::ResolveAnimationRootMotionOwnerDelta(
			glm::vec3(0.0f, -100.0f, 0.0f),
			glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			rootMotionStartYaw,
			0.01f);
	const glm::vec3 resolvedRootMotionPosition =
		rootMotionStartPosition + rootMotionDelta.translationWorld;
	const float resolvedRootMotionYaw = rootMotionStartYaw + rootMotionDelta.yawDegrees;
	if (!Expect(glm::length(resolvedRootMotionPosition - glm::vec3(13.0f, 0.0f, -7.0f)) < 0.0001f
		&& std::abs(resolvedRootMotionYaw - 120.0f) < 0.0001f,
		"Root Motion did not preserve and increment the current CCT Transform basis"))
		return false;

	Vans::VansCharacterMotionSettings settings;
	Vans::VansCharacterTrajectoryGenerator generator;
	Vans::VansCharacterMotionIntent intent;
	intent.moveInputLocal = glm::vec2(0.0f, 1.0f);
	intent.desiredSpeed = 4.0f;
	intent.movementReferenceYaw = 0.0f;
	intent.desiredFacingYaw = 0.0f;
	intent.hasFacing = true;
	intent.valid = true;

	constexpr float dt = 1.0f / 60.0f;
	glm::vec3 position(0.0f);
	generator.Reset(position, 0.0f);
	for (int frame = 0; frame < 45; ++frame)
	{
		generator.Update(dt, intent, settings, position, 0.0f);
		const glm::vec3 velocity = generator.GetPlannedVelocityWorld();
		position += velocity * dt;
		generator.RecordResolvedMotion(dt, position, velocity, velocity);
	}

	// An authored turn supplies the heading already evaluated from its curve.
	// The generic trajectory owner must apply that heading for this frame and
	// expose the same heading to future samples, without another half-life pass
	// or angular-rate extrapolation.
	intent.desiredFacingYaw = 137.0f;
	intent.immediateFacing = true;
	generator.Update(dt, intent, settings, position, 0.0f);
	if (!Expect(std::abs(generator.GetPlannedFacingYaw() - 137.0f) < 0.0001f,
		"Immediate facing intent was filtered by the trajectory generator"))
		return false;
	for (const auto& sample : generator.GetTrajectory().future)
	{
		if (!Expect(std::abs(std::remainder(sample.facingYaw - 137.0f, 360.0f)) < 0.0001f,
			"Immediate facing intent was extrapolated in future trajectory samples"))
			return false;
	}
	intent.immediateFacing = false;

	// Holding Forward while the camera rotates must bend the world trajectory,
	// but it is not a player-requested change from Forward to another input bucket.
	glm::vec3 previousRelativeFuture =
		generator.GetTrajectory().future.back().positionWorld - position;
	for (int frame = 1; frame <= 20; ++frame)
	{
		intent.movementReferenceYaw = static_cast<float>(frame) * 3.0f;
		intent.desiredFacingYaw = intent.movementReferenceYaw;
		generator.Update(dt, intent, settings, position, intent.desiredFacingYaw);
		const Vans::VansCharacterTrajectory& trajectory = generator.GetTrajectory();
		if (!Expect(trajectory.inputDirectionChangeDegrees <= 0.001f,
			"Camera rotation was reported as a local input direction change"))
			return false;
		const glm::vec3 relativeFuture = trajectory.future.back().positionWorld - position;
		const float futureJump = glm::length(relativeFuture - previousRelativeFuture);
		if (futureJump >= 0.45f)
		{
			std::cerr << "[ForestContractTests] future trajectory jump=" << futureJump
				<< " at frame=" << frame << '\n';
			return Expect(false, "Camera rotation produced a discontinuous future trajectory");
		}
		previousRelativeFuture = relativeFuture;
		const glm::vec3 velocity = generator.GetPlannedVelocityWorld();
		position += velocity * dt;
		generator.RecordResolvedMotion(dt, position, velocity, velocity);
	}

	intent.moveInputLocal = glm::vec2(0.0f, -1.0f);
	generator.Update(dt, intent, settings, position, intent.desiredFacingYaw);
	if (!Expect(generator.GetTrajectory().inputDirectionChangeDegrees >= 179.0f,
		"Forward-to-backward input did not produce an immediate pivot intent"))
		return false;
	if (!Expect(generator.GetTrajectory().hasPredictedPivot &&
		generator.GetTrajectory().predictedPivotTime > 0.0f,
		"Direction reversal did not predict a future velocity zero crossing"))
		return false;

	// A root-motion/collision result feeds the planner gradually. A single
	// anomalous resolved velocity must not replace the complete future plan.
	Vans::VansCharacterTrajectoryGenerator feedbackGenerator;
	position = glm::vec3(0.0f);
	intent.moveInputLocal = glm::vec2(0.0f, 1.0f);
	intent.movementReferenceYaw = 0.0f;
	intent.desiredFacingYaw = 0.0f;
	feedbackGenerator.Reset(position, 0.0f);
	for (int frame = 0; frame < 45; ++frame)
	{
		feedbackGenerator.Update(dt, intent, settings, position, 0.0f);
		const glm::vec3 velocity = feedbackGenerator.GetPlannedVelocityWorld();
		position += velocity * dt;
		feedbackGenerator.RecordResolvedMotion(dt, position, velocity, velocity);
	}
	const glm::vec3 plannedBefore = feedbackGenerator.GetPlannedVelocityWorld();
	feedbackGenerator.RecordResolvedMotion(
		dt, position, glm::vec3(0.0f, 0.0f, -20.0f), plannedBefore);
	feedbackGenerator.Update(dt, intent, settings, position, 0.0f);
	const float maximumExpectedCorrection =
		settings.maxDeceleration * dt + settings.maxAcceleration * dt * 0.5f + 0.01f;
	if (!Expect(glm::length(feedbackGenerator.GetPlannedVelocityWorld() - plannedBefore) <=
		maximumExpectedCorrection,
		"Resolved Root Motion replaced the planned velocity instead of correcting it"))
		return false;

	Vans::VansCharacterMotionSettings hybridSettings;
	hybridSettings.driveMode = Vans::VansLocomotionDriveMode::Hybrid;
	const Vans::VansLocomotionAuthority hybridMotionMatchingAuthority =
		Vans::SelectLocomotionAuthority(hybridSettings, true, true, false);
	if (!Expect(hybridMotionMatchingAuthority.mode ==
		Vans::VansLocomotionAuthorityMode::RootMotion,
		"Hybrid Motion Matching frame retained two locomotion authorities"))
		return false;

	Vans::VansCharacterLocomotionResolver locomotion;
	locomotion.Clear(glm::vec3(0.0f), 0.0f);
	Vans::VansCharacterMotionIntent jumpIntent;
	jumpIntent.jumpRequested = true;
	jumpIntent.jumpSpeed = 5.5f;
	jumpIntent.gravity = 16.0f;
	locomotion.SetIntent(jumpIntent);
	locomotion.Prepare(0.1f, settings, glm::vec3(0.0f), 0.0f, true, false);
	Vans::VansLocomotionAuthority capsuleAuthority;
	const Vans::VansCharacterLocomotionResult jumpFrame = locomotion.Resolve(
		glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false,
		capsuleAuthority, settings, glm::vec3(1.0f), 0.0f);
	if (!Expect(jumpFrame.hasMove && jumpFrame.displacementWorld.y > 0.5f,
		"Locomotion resolver did not produce the prepared jump frame"))
		return false;
	if (!Expect(!locomotion.Resolve(
		glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false,
		capsuleAuthority, settings, glm::vec3(1.0f), 0.0f).hasMove,
		"Locomotion resolver reused a consumed frame"))
		return false;

	locomotion.ResetMotion(glm::vec3(3.0f, 0.0f, 2.0f), 45.0f);
	jumpIntent.jumpRequested = false;
	locomotion.SetIntent(jumpIntent);
	locomotion.Prepare(0.1f, settings, glm::vec3(3.0f, 0.0f, 2.0f), 45.0f, false, false);
	const Vans::VansCharacterLocomotionResult resetFrame = locomotion.Resolve(
		glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false,
		capsuleAuthority, settings, glm::vec3(1.0f), 45.0f);
	if (!Expect(resetFrame.hasMove && resetFrame.displacementWorld.y < 0.0f,
		"Locomotion reset retained the pre-teleport vertical velocity"))
		return false;

	return true;
}

bool TestMotionMatchingRootMotionRigContract()
{
    using namespace VansGraphics;

    Skeleton skeleton;
    skeleton.bones.resize(2);
    skeleton.bones[0].id = 0;
    skeleton.bones[0].name = "SKM_UEFN_Mannequin";
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[0].localTransform = glm::mat4(1.0f);
    skeleton.bones[0].children.push_back(1);
    skeleton.bones[1].id = 1;
    skeleton.bones[1].name = "root";
    skeleton.bones[1].parentIndex = 0;
    skeleton.bones[1].localTransform = glm::mat4(1.0f);
    skeleton.boneNameToIndex["SKM_UEFN_Mannequin"] = 0;
    skeleton.boneNameToIndex["root"] = 1;
    skeleton.BuildTopologicalOrder();

    VansAnimationClip clip;
    clip.clipName = "Walk_F";
    clip.duration = 1.0f;
    clip.boneKeyframes.resize(2);
    clip.boneKeyframes[1] = {
        { 0.0f, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) },
        { 1.0f, glm::vec3(0.0f, -100.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) }
    };

    VansAnimationSampleRequest request;
    request.previousTime = 0.0f;
    request.currentTime = 0.5f;
    request.loop = false;
    VansPosePayload payload;
    if (!Expect(VansAnimationSampler::Sample(clip, skeleton, request, payload),
        "Animation sampler rejected the wrapper-root fixture"))
        return false;
    if (!Expect(!payload.rootMotion.valid,
        "An unanimated skeleton wrapper root was reported as valid root motion"))
        return false;

    request.rootMotionBoneIndex = 1;
    if (!Expect(VansAnimationSampler::Sample(clip, skeleton, request, payload)
        && payload.rootMotion.valid
        && std::fabs(payload.rootMotion.translation.y + 50.0f) <= 0.0001f,
        "Motion matching rig root did not override the unanimated skeleton wrapper root"))
        return false;

    const glm::vec3 animationForward = Vans::EngineLocalToAnimationPlanar(
        glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 engineForward = Vans::AnimationToEngineLocalPlanar(
        glm::vec3(0.0f, -1.0f, 0.0f));
    if (!Expect(
        glm::length(animationForward - glm::vec3(0.0f, -1.0f, 0.0f)) <= 0.0001f &&
        glm::length(engineForward - glm::vec3(0.0f, 0.0f, 1.0f)) <= 0.0001f,
        "Animation/engine planar conversion reversed locomotion forward"))
        return false;

    // A character may carry arbitrary pitch/roll as model-import correction.
    // Locomotion direction is defined only by its explicit yaw, so validate all
    // eight authored buckets at a non-axis-aligned world facing.
    constexpr float facingYaw = 37.0f;
    const glm::quat worldFromLocomotion = glm::angleAxis(
        glm::radians(facingYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    const float diagonal = std::sqrt(0.5f);
    const std::array<glm::vec3, 8> engineLocalDirections{
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(diagonal, 0.0f, diagonal),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(diagonal, 0.0f, -diagonal),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(-diagonal, 0.0f, -diagonal),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(-diagonal, 0.0f, diagonal)
    };
    const std::array<glm::vec3, 8> animationDirections{
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(diagonal, -diagonal, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(diagonal, diagonal, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(-diagonal, diagonal, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(-diagonal, -diagonal, 0.0f)
    };
    for (std::size_t bucket = 0; bucket < engineLocalDirections.size(); ++bucket)
    {
        const glm::vec3 worldDirection = worldFromLocomotion * engineLocalDirections[bucket];
        const glm::vec3 resolved = Vans::WorldToAnimationPlanar(worldDirection, facingYaw);
        if (!Expect(glm::length(resolved - animationDirections[bucket]) <= 0.0001f,
            "Locomotion yaw did not preserve an authored direction bucket"))
            return false;
    }
    return true;
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
	VansPosePayload payload;
	Vans::VansCharacterTrajectory trajectory;
	trajectory.valid = true;
	trajectory.currentFacingYaw = 37.0f;
	trajectory.originWorld = glm::vec3(0.0f);
	for (std::size_t index = 0; index < trajectory.future.size(); ++index)
	{
		trajectory.future[index].time = settings.schema.futureTimes[index];
		trajectory.future[index].facingYaw = trajectory.currentFacingYaw;
	}

	runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload);
    const MotionMatchingDebugData& idleDebug = runtime.GetDebugData();
    if (!Expect(idleDebug.databaseReady, "Motion matching auto database did not build"))
        return false;
    if (!Expect(idleDebug.activeClip == "Idle_Stand", "Idle query did not remain on Idle_Stand"))
        return false;

    parameters["Speed"].floatVal = 0.6f;
    parameters["Direction"].floatVal = 0.0f;
    parameters["MoveState"].intVal = 1;
	const glm::quat worldFromLocomotion = glm::angleAxis(
		glm::radians(trajectory.currentFacingYaw), glm::vec3(0.0f, 1.0f, 0.0f));
	trajectory.currentVelocityWorld = worldFromLocomotion * glm::vec3(0.0f, 0.0f, 0.6f);
	trajectory.desiredVelocityWorld = trajectory.currentVelocityWorld;
	for (std::size_t index = 0; index < trajectory.future.size(); ++index)
	{
		auto& future = trajectory.future[index];
		future.time = settings.schema.futureTimes[index];
		future.positionWorld = trajectory.originWorld +
			trajectory.currentVelocityWorld * future.time;
		future.velocityWorld = trajectory.currentVelocityWorld;
	}
    for (int i = 0; i < 8; ++i)
		runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload);

    const MotionMatchingDebugData& walkDebug = runtime.GetDebugData();
    if (!Expect(walkDebug.usedThisFrame, "Motion matching did not run for walk query"))
        return false;
    const bool selectedWalk =
        walkDebug.activeClip.find("Walk") != std::string::npos ||
        walkDebug.selectedClip.find("Walk") != std::string::npos;
    if (!Expect(selectedWalk, "Walk query did not select a Walk clip from auto-built metadata"))
        return false;
	if (!Expect(walkDebug.switches > 0, "Walk query did not switch away from idle"))
		return false;
	if (!ExpectNear(walkDebug.queryDirection, 0.0f, 0.0001f,
		"Yaw-relative world forward did not produce a forward Motion Matching query"))
		return false;
	if (!Expect(payload.rootMotion.valid,
		"Motion matching did not publish the selected clip root-motion interval"))
		return false;
	if (!Expect(glm::length(payload.rootMotion.translation) > 0.0001f,
		"Motion matching published an empty root-motion interval for moving playback"))
		return false;
	for (int frame = 0; frame < 40; ++frame)
	{
		if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
			return Expect(false, "Motion matching stopped while traversing a locomotion loop");
		if (payload.rootMotion.valid && payload.rootMotion.translation.y > 0.0001f)
			return Expect(false, "Motion matching reversed Root Motion at a loop or clip switch seam");
	}
	runtime.BeginEvaluationFrame();
	return Expect(!runtime.GetDebugData().usedThisFrame,
		"Motion Matching debug usage leaked into a later non-MM Graph evaluation");
}

bool TestTurnInPlaceWarpingMathContract()
{
	using namespace VansGraphics;

	if (!Expect(!TurnInPlaceWarpingSettings{}.enabled,
		"Turn In Place Warping must remain opt-in for existing Motion Matching scenes"))
		return false;

	TurnYawProfile profile;
	profile.sampleTimesSeconds = { 0.0f, 0.5f, 1.0f };
	profile.cumulativeYawDegrees = { 0.0f, 40.0f, 90.0f };
	profile.durationSeconds = 1.0f;
	profile.motionEndTimeSeconds = 1.0f;
	profile.totalYawDegrees = 90.0f;
	profile.directionSign = 1;
	profile.valid = true;
	if (!ExpectNear(profile.SampleCumulativeYaw(0.25f), 20.0f, 0.0001f,
		"Turn Yaw Profile did not interpolate cumulative root yaw"))
		return false;
	if (!ExpectNear(profile.RemainingYaw(0.75f), 25.0f, 0.0001f,
		"Turn Yaw Profile did not report the sampled remaining yaw"))
		return false;

	const glm::quat tiltedTurn = glm::normalize(
		glm::angleAxis(glm::radians(12.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
		glm::angleAxis(glm::radians(35.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
	glm::quat correctedTurn = tiltedTurn;
	const float authoredYaw = ExtractRootMotionYawDegrees(tiltedTurn);
	ApplyRootMotionYawCorrection(18.0f, correctedTurn);
	if (!ExpectNear(
		std::remainder(ExtractRootMotionYawDegrees(correctedTurn) - authoredYaw, 360.0f),
		18.0f, 0.01f,
		"Root Yaw correction did not preserve a stable signed twist under tilt"))
		return false;

	TurnInPlaceWarpingSettings settings;
	settings.enabled = true;
	VansTurnInPlaceWarping warping;
	warping.Configure(settings);
	const TurnInPlaceReachability reachable =
		warping.EvaluateCandidate(73.0f, 90.0f, 0.8f);
	if (!Expect(reachable.reachable
		&& reachable.scaleRatio >= settings.minRootYawScaleRatio
		&& reachable.scaleRatio <= settings.maxRootYawScaleRatio
		&& std::abs(reachable.residualDegrees)
			<= settings.maxAdditiveCorrectionDegrees,
		"Turn endpoint reachability rejected a valid arbitrary angle"))
		return false;
	const TurnInPlaceReachability wrongDirection =
		warping.EvaluateCandidate(-73.0f, 90.0f, 0.8f);
	return Expect(!wrongDirection.reachable
		&& wrongDirection.reason == "WrongDirection",
		"Turn endpoint reachability accepted an opposite-direction candidate");
}

bool TestAnimationEditorPreviewPolicyContract()
{
	using namespace VansGraphics;
	using namespace Vans::EditorAPI;
	{
		// Imported bind offsets need not cancel to identity (e.g. FBX axis conversion).
		Skeleton skeleton;
		skeleton.bones.resize(2);
		skeleton.bones[0].parentIndex = 1;
		skeleton.bones[0].localTransform = glm::translate(glm::mat4(1), glm::vec3(2, 0, 0));
		skeleton.bones[0].offsetMatrix = glm::translate(glm::mat4(1), glm::vec3(-1, 0, 0));
		skeleton.bones[1].children = {0};
		skeleton.bones[1].localTransform = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(0, 0, 1));
		skeleton.bones[1].offsetMatrix = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1, 0, 0));
		skeleton.BuildTopologicalOrder();
		VansAnimationNode attachment("DisabledSkinnedAttachment");
		attachment.SetEnabled(false);
		attachment.SetSkeleton(skeleton);
		const auto& initial = attachment.GetBoneSSBO();
		if (!Expect(glm::length(glm::vec3(initial.boneMatrices[0] * glm::vec4(1, 0, 0, 1)) - glm::vec3(0, 2, 0)) < 0.0001f
			&& glm::length(glm::vec3(initial.boneMatrices[1] * glm::vec4(0, 1, 0, 1)) - glm::vec3(0, 0, 1)) < 0.0001f,
			"A disabled attachment must have its imported bind pose before its first animation tick"))
			return false;
		attachment.SetSkeleton(Skeleton{});
		if (!Expect(attachment.GetBoneSSBO().boneMatrices[0] == glm::mat4(1),
			"Replacing a skeleton must clear stale initial skinning transforms"))
			return false;
	}

	const VansAnimationFrameContext gameplay{
		VansAnimationEvaluationPurpose::Gameplay, 1.0f / 60.0f };
	const VansAnimationFrameContext editorPreview{
		VansAnimationEvaluationPurpose::EditorPreview, 1.0f / 60.0f };
	if (!Expect(gameplay.AllowsOwnerMotion(),
		"Gameplay animation evaluation must retain owner/CCT Root Motion submission"))
		return false;
	if (!Expect(!editorPreview.AllowsOwnerMotion(),
		"Editor animation evaluation must not overwrite the authorable Scene Transform"))
		return false;

	AnimationPreviewCreateRequest sceneRequest;
	sceneRequest.targetKind = AnimationPreviewTargetKind::SceneAnimationComponent;
	sceneRequest.animatorAssetGuid = "00000000-0000-4000-8000-000000000001";
	sceneRequest.entityGuid = "character";
	sceneRequest.animationComponentGuid = "animation";
	if (!Expect(sceneRequest.targetKind ==
			AnimationPreviewTargetKind::SceneAnimationComponent &&
		!sceneRequest.animatorAssetGuid.empty() &&
		!sceneRequest.entityGuid.empty() &&
		!sceneRequest.animationComponentGuid.empty(),
		"Scene animation preview target identity was not represented explicitly"))
		return false;

	AnimationPreviewPlaybackRequest playback;
	playback.seek = true;
	playback.seekSeconds = 0.75f;
	playback.rootMotionMode =
		AnimationPreviewPlaybackRequest::RootMotionMode::TrailOnly;
	if (!ExpectNear(playback.seekSeconds, 0.75f, 0.0001f,
		"Animation preview timeline must use absolute seconds"))
		return false;

	AnimationPreviewSnapshot motionMatchingPreview;
	motionMatchingPreview.seekSupported = false;
	motionMatchingPreview.seekUnavailableReason =
		"Motion Matching requires forward playback";
	if (!Expect(motionMatchingPreview.diagnostic.empty()
		&& !motionMatchingPreview.seekUnavailableReason.empty(),
		"A timeline-seek limitation must not be exposed as a preview error"))
		return false;

	const AssetTypeFilter previewModelFilter{
		AssetType::Model, false, AssetQueryCapability::SkeletalModel };
	return Expect(previewModelFilter.requiredCapability ==
		AssetQueryCapability::SkeletalModel,
		"Animation preview model query must require imported skeletal capability");
}

bool TestAnimationSocketAttachmentAuthoringContract()
{
	using namespace VansGraphics;
	using namespace Vans::EditorAPI;
	const std::string modelGuid = "00000000-0000-4000-8000-000000000013";
	Vans::VansSceneObjectBuildConfig multiMeshRoot;
	multiMeshRoot.multiMeshRoot = Vans::VansSceneMultiMeshRootConfig{ modelGuid };
	Vans::VansSceneObjectBuildConfig renderedObject;
	Vans::VansSceneRenderNodeConfig render;
	render.mesh = modelGuid;
	renderedObject.render = render;
	if (!Expect(multiMeshRoot.ResolveModelAssetGuid() == modelGuid
		&& renderedObject.ResolveModelAssetGuid() == modelGuid,
		"Scene Model identity must cover both MultiMeshRoot and ModelRenderer entities"))
		return false;

	struct RegistryReset
	{
		~RegistryReset() { Vans::VansAssetDocumentRegistry::Get().Clear(); }
	} registryReset;
	TemporaryDirectory temporary;
	const fs::path rigPath = temporary.path / "SocketPreview.vanimrig";
	std::string error;
	VansAnimationRigAsset originalRig;
	originalRig.name = "Socket Preview Rig";
	originalRig.skeletonGuid = "00000000-0000-4000-8000-000000000010";
	if (!Expect(VansAnimationRigStorage::SaveAtomic(rigPath, originalRig, error),
		error.c_str()))
		return false;

	auto rigDocument = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(rigPath);
	if (!Expect(rigDocument && rigDocument->sourceDocument.IsLoaded(),
		"Socket Rig save fixture did not load"))
		return false;

	VansAnimationRigAsset editedRig = originalRig;
	editedRig.name = "Edited Socket Preview Rig";
	VansRigSocketDefinition socket;
	socket.guid = "00000000-0000-4000-8000-000000000011";
	socket.name = "Weapon";
	socket.boneGuid = "00000000-0000-4000-8000-000000000012";
	socket.positionLocal = glm::vec3(0.1f, 0.2f, 0.3f);
	editedRig.sockets.push_back(socket);
	VansRigAttachmentProfileDefinition profile;
	profile.modelGuid = modelGuid;
	profile.parentKind = VansRigAttachmentParentKind::Socket;
	profile.anchorGuid = socket.guid;
	profile.positionLocal = glm::vec3(-0.4f, 0.5f, 0.6f);
	profile.rotationLocal = glm::angleAxis(glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	profile.scaleLocal = glm::vec3(1.0f);
	editedRig.attachmentProfiles.push_back(profile);
	nlohmann::json rigJson;
	if (!Expect(VansAnimationRigStorage::SerializeToJsonObject(
		editedRig, rigJson, error), error.c_str()))
		return false;
	const auto rigEdit = Vans::VansAssetDocumentEditService::ReplaceRoot(
		rigDocument->sourceDocument, Vans::DecodeSerializedValueJson(rigJson));
	if (!Expect(rigEdit && rigDocument->sourceDocument.IsDirty(),
		"Socket Rig fixture did not become dirty"))
		return false;

	const auto save = Vans::VansAnimationRigSaveService::Save(rigDocument);
	VansAnimationRigAsset diskRig;
	if (!Expect(save && save.published
		&& !rigDocument->sourceDocument.IsDirty()
		&& VansAnimationRigStorage::Load(rigPath, diskRig, error)
		&& diskRig.name == editedRig.name
		&& diskRig.sockets.size() == 1
		&& diskRig.sockets.front().guid == socket.guid
		&& diskRig.attachmentProfiles.size() == 1
		&& diskRig.attachmentProfiles.front().modelGuid == profile.modelGuid
		&& diskRig.attachmentProfiles.front().anchorGuid == socket.guid
		&& glm::length(diskRig.attachmentProfiles.front().positionLocal
			- profile.positionLocal) <= 1.0e-6f,
		"Rig-only save did not publish Socket and attachment-profile data"))
		return false;

	Skeleton skeleton;
	skeleton.sourceSkeletonGuid = originalRig.skeletonGuid;
	BoneInfo bone;
	bone.id = 0;
	bone.guid = socket.boneGuid;
	bone.name = "weapon";
	bone.canonicalPath = "weapon";
	bone.parentIndex = -1;
	skeleton.bones.push_back(std::move(bone));
	skeleton.RebuildIdentityMapsAndSignature();
	VansCompiledAnimationRig compiledRig;
	if (!Expect(VansAnimationRigCompiler::Compile(
		diskRig, skeleton, compiledRig, error), error.c_str()))
	{
		return false;
	}
	const VansCompiledRigAttachmentProfile* compiledProfile =
		compiledRig.FindAttachmentProfile(
			modelGuid, VansRigAttachmentParentKind::Socket, socket.guid);
	if (!Expect(compiledProfile
		&& glm::length(compiledProfile->positionLocal - profile.positionLocal) <= 1.0e-6f
		&& std::abs(glm::dot(compiledProfile->rotationLocal,
			glm::normalize(profile.rotationLocal))) >= 0.999999f
		&& glm::length(compiledProfile->scaleLocal - profile.scaleLocal) <= 1.0e-6f,
		"Animation Rig compiler did not publish the attachment profile for runtime lookup"))
	{
		return false;
	}

	const fs::path scenePath=temporary.path / "IKScene.json";
	if(!Expect(Vans::VansSceneFileStorage::CreateEmptySceneDocument(scenePath,error),error.c_str()))return false;
	auto sceneDocument=Vans::VansSceneDocumentLoader::Load(scenePath);
	if(!Expect(bool(sceneDocument),"IK save Scene fixture failed"))return false;
	Vans::VansSceneEditService sceneEdits(*sceneDocument.document);
	if(!Expect(bool(sceneEdits.Set({Vans::DocumentPropertySpace::Scene,"/name"},
		Vans::VansSerializedValue::String("Saved IK Scene"))),"IK Scene edit failed"))return false;
	rigJson["name"]="Saved IK Rig";
	Vans::VansAssetDocumentEditService::ReplaceRoot(rigDocument->sourceDocument,Vans::DecodeSerializedValueJson(rigJson));
	Vans::VansEditorAssetSaveOperations saveOperations;
	saveOperations.refreshProjectAsset = [](const fs::path&, std::string& refreshError)
	{ refreshError.clear(); return true; };
	const auto savedSetup = Vans::VansEditorAssetSaveService::Get().SaveSceneAndAssets(
		saveOperations, *sceneDocument.document, {rigDocument});
	if(!Expect(bool(savedSetup),savedSetup.message.c_str()))return false;
	auto reopened=Vans::VansSceneDocumentLoader::Load(scenePath);
	if(!Expect(bool(reopened) && !sceneDocument.document->IsDirty() && !rigDocument->IsDirty() &&
		Vans::ReadSerializedStringField(reopened.document->SerializedRootSnapshot(),"name")=="Saved IK Scene",
		"Scene and Rig transaction did not survive reopen"))return false;
	const auto originalFingerprint=Vans::VansSceneDocumentLoader::Fingerprint(scenePath);
	sceneEdits.Set({Vans::DocumentPropertySpace::Scene,"/name"},Vans::VansSerializedValue::String("Rejected IK Scene"));
	rigJson["sockets"]="invalid array";
	Vans::VansAssetDocumentEditService::ReplaceRoot(rigDocument->sourceDocument,Vans::DecodeSerializedValueJson(rigJson));
	const auto rejectedSetup = Vans::VansEditorAssetSaveService::Get().SaveSceneAndAssets(
		saveOperations, *sceneDocument.document, {rigDocument});
	if(!Expect(!rejectedSetup &&
		Vans::VansSceneDocumentLoader::Fingerprint(scenePath)==originalFingerprint && sceneDocument.document->IsDirty(),
		"Invalid Rig partially published the Scene"))return false;

	AnimationPreviewAttachmentBindingRequest previewBinding;
	previewBinding.entityGuid = "scene-object";
	previewBinding.parent.kind = RuntimeParentKind::Socket;
	previewBinding.parent.anchorGuid = socket.guid;
	return Expect(!previewBinding.entityGuid.empty()
		&& previewBinding.parent.kind == RuntimeParentKind::Socket
		&& !previewBinding.parent.anchorGuid.empty(),
		"Temporary Scene attachment binding was not represented independently");
}

bool TestUIActionEventContract()
{
	std::vector<std::string> trace;
	bool payloadMatches = false;
	auto saveConnection = Vans::VansEventBus::Get().Subscribe<VansRuntime::VansUIActionEvent>(
		[&](const VansRuntime::VansUIActionEvent& event)
		{
			if (event.name != "UI.Save")
				return;
			const auto value = event.params.find("slot");
			const auto* slot = value != event.params.end()
				? std::get_if<std::int64_t>(&value->second.value) : nullptr;
			payloadMatches = slot && *slot == 7 && event.sourceScreen == 42 &&
				event.sourceElement == "SaveButton";
			trace.push_back(event.name);
		}, Vans::VansEventLane::MainThread);
	auto cancelConnection = Vans::VansEventBus::Get().Subscribe<VansRuntime::VansUIActionEvent>(
		[&](const VansRuntime::VansUIActionEvent& event)
		{
			if (event.name == "UI.Cancel")
				trace.push_back(event.name);
		}, Vans::VansEventLane::MainThread);

	VansRuntime::VansUIVariantMap params;
	params.emplace("slot", VansRuntime::VansUIVariant(std::int64_t{ 7 }));
	Vans::VansEventBus::Get().PublishNow(VansRuntime::VansUIActionEvent{
		"UI.Save", std::move(params), 42, "SaveButton" });
	if (!Expect(payloadMatches && trace == std::vector<std::string>{ "UI.Save" },
		"UI action event lost its string key, owned params, source, or synchronous dispatch"))
		return false;

	saveConnection.Disconnect();
	Vans::VansEventBus::Get().PublishNow(VansRuntime::VansUIActionEvent{
		"UI.Save", {}, 42, "SaveButton" });
	Vans::VansEventBus::Get().PublishNow(VansRuntime::VansUIActionEvent{
		"UI.Cancel", {}, 42, "CancelButton" });
	return Expect(trace == std::vector<std::string>{ "UI.Save", "UI.Cancel" },
		"UI action event name filtering or connection lifetime changed");
}

bool TestLuaUIActionEventContract()
{
	lua_State* state = luaL_newstate();
	if (!Expect(state != nullptr, "Lua UI action contract could not create a Lua state"))
		return false;
	luaL_openlibs(state);
	lua_newtable(state);
	VansRuntime::VansLuaUIBridge::Register(state);
	lua_setglobal(state, "vans");

	constexpr const char* Contract = R"(
		assert(vans.ui.create_component == nil)
		assert(type(vans.ui.load_component) == "function")
		local calls = 0
		local subscription = vans.ui.on_action("UI.Save", function(event)
			assert(event.name == "UI.Save")
			assert(event.params.slot == 7)
			assert(event.source_screen == 0)
			assert(event.source_element == "")
			calls = calls + 1
		end)
		assert(subscription ~= nil)
		vans.ui.dispatch("UI.Other", { slot = 1 })
		vans.ui.dispatch("UI.Save", { slot = 7 })
		assert(calls == 1)
		subscription:unsubscribe()
		vans.ui.dispatch("UI.Save", { slot = 7 })
		assert(calls == 1)

		shutdown_calls = 0
		shutdown_subscription = vans.ui.on_action("UI.Shutdown", function(_)
			shutdown_calls = shutdown_calls + 1
		end)
		assert(shutdown_subscription ~= nil)
	)";
	const bool scriptPassed = luaL_loadstring(state, Contract) == LUA_OK &&
		lua_pcall(state, 0, 0, 0) == LUA_OK;
	const std::string scriptError = scriptPassed || !lua_tostring(state, -1)
		? std::string{} : lua_tostring(state, -1);

	VansRuntime::VansLuaUIBridge::Shutdown(state);
	Vans::VansEventBus::Get().PublishNow(VansRuntime::VansUIActionEvent{
		"UI.Shutdown", {}, 0, {} });
	lua_getglobal(state, "shutdown_calls");
	const lua_Integer shutdownCalls = lua_tointeger(state, -1);
	lua_pop(state, 1);
	lua_close(state);

	if (!Expect(scriptPassed, scriptError.empty()
		? "Lua UI action contract failed" : scriptError.c_str()))
		return false;
	return Expect(shutdownCalls == 0,
		"Lua UI action subscription survived bridge shutdown");
}

bool TestLuaUIValueCodecContract()
{
	lua_State* state = luaL_newstate();
	if (!Expect(state != nullptr, "Lua UI codec contract could not create a Lua state"))
		return false;

	std::string error;
	VansRuntime::VansUIVariant value;
	lua_newtable(state);
	lua_pushstring(state, "second");
	lua_seti(state, -2, 2);
	lua_pushstring(state, "first");
	lua_seti(state, -2, 1);
	const bool arrayDecoded = VansRuntime::VansLuaValueConverter::TryToVariant(
		state, -1, value, error);
	const auto* array = std::get_if<VansRuntime::VansUIVariantArray>(&value.value);
	const bool arrayMatches = arrayDecoded && array && array->size() == 2 &&
		std::get<std::string>((*array)[0].value) == "first" &&
		std::get<std::string>((*array)[1].value) == "second";
	lua_pop(state, 1);
	if (!Expect(arrayMatches,
		"Lua UI codec depended on lua_next iteration order for arrays"))
	{
		lua_close(state);
		return false;
	}

	lua_newtable(state);
	lua_pushvalue(state, -1);
	lua_setfield(state, -2, "self");
	error.clear();
	const bool cycleRejected = !VansRuntime::VansLuaValueConverter::TryToVariant(
		state, -1, value, error) && error.find("cycle") != std::string::npos;
	lua_pop(state, 1);
	if (!Expect(cycleRejected, "Lua UI codec accepted a cyclic table"))
	{
		lua_close(state);
		return false;
	}

	lua_newtable(state);
	lua_pushinteger(state, 1);
	lua_seti(state, -2, 1);
	lua_pushinteger(state, 2);
	lua_setfield(state, -2, "named");
	error.clear();
	const bool mixedRejected = !VansRuntime::VansLuaValueConverter::TryToVariant(
		state, -1, value, error) && error.find("mix") != std::string::npos;
	lua_pop(state, 1);
	if (!Expect(mixedRejected, "Lua UI codec accepted mixed array and map keys"))
	{
		lua_close(state);
		return false;
	}

	VansRuntime::VansUIVariantMap nested;
	nested.emplace("title", VansRuntime::VansUIVariant("Inventory"));
	nested.emplace("items", VansRuntime::VansUIVariant(VansRuntime::VansUIVariantArray{
		VansRuntime::VansUIVariant(std::int64_t{ 3 }),
		VansRuntime::VansUIVariant(std::int64_t{ 5 }) }));
	const int originalTop = lua_gettop(state);
	error.clear();
	const bool pushed = VansRuntime::VansLuaValueConverter::TryPushVariantMap(
		state, nested, error);
	VansRuntime::VansUIVariantMap roundTrip;
	const bool decoded = pushed && VansRuntime::VansLuaValueConverter::TryToVariantMap(
		state, -1, roundTrip, error);
	if (pushed) lua_pop(state, 1);
	const auto title = roundTrip.find("title");
	const auto items = roundTrip.find("items");
	const auto* roundTripItems = items == roundTrip.end()
		? nullptr : std::get_if<VansRuntime::VansUIVariantArray>(&items->second.value);
	const bool roundTripMatches = decoded && lua_gettop(state) == originalTop &&
		title != roundTrip.end() && std::get<std::string>(title->second.value) == "Inventory" &&
		roundTripItems && roundTripItems->size() == 2;
	lua_close(state);
	return Expect(roundTripMatches, "Lua UI value codec round trip changed the value or Lua stack");
}

bool TestLuaUIStateIsolationContract()
{
	const auto createState = []()
	{
		lua_State* state = luaL_newstate();
		if (!state) return state;
		luaL_openlibs(state);
		lua_newtable(state);
		VansRuntime::VansLuaUIBridge::Register(state);
		lua_setglobal(state, "vans");
		return state;
	};
	lua_State* first = createState();
	lua_State* second = createState();
	if (!Expect(first && second, "Lua UI state isolation could not create both states"))
	{
		if (first) lua_close(first);
		if (second) lua_close(second);
		return false;
	}
	constexpr const char* Subscribe = R"(
		calls = 0
		subscription = vans.ui.on_action("UI.MultiState", function(_)
			calls = calls + 1
		end)
	)";
	const bool registered =
		luaL_dostring(first, Subscribe) == LUA_OK &&
		luaL_dostring(second, Subscribe) == LUA_OK;
	if (!Expect(registered, "Lua UI multi-state subscriptions could not be registered"))
	{
		VansRuntime::VansLuaUIBridge::Shutdown(first);
		VansRuntime::VansLuaUIBridge::Shutdown(second);
		lua_close(first);
		lua_close(second);
		return false;
	}

	VansRuntime::VansLuaUIBridge::Shutdown(first);
	Vans::VansEventBus::Get().PublishNow(VansRuntime::VansUIActionEvent{
		"UI.MultiState", {}, 0, {} });
	lua_getglobal(first, "calls");
	const lua_Integer firstCalls = lua_tointeger(first, -1);
	lua_pop(first, 1);
	lua_getglobal(second, "calls");
	const lua_Integer secondCalls = lua_tointeger(second, -1);
	lua_pop(second, 1);
	VansRuntime::VansLuaUIBridge::Shutdown(second);
	lua_close(first);
	lua_close(second);

	const VansRuntime::VansUIHandleId firstHandle = VansRuntime::AllocateUIHandle();
	const VansRuntime::VansUIHandleId secondHandle = VansRuntime::AllocateUIHandle();
	return Expect(firstCalls == 0 && secondCalls == 1 &&
		firstHandle != VansRuntime::kInvalidUIHandle && secondHandle > firstHandle,
		"Closing one Lua UI state invalidated another state or reused a lifecycle handle");
}

bool TestLuaScriptDeclaredAssetDependencyContract()
{
	using Vans::VansSerializedValue;
	const std::string assetGuid = "11111111-2222-3333-4444-555555555555";
	const VansSerializedValue scriptData = VansSerializedValue::Object({
		{ "fields", VansSerializedValue::Object({
			{ "ordinaryText", VansSerializedValue::String(assetGuid) },
			{ "sceneTarget", Vans::MakeSerializedSceneEntityObjectReference("scene-entity") },
			{ "texture", Vans::MakeSerializedProjectAssetObjectReference(assetGuid, "texture") }
		}) }
	});
	std::vector<VansScriptSerializedObjectReference> references;
	std::string error;
	if (!Expect(VansScriptComponentReader::CollectProjectAssetReferences(
			scriptData, references, error) && references.size() == 1 &&
			references[0].guid == assetGuid && references[0].assetType == "texture",
		"Script dependency collection guessed strings or lost declared ProjectAsset metadata"))
		return false;

	const VansSerializedValue invalid = VansSerializedValue::Object({
		{ "fields", VansSerializedValue::Object({
			{ "asset", VansSerializedValue::Object({
				{ "domain", VansSerializedValue::String("ProjectAsset") },
				{ "guid", VansSerializedValue::String(assetGuid) }
			}) }
		}) }
	});
	return Expect(!VansScriptComponentReader::CollectProjectAssetReferences(
		invalid, references, error) && error.find("assetType") != std::string::npos,
		"Script ProjectAsset dependency accepted a reference without assetType");
}

bool TestDemoHallMotionMatchingMovementLibraryContract()
{
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
	{
		if (!workspace.has_parent_path() || workspace.parent_path() == workspace)
			break;
		workspace = workspace.parent_path();
	}
	const fs::path projectRoot = workspace / "DemoHallProject";
	const fs::path scenePath = projectRoot / "Scenes" / "DemoHall.json";
	const fs::path animatorPath = projectRoot / "Assets" / "MotionMatchDataBase" /
		"UEFN_Mannequin.vanimator";
	if (!fs::exists(scenePath) || !fs::exists(animatorPath))
		return true;

	nlohmann::json scene;
	nlohmann::json animator;
	const auto readJsonText = [](const fs::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
	};
	try
	{
		scene = nlohmann::json::parse(readJsonText(scenePath));
		animator = nlohmann::json::parse(readJsonText(animatorPath));
	}
	catch (const std::exception& error)
	{
		return Expect(false, ("DemoHall Motion Matching movement assets are invalid: " +
			std::string(error.what())).c_str());
	}

	const auto isActionClip = [](const std::string& name)
	{
		return name.rfind("AlbomBreak", 0) == 0 ||
			name.rfind("Attack_", 0) == 0 || name.rfind("Pistol_", 0) == 0 ||
			name.rfind("Throw", 0) == 0 || name.rfind("Vault", 0) == 0;
	};
	std::unordered_set<std::string> movementClips;
	std::unordered_set<std::string> actionClips;
	for (const nlohmann::json& clip : animator.value("clips", nlohmann::json::array()))
	{
		const std::string name = clip.value("name", "");
		if (isActionClip(name))
			actionClips.insert(name);
		else if (!name.empty())
			movementClips.insert(name);
	}
	if (!Expect(movementClips.size() == 195 && actionClips.size() == 10 &&
		movementClips.size() + actionClips.size() == animator.value("clips", nlohmann::json::array()).size(),
		"UEFN_Mannequin movement inventory is incomplete or contains an unclassified action"))
		return false;

	// 资源清单统一由 Animator 持有，移动必须经过 Base Graph 的 MM 节点。
	// 动作可由 GAF 动态提交 Slot，因此不能要求每个动作都有静态状态引用。
	bool baseGraphHasMotionMatching = false;
	for (const nlohmann::json& graphDocument :
		 animator.value("graphs", nlohmann::json::array()))
	{
		if (graphDocument.value("id", "") != "graph-base")
			continue;
		for (const nlohmann::json& node :
			 graphDocument.value("graph", nlohmann::json::object()).value(
				 "nodes", nlohmann::json::array()))
			baseGraphHasMotionMatching = baseGraphHasMotionMatching ||
				node.value("type", "") == "MotionMatching";
	}
	if (!Expect(baseGraphHasMotionMatching,
		"Base Graph lost its Motion Matching node"))
		return false;

	std::size_t activeCharacters = 0;
	for (const nlohmann::json& entity : scene.value("entities", nlohmann::json::array()))
	{
		for (const nlohmann::json& component :
			 entity.value("components", nlohmann::json::array()))
		{
			if (component.value("type", "") != "Animation" ||
				!component.contains("data") ||
				!component.at("data").contains("motion_matching"))
				continue;
			const nlohmann::json& motion = component.at("data").at("motion_matching");
			if (!motion.value("enabled", false))
				continue;
			++activeCharacters;
			const nlohmann::json& model = motion.at("motion_model");
			if (!Expect(model.value("drive_mode", "") == "capsule" &&
			motion.value("search_throttle", 1.0f) <= 0.0001f &&
			motion.value("min_switch_interval", 1.0f) <= 0.0001f,
			"DemoHall locomotion must use responsive Capsule/Movement authority"))
			return false;

		std::unordered_set<std::string> indexed;
		bool hasAirDatabase = false;
		bool hasSecondaryDatabase = false;
		for (const nlohmann::json& database :
			 motion.value("databases", nlohmann::json::array()))
		{
			const std::string databaseName = database.value("name", "");
			hasAirDatabase = hasAirDatabase || databaseName == "PSD_DemoHall_Air_Movement";
			hasSecondaryDatabase = hasSecondaryDatabase ||
				databaseName == "PSD_DemoHall_Secondary_Movement";
			for (const nlohmann::json& clip :
				 database.value("clips", nlohmann::json::array()))
			{
				const std::string name = clip.value("name", "");
				if (!name.empty()) indexed.insert(name);
				if (databaseName == "PSD_DemoHall_Air_Movement" && name == "Jump_Fall" &&
					!Expect(clip.value("loop", false),
						"Jump_Fall must hold as the airborne loop until the CCT reports landing"))
					return false;
			}
			// Empty token databases are intentionally part of the resource index;
			// runtime auto-build expands them from the same UEFN clip inventory.
			for (const nlohmann::json& token :
				 database.value("include_tokens", nlohmann::json::array()))
			{
				const std::string needle = token.get<std::string>();
				for (const std::string& name : movementClips)
					if (name.find(needle) != std::string::npos)
						indexed.insert(name);
			}
		}
		if (!Expect(hasAirDatabase && hasSecondaryDatabase,
			"DemoHall movement databases are missing Air or secondary movement coverage"))
			return false;
		for (const std::string& name : movementClips)
			if (!Expect(indexed.count(name) > 0,
				("UE movement clip is absent from the Motion Matching resource index: " + name).c_str()))
				return false;
		for (const std::string& name : actionClips)
			if (!Expect(indexed.count(name) == 0,
				("Gameplay Action clip leaked into the locomotion Motion Matching index: " + name).c_str()))
				return false;
		for (const nlohmann::json& row :
			 motion.value("selector", nlohmann::json::array()))
		{
			if (row.value("name", "") != "AirMovement")
				continue;
			const auto selectedDatabases = row.value("databases", nlohmann::json::array());
			const bool selectsAirDatabase = std::find(
				selectedDatabases.begin(), selectedDatabases.end(),
				nlohmann::json("PSD_DemoHall_Air_Movement")) != selectedDatabases.end();
			if (!Expect(row.value("phase", "") == "Air" &&
				selectsAirDatabase,
				"DemoHall Air movement selector does not activate the Air database"))
				return false;
		}
	}
	}
	return Expect(activeCharacters == 2,
		"DemoHall must keep both Motion Matching characters on the movement path");
}

bool TestMotionMatchingCameraFacingTurnContract()
{
    using namespace VansGraphics;

    const float predictedMovingCameraYaw = Vans::PredictFacingYaw(
        0.0f, 0.0f, 90.0f, 1.0f, 0.10f);
    if (!Expect(predictedMovingCameraYaw > 70.0f && predictedMovingCameraYaw < 90.0f,
        "Future facing did not account for camera angular velocity"))
        return false;

    const Skeleton skeleton = BuildContractHumanoidSkeleton();
    std::unordered_map<std::string, VansAnimationClip> clips;
    clips.emplace("Idle_Stand", BuildContractClip("Idle_Stand", 0.0f, 0.0f));
    clips.emplace("IdleTurn_L_090", BuildContractTurnClip("IdleTurn_L_090", 90.0f));
    clips.emplace("IdleTurn_R_090", BuildContractTurnClip("IdleTurn_R_090", -90.0f));
    clips.emplace("WalkStart_F", BuildContractClip("WalkStart_F", -0.30f, 0.10f));
    clips.emplace("Walk_F", BuildContractClip("Walk_F", -0.70f, 0.20f));
    clips.emplace("WalkStart_L", BuildContractClip("WalkStart_L", 0.30f, 0.10f));
    clips.emplace("Walk_L", BuildContractClip("Walk_L", 0.70f, 0.20f));
    clips.emplace("WalkTurn_L_090", BuildContractTurnClip("WalkTurn_L_090", 90.0f));

    std::unordered_map<std::string, AnimatorParameter> parameters;
    parameters["Speed"] = { "Speed", AnimatorParamType::Float };
    parameters["Direction"] = { "Direction", AnimatorParamType::Float };
    parameters["MoveState"] = { "MoveState", AnimatorParamType::Int };
    parameters["UseMotionMatching"] = { "UseMotionMatching", AnimatorParamType::Bool };
    parameters["UseMotionMatching"].boolVal = true;

    MotionMatchingSettings settings;
    settings.enabled = true;
    settings.autoBuild = true;
    settings.searchThrottle = 0.01f;
    settings.minSwitchInterval = 0.0f;
    settings.minSwitchCostImprovement = 0.0f;
    settings.desiredSpeedScale = 1.0f;
    settings.facingTurnEnterThresholdDegrees = 10.0f;
    settings.facingTurnExitThresholdDegrees = 3.0f;
    settings.facingTurnExitYawRateDegreesPerSecond = 8.0f;
    settings.turnInPlaceWarping.enabled = true;
    settings.includeClipTokens = { "Idle", "Walk" };
    settings.rig.root = "root";
    settings.rig.trajectoryRoot = "root";
    settings.rig.pelvis = "pelvis";
    settings.rig.leftFoot = "foot_l";
    settings.rig.rightFoot = "foot_r";
    settings.rig.head = "head";
    settings.rig.forwardAxis = glm::vec3(0.0f, -1.0f, 0.0f);

    auto buildTrajectory = [&](float desiredFacingYaw)
    {
        Vans::VansCharacterTrajectory trajectory;
        trajectory.valid = true;
        trajectory.hasFacing = true;
        trajectory.currentFacingYaw = 37.0f;
        trajectory.desiredFacingYaw = desiredFacingYaw;
        trajectory.originWorld = glm::vec3(0.0f);
        for (std::size_t index = 0; index < trajectory.future.size(); ++index)
        {
            trajectory.future[index].time = settings.schema.futureTimes[index];
            trajectory.future[index].facingYaw = desiredFacingYaw;
        }
        return trajectory;
    };

    for (const float deltaTime : { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f })
    {
      for (const int turnSign : { 1, -1 })
      {
        VansMotionMatchingRuntime runtime;
        runtime.Configure(settings);
        VansPosePayload payload;
        Vans::VansCharacterTrajectory trajectory = buildTrajectory(37.0f);
        if (!runtime.Update(deltaTime, skeleton, clips, parameters, &trajectory, payload))
            return Expect(false, "Motion matching rejected the camera-facing idle fixture");

        // 73° 不存在于离散动画库中。测试必须像 CCT 一样把每帧最终 Root Yaw
        // 积分回当前 Transform，验证一个 90° Turn 能否一次命中真实目标。
        const float targetFacingYaw =
            37.0f + static_cast<float>(turnSign) * 73.0f;
        trajectory.desiredFacingYaw = targetFacingYaw;
        for (auto& future : trajectory.future)
            future.facingYaw = targetFacingYaw;

        int turnEntries = 0;
        bool previousTurnActive = false;
        bool selectedExpectedTurn = false;
        bool observedTurnWarp = false;
        bool observedResidualReplan = false;
        bool preservedRootAuthorityOnTurnExit = false;
        bool completed = false;
        const int maximumFrames = static_cast<int>(std::ceil(3.0f / deltaTime));
        for (int frame = 0; frame < maximumFrames; ++frame)
        {
            if (!runtime.Update(deltaTime, skeleton, clips, parameters, &trajectory, payload))
                return Expect(false, "Motion matching stopped during a camera-facing turn");
            const MotionMatchingDebugData& debug = runtime.GetDebugData();
            const bool activeTurn = debug.activeClip.find("Turn") != std::string::npos;
            const bool exitedTurnThisFrame = previousTurnActive && !activeTurn;
            if (activeTurn && !previousTurnActive)
                ++turnEntries;
            if (exitedTurnThisFrame)
                preservedRootAuthorityOnTurnExit =
					runtime.PrefersRootMotionThisFrame();
            previousTurnActive = activeTurn;
            const std::string expectedToken =
                turnSign > 0 ? "Turn_L_090" : "Turn_R_090";
            selectedExpectedTurn = selectedExpectedTurn ||
                debug.activeClip.find(expectedToken) != std::string::npos;
            observedTurnWarp = observedTurnWarp ||
                debug.turnWarpActive || debug.turnWarpProfileIndex >= 0;
            observedResidualReplan = observedResidualReplan ||
                debug.turnWarpReplanReason == "ResidualReplan";

            if (payload.rootMotion.valid)
            {
                trajectory.currentFacingYaw +=
                    ExtractRootMotionYawDegrees(payload.rootMotion.rotation);
                trajectory.plannedFacingYaw = trajectory.currentFacingYaw;
            }
            for (auto& future : trajectory.future)
                future.facingYaw = targetFacingYaw;

            const float remainingError = std::abs(std::remainder(
                targetFacingYaw - trajectory.currentFacingYaw, 360.0f));
            if (turnEntries > 0 && !activeTurn && remainingError <= 1.0f)
            {
                completed = true;
                break;
            }
        }
        if (!Expect(selectedExpectedTurn,
            "Camera facing error did not select the matching left/right turn clip"))
            return false;
        if (!Expect(observedTurnWarp,
            "Turn-in-place did not publish an active root-yaw profile/warp"))
            return false;
        if (!completed)
        {
            const MotionMatchingDebugData& debug = runtime.GetDebugData();
            std::cerr << "[ForestContractTests] turn endpoint target=" << targetFacingYaw
                << " current=" << trajectory.currentFacingYaw
                << " frameRate=" << (1.0f / deltaTime)
                << " error=" << std::remainder(
                    targetFacingYaw - trajectory.currentFacingYaw, 360.0f)
                << " entries=" << turnEntries
                << " active=" << debug.activeClip
                << " turnState=" << debug.facingTurnState
                << " warpReason=" << debug.turnWarpDisableReason
                << " replan=" << debug.turnWarpReplanReason
                << " targetDelta=" << debug.turnWarpTargetDeltaDegrees
                << " authoredRemaining=" << debug.turnWarpAuthoredRemainingYawDegrees
                << " scale=" << debug.turnWarpScaleRatio
                << " residual=" << debug.turnWarpResidualDegrees << '\n';
            return Expect(false,
                "A discrete Turn animation did not converge to the arbitrary target yaw");
        }
        if (!Expect(turnEntries == 1,
            "A static arbitrary facing target replayed a second Turn animation"))
            return false;
        if (!Expect(!observedResidualReplan,
            "A static arbitrary facing target produced a residual Turn replan"))
            return false;
        if (!Expect(preservedRootAuthorityOnTurnExit,
			"Hybrid drive lost source Turn root authority on the outgoing switch frame"))
			return false;
        if (!ExpectNear(
            std::remainder(targetFacingYaw - trajectory.currentFacingYaw, 360.0f),
            0.0f,
            1.0f,
            "Turn-in-place endpoint did not match the owner Transform yaw"))
            return false;
      }
    }

    // Capsule 驱动不拥有完整根旋转控制权：仍可保留既有离散 Turn 行为，
    // 但不得宣称端点修正生效，也不得偷偷修改 Root Motion。
    MotionMatchingSettings capsuleSettings = settings;
    capsuleSettings.motionModel.driveMode = Vans::VansLocomotionDriveMode::Capsule;
    VansMotionMatchingRuntime capsuleRuntime;
    capsuleRuntime.Configure(capsuleSettings);
    VansPosePayload capsulePayload;
    Vans::VansCharacterTrajectory capsuleTrajectory = buildTrajectory(37.0f);
    if (!capsuleRuntime.Update(
        1.0f / 60.0f, skeleton, clips, parameters, &capsuleTrajectory, capsulePayload))
    {
        return Expect(false, "Capsule-drive Motion Matching fixture failed to initialize");
    }
    capsuleTrajectory.desiredFacingYaw = 110.0f;
    for (auto& future : capsuleTrajectory.future)
        future.facingYaw = capsuleTrajectory.desiredFacingYaw;
    bool observedNonAuthoritativeTurn = false;
    bool capsuleWarpedRootYaw = false;
    for (int frame = 0; frame < 30; ++frame)
    {
        if (!capsuleRuntime.Update(
            1.0f / 60.0f, skeleton, clips, parameters, &capsuleTrajectory, capsulePayload))
        {
            return Expect(false, "Capsule-drive Motion Matching stopped during a Turn");
        }
        const MotionMatchingDebugData& debug = capsuleRuntime.GetDebugData();
        observedNonAuthoritativeTurn = observedNonAuthoritativeTurn ||
            debug.turnWarpDisableReason == "RootRotationNotAuthoritative";
        capsuleWarpedRootYaw = capsuleWarpedRootYaw || debug.turnWarpActive;
    }
    if (!Expect(observedNonAuthoritativeTurn && !capsuleWarpedRootYaw,
        "Capsule drive did not explicitly disable non-authoritative Turn endpoint warping"))
        return false;

    // 移动中转相机不得把 one-shot Turn 当成连续转向控制器。未来轨迹继续
    // 参与 Pose Search，剩余朝向误差由 Root Motion Steering 平滑施加。
    VansMotionMatchingRuntime movingTurnRuntime;
    movingTurnRuntime.Configure(settings);
    VansPosePayload movingTurnPayload;
    MotionMatchingSettings movingBaselineSettings = settings;
    movingBaselineSettings.turnInPlaceWarping.enabled = false;
    VansMotionMatchingRuntime movingBaselineRuntime;
    movingBaselineRuntime.Configure(movingBaselineSettings);
    VansPosePayload movingBaselinePayload;
    Vans::VansCharacterTrajectory movingTurnTrajectory = buildTrajectory(37.0f);
    parameters["Speed"].floatVal = 0.6f;
    parameters["Direction"].floatVal = 0.0f;
    parameters["MoveState"].intVal = 1;
    const glm::quat initialWorldFromFacing = glm::angleAxis(
        glm::radians(movingTurnTrajectory.currentFacingYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    movingTurnTrajectory.currentVelocityWorld =
        initialWorldFromFacing * glm::vec3(0.0f, 0.0f, 0.6f);
    movingTurnTrajectory.desiredVelocityWorld = movingTurnTrajectory.currentVelocityWorld;
    for (auto& future : movingTurnTrajectory.future)
    {
        future.positionWorld = movingTurnTrajectory.originWorld +
            movingTurnTrajectory.desiredVelocityWorld * future.time;
        future.velocityWorld = movingTurnTrajectory.desiredVelocityWorld;
    }
    bool movingPathParity = true;
    auto compareMovingPath = [&]()
    {
        const MotionMatchingDebugData& enabledDebug = movingTurnRuntime.GetDebugData();
        const MotionMatchingDebugData& baselineDebug = movingBaselineRuntime.GetDebugData();
        const bool rotationsMatch =
            movingTurnPayload.rootMotion.valid == movingBaselinePayload.rootMotion.valid
            && (!movingTurnPayload.rootMotion.valid ||
                std::abs(glm::dot(
                    movingTurnPayload.rootMotion.rotation,
                    movingBaselinePayload.rootMotion.rotation)) >= 0.99999f);
        return enabledDebug.activeClip == baselineDebug.activeClip
            && std::abs(enabledDebug.activeTime - baselineDebug.activeTime) <= 0.0001f
            && glm::length(movingTurnPayload.rootMotion.translation
                - movingBaselinePayload.rootMotion.translation) <= 0.0001f
            && rotationsMatch;
    };
    for (int frame = 0; frame < 16; ++frame)
    {
        if (!movingTurnRuntime.Update(
            0.033f, skeleton, clips, parameters, &movingTurnTrajectory, movingTurnPayload)
            || !movingBaselineRuntime.Update(
                0.033f, skeleton, clips, parameters,
                &movingTurnTrajectory, movingBaselinePayload))
            return Expect(false, "Motion matching stopped before the moving-turn fixture stabilized");
        movingPathParity = movingPathParity && compareMovingPath();
    }

    movingTurnTrajectory.desiredFacingYaw = movingTurnTrajectory.currentFacingYaw + 20.0f;
    movingTurnTrajectory.desiredFacingYawRate = 45.0f;
    const glm::quat desiredWorldFromFacing = glm::angleAxis(
        glm::radians(movingTurnTrajectory.desiredFacingYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    movingTurnTrajectory.desiredVelocityWorld =
        desiredWorldFromFacing * glm::vec3(0.0f, 0.0f, 0.6f);
    for (auto& future : movingTurnTrajectory.future)
    {
        future.positionWorld = movingTurnTrajectory.originWorld +
            movingTurnTrajectory.desiredVelocityWorld * future.time;
        future.velocityWorld = movingTurnTrajectory.desiredVelocityWorld;
        future.facingYaw = Vans::PredictFacingYaw(
            movingTurnTrajectory.currentFacingYaw,
            movingTurnTrajectory.desiredFacingYaw,
            movingTurnTrajectory.desiredFacingYawRate,
            future.time,
            0.10f);
    }
    bool observedSteering = false;
    bool emittedSteeredRootRotation = false;
    bool remainedInMovingLoop = true;
    for (int frame = 0; frame < 80; ++frame)
    {
        if (!movingTurnRuntime.Update(
            0.033f, skeleton, clips, parameters, &movingTurnTrajectory, movingTurnPayload)
            || !movingBaselineRuntime.Update(
                0.033f, skeleton, clips, parameters,
                &movingTurnTrajectory, movingBaselinePayload))
            return Expect(false, "Motion matching stopped during continuous moving-camera turn");
        movingPathParity = movingPathParity && compareMovingPath();
        const auto& debug = movingTurnRuntime.GetDebugData();
        remainedInMovingLoop = remainedInMovingLoop &&
            debug.activeClip.find("Turn") == std::string::npos &&
            !debug.facingTurnRequested;
        observedSteering = observedSteering || debug.steeringActive;
        emittedSteeredRootRotation = emittedSteeredRootRotation ||
            (movingTurnPayload.rootMotion.valid &&
             std::abs(glm::degrees(glm::eulerAngles(
                 movingTurnPayload.rootMotion.rotation)).z) > 0.01f);
    }
    if (!Expect(remainedInMovingLoop,
        "Moving camera intent re-entered a discrete Turn clip"))
        return false;
    if (!Expect(observedSteering,
        "Moving camera intent did not activate Root Motion Steering"))
        return false;
    if (!Expect(emittedSteeredRootRotation,
        "Root Motion Steering did not publish a continuous yaw correction"))
        return false;
    if (!Expect(movingPathParity,
        "Enabling Turn-in-place warping changed the existing moving Motion Matching path"))
        return false;

	// Camera-relative world travel may already be curving while the held input is
	// still Forward. Directional database filtering must follow the local input
	// semantic; the world trajectory remains available to the numeric cost.
	VansMotionMatchingRuntime referenceFrameRuntime;
	referenceFrameRuntime.Configure(settings);
	VansPosePayload referenceFramePayload;
	Vans::VansCharacterTrajectory referenceFrameTrajectory = buildTrajectory(0.0f);
	referenceFrameTrajectory.hasFacing = false;
	referenceFrameTrajectory.currentFacingYaw = 0.0f;
	referenceFrameTrajectory.moveInputLocal = glm::vec2(0.0f, 1.0f);
	referenceFrameTrajectory.currentVelocityWorld = glm::vec3(0.0f, 0.0f, 0.6f);
	referenceFrameTrajectory.desiredVelocityWorld = glm::vec3(0.6f, 0.0f, 0.0f);
	for (auto& future : referenceFrameTrajectory.future)
	{
		future.positionWorld = referenceFrameTrajectory.originWorld +
			referenceFrameTrajectory.desiredVelocityWorld * future.time;
		future.velocityWorld = referenceFrameTrajectory.desiredVelocityWorld;
		future.facingYaw = 0.0f;
	}
	parameters["Speed"].floatVal = 0.6f;
	parameters["Direction"].floatVal = 1.57079632679f;
	parameters["MoveState"].intVal = 1;
	for (int frame = 0; frame < 16; ++frame)
	{
		if (!referenceFrameRuntime.Update(
			0.033f, skeleton, clips, parameters,
			&referenceFrameTrajectory, referenceFramePayload))
		{
			return Expect(false,
				"Motion matching stopped during movement-reference trajectory testing");
		}
	}
	const MotionMatchingDebugData& referenceFrameDebug =
		referenceFrameRuntime.GetDebugData();
	if (!ExpectNear(referenceFrameDebug.queryDirection, 0.0f, 0.0001f,
		"Camera-relative world trajectory replaced the held local Forward direction"))
		return false;
	if (!Expect(referenceFrameDebug.activeClip.find("_L") == std::string::npos,
		"Camera-relative world trajectory selected a lateral direction database"))
		return false;

	referenceFrameTrajectory.hasDirectionChange = true;
	referenceFrameTrajectory.directionChangeDegrees = 180.0f;
	referenceFrameTrajectory.inputDirectionChangeDegrees = 180.0f;
	if (!referenceFrameRuntime.Update(
		0.033f, skeleton, clips, parameters,
		&referenceFrameTrajectory, referenceFramePayload))
	{
		return Expect(false, "Motion matching stopped while checking pivot availability");
	}
	const MotionMatchingDebugData& noPivotDebug = referenceFrameRuntime.GetDebugData();
	if (!Expect(!noPivotDebug.pivotDatabaseAvailable && !noPivotDebug.pivotRequested,
		"Missing pivot data still suppressed normal locomotion/facing matching"))
		return false;

    // Changing travel direction while keeping camera-facing yaw fixed must
    // select a strafe/start clip, never a root-rotating turn clip.
    VansMotionMatchingRuntime strafeRuntime;
    strafeRuntime.Configure(settings);
    VansPosePayload strafePayload;
    Vans::VansCharacterTrajectory strafeTrajectory = buildTrajectory(37.0f);
    parameters["Speed"].floatVal = 0.0f;
    parameters["Direction"].floatVal = 0.0f;
    parameters["MoveState"].intVal = 0;
    if (!strafeRuntime.Update(0.033f, skeleton, clips, parameters, &strafeTrajectory, strafePayload))
        return Expect(false, "Motion matching rejected the strafe fixture");
    parameters["Speed"].floatVal = 0.6f;
    parameters["Direction"].floatVal = 1.57079632679f;
    parameters["MoveState"].intVal = 1;
    const glm::quat worldFromFacing = glm::angleAxis(
        glm::radians(strafeTrajectory.currentFacingYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    strafeTrajectory.currentVelocityWorld = worldFromFacing * glm::vec3(0.6f, 0.0f, 0.0f);
    strafeTrajectory.desiredVelocityWorld = strafeTrajectory.currentVelocityWorld;
    for (auto& future : strafeTrajectory.future)
    {
        future.positionWorld = strafeTrajectory.originWorld +
            strafeTrajectory.currentVelocityWorld * future.time;
        future.velocityWorld = strafeTrajectory.currentVelocityWorld;
        future.facingYaw = strafeTrajectory.currentFacingYaw;
    }
    for (int frame = 0; frame < 12; ++frame)
    {
        if (!strafeRuntime.Update(0.033f, skeleton, clips, parameters, &strafeTrajectory, strafePayload))
            return Expect(false, "Motion matching stopped while selecting a strafe clip");
        const MotionMatchingDebugData& debug = strafeRuntime.GetDebugData();
        if (!Expect(!debug.facingTurnRequested &&
                    debug.activeClip.find("Turn") == std::string::npos,
            "Travel direction change incorrectly triggered a facing turn"))
            return false;
    }
    return true;
}

bool TestRootMotionSteeringContract()
{
	using namespace VansGraphics;
	RootMotionSteeringSettings settings;
	settings.predictionTime = 0.5f;
	settings.correctionHalfLife = 0.01f;
	settings.maxCorrectionAngleDegrees = 70.0f;
	settings.maxCorrectionYawRateDegreesPerSecond = 300.0f;
	settings.minMovementSpeed = 0.1f;
	VansRootMotionSteering steering;
	steering.Configure(settings);
	glm::quat rootRotation(1.0f, 0.0f, 0.0f, 0.0f);
	const RootMotionSteeringResult result = steering.Apply(
		0.1f, 1.0f, 0.0f, 90.0f, 0.0f, rootRotation);
	if (!Expect(result.active && result.limited,
		"Root Motion Steering did not activate or clamp an oversized correction"))
		return false;
	if (!Expect(result.appliedCorrectionDegrees > 0.0f &&
		result.appliedCorrectionDegrees <= 30.001f,
		"Root Motion Steering exceeded its configured yaw-rate limit"))
		return false;
	if (!ExpectNear(
		glm::degrees(glm::eulerAngles(rootRotation)).z,
		result.appliedCorrectionDegrees,
		0.01f,
		"Root Motion Steering debug result did not match the emitted rotation"))
		return false;
	const RootMotionSteeringResult stopped = steering.Apply(
		0.1f, 0.0f, 0.0f, 90.0f, 0.0f, rootRotation);
	return Expect(!stopped.active,
		"Root Motion Steering remained active after locomotion stopped");
}

bool TestMotionMatchingPivotDirectionContract()
{
	using namespace VansGraphics;
	const Skeleton skeleton = BuildContractHumanoidSkeleton();
	std::unordered_map<std::string, VansAnimationClip> clips;
	clips.emplace("Walk_F", BuildContractClip("Walk_F", -0.70f, 0.20f));
	clips.emplace("Walk_B", BuildContractClip("Walk_B", 0.70f, 0.20f));
	clips.emplace("WalkPivot_F_B_Lfoot",
		BuildContractClip("WalkPivot_F_B_Lfoot", 0.20f, 0.15f));
	clips.emplace("WalkPivot_B_F_Rfoot",
		BuildContractClip("WalkPivot_B_F_Rfoot", -0.20f, 0.15f));

	std::unordered_map<std::string, AnimatorParameter> parameters;
	parameters["Speed"] = { "Speed", AnimatorParamType::Float };
	parameters["Direction"] = { "Direction", AnimatorParamType::Float };
	parameters["MoveState"] = { "MoveState", AnimatorParamType::Int };
	parameters["UseMotionMatching"] = { "UseMotionMatching", AnimatorParamType::Bool };
	parameters["Speed"].floatVal = 0.7f;
	parameters["MoveState"].intVal = 1;
	parameters["UseMotionMatching"].boolVal = true;

	MotionMatchingSettings settings;
	settings.enabled = true;
	settings.autoBuild = true;
	settings.searchThrottle = 0.01f;
	settings.minSwitchInterval = 0.0f;
	settings.minSwitchCostImprovement = 0.0f;
	settings.desiredSpeedScale = 1.0f;
	settings.pivotEnterAngleDegrees = 55.0f;
	settings.pivotExitAngleDegrees = 25.0f;
	settings.pivotMinSpeed = 0.1f;
	settings.directionBucketTolerance = 0;
	settings.rig.root = "root";
	settings.rig.trajectoryRoot = "root";
	settings.rig.pelvis = "pelvis";
	settings.rig.leftFoot = "foot_l";
	settings.rig.rightFoot = "foot_r";
	settings.rig.head = "head";
	settings.rig.forwardAxis = glm::vec3(0.0f, -1.0f, 0.0f);
	MotionMatchingDatabase moveDatabase;
	moveDatabase.name = "WalkMove";
	moveDatabase.phase = "Move";
	moveDatabase.moveStates = { 1 };
	moveDatabase.includeTokens = { "Walk_" };
	settings.databases.push_back(moveDatabase);
	MotionMatchingDatabase pivotDatabase;
	pivotDatabase.name = "WalkPivot";
	pivotDatabase.phase = "Pivot";
	pivotDatabase.moveStates = { 1 };
	pivotDatabase.includeTokens = { "WalkPivot_" };
	settings.databases.push_back(pivotDatabase);
	MotionMatchingSelectorRow moveRow;
	moveRow.name = "Move";
	moveRow.phase = "Move";
	moveRow.moveStates = { 1 };
	moveRow.databases = { "WalkMove" };
	settings.selectorRows.push_back(moveRow);
	MotionMatchingSelectorRow pivotRow;
	pivotRow.name = "Pivot";
	pivotRow.phase = "Pivot";
	pivotRow.moveStates = { 1 };
	pivotRow.databases = { "WalkPivot" };
	settings.selectorRows.push_back(pivotRow);

	Vans::VansCharacterTrajectory trajectory;
	trajectory.valid = true;
	trajectory.moveInputLocal = glm::vec2(0.0f, 1.0f);
	trajectory.currentVelocityWorld = glm::vec3(0.0f, 0.0f, 0.7f);
	trajectory.plannedVelocityWorld = trajectory.currentVelocityWorld;
	trajectory.desiredVelocityWorld = trajectory.currentVelocityWorld;
	for (std::size_t index = 0; index < trajectory.future.size(); ++index)
	{
		auto& future = trajectory.future[index];
		future.time = settings.schema.futureTimes[index];
		future.positionWorld = trajectory.originWorld +
			trajectory.desiredVelocityWorld * future.time;
		future.velocityWorld = trajectory.desiredVelocityWorld;
	}

	VansMotionMatchingRuntime runtime;
	runtime.Configure(settings);
	VansPosePayload payload;
	for (int frame = 0; frame < 16; ++frame)
		if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
			return Expect(false, "Motion matching rejected the Pivot fixture");
	if (!Expect(runtime.GetDebugData().activeClip == "Walk_F",
		"Pivot fixture did not stabilize in the Forward loop"))
		return false;

	trajectory.moveInputLocal = glm::vec2(0.0f, -1.0f);
	trajectory.desiredVelocityWorld = glm::vec3(0.0f, 0.0f, -0.7f);
	trajectory.hasDirectionChange = true;
	trajectory.directionChangeDegrees = 180.0f;
	trajectory.inputDirectionChangeDegrees = 180.0f;
	trajectory.hasPredictedPivot = true;
	trajectory.predictedPivotTime = 0.20f;
	for (auto& future : trajectory.future)
	{
		future.positionWorld = trajectory.originWorld +
			trajectory.desiredVelocityWorld * future.time;
		future.velocityWorld = trajectory.desiredVelocityWorld;
	}
	bool selectedForwardToBackPivot = false;
	for (int frame = 0; frame < 12; ++frame)
	{
		if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
			return Expect(false, "Motion matching stopped during a Pivot request");
		const MotionMatchingDebugData& debug = runtime.GetDebugData();
		if (!Expect(debug.pivotDatabaseAvailable && debug.pivotRequested,
			"Authored Pivot database was not selected for a direction reversal"))
			return false;
		selectedForwardToBackPivot = selectedForwardToBackPivot ||
			debug.activeClip == "WalkPivot_F_B_Lfoot";
	}
	if (!selectedForwardToBackPivot)
	{
		const MotionMatchingDebugData& debug = runtime.GetDebugData();
		std::cerr << "[ForestContractTests] active=" << debug.activeClip
			<< " selected=" << debug.selectedClip << " databases:";
		for (const auto& database : debug.activeDatabases)
			std::cerr << ' ' << database;
		std::cerr << " candidates:";
		for (const auto& candidate : debug.topCandidates)
			std::cerr << ' ' << candidate.clipName << '=' << candidate.totalCost;
		std::cerr << '\n';
	}
	if (!Expect(selectedForwardToBackPivot,
		"Pivot source/target direction metadata selected the wrong transition"))
		return false;
	trajectory.hasPredictedPivot = false;
	trajectory.directionChangeDegrees = 0.0f;
	for (int frame = 0; frame < 8; ++frame)
	{
		if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
			return Expect(false, "Motion matching stopped while exiting Pivot");
	}
	return Expect(!runtime.GetDebugData().pivotRequested,
		"Pivot request remained locked after the predicted zero crossing disappeared");
}

bool TestRootMotionReconciliationContract()
{
	using namespace VansGraphics;
	RootMotionReconciliationSettings settings;
	settings.enabled = true;
	settings.linearVelocityHalfLife = 0.10f;
	settings.angularVelocityHalfLife = 0.10f;
	settings.maxDuration = 1.0f;
	settings.maxLinearVelocityCorrection = 100.0f;
	settings.maxAngularVelocityCorrectionDegreesPerSecond = 1000.0f;
	VansRootMotionReconciler reconciler;
	reconciler.Configure(settings);
	reconciler.RequestTransition(glm::vec3(0.0f, 4.0f, 0.0f), 90.0f);

	constexpr float dt = 0.10f;
	glm::vec3 translation(0.0f, 0.10f, 0.0f);
	glm::quat rotation = glm::angleAxis(
		glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
		glm::angleAxis(glm::radians(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	const float sharedTargetYawRate =
		ExtractRootMotionYawDegrees(rotation) / dt;
	const RootMotionReconciliationResult first = reconciler.Apply(
		dt, translation, rotation);
	if (!Expect(first.active &&
		std::abs(first.appliedVelocityAnimation.y - 4.0f) < 0.001f &&
		std::abs(first.targetYawRateDegreesPerSecond - sharedTargetYawRate) < 0.01f &&
		std::abs(first.appliedYawRateDegreesPerSecond - 90.0f) < 0.01f,
		"Root transition did not preserve outgoing linear/angular velocity"))
		return false;

	translation = glm::vec3(0.0f, 0.10f, 0.0f);
	rotation = glm::angleAxis(
		glm::radians(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	const RootMotionReconciliationResult second = reconciler.Apply(
		dt, translation, rotation);
	return Expect(second.active &&
		second.appliedVelocityAnimation.y < first.appliedVelocityAnimation.y &&
		second.appliedVelocityAnimation.y > second.targetVelocityAnimation.y,
		"Root transition velocity did not decay toward the destination clip");
}

std::string BuildMinimalVClipHeader(
	bool includeNodeTransformChannels, std::uint64_t skeletonSignature)
{
    std::string header =
        R"({"clipName":"ConfiguredSkeletalOnly","duration":0.0,"ticksPerSecond":60.0,"boneCount":0,)"
		R"("sourceSkeletonGuid":"contract-skeleton","skeletonSignature":)"
		+ std::to_string(skeletonSignature)
		+ R"(,"globalInverseTransform":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"bones":[])";
    if (includeNodeTransformChannels)
        header += R"(,"nodeTransformChannels":[])";
    header += "}";
    return header;
}

bool WriteMinimalVClip(
	const fs::path& path, bool includeNodeTransformChannels,
	std::uint64_t skeletonSignature)
{
	const std::string header = BuildMinimalVClipHeader(
		includeNodeTransformChannels, skeletonSignature);
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
	Skeleton fixtureSkeleton;
	fixtureSkeleton.sourceSkeletonGuid = "contract-skeleton";
	fixtureSkeleton.RebuildIdentityMapsAndSignature();

	const fs::path unconfiguredClip = temporary.path / "unconfigured_skeletal_only.vclip";
	if (!Expect(WriteMinimalVClip(unconfiguredClip, false, fixtureSkeleton.signature),
        "Failed to write unconfigured skeletal-only vclip fixture"))
        return false;
    if (!Expect(!VansAnimationClipIO::Load(unconfiguredClip.string(), clip, skeleton),
        "Skeletal-only vclip without nodeTransformChannels resource config loaded unexpectedly"))
        return false;

	const fs::path configuredClip = temporary.path / "configured_skeletal_only.vclip";
	if (!Expect(WriteMinimalVClip(configuredClip, false, fixtureSkeleton.signature) &&
		WriteVClipMeta(configuredClip, false),
		"Failed to write configured skeletal-only vclip fixture"))
		return false;
	if (!Expect(!VansAnimationClipIO::Load(configuredClip.string(), clip, skeleton),
		"Legacy metadata changed the current Animation Clip source schema"))
		return false;

	const fs::path currentClip = temporary.path / "current_empty_node_channels.vclip";
	if (!Expect(WriteMinimalVClip(currentClip, true, fixtureSkeleton.signature),
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
    const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
	asset.animationRigGuid = "44444444-4444-4444-8444-444444444444";
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
    const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
    baseLayer.kind = VansAnimationLayerKind::Base;
    baseLayer.rootMotion = VansLayerRootMotionMode::Base;
    baseLayer.nodeTracks = VansLayerNodeTrackMode::Override;
    asset.layers.push_back(baseLayer);
	VansAnimationGraphSetDefinition graphSet;
	graphSet.id = "graph-set-default";
	graphSet.name = "Default";
	graphSet.bindings.push_back({ "layer-base", "graph-base", true });
	asset.defaultGraphSetId = graphSet.id;
	asset.graphSets.push_back(std::move(graphSet));

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
		&& loaded.graphSets.size() == 1 && loaded.defaultGraphSetId == "graph-set-default"
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
    const int firstId = graph.AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::SpeedScale));
    const int secondId = graph.AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::SpeedScale));
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
            m_Type = VansAnimGraphNodeType::Clip;
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
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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

bool TestCurrentProjectTimelineAssetsContract()
{
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
		"AnimationV2Project", "DustV3Project", "DemoHallProject", "SponzaProject"
	};
	std::size_t timelineCount = 0;
	for (const std::string& projectName : projectNames)
	{
		const fs::path assetsRoot = workspace / projectName / "Assets";
		if (!fs::exists(assetsRoot))
			continue;

		std::error_code error;
		for (fs::recursive_directory_iterator iterator(assetsRoot, error), end;
			!error && iterator != end; iterator.increment(error))
		{
			if (!iterator->is_regular_file() || iterator->path().extension() != ".vtimeline")
				continue;
			++timelineCount;
			Vans::VansTimelineAsset timeline;
			std::string timelineError;
			if (!Expect(Vans::VansTimelineSerialization::Load(
				iterator->path(), timeline, timelineError),
				"Current project Timeline failed canonical loading"))
			{
				std::cerr << "[ProjectTimeline] " << iterator->path().string()
					<< " :: " << timelineError << std::endl;
				return false;
			}

			Vans::VansTimelineValidationContext validation;
			validation.requireRuntimeCapabilities = true;
			validation.extensions = TimelineCatalog().trackExtensions;
			const Vans::VansTimelineDiagnostics diagnostics =
				Vans::VansTimelineValidator::Validate(timeline, validation);
			for (const Vans::VansTimelineDiagnostic& diagnostic : diagnostics)
			{
				if (diagnostic.severity != Vans::VansTimelineDiagnosticSeverity::Error)
					continue;
				std::cerr << "[ProjectTimeline] " << iterator->path().string() << " :: "
					<< diagnostic.objectId << "." << diagnostic.propertyPath << " :: "
					<< diagnostic.message << std::endl;
			}
			if (!Expect(!Vans::VansTimelineValidator::HasErrors(diagnostics),
				"Current project Timeline requires an unregistered track capability"))
				return false;
		}
		if (!Expect(!error, "Failed while scanning current project Timeline assets"))
			return false;
	}
	return Expect(timelineCount > 0, "No current project Timeline assets were validated");
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
			if (iterator->is_directory() && iterator->path().filename() == "Builds")
			{
				iterator.disable_recursion_pending();
				continue;
			}
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
			if (type == Vans::VansAssetType::Unknown || type == Vans::VansAssetType::Scene)
				continue;
			if (!fs::is_regular_file(Vans::VansAssetMeta::MetaPathFor(iterator->path())))
				continue;
            std::string registerError;
            if (!Expect(database.RegisterOrRefresh(iterator->path(), readOnly, registerError),
                "Animation or Timeline dependency asset failed read-only registration"))
                return false;
        }
        if (!Expect(!error, "Failed while registering project animation and Timeline assets"))
            return false;
		Vans::VansAssetObjectRepository objectRepository;
		const Vans::VansAssetObjectBootstrapResult objectBootstrap =
			Vans::VansAssetObjectBootstrapper::Publish(database.All(), objectRepository);
		if (!Expect(static_cast<bool>(objectBootstrap),
			objectBootstrap.errors.empty()
				? "Project animation memory repository bootstrap failed"
				: objectBootstrap.errors.front().c_str()))
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
				if (projectName != "DemoHallProject")
					continue;
                Vans::VansTimelineAsset timeline;
                std::string timelineError;
                if (!Expect(Vans::VansTimelineSerialization::Load(
                    record.sourcePath, timeline, timelineError),
                    "Project Timeline failed canonical loading"))
                {
                    std::cerr << "[ProjectTimeline] " << record.sourcePath.string()
                        << " :: " << timelineError << std::endl;
                    return false;
                }
                Vans::VansTimelineValidationContext validation;
				validation.requireRuntimeCapabilities = true;
				validation.extensions = TimelineCatalog().trackExtensions;
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
			Vans::VansAssetGuid rigGuid;
			const auto rigDependency = Vans::VansAssetGuid::TryParse(animator.animationRigGuid, rigGuid)
				? database.Find(rigGuid) : std::optional<Vans::VansAssetRecord>{};
			if (!Expect(rigDependency && rigDependency->type == Vans::VansAssetType::AnimationRig,
				"Animator Animation Rig GUID does not resolve inside its project"))
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
				if (layer.maskGuid.empty())
				{
					if (!Expect(layer.maskPathHint.empty(),
						"Full-body Animator Overlay has a pathHint without a Bone Mask GUID"))
						return false;
					continue;
				}
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
		if (projectName != "DemoHallProject" && projectName != "AnimationV2Project")
			continue;
        error.clear();
        for (fs::recursive_directory_iterator iterator(scenesRoot, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            if (!iterator->is_regular_file() || iterator->path().extension() != ".json")
                continue;
			Vans::SceneDocumentLoadResult sceneDocumentLoad =
				Vans::VansSceneDocumentLoader::Load(iterator->path());
			if (!Expect(static_cast<bool>(sceneDocumentLoad),
				"Project scene document could not be loaded before dependency analysis"))
				return false;
            const Vans::VansSceneAssetDependencyBuildResult dependencies =
				Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
					database,
					sceneDocumentLoad.document->SerializedRootSnapshot(),
					iterator->path(),
					{},
					objectRepository);
            if (!Expect(dependencies.success,
                "Project scene animation or Timeline dependency closure is incomplete or contains path/cross-project references"))
                return false;
			if (projectName == "AnimationV2Project" &&
				iterator->path().filename() == "MainScene.json" && !Expect(
				dependencies.requiredAssets.find("4dbe6fc1-88c9-4b06-82b5-e4c6c9f37005")
					!= dependencies.requiredAssets.end(),
				"AnimationV2 packaged dependency closure omitted the Survival Animation Rig"))
				return false;
			if (projectName == "DemoHallProject")
			{
				if (!Expect(
					dependencies.requiredAssets.find("65af9371-2a97-43f1-93a8-04dc2e4f1002")
						!= dependencies.requiredAssets.end()
					&& dependencies.requiredModels.find("17ee1769-fddf-4cf8-a134-2d5471e5218c")
						!= dependencies.requiredModels.end()
					&& dependencies.requiredMaterials.find("b34fea44-f13c-4df3-a2c3-743898ba1b04")
						!= dependencies.requiredMaterials.end(),
					"DemoHall packaged dependency closure omitted the Survival back axe Rig, Model, or Material"))
					return false;
				constexpr const char* requiredBackAxeTextures[] = {
					"32a681a1-0468-4d40-beca-13b8a9c814c6",
					"eff6df6d-5b3f-457a-b00c-64990836e4aa",
					"3341e966-03e9-4cde-812e-69c663a3e14a",
					"ba6a0d99-d6fb-4f6f-82d9-04318f635d6d"
				};
				for (const char* guid : requiredBackAxeTextures)
				{
					const std::string message =
						std::string("DemoHall packaged dependency closure omitted Survival back axe Texture ")
						+ guid;
					if (!Expect(dependencies.requiredTextures.find(guid)
						!= dependencies.requiredTextures.end(),
						message.c_str()))
						return false;
				}
			}
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

bool TestAnimationV2RetargetSceneContract()
{
	using namespace VansGraphics;

	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 5 && !fs::exists(workspace / "AnimationV2Project"); ++depth)
	{
		if (!workspace.has_parent_path() || workspace.parent_path() == workspace)
			break;
		workspace = workspace.parent_path();
	}
	const fs::path scenePath =
		workspace / "AnimationV2Project" / "Scenes" / "MainScene.json";
	if (!fs::exists(scenePath))
		return true;

	nlohmann::json scene;
	std::ifstream input(scenePath);
	if (!Expect(input.good(), "AnimationV2 MainScene could not be opened"))
		return false;
	try
	{
		input >> scene;
	}
	catch (...)
	{
		return Expect(false, "AnimationV2 MainScene is not valid JSON");
	}

	std::unordered_set<std::string> validatedCharacters;
	if (!Expect(scene.contains("entities") && scene.at("entities").is_array(),
		"AnimationV2 MainScene has no entities array"))
	{
		return false;
	}
	for (const nlohmann::json& entity : scene.at("entities"))
	{
		for (const nlohmann::json& component :
			 entity.value("components", nlohmann::json::array()))
		{
			if (component.value("type", "") != "Animation" ||
				!component.contains("data") || !component["data"].is_object())
			{
				continue;
			}
			const nlohmann::json& animation = component["data"];
			const std::string name = animation.value("name", "");
			if (name != "TwinBlast" && name != "SWAT" && name != "Survival")
				continue;
			if (!Expect(animation.contains("retarget")
				&& animation.contains("rig")
				&& animation.value("root_motion", false)
				&& animation.at("retarget").value("enabled", false)
				&& !animation.at("retarget").contains("runtime_mode")
				&& !animation.at("retarget").contains("cache_policy")
				&& !animation.contains("foot_placement"),
				"AnimationV2 retargeted character configuration is incomplete"))
			{
				return false;
			}
			validatedCharacters.insert(name);
		}
	}
	if (!Expect(validatedCharacters.size() == 3,
		"AnimationV2 must configure TwinBlast, SWAT, and Survival for retargeted root motion"))
	{
		return false;
	}

	constexpr const char* kSurvivalEntityGuid = "0c3a065a-e695-4f25-b8d5-6d83bcce00af";
	constexpr const char* kSurvivalAnimationGuid = "b838475c-7e7f-479a-b677-e5dea83c2fff";
	constexpr const char* kBackAxeSocketGuid = "5e6b4b56-6b5a-464e-9257-29129b7581cf";
	constexpr const char* kBackAxeEntityGuid = "24b30a8b-ad95-423f-9125-61fd1fda15e7";
	const nlohmann::json* backAxeEntity = nullptr;
	const nlohmann::json* backAxeMeshEntity = nullptr;
	for (const nlohmann::json& entity : scene.at("entities"))
	{
		const std::string name = entity.value("name", "");
		if (name == "Survival_Back_Axe")
			backAxeEntity = &entity;
		else if (name == "Survival_Back_Axe_metal-low")
			backAxeMeshEntity = &entity;
	}
	if (!Expect(backAxeEntity && backAxeMeshEntity,
		"AnimationV2 Survival back axe Object hierarchy is missing"))
	{
		return false;
	}
	const nlohmann::json& axeParent = backAxeEntity->at("parent");
	bool hasIdentityTransform = false;
	bool hasAxeModel = false;
	for (const nlohmann::json& component : backAxeEntity->at("components"))
	{
		const nlohmann::json& data = component.at("data");
		if (component.value("type", "") == "Transform")
		{
			hasIdentityTransform = data.at("position") == nlohmann::json::array({ 0, 0, 0 })
				&& data.at("rotation") == nlohmann::json::array({ 0, 0, 0, 1 })
				&& data.at("scale") == nlohmann::json::array({ 1, 1, 1 });
		}
		else if (component.value("type", "") == "MultiMeshRoot")
		{
			hasAxeModel = data.at("model").value("guid", "")
				== "17ee1769-fddf-4cf8-a134-2d5471e5218c";
		}
	}
	const nlohmann::json& axeMeshParent = backAxeMeshEntity->at("parent");
	if (!Expect(backAxeEntity->value("id", "") == kBackAxeEntityGuid
		&& axeParent.value("kind", "") == "socket"
		&& axeParent.value("entityGuid", "") == kSurvivalEntityGuid
		&& axeParent.value("animationComponentGuid", "") == kSurvivalAnimationGuid
		&& axeParent.value("anchorGuid", "") == kBackAxeSocketGuid
		&& axeMeshParent.value("kind", "") == "entity"
		&& axeMeshParent.value("entityGuid", "") == kBackAxeEntityGuid
		&& hasIdentityTransform && hasAxeModel,
		"AnimationV2 axe must be attached to the Survival Back_Axe Socket with identity local Transform"))
	{
		return false;
	}

	VansAnimationRigAsset survivalRig;
	std::string survivalRigError;
	if (!Expect(VansAnimationRigStorage::Load(
		workspace / "AnimationV2Project" / "Assets" / "AnimationRigs" / "Survival.vanimrig",
		survivalRig, survivalRigError), survivalRigError.c_str()))
	{
		return false;
	}
	const auto backAxeSocket = std::find_if(
		survivalRig.sockets.begin(), survivalRig.sockets.end(),
		[](const VansRigSocketDefinition& socket) { return socket.name == "Back_Axe"; });
	if (!Expect(backAxeSocket != survivalRig.sockets.end()
		&& backAxeSocket->guid == kBackAxeSocketGuid
		&& backAxeSocket->boneGuid == "0aab6b03-caaa-5326-8521-c0f4f380e3bd"
		&& glm::length(backAxeSocket->scaleLocal - glm::vec3(1.0f)) <= 1.0e-5f,
		"AnimationV2 Survival Back_Axe Socket must resolve to Survival spine_04"))
	{
		return false;
	}

	const fs::path survivalModelPath = workspace / "AnimationV2Project" / "Assets"
		/ "Characters" / "Survival" / "Models" / "survival_character.fbx";
	Vans::VansAssetMeta survivalModelMeta;
	std::string survivalModelError;
	if (!Expect(Vans::VansAssetMetaStorage::Load(
		Vans::VansAssetMeta::MetaPathFor(survivalModelPath),
		survivalModelMeta, survivalModelError), survivalModelError.c_str()))
	{
		return false;
	}
	Assimp::Importer survivalImporter;
	const aiScene* survivalScene = survivalImporter.ReadFile(
		survivalModelPath.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
	if (!Expect(survivalScene != nullptr, survivalImporter.GetErrorString()))
		return false;
	Skeleton survivalSkeleton;
	VansSkinnedMeshLoader::ExtractSkeleton(survivalScene, survivalSkeleton, 1.0f,
		Vans::ReadSkeletalMeshImportSettings(survivalModelMeta));
	VansCompiledAnimationRig compiledSurvivalRig;
	std::string compileSurvivalRigError;
	if (!Expect(VansAnimationRigCompiler::Compile(
		survivalRig, survivalSkeleton, compiledSurvivalRig, compileSurvivalRigError),
		compileSurvivalRigError.c_str()))
	{
		return false;
	}
	const int compiledSocketIndex = compiledSurvivalRig.FindSocketByGuid(kBackAxeSocketGuid);
	if (!Expect(compiledSocketIndex >= 0,
		"AnimationV2 Survival Back_Axe Socket did not compile against the Survival Skeleton"))
	{
		return false;
	}
	std::vector<glm::mat4> survivalBindModelTransforms(
		survivalSkeleton.bones.size(), glm::mat4(1.0f));
	for (const int boneIndex : survivalSkeleton.topologicalOrder)
	{
		const BoneInfo& bone = survivalSkeleton.bones[static_cast<std::size_t>(boneIndex)];
		survivalBindModelTransforms[static_cast<std::size_t>(boneIndex)] = bone.localTransform;
		if (bone.parentIndex >= 0)
		{
			survivalBindModelTransforms[static_cast<std::size_t>(boneIndex)] =
				survivalBindModelTransforms[static_cast<std::size_t>(bone.parentIndex)]
				* bone.localTransform;
		}
	}
	const VansCompiledRigSocket& compiledSocket =
		compiledSurvivalRig.sockets[static_cast<std::size_t>(compiledSocketIndex)];
	const glm::mat4 socketBindModelTransform =
		survivalBindModelTransforms[static_cast<std::size_t>(compiledSocket.boneIndex)]
		* compiledSocket.localTransform;
	const glm::vec3 axeMeshCenter = glm::vec3(socketBindModelTransform
		* glm::vec4(-14.579931f, 114.222820f, 19.733490f, 1.0f));
	const float axePlacementError =
		glm::length(axeMeshCenter - glm::vec3(0.0f, 125.0f, -14.0f));
	if (axePlacementError > 0.01f)
	{
		std::cerr << "[ForestContractTests] Survival Back_Axe bind center actual=("
			<< axeMeshCenter.x << ", " << axeMeshCenter.y << ", " << axeMeshCenter.z
			<< ") error=" << axePlacementError << std::endl;
	}
	if (!Expect(axePlacementError <= 0.01f,
		"AnimationV2 Survival Back_Axe Socket no longer places the axe diagonally on the upper back"))
	{
		return false;
	}

	struct RetargetProfileExpectation
	{
		const char* fileName;
		const char* profileName;
	};
	const RetargetProfileExpectation profileExpectations[] = {
		{"RTG_UEFN_To_Survival.vretarget", "RTG_UEFN_To_Survival"},
		{"RTG_UEFN_To_SWAT.vretarget", "RTG_UEFN_To_SWAT"}
	};
	for (const RetargetProfileExpectation& expectation : profileExpectations)
	{
		const fs::path profilePath = workspace / "AnimationV2Project" / "Assets"
			/ "Retarget" / expectation.fileName;
		VansRetargetProfileAsset profile;
		std::string profileError;
		if (!Expect(VansRetargetProfileStorage::Load(profilePath, profile, profileError),
			profileError.c_str()))
			return false;
		auto validArmChain = [&profile](std::size_t index, const char* name,
			const char* upper, const char* lower, const char* hand)
		{
			if (index >= profile.limbChains.size())
				return false;
			const VansRetargetLimbChainDesc& chain = profile.limbChains[index];
			return chain.name == name
				&& chain.sourceRoot == upper && chain.sourceMid == lower && chain.sourceTip == hand
				&& chain.targetChainId == (index == 0 ? "leftArm" : "rightArm")
				&& chain.positionWeight == 1.0f;
		};
		if (!Expect(profile.name == expectation.profileName
			&& profile.translationScaleMode == VansRetargetTranslationScaleMode::CompatibleSkeleton
			&& profile.targetModelSpaceAlignment == VansRetargetModelSpaceAlignment::SourceBindPose
			&& profile.rootAlignment == VansRetargetRootAlignment::FeetToOwner
			&& profile.limbChains.size() == 2
			&& validArmChain(0, "LeftArm", "upperarm_l", "lowerarm_l", "hand_l")
			&& validArmChain(1, "RightArm", "upperarm_r", "lowerarm_r", "hand_r"),
			"AnimationV2 Survival/SWAT retarget profile is incomplete or inconsistent"))
		{
			return false;
		}
	}

	AnimatorAssetData sourceAnimator;
	const fs::path sourceAnimatorPath = workspace / "AnimationV2Project" / "Assets"
		/ "MotionMatchDataBase" / "UEFN_Mannequin.vanimator";
	if (!Expect(VansAnimatorIO::Load(sourceAnimatorPath.string(), sourceAnimator),
		"AnimationV2 UEFN Motion Matching source Animator could not be loaded"))
	{
		return false;
	}
	const std::size_t pivotClipCount = static_cast<std::size_t>(std::count_if(
		sourceAnimator.clipRefs.begin(), sourceAnimator.clipRefs.end(),
		[](const AnimatorClipRef& clip)
		{
			return clip.name.find("Pivot") != std::string::npos;
		}));
	return Expect(sourceAnimator.clipRefs.size() >= 195 && pivotClipCount == 60,
		"AnimationV2 UEFN source Animator is missing the canonical Pivot clip set");
}

bool TestSurvivalPistolPoseContract(const char* projectName, const fs::path& assetDirectory)
{
    // 使用正式导入器、采样器和重定向器检查姿态，不向项目写临时证据。
    fs::path workspace = fs::current_path();
    for (int depth = 0; depth < 6 && !fs::exists(workspace / projectName); ++depth)
        workspace = workspace.parent_path();
    const fs::path project = workspace / projectName;
    if (!Expect(fs::exists(project), "Pistol test project is missing")) return false;
    const fs::path assets = project / assetDirectory;
    const fs::path model = assets / "Characters/Survival/Models/survival_character.fbx";
    Vans::VansAssetMeta meta;
    std::string error;
    if (!Expect(Vans::VansAssetMetaStorage::Load(Vans::VansAssetMeta::MetaPathFor(model), meta, error), error.c_str())) return false;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(model.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
    if (!Expect(scene != nullptr, importer.GetErrorString())) return false;
    Skeleton target;
    VansSkinnedMeshLoader::ExtractSkeleton(scene, target, 1.0f, Vans::ReadSkeletalMeshImportSettings(meta));
    VansAnimationRigAsset rig;
    if (!Expect(VansAnimationRigStorage::Load(assets / "AnimationRigs/Survival.vanimrig", rig, error), error.c_str())) return false;
    VansCompiledAnimationRig compiled;
    if (!Expect(VansAnimationRigCompiler::Compile(rig, target, compiled, error), error.c_str())) return false;
    VansRetargetProfileAsset profile;
    if (!Expect(VansRetargetProfileStorage::Load(assets / "Retarget/RTG_UEFN_To_Survival.vretarget", profile, error), error.c_str())) return false;
    VansRetargetRuntimeDesc desc;
    desc.translationScaleMode = profile.translationScaleMode;
    desc.translationScale = profile.explicitTranslationScale;
    desc.rootAlignment = profile.rootAlignment;
    desc.targetModelSpaceAlignment = profile.targetModelSpaceAlignment;
    desc.limbChains = profile.limbChains;
    for (const std::string file : { "AS_idle_to_aim_Unreal_Take.vclip", "AS_aim_to_idle_Unreal_Take.vclip", "Pistol_Shot.vclip" })
    {
        VansAnimationClip clip;
        Skeleton source;
        if (!Expect(VansAnimationClipIO::Load((assets / "Animations/Combat/Pistol" / file).string(), clip, source), "Pistol clip load failed")) return false;
        VansRetargetProcessor retarget;
        if (!Expect(retarget.Build(source, target, compiled, desc), "Pistol retarget build failed")) return false;
        for (int index = 0; index <= 20; ++index)
        {
            VansAnimationSampleRequest request;
            request.currentTime = clip.duration * static_cast<float>(index) / 20.0f;
            request.endTime = clip.duration;
            request.loop = false;
            VansPosePayload pose;
            if (!Expect(VansAnimationSampler::Sample(clip, source, request, pose), "Pistol sample failed")) return false;
            std::vector<glm::mat4> models;
            VansPoseMath::ToMatrices(pose.localPose, models);
            for (int bone : source.topologicalOrder)
                if (source.bones[bone].parentIndex >= 0) models[bone] = models[source.bones[bone].parentIndex] * models[bone];
            std::vector<glm::mat4> targetModels;
            if (!Expect(retarget.Process(models, source, target, targetModels), "Pistol retarget sample failed")) return false;
            for (std::size_t bone = 0; bone < target.bones.size(); ++bone)
            {
                for (int row = 0; row < 4; ++row)
                {
                    for (int column = 0; column < 4; ++column)
                    {
                        const float value = targetModels[bone][column][row];
                        if (!Expect(std::isfinite(value), "Pistol retarget produced a non-finite transform")) return false;
                    }
                }
            }
        }
    }
    return true;
}

bool TestSurvivalPistolOverlayContract(const char* projectName, const fs::path& assetDirectory, const char* sceneFile)
{
    // 正式项目的同一套输入分别进入基础图与叠层图，比较实际 MM 和重定向输出。
    fs::path workspace = fs::current_path();
    for (int depth = 0; depth < 6 && !fs::exists(workspace / projectName); ++depth)
        workspace = workspace.parent_path();
    const fs::path project = workspace / projectName;
    if (!Expect(fs::exists(project), "Pistol test project is missing")) return false;
    const fs::path assets = project / assetDirectory;
    std::string error;
    AnimatorAssetData asset, baselineAsset;
    if (!Expect(VansAnimatorIO::Load((assets / "MotionMatchDataBase/UEFN_Mannequin.vanimator").string(), asset), "Pistol Animator load failed")) return false;
    nlohmann::json baselineJson;
    if (!Expect(VansAnimatorIO::SerializeToJsonObject(asset, baselineJson, error), error.c_str())) return false;
    // This contract isolates the pre-existing pistol overlay path.  Survival
    // owns a separate attack layer and is validated by the GAF attack contract.
    baselineJson["layers"].erase(std::remove_if(baselineJson["layers"].begin(),
        baselineJson["layers"].end(), [](const auto& layer)
        {
            return layer.value("id", std::string{}) == "layer-survival-attack-upper";
        }), baselineJson["layers"].end());
    for (auto& set : baselineJson["graphSets"])
    {
        auto& bindings = set["bindings"];
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
            [](const auto& binding)
            {
                return binding.value("layerId", std::string{}) == "layer-survival-attack-upper";
            }), bindings.end());
    }
    if (!Expect(VansAnimatorIO::DeserializeFromJsonObject(baselineJson, asset, error), error.c_str())) return false;
    // The production animator may contain more than one overlay layer.  Build
    // the baseline from the base layer explicitly so adding Survival's attack
    // overlay cannot accidentally turn it into a second overlay fixture.
    const auto baseLayer = baselineJson["layers"].at(0);
    baselineJson["layers"] = nlohmann::json::array({ baseLayer });
    for (auto& set : baselineJson["graphSets"])
    {
        auto& bindings = set["bindings"];
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
            [](const auto& binding)
            {
                return binding.value("layerId", std::string{}) != "layer-base";
            }), bindings.end());
    }
    for (auto& rule : baselineJson["graphSetTransitions"]["rules"]) rule["policy"]["phase"] = "restart";
    if (!Expect(VansAnimatorIO::DeserializeFromJsonObject(baselineJson, baselineAsset, error), error.c_str())) return false;
    std::unordered_map<std::string, std::shared_ptr<const VansAnimationClipAsset>> clips;
    for (const auto& ref : asset.clipRefs)
    {
        auto clip = std::make_shared<VansAnimationClipAsset>();
        if (!Expect(VansAnimationClipIO::Load((project / ref.pathHint).string(), clip->clip, clip->skeleton), ref.pathHint.c_str())) return false;
        clips.emplace(ref.assetGuid, std::move(clip));
    }
    const auto raiseRef = std::find_if(asset.clipRefs.begin(), asset.clipRefs.end(), [](const auto& ref) { return ref.name == "Pistol_Raise"; });
    if (!Expect(raiseRef != asset.clipRefs.end(), "Missing raise clip")) return false;
    const Skeleton& source = clips.at(raiseRef->assetGuid)->skeleton;
    auto rig = std::make_shared<VansAnimationRigAsset>();
    auto mask = std::make_shared<VansBoneMaskAsset>();
    if (!Expect(VansAnimationRigStorage::Load(assets / "AnimationRigs/UEFN.vanimrig", *rig, error)
        && VansBoneMaskStorage::Load(assets / "Animations/Combat/Pistol/Pistol_UpperBody.vbonemask", *mask, error), error.c_str())) return false;
    VansAnimatorRuntimeCompileOptions options;
    options.enableTargetPostProcess = false;
    options.enableRootMotion = true;
    options.rigResolver = [rig](const auto&, auto&) { return rig; };
    auto clipResolver = [&clips](const AnimatorClipRef& ref, std::shared_ptr<const VansAnimationClipAsset>& out, std::string&) { out = clips.at(ref.assetGuid); return true; };
    auto maskResolver = [mask](const VansAnimationLayerDefinition&, std::shared_ptr<const VansBoneMaskAsset>& out, std::string&) { out = mask; return true; };
    auto baseline = VansAnimatorRuntimeCompiler::Compile(baselineAsset, source, clipResolver, maskResolver, options, error);
    auto layered = VansAnimatorRuntimeCompiler::Compile(asset, source, clipResolver, maskResolver, options, error);
    if (!Expect(baseline && layered, error.c_str())) return false;
    nlohmann::json sceneJson;
    std::ifstream(project / "Scenes" / sceneFile) >> sceneJson;
    std::optional<MotionMatchingSettings> settings;
    for (const auto& entity : sceneJson["entities"])
        for (const auto& component : entity["components"])
            if (component.value("id", "") == "87f6e3d4-c8fe-458d-82f2-3e56f20b93e5")
                settings = Vans::VansSceneAnimationComponentReader::ReadAuthoringAnimationComponent(Vans::DecodeSerializedValueJson(component)).motionMatching;
    if (!Expect(settings && settings->enabled, "Survival MM settings were not read")) return false;
    if (!Expect(baseline->ConfigureMotionMatching(*settings, error) && layered->ConfigureMotionMatching(*settings, error), error.c_str())) return false;
    const fs::path model = assets / "Characters/Survival/Models/survival_character.fbx";
    Vans::VansAssetMeta meta;
    if (!Expect(Vans::VansAssetMetaStorage::Load(Vans::VansAssetMeta::MetaPathFor(model), meta, error), error.c_str())) return false;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(model.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
    if (!Expect(scene != nullptr, importer.GetErrorString())) return false;
    Skeleton target;
    VansSkinnedMeshLoader::ExtractSkeleton(scene, target, 1.0f, Vans::ReadSkeletalMeshImportSettings(meta));
    VansAnimationRigAsset targetRig;
    VansCompiledAnimationRig compiledRig;
    VansRetargetProfileAsset profile;
    if (!Expect(VansAnimationRigStorage::Load(assets / "AnimationRigs/Survival.vanimrig", targetRig, error)
        && VansAnimationRigCompiler::Compile(targetRig, target, compiledRig, error)
        && VansRetargetProfileStorage::Load(assets / "Retarget/RTG_UEFN_To_Survival.vretarget", profile, error), error.c_str())) return false;
    VansRetargetRuntimeDesc desc;
    desc.translationScaleMode = profile.translationScaleMode; desc.translationScale = profile.explicitTranslationScale;
    desc.rootAlignment = profile.rootAlignment; desc.targetModelSpaceAlignment = profile.targetModelSpaceAlignment; desc.limbChains = profile.limbChains;
    // 使用面板同一条参数编辑入口，验证真实 Pistol 图经过 Node 的 Retarget 同步后仍然生效。
    if (std::string(projectName) == "DemoHallProject")
    {
        using namespace Vans::EditorAPI;
        auto previewSource = VansAnimatorRuntimeCompiler::Compile(asset, source, clipResolver, maskResolver, options, error);
        auto previewTarget = std::make_unique<VansAnimationController>();
        if (!Expect(previewSource && previewTarget->SetAnimationRig(compiledRig, {}, error), error.c_str())) return false;
        previewTarget->ReplaceParameterDefinitions(*previewSource, false);
        VansAnimationController* previewTargetView = previewTarget.get();
        VansAnimationNode previewNode("PistolParameterPreviewContract");
        previewNode.SetSkeleton(target);
        if (!Expect(previewNode.SetController(std::move(previewTarget)) && previewNode.ConfigureRetargetSource(
            source, std::move(previewSource), desc, error), error.c_str())) return false;
        previewNode.Play(VansAnimationEvaluationPurpose::EditorPreview);
        auto* motion = previewNode.GetCharacterMotionController();
        auto step = [&](int frames) { for (int i=0; i<frames; ++i)
            previewNode.Update({VansAnimationEvaluationPurpose::EditorPreview, 1.0f/60.0f}); };
        // 旧面板写入 Source，下一帧会被组件里的默认值覆盖。
        motion->SetInt("PistolPhase", 2);
        motion->SetFloat("PistolUpperBodyWeight", 1.0f);
        step(1);
        if (!Expect(motion->GetInt("PistolPhase") == 0 && motion->GetFloat("PistolUpperBodyWeight") == 0,
            "Retarget parameter overwrite reproducer no longer matches the component contract")) return false;
        const int hand = previewNode.GetSkeleton().boneNameToIndex.at("hand_r");
        const auto idleHand = glm::vec3(previewTargetView->GetCachedGlobalTransform(hand)[3]);
        auto setFloat = [&](const char* name, float number)
        {
            AnimationPreviewParameterValue value;
            value.name=name; value.type=AnimationPreviewParameterType::Float; value.floatValue=number;
            return AnimationPreviewParameterEditing::Apply(previewNode,value);
        };
        auto setPhase = [&](int phase)
        {
            AnimationPreviewParameterValue value;
            value.name="PistolPhase"; value.type=AnimationPreviewParameterType::Int; value.intValue=phase;
            return AnimationPreviewParameterEditing::Apply(previewNode,value);
        };
        AnimationPreviewParameterValue mode;
        mode.name="UseMotionMatching"; mode.type=AnimationPreviewParameterType::Bool; mode.boolValue=false;
        AnimationPreviewParameterValue direction;
        direction.name="PistolAimDirection"; direction.type=AnimationPreviewParameterType::Vector3; direction.vectorValue={0,1,0};
        if (!Expect(AnimationPreviewParameterEditing::Apply(previewNode,mode)
            && AnimationPreviewParameterEditing::Apply(previewNode,direction)
            && setFloat("PistolUpperBodyWeight",1) && setFloat("PistolGripWeight",1) && setPhase(2),
            "Preview parameter edits were rejected")) return false;
        step(30);
        auto debug = motion->GetLayerRuntimeDebugInfo();
        const float handTravel = glm::length(glm::vec3(previewTargetView->GetCachedGlobalTransform(hand)[3]) - idleHand);
        if (!Expect(debug.size()==2 && debug[1].state=="PistolAim" && debug[1].weight>0.99f && handTravel>1.0f
            && motion->GetInt("PistolPhase")==2 && previewTargetView->GetFloat("PistolGripWeight")==1.0f
            && motion->GetFloat("PistolGripWeight")==1.0f && !motion->GetBool("UseMotionMatching")
            && glm::length(motion->GetVector3("PistolAimDirection")-glm::vec3(0,1,0))<1.e-6f,
            "Preview parameter values did not drive the Aim state, target hand pose, or IK owner")) return false;
        AnimationPreviewParameterValue trigger;
        trigger.name="PistolActionStart"; trigger.type=AnimationPreviewParameterType::Trigger;
        if (!Expect(setPhase(3) && AnimationPreviewParameterEditing::Apply(previewNode,trigger),
            "Shot preview request was rejected")) return false;
        step(6);
        debug = motion->GetLayerRuntimeDebugInfo();
        if (!Expect(debug[1].state=="PistolShot" && debug[1].playbackTime>0.01f
            && !previewTargetView->IsTriggerSet("PistolActionStart"), "Shot trigger did not reach the source state machine exactly once")) return false;
        step(12);
        const float completedTime = motion->GetLayerRuntimeDebugInfo()[1].playbackTime;
        if (!Expect(AnimationPreviewParameterEditing::Apply(previewNode,trigger), "Repeated shot was rejected")) return false;
        step(2);
        if (!Expect(motion->GetLayerRuntimeDebugInfo()[1].playbackTime<completedTime,
            "Repeated Shot preview did not restart playback")) return false;
        if (!Expect(setFloat("PistolGripWeight",0.25f), "Paused grip edit was rejected")) return false;
        previewNode.Update({VansAnimationEvaluationPurpose::EditorPreview,0.0f});
        if (!Expect(previewTargetView->GetFloat("PistolGripWeight")==0.25f && motion->GetFloat("PistolGripWeight")==0.25f,
            "Paused frame lost the edited IK weight")) return false;
        // 无重定向的组件与独立预览继续写自己的控制器。
        auto directController = std::make_unique<VansAnimationController>();
        directController->AddParameter("PistolPhase",VansGraphics::AnimatorParamType::Int);
        VansAnimationController* directControllerView = directController.get();
        VansAnimationNode directNode("DirectParameterPreviewContract");
        if (!Expect(directNode.SetController(std::move(directController)), "Direct preview fixture setup failed")) return false;
        AnimationPreviewParameterValue directValue;
        directValue.name="PistolPhase"; directValue.type=AnimationPreviewParameterType::Int; directValue.intValue=2;
        if (!Expect(AnimationPreviewParameterEditing::Apply(directNode,directValue) && directControllerView->GetInt("PistolPhase")==2,
            "Direct scene preview parameter routing changed")) return false;
        directValue.intValue=3;
        if (!Expect(AnimationPreviewParameterEditing::Apply(*directControllerView,directValue) && directControllerView->GetInt("PistolPhase")==3,
            "Isolated preview parameter routing changed")) return false;
        std::cout << "[PistolPreviewParameters] old source write overwritten; Aim/Shot/retrigger/paused IK passed; target hand travel="
            << handTravel << '\n';
    }
    VansRetargetProcessor baseRetarget, layerRetarget;
    if (!Expect(baseRetarget.Build(source, target, compiledRig, desc) && layerRetarget.Build(source, target, compiledRig, desc), "Retarget build failed")) return false;
    auto matrixError = [](const glm::mat4& a, const glm::mat4& b)
    {
        float result = 0.0f;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) result = std::max(result, std::abs(a[c][r] - b[c][r]));
        return result;
    };
    auto lowerBone = [](const std::string& name)
    {
        return name == "root" || name == "pelvis" || name.find("thigh") == 0 || name.find("calf") == 0
            || name.find("foot") == 0 || name.find("ball") == 0 || name.find("ankle") == 0 || name.find("ik_foot") == 0;
    };
    // 正式目标后处理图独立接收重定向姿态，验证肩带、头部和 MM 隔离。
    VansAnimationController aimController, aimOffController;
    nlohmann::json postJson;
    asset.FindTargetPostProcessGraph()->SerializeToJsonObject(postJson);
    for (auto* controller : { &aimController, &aimOffController })
    {
        controller->AddParameter("PistolAimDirection", AnimatorParamType::Vector3);
        controller->AddParameter("PistolAimWeight", AnimatorParamType::Float);
        if (!Expect(controller->SetAnimationRig(compiledRig,
                [](const std::string&, std::uint32_t& mask, std::string&) { mask = 1; return true; }, error)
            && controller->SetTargetPostProcessGraph(VansAnimGraph::DeserializeFromJsonObject(postJson), error), error.c_str())) return false;
    }
    const int aimLeft = target.boneNameToIndex.at("hand_l"), aimRight = target.boneNameToIndex.at("hand_r");
    const int aimHead = target.boneNameToIndex.at("head"), aimChest = target.boneNameToIndex.at("spine_05");
    float aimLowerError = 0.0f, aimDisabledError = 0.0f, aimGripError = 0.0f, aimMaxChestAngle = 0.0f;
    float aimHeadHandError = 0.0f, aimSteadyPitchError = 0.0f;
    auto rotationOf = [](const glm::mat4& value)
    {
        VansBoneTransform decomposed;
        VansPoseMath::TryDecompose(value, decomposed);
        return decomposed.rotation;
    };
    float sourceLowerError = 0.0f, targetLowerError = 0.0f, offError = 0.0f, upperDifference = 0.0f, rootError = 0.0f;
    int mmFrames = 0, crouchFrames = 0;
    std::set<std::string> activeClips;
    std::unordered_map<std::string, int> events;
    Vans::VansCharacterTrajectory trajectory;
    trajectory.valid = trajectory.hasFacing = trajectory.hasGrounding = true;
    baseline->SetCharacterTrajectory(&trajectory); layered->SetCharacterTrajectory(&trajectory);
    baseline->Play(); layered->Play();
    std::string previousUpperState;
    float previousUpperTime = 0.0f;
    int previousPhase = 0;
    constexpr float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 1200; ++frame)
    {
        const bool crouching = frame >= 900 && frame < 1080;
        const bool stanceClip = (frame >= 840 && frame < 900) || (frame >= 1020 && frame < 1080);
        const float speed = frame < 60 || stanceClip || frame >= 1140 ? 0.0f : (crouching ? 2.1f : 2.5f);
        const float direction = frame < 300 ? 0.0f : frame < 510 ? 1.5707963f : frame < 780 ? 3.1415926f : 0.0f;
        trajectory.currentFacingYaw = frame * .1f;
        trajectory.desiredFacingYaw = trajectory.currentFacingYaw + 20.0f;
        trajectory.plannedFacingYaw = trajectory.currentFacingYaw + .2f;
        trajectory.movementReferenceYaw = trajectory.currentFacingYaw;
        trajectory.moveInputLocal = glm::vec2(-std::sin(direction), std::cos(direction));
        trajectory.currentVelocityWorld = Vans::LocomotionLocalToWorldPlanar(glm::vec3(trajectory.moveInputLocal.x, 0.0f, trajectory.moveInputLocal.y) * speed, trajectory.currentFacingYaw);
        trajectory.desiredVelocityWorld = trajectory.plannedVelocityWorld = trajectory.requestedVelocityWorld = trajectory.currentVelocityWorld;
        trajectory.originWorld += trajectory.currentVelocityWorld * dt;
        for (int sample = 0; sample < 3; ++sample)
        {
            auto& point = trajectory.future[sample]; point.time = settings->schema.futureTimes[sample];
            point.positionWorld = trajectory.originWorld + trajectory.currentVelocityWorld * point.time;
            point.velocityWorld = trajectory.currentVelocityWorld; point.facingYaw = trajectory.currentFacingYaw + 12.0f * point.time;
        }
        const int phase = frame < 120 ? 0 : frame < 300 ? 1 : frame < 360 ? 2 : frame < 450 ? 3
            : frame < 510 ? 2 : frame < 690 ? 4 : frame < 780 ? 0 : frame < 960 ? 1 : frame < 990 ? 2 : frame < 1110 ? 3 : 0;
        layered->SetInt("PistolPhase", phase); layered->SetFloat("PistolUpperBodyWeight", phase == 0 ? 0.0f : 1.0f);
        if (phase != previousPhase && (phase == 1 || phase == 3 || phase == 4)) layered->SetTrigger("PistolActionStart");
        previousPhase = phase;
        for (auto* controller : {baseline.get(), layered.get()})
        {
            controller->SetBool("UseMotionMatching", true); controller->SetFloat("Speed", speed / 7.0f);
            controller->SetFloat("Direction", direction); controller->SetFloat("IsCrouching", crouching ? 1.0f : 0.0f);
            controller->SetInt("MoveState", crouching ? 4 : speed > 0.0f ? 1 : 0);
            controller->SetOwnerWorldTransform(glm::translate(glm::mat4(1.0f), trajectory.originWorld));
            if (frame == 840) controller->SwitchGraphSet("graph-set-crouch-enter");
            if (frame == 1020) controller->SwitchGraphSet("graph-set-crouch-exit");
            if (frame == 900 || frame == 1080) controller->SwitchGraphSet("graph-set-default");
            controller->Update(dt, source);
        }
        const auto* a = baseline->GetMotionMatchingDebugData(); const auto* b = layered->GetMotionMatchingDebugData();
        if (!Expect(a && b && a->databaseReady && b->databaseReady, "Actual MM database was not built")) return false;
        if (!Expect(a->usedThisFrame == b->usedThisFrame && a->activeClip == b->activeClip
            && std::abs(a->activeTime - b->activeTime) < 1.e-6f && std::abs(a->poseCost - b->poseCost) < 1.e-6f
            && std::abs(a->currentCost - b->currentCost) < 1.e-6f, ("Overlay changed MM state at frame " + std::to_string(frame)).c_str())) return false;
        if (!stanceClip && !baseline->IsGraphSetTransitioning())
        {
            if (!Expect(a->usedThisFrame, "MM stopped evaluating under the pistol layer")) return false;
            ++mmFrames; activeClips.insert(a->activeClip);
        }
        if (stanceClip) ++crouchFrames;
        const auto debug = layered->GetLayerRuntimeDebugInfo();
        if (!Expect(debug.size() == 2, "Pistol layer missing")) return false;
        const auto& upper = debug[1];
        if (upper.state == previousUpperState && (upper.state == "PistolRaise" || upper.state == "PistolShot"))
            if (!Expect(upper.playbackTime + .001f >= previousUpperTime, "Crouch switch restarted the upper-body clip")) return false;
        previousUpperState = upper.state; previousUpperTime = upper.playbackTime;
        for (const auto& event : layered->GetSampledEvents()) ++events[std::string(event.name)];
        rootError = std::max(rootError, glm::length(baseline->GetRootMotionDelta() - layered->GetRootMotionDelta()));
        rootError = std::max(rootError, glm::length(baseline->GetRootRotationDelta() - layered->GetRootRotationDelta()));
        const auto& basePose = baseline->GetCachedGlobalTransforms(); const auto& layerPose = layered->GetCachedGlobalTransforms();
        for (std::size_t bone = 0; bone < source.bones.size(); ++bone)
        {
            const float difference = matrixError(basePose[bone], layerPose[bone]);
            if (lowerBone(source.bones[bone].name)) sourceLowerError = std::max(sourceLowerError, difference);
            if (frame < 120 || frame >= 1160) offError = std::max(offError, difference);
            if (source.bones[bone].name == "hand_r") upperDifference = std::max(upperDifference, difference);
        }
        std::vector<glm::mat4> baseTarget, layerTarget;
        if (!Expect(baseRetarget.Process(basePose, source, target, baseTarget) && layerRetarget.Process(layerPose, source, target, layerTarget), "Layered retarget failed")) return false;
        for (std::size_t bone = 0; bone < target.bones.size(); ++bone)
            if (lowerBone(target.bones[bone].name)) targetLowerError = std::max(targetLowerError, matrixError(baseTarget[bone], layerTarget[bone]));
        const float pitchSamples[] = { -80.0f, -30.0f, 0.0f, 30.0f, 80.0f };
        const float requestedPitch = pitchSamples[(frame / 120) % 5];
        const bool aimActive = phase != 0 && !stanceClip;
        VansAnimationExternalInputSnapshot aimInput;
        aimInput.grounded = false;
        aimInput.airborne = true;
        aimInput.ownerWorld = glm::translate(glm::mat4(1.0f), trajectory.originWorld)
            * glm::mat4_cast(glm::angleAxis(glm::radians(trajectory.currentFacingYaw), glm::vec3(0,1,0)))
            * glm::mat4_cast(glm::angleAxis(glm::radians(-90.0f), glm::vec3(1,0,0)))
            * glm::scale(glm::mat4(1.0f), glm::vec3(.01f));
        const auto modelDirection = glm::vec3(0, -std::cos(glm::radians(requestedPitch)), std::sin(glm::radians(requestedPitch)));
        const auto worldDirection = glm::normalize(glm::vec3(aimInput.ownerWorld * glm::vec4(modelDirection, 0)));
        for (auto* controller : { &aimController, &aimOffController })
        {
            controller->SetAnimationExternalInput(aimInput);
            controller->SetVector3("PistolAimDirection", worldDirection);
            controller->SetFloat("PistolAimWeight", controller == &aimController && aimActive ? 1.0f : 0.0f);
            if (!Expect(controller->SubmitExternalModelPose(layerTarget, target, dt,
                VansExternalPoseEvaluationMode::TargetPostProcess), "Pistol target Aim evaluation failed")) return false;
        }
        const auto& aimed = aimController.GetCachedGlobalTransforms();
        const auto& disabled = aimOffController.GetCachedGlobalTransforms();
        for (std::size_t bone = 0; bone < target.bones.size(); ++bone)
        {
            aimDisabledError = std::max(aimDisabledError, matrixError(disabled[bone], layerTarget[bone]));
            if (!aimActive) aimDisabledError = std::max(aimDisabledError, matrixError(aimed[bone], layerTarget[bone]));
            if (lowerBone(target.bones[bone].name)) aimLowerError = std::max(aimLowerError, matrixError(aimed[bone], layerTarget[bone]));
        }
        aimGripError = std::max(aimGripError, matrixError(glm::inverse(aimed[aimRight]) * aimed[aimLeft],
            glm::inverse(layerTarget[aimRight]) * layerTarget[aimLeft]));
        const auto chestCorrection = rotationOf(aimed[aimChest]) * glm::inverse(rotationOf(layerTarget[aimChest]));
        const auto handCorrection = rotationOf(aimed[aimRight]) * glm::inverse(rotationOf(layerTarget[aimRight]));
        const auto headCorrection = rotationOf(aimed[aimHead]) * glm::inverse(rotationOf(layerTarget[aimHead]));
        aimMaxChestAngle = std::max(aimMaxChestAngle, VansQuaternionAngleDegrees(chestCorrection));
        aimHeadHandError = std::max(aimHeadHandError, VansQuaternionAngleDegrees(headCorrection * glm::inverse(handCorrection)));
        if (aimActive && frame % 120 > 75 && !(frame >= 1080 && frame < 1100))
        {
            const auto expected = glm::angleAxis(glm::radians(-std::clamp(requestedPitch, -45.0f, 45.0f)), glm::vec3(1,0,0));
            aimSteadyPitchError = std::max(aimSteadyPitchError, VansQuaternionAngleDegrees(expected * glm::inverse(handCorrection)));
        }
        if (!Expect(layered->GetMotionMatchingDebugData()->activeClip == b->activeClip
            && layered->GetCachedGlobalTransforms() == layerPose, "Target Aim mutated source MM pose")) return false;
    }
    std::cout << "[PistolAim] " << projectName << " lowerError=" << aimLowerError << " disabledError=" << aimDisabledError
        << " gripError=" << aimGripError << " maxChestDegrees=" << aimMaxChestAngle
        << " headHandDegrees=" << aimHeadHandError << " steadyPitchDegrees=" << aimSteadyPitchError << '\n';
    if (!Expect(aimLowerError < .002f && aimDisabledError < .002f, "Aim changed lower body or disabled pose")
        || !Expect(aimGripError < .01f, "Aim changed two-hand grip")
        || !Expect(aimMaxChestAngle > 8.0f && aimMaxChestAngle < 9.1f, "Aim torso must only bend slightly")
        || !Expect(aimHeadHandError < .1f && aimSteadyPitchError < .1f, "Arms/head did not receive full limited pitch")) return false;
    // 用正式转身片段锁定朝向回归：只在内存统一输入根参考系，作为独立对照。
    // 其余骨骼、正式遮罩和 Survival 重定向完全相同，资源本身不得被改写。
    const int facingRoot = source.boneNameToIndex.at("root");
    VansBoneTransform facingBind;
    if (!Expect(VansPoseMath::TryDecompose(source.bones[facingRoot].localTransform, facingBind),
        "Pistol facing fixture could not read the source root bind transform")) return false;
    auto makeFacingController = [&](const std::string& turnName, const std::string& upperName,
                                    bool normalizedInput, bool withOverlay, float upperSampleTime = -1.0f)
    {
        auto controller = std::make_unique<VansAnimationController>();
        std::vector<VansAnimationLayerSetup> layers;
        std::vector<std::string> graphIds;
        std::vector<std::unique_ptr<VansAnimGraph>> graphs;
        const std::string names[] = { turnName, upperName };
        for (int layerIndex = 0; layerIndex < (withOverlay ? 2 : 1); ++layerIndex)
        {
            const auto ref = std::find_if(asset.clipRefs.begin(), asset.clipRefs.end(),
                [&](const auto& candidate) { return candidate.name == names[layerIndex]; });
            if (ref == asset.clipRefs.end()) return std::unique_ptr<VansAnimationController>{};
            VansAnimationClip clip = clips.at(ref->assetGuid)->clip;
            if (layerIndex == 1 && upperSampleTime >= 0.0f)
            {
                VansAnimationSampleRequest request;
                request.currentTime = upperSampleTime;
                request.loop = false;
                VansPosePayload held;
                if (!VansAnimationSampler::Sample(clip, source, request, held))
                    return std::unique_ptr<VansAnimationController>{};
                for (std::size_t bone = 0; bone < clip.boneKeyframes.size(); ++bone)
                    for (auto& key : clip.boneKeyframes[bone])
                    {
                        key.position = held.localPose[bone].translation;
                        key.rotation = held.localPose[bone].rotation;
                        key.scale = held.localPose[bone].scale;
                    }
                clip.events.clear();
            }
            if (normalizedInput)
                for (auto& key : clip.boneKeyframes[facingRoot])
                {
                    key.position = facingBind.translation;
                    key.rotation = facingBind.rotation;
                }
            controller->AddClip(names[layerIndex], std::move(clip));
            VansAnimationLayerSetup layer;
            layer.definition = asset.layers[layerIndex];
            layer.definition.useWeightParameter = false;
            layer.definition.fixedWeight = 1.0f;
            layer.definition.weightSmoothingTime = 0.0f;
            if (layerIndex == 1) layer.mask = *mask;
            layers.push_back(std::move(layer));
            graphIds.push_back("facing-graph-" + std::to_string(layerIndex));
            auto graph = std::make_unique<VansAnimGraph>();
            auto clipNode = std::make_unique<AnimGraphClipNode>();
            clipNode->m_ClipName = names[layerIndex]; clipNode->m_Loop = false;
            const int input = graph->AddNode(std::move(clipNode));
            const int output = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
            graph->AddLink(input, 0, output, 0);
            graphs.push_back(std::move(graph));
        }
        if (!InstallTestGraphSet(*controller, std::move(layers), std::move(graphIds), std::move(graphs), error))
            return std::unique_ptr<VansAnimationController>{};
        controller->EnableRootMotion(true); controller->Play();
        return controller;
    };
    int facingCases = 0, facingFrames = 0;
    float sourceFacingError = 0.0f, targetFacingError = 0.0f, facingRootMotionError = 0.0f;
    for (const std::string turn : { "IdleTurn_L_090", "IdleTurn_R_090", "IdleTurn_L_180", "IdleTurn_R_180" })
    {
        for (const std::string upper : { "Pistol_Raise", "Pistol_Aim", "Pistol_Shot", "Pistol_Lower" })
        {
            auto actual = makeFacingController(turn, upper, false, true);
            auto expected = makeFacingController(turn, upper, true, true);
            auto baseOnly = makeFacingController(turn, upper, false, false);
            if (!Expect(actual && expected && baseOnly, error.empty() ? "Pistol facing clip fixture failed" : error.c_str())) return false;
            const float duration = std::max(actual->GetClip(turn)->duration, actual->GetClip(upper)->duration);
            const int frameCount = static_cast<int>(std::ceil(duration / dt));
            for (int frame = 0; frame <= frameCount; ++frame)
            {
                for (auto* controller : { actual.get(), expected.get(), baseOnly.get() })
                    controller->Update(frame == 0 ? 0.0f : dt, source);
                const auto& actualPose = actual->GetCachedGlobalTransforms();
                const auto& expectedPose = expected->GetCachedGlobalTransforms();
                for (std::size_t bone = 0; bone < source.bones.size(); ++bone)
                {
                    const float difference = matrixError(actualPose[bone], expectedPose[bone]);
                    sourceFacingError = std::max(sourceFacingError, difference);
                    if (!Expect(difference < .001f, ("Pistol source facing changed under " + turn
                        + "/" + upper + " bone=" + source.bones[bone].name + " frame=" + std::to_string(frame)).c_str())) return false;
                    if (lowerBone(source.bones[bone].name))
                        if (!Expect(matrixError(actualPose[bone], baseOnly->GetCachedGlobalTransform(static_cast<int>(bone))) < .001f,
                            "Pistol facing correction changed turn-clip lower-body output")) return false;
                }
                facingRootMotionError = std::max(facingRootMotionError,
                    glm::length(actual->GetRootMotionDelta() - baseOnly->GetRootMotionDelta()));
                facingRootMotionError = std::max(facingRootMotionError,
                    glm::length(actual->GetRootRotationDelta() - baseOnly->GetRootRotationDelta()));
                std::vector<glm::mat4> actualTarget, expectedTarget;
                if (!Expect(layerRetarget.Process(actualPose, source, target, actualTarget)
                    && baseRetarget.Process(expectedPose, source, target, expectedTarget), "Pistol facing retarget failed")) return false;
                for (std::size_t bone = 0; bone < target.bones.size(); ++bone)
                {
                    const float difference = matrixError(actualTarget[bone], expectedTarget[bone]);
                    targetFacingError = std::max(targetFacingError, difference);
                    if (!Expect(difference < .002f, ("Pistol Survival facing changed under " + turn
                        + "/" + upper + " bone=" + target.bones[bone].name + " frame=" + std::to_string(frame)).c_str())) return false;
                }
                ++facingFrames;
            }
            ++facingCases;
        }
    }
    if (!Expect(facingRootMotionError < 1.e-6f, "Pistol facing correction changed turn-clip root motion")) return false;
    // 直接比较 Aim 与 Shot 的首尾姿态，经过正式 Mesh 遮罩、根参考系和 Survival 重定向。
    // 此检查不同于上面的转身稳定性检查：旧 Shot 基础姿态不一致时必须失败。
    const auto shotAlignmentRef = std::find_if(asset.clipRefs.begin(), asset.clipRefs.end(),
        [](const auto& ref) { return ref.name == "Pistol_Shot"; });
    if (!Expect(shotAlignmentRef != asset.clipRefs.end(), "Missing configured pistol shot")) return false;
    const float shotAlignmentDuration = clips.at(shotAlignmentRef->assetGuid)->clip.duration;
    const int pistolSocketIndex = compiledRig.FindSocketByName("RightHand_Pistol");
    if (!Expect(pistolSocketIndex >= 0, "Missing right-hand pistol socket")) return false;
    const auto& pistolSocket = compiledRig.sockets[pistolSocketIndex];
    float shotEndpointSocketError = 0.0f, shotEndpointHandError = 0.0f;
    int shotEndpointFrames = 0;
    for (const std::string turn : { "IdleTurn_L_090", "IdleTurn_R_090", "IdleTurn_L_180", "IdleTurn_R_180" })
    {
        for (const float time : { 0.0f, shotAlignmentDuration })
        {
            auto shot = makeFacingController(turn, "Pistol_Shot", false, true, time);
            auto aim = makeFacingController(turn, "Pistol_Aim", false, true, 0.0f);
            if (!Expect(shot && aim, "Pistol endpoint controller creation failed")) return false;
            const int frames = static_cast<int>(std::ceil(shot->GetClip(turn)->duration / dt));
            for (int frame = 0; frame <= frames; ++frame)
            {
                shot->Update(frame == 0 ? 0.0f : dt, source);
                aim->Update(frame == 0 ? 0.0f : dt, source);
                std::vector<glm::mat4> shotTarget, aimTarget;
                if (!Expect(layerRetarget.Process(shot->GetCachedGlobalTransforms(), source, target, shotTarget)
                    && baseRetarget.Process(aim->GetCachedGlobalTransforms(), source, target, aimTarget),
                    "Pistol endpoint retarget failed")) return false;
                const glm::mat4 shotSocket = shotTarget[pistolSocket.boneIndex] * pistolSocket.localTransform;
                const glm::mat4 aimSocket = aimTarget[pistolSocket.boneIndex] * pistolSocket.localTransform;
                shotEndpointSocketError = std::max(shotEndpointSocketError, matrixError(shotSocket, aimSocket));
                for (const char* hand : { "hand_l", "hand_r" })
                {
                    const int bone = target.boneNameToIndex.at(hand);
                    shotEndpointHandError = std::max(shotEndpointHandError, matrixError(shotTarget[bone], aimTarget[bone]));
                }
                if (!Expect(shotEndpointSocketError < .002f && shotEndpointHandError < .002f,
                    "Pistol Shot endpoints do not align with Aim after the layer and Survival retarget")) return false;
                ++shotEndpointFrames;
            }
        }
    }
    nlohmann::json report = {{"project",projectName},{"frames",1200},{"motionMatchingFrames",mmFrames},{"stanceTransitionFrames",crouchFrames},
        {"distinctMotionClips",activeClips},{"sourceLowerMaxMatrixError",sourceLowerError},{"targetLowerMaxMatrixError",targetLowerError},
        {"inactiveLayerMaxMatrixError",offError},{"upperBodyDifference",upperDifference},{"rootMotionMaxError",rootError},{"events",events},
        {"facingCases",facingCases},{"facingFrames",facingFrames},{"sourceFacingMaxMatrixError",sourceFacingError},
        {"survivalFacingMaxMatrixError",targetFacingError},{"facingRootMotionMaxError",facingRootMotionError},
        {"shotEndpointFrames",shotEndpointFrames},{"shotEndpointSocketMaxMatrixError",shotEndpointSocketError},
        {"shotEndpointHandMaxMatrixError",shotEndpointHandError}};
    std::cout << report.dump(2) << '\n';
    // 完成回调与下一次输入同帧发生时，相位值没有经过中间采样；显式触发仍须重播。
    AnimatorAssetData weaponAsset;
    if (!Expect(VansAnimatorIO::Load((assets / "Imported/Pistol_9mm/Animation/Pistol.vanimator").string(), weaponAsset), "Weapon Animator load failed")) return false;
    Skeleton weaponSkeleton;
    for (const auto& ref : weaponAsset.clipRefs)
    {
        auto clip = std::make_shared<VansAnimationClipAsset>();
        if (!Expect(VansAnimationClipIO::Load((project / ref.pathHint).string(), clip->clip, clip->skeleton), "Weapon clip load failed")) return false;
        weaponSkeleton = clip->skeleton;
        clips.emplace(ref.assetGuid, std::move(clip));
    }
    VansAnimatorRuntimeCompileOptions weaponOptions;
    weaponOptions.enableTargetPostProcess = false;
    auto weaponRig = std::make_shared<VansAnimationRigAsset>();
    if (!Expect(VansAnimationRigStorage::Load(assets / "Imported/Pistol_9mm/Animation/Pistol.vanimrig", *weaponRig, error), error.c_str())) return false;
    weaponOptions.rigResolver = [weaponRig](const auto&, auto&) { return weaponRig; };
    auto weapon = VansAnimatorRuntimeCompiler::Compile(weaponAsset, weaponSkeleton, clipResolver, maskResolver, weaponOptions, error);
    if (!Expect(weapon != nullptr, error.c_str())) return false;
    weapon->Play();
    // 验证正式状态机的实际完成时间；动作仍由动画事件结束，不另设玩法计时器。
    for (const auto& phase : { std::make_pair(1, "Pistol_Raise.Finished"), std::make_pair(4, "Pistol_Lower.Finished") })
    {
        layered->SetInt("PistolPhase", phase.first);
        layered->SetFloat("PistolUpperBodyWeight", 1.0f);
        layered->SetTrigger("PistolActionStart");
        int finishes = 0;
        float finishedAt = 0.0f;
        for (int frame = 0; frame < 90; ++frame)
        {
            layered->Update(dt, source);
            for (const auto& event : layered->GetSampledEvents())
                if (event.name == phase.second) { ++finishes; finishedAt = (frame + 1) * dt; }
        }
        std::cout << "Pistol timing " << phase.second << " seconds=" << finishedAt << '\n';
        if (!Expect(finishes == 1 && std::abs(finishedAt - 1.0f) <= 2.0f * dt,
            "Pistol raise/lower must finish once in one second")) return false;
    }
    for (int shot = 0; shot < 3; ++shot)
    {
        layered->SetInt("PistolPhase", 2); layered->SetInt("PistolPhase", 3);
        layered->SetFloat("PistolUpperBodyWeight", 1.0f); layered->SetTrigger("PistolActionStart");
        weapon->SetBool("PistolFiring", false); weapon->SetBool("PistolFiring", true); weapon->SetTrigger("PistolActionStart");
        if (shot == 1) layered->SwitchGraphSet("graph-set-crouch-enter");
        if (shot == 2) layered->SwitchGraphSet("graph-set-default");
        int finishes = 0;
        float finishedAt = 0.0f;
        for (int frame = 0; frame < 80; ++frame)
        {
            layered->Update(dt, source); weapon->Update(dt, weaponSkeleton);
            for (const auto& event : layered->GetSampledEvents())
                if (event.name == "Pistol_Shot.Finished") { ++finishes; finishedAt = (frame + 1) * dt; }
            if (frame == 1)
            {
                const auto upper = layered->GetLayerRuntimeDebugInfo().at(1);
                const auto mechanism = weapon->GetLayerRuntimeDebugInfo().at(0);
                const auto shotRef = std::find_if(asset.clipRefs.begin(), asset.clipRefs.end(),
                    [](const auto& ref) { return ref.name == "Pistol_Shot"; });
                const float bodyProgress = upper.playbackTime / clips.at(shotRef->assetGuid)->clip.duration;
                const float weaponProgress = mechanism.playbackTime / clips.at(weaponAsset.clipRefs.front().assetGuid)->clip.duration;
                if (!Expect(upper.state == "PistolShot" && bodyProgress < .15f
                    && mechanism.state == "Recoil" && weaponProgress < .15f
                    && std::abs(bodyProgress - weaponProgress) < .01f,
                    "Consecutive shot did not restart body and weapon")) return false;
            }
        }
        std::cout << "Pistol timing shot=" << shot << " seconds=" << finishedAt << '\n';
        if (!Expect(finishes == 1 && std::abs(finishedAt - .30f) <= 2.0f * dt
            && !layered->IsTriggerSet("PistolActionStart") && !weapon->IsTriggerSet("PistolActionStart"),
            "Consecutive shot trigger was lost or repeated")) return false;
    }
    return Expect(mmFrames > 1000 && activeClips.size() > 4 && sourceLowerError < .001f && targetLowerError < .001f
        && offError < .01f && rootError < 1.e-6f && upperDifference > 1.0f
        && events["Pistol_Raise.Finished"] == 2 && events["Pistol_Shot.Finished"] == 2 && events["Pistol_Lower.Finished"] == 1,
        "Pistol overlay violated movement, mask, phase continuity or event ownership");
}

bool TestDemoHallSurvivalBackAxeSceneContract()
{
	using namespace VansGraphics;

	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 5 && !fs::exists(workspace / "DemoHallProject"); ++depth)
	{
		if (!workspace.has_parent_path() || workspace.parent_path() == workspace)
			break;
		workspace = workspace.parent_path();
	}
	const fs::path projectRoot = workspace / "DemoHallProject";
	if (!fs::exists(projectRoot))
		return true;

	constexpr const char* kSurvivalEntityGuid = "38dbe7af-653a-4aeb-bfa7-1ca72e2b972c";
	constexpr const char* kSurvivalAnimationGuid = "87f6e3d4-c8fe-458d-82f2-3e56f20b93e5";
	constexpr const char* kBackAxeSocketGuid = "0cf5406a-6809-4b8a-9d82-062643893f56";
	constexpr const char* kRightHandAxeSocketGuid = "7a33b99b-0664-4b5b-bc86-a5a85ddf03b7";
	constexpr const char* kBackAxeEntityGuid = "d12e84ac-99d2-4a35-8e13-33a6c9032f27";
	constexpr const char* kBackAxeModelGuid = "17ee1769-fddf-4cf8-a134-2d5471e5218c";
	constexpr const char* kBackAxeMaterialGuid = "b34fea44-f13c-4df3-a2c3-743898ba1b04";

	nlohmann::json scene;
	std::ifstream sceneInput(projectRoot / "Scenes" / "DemoHall.json");
	if (!Expect(sceneInput.good(), "DemoHall scene could not be opened"))
		return false;
	try
	{
		sceneInput >> scene;
	}
	catch (...)
	{
		return Expect(false, "DemoHall scene is not valid JSON");
	}

	const nlohmann::json* survivalEntity = nullptr;
	const nlohmann::json* backAxeEntity = nullptr;
	const nlohmann::json* backAxeMeshEntity = nullptr;
	if (!Expect(scene.contains("entities") && scene.at("entities").is_array(),
		"DemoHall scene has no entities array"))
	{
		return false;
	}
	std::size_t activeMotionMatchingCharacters = 0;
	for (const nlohmann::json& entity : scene.at("entities"))
	{
		for (const nlohmann::json& component :
			 entity.value("components", nlohmann::json::array()))
		{
			if (component.value("type", "") != "Animation"
				|| !component.contains("data")
				|| !component.at("data").contains("motion_matching"))
			{
				continue;
			}
			const nlohmann::json& motionMatching =
				component.at("data").at("motion_matching");
			if (!motionMatching.value("enabled", false))
				continue;
			++activeMotionMatchingCharacters;
			if (!Expect(motionMatching.contains("motion_model")
				&& motionMatching.contains("turn_in_place_warping")
				&& motionMatching.contains("databases"),
				"DemoHall active Motion Matching character is missing Turn warping data"))
			{
				return false;
			}
			const nlohmann::json& motionModel = motionMatching.at("motion_model");
			const nlohmann::json& turnWarping =
				motionMatching.at("turn_in_place_warping");
			std::size_t turnDatabaseCount = 0;
			std::unordered_set<std::string> idleTurnClips;
			for (const nlohmann::json& database : motionMatching.at("databases"))
			{
				if (database.value("phase", "") != "Turn")
					continue;
				++turnDatabaseCount;
				if (database.value("name", "") != "PSD_DemoHall_Stand_Idle_Turns")
					continue;
				for (const nlohmann::json& clip :
					 database.value("clips", nlohmann::json::array()))
				{
					idleTurnClips.insert(clip.value("name", ""));
				}
			}
			if (!Expect(motionModel.value("drive_mode", "") == "capsule"
				&& motionModel.value("root_rotation_weight", 0.0f) == 1.0f
				&& turnWarping.value("enabled", false)
				&& turnWarping.value("min_root_yaw_scale_ratio", 0.0f) == 0.75f
				&& turnWarping.value("max_root_yaw_scale_ratio", 0.0f) == 1.25f
				&& turnWarping.value("max_additive_correction_degrees", 0.0f) == 15.0f
				&& turnWarping.value("final_tolerance_degrees", 0.0f) == 1.0f
				&& turnDatabaseCount >= 5
				&& idleTurnClips.size() == 8
				&& idleTurnClips.count("IdleTurn_L_045") > 0
				&& idleTurnClips.count("IdleTurn_L_180") > 0
				&& idleTurnClips.count("IdleTurn_R_045") > 0
				&& idleTurnClips.count("IdleTurn_R_180") > 0,
				"DemoHall Capsule movement authority or left/right Turn database coverage is incomplete"))
			{
				return false;
			}
		}
	}
	if (!Expect(activeMotionMatchingCharacters == 2,
		"DemoHall must explicitly configure both active Motion Matching characters"))
	{
		return false;
	}
	for (const nlohmann::json& entity : scene.at("entities"))
	{
		const std::string name = entity.value("name", "");
		if (name == "SurvivalCharacter")
			survivalEntity = &entity;
		else if (name == "Survival_Back_Axe")
			backAxeEntity = &entity;
		else if (name == "Survival_Back_Axe_metal-low")
			backAxeMeshEntity = &entity;
	}
	if (!Expect(survivalEntity && backAxeEntity && backAxeMeshEntity,
		"DemoHall Survival back axe Object hierarchy is missing"))
	{
		return false;
	}

	const nlohmann::json* survivalAnimation = nullptr;
	for (const nlohmann::json& component : survivalEntity->at("components"))
	{
		if (component.value("type", "") == "Animation")
			survivalAnimation = &component;
	}
	const nlohmann::json& axeParent = backAxeEntity->at("parent");
	const nlohmann::json& meshParent = backAxeMeshEntity->at("parent");
	bool hasIdentityTransform = false;
	bool hasAxeModel = false;
	for (const nlohmann::json& component : backAxeEntity->at("components"))
	{
		const nlohmann::json& data = component.at("data");
		if (component.value("type", "") == "Transform")
		{
			hasIdentityTransform = data.at("position") == nlohmann::json::array({ 0, 0, 0 })
				&& data.at("rotation") == nlohmann::json::array({ 0, 0, 0, 1 })
				&& data.at("scale") == nlohmann::json::array({ 1, 1, 1 });
		}
		else if (component.value("type", "") == "MultiMeshRoot")
		{
			hasAxeModel = data.at("model").value("guid", "") == kBackAxeModelGuid;
		}
	}
	bool hasAxeRenderer = false;
	for (const nlohmann::json& component : backAxeMeshEntity->at("components"))
	{
		if (component.value("type", "") != "ModelRenderer")
			continue;
		const nlohmann::json& data = component.at("data");
		hasAxeRenderer = !component.value("enabled", true)
			&& data.at("model").value("guid", "") == kBackAxeModelGuid
			&& data.at("submesh").value("index", -1) == 0
			&& data.at("submesh").value("sourceNode", "") == "metal-low"
			&& data.at("materialOverrides").at("default").value("guid", "")
				== kBackAxeMaterialGuid;
	}
	if (!Expect(survivalEntity->value("id", "") == kSurvivalEntityGuid
		&& survivalAnimation
		&& survivalAnimation->value("id", "") == kSurvivalAnimationGuid
		&& survivalAnimation->at("data").at("rig").value("guid", "")
			== "65af9371-2a97-43f1-93a8-04dc2e4f1002"
		&& backAxeEntity->value("id", "") == kBackAxeEntityGuid
		&& axeParent.value("kind", "") == "socket"
		&& axeParent.value("entityGuid", "") == kSurvivalEntityGuid
		&& axeParent.value("animationComponentGuid", "") == kSurvivalAnimationGuid
		&& axeParent.value("anchorGuid", "") == kBackAxeSocketGuid
		&& meshParent.value("kind", "") == "entity"
		&& meshParent.value("entityGuid", "") == kBackAxeEntityGuid
		&& hasIdentityTransform && hasAxeModel && hasAxeRenderer,
		"DemoHall axe must be attached to the Survival Back_Axe Socket and follow character visibility"))
	{
		return false;
	}

	VansAnimationRigAsset survivalRig;
	std::string rigError;
	if (!Expect(VansAnimationRigStorage::Load(
		projectRoot / "Assets" / "AnimationRigs" / "Survival.vanimrig",
		survivalRig, rigError), rigError.c_str()))
	{
		return false;
	}
	const auto backSocket = std::find_if(survivalRig.sockets.begin(), survivalRig.sockets.end(),
		[](const VansRigSocketDefinition& value) { return value.name == "Back_Axe"; });
	const auto handSocket = std::find_if(survivalRig.sockets.begin(), survivalRig.sockets.end(),
		[](const VansRigSocketDefinition& value) { return value.name == "RightHand_Axe"; });
	if (!Expect(backSocket != survivalRig.sockets.end()
		&& backSocket->guid == kBackAxeSocketGuid
		&& backSocket->boneGuid == "0aab6b03-caaa-5326-8521-c0f4f380e3bd"
		&& glm::length(backSocket->positionLocal
			- glm::vec3(-99.915901f, 19.805099f, 49.349899f)) <= 1.0e-4f
		&& glm::length(backSocket->scaleLocal - glm::vec3(1.0f)) <= 1.0e-5f,
		"DemoHall Survival Back_Axe Socket must resolve to spine_04 with the validated back offset"))
	{
		return false;
	}
	if (!Expect(handSocket != survivalRig.sockets.end()
		&& handSocket->guid == kRightHandAxeSocketGuid
		&& handSocket->boneGuid == "f5b8c223-b747-5967-8751-2efed3816b1c"
		&& glm::length(handSocket->scaleLocal - glm::vec3(1.0f)) <= 1.0e-5f
		&& std::abs(glm::length(handSocket->rotationLocal) - 1.0f) <= 1.0e-5f,
		"DemoHall Survival RightHand_Axe Socket must use a normalized rigid local transform"))
	{
		return false;
	}

	const fs::path survivalModelPath = projectRoot / "Assets" / "Characters"
		/ "Survival" / "Models" / "survival_character.fbx";
	Vans::VansAssetMeta survivalModelMeta;
	std::string survivalModelError;
	if (!Expect(Vans::VansAssetMetaStorage::Load(
		Vans::VansAssetMeta::MetaPathFor(survivalModelPath),
		survivalModelMeta, survivalModelError), survivalModelError.c_str()))
	{
		return false;
	}
	Assimp::Importer survivalImporter;
	const aiScene* survivalScene = survivalImporter.ReadFile(
		survivalModelPath.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
	if (!Expect(survivalScene != nullptr, survivalImporter.GetErrorString()))
		return false;
	Skeleton survivalSkeleton;
	VansSkinnedMeshLoader::ExtractSkeleton(survivalScene, survivalSkeleton, 1.0f,
		Vans::ReadSkeletalMeshImportSettings(survivalModelMeta));
	VansCompiledAnimationRig compiledSurvivalRig;
	std::string compileRigError;
	if (!Expect(VansAnimationRigCompiler::Compile(
		survivalRig, survivalSkeleton, compiledSurvivalRig, compileRigError),
		compileRigError.c_str()))
	{
		return false;
	}
	const int handSocketIndex = compiledSurvivalRig.FindSocketByGuid(kRightHandAxeSocketGuid);
	bool handSocketCompilesToWeaponBone = handSocketIndex >= 0;
	if (handSocketCompilesToWeaponBone)
	{
		const int boneIndex = compiledSurvivalRig.sockets[
			static_cast<std::size_t>(handSocketIndex)].boneIndex;
		handSocketCompilesToWeaponBone = boneIndex >= 0
			&& boneIndex < static_cast<int>(survivalSkeleton.bones.size())
			&& survivalSkeleton.bones[static_cast<std::size_t>(boneIndex)].name == "weapon_r";
	}
	if (!Expect(handSocketCompilesToWeaponBone,
		"DemoHall Survival RightHand_Axe Socket did not compile to the weapon_r runtime bone"))
	{
		return false;
	}
	const VansCompiledRigAttachmentProfile* backAxeProfile =
		compiledSurvivalRig.FindAttachmentProfile(
			kBackAxeModelGuid, VansRigAttachmentParentKind::Socket, kBackAxeSocketGuid);
	const VansCompiledRigAttachmentProfile* handAxeProfile =
		compiledSurvivalRig.FindAttachmentProfile(
			kBackAxeModelGuid, VansRigAttachmentParentKind::Socket, kRightHandAxeSocketGuid);
	if (!Expect(backAxeProfile && handAxeProfile
		&& glm::length(backAxeProfile->positionLocal) <= 1.0e-6f
		&& std::abs(backAxeProfile->rotationLocal.w - 1.0f) <= 1.0e-6f
		&& glm::length(backAxeProfile->scaleLocal - glm::vec3(1.0f)) <= 1.0e-6f
		&& glm::length(handAxeProfile->positionLocal
			- glm::vec3(-60.95047f, 39.508575f, -6.698608f)) <= 1.0e-4f
		&& std::abs(glm::length(handAxeProfile->rotationLocal) - 1.0f) <= 1.0e-5f,
		"DemoHall Fire Axe hand/back attachment profiles were not compiled for runtime consumption"))
	{
		return false;
	}

	VansAnimationClip attackClip;
	Skeleton attackSkeleton;
	if (!Expect(VansAnimationClipIO::Load(
		(projectRoot / "Assets" / "Animations" / "Combat" / "CloseCombat"
			/ "A_Crowbar_Attack_Unreal_Take.vclip").string(), attackClip, attackSkeleton),
		"DemoHall attack clip could not be loaded for axe grip diagnostics"))
	{
		return false;
	}
	VansRetargetProfileAsset attackRetargetProfile;
	std::string attackRetargetError;
	if (!Expect(VansRetargetProfileStorage::Load(
		projectRoot / "Assets" / "Retarget" / "RTG_UEFN_To_Survival.vretarget",
		attackRetargetProfile, attackRetargetError), attackRetargetError.c_str()))
	{
		return false;
	}
	VansRetargetRuntimeDesc attackRetargetDesc;
	attackRetargetDesc.translationScaleMode = attackRetargetProfile.translationScaleMode;
	attackRetargetDesc.translationScale = attackRetargetProfile.explicitTranslationScale;
	attackRetargetDesc.rootAlignment = attackRetargetProfile.rootAlignment;
	attackRetargetDesc.targetModelSpaceAlignment =
		attackRetargetProfile.targetModelSpaceAlignment;
	attackRetargetDesc.limbChains = attackRetargetProfile.limbChains;
	VansRetargetProcessor attackRetargetProcessor;
	if (!Expect(attackRetargetProcessor.Build(attackSkeleton, survivalSkeleton,
		compiledSurvivalRig, attackRetargetDesc),
		"DemoHall attack retarget processor could not be built for axe grip diagnostics"))
	{
		return false;
	}
	const int targetWeapon = survivalSkeleton.boneNameToIndex.at("weapon_r");
	const int targetHandL = survivalSkeleton.boneNameToIndex.at("hand_l");
	const int targetHandR = survivalSkeleton.boneNameToIndex.at("hand_r");
	const int targetMiddleR = survivalSkeleton.boneNameToIndex.at("middle_01_r");
	const int targetIndexR = survivalSkeleton.boneNameToIndex.at("index_01_r");
	const int targetRingR = survivalSkeleton.boneNameToIndex.at("ring_01_r");
	const int targetPinkyR = survivalSkeleton.boneNameToIndex.at("pinky_01_r");
	const int targetMiddleL = survivalSkeleton.boneNameToIndex.at("middle_01_l");
	const int targetIndexL = survivalSkeleton.boneNameToIndex.at("index_01_l");
	const int targetRingL = survivalSkeleton.boneNameToIndex.at("ring_01_l");
	const int targetPinkyL = survivalSkeleton.boneNameToIndex.at("pinky_01_l");
	glm::vec3 sampledRightPalmCenter(0.0f);
	glm::vec3 sampledHandleAxis(0.0f);
	std::size_t sampledGripPoseCount = 0;
	for (int sampleIndex = 0; sampleIndex <= 10; ++sampleIndex)
	{
		VansAnimationSampleRequest request;
		request.currentTime = attackClip.duration * static_cast<float>(sampleIndex) / 10.0f;
		request.endTime = attackClip.duration;
		request.loop = false;
		VansPosePayload payload;
		if (!VansAnimationSampler::Sample(attackClip, attackSkeleton, request, payload))
			continue;
		std::vector<glm::mat4> sourceModels;
		VansPoseMath::ToMatrices(payload.localPose, sourceModels);
		for (int boneIndex : attackSkeleton.topologicalOrder)
		{
			const int parentIndex = attackSkeleton.bones[boneIndex].parentIndex;
			if (parentIndex >= 0)
				sourceModels[boneIndex] = sourceModels[parentIndex] * sourceModels[boneIndex];
		}
		std::vector<glm::mat4> targetModels;
		if (!attackRetargetProcessor.Process(
			sourceModels, attackSkeleton, survivalSkeleton, targetModels))
		{
			continue;
		}
		const glm::mat4 weaponInverse = glm::inverse(targetModels[targetWeapon]);
		const glm::vec3 leftInWeapon = glm::vec3(weaponInverse
			* glm::vec4(glm::vec3(targetModels[targetHandL][3]), 1.0f));
		const glm::vec3 rightInWeapon = glm::vec3(weaponInverse
			* glm::vec4(glm::vec3(targetModels[targetHandR][3]), 1.0f));
		const auto boneInWeapon = [&](int boneIndex)
		{
			return glm::vec3(weaponInverse
				* glm::vec4(glm::vec3(targetModels[boneIndex][3]), 1.0f));
		};
		const glm::vec3 rightFingerCentroid = (boneInWeapon(targetMiddleR)
			+ boneInWeapon(targetIndexR) + boneInWeapon(targetRingR)
			+ boneInWeapon(targetPinkyR)) * 0.25f;
		const glm::vec3 leftFingerCentroid = (boneInWeapon(targetMiddleL)
			+ boneInWeapon(targetIndexL) + boneInWeapon(targetRingL)
			+ boneInWeapon(targetPinkyL)) * 0.25f;
		const glm::vec3 rightPalmCenter = (rightInWeapon + rightFingerCentroid) * 0.5f;
		const glm::vec3 leftPalmCenter = (leftInWeapon + leftFingerCentroid) * 0.5f;
		const glm::vec3 handSeparation = leftPalmCenter - rightPalmCenter;
		const float handSeparationLength = glm::length(handSeparation);
		if (handSeparationLength <= 1.0e-5f)
			continue;
		sampledRightPalmCenter += rightPalmCenter;
		sampledHandleAxis += handSeparation / handSeparationLength;
		++sampledGripPoseCount;
	}
	if (!Expect(sampledGripPoseCount == 11,
		"DemoHall attack clip did not provide the expected retargeted two-hand grip samples"))
	{
		return false;
	}
	sampledRightPalmCenter /= static_cast<float>(sampledGripPoseCount);
	sampledHandleAxis = glm::normalize(sampledHandleAxis);
	// Fire Axe 的斧头在 -Y 端；右手应握在 +Y 柄尾下方约 13 cm，并让 -Y 朝向左手。
	const glm::vec3 axeRightHandGripLocal(-14.579931f, 145.417932f, 20.369572f);
	const glm::quat configuredRotation = glm::normalize(handSocket->rotationLocal);
	const glm::quat expectedRotation = glm::rotation(
		glm::vec3(0.0f, 1.0f, 0.0f), sampledHandleAxis)
		* glm::angleAxis(3.14159265358979323846f, glm::vec3(0.0f, 0.0f, 1.0f));
	const glm::vec3 configuredGripCenter = handSocket->positionLocal
		+ configuredRotation * axeRightHandGripLocal;
	if (!Expect(glm::length(configuredGripCenter - sampledRightPalmCenter) <= 0.01f
		&& std::abs(glm::dot(configuredRotation, expectedRotation)) >= 0.99999f,
		"DemoHall Survival RightHand_Axe Socket must place the lower handle in the right palm "
		"and align the Fire Axe -Y head direction with the retargeted two-hand attack grip"))
	{
		return false;
	}

	struct AssetExpectation
	{
		const char* relativePath;
		const char* guid;
	};
	const AssetExpectation assets[] = {
		{"Assets/Imported/Fire_Axe/Source/Fire_Axe.fbx", kBackAxeModelGuid},
		{"Assets/Imported/Fire_Axe/Materials/Fire_Axe_PBR.mat", kBackAxeMaterialGuid},
		{"Assets/Imported/Fire_Axe/Source/textures/Material_BaseColor.png", "32a681a1-0468-4d40-beca-13b8a9c814c6"},
		{"Assets/Imported/Fire_Axe/Source/textures/Material_Metallic.png", "eff6df6d-5b3f-457a-b00c-64990836e4aa"},
		{"Assets/Imported/Fire_Axe/Source/textures/Material_Normal.png", "3341e966-03e9-4cde-812e-69c663a3e14a"},
		{"Assets/Imported/Fire_Axe/Source/textures/Material_Roughness.png", "ba6a0d99-d6fb-4f6f-82d9-04318f635d6d"}
	};
	for (const AssetExpectation& asset : assets)
	{
		const fs::path path = projectRoot / asset.relativePath;
		Vans::VansAssetMeta meta;
		std::string metaError;
		if (!Expect(fs::exists(path)
			&& Vans::VansAssetMetaStorage::Load(
				Vans::VansAssetMeta::MetaPathFor(path), meta, metaError)
			&& meta.guid.ToString() == asset.guid,
			"DemoHall Survival back axe asset or metadata is missing"))
		{
			return false;
		}
	}

	nlohmann::json material;
	std::ifstream materialInput(
		projectRoot / "Assets" / "Imported" / "Fire_Axe" / "Materials" / "Fire_Axe_PBR.mat");
	if (!Expect(materialInput.good(), "DemoHall Fire Axe material could not be opened"))
		return false;
	materialInput >> material;
	const nlohmann::json& textures = material.at("textures");
	if (!Expect(textures.at("basecolor").value("guid", "")
			== "32a681a1-0468-4d40-beca-13b8a9c814c6"
		&& textures.at("metal").value("guid", "")
			== "eff6df6d-5b3f-457a-b00c-64990836e4aa"
		&& textures.at("normal").value("guid", "")
			== "3341e966-03e9-4cde-812e-69c663a3e14a"
		&& textures.at("roughness").value("guid", "")
			== "ba6a0d99-d6fb-4f6f-82d9-04318f635d6d",
		"DemoHall Fire Axe material texture bindings are incomplete"))
	{
		return false;
	}

	std::ifstream scriptInput(projectRoot / "Scripts" / "forest_lua_behaviors.lua");
	const std::string scriptText{
		std::istreambuf_iterator<char>{ scriptInput }, std::istreambuf_iterator<char>{} };
	return Expect(scriptInput.good() || scriptInput.eof(),
		"DemoHall character switch script could not be read")
		&& Expect(scriptText.find("\"Survival_Back_Axe_metal-low\"")
			!= std::string::npos
			&& scriptText.find("set_character_attachments_enabled(option, enabled)")
				!= std::string::npos
			&& scriptText.find("weapon:bind_to_socket_profile(") != std::string::npos
			&& scriptText.find("weapon:reparent_to_socket(") == std::string::npos,
			"DemoHall character switching and attack must consume the Fire Axe attachment profiles");
}

bool TestProjectRetargetOwnedSkeletonAndSkinningContract()
{
	using namespace VansGraphics;

	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 5
		&& !fs::exists(workspace / "AnimationV2Project")
		&& !fs::exists(workspace / "DemoHallProject"); ++depth)
	{
		if (!workspace.has_parent_path() || workspace.parent_path() == workspace)
			break;
		workspace = workspace.parent_path();
	}
	if (!fs::exists(workspace / "AnimationV2Project")
		&& !fs::exists(workspace / "DemoHallProject"))
		return true;

	auto loadSkeleton = [](const fs::path& modelPath, Skeleton& skeleton, std::string& error)
	{
		Vans::VansAssetMeta meta;
		if (!Vans::VansAssetMetaStorage::Load(
			Vans::VansAssetMeta::MetaPathFor(modelPath), meta, error))
			return false;
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(
			modelPath.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
		if (!scene)
		{
			error = importer.GetErrorString();
			return false;
		}
		VansSkinnedMeshLoader::ExtractSkeleton(
			scene, skeleton, 1.0f, Vans::ReadSkeletalMeshImportSettings(meta));
		if (skeleton.bones.empty())
		{
			error = "model has no Skeleton";
			return false;
		}
		return true;
	};
	auto buildModelTransforms = [](const Skeleton& skeleton,
		const std::vector<glm::mat4>& localTransforms)
	{
		std::vector<glm::mat4> modelTransforms(localTransforms.size(), glm::mat4(1.0f));
		for (int boneIndex : skeleton.topologicalOrder)
		{
			modelTransforms[static_cast<std::size_t>(boneIndex)] =
				localTransforms[static_cast<std::size_t>(boneIndex)];
			const int parentIndex = skeleton.bones[static_cast<std::size_t>(boneIndex)].parentIndex;
			if (parentIndex >= 0)
				modelTransforms[static_cast<std::size_t>(boneIndex)] =
					modelTransforms[static_cast<std::size_t>(parentIndex)]
					* modelTransforms[static_cast<std::size_t>(boneIndex)];
		}
		return modelTransforms;
	};
	auto matrixDifference = [](const glm::mat4& lhs, const glm::mat4& rhs)
	{
		float difference = 0.0f;
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				difference = std::max(
					difference, std::fabs(lhs[column][row] - rhs[column][row]));
		return difference;
	};

	struct Fixture
	{
		const char* label;
		const char* project;
		const char* sourceModel;
		const char* targetModel;
		const char* sourceRig;
		const char* targetRig;
		const char* profile;
	};
	const Fixture fixtures[] = {
		{"AnimationV2/TwinBlast", "AnimationV2Project",
			"Assets/Models/SKM_UEFN_Mannequin.fbx",
			"Assets/Characters/TwinBlast/Models/SKM_TwinBlast_ActionHero.fbx",
			"Assets/AnimationRigs/UEFN.vanimrig", "Assets/AnimationRigs/TwinBlast.vanimrig",
			"Assets/Retarget/RTG_UEFN_To_TwinBlast.vretarget"},
		{"AnimationV2/SWAT", "AnimationV2Project",
			"Assets/Models/SKM_UEFN_Mannequin.fbx",
			"Assets/Characters/SWAT/Models/swat.fbx",
			"Assets/AnimationRigs/UEFN.vanimrig", "Assets/AnimationRigs/SWAT.vanimrig",
			"Assets/Retarget/RTG_UEFN_To_SWAT.vretarget"},
		{"AnimationV2/Survival", "AnimationV2Project",
			"Assets/Models/SKM_UEFN_Mannequin.fbx",
			"Assets/Characters/Survival/Models/survival_character.fbx",
			"Assets/AnimationRigs/UEFN.vanimrig", "Assets/AnimationRigs/Survival.vanimrig",
			"Assets/Retarget/RTG_UEFN_To_Survival.vretarget"},
		{"DemoHall/Survival", "DemoHallProject",
			"Assets/Models/SKM_UEFN_Mannequin.fbx",
			"Assets/Characters/Survival/Models/survival_character.fbx",
			"Assets/AnimationRigs/UEFN.vanimrig", "Assets/AnimationRigs/Survival.vanimrig",
			"Assets/Retarget/RTG_UEFN_To_Survival.vretarget"}
	};

	for (const Fixture& fixture : fixtures)
	{
		const fs::path projectRoot = workspace / fixture.project;
		if (!fs::exists(projectRoot))
			continue;
		std::string error;
		Skeleton importedSourceSkeleton;
		Skeleton importedTargetSkeleton;
		if (!loadSkeleton(projectRoot / fixture.sourceModel, importedSourceSkeleton, error)
			|| !loadSkeleton(projectRoot / fixture.targetModel, importedTargetSkeleton, error))
		{
			return Expect(false, (std::string(fixture.label)
				+ " failed to import its Retarget Skeletons: " + error).c_str());
		}
		if (std::string(fixture.sourceModel).find("SKM_UEFN_Mannequin.fbx") != std::string::npos)
		{
			const std::size_t mannequinWrapperCount = static_cast<std::size_t>(std::count_if(
				importedSourceSkeleton.bones.begin(), importedSourceSkeleton.bones.end(),
				[](const BoneInfo& bone) { return bone.name == "SKM_UEFN_Mannequin"; }));
			if (!Expect(importedSourceSkeleton.bones.size() == 89
				&& mannequinWrapperCount == 1,
				(std::string(fixture.label)
					+ " imported a duplicate non-skeleton UEFN Mesh wrapper into the deformation Skeleton").c_str()))
			{
				return false;
			}
		}
		Skeleton incompatibleCacheLayout = importedSourceSkeleton;
		incompatibleCacheLayout.bones.front().canonicalPath += "/stale-cache-layout";
		incompatibleCacheLayout.RebuildIdentityMapsAndSignature();
		if (!Expect(!incompatibleCacheLayout.MatchesAnimationLayout(importedSourceSkeleton),
			(std::string(fixture.label)
				+ " accepted a same-count Animation Clip cache with a different Skeleton identity").c_str()))
		{
			return false;
		}

		VansAnimationRigAsset sourceRigAsset;
		VansAnimationRigAsset targetRigAsset;
		VansRetargetProfileAsset profile;
		if (!VansAnimationRigStorage::Load(
				projectRoot / fixture.sourceRig, sourceRigAsset, error)
			|| !VansAnimationRigStorage::Load(
				projectRoot / fixture.targetRig, targetRigAsset, error)
			|| !VansRetargetProfileStorage::Load(
				projectRoot / fixture.profile, profile, error))
		{
			return Expect(false, (std::string(fixture.label)
				+ " failed to load its Retarget assets: " + error).c_str());
		}
		if (!Expect(!profile.limbChains.empty(),
			(std::string(fixture.label) + " Retarget profile has no limb mappings").c_str()))
			return false;

		VansCompiledAnimationRig compiledSourceRig;
		VansCompiledAnimationRig compiledTargetRig;
		if (!VansAnimationRigCompiler::Compile(
				sourceRigAsset, importedSourceSkeleton, compiledSourceRig, error)
			|| !VansAnimationRigCompiler::Compile(
				targetRigAsset, importedTargetSkeleton, compiledTargetRig, error))
		{
			return Expect(false, (std::string(fixture.label)
				+ " failed to compile its Animation Rigs: " + error).c_str());
		}

		VansRetargetRuntimeDesc desc;
		desc.translationScaleMode = profile.translationScaleMode;
		desc.translationScale = profile.explicitTranslationScale;
		desc.rootAlignment = profile.rootAlignment;
		desc.targetModelSpaceAlignment = profile.targetModelSpaceAlignment;
		desc.limbChains = profile.limbChains;

		// 复现 Scene Builder 的真实所有权顺序：Node 复制 Target Skeleton、
		// ConfigureRetargetSource 复制 Source Skeleton，并在内部重绑定两套 Rig。
		VansAnimationNode targetNode(fixture.label);
		auto targetController = std::make_unique<VansAnimationController>();
		auto sourceController = std::make_unique<VansAnimationController>();
		if (!sourceController->SetAnimationRig(std::move(compiledSourceRig), {}, error)
			|| !targetController->SetAnimationRig(std::move(compiledTargetRig), {}, error))
		{
			return Expect(false, (std::string(fixture.label)
				+ " rejected its compiled Animation Rig: " + error).c_str());
		}
		targetNode.SetSkeleton(importedTargetSkeleton);
		VansAnimationController* targetControllerView = targetController.get();
		if (!targetNode.SetController(std::move(targetController))
			|| !targetNode.ConfigureRetargetSource(
				importedSourceSkeleton, std::move(sourceController), desc, error))
		{
			return Expect(false, (std::string(fixture.label)
				+ " rejected equivalent node-owned Skeletons: " + error).c_str());
		}
		const Skeleton& sourceNodeSkeleton = targetNode.GetRetargetSourceSkeleton();
		const Skeleton& targetNodeSkeleton = targetNode.GetSkeleton();
		if (!Expect(targetNode.GetRetargetSourceController()->GetAnimationRig()->skeleton
				== &sourceNodeSkeleton
			&& targetControllerView->GetAnimationRig()->skeleton == &targetNodeSkeleton,
			(std::string(fixture.label)
				+ " controllers retained imported or temporary Skeleton pointers").c_str()))
			return false;

		VansRetargetProcessor processor;
		if (!Expect(processor.Build(
			sourceNodeSkeleton, targetNodeSkeleton,
			*targetControllerView->GetAnimationRig(), desc),
			(std::string(fixture.label)
				+ " failed Source -> Target runtime construction").c_str()))
			return false;

		if (profile.rootAlignment == VansRetargetRootAlignment::FeetToOwner)
		{
			std::vector<glm::mat4> sourceBindLocals;
			sourceBindLocals.reserve(sourceNodeSkeleton.bones.size());
			for (const BoneInfo& bone : sourceNodeSkeleton.bones)
				sourceBindLocals.push_back(bone.localTransform);
			const std::vector<glm::mat4> sourceBindModels =
				buildModelTransforms(sourceNodeSkeleton, sourceBindLocals);
			std::vector<glm::mat4> alignedTargetBind;
			if (!Expect(processor.Process(sourceBindModels, sourceNodeSkeleton,
				targetNodeSkeleton, alignedTargetBind),
				(std::string(fixture.label) + " failed bind-pose feetToOwner calibration").c_str()))
			{
				return false;
			}
			const int targetFootL = targetNodeSkeleton.boneNameToIndex.at("foot_l");
			const int targetFootR = targetNodeSkeleton.boneNameToIndex.at("foot_r");
			const auto footCenter = [&](const std::vector<glm::mat4>& pose)
			{
				return (glm::vec3(pose[static_cast<std::size_t>(targetFootL)][3])
					+ glm::vec3(pose[static_cast<std::size_t>(targetFootR)][3])) * 0.5f;
			};
			const glm::vec3 bindFootCenter = footCenter(alignedTargetBind);
			const float bindGround = std::min(
				alignedTargetBind[static_cast<std::size_t>(targetFootL)][3].y,
				alignedTargetBind[static_cast<std::size_t>(targetFootR)][3].y);
			if (!Expect(std::abs(bindFootCenter.x) <= 1.0e-3f
				&& std::abs(bindFootCenter.z) <= 1.0e-3f
				&& std::abs(bindGround) <= 1.0e-3f,
				(std::string(fixture.label)
					+ " feetToOwner did not calibrate the target Bind Pose to the entity origin").c_str()))
			{
				return false;
			}

			std::vector<glm::mat4> translatedSource = sourceBindModels;
			const int sourceRoot = sourceNodeSkeleton.boneNameToIndex.at("root");
			const float authoredRootTranslation = 5.0f;
			const glm::mat4 sourceTranslation = glm::translate(
				glm::mat4(1.0f), glm::vec3(authoredRootTranslation, 0.0f, 0.0f));
			for (std::size_t boneIndex = 0; boneIndex < sourceNodeSkeleton.bones.size(); ++boneIndex)
			{
				int ancestor = static_cast<int>(boneIndex);
				while (ancestor >= 0 && ancestor != sourceRoot)
					ancestor = sourceNodeSkeleton.bones[static_cast<std::size_t>(ancestor)].parentIndex;
				if (ancestor == sourceRoot)
					translatedSource[boneIndex] = sourceTranslation * translatedSource[boneIndex];
			}
			std::vector<glm::mat4> translatedTarget;
			if (!Expect(processor.Process(translatedSource, sourceNodeSkeleton,
				targetNodeSkeleton, translatedTarget)
				&& std::abs(glm::length(footCenter(translatedTarget) - bindFootCenter)
					- authoredRootTranslation) <= 1.0e-3f,
				(std::string(fixture.label)
					+ " feetToOwner changed the authored component-space Root Motion magnitude").c_str()))
			{
				return false;
			}
		}

		std::vector<glm::mat4> sourceLocalTransforms;
		sourceLocalTransforms.reserve(sourceNodeSkeleton.bones.size());
		for (const BoneInfo& bone : sourceNodeSkeleton.bones)
			sourceLocalTransforms.push_back(bone.localTransform);
		const int drivenSourceBone = sourceNodeSkeleton.boneNameToIndex.at(
			profile.limbChains.front().sourceRoot);
		sourceLocalTransforms[static_cast<std::size_t>(drivenSourceBone)] *=
			glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const std::vector<glm::mat4> sourceModelTransforms =
			buildModelTransforms(sourceNodeSkeleton, sourceLocalTransforms);
		std::vector<glm::mat4> targetModelTransforms;
		if (!Expect(processor.Process(sourceModelTransforms, sourceNodeSkeleton,
			targetNodeSkeleton, targetModelTransforms),
			(std::string(fixture.label) + " failed Retarget pose evaluation").c_str()))
			return false;

		std::vector<glm::mat4> targetBindLocals;
		targetBindLocals.reserve(targetNodeSkeleton.bones.size());
		for (const BoneInfo& bone : targetNodeSkeleton.bones)
			targetBindLocals.push_back(bone.localTransform);
		const std::vector<glm::mat4> targetBindModels =
			buildModelTransforms(targetNodeSkeleton, targetBindLocals);
		float poseDifference = 0.0f;
		for (std::size_t boneIndex = 0; boneIndex < targetModelTransforms.size(); ++boneIndex)
			poseDifference = std::max(poseDifference,
				matrixDifference(targetModelTransforms[boneIndex], targetBindModels[boneIndex]));
		if (!Expect(poseDifference > 0.001f,
			(std::string(fixture.label)
				+ " Retarget output remained in the target bind pose").c_str()))
			return false;

		if (!Expect(targetControllerView->SubmitExternalModelPose(
			targetModelTransforms, targetNodeSkeleton, 1.0f / 60.0f,
			VansExternalPoseEvaluationMode::DirectFinalPose),
			(std::string(fixture.label) + " rejected the final target pose").c_str()))
			return false;
		float skinningDifference = 0.0f;
		const BoneMatricesSSBO& skinning = targetControllerView->GetBoneMatricesSSBO();
		for (std::size_t boneIndex = 0; boneIndex < targetNodeSkeleton.bones.size(); ++boneIndex)
		{
			const glm::mat4 bindSkinning = targetBindModels[boneIndex]
				* targetNodeSkeleton.bones[boneIndex].offsetMatrix;
			skinningDifference = std::max(skinningDifference,
				matrixDifference(skinning.boneMatrices[boneIndex], bindSkinning));
		}
		if (!Expect(skinningDifference > 0.001f,
			(std::string(fixture.label)
				+ " final skinning matrices remained in A-Pose").c_str()))
			return false;

		VansCompiledAnimationRig incompatibleRig = *targetControllerView->GetAnimationRig();
		Skeleton incompatibleSkeleton = targetNodeSkeleton;
		incompatibleSkeleton.bones.front().localTransform[3].x += 0.25f;
		if (!Expect(!incompatibleRig.BindSkeleton(incompatibleSkeleton, error),
			(std::string(fixture.label)
				+ " accepted a Skeleton with a different bind pose").c_str()))
			return false;
	}
	return true;
}

bool TestRetargetUnmappedTargetBoneInheritanceContract()
{
	using namespace VansGraphics;

	auto buildSkeleton = [](bool includeTargetOnlyWrist)
	{
		Skeleton skeleton;
		skeleton.sourceSkeletonGuid = "retarget-test-skeleton";
		auto addBone = [&](const char* name, int parentIndex,
			const glm::vec3& translation, float rotationRadians)
		{
			BoneInfo bone;
			bone.id = static_cast<int>(skeleton.bones.size());
			bone.name = name;
			bone.parentIndex = parentIndex;
			bone.localTransform = glm::translate(glm::mat4(1.0f), translation) *
				glm::rotate(glm::mat4(1.0f), rotationRadians, glm::vec3(0.0f, 0.0f, 1.0f));
			bone.offsetMatrix = glm::mat4(1.0f);
			skeleton.bones.push_back(std::move(bone));
			const int boneIndex = static_cast<int>(skeleton.bones.size()) - 1;
			skeleton.boneNameToIndex[name] = boneIndex;
			if (parentIndex >= 0)
				skeleton.bones[parentIndex].children.push_back(boneIndex);
			return boneIndex;
		};

		const int root = addBone("root", -1, glm::vec3(0.0f), 0.0f);
		const int pelvis = addBone("pelvis", root, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
		addBone("head", pelvis, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
		const int handL = addBone("hand_l", pelvis, glm::vec3(-1.0f, 0.25f, 0.0f), 0.0f);
		int fingerParent = handL;
		glm::vec3 fingerTranslation(-0.35f, 0.0f, 0.0f);
		if (includeTargetOnlyWrist)
		{
			fingerParent = addBone(
				"wrist_bridge_l", handL, glm::vec3(-0.1f, 0.0f, 0.0f), 0.0f);
			fingerTranslation = glm::vec3(-0.25f, 0.0f, 0.0f);
		}
		addBone("index_01_l", fingerParent, fingerTranslation, 0.0f);
		addBone("hand_r", pelvis, glm::vec3(1.0f, 0.25f, 0.0f), 0.0f);
		addBone("foot_l", pelvis, glm::vec3(-0.4f, -1.0f, 0.0f), 0.0f);
		addBone("foot_r", pelvis, glm::vec3(0.4f, -1.0f, 0.0f), 0.0f);
		if (includeTargetOnlyWrist)
			addBone("wrist_outer_l", handL, glm::vec3(-0.15f, 0.0f, 0.0f), 0.2f);
		skeleton.BuildTopologicalOrder();
		return skeleton;
	};

	auto buildModelTransforms = [](const Skeleton& skeleton,
		const std::vector<glm::mat4>& localTransforms)
	{
		std::vector<glm::mat4> modelTransforms = localTransforms;
		for (int boneIndex : skeleton.topologicalOrder)
		{
			const int parentIndex = skeleton.bones[boneIndex].parentIndex;
			if (parentIndex >= 0)
				modelTransforms[boneIndex] = modelTransforms[parentIndex] * modelTransforms[boneIndex];
		}
		return modelTransforms;
	};

	auto maxMatrixDifference = [](const glm::mat4& lhs, const glm::mat4& rhs)
	{
		float difference = 0.0f;
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				difference = std::max(difference, std::fabs(lhs[column][row] - rhs[column][row]));
		return difference;
	};

	const Skeleton sourceSkeleton = buildSkeleton(false);
	const Skeleton targetWithoutAuxiliary = buildSkeleton(false);
	const Skeleton targetWithAuxiliary = buildSkeleton(true);
	std::vector<glm::mat4> sourceLocalTransforms;
	sourceLocalTransforms.reserve(sourceSkeleton.bones.size());
	for (const BoneInfo& bone : sourceSkeleton.bones)
		sourceLocalTransforms.push_back(bone.localTransform);
	const int sourceHandL = sourceSkeleton.boneNameToIndex.at("hand_l");
	const glm::vec3 handBindTranslation(sourceLocalTransforms[sourceHandL][3]);
	sourceLocalTransforms[sourceHandL] =
		glm::translate(glm::mat4(1.0f), handBindTranslation) *
		glm::rotate(glm::mat4(1.0f), glm::radians(65.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	const std::vector<glm::mat4> sourceModelTransforms =
		buildModelTransforms(sourceSkeleton, sourceLocalTransforms);

	VansRetargetRuntimeDesc desc;
	desc.translationScaleMode = VansRetargetTranslationScaleMode::CompatibleSkeleton;
	desc.targetModelSpaceAlignment = VansRetargetModelSpaceAlignment::SourceBindPose;
	auto compileEmptyRig = [](const Skeleton& skeleton, VansCompiledAnimationRig& rig)
	{
		VansAnimationRigAsset asset;
		asset.name = "Retarget Test Rig";
		asset.skeletonGuid = "retarget-test-skeleton";
		std::string error;
		return VansAnimationRigCompiler::Compile(asset, skeleton, rig, error);
	};
	VansCompiledAnimationRig baselineRig;
	VansCompiledAnimationRig auxiliaryRig;
	VansRetargetProcessor baselineProcessor;
	VansRetargetProcessor auxiliaryProcessor;
	if (!Expect(compileEmptyRig(targetWithoutAuxiliary, baselineRig)
		&& compileEmptyRig(targetWithAuxiliary, auxiliaryRig)
		&& baselineProcessor.Build(sourceSkeleton, targetWithoutAuxiliary, baselineRig, desc)
		&& auxiliaryProcessor.Build(sourceSkeleton, targetWithAuxiliary, auxiliaryRig, desc),
		"Retarget inheritance fixture failed to build"))
	{
		return false;
	}
	if (!Expect(auxiliaryProcessor.GetStats().unmappedTargetBoneCount == 2,
		"Retarget inheritance fixture did not isolate its target-only auxiliary bones"))
	{
		return false;
	}

	std::vector<glm::mat4> baselinePose;
	std::vector<glm::mat4> auxiliaryPose;
	if (!Expect(baselineProcessor.Process(sourceModelTransforms, sourceSkeleton,
		targetWithoutAuxiliary, baselinePose) &&
		auxiliaryProcessor.Process(sourceModelTransforms, sourceSkeleton,
		targetWithAuxiliary, auxiliaryPose),
		"Retarget inheritance fixture failed to evaluate"))
	{
		return false;
	}

	for (const BoneInfo& sourceBone : sourceSkeleton.bones)
	{
		const int baselineIndex = targetWithoutAuxiliary.boneNameToIndex.at(sourceBone.name);
		const int auxiliaryIndex = targetWithAuxiliary.boneNameToIndex.at(sourceBone.name);
		if (!Expect(maxMatrixDifference(
			baselinePose[baselineIndex], auxiliaryPose[auxiliaryIndex]) <= 0.0001f,
			"A target-only auxiliary bone changed an existing mapped-bone result"))
		{
			return false;
		}
	}

	auto expectPreservedTargetLocal = [&](const char* boneName)
	{
		const int boneIndex = targetWithAuxiliary.boneNameToIndex.at(boneName);
		const int parentIndex = targetWithAuxiliary.bones[boneIndex].parentIndex;
		const glm::mat4 evaluatedLocal =
			glm::inverse(auxiliaryPose[parentIndex]) * auxiliaryPose[boneIndex];
		return Expect(maxMatrixDifference(
			evaluatedLocal, targetWithAuxiliary.bones[boneIndex].localTransform) <= 0.0001f,
			"An unmapped target auxiliary bone did not preserve its target bind local transform");
	};
	return expectPreservedTargetLocal("wrist_bridge_l") &&
		expectPreservedTargetLocal("wrist_outer_l");
}

bool TestRetargetConfiguredLimbChainContract()
{
	using namespace VansGraphics;

	auto buildSkeleton = [](bool sourceArm)
	{
		Skeleton skeleton;
		skeleton.sourceSkeletonGuid = "retarget-limb-test-skeleton";
		auto addBone = [&](const char* name, int parentIndex, const glm::vec3& translation)
		{
			BoneInfo bone;
			bone.id = static_cast<int>(skeleton.bones.size());
			bone.name = name;
			bone.parentIndex = parentIndex;
			bone.localTransform = glm::translate(glm::mat4(1.0f), translation);
			bone.offsetMatrix = glm::mat4(1.0f);
			skeleton.bones.push_back(std::move(bone));
			const int boneIndex = static_cast<int>(skeleton.bones.size()) - 1;
			skeleton.boneNameToIndex[name] = boneIndex;
			if (parentIndex >= 0)
				skeleton.bones[parentIndex].children.push_back(boneIndex);
			return boneIndex;
		};

		const int root = addBone("root", -1, glm::vec3(0.0f));
		const int pelvis = addBone("pelvis", root, glm::vec3(0.0f, 1.0f, 0.0f));
		const int upperArm = addBone("upperarm_l", pelvis, glm::vec3(-0.5f, 0.5f, 0.0f));
		const glm::vec3 segment = sourceArm
			? glm::vec3(-1.0f, 0.0f, 0.0f)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		const int lowerArm = addBone("lowerarm_l", upperArm, segment);
		addBone("hand_l", lowerArm, segment);
		skeleton.BuildTopologicalOrder();
		return skeleton;
	};

	auto buildBindModelTransforms = [](const Skeleton& skeleton)
	{
		std::vector<glm::mat4> transforms(skeleton.bones.size(), glm::mat4(1.0f));
		for (int boneIndex : skeleton.topologicalOrder)
		{
			transforms[boneIndex] = skeleton.bones[boneIndex].localTransform;
			const int parentIndex = skeleton.bones[boneIndex].parentIndex;
			if (parentIndex >= 0)
				transforms[boneIndex] = transforms[parentIndex] * transforms[boneIndex];
		}
		return transforms;
	};
	auto buildModelTransforms = [](const Skeleton& skeleton,
		const std::vector<glm::mat4>& localTransforms)
	{
		std::vector<glm::mat4> transforms = localTransforms;
		for (int boneIndex : skeleton.topologicalOrder)
		{
			const int parentIndex = skeleton.bones[boneIndex].parentIndex;
			if (parentIndex >= 0)
				transforms[boneIndex] = transforms[parentIndex] * transforms[boneIndex];
		}
		return transforms;
	};

	const Skeleton sourceSkeleton = buildSkeleton(true);
	const Skeleton targetSkeleton = buildSkeleton(false);
	const std::vector<glm::mat4> sourcePose = buildBindModelTransforms(sourceSkeleton);
	VansRetargetRuntimeDesc baselineDesc;
	baselineDesc.translationScaleMode = VansRetargetTranslationScaleMode::CompatibleSkeleton;
	VansRetargetRuntimeDesc chainDesc = baselineDesc;
	VansRetargetLimbChainDesc leftArm;
	leftArm.name = "LeftArm";
	leftArm.sourceRoot = "upperarm_l";
	leftArm.sourceMid = "lowerarm_l";
	leftArm.sourceTip = "hand_l";
	leftArm.targetChainId = "leftArm";
	chainDesc.limbChains.push_back(leftArm);

	VansAnimationRigAsset targetRigAsset;
	targetRigAsset.name = "Retarget Limb Test Rig";
	targetRigAsset.skeletonGuid = "retarget-limb-test-skeleton";
	targetRigAsset.goals.push_back({ "leftHand", "hand_l" });
	VansRigChainDefinition targetArm;
	targetArm.id = "leftArm";
	targetArm.solver = VansRigSolverKind::Limb;
	targetArm.bones = { "upperarm_l", "lowerarm_l", "hand_l" };
	targetArm.goal = "leftHand";
	targetArm.poleAxisLocal = { 0.0f, 0.0f, 1.0f };
	targetRigAsset.chains.push_back(std::move(targetArm));
	VansCompiledAnimationRig targetRig;
	std::string rigError;
	if (!Expect(VansAnimationRigCompiler::Compile(targetRigAsset, targetSkeleton, targetRig, rigError),
		rigError.c_str()))
		return false;

	VansRetargetProcessor baselineProcessor;
	VansRetargetProcessor chainProcessor;
	if (!Expect(baselineProcessor.Build(sourceSkeleton, targetSkeleton, targetRig, baselineDesc) &&
		chainProcessor.Build(sourceSkeleton, targetSkeleton, targetRig, chainDesc) &&
		baselineProcessor.GetStats().limbChainCount == 0 &&
		chainProcessor.GetStats().limbChainCount == 1,
		"Configured retarget Limb chain did not compile as an opt-in feature"))
	{
		return false;
	}

	std::vector<glm::mat4> baselinePose;
	std::vector<glm::mat4> correctedPose;
	if (!Expect(baselineProcessor.Process(sourcePose, sourceSkeleton, targetSkeleton, baselinePose) &&
		chainProcessor.Process(sourcePose, sourceSkeleton, targetSkeleton, correctedPose),
		"Configured retarget Limb chain fixture failed to evaluate"))
	{
		return false;
	}

	const int sourceRoot = sourceSkeleton.boneNameToIndex.at("upperarm_l");
	const int sourceTip = sourceSkeleton.boneNameToIndex.at("hand_l");
	const int targetRoot = targetSkeleton.boneNameToIndex.at("upperarm_l");
	const int targetTip = targetSkeleton.boneNameToIndex.at("hand_l");
	const glm::vec3 sourceDirection = glm::normalize(
		glm::vec3(sourcePose[sourceTip][3]) - glm::vec3(sourcePose[sourceRoot][3]));
	const glm::vec3 baselineDirection = glm::normalize(
		glm::vec3(baselinePose[targetTip][3]) - glm::vec3(baselinePose[targetRoot][3]));
	const glm::vec3 correctedDirection = glm::normalize(
		glm::vec3(correctedPose[targetTip][3]) - glm::vec3(correctedPose[targetRoot][3]));
	if (!Expect(glm::dot(sourceDirection, baselineDirection) < 0.1f,
		"Unconfigured retarget unexpectedly changed the target arm bind direction") &&
		Expect(glm::dot(sourceDirection, correctedDirection) > 0.999f,
			"Configured retarget Limb chain did not match the source end-effector direction"))
	{
		return false;
	}

	// The source elbow plane must drive a configured retarget chain. A static
	// target pole can otherwise twist a differently authored target wrist even
	// when the hand position is correct.
	std::vector<glm::mat4> bentSourceLocals;
	bentSourceLocals.reserve(sourceSkeleton.bones.size());
	for (const BoneInfo& bone : sourceSkeleton.bones)
		bentSourceLocals.push_back(bone.localTransform);
	const int sourceLowerArm = sourceSkeleton.boneNameToIndex.at("lowerarm_l");
	const glm::vec3 sourceLowerTranslation = glm::vec3(
		bentSourceLocals[static_cast<std::size_t>(sourceLowerArm)][3]);
	bentSourceLocals[static_cast<std::size_t>(sourceLowerArm)] =
		glm::translate(glm::mat4(1.0f), sourceLowerTranslation) *
		glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const std::vector<glm::mat4> bentSourcePose =
		buildModelTransforms(sourceSkeleton, bentSourceLocals);
	std::vector<glm::mat4> bentTargetPose;
	if (!Expect(chainProcessor.Process(bentSourcePose, sourceSkeleton,
		targetSkeleton, bentTargetPose),
		"Configured retarget Limb chain failed on a bent source elbow pose"))
	{
		return false;
	}
	const glm::vec3 bentSourceRoot = glm::vec3(bentSourcePose[sourceRoot][3]);
	const glm::vec3 bentSourceMid = glm::vec3(bentSourcePose[
		sourceSkeleton.boneNameToIndex.at("lowerarm_l")][3]);
	const glm::vec3 bentSourceTip = glm::vec3(bentSourcePose[sourceTip][3]);
	const glm::vec3 bentSourceDirection = bentSourceTip - bentSourceRoot;
	const glm::vec3 bentSourcePole = glm::normalize(
		(bentSourceMid - bentSourceRoot) - bentSourceDirection *
		(glm::dot(bentSourceMid - bentSourceRoot, bentSourceDirection) /
			glm::dot(bentSourceDirection, bentSourceDirection)));
	const glm::vec3 bentTargetRoot = glm::vec3(bentTargetPose[targetRoot][3]);
	const glm::vec3 bentTargetMid = glm::vec3(bentTargetPose[
		targetSkeleton.boneNameToIndex.at("lowerarm_l")][3]);
	const glm::vec3 bentTargetTip = glm::vec3(bentTargetPose[targetTip][3]);
	const glm::vec3 bentTargetDirection = bentTargetTip - bentTargetRoot;
	const glm::vec3 bentTargetPole = glm::normalize(
		(bentTargetMid - bentTargetRoot) - bentTargetDirection *
		(glm::dot(bentTargetMid - bentTargetRoot, bentTargetDirection) /
			glm::dot(bentTargetDirection, bentTargetDirection)));
	if (!Expect(glm::dot(bentSourcePole, bentTargetPole) > 0.999f,
		"Configured retarget Limb chain did not preserve the source elbow bend plane"))
	{
		return false;
	}

	// Some FBX skeletons, including SWAT, carry a 100x scale above otherwise
	// normal-looking arm bones. The retarget goal must use component-space chain
	// length, matching the component-space Limb solver, instead of raw local
	// translations that would place the hand goal next to the shoulder.
	Skeleton scaledTargetSkeleton = buildSkeleton(false);
	const int scaledSkeletonRoot = scaledTargetSkeleton.boneNameToIndex.at("root");
	scaledTargetSkeleton.bones[scaledSkeletonRoot].localTransform = glm::scale(
		scaledTargetSkeleton.bones[scaledSkeletonRoot].localTransform,
		glm::vec3(100.0f));
	const std::vector<glm::mat4> scaledBindPose =
		buildBindModelTransforms(scaledTargetSkeleton);

	VansCompiledAnimationRig scaledTargetRig;
	if (!Expect(VansAnimationRigCompiler::Compile(
		targetRigAsset, scaledTargetSkeleton, scaledTargetRig, rigError), rigError.c_str()))
	{
		return false;
	}

	VansRetargetProcessor scaledChainProcessor;
	std::vector<glm::mat4> scaledCorrectedPose;
	if (!Expect(scaledChainProcessor.Build(
		sourceSkeleton, scaledTargetSkeleton, scaledTargetRig, chainDesc) &&
		scaledChainProcessor.Process(
			sourcePose, sourceSkeleton, scaledTargetSkeleton, scaledCorrectedPose),
		"Scaled target retarget Limb chain fixture failed to evaluate"))
	{
		return false;
	}

	const int scaledTargetRoot = scaledTargetSkeleton.boneNameToIndex.at("upperarm_l");
	const int scaledTargetMid = scaledTargetSkeleton.boneNameToIndex.at("lowerarm_l");
	const int scaledTargetTip = scaledTargetSkeleton.boneNameToIndex.at("hand_l");
	const float scaledBindReach = glm::distance(
		glm::vec3(scaledBindPose[scaledTargetRoot][3]),
		glm::vec3(scaledBindPose[scaledTargetMid][3])) + glm::distance(
		glm::vec3(scaledBindPose[scaledTargetMid][3]),
		glm::vec3(scaledBindPose[scaledTargetTip][3]));
	const glm::vec3 scaledRootToTip =
		glm::vec3(scaledCorrectedPose[scaledTargetTip][3])
		- glm::vec3(scaledCorrectedPose[scaledTargetRoot][3]);
	return Expect(glm::dot(sourceDirection, glm::normalize(scaledRootToTip)) > 0.999f,
		"Scaled target retarget Limb chain did not match the source direction") &&
		Expect(glm::length(scaledRootToTip) > scaledBindReach * 0.95f,
			"Scaled target retarget Limb chain collapsed because inherited scale was ignored");
}

bool TestAnimationAuthoringBoundaryContract()
{
	using namespace VansGraphics;

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

	AnimatorAssetData nodeCoverageAsset;
	nodeCoverageAsset.name = "NodeTypeCoverage";
	nodeCoverageAsset.animationRigGuid = "44444444-4444-4444-8444-444444444444";
	nodeCoverageAsset.clipRefs.push_back(
		{ "Base", "11111111-1111-4111-8111-111111111111", "Animation/Base.vclip" });
	auto nodeCoverageGraph = std::make_unique<VansAnimGraph>();
	auto baseClip = std::make_unique<AnimGraphClipNode>();
	baseClip->m_ClipName = "Base";
	const int baseClipId = nodeCoverageGraph->AddNode(std::move(baseClip));
	auto savePose = std::make_unique<AnimGraphSaveCachedPoseNode>();
	savePose->m_CacheName = "BaseCache";
	const int savePoseId = nodeCoverageGraph->AddNode(std::move(savePose));
	auto usePose = std::make_unique<AnimGraphUseCachedPoseNode>();
	usePose->m_CacheName = "BaseCache";
	const int usePoseId = nodeCoverageGraph->AddNode(std::move(usePose));
	auto layeredBlend = std::make_unique<AnimGraphLayeredBlendPerBoneNode>();
	layeredBlend->m_Mask.id = "upper-body";
	layeredBlend->m_Mask.name = "Upper Body";
	layeredBlend->m_Mask.defaultWeight = 0.25f;
	layeredBlend->m_Mask.branchRules.push_back(
		{ "spine", VansBoneMaskRuleMode::Include, "spine_01", true, 3,
			0.5f, 1.0f, VansBoneMaskFalloff::SmoothStep });
	layeredBlend->m_Mask.explicitWeights.emplace("hand_r", 0.75f);
	layeredBlend->m_BlendMode = VansLayerBlendMode::Additive;
	layeredBlend->m_RotationSpace = VansRotationBlendSpace::Local;
	layeredBlend->m_WeightParameter = "UpperBodyWeight";
	layeredBlend->m_FixedWeight = 0.8f;
	layeredBlend->m_UseWeightParameter = true;
	layeredBlend->m_ApplyAdditiveInput = true;
	const int layeredBlendId = nodeCoverageGraph->AddNode(std::move(layeredBlend));
	const int nodeCoverageOutputId = nodeCoverageGraph->AddNode(
		VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
	if (!Expect(baseClipId > 0 && savePoseId > 0 && usePoseId > 0
		&& layeredBlendId > 0 && nodeCoverageOutputId > 0
		&& nodeCoverageGraph->AddLink(baseClipId, 0, savePoseId, 0) > 0
		&& nodeCoverageGraph->AddLink(savePoseId, 0, layeredBlendId, 0) > 0
		&& nodeCoverageGraph->AddLink(usePoseId, 0, layeredBlendId, 1) > 0
		&& nodeCoverageGraph->AddLink(layeredBlendId, 0, nodeCoverageOutputId, 0) > 0,
		"Failed to build complete animation node-type authoring fixture"))
		return false;
	AnimatorGraphAsset nodeCoverageGraphAsset;
	nodeCoverageGraphAsset.id = "graph-node-coverage";
	nodeCoverageGraphAsset.name = "Node Coverage";
	nodeCoverageGraphAsset.graph = std::move(nodeCoverageGraph);
	nodeCoverageAsset.graphs.push_back(std::move(nodeCoverageGraphAsset));
	VansAnimationLayerDefinition nodeCoverageLayer;
	nodeCoverageLayer.id = "layer-base";
	nodeCoverageLayer.name = "Base";
	nodeCoverageLayer.kind = VansAnimationLayerKind::Base;
	nodeCoverageAsset.layers.push_back(nodeCoverageLayer);
	VansAnimationGraphSetDefinition nodeCoverageSet;
	nodeCoverageSet.id = "set-default";
	nodeCoverageSet.name = "Default";
	nodeCoverageSet.bindings.push_back({ "layer-base", "graph-node-coverage", true });
	nodeCoverageAsset.defaultGraphSetId = nodeCoverageSet.id;
	nodeCoverageAsset.graphSets.push_back(std::move(nodeCoverageSet));
	nlohmann::json nodeCoverageJson;
	std::string nodeCoverageError;
	if (!Expect(VansAnimatorIO::SerializeToJsonObject(
		nodeCoverageAsset, nodeCoverageJson, nodeCoverageError), nodeCoverageError.c_str()))
		return false;
	auto nodeCoverageDocument = Vans::EditorAPI::AnimationAuthoringBridge::DecodeAnimator(
		nodeCoverageJson.dump());
	if (!Expect(nodeCoverageDocument.success && nodeCoverageDocument.document,
		"Animation authoring DTO rejected a current runtime node type"))
		return false;
	const auto nodeCoverageEncoded = Vans::EditorAPI::AnimationAuthoringBridge::EncodeAnimator(
		*nodeCoverageDocument.document);
	if (!Expect(nodeCoverageEncoded.success
		&& nlohmann::json::parse(nodeCoverageEncoded.canonicalJson) == nodeCoverageJson,
		"Animation authoring DTO changed cached-pose or layered-blend node data"))
		return false;

	const VansAnimGraph& sourceGraph = *nodeCoverageAsset.graphs.front().graph;
	auto clonedGraph = sourceGraph.Clone();
	nlohmann::json sourceGraphJson;
	nlohmann::json clonedGraphJson;
	if (!Expect(clonedGraph != nullptr, "Typed animation graph clone failed"))
		return false;
	sourceGraph.SerializeToJsonObject(sourceGraphJson);
	clonedGraph->SerializeToJsonObject(clonedGraphJson);
	if (!Expect(sourceGraphJson == clonedGraphJson,
		"Typed animation graph clone changed authored graph data"))
		return false;
	auto* clonedSavePose = static_cast<AnimGraphSaveCachedPoseNode*>(
		clonedGraph->GetNode(savePoseId));
	clonedSavePose->m_CacheName = "IndependentCloneCache";
	if (!Expect(static_cast<const AnimGraphSaveCachedPoseNode*>(
			sourceGraph.GetNode(savePoseId))->m_CacheName == "BaseCache",
		"Typed animation graph clone shares mutable node definition state"))
		return false;

	auto invalidPoseGraph = sourceGraph.Clone();
	invalidPoseGraph->AddNode(std::make_unique<AnimGraphGoalNode>());
	std::string graphValidationError;
	if (!Expect(!VansAnimatorValidator::ValidateGraph(
			*invalidPoseGraph, AnimatorGraphAsset::Role::Pose,
			"Invalid Pose", graphValidationError),
		"Pose Graph accepted a target procedural node"))
		return false;
	if (!Expect(!VansAnimatorValidator::ValidateGraph(
			sourceGraph, AnimatorGraphAsset::Role::TargetPostProcess,
			"Invalid Target", graphValidationError),
		"Target Post Process Graph accepted a playback graph"))
		return false;

	nlohmann::json unknownPropertyJson = nodeCoverageJson;
	for (auto& node : unknownPropertyJson["graphs"][0]["graph"]["nodes"])
	{
		if (node["type"] == "Clip")
		{
			node["properties"]["unknownProperty"] = true;
			break;
		}
	}
	AnimatorAssetData rejectedUnknownProperty;
	std::string strictSchemaError;
	if (!Expect(!VansAnimatorIO::DeserializeFromJsonObject(
			unknownPropertyJson, rejectedUnknownProperty, strictSchemaError),
		"Animation graph node silently accepted an unknown property"))
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

	std::size_t currentProjectAnimatorCount = 0;
	for (const char* projectName : {
		"AnimationV2Project", "DustV3Project", "DemoHallProject", "SponzaProject" })
	{
		const fs::path assetsRoot = workspace / projectName / "Assets";
		if (!fs::exists(assetsRoot))
			continue;
		std::error_code assetScanError;
		for (fs::recursive_directory_iterator iterator(assetsRoot, assetScanError), end;
			!assetScanError && iterator != end; iterator.increment(assetScanError))
		{
			if (!iterator->is_regular_file() || iterator->path().extension() != ".vanimator")
				continue;
			++currentProjectAnimatorCount;
			AnimatorAssetData currentAsset;
			std::string currentError;
			const nlohmann::json currentJson = nlohmann::json::parse(readText(iterator->path()));
			if (!Expect(VansAnimatorIO::DeserializeFromJsonObject(
					currentJson, currentAsset, currentError), currentError.c_str()))
				return false;
			nlohmann::json currentCanonical;
			if (!Expect(VansAnimatorIO::SerializeToJsonObject(
					currentAsset, currentCanonical, currentError), currentError.c_str()))
				return false;
		}
		if (!Expect(!assetScanError, "Unable to scan current project Animator assets"))
			return false;
	}
	if (!Expect(currentProjectAnimatorCount > 0,
		"Current project Animator asset audit found no source assets"))
		return false;

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
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
	context.deltaTime = 0.25f;
	instance.EvaluateFrame(context);
	instance.EvaluateFrame(context);
	if (!Expect(instance.GetPrimaryClipName() == "Active",
		"Animation graph selected the inactive branch"))
		return false;
	if (!ExpectNear(instance.GetPrimaryPlaybackTime(), 0.25f, 0.0001f,
        "Active animation graph node did not advance"))
        return false;
	return true;
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
        VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
	context.deltaTime = 0.25f;
	first.EvaluateFrame(context);
	context.deltaTime = 0.75f;
	second.EvaluateFrame(context);
	if (!ExpectNear(first.GetPrimaryPlaybackTime(), 0.25f, 0.0001f,
        "First animation graph instance lost its independent time"))
        return false;
	if (!ExpectNear(second.GetPrimaryPlaybackTime(), 0.75f, 0.0001f,
        "Second animation graph instance was contaminated by the first"))
        return false;

    first.Reset();
	if (!ExpectNear(first.GetPrimaryPlaybackTime(), 0.0f, 0.0001f,
        "Reset did not clear only the target graph instance"))
        return false;
	return ExpectNear(second.GetPrimaryPlaybackTime(), 0.75f, 0.0001f,
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
    const int outputId = graph.AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
	context.deltaTime = 0.25f;
	instance.EvaluateFrame(context);
	if (!ExpectNear(instance.GetPrimaryPlaybackTime(), 0.5f, 0.0001f,
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
        VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
	skeleton.sourceSkeletonGuid = "test-skeleton";
    skeleton.bones.resize(3);
    skeleton.bones[0].name = "root";
	skeleton.bones[0].guid = "00000000-0000-4000-8000-000000000001";
	skeleton.bones[0].canonicalPath = "root";
    skeleton.bones[0].id = 0;
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[0].children = { 1 };
    skeleton.bones[1].name = "spine";
	skeleton.bones[1].guid = "00000000-0000-4000-8000-000000000002";
	skeleton.bones[1].canonicalPath = "root/spine";
    skeleton.bones[1].id = 1;
    skeleton.bones[1].parentIndex = 0;
    skeleton.bones[1].children = { 2 };
    skeleton.bones[2].name = "arm";
	skeleton.bones[2].guid = "00000000-0000-4000-8000-000000000003";
	skeleton.bones[2].canonicalPath = "root/spine/arm";
    skeleton.bones[2].id = 2;
    skeleton.bones[2].parentIndex = 1;
    for (size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        skeleton.bones[index].localTransform = glm::mat4(1.0f);
        skeleton.bones[index].offsetMatrix = glm::mat4(1.0f);
        skeleton.boneNameToIndex[skeleton.bones[index].name] = static_cast<int>(index);
    }
    skeleton.BuildTopologicalOrder();
	skeleton.RebuildIdentityMapsAndSignature();
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

bool TestAnimationPreviewRigSessionContract(
	VansGraphics::VansAnimationController& controller,
	const VansGraphics::Skeleton& skeleton,
	const VansGraphics::VansAnimationRigAsset& rig,
	const fs::path& rigPath,
	bool retargetTarget)
{
	using namespace VansGraphics;
	using namespace Vans::EditorAPI;
	auto& project = Vans::VansProjectManager::Get();
	project.CloseProject();
	constexpr AnimationPreviewSessionId sessionId = 91001;
	AnimationPreviewRigAuthoringService rigAuthoring;
	struct SessionGuard
	{
		AnimationPreviewRigAuthoringService& authoring;
		VansAnimationController& controller;
		~SessionGuard()
		{
			std::string ignored;
			authoring.EndSession(91001, controller, ignored);
			Vans::VansAssetDocumentRegistry::Get().Clear();
			Vans::VansProjectManager::Get().CloseProject();
		}
	} guard{ rigAuthoring, controller };
	std::string error;
	Vans::VansAssetRecord record;
	if (!Expect(Vans::VansAssetGuid::TryParse(controller.GetAnimationRigAssetGuid(), record.guid),
		"Memory-compiled controller lost its target Rig GUID"))
		return false;
	record.type = Vans::VansAssetType::AnimationRig;
	record.state = Vans::VansAssetState::CpuReady;
	// 同时覆盖当前 Rig 的 sourcePath，以及显式作者文档优先于 source/artifact 的情况。
	record.sourcePath = retargetTarget ? rigPath.parent_path() / "wrong-source.vanimrig" : rigPath;
	if (retargetTarget) record.authoringPath = rigPath;
	record.artifactPath = rigPath.parent_path() / "wrong-artifact.vanimrig";
	project.SetPackagedAssetRecords({ record });
	auto memoryRig = rig;
	memoryRig.name += retargetTarget ? " Retarget Memory Preview" : " Direct Memory Preview";
	if (!Expect(project.GetAssetObjectRepository().Publish<VansAnimationRigAsset>(
		record.guid, record.type, 1, std::make_shared<const VansAnimationRigAsset>(memoryRig),
		{}, error).IsValid(), error.c_str()))
		return false;

	AnimationPreviewRigContext context;
	const AnimationPreviewWriteToken writeToken{ sessionId, 0 };
	context.writeToken = writeToken;
	context.retargetEnabled = retargetTarget;
	Vans::VansIOAudit::Reset();
	if (!Expect(rigAuthoring.BeginSession(writeToken, controller, error),
		error.c_str()))
		return false;
	auto snapshot = rigAuthoring.GetSnapshot(
		context, controller, skeleton);
	std::string workingJson;
	if (!Expect(snapshot.available && snapshot.rigAssetGuid == record.guid.ToString()
		&& fs::path(snapshot.rigAssetPath) == rigPath.lexically_normal()
		&& snapshot.retargetEnabled == retargetTarget && snapshot.sockets.size() == 1
		&& rigAuthoring.GetWorkingCanonicalJson(sessionId, workingJson, error)
		&& nlohmann::json::parse(workingJson).at("name") == memoryRig.name,
		"Preview did not resolve the target Rig document from its GUID and memory object"))
		return false;

	AnimationPreviewRigSocketTransformRequest edit;
	edit.sessionId = sessionId;
	edit.socketGuid = rig.sockets.front().guid;
	edit.space = RuntimeTransformSpace::Local;
	edit.transform = snapshot.sockets.front().localTransform;
	edit.transform.position.x += 2.0f;
	const auto edited = rigAuthoring.SetSocketTransform(
		context, controller, skeleton, edit);
	if (!Expect(edited.success
		&& rigAuthoring.EndSession(sessionId, controller, error)
		&& Vans::VansIOAudit::Snapshot().empty()
		&& std::abs(controller.GetAnimationRig()->sockets.front().localTransform[3].x
			- rig.sockets.front().positionLocal.x) < 1.0e-6f,
		"Preview start/edit/stop performed I/O or failed to restore the target Socket"))
		return false;

	// 通过预览返回的文档路径走实际保存服务，保存后结束会话须保留已采用的修改。
	if (!Expect(rigAuthoring.BeginSession(writeToken, controller, error),
		error.c_str()))
		return false;
	const auto savedEdit = rigAuthoring.SetSocketTransform(
		context, controller, skeleton, edit);
	if (!Expect(savedEdit.success && rigAuthoring.GetWorkingCanonicalJson(
		sessionId, workingJson, error), error.c_str()))
		return false;
	auto document = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(snapshot.rigAssetPath);
	if (!Expect(document && document->sourceDocument.IsLoaded(),
		"Resolved preview Rig document could not be opened for explicit Save"))
		return false;
	const auto documentEdit = Vans::VansAssetDocumentEditService::ReplaceRoot(
		document->sourceDocument,
		Vans::DecodeSerializedValueJson(nlohmann::json::parse(workingJson)));
	if (!Expect(static_cast<bool>(documentEdit), documentEdit.message.c_str()))
		return false;
	const auto saved = Vans::VansAnimationRigSaveService::Save(document);
	VansAnimationRigAsset diskRig;
	if (!Expect(saved.success && saved.published
		&& VansAnimationRigStorage::Load(rigPath, diskRig, error)
		&& diskRig.name == memoryRig.name && diskRig.sockets.size() == 1
		&& std::abs(diskRig.sockets.front().positionLocal.x - edit.transform.position.x) < 1.0e-6f
		&& AnimationPreviewAdoptService::AdoptRig(
			rigAuthoring,
			writeToken, { sessionId, savedEdit.acceptedRevision }, controller).success
		&& rigAuthoring.EndSession(sessionId, controller, error)
		&& std::abs(controller.GetAnimationRig()->sockets.front().localTransform[3].x
			- edit.transform.position.x) < 1.0e-6f,
		"Preview Rig save/adopt/stop did not preserve the authored target Socket"))
		return false;

	// 通用 Rig 定义编辑同样受编译与 revision 保护，取消时恢复已保存的基线。
	Vans::VansIOAudit::Reset();
	if (!Expect(rigAuthoring.BeginSession(writeToken, controller, error), error.c_str())) return false;
	auto candidate = nlohmann::json::parse(workingJson);
	candidate["sockets"][0]["boneGuid"] = "00000000-0000-4000-8000-000000000099";
	if (!Expect(!rigAuthoring.SetDefinition(
		context, controller, skeleton, 0, candidate.dump()).success,
		"Invalid Rig definition replaced the last good Rig")) return false;
	candidate = nlohmann::json::parse(workingJson);
	candidate["name"] = "Edited Rig definition";
	const auto definitionEdit = rigAuthoring.SetDefinition(
		context, controller, skeleton, 0, candidate.dump());
	if (!Expect(definitionEdit.success && definitionEdit.acceptedRevision == 1
		&& !rigAuthoring.SetDefinition(
			context, controller, skeleton, 0, candidate.dump()).success
		&& rigAuthoring.EndSession(sessionId, controller, error)
		&& Vans::VansIOAudit::Snapshot().empty()
		&& std::abs(controller.GetAnimationRig()->sockets.front().localTransform[3].x - edit.transform.position.x) < 1.0e-6f,
		"Rig definition revision, rollback, or memory-only authoring contract failed")) return false;

	// 每个 EngineAPI owner 必须拥有独立作者会话；相同 session id 不得跨实例互相删除。
	AnimationPreviewRigAuthoringService isolatedAuthoring;
	if (!Expect(rigAuthoring.BeginSession(writeToken, controller, error)
		&& isolatedAuthoring.BeginSession(writeToken, controller, error)
		&& isolatedAuthoring.EndSession(sessionId, error)
		&& rigAuthoring.GetWorkingCanonicalJson(sessionId, workingJson, error)
		&& rigAuthoring.EndSession(sessionId, controller, error),
		"Animation preview Rig authoring instances shared session state"))
		return false;

	record.sourcePath.clear();
	record.authoringPath.clear();
	project.SetPackagedAssetRecords({ record });
	if (!Expect(!rigAuthoring.BeginSession(writeToken, controller, error)
		&& error.find("authoring document") != std::string::npos
		&& !rigAuthoring.GetSnapshot(
			context, controller, skeleton).available,
		"Preview accepted a cooked artifact as an authoring document or retained a failed session"))
		return false;
	std::cout << "[ForestContractTests] Animation preview Rig "
		<< (retargetTarget ? "retarget target" : "direct target")
		<< ": GUID resolution, memory-only preview, Socket rollback and explicit Save passed\n";
	return true;
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
	const auto baseClipAsset = std::make_shared<VansAnimationClipAsset>();
	baseClipAsset->clip = makeClip("Base", 0.0f, 0.0f);
	baseClipAsset->skeleton = skeleton;
	const auto overlayClipAsset = std::make_shared<VansAnimationClipAsset>();
	overlayClipAsset->clip = makeClip("Overlay", 10.0f, 20.0f);
	overlayClipAsset->skeleton = skeleton;

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
	VansAnimationRigAsset rig;
	rig.name = "RuntimeCompilerRig";
	rig.skeletonGuid = skeleton.sourceSkeletonGuid;
	VansRigSocketDefinition previewSocket;
	previewSocket.guid = "88888888-8888-4888-8888-888888888801";
	previewSocket.name = "PreviewSocket";
	previewSocket.boneGuid = skeleton.bones[2].guid;
	previewSocket.positionLocal = glm::vec3(0.5f, 0.0f, 0.0f);
	rig.sockets.push_back(previewSocket);
	const fs::path rigPath = temporary.path / "runtime.vanimrig";
	if (!Expect(VansAnimationRigStorage::SaveAtomic(rigPath, rig, error), error.c_str()))
		return false;

    AnimatorAssetData asset;
    asset.name = "RuntimeCompiler";
	asset.animationRigGuid = "88888888-8888-4888-8888-888888888888";
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
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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
    baseLayer.kind = VansAnimationLayerKind::Base;
    baseLayer.rootMotion = VansLayerRootMotionMode::Base;
    asset.layers.push_back(baseLayer);
    VansAnimationLayerDefinition overlayLayer;
    overlayLayer.id = "layer-upper";
    overlayLayer.name = "Upper";
    overlayLayer.kind = VansAnimationLayerKind::Overlay;
    overlayLayer.maskGuid = "77777777-7777-4777-8777-777777777777";
    overlayLayer.maskPathHint = "upper.vbonemask";
    overlayLayer.useWeightParameter = true;
    overlayLayer.weightParameter = "UpperBodyWeight";
    overlayLayer.rootMotion = VansLayerRootMotionMode::Ignore;
    asset.layers.push_back(overlayLayer);
	VansAnimationGraphSetDefinition graphSet;
	graphSet.id = "graph-set-default";
	graphSet.name = "Default";
	graphSet.bindings = {
		{ "layer-base", "graph-base", true },
		{ "layer-upper", "graph-upper", true }
	};
	asset.defaultGraphSetId = graphSet.id;
	asset.graphSets.push_back(std::move(graphSet));
    VansAnimationSlotDefinition slot;
    slot.id = "slot-upper";
    slot.name = "Upper";
    slot.layerId = "layer-upper";
    asset.slots.push_back(slot);

	const auto clipResolver = [&](const AnimatorClipRef& reference,
		std::shared_ptr<const VansAnimationClipAsset>& clip, std::string& resolveError)
    {
		if (reference.assetGuid == "55555555-5555-4555-8555-555555555555") clip = baseClipAsset;
		else if (reference.assetGuid == "66666666-6666-4666-8666-666666666666") clip = overlayClipAsset;
        else { resolveError = "Unexpected Clip GUID"; return false; }
        return true;
    };
	const auto maskObject = std::make_shared<const VansBoneMaskAsset>(mask);
	const auto rigObject = std::make_shared<const VansAnimationRigAsset>(rig);
	const auto maskResolver = [&](const VansAnimationLayerDefinition& layer,
		std::shared_ptr<const VansBoneMaskAsset>& resolvedMask, std::string& resolveError)
    {
        if (layer.maskGuid != "77777777-7777-4777-8777-777777777777")
        {
            resolveError = "Unexpected Bone Mask GUID";
            return false;
        }
		resolvedMask = maskObject;
        return true;
    };
	VansAnimatorRuntimeCompileOptions runtimeOptions;
	runtimeOptions.rigResolver = [&](const std::string& guid, std::string& resolveError)
	{
		if (guid != asset.animationRigGuid)
		{
			resolveError = "Unexpected Animation Rig GUID";
			return std::shared_ptr<const VansAnimationRigAsset>{};
		}
		return rigObject;
	};
	Vans::VansIOAudit::Reset();
    auto controller = VansAnimatorRuntimeCompiler::Compile(asset, skeleton,
        clipResolver, maskResolver, runtimeOptions, error);
    if (!Expect(controller && controller->GetLayerCount() == 2, error.c_str()))
        return false;
	if (!Expect(Vans::VansIOAudit::Snapshot().empty(),
		"Animator runtime compilation performed I/O after asset memory publication"))
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
        asset, skeleton, {}, maskResolver, runtimeOptions, error),
        "Full-graph runtime compilation accepted a missing Clip resolver"))
        return false;

    bool clipResolverInvoked = false;
    bool maskResolverInvoked = false;
	auto targetRig = rig;
	targetRig.name = "RetargetPreviewRig";
	targetRig.sockets.front().positionLocal.x = 0.75f;
	const fs::path targetRigPath = temporary.path / "target.vanimrig";
	if (!Expect(VansAnimationRigStorage::SaveAtomic(targetRigPath, targetRig, error), error.c_str()))
		return false;
    VansAnimatorRuntimeCompileOptions targetOptions;
    targetOptions.mode = VansAnimatorRuntimeCompileMode::ExternalPoseTarget;
	targetOptions.animationRigGuidOverride = "99999999-9999-4999-8999-999999999999";
	targetOptions.rigResolver = [&](const std::string& guid, std::string& resolveError)
	{
		if (guid == targetOptions.animationRigGuidOverride)
			return std::make_shared<const VansAnimationRigAsset>(targetRig);
		resolveError = "Retarget preview resolved the source Animator Rig instead of the target Rig";
		return std::shared_ptr<const VansAnimationRigAsset>{};
	};
    auto targetController = VansAnimatorRuntimeCompiler::Compile(
        asset,
        skeleton,
		[&](const AnimatorClipRef&,
			std::shared_ptr<const VansAnimationClipAsset>&, std::string& resolveError)
        {
            clipResolverInvoked = true;
            resolveError = "External-pose target must not resolve source Clips";
            return false;
        },
		[&](const VansAnimationLayerDefinition&,
			std::shared_ptr<const VansBoneMaskAsset>&, std::string& resolveError)
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
	if (!Expect(targetController->GetClipNames().empty() && !targetController->HasGraphSets()
        && ExpectNear(targetController->GetFloat("UpperBodyWeight"), 0.0f, 0.0001f,
            "External-pose target did not preserve authored parameters"),
        "External-pose target retained the source Clip/Layer runtime"))
        return false;

    std::vector<glm::mat4> externalPose(skeleton.bones.size(), glm::mat4(1.0f));
    if (!Expect(targetController->SubmitExternalModelPose(
        externalPose, skeleton, 0.016f, VansExternalPoseEvaluationMode::TargetPostProcess)
        && targetController->GetCachedGlobalTransforms().size() == skeleton.bones.size(),
        "External-pose target did not accept a retargeted model-space pose"))
		return false;
	return TestAnimationPreviewRigSessionContract(*controller, skeleton, rig, rigPath, false)
		&& TestAnimationPreviewRigSessionContract(*targetController, skeleton, targetRig, targetRigPath, true);
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
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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

	VansAnimationLayerSetup base;
    base.definition.id = "layer-base";
    base.definition.name = "Base";
    base.definition.kind = VansAnimationLayerKind::Base;
    base.definition.rootMotion = VansLayerRootMotionMode::Base;
    base.definition.nodeTracks = VansLayerNodeTrackMode::Override;
	auto baseGraph = buildGraph("Base");

	VansAnimationLayerSetup overlay;
    overlay.definition.id = "layer-upper";
    overlay.definition.name = "Upper";
    overlay.definition.kind = VansAnimationLayerKind::Overlay;
    overlay.definition.blendMode = VansLayerBlendMode::Override;
    overlay.definition.fixedWeight = 1.0f;
    overlay.definition.rootMotion = VansLayerRootMotionMode::Ignore;
    overlay.definition.events = VansLayerEventMode::ActiveOnly;
	auto overlayGraph = buildGraph("Overlay");
    overlay.mask = upperMask;

    VansAnimationController controller;
    controller.AddClip("Base", buildClip("Base", 0.0f, 0.0f, 0.0f));
    controller.AddClip("Overlay", buildClip("Overlay", 100.0f, 10.0f, 20.0f));
	std::vector<VansAnimationLayerSetup> layers;
    layers.push_back(std::move(base));
    layers.push_back(std::move(overlay));
    std::string error;
	std::vector<std::unique_ptr<VansAnimGraph>> graphs;
	graphs.push_back(std::move(baseGraph));
	graphs.push_back(std::move(overlayGraph));
	if (!Expect(InstallTestGraphSet(controller, std::move(layers),
		{ "graph-base", "graph-upper" }, std::move(graphs), error), error.c_str()))
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

	VansAnimationLayerSetup invalidBase;
    invalidBase.definition.id = "not-base";
    invalidBase.definition.name = "OverlayFirst";
    invalidBase.definition.kind = VansAnimationLayerKind::Overlay;
	auto invalidGraph = buildGraph("Base");
    invalidBase.mask = upperMask;
	std::vector<VansAnimationLayerSetup> invalidLayers;
    invalidLayers.push_back(std::move(invalidBase));
	std::vector<std::unique_ptr<VansAnimGraph>> invalidGraphs;
	invalidGraphs.push_back(std::move(invalidGraph));
	return Expect(!InstallTestGraphSet(controller, std::move(invalidLayers),
		{ "invalid-graph" }, std::move(invalidGraphs), error),
        "Layer Stack accepted an Animator without exactly one Base layer");
}

bool TestAnimationLayerRootReferenceFrameContract()
{
    using namespace VansGraphics;

    // 同一动作仅改变被提取的根骨参考系，角色空间的瞄准结果必须保持不变。
    Skeleton skeleton;
    skeleton.sourceSkeletonGuid = "layer-root-frame";
    const char* names[] = { "scene", "root", "pelvis", "spine", "hand", "leg" };
    const int parents[] = { -1, 0, 1, 2, 3, 2 };
    skeleton.bones.resize(6);
    std::vector<VansBoneTransform> bind(6);
    bind[0].rotation = glm::angleAxis(glm::radians(31.0f), glm::vec3(0, 1, 0));
    bind[0].translation = glm::vec3(3, 0, -2);
    bind[1].rotation = glm::angleAxis(glm::radians(23.0f), glm::vec3(0, 1, 0));
    bind[1].translation = glm::vec3(2, 0, 3);
    bind[2].translation = glm::vec3(0, 2, 0);
    bind[3].translation = glm::vec3(0, 1, 0);
    bind[4].translation = glm::vec3(1, 1, 0);
    bind[5].translation = glm::vec3(0, -2, 0);
    for (int index = 0; index < 6; ++index)
    {
        auto& bone = skeleton.bones[index];
        bone.name = names[index]; bone.id = index; bone.parentIndex = parents[index];
        bone.guid = "layer-root-frame-" + std::to_string(index);
        bone.canonicalPath = parents[index] < 0 ? names[index]
            : skeleton.bones[parents[index]].canonicalPath + "/" + names[index];
        bone.localTransform = VansPoseMath::Compose(bind[index]);
        bone.offsetMatrix = glm::mat4(1.0f);
        skeleton.boneNameToIndex[bone.name] = index;
        if (parents[index] >= 0) skeleton.bones[parents[index]].children.push_back(index);
    }
    skeleton.BuildTopologicalOrder();
    skeleton.RebuildIdentityMapsAndSignature();

    auto makeClip = [&](const std::string& name, float rootYaw, bool normalized, bool upper)
    {
        VansAnimationClip clip;
        clip.clipName = name; clip.duration = 2.0f;
        clip.rootMotion.enabled = true; clip.rootMotion.boneName = "root";
        clip.rootMotion.extractTranslation = clip.rootMotion.extractRotation = true;
        clip.boneKeyframes.resize(bind.size());
        for (std::size_t bone = 0; bone < bind.size(); ++bone)
        {
            for (float time : { 0.0f, 2.0f })
            {
                VansBoneTransform transform = bind[bone];
                if (bone == 1)
                {
                    // 缩放属于姿态，不能随根平移和根旋转一起清除。
                    transform.scale = glm::vec3(1.1f);
                    if (!normalized)
                    {
                        transform.translation += glm::vec3(4.0f + time, 0, 2.0f * time);
                        transform.rotation = bind[bone].rotation * glm::angleAxis(
                            glm::radians(rootYaw + time * 8.0f), glm::vec3(0, 1, 0));
                    }
                }
                if (bone == 3) transform.rotation = glm::angleAxis(
                    glm::radians(upper ? 13.0f : -6.0f), glm::vec3(0, 1, 0));
                if (bone == 4) transform.rotation = glm::angleAxis(
                    glm::radians(upper ? -35.0f : 22.0f), glm::vec3(1, 0, 0));
                if (bone == 5) transform.rotation = glm::angleAxis(
                    glm::radians(upper ? 67.0f : -12.0f), glm::vec3(1, 0, 0));
                clip.boneKeyframes[bone].push_back(
                    { time, transform.translation, transform.rotation, transform.scale });
            }
        }
        return clip;
    };
    auto makeGraph = [](const std::string& clipName)
    {
        auto graph = std::make_unique<VansAnimGraph>();
        auto clip = std::make_unique<AnimGraphClipNode>();
        clip->m_ClipName = clipName;
        const int input = graph->AddNode(std::move(clip));
        const int output = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        graph->AddLink(input, 0, output, 0);
        return graph;
    };
    std::string error;
    auto makeController = [&](float yaw, bool normalized, bool withOverlay,
                              VansLayerBlendMode mode, VansAdditiveReferenceMode referenceMode)
    {
        auto controller = std::make_unique<VansAnimationController>();
        controller->AddClip("Turn", makeClip("Turn", yaw, normalized, false));
        controller->AddClip("Aim", makeClip("Aim", -37.0f, normalized, true));
        controller->AddClip("Reference", makeClip("Reference", 69.0f, normalized, false));
        VansAnimationLayerSetup base;
        base.definition.id = "layer-base"; base.definition.name = "Base";
        base.definition.kind = VansAnimationLayerKind::Base;
        base.definition.rootMotion = VansLayerRootMotionMode::Base;
        std::vector<VansAnimationLayerSetup> layers;
        layers.push_back(std::move(base));
        std::vector<std::string> ids{ "graph-base" };
        std::vector<std::unique_ptr<VansAnimGraph>> graphs;
        graphs.push_back(makeGraph("Turn"));
        if (withOverlay)
        {
            VansAnimationLayerSetup overlay;
            overlay.definition.id = "layer-upper"; overlay.definition.name = "Upper";
            overlay.definition.kind = VansAnimationLayerKind::Overlay;
            overlay.definition.blendMode = mode;
            overlay.definition.rotationSpace = VansRotationBlendSpace::Mesh;
            overlay.definition.rootMotion = VansLayerRootMotionMode::Ignore;
            overlay.definition.additiveReference = referenceMode;
            overlay.definition.referenceClipName = "Reference";
            overlay.definition.referenceTime = 0.25f;
            VansBoneMaskAsset mask;
            mask.id = "mask-upper"; mask.name = "Upper";
            mask.explicitWeights = { { "spine", 0.25f }, { "hand", 1.0f } };
            overlay.mask = std::move(mask);
            layers.push_back(std::move(overlay));
            ids.push_back("graph-upper"); graphs.push_back(makeGraph("Aim"));
        }
        if (!InstallTestGraphSet(*controller, std::move(layers), std::move(ids), std::move(graphs), error))
            return std::unique_ptr<VansAnimationController>{};
        controller->EnableRootMotion(true);
        controller->Play();
        return controller;
    };
    auto matrixError = [](const glm::mat4& a, const glm::mat4& b)
    {
        float result = 0;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            result = std::max(result, std::abs(a[c][r] - b[c][r]));
        return result;
    };
    int cases = 0;
    for (float yaw : { 0.0f, 90.0f, -90.0f, 180.0f, -180.0f })
    {
        for (const auto reference : { VansAdditiveReferenceMode::BindPose,
            VansAdditiveReferenceMode::FirstFrame, VansAdditiveReferenceMode::ClipTime,
            VansAdditiveReferenceMode::ReferenceClip })
        {
            const auto mode = reference == VansAdditiveReferenceMode::BindPose
                ? VansLayerBlendMode::Override : VansLayerBlendMode::Additive;
            auto raw = makeController(yaw, false, true, mode, reference);
            auto normalized = makeController(yaw, true, true, mode, reference);
            auto locomotion = makeController(yaw, false, false, mode, reference);
            if (!Expect(raw && normalized && locomotion, error.c_str())) return false;
            for (float dt : { 0.0f, 0.25f, 0.25f })
            {
                raw->Update(dt, skeleton); normalized->Update(dt, skeleton); locomotion->Update(dt, skeleton);
                for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone)
                {
                    if (!Expect(matrixError(raw->GetCachedGlobalTransform(static_cast<int>(bone)),
                        normalized->GetCachedGlobalTransform(static_cast<int>(bone))) < .0001f,
                        ("Layer retained extracted root facing: yaw=" + std::to_string(yaw)
                            + " bone=" + skeleton.bones[bone].name + " reference="
                            + std::to_string(static_cast<int>(reference))).c_str())) return false;
                }
                for (int bone : { 0, 1, 2, 5 })
                    if (!Expect(matrixError(raw->GetCachedGlobalTransform(bone),
                        locomotion->GetCachedGlobalTransform(bone)) < .0001f,
                        "Root-frame correction changed an unmasked locomotion bone")) return false;
                if (!Expect(raw->HasRootMotionDelta() == locomotion->HasRootMotionDelta()
                    && glm::length(raw->GetRootMotionDelta() - locomotion->GetRootMotionDelta()) < .000001f
                    && glm::length(raw->GetRootRotationDelta() - locomotion->GetRootRotationDelta()) < .000001f,
                    "Root-frame correction changed extracted movement or turning")) return false;
                VansBoneTransform root;
                if (!Expect(VansPoseMath::TryDecompose(glm::inverse(raw->GetCachedGlobalTransform(0))
                    * raw->GetCachedGlobalTransform(1), root)
                    && glm::length(root.scale - glm::vec3(1.1f)) < .0001f,
                    "Root-frame correction discarded authored root scale")) return false;
            }
            if (!Expect(raw->HasRootMotionDelta() && glm::length(raw->GetRootMotionDelta()) > .1f
                && std::abs(raw->GetRootRotationDelta().w) < .99999f,
                "Root-frame fixture did not exercise moving and rotating root motion")) return false;
            ++cases;
        }
    }
    // Viewmodel-style clips may intentionally author a coordinate-basis
    // transform on the animated root while root motion remains disabled.  The
    // generic opt-out must preserve that pose; normalization stays enabled by
    // default for locomotion and all existing serialized components.
    auto preservedRoot = makeController(90.0f, false, false,
        VansLayerBlendMode::Override, VansAdditiveReferenceMode::BindPose);
    if (!Expect(preservedRoot != nullptr, error.c_str())) return false;
    preservedRoot->EnableRootMotion(false);
    preservedRoot->SetNormalizeRootPose(false);
    preservedRoot->Update(0.0f, skeleton);
    VansBoneTransform authoredRoot;
    if (!Expect(!preservedRoot->ShouldNormalizeRootPose()
        && VansPoseMath::TryDecompose(
            glm::inverse(preservedRoot->GetCachedGlobalTransform(0))
                * preservedRoot->GetCachedGlobalTransform(1),
            authoredRoot)
        && glm::length(authoredRoot.translation - bind[1].translation) > 3.5f
        && std::abs(glm::dot(glm::normalize(authoredRoot.rotation),
            glm::normalize(bind[1].rotation))) < 0.8f,
        "Animation root normalization opt-out discarded the authored root basis"))
        return false;
    std::cout << "[ForestContractTests] Layer root reference frame: " << cases
        << " cases, mesh override/additive, partial mask, root ancestry, scale and configurable root pose preserved\n";
    return true;
}

bool TestAnimationGraphSetSwitchRuntimeContract()
{
	using namespace VansGraphics;
	const Skeleton skeleton = BuildLayerContractSkeleton();
	auto makeClip = [](const std::string& name, float armStart, float armEnd)
	{
		VansAnimationClip clip;
		clip.clipName = name;
		clip.duration = 2.0f;
		clip.boneKeyframes.resize(3);
		for (int bone = 0; bone < 3; ++bone)
		{
			const float start = bone == 2 ? armStart : 0.0f;
			const float end = bone == 2 ? armEnd : 0.0f;
			clip.boneKeyframes[bone] = {
				{ 0.0f, glm::vec3(start, 0.0f, 0.0f),
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) },
				{ 2.0f, glm::vec3(end, 0.0f, 0.0f),
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) }
			};
		}
		return clip;
	};
	auto makeGraph = [](const std::string& clipName)
	{
		auto graph = std::make_unique<VansAnimGraph>();
		auto clip = std::make_unique<AnimGraphClipNode>();
		clip->m_ClipName = clipName;
		const int clipId = graph->AddNode(std::move(clip));
		const int outputId = graph->AddNode(
			VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
		graph->AddLink(clipId, 0, outputId, 0);
		return graph;
	};
	auto makeGraphSet = [&](const std::string& id, const std::string& graphId,
		const std::string& clipName)
	{
		VansAnimationGraphSetSetup graphSet;
		graphSet.definition.id = id;
		graphSet.definition.name = id;
		graphSet.definition.bindings.push_back({ "layer-base", graphId, true });
		VansAnimationGraphBindingSetup binding;
		binding.definition = graphSet.definition.bindings.front();
		binding.graph = makeGraph(clipName);
		graphSet.bindings.push_back(std::move(binding));
		return graphSet;
	};

	VansAnimationController controller;
	controller.AddClip("Exploration", makeClip("Exploration", 0.0f, 4.0f));
	controller.AddClip("Combat", makeClip("Combat", 10.0f, 18.0f));
	controller.AddClip("Cinematic", makeClip("Cinematic", 20.0f, 28.0f));
	VansAnimationLayerSetup base;
	base.definition.id = "layer-base";
	base.definition.name = "Base";
	base.definition.kind = VansAnimationLayerKind::Base;
	std::vector<VansAnimationLayerSetup> layers;
	layers.push_back(std::move(base));
	std::vector<VansAnimationGraphSetSetup> graphSets;
	graphSets.push_back(makeGraphSet("exploration", "graph-exploration", "Exploration"));
	graphSets.push_back(makeGraphSet("combat", "graph-combat", "Combat"));
	graphSets.push_back(makeGraphSet("cinematic", "graph-cinematic", "Cinematic"));
	VansGraphSetTransitionPolicy transition;
	transition.duration = 1.0f;
	transition.curve = VansGraphSetBlendCurve::Linear;
	transition.phase = VansGraphSetPhasePolicy::MatchNormalizedTime;
	transition.events = VansGraphSetEventPolicy::DominantSource;
	transition.interruption = VansGraphSetInterruptionPolicy::QueueLatest;
	VansGraphSetTransitionRule combatToCinematic;
	combatToCinematic.fromGraphSetId = "combat";
	combatToCinematic.toGraphSetId = "cinematic";
	combatToCinematic.policy = transition;
	combatToCinematic.policy.duration = 0.25f;
	combatToCinematic.policy.interruption = VansGraphSetInterruptionPolicy::Reject;
	std::vector<VansGraphSetTransitionRule> transitionRules;
	transitionRules.push_back(combatToCinematic);
	std::string error;
	if (!Expect(controller.SetAnimationGraphSets(
		std::move(layers), std::move(graphSets), "exploration", transition,
		std::move(transitionRules), error),
		error.c_str()))
		return false;
	controller.Play();
	controller.Update(0.0f, skeleton);
	controller.Update(0.5f, skeleton);
	if (!Expect(controller.SwitchGraphSet("combat") == VansGraphSetSwitchResult::Started,
		"Graph Set switch request was not accepted"))
		return false;
	controller.Update(0.5f, skeleton);
	if (!Expect(controller.GetActiveGraphSetId() == "exploration"
		&& controller.GetIncomingGraphSetId() == "combat"
		&& controller.IsGraphSetTransitioning(),
		"Graph Set transition did not expose Active/Incoming state"))
		return false;
	// Exploration 在 t=1.0 为 2，Combat 经归一化交接后推进到 t=1.0 为 14；50% 为 8。
	const float blendedArmX = controller.GetCachedGlobalTransform(2)[3].x;
	if (std::fabs(blendedArmX - 8.0f) > 0.001f)
		std::cerr << "[ForestContractTests] Graph Set blended arm X: " << blendedArmX << '\n';
	if (!ExpectNear(blendedArmX, 8.0f, 0.001f,
		"Dual Graph Set evaluation did not blend the composed poses"))
		return false;
	if (!Expect(controller.SwitchGraphSet("cinematic") == VansGraphSetSwitchResult::Queued,
		"QueueLatest did not retain the latest Graph Set request"))
		return false;
	controller.Update(0.5f, skeleton);
	if (!Expect(controller.GetActiveGraphSetId() == "combat"
		&& controller.GetIncomingGraphSetId() == "cinematic"
		&& controller.IsGraphSetTransitioning(),
		"Graph Set transition did not promote Incoming and start the queued request"))
		return false;
	if (!ExpectNear(controller.GetCachedGlobalTransform(2)[3].x, 16.0f, 0.001f,
		"Completed Graph Set transition did not output the incoming pose"))
		return false;
	if (!Expect(controller.SwitchGraphSet("exploration") == VansGraphSetSwitchResult::Rejected,
		"Pair-specific Reject policy did not reject an interrupting request"))
		return false;
	controller.Update(0.125f, skeleton);
	if (!ExpectNear(controller.GetGraphSetTransitionProgress(), 0.5f, 0.001f,
		"Pair-specific Graph Set transition duration was not applied"))
		return false;
	controller.Update(0.125f, skeleton);
	if (!Expect(controller.GetActiveGraphSetId() == "cinematic"
		&& controller.GetIncomingGraphSetId().empty()
		&& !controller.IsGraphSetTransitioning(),
		"Queued Graph Set transition did not atomically promote its target"))
		return false;
	if (!Expect(controller.SwitchGraphSet("missing")
		== VansGraphSetSwitchResult::UnknownGraphSet,
		"Graph Set runtime accepted an unknown stable ID"))
		return false;

	// Root Motion 生产与 Motion Matching 解耦：CCT 在脚本/物理提交阶段提前
	// 评估任意 Graph，常规动画阶段不得重复推进，也不得把增量解释为绝对位置。
	auto makeRootMotionClip = [](const std::string& name, float rootTravel)
	{
		VansAnimationClip clip;
		clip.clipName = name;
		clip.duration = 2.0f;
		clip.rootMotion.enabled = true;
		clip.rootMotion.boneName = "root";
		clip.rootMotion.extractTranslation = true;
		clip.rootMotion.extractRotation = true;
		clip.boneKeyframes.resize(3);
		for (int bone = 0; bone < 3; ++bone)
		{
			const float end = bone == 0 ? rootTravel : 0.0f;
			clip.boneKeyframes[bone] = {
				{ 0.0f, glm::vec3(0.0f),
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) },
				{ 2.0f, glm::vec3(end, 0.0f, 0.0f),
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) }
			};
		}
		return clip;
	};

	auto characterMotionController = std::make_unique<VansAnimationController>();
	characterMotionController->AddClip(
		"RootAttack", makeRootMotionClip("RootAttack", 100.0f));
	characterMotionController->AddClip(
		"Idle", makeRootMotionClip("Idle", 0.0f));
	VansAnimationLayerSetup motionBase;
	motionBase.definition.id = "layer-base";
	motionBase.definition.name = "Base";
	motionBase.definition.kind = VansAnimationLayerKind::Base;
	motionBase.definition.rootMotion = VansLayerRootMotionMode::Base;
	std::vector<VansAnimationLayerSetup> motionLayers;
	motionLayers.push_back(std::move(motionBase));
	std::vector<VansAnimationGraphSetSetup> motionGraphSets;
	motionGraphSets.push_back(makeGraphSet(
		"root-attack", "graph-root-attack", "RootAttack"));
	motionGraphSets.push_back(makeGraphSet("idle", "graph-idle", "Idle"));
	VansGraphSetTransitionPolicy rootTransition;
	rootTransition.duration = 0.1f;
	rootTransition.curve = VansGraphSetBlendCurve::Linear;
	rootTransition.phase = VansGraphSetPhasePolicy::MatchNormalizedTime;
	rootTransition.rootMotion = VansGraphSetRootMotionPolicy::IncomingOnly;
	if (!Expect(characterMotionController->SetAnimationGraphSets(
		std::move(motionLayers), std::move(motionGraphSets), "root-attack",
		rootTransition, {}, error), error.c_str()))
		return false;
	characterMotionController->EnableRootMotion(true);
	VansAnimationController* characterMotionControllerView = characterMotionController.get();
	VansAnimationNode characterMotionNode("CharacterMotionContract");
	characterMotionNode.SetSkeleton(skeleton);
	if (!Expect(characterMotionNode.SetController(std::move(characterMotionController)),
		"Character motion fixture could not bind its controller"))
		return false;
	characterMotionNode.Play(VansAnimationEvaluationPurpose::Gameplay);
	Vans::VansCharacterTrajectory stationaryTrajectory;
	characterMotionNode.PrepareCharacterMotionFrame(0.25f, stationaryTrajectory);
	if (!Expect(!characterMotionControllerView->IsMotionMatchingConfigured() &&
		characterMotionNode.HasRootMotionDelta() &&
		std::abs(characterMotionNode.GetRootMotionDelta().x - 12.5f) < 0.001f,
		"Non-Motion-Matching Graph did not publish Root Motion for CCT submission"))
		return false;
	const float preparedPlayTime = characterMotionNode.GetCurrentPlayTime();
	characterMotionNode.PrepareAnimationFrame({
		VansAnimationEvaluationPurpose::Gameplay, 0.25f });
	if (!ExpectNear(characterMotionNode.GetCurrentPlayTime(), preparedPlayTime, 0.0001f,
		"CCT-prepared animation frame advanced twice"))
		return false;

	glm::vec3 committedOwnerPosition(4.0f, 0.0f, -3.0f);
	committedOwnerPosition += characterMotionNode.GetRootMotionDelta();
	const glm::vec3 positionBeforeReturn = committedOwnerPosition;
	if (!Expect(characterMotionNode.SwitchGraphSet("idle") ==
		VansGraphSetSwitchResult::Started,
		"Root Motion fixture could not return to its idle Graph Set"))
		return false;
	characterMotionNode.PrepareCharacterMotionFrame(0.1f, stationaryTrajectory);
	committedOwnerPosition += characterMotionNode.GetRootMotionDelta();
	characterMotionNode.PrepareAnimationFrame({
		VansAnimationEvaluationPurpose::Gameplay, 0.1f });
	return Expect(glm::length(committedOwnerPosition - positionBeforeReturn) < 0.0001f,
		"Graph Set return reinterpreted Root Motion as an absolute owner Transform");
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
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        graph->AddLink(clipId, 0, outputId, 0);
        return graph;
    };
    auto slotGraph = std::make_unique<VansAnimGraph>();
    auto slotNode = std::make_unique<AnimGraphSlotNode>();
    slotNode->m_SlotId = "slot-upper";
    slotNode->m_EnableFallbackInput = false;
    const int slotNodeId = slotGraph->AddNode(std::move(slotNode));
    const int slotOutputId = slotGraph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
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

	VansAnimationLayerSetup base;
    base.definition.id = "layer-base";
    base.definition.name = "Base";
    base.definition.kind = VansAnimationLayerKind::Base;
    base.definition.rootMotion = VansLayerRootMotionMode::Base;
	auto baseGraph = makeBaseGraph();
	VansAnimationLayerSetup overlay;
    overlay.definition.id = "layer-upper";
    overlay.definition.name = "Upper";
    overlay.definition.kind = VansAnimationLayerKind::Overlay;
    overlay.definition.events = VansLayerEventMode::ActiveOnly;
    overlay.mask = mask;

    VansAnimationController controller;
    controller.AddClip("Base", makeClip("Base", 0.0f));
    controller.AddClip("Fire", makeClip("Fire", 20.0f));
    controller.AddClip("Reload", makeClip("Reload", 30.0f));
	std::vector<VansAnimationLayerSetup> layers;
    layers.push_back(std::move(base));
    layers.push_back(std::move(overlay));
    std::string error;
	std::vector<std::unique_ptr<VansAnimGraph>> graphs;
	graphs.push_back(std::move(baseGraph));
	graphs.push_back(std::move(slotGraph));
	if (!Expect(InstallTestGraphSet(controller, std::move(layers),
		{ "graph-base", "graph-slot" }, std::move(graphs), error), error.c_str()))
        return false;

    VansAnimationSlotDefinition slot;
    slot.id = "slot-upper";
    slot.name = "Upper";
    slot.layerId = "layer-upper";
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
        const int baseOutput = baseGraph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        baseGraph->AddLink(stateMachineId, 0, baseOutput, 0);

        auto slotGraph = std::make_unique<VansAnimGraph>();
        auto slotNode = std::make_unique<AnimGraphSlotNode>();
        slotNode->m_SlotId = "slot-upper";
        slotNode->m_EnableFallbackInput = false;
        const int slotNodeId = slotGraph->AddNode(std::move(slotNode));
        const int slotOutput = slotGraph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        slotGraph->AddLink(slotNodeId, 0, slotOutput, 0);

		VansAnimationLayerSetup base;
        base.definition.id = "layer-base";
        base.definition.name = "Base";
        base.definition.kind = VansAnimationLayerKind::Base;
		VansAnimationLayerSetup overlay;
        overlay.definition.id = "layer-upper";
        overlay.definition.name = "Upper";
        overlay.definition.kind = VansAnimationLayerKind::Overlay;
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
		std::vector<VansAnimationLayerSetup> layers;
        layers.push_back(std::move(base));
        layers.push_back(std::move(overlay));
        std::string error;
		std::vector<std::unique_ptr<VansAnimGraph>> graphs;
		graphs.push_back(std::move(baseGraph));
		graphs.push_back(std::move(slotGraph));
		if (!InstallTestGraphSet(*controller, std::move(layers),
			{ "graph-base", "graph-upper" }, std::move(graphs), error))
            return nullptr;
        VansAnimationSlotDefinition slot;
        slot.id = "slot-upper";
        slot.name = "Upper";
        slot.layerId = "layer-upper";
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
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        graph->AddLink(clipId, 0, outputId, 0);
        return graph;
    };

    VansBoneMaskAsset armMask;
    armMask.id = "mask-arm";
    armMask.name = "Arm";
    armMask.explicitWeights["arm"] = 1.0f;
	VansAnimationLayerSetup leader;
    leader.definition.id = "layer-leader";
    leader.definition.name = "Leader";
    leader.definition.kind = VansAnimationLayerKind::Base;
    leader.definition.rootMotion = VansLayerRootMotionMode::Base;
	auto leaderGraph = makeGraph("Leader");
	VansAnimationLayerSetup follower;
    follower.definition.id = "layer-follower";
    follower.definition.name = "Follower";
    follower.definition.kind = VansAnimationLayerKind::Overlay;
    follower.definition.sync = VansLayerSyncMode::MarkerSync;
    follower.definition.syncLeaderLayerId = "layer-leader";
	auto followerGraph = makeGraph("Follower");
    follower.mask = armMask;

    VansAnimationController controller;
    controller.AddClip("Leader", makeClip("Leader", 2.0f, 0.0f, 0.25f, 1.25f));
    controller.AddClip("Follower", makeClip("Follower", 4.0f, 40.0f, 1.0f, 3.0f));
	std::vector<VansAnimationLayerSetup> layers;
    layers.push_back(std::move(leader));
    layers.push_back(std::move(follower));
    std::string error;
	std::vector<std::unique_ptr<VansAnimGraph>> graphs;
	graphs.push_back(std::move(leaderGraph));
	graphs.push_back(std::move(followerGraph));
	if (!Expect(InstallTestGraphSet(controller, std::move(layers),
		{ "graph-leader", "graph-follower" }, std::move(graphs), error), error.c_str()))
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
	VansAnimationRigAsset rigAsset;
	rigAsset.name = "Target Post Process Test Rig";
	rigAsset.skeletonGuid = "test-skeleton";
	rigAsset.goals.push_back({ "aim", "arm" });
	VansRigChainDefinition aimChain;
	aimChain.id = "upperAim";
	aimChain.solver = VansRigSolverKind::Aim;
	aimChain.bones = { "spine", "arm" };
	aimChain.goal = "aim";
	aimChain.weights = { 0.0f, 1.0f };
	aimChain.forwardAxisLocal = { 0.0f, 0.0f, -1.0f };
	aimChain.upAxisLocal = { 0.0f, 1.0f, 0.0f };
	rigAsset.chains.push_back(std::move(aimChain));
	VansCompiledAnimationRig compiledRig;
	std::string error;
	if (!VansAnimationRigCompiler::Compile(rigAsset, skeleton, compiledRig, error))
	{
		std::cerr << "[ForestContractTests] Target Post Process Rig compile: " << error << '\n';
		return false;
	}
	auto installRig = [&](VansAnimationController& controller)
	{
		return controller.SetAnimationRig(compiledRig, {}, error);
	};
    auto makePostProcessGraph = []
    {
        auto graph = std::make_unique<VansAnimGraph>();
        const int inputId = graph->AddNode(
            VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::TargetPoseInput));
        auto aim = std::make_unique<AnimGraphAimConstraintNode>();
		aim->m_ChainId = "upperAim";
		aim->m_Target.goalId = "aim";
		aim->m_Target.source = VansGraphGoalSource::Fixed;
		aim->m_Target.fixedPositionModel = glm::vec3(1.0f, 0.0f, 0.0f);
		aim->m_Target.fixedPositionWeight = 1.0f;
		aim->m_TargetHalfLife = 0.0f;
		aim->m_Settings.maxAngularSpeedDegrees = 100000.0f;
        const int aimId = graph->AddNode(std::move(aim));
        const int outputId = graph->AddNode(
            VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        graph->AddLink(inputId, 0, aimId, 0);
        graph->AddLink(aimId, 0, outputId, 0);
        return graph;
    };
    auto aimedForward = [](const VansAnimationController& controller)
    {
        return glm::normalize(glm::vec3(controller.GetCachedGlobalTransform(2)
            * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    };

    VansAnimationController externalController;
	if (!installRig(externalController))
	{
		std::cerr << "[ForestContractTests] Target Post Process Rig install: " << error << '\n';
		return false;
	}
	if (!externalController.SetTargetPostProcessGraph(makePostProcessGraph(), error))
	{
		std::cerr << "[ForestContractTests] Target Post Process Graph install: " << error << '\n';
        return false;
	}
	VansAnimationRigAsset incompatibleRigAsset;
	incompatibleRigAsset.name = "Target Post Process Incompatible Rig";
	incompatibleRigAsset.skeletonGuid = "test-skeleton";
	VansCompiledAnimationRig incompatibleRig;
	if (!VansAnimationRigCompiler::Compile(
		incompatibleRigAsset, skeleton, incompatibleRig, error))
	{
		std::cerr << "[ForestContractTests] Incompatible Rig compile: " << error << '\n';
		return false;
	}
	if (!Expect(!externalController.ReplaceAnimationRig(std::move(incompatibleRig), error)
		&& externalController.GetAnimationRig()
		&& externalController.GetAnimationRig()->FindChain("upperAim") >= 0,
		"Rejected Animation Rig replacement did not preserve the last-good live Rig"))
		return false;
	VansCompiledAnimationRig replacementRig;
	if (!VansAnimationRigCompiler::Compile(
		rigAsset, skeleton, replacementRig, error))
	{
		std::cerr << "[ForestContractTests] Replacement Rig compile: " << error << '\n';
		return false;
	}
	if (!Expect(externalController.ReplaceAnimationRig(
		std::move(replacementRig), error),
		"Compatible Animation Rig replacement was rejected"))
		return false;
    std::vector<glm::mat4> externalModelPose(3, glm::mat4(1.0f));
    if (!Expect(externalController.SubmitExternalModelPose(
        externalModelPose, skeleton, 0.016f,
        VansExternalPoseEvaluationMode::TargetPostProcess),
		"Compatible Rig replacement left Target Post Process with an invalid Rig lifetime"))
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
	if (!installRig(directController))
	{
		std::cerr << "[ForestContractTests] Direct-pose Rig install: " << error << '\n';
		return false;
	}
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
        VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
    baseGraph->AddLink(clipId, 0, outputId, 0);

    VansAnimationController layeredController;
    layeredController.AddClip("Base", clip);
	if (!installRig(layeredController))
	{
		std::cerr << "[ForestContractTests] Layered-pose Rig install: " << error << '\n';
		return false;
	}
	if (!InstallTestBaseLayer(layeredController, std::move(baseGraph), error))
	{
		std::cerr << "[ForestContractTests] Layered-pose base Graph install: " << error << '\n';
		return false;
	}
	if (!layeredController.SetTargetPostProcessGraph(makePostProcessGraph(), error))
	{
		std::cerr << "[ForestContractTests] Layered Target Post Process Graph install: " << error << '\n';
        return false;
    }
    layeredController.Play();
    layeredController.Update(1.0f / 60.0f, skeleton);
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
            VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType::Output));
        graph->AddLink(stateMachineId, 0, outputId, 0);
        return graph;
    };

	VansAnimationLayerSetup leader;
    leader.definition.id = "layer-leader";
    leader.definition.name = "Leader";
    leader.definition.kind = VansAnimationLayerKind::Base;
	auto leaderGraph = makeStateGraph("BaseIdle", "BaseRun", true);

    VansBoneMaskAsset armMask;
    armMask.id = "mask-arm";
    armMask.name = "Arm";
    armMask.explicitWeights["arm"] = 1.0f;
	VansAnimationLayerSetup follower;
    follower.definition.id = "layer-follower";
    follower.definition.name = "Follower";
    follower.definition.kind = VansAnimationLayerKind::Overlay;
    follower.definition.sync = VansLayerSyncMode::SyncedGraph;
    follower.definition.syncLeaderLayerId = "layer-leader";
	auto followerGraph = makeStateGraph("UpperIdle", "UpperRun", false);
    follower.mask = armMask;

    VansAnimationController controller;
    controller.AddParameter("Go", AnimatorParamType::Bool);
    controller.AddClip("BaseIdle", makeClip("BaseIdle", 0.0f, 1.0f));
    controller.AddClip("BaseRun", makeClip("BaseRun", 0.0f, 2.0f));
    controller.AddClip("UpperIdle", makeClip("UpperIdle", 10.0f, 3.0f));
    controller.AddClip("UpperRun", makeClip("UpperRun", 30.0f, 4.0f));
	std::vector<VansAnimationLayerSetup> layers;
    layers.push_back(std::move(leader));
    layers.push_back(std::move(follower));
    std::string error;
	std::vector<std::unique_ptr<VansAnimGraph>> graphs;
	graphs.push_back(std::move(leaderGraph));
	graphs.push_back(std::move(followerGraph));
	if (!Expect(InstallTestGraphSet(controller, std::move(layers),
		{ "graph-leader", "graph-follower" }, std::move(graphs), error), error.c_str()))
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
    if (!Expect(NormalizeAudioBusName(" FootSteps ") == "footsteps",
        "Custom audio bus names should use one lowercase identity"))
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
    manager.SetBusLowpassHighFrequencyGain("Master", 0.8f);
    manager.SetBusLowpassHighFrequencyGain("Music", 0.5f);
    if (!ExpectNear(manager.GetEffectiveBusLowpassHighFrequencyGain("music"), 0.4f, 0.0001f,
        "Audio bus lowpass composition or name normalization changed") ||
        !ExpectNear(manager.GetEffectiveBusLowpassHighFrequencyGain("Master"), 0.8f, 0.0001f,
            "Master bus lowpass was applied twice"))
        return false;
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
        "device": {
            "hrtf": "disabled",
            "outputDevice": "Contract Device",
            "masterGain": 0.75,
            "sourceLimit": 64
        },
        "defaultSnapshot": "Gameplay",
        "buses": {
            "Master": { "gain": 0.9, "lowpassHighFrequencyGain": 1.0 },
            "Ambient": { "gain": 0.8, "lowpassHighFrequencyGain": 0.5 },
            "SFX": { "gain": 1.0, "lowpassHighFrequencyGain": 1.0 }
        },
        "snapshots": {
            "Gameplay": {
                "fadeSeconds": 0.0,
                "buses": [
                    { "bus": "Ambient", "gain": 0.4, "lowpassHighFrequencyGain": 0.3 }
                ]
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

    VansAudioMixConfig config;
    std::string error;
    if (!Expect(VansAudioMixConfigStorage::Load(mixPath, config, error), error.c_str()))
        return false;
    if (!Expect(config.displayName == "Contract Mix", "Audio mix display name did not read"))
        return false;
    if (!Expect(config.device.m_HrtfMode == VansAudioHrtfMode::Disabled &&
        config.device.m_OutputDevice == "Contract Device" &&
        config.device.m_SourceLimit == 64,
        "Audio device configuration did not read"))
        return false;
    if (!ExpectNear(config.device.m_MasterGain, 0.75f, 0.0001f,
        "Audio device master gain did not read"))
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
    if (!Expect(config->assetGuid == "DoorLoop", "Audio component source did not read"))
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

bool TestMediaDecodeSessionContract()
{
    namespace fs = std::filesystem;
    using namespace VansEngine;

    fs::path imagePath;
    for (fs::path cursor = fs::current_path(); !cursor.empty() && imagePath.empty();
         cursor = cursor.parent_path())
    {
        const fs::path candidates[] = {
            cursor / "EngineAssets" / "Textures" / "Default" / "defaultSkinMask.png",
            cursor / "ForestEngine" / "EngineAssets" / "Textures" / "Default" /
                "defaultSkinMask.png"
        };
        for (const fs::path& candidate : candidates)
        {
            if (fs::exists(candidate))
            {
                imagePath = candidate;
                break;
            }
        }
        if (cursor == cursor.parent_path())
            break;
    }
    if (!Expect(!imagePath.empty(), "Media decode contract source image is missing"))
        return false;

    VansMediaDecodeSession session;
    std::string error;
    if (!Expect(session.OpenVideo(imagePath.string(), error), error.c_str()))
        return false;
    if (!Expect(session.GetWidth() > 0 && session.GetHeight() > 0,
        "Media decode session did not expose video dimensions"))
        return false;

    std::vector<std::uint8_t> firstPixels;
    double firstTime = 0.0;
    if (!Expect(session.DecodeVideoFrame(firstPixels, firstTime,
        VansMediaEndPolicy::DrainDecoder, error) == VansMediaDecodeStatus::FrameReady,
        error.c_str()))
        return false;
    const std::size_t expectedSize = static_cast<std::size_t>(session.GetWidth()) *
        static_cast<std::size_t>(session.GetHeight()) * 4u;
    if (!Expect(firstPixels.size() == expectedSize,
        "Media decode session did not return one complete RGBA frame"))
        return false;

    if (!Expect(session.Reset(error), error.c_str()))
        return false;
    std::vector<std::uint8_t> resetPixels;
    double resetTime = 0.0;
    if (!Expect(session.DecodeVideoFrame(resetPixels, resetTime,
        VansMediaEndPolicy::DrainDecoder, error) == VansMediaDecodeStatus::FrameReady,
        error.c_str()))
        return false;
    return Expect(resetPixels == firstPixels && resetTime == firstTime,
        "Media decode session reset changed the first decoded frame");
}

bool TestAudioSourcePoolContract()
{
    using namespace VansEngine;

    VansAudioSystem& system = VansAudioSystem::GetInstance();
    if (system.IsInitialized())
        system.Shutdown();

    const VansAudioDeviceConfig originalConfig = system.GetDeviceConfig();
    VansAudioDeviceConfig testConfig = originalConfig;
    testConfig.m_MasterGain = 0.75f;
    testConfig.m_SourceLimit = 2;
    if (!Expect(system.Initialize(testConfig), "Audio source pool could not initialize OpenAL"))
    {
        system.SetSourceLimit(originalConfig.m_SourceLimit);
        system.SetMasterVolume(originalConfig.m_MasterGain);
        return false;
    }
    if (!Expect(system.GetDeviceConfig().m_SourceLimit == 2 &&
        std::abs(system.GetDeviceConfig().m_MasterGain - 0.75f) < 0.0001f,
        "Audio system did not apply device configuration"))
    {
        system.Shutdown();
        system.SetSourceLimit(originalConfig.m_SourceLimit);
        system.SetMasterVolume(originalConfig.m_MasterGain);
        return false;
    }

    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::uint32_t rejected = 0;
    const auto firstStatus = system.TryAcquireSource(first);
    const auto secondStatus = system.TryAcquireSource(second);
    const auto rejectedStatus = system.TryAcquireSource(rejected);
    bool passed = true;
    passed = Expect(firstStatus == VansAudioSourceAcquireStatus::Acquired && first != 0,
        "Audio source pool did not acquire its first source") && passed;
    passed = Expect(secondStatus == VansAudioSourceAcquireStatus::Acquired && second != 0,
        "Audio source pool did not acquire up to its limit") && passed;
    passed = Expect(rejectedStatus == VansAudioSourceAcquireStatus::LimitReached && rejected == 0,
        "Audio source pool did not report its hard limit") && passed;
    passed = Expect(system.GetActiveSourceLeaseCount() == 2 &&
        system.GetSourceLimitRejectionCount() == 1,
        "Audio source pool diagnostics did not track active and rejected leases") && passed;

    const std::uint32_t pooledSource = first;
    system.ReleaseSource(first);
    passed = Expect(first == 0 && system.GetActiveSourceLeaseCount() == 1 &&
        system.GetPooledSourceCount() == 1,
        "Audio source release did not move the source into the pool") && passed;

    const auto reusedStatus = system.TryAcquireSource(rejected);
    passed = Expect(reusedStatus == VansAudioSourceAcquireStatus::Acquired &&
        rejected == pooledSource && system.GetPooledSourceCount() == 0,
        "Audio source pool did not reuse a released source") && passed;

    system.ReleaseSource(second);
    system.ReleaseSource(rejected);
    system.SetSourceLimit(1);
    passed = Expect(system.GetActiveSourceLeaseCount() == 0 &&
        system.GetPooledSourceCount() == 1,
        "Audio source pool did not trim pooled sources after lowering its limit") && passed;

    std::uint32_t activeAtShutdown = 0;
    const auto shutdownAcquireStatus = system.TryAcquireSource(activeAtShutdown);
    passed = Expect(shutdownAcquireStatus == VansAudioSourceAcquireStatus::Acquired &&
        activeAtShutdown != 0 && system.GetActiveSourceLeaseCount() == 1,
        "Audio source pool could not establish the active shutdown case") && passed;
    system.Shutdown();
    passed = Expect(!system.IsInitialized() &&
        system.GetActiveSourceLeaseCount() == 0 && system.GetPooledSourceCount() == 0,
        "Audio source pool did not reclaim active and pooled sources during shutdown") && passed;
    system.SetSourceLimit(originalConfig.m_SourceLimit);
    system.SetMasterVolume(originalConfig.m_MasterGain);
    return passed;
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
    if (!Expect(Vans::VansAssetDatabase::Classify("Legacy.vbusnapshot") == Vans::VansAssetType::Unknown,
        "Removed audio bus snapshot extension must not remain classified"))
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
                { "bus", Value::String("SFX") },
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
                { "triggerBus", Value::String("voice") },
                { "targetBus", Value::String("music") },
                { "targetGain", Value::Float(-1.0) },
                { "attackSeconds", Value::Float(0.08) },
                { "releaseSeconds", Value::Float(99.0) },
                { "enabled", Value::Bool(true) }
            }),
            Value::Object({
                { "triggerBus", Value::String("SFX") },
                { "targetBus", Value::String("Ambient") },
                { "targetGain", Value::Float(0.6) }
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
        "Audio ducking rule target gain did not read"))
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
	Vans::VansPackagedAssetIndexRecord indexedAsset;
	indexedAsset.guid = "2c86c128-f3f0-4dbd-9e4e-0f0f0a61c9d1";
	indexedAsset.type = "model";
	indexedAsset.artifactPath = "Library/Artifacts/Resources/2c86c128/model.vmesh";
	indexedAsset.metaPath = "Library/Artifacts/Metadata/2c86c128-f3f0-4dbd-9e4e-0f0f0a61c9d1.meta";
	indexedAsset.artifactFormat = "imported";
	indexedAsset.sourceHash = 7;
	indexedAsset.metaHash = 11;
	plan.assetIndex.push_back(indexedAsset);
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
	if (!Expect(loaded.assetIndex.size() == 1 &&
		loaded.assetIndex.front().metaPath ==
			(temporary.path / indexedAsset.metaPath).lexically_normal().generic_string(),
		"Packaged asset metadata path did not round-trip into the package root"))
		return false;

    const Vans::VansSceneAudioResourceRequest& roundTrip = loaded.resourcePlan.audios.front();
    if (!Expect(roundTrip.attenuationMode == "inverse", "Packaged audio attenuation mode did not round-trip"))
        return false;
    if (!ExpectNear(roundTrip.reverbSend, 0.35f, 0.0001f,
        "Packaged audio reverb send did not round-trip"))
        return false;
    return Expect(roundTrip.bus == "Music", "Packaged audio bus did not round-trip");
}

bool TestMediaComponentGuidProjection()
{
	using Value = Vans::VansSerializedValue;
	const std::string audioGuid = "0ad3dc29-20bb-4b43-87a5-8e63c6b25dda";
	const std::string videoGuid = "a1d4b131-8512-4f23-8bdb-445f4b4dc825";
	Value sceneRoot = Value::Object({
		{ "schemaVersion", Value::Int(Vans::VansSceneSchemaVersion) },
		{ "sceneGuid", Value::String("d89a5167-195a-4e37-93b3-a08f9cbcaf15") },
		{ "settings", Value::Object({
			{ "environment", BuildValidEnvironmentSettingsForTest() }
		}) },
		{ "entities", Value::Array({
			Value::Object({
				{ "id", Value::String("1ed05d94-4d0f-4dc3-933c-b124284f17a0") },
				{ "name", Value::String("Media") },
				{ "parent", Value::Null() },
				{ "components", Value::Array({
					Value::Object({
						{ "id", Value::String("d7af1600-5ec1-49ca-b0ca-b8b34c53db74") },
						{ "type", Value::String("Transform") },
						{ "version", Value::Int(1) },
						{ "enabled", Value::Bool(true) },
						{ "data", Value::Object({
							{ "position", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0) }) },
							{ "rotation", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0), Value::Float(1.0) }) },
							{ "scale", Value::Array({ Value::Float(1.0), Value::Float(1.0), Value::Float(1.0) }) }
						}) }
					}),
					Value::Object({
						{ "id", Value::String("69ceab05-4636-41ec-a6bb-feea180f3f83") },
						{ "type", Value::String("Audio") },
						{ "version", Value::Int(1) },
						{ "enabled", Value::Bool(true) },
						{ "data", Value::Object({
							{ "source", Value::Object({
								{ "guid", Value::String(audioGuid) }
							}) }
						}) }
					}),
					Value::Object({
						{ "id", Value::String("845064a5-03a1-4769-ae4a-e10e7c15bc27") },
						{ "type", Value::String("Video") },
						{ "version", Value::Int(1) },
						{ "enabled", Value::Bool(true) },
						{ "data", Value::Object({
							{ "source", Value::Object({
								{ "guid", Value::String(videoGuid) }
							}) }
						}) }
					})
				}) }
			})
		}) }
	});

	Vans::VansSceneContentBuildPlan plan;
	std::string error;
	if (!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
		sceneRoot, {}, plan, error))
	{
		std::cerr << "[MediaGuidProjection] " << error << '\n';
		return false;
	}
	if (!Expect(plan.objects.objects.size() == 1,
		"Media component GUID projection changed the scene object count"))
		return false;
	const Vans::VansSceneCameraMediaComponentConfig& media =
		plan.objects.objects.front().cameraMediaComponents;
	if (!Expect(media.audio && media.audio->assetGuid == audioGuid &&
		media.video && media.video->assetGuid == videoGuid,
		"Audio/video asset references were converted from GUIDs to runtime aliases"))
		return false;

	Value* entities = Vans::FindObjectField(sceneRoot, "entities");
	Value invalidMediaEntity = entities && entities->kind == Value::Kind::Array &&
		!entities->arrayItems.empty() ? entities->arrayItems.front() : Value::Null();
	Value* components = Vans::FindObjectField(invalidMediaEntity, "components");
	Value* audioData = components && components->kind == Value::Kind::Array &&
		components->arrayItems.size() > 1
		? Vans::FindObjectField(components->arrayItems[1], "data") : nullptr;
	if (!Expect(audioData != nullptr,
		"Media GUID strict-admission fixture is missing Audio data"))
		return false;
	Vans::SetSerializedObjectField(*audioData, "source", Value::String(audioGuid));
	plan = {};
	error.clear();
	return Expect(!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
		Value::Array({ invalidMediaEntity }), {}, plan, error) &&
		error.find("Audio.source") != std::string::npos &&
		plan.objects.objects.empty(),
		"Audio bare-string GUID bypassed exact Scene asset-reference admission");
}

bool TestSceneProjectionStrictAdmission()
{
	using Value = Vans::VansSerializedValue;
	const Value disabledSpecial = Value::Object({
		{ "id", Value::String("51b1e5dc-7ce2-461a-b50b-61fb2bf34687") },
		{ "name", Value::String("Disabled special render node") },
		{ "parent", Value::Null() },
		{ "components", Value::Array({
			Value::Object({
				{ "id", Value::String("399ba764-bb1c-4321-8a3b-e3a268ce23fe") },
				{ "type", Value::String("Transform") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(true) },
				{ "data", Value::Object({}) }
			}),
			Value::Object({
				{ "id", Value::String("bf25c1ab-82c5-42ad-a176-3eec38d17d77") },
				{ "type", Value::String("ModelRenderer") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(false) },
				{ "data", Value::Object({
					{ "model", Value::Object({
						{ "guid", Value::String("52d8afaa-822c-4986-b623-fcc4e8c37036") }
					}) },
					{ "renderRole", Value::String("Environment") }
				}) }
			})
		}) }
	});
	Vans::VansSceneContentBuildPlan plan;
	std::string error;
	if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
		Value::Array({ disabledSpecial }), {}, plan, error) &&
		plan.renderNodes.empty() && plan.objects.objects.empty(),
		"Disabled special render node entered the runtime build plan"))
		return false;

	const std::string parentEntityGuid = "2e956def-ed8e-4d29-a7ee-ea2921cbb6ea";
	const std::string childEntityGuid = "2aec441f-c13d-474a-a22c-0cb22fcf5ff0";
	const Value parentEntity = Value::Object({
		{ "id", Value::String(parentEntityGuid) },
		{ "name", Value::String("Submesh parent") },
		{ "parent", Value::Null() },
		{ "components", Value::Array({
			Value::Object({
				{ "id", Value::String("a715ad11-1498-47f5-8dbb-a3e20d98e1fe") },
				{ "type", Value::String("Transform") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(true) },
				{ "data", Value::Object({}) }
			})
		}) }
	});
	const Value submeshChild = Value::Object({
		{ "id", Value::String(childEntityGuid) },
		{ "name", Value::String("Submesh child") },
		{ "parent", Vans::WriteEntityParentReference(parentEntityGuid) },
		{ "components", Value::Array({
			Value::Object({
				{ "id", Value::String("58381062-a222-4458-83ff-4939c67bf176") },
				{ "type", Value::String("Transform") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(true) },
				{ "data", Value::Object({}) }
			}),
			Value::Object({
				{ "id", Value::String("45df3d80-cf58-411b-a0d7-da9f2c7a42d7") },
				{ "type", Value::String("ModelRenderer") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(true) },
				{ "data", Value::Object({
					{ "model", Value::Object({
						{ "guid", Value::String("765a8d93-bc3c-417f-a17c-954126b74f57") }
					}) },
					{ "submesh", Value::Object({
						{ "index", Value::Int(3) },
						{ "sourceNode", Value::String("ImportedMeshNode") },
						{ "sourceMaterial", Value::String("ImportedMaterial") },
						{ "slotName", Value::String("body") }
					}) }
				}) }
			})
		}) }
	});
	plan = {};
	error.clear();
	if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
		Value::Array({ parentEntity, submeshChild }), {}, plan, error),
		"Submesh source metadata fixture did not project"))
		return false;
	const auto childObject = std::find_if(plan.objects.objects.begin(),
		plan.objects.objects.end(), [&](const Vans::VansSceneObjectBuildConfig& object)
		{
			return object.entityGuid == childEntityGuid;
		});
	if (!Expect(plan.renderNodes.empty() && childObject != plan.objects.objects.end()
		&& childObject->parent && childObject->parent->IsEntity()
		&& childObject->parent->entityGuid.ToString() == parentEntityGuid
		&& childObject->render && childObject->render->submesh == 3u
		&& childObject->render->submeshSlotName == "body",
		"ModelRenderer sourceNode metadata was conflated with transform parenting"))
		return false;

	const Value invalidTimeline = Value::Object({
		{ "id", Value::String("60cd2a74-78c6-4fac-874a-f30550bac7cd") },
		{ "name", Value::String("Invalid timeline enum") },
		{ "parent", Value::Null() },
		{ "components", Value::Array({
			Value::Object({
				{ "id", Value::String("d2d952d3-5f86-4556-a3c3-6095e3b2a29a") },
				{ "type", Value::String("Transform") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(true) },
				{ "data", Value::Object({}) }
			}),
			Value::Object({
				{ "id", Value::String("a07d9e70-c3ab-4870-9951-8dc2a2e5de23") },
				{ "type", Value::String("Timeline") },
				{ "version", Value::Int(1) },
				{ "enabled", Value::Bool(true) },
				{ "data", Value::Object({
					{ "timeline", Value::Object({
						{ "guid", Value::String("12df962c-4ab3-47f1-af78-1e4e6d77afc6") }
					}) },
					{ "playOn", Value::String("Awake ") },
					{ "bindingRootMode", Value::String("OwnerRelative") },
					{ "loopMode", Value::String("None") }
				}) }
			})
		}) }
	});
	auto RejectsInvalidTimelineEnum = [&](const Value& entity,
		const char* expectedError, const char* message)
	{
		plan = {};
		error.clear();
		return Expect(!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
			Value::Array({ entity }), {}, plan, error) &&
			error.find(expectedError) != std::string::npos &&
			plan.renderNodes.empty() && plan.objects.objects.empty(), message);
	};
	if (!RejectsInvalidTimelineEnum(invalidTimeline, "Timeline.playOn",
		"Invalid Timeline playOn silently projected with a default value"))
		return false;

	Value invalidBindingRoot = invalidTimeline;
	Value* invalidBindingComponents = Vans::FindObjectField(invalidBindingRoot, "components");
	Value* invalidBindingData = invalidBindingComponents &&
		invalidBindingComponents->kind == Value::Kind::Array &&
		invalidBindingComponents->arrayItems.size() > 1
		? Vans::FindObjectField(invalidBindingComponents->arrayItems[1], "data") : nullptr;
	if (!Expect(invalidBindingData != nullptr,
		"Timeline strict-admission fixture is missing component data"))
		return false;
	Vans::SetSerializedObjectField(*invalidBindingData, "playOn", Value::String("Manual"));
	Vans::SetSerializedObjectField(*invalidBindingData, "bindingRootMode", Value::String("owner"));
	if (!RejectsInvalidTimelineEnum(invalidBindingRoot, "Timeline.bindingRootMode",
		"Invalid Timeline bindingRootMode silently projected with a default value"))
		return false;

	Value invalidLoopMode = invalidBindingRoot;
	Value* invalidLoopComponents = Vans::FindObjectField(invalidLoopMode, "components");
	Value* invalidLoopData = invalidLoopComponents &&
		invalidLoopComponents->kind == Value::Kind::Array &&
		invalidLoopComponents->arrayItems.size() > 1
		? Vans::FindObjectField(invalidLoopComponents->arrayItems[1], "data") : nullptr;
	if (!Expect(invalidLoopData != nullptr,
		"Timeline strict-admission loop fixture is missing component data"))
		return false;
	Vans::SetSerializedObjectField(*invalidLoopData,
		"bindingRootMode", Value::String("OwnerRelative"));
	Vans::SetSerializedObjectField(*invalidLoopData, "loopMode", Value::String("loop"));
	if (!RejectsInvalidTimelineEnum(invalidLoopMode, "Timeline.loopMode",
		"Invalid Timeline loopMode silently projected with a default value"))
		return false;

	Value invalidTimelineReference = invalidLoopMode;
	Value* invalidReferenceComponents = Vans::FindObjectField(
		invalidTimelineReference, "components");
	Value* invalidReferenceData = invalidReferenceComponents &&
		invalidReferenceComponents->kind == Value::Kind::Array &&
		invalidReferenceComponents->arrayItems.size() > 1
		? Vans::FindObjectField(invalidReferenceComponents->arrayItems[1], "data") : nullptr;
	if (!Expect(invalidReferenceData != nullptr,
		"Timeline reference strict-admission fixture is missing component data"))
		return false;
	Vans::SetSerializedObjectField(*invalidReferenceData, "loopMode", Value::String("None"));
	Vans::SetSerializedObjectField(*invalidReferenceData, "timeline",
		Value::String("12df962c-4ab3-47f1-af78-1e4e6d77afc6"));
	if (!RejectsInvalidTimelineEnum(invalidTimelineReference, "Timeline.timeline",
		"Timeline bare-string GUID bypassed exact Scene asset-reference admission"))
		return false;

	Value unresolvedTimelineReference = invalidTimelineReference;
	Value* unresolvedComponents = Vans::FindObjectField(
		unresolvedTimelineReference, "components");
	Value* unresolvedData = unresolvedComponents &&
		unresolvedComponents->kind == Value::Kind::Array &&
		unresolvedComponents->arrayItems.size() > 1
		? Vans::FindObjectField(unresolvedComponents->arrayItems[1], "data") : nullptr;
	if (!Expect(unresolvedData != nullptr,
		"Timeline resolution strict-admission fixture is missing component data"))
		return false;
	Vans::SetSerializedObjectField(*unresolvedData, "timeline", Value::Object({
		{ "guid", Value::String("12df962c-4ab3-47f1-af78-1e4e6d77afc6") }
	}));
	if (!RejectsInvalidTimelineEnum(unresolvedTimelineReference, "not indexed",
		"Unindexed Timeline GUID was published as its raw path token"))
		return false;

	Value obsoleteOverrideType = invalidLoopMode;
	Value* obsoleteComponents = Vans::FindObjectField(obsoleteOverrideType, "components");
	Value* obsoleteData = obsoleteComponents && obsoleteComponents->kind == Value::Kind::Array &&
		obsoleteComponents->arrayItems.size() > 1
		? Vans::FindObjectField(obsoleteComponents->arrayItems[1], "data") : nullptr;
	if (!Expect(obsoleteData != nullptr,
		"Timeline integer component-type fixture is missing component data"))
		return false;
	Vans::SetSerializedObjectField(*obsoleteData, "loopMode", Value::String("None"));
	Vans::SetSerializedObjectField(*obsoleteData, "bindingOverrides", Value::Array({ Value::Object({
		{ "bindingId", Value::String("camera") },
		{ "targetComponent", Value::String("camera-component") },
		{ "targetComponentTypeId", Value::Int(Vans::VansRuntimeComponentType_Camera) }
	}) }));
	if (!RejectsInvalidTimelineEnum(obsoleteOverrideType, "obsolete integer targetComponentTypeId",
		"Timeline component override accepted an integer type ID from disk"))
		return false;

	Value unknownOverrideType = obsoleteOverrideType;
	Value* unknownComponents = Vans::FindObjectField(unknownOverrideType, "components");
	Value* unknownData = unknownComponents && unknownComponents->kind == Value::Kind::Array &&
		unknownComponents->arrayItems.size() > 1
		? Vans::FindObjectField(unknownComponents->arrayItems[1], "data") : nullptr;
	if (!Expect(unknownData != nullptr,
		"Timeline named component-type fixture is missing component data"))
		return false;
	Vans::SetSerializedObjectField(*unknownData, "bindingOverrides", Value::Array({ Value::Object({
		{ "bindingId", Value::String("camera") },
		{ "targetComponent", Value::String("camera-component") },
		{ "targetComponentType", Value::String("missing_component_type") }
	}) }));
	return RejectsInvalidTimelineEnum(unknownOverrideType, "unknown targetComponentType",
		"Timeline component override silently accepted an unknown stable type name");
}

bool TestAudioReverbZoneRuntimeProjection()
{
    using Value = Vans::VansSerializedValue;
    Value sceneRoot = Value::Object({
        { "schemaVersion", Value::Int(Vans::VansSceneSchemaVersion) },
        { "sceneGuid", Value::String("34055de6-8c36-4bd0-8d27-1391550c213a") },
        { "name", Value::String("Audio Reverb Projection Contract") },
        { "settings", Value::Object({
            { "environment", BuildValidEnvironmentSettingsForTest() }
        }) },
        { "entities", Value::Array({
            Value::Object({
                { "id", Value::String("e4e33afc-a470-4cd8-8a87-3d537a11d1b2") },
                { "name", Value::String("Reverb Zone") },
                { "parent", Value::Null() },
                { "components", Value::Array({
                    Value::Object({
                        { "id", Value::String("27e6a32a-4461-4e48-8f31-cacdb14a1554") },
                        { "type", Value::String("Transform") },
                        { "version", Value::Int(1) },
                        { "enabled", Value::Bool(true) },
                        { "data", Value::Object({
                            { "position", Value::Array({ Value::Float(1.0), Value::Float(2.0), Value::Float(3.0) }) },
                            { "rotation", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0), Value::Float(1.0) }) },
                            { "scale", Value::Array({ Value::Float(1.0), Value::Float(1.0), Value::Float(1.0) }) }
                        }) }
                    }),
                    Value::Object({
                        { "id", Value::String("ae30bc4d-7a2d-4946-8402-3cb07f021a2f") },
                        { "type", Value::String("AudioReverbZone") },
                        { "version", Value::Int(1) },
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
                { "id", Value::String("df20ac20-1962-4753-85b3-daa0b8371796") },
                { "name", Value::String("Audio Volume") },
                { "parent", Value::Null() },
                { "components", Value::Array({
                    Value::Object({
                        { "id", Value::String("5d7ffef6-42f4-4f1d-ac80-74a9ca384b75") },
                        { "type", Value::String("Transform") },
                        { "version", Value::Int(1) },
                        { "enabled", Value::Bool(true) },
                        { "data", Value::Object({
                            { "position", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0) }) },
                            { "rotation", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0), Value::Float(1.0) }) },
                            { "scale", Value::Array({ Value::Float(1.0), Value::Float(1.0), Value::Float(1.0) }) }
                        }) }
                    }),
                    Value::Object({
                        { "id", Value::String("df9c92b9-6c2a-4a98-a485-d8b3d0744563") },
                        { "type", Value::String("AudioVolume") },
                        { "version", Value::Int(1) },
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
    const bool projected = Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
        sceneRoot,
        "",
        plan,
        error);
    if (!Expect(projected, error.c_str()))
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
    if (!ExpectNear(volumeObject.audioReverbZone->presetParameters.gainHF, 0.2f, 0.0001f,
        "AudioVolume custom reverb gainHF did not project"))
        return false;

    Value* entities = Vans::FindObjectField(sceneRoot, "entities");
    Value invalidZoneEntity = entities && entities->kind == Value::Kind::Array &&
        !entities->arrayItems.empty() ? entities->arrayItems.front() : Value::Null();
    Value* components = Vans::FindObjectField(invalidZoneEntity, "components");
    Value* zoneData = components && components->kind == Value::Kind::Array &&
        components->arrayItems.size() > 1
        ? Vans::FindObjectField(components->arrayItems[1], "data") : nullptr;
    if (!Expect(zoneData != nullptr,
        "Reverb preset GUID strict-admission fixture is missing component data"))
        return false;
    Vans::SetSerializedObjectField(*zoneData, "presetAsset",
        Value::String("0a0d81aa-63cb-4186-9586-da9d03e7c980"));
    plan = {};
    error.clear();
    return Expect(!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
        Value::Array({ invalidZoneEntity }), {}, plan, error) &&
        error.find("AudioReverbZone.presetAsset") != std::string::npos &&
        plan.objects.objects.empty(),
        "Audio Reverb bare-string preset GUID bypassed exact Scene asset-reference admission");
}

bool TestExposureParameterContract()
{
	if (!Expect(sizeof(VansGraphics::VansPostProcessParamsGPU) == 112,
		"Post-process UBO size changed unexpectedly"))
		return false;
	if (!Expect(offsetof(VansGraphics::VansPostProcessParamsGPU, m_BloomIntensity) == 16,
		"Post-process bloom params are not vec4-aligned after exposure"))
		return false;
	if (!Expect(offsetof(VansGraphics::VansPostProcessParamsGPU, m_EnableDOF) == 96,
		"Post-process DOF enable flag is not aligned with the shader UBO"))
		return false;
	if (!Expect(offsetof(VansGraphics::VansPostProcessParamsGPU, m_EnableAutoExposure) == 100,
		"Post-process auto exposure flag is not aligned with the shader UBO"))
		return false;
	if (!Expect(sizeof(VansGraphics::VansBloomShapeParamsGPU) == 32,
		"Bloom shape UBO size changed unexpectedly"))
		return false;

	const VansGraphics::VansPostProcessProfile defaultProfile;
	if (!Expect(defaultProfile.ToExposureAdaptParams(0.016f).m_EnableAutoExposure == 0,
		"Auto exposure should default to disabled"))
		return false;
	if (!Expect(defaultProfile.m_DOFBlurTransmissionBackground,
		"DOF should default to a transparent-safe transmission background"))
		return false;
	const VansGraphics::VansPostProcessParamsGPU defaultPostProcess = defaultProfile.ToGPUParams();
	if (!Expect(defaultPostProcess.m_EnableAutoExposure == 0,
		"Auto exposure should default to disabled in final post-process params"))
		return false;

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
	if (!Expect(params.m_ExposureCompensation == profile.m_ExposureCompensation,
		"FSR exposure compensation was not uploaded"))
		return false;
	if (!Expect(params.m_EnableAutoExposure == 1,
		"Exposure enable state was not uploaded"))
		return false;
	if (!Expect(profile.ToGPUParams().m_EnableAutoExposure == 1,
		"Auto exposure enable state was not uploaded to final post-process params"))
		return false;

	profile.m_EnableBloom = true;
	profile.m_BloomThreshold = 2.0f;
	profile.m_BloomKnee = 0.35f;
	profile.m_BloomScatter = 0.65f;
	profile.m_BloomIntensity = 0.9f;
	profile.m_BloomClamp = 32.0f;
	profile.m_BloomTintR = 1.2f;
	profile.m_BloomTintG = 0.8f;
	profile.m_BloomTintB = 0.6f;
	const VansGraphics::VansBloomParamsGPU bloom = profile.ToBloomParams();
	if (!ExpectNear(bloom.m_Threshold, 2.0f, 0.0001f,
		"Bloom threshold was not uploaded"))
		return false;
	if (!ExpectNear(bloom.m_Knee, 0.35f, 0.0001f,
		"Bloom knee was not uploaded"))
		return false;
	if (!ExpectNear(bloom.m_Scatter, 0.65f, 0.0001f,
		"Bloom scatter was not uploaded in shader order"))
		return false;
	if (!ExpectNear(bloom.m_Clamp, 32.0f, 0.0001f,
		"Bloom clamp was not uploaded"))
		return false;
	if (!ExpectNear(bloom.m_TintR, 1.2f, 0.0001f,
		"Bloom tint R was not uploaded"))
		return false;
	if (!ExpectNear(bloom.m_TintG, 0.8f, 0.0001f,
		"Bloom tint G was not uploaded"))
		return false;
	if (!ExpectNear(bloom.m_TintB, 0.6f, 0.0001f,
		"Bloom tint B was not uploaded"))
		return false;

	profile.m_BloomShapeMode = 2;
	profile.m_BloomShapeIntensity = 0.85f;
	profile.m_BloomShapeBlend = 0.7f;
	profile.m_BloomShapeAngleDeg = 90.0f;
	profile.m_BloomAnamorphicStretch = 6.0f;
	profile.m_BloomStreakCount = 6;
	profile.m_BloomStreakLength = 42.0f;
	profile.m_BloomStreakAttenuation = 0.8f;
	const VansGraphics::VansBloomShapeParamsGPU shape = profile.ToBloomShapeParams();
	if (!Expect(shape.m_Mode == 2 && shape.m_StreakCount == 6,
		"Bloom shape mode or arm count was not uploaded"))
		return false;
	if (!ExpectNear(shape.m_ShapeIntensity, 0.85f, 0.0001f,
		"Bloom shape intensity was not uploaded"))
		return false;
	if (!ExpectNear(shape.m_ShapeBlend, 0.7f, 0.0001f,
		"Bloom shape blend was not uploaded"))
		return false;
	if (!ExpectNear(shape.m_ShapeAngleRadians, 1.5707964f, 0.0001f,
		"Bloom shape angle was not converted to radians"))
		return false;
	if (!ExpectNear(shape.m_AnamorphicStretch, 6.0f, 0.0001f,
		"Bloom anamorphic stretch was not uploaded"))
		return false;
	if (!ExpectNear(shape.m_StreakLength, 42.0f, 0.0001f,
		"Bloom streak length was not uploaded"))
		return false;
	if (!ExpectNear(shape.m_StreakAttenuation, 0.8f, 0.0001f,
		"Bloom streak attenuation was not uploaded"))
		return false;

	profile.m_EnableDOF = true;
	profile.m_FocusDistance = 7.5f;
	profile.m_FocalLengthMm = 85.0f;
	profile.m_FStop = 1.4f;
	profile.m_SensorHeightMm = 24.0f;
	profile.m_MaxCoC = 18.0f;
	const VansGraphics::VansDepthOfFieldParamsGPU dof =
		profile.ToDepthOfFieldParams(1920, 1080);
	if (!Expect(dof.m_EnableDOF == 1,
		"DOF enable state was not uploaded"))
		return false;
	if (!ExpectNear(dof.m_FocusDistance, 7.5f, 0.0001f,
		"DOF focus distance was not uploaded"))
		return false;
	if (!ExpectNear(dof.m_FocalLengthMm, 85.0f, 0.0001f,
		"DOF focal length was not uploaded"))
		return false;
	if (!ExpectNear(dof.m_FStop, 1.4f, 0.0001f,
		"DOF f-stop was not uploaded"))
		return false;
	if (!ExpectNear(dof.m_SensorHeightMm, 24.0f, 0.0001f,
		"DOF sensor height was not uploaded"))
		return false;
	if (!ExpectNear(dof.m_MaxCoC, 18.0f, 0.0001f,
		"DOF max CoC was not uploaded"))
		return false;
	if (!ExpectNear(dof.m_InvRenderWidth, 1.0f / 1920.0f, 0.000001f,
		"DOF inverse render width was not uploaded"))
		return false;
	return ExpectNear(dof.m_InvRenderHeight, 1.0f / 1080.0f, 0.000001f,
		"DOF inverse render height was not uploaded");
}

Vans::VansSerializedValue BuildValidEnvironmentSettingsForTest()
{
	using Value = Vans::VansSerializedValue;
	return Value::Object({
        { "skyLighting", Value::Object({ { "intensity", Value::Float(1.0) } }) },
		{ "planet", Value::Object({
			{ "centerWorldMeters", Value::Array({ Value::Float(0.0), Value::Float(-6340200.0), Value::Float(0.0) }) },
			{ "bottomRadiusMeters", Value::Float(6340000.0) },
			{ "atmosphereHeightMeters", Value::Float(80000.0) }
		}) },
		{ "physicalAtmosphere", Value::Object({
			{ "enabled", Value::Bool(true) },
			{ "groundAlbedo", Value::Array({ Value::Float(0.4), Value::Float(0.4), Value::Float(0.4) }) },
			{ "rayleigh", Value::Object({
				{ "scatteringPerMeterAtGround", Value::Array({ Value::Float(5.802e-6), Value::Float(13.558e-6), Value::Float(33.1e-6) }) },
				{ "densityScaleHeightMeters", Value::Float(8500.0) }
			}) },
			{ "mie", Value::Object({
				{ "scatteringPerMeterAtGround", Value::Array({ Value::Float(3.996e-6), Value::Float(3.996e-6), Value::Float(3.996e-6) }) },
				{ "absorptionPerMeterAtGround", Value::Array({ Value::Float(4.4e-6), Value::Float(4.4e-6), Value::Float(4.4e-6) }) },
				{ "densityScaleHeightMeters", Value::Float(1200.0) },
				{ "anisotropy", Value::Float(0.78) }
			}) },
			{ "ozone", Value::Object({
				{ "absorptionPerMeter", Value::Array({ Value::Float(0.650e-6), Value::Float(1.881e-6), Value::Float(0.085e-6) }) },
				{ "centerAltitudeMeters", Value::Float(25000.0) },
				{ "halfWidthMeters", Value::Float(15000.0) }
			}) },
			{ "aerialPerspective", Value::Object({
				{ "distanceScale", Value::Float(1.0) }
			}) },
			{ "mainLightVolumetricScatteringScale", Value::Float(1.0) },
			{ "celestialBodies", Value::Array({ Value::Object({
				{ "name", Value::String("Sun") },
				{ "lightEntityId", Value::String("test-sun") },
				{ "disk", Value::Object({
					{ "enabled", Value::Bool(true) },
					{ "angularRadiusRadians", Value::Float(0.018) },
					{ "featherRadians", Value::Float(0.0015) },
					{ "radianceScale", Value::Float(1.0) },
					{ "occlusionStrength", Value::Float(8.0) }
				}) }
			}) }) }
		}) },
		{ "heightFog", Value::Object({
			{ "enabled", Value::Bool(true) },
			{ "groundHeightWorldMeters", Value::Float(100.0) },
			{ "visibilityAtGroundMeters", Value::Float(500.0) },
			{ "densityFalloffHeightMeters", Value::Float(120.0) },
			{ "startDistanceMeters", Value::Float(0.0) },
			{ "nearFadeDistanceMeters", Value::Float(20.0) },
			{ "maximumDistanceMeters", Value::Float(1500.0) },
			{ "farFadeDistanceMeters", Value::Float(300.0) },
			{ "singleScatteringAlbedo", Value::Array({ Value::Float(0.95), Value::Float(0.97), Value::Float(1.0) }) },
			{ "anisotropy", Value::Float(0.2) },
			{ "emissivePerMeter", Value::Array({ Value::Float(0.0), Value::Float(0.0), Value::Float(0.0) }) },
			{ "skyLightingScale", Value::Float(1.0) },
			{ "mainLightVolumetricScale", Value::Float(1.0) },
			{ "receiveCloudShadows", Value::Bool(true) }
		}) },
		{ "volumetricClouds", Value::Object({
			{ "enabled", Value::Bool(true) },
			{ "cloudMinHeight", Value::Float(1070.0) },
			{ "cloudMaxHeight", Value::Float(7410.0) },
			{ "density", Value::Float(0.025) },
			{ "coverage", Value::Float(0.35) },
			{ "sunBrightness", Value::Float(0.38) },
			{ "mainTileMeters", Value::Float(43300.0) },
			{ "detailTileMeters", Value::Float(2200.0) },
			{ "mainHeightScale", Value::Float(0.26) },
			{ "detailHeightScale", Value::Float(3.07) },
			{ "thresholdLowCoverage", Value::Float(0.115) },
			{ "thresholdHighCoverage", Value::Float(0.72) },
			{ "densityRemapLow", Value::Float(0.425) },
			{ "densityRemapHigh", Value::Float(0.915) },
			{ "mainErosionStrength", Value::Float(1.16) },
			{ "detailErosionStrength", Value::Float(1.34) },
			{ "edgeErosionStrength", Value::Float(0.5) },
			{ "verticalShapePower", Value::Float(1.42) },
			{ "detailErosionLow", Value::Float(0.28) },
			{ "detailErosionHigh", Value::Float(0.81) },
			{ "detailEdgeStrength", Value::Float(0.27) },
			{ "sigmaTRef", Value::Float(1.0) },
			{ "viewAbsorption", Value::Float(1.0) },
			{ "lightAbsorption", Value::Float(1.0) },
			{ "singleScatteringAlbedo", Value::Float(0.999) },
			{ "forwardEccentricity", Value::Float(0.7) },
			{ "backwardEccentricity", Value::Float(0.25) },
			{ "msAttenuation", Value::Float(0.5) },
			{ "msContribution", Value::Float(0.5) },
			{ "msEccentricity", Value::Float(1.0) },
			{ "scatteringTintR", Value::Float(1.0) },
			{ "scatteringTintG", Value::Float(1.0) },
			{ "scatteringTintB", Value::Float(1.0) },
			{ "scatterSourceODScale", Value::Float(0.12) },
			{ "scatterSourceCurvePow", Value::Float(1.0) },
			{ "aoUpwardScale", Value::Float(1.0) },
			{ "ambientBottomStrength", Value::Float(0.1) },
			{ "ambientTopStrength", Value::Float(0.35) },
			{ "ambientDuskWarmth", Value::Float(0.65) },
			{ "boundaryConfidence", Value::Float(0.75) },
			{ "boundaryWrap", Value::Float(0.35) },
			{ "phiFwdIntensity", Value::Float(0.8) },
			{ "phiFwdDepthPow", Value::Float(1.0) },
			{ "phiFwdDepthBias", Value::Float(0.05) },
			{ "phiFwdMSBuildScale", Value::Float(1.0) },
			{ "phiFwdCompress", Value::Float(1.0) },
			{ "phiFwdMaxDistance", Value::Float(6000.0) },
			{ "phiFwdConeRatio", Value::Float(1.45) },
			{ "phiFwdMinStep", Value::Float(80.0) },
			{ "lightStepCount", Value::Float(8.0) },
			{ "boundaryGradientStep", Value::Float(250.0) },
			{ "boundaryGradientStrength", Value::Float(0.0) },
			{ "shadingDebugMode", Value::Float(0.0) },
			{ "shadow", Value::Object({
				{ "enabled", Value::Bool(true) },
				{ "atmosphereStrength", Value::Float(0.75) },
				{ "ambientOcclusionStrength", Value::Float(0.25) }
			}) }
		}) }
	});
}

bool TestPostProcessSceneSettingsProjection()
{
	using Value = Vans::VansSerializedValue;
	const Value sceneSettings = Value::Object({
		{ "environment", BuildValidEnvironmentSettingsForTest() },
		{ "postProcess", Value::Object({
			{ "exposure", Value::Object({
				{ "enableAutoExposure", Value::Bool(false) },
				{ "exposureCompensation", Value::Float(1.25) },
				{ "minEV100", Value::Float(-4.0) },
				{ "maxEV100", Value::Float(12.0) }
			}) },
			{ "bloom", Value::Object({
				{ "enable", Value::Bool(true) },
				{ "intensity", Value::Float(0.75) },
				{ "clamp", Value::Float(48.0) },
				{ "tintR", Value::Float(1.1) },
				{ "tintG", Value::Float(0.9) },
				{ "tintB", Value::Float(0.7) },
				{ "shapeMode", Value::Int(2) },
				{ "shapeIntensity", Value::Float(0.6) },
				{ "shapeBlend", Value::Float(0.8) },
				{ "shapeAngleDeg", Value::Float(30.0) },
				{ "anamorphicStretch", Value::Float(5.0) },
				{ "streakCount", Value::Int(6) },
				{ "streakLength", Value::Float(36.0) },
				{ "streakAttenuation", Value::Float(0.75) }
			}) },
			{ "dof", Value::Object({
				{ "enable", Value::Bool(true) },
				{ "focusDistance", Value::Float(6.0) },
				{ "focalLengthMm", Value::Float(70.0) },
				{ "fStop", Value::Float(2.0) },
				{ "sensorHeightMm", Value::Float(24.0) },
				{ "maxCoC", Value::Float(16.0) },
				{ "blurTransmissionBackground", Value::Bool(false) }
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

	Vans::VansSceneRenderSettingsConfig config;
	std::string error;
	const bool readSucceeded =
		Vans::VansSceneRenderSettingsConfigReader::Read(sceneSettings, config, error);
	const std::string validationMessage =
		"Scene render settings failed strict environment validation: " + error;
	if (!Expect(readSucceeded, validationMessage.c_str()))
		return false;
	if (!Expect(config.postProcess.has_value(), "Post-process scene settings were not projected"))
		return false;
	const Vans::VansScenePostProcessSettingsConfig& postProcess = *config.postProcess;
	if (!Expect(postProcess.enableAutoExposure == false &&
		postProcess.exposureCompensation == 1.25f &&
		postProcess.minEV100 == -4.0f && postProcess.maxEV100 == 12.0f,
		"Exposure scene settings were not projected"))
		return false;
	if (!Expect(postProcess.enableBloom == true && postProcess.bloomIntensity == 0.75f &&
		postProcess.bloomClamp == 48.0f &&
		postProcess.bloomTintR == 1.1f && postProcess.bloomTintG == 0.9f &&
		postProcess.bloomTintB == 0.7f &&
		postProcess.bloomShapeMode == 2 &&
		postProcess.bloomShapeIntensity == 0.6f &&
		postProcess.bloomShapeBlend == 0.8f &&
		postProcess.bloomShapeAngleDeg == 30.0f &&
		postProcess.bloomAnamorphicStretch == 5.0f &&
		postProcess.bloomStreakCount == 6 &&
		postProcess.bloomStreakLength == 36.0f &&
		postProcess.bloomStreakAttenuation == 0.75f,
		"Bloom scene settings were not projected"))
		return false;
	if (!Expect(postProcess.enableDOF == true &&
		postProcess.focusDistance == 6.0f && postProcess.focalLengthMm == 70.0f &&
		postProcess.fStop == 2.0f && postProcess.sensorHeightMm == 24.0f &&
		postProcess.maxCoC == 16.0f && postProcess.dofBlurTransmissionBackground == false,
		"DOF scene settings were not projected"))
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
	if (!Expect(manager.GetStatistics().residentLights == 0,
		"Pending punctual allocations were published before GPU submission"))
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
	}
	manager.NotifyRenderJobsSubmitted();
	manager.PrepareFrame(camera, lights, 2);
	if (!Expect(manager.GetStatistics().residentLights == lights.size(),
		"Submitted punctual allocations were not published on the next frame"))
		return false;

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
	manager.PrepareFrame(camera, lights, 3);

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
	manager.PrepareFrame(camera, lights, 4);
	return Expect(manager.GetShadowMetaIndex(duplicatedStableId) == VANS_INVALID_SHADOW_INDEX,
		"Duplicate stable light IDs were allowed to alias one shadow metadata entry");
}

bool TestPunctualShadowResolutionAndPendingLifecycle()
{
	using namespace VansGraphics;
	VansPunctualShadowCameraData camera;
	camera.position = glm::vec3(0.0f);
	camera.viewportWidth = 3840;
	camera.viewportHeight = 2160;

	VansPunctualShadowLightInput highResolutionLight;
	highResolutionLight.stableLightId = 1;
	highResolutionLight.type = VansPunctualShadowLightType::Point;
	highResolutionLight.intensity = 10.0f;
	highResolutionLight.radius = 20.0f;
	highResolutionLight.settings.castShadows = true;
	highResolutionLight.settings.resolution = VansShadowResolution::R1024;
	highResolutionLight.settings.maxShadowDistance = 100.0f;

	VansPunctualShadowManager resolutionManager(2048, 128, 2);
	resolutionManager.PrepareFrame(camera, { highResolutionLight }, 1);
	if (!Expect(resolutionManager.GetRenderJobs().size() == 6,
		"Capped point shadow did not schedule an atomic six-face update"))
		return false;
	for (const VansPunctualShadowRenderJob& job : resolutionManager.GetRenderJobs())
	{
		if (!Expect(job.resolution == 512,
			"Punctual shadow resolution exceeded the temporary 512px cap"))
			return false;
	}

	VansPunctualShadowManager pendingManager(512, 128, 2);
	VansPunctualShadowBudget budget = pendingManager.GetBudget();
	budget.atlasPageBudget = 6;
	budget.maxDirtyTexelsPerFrame = 0;
	pendingManager.SetBudget(budget);

	std::vector<VansPunctualShadowLightInput> lights(2, highResolutionLight);
	for (uint32_t index = 0; index < lights.size(); ++index)
	{
		lights[index].stableLightId = index + 10u;
		lights[index].gpuLightIndex = index;
		lights[index].position = glm::vec3(index == 0 ? 0.0f : 100.0f, 0.0f, 0.0f);
		lights[index].radius = 1.0f;
		lights[index].settings.policy = VansShadowPolicy::DistanceDynamic;
		lights[index].settings.resolution = VansShadowResolution::R128;
		lights[index].settings.maxShadowDistance = 200.0f;
	}

	pendingManager.PrepareFrame(camera, lights, 1);
	if (!Expect(pendingManager.GetStatistics().usedAtlasPages == 6,
		"Initial pending point allocation did not consume exactly six pages"))
		return false;
	camera.position.x = 100.0f;
	pendingManager.PrepareFrame(camera, lights, 2);
	return Expect(pendingManager.GetStatistics().usedAtlasPages == 6 &&
		pendingManager.GetStatistics().allocationFailures == 0,
		"Unselected pending point allocation leaked Atlas pages");
}

bool TestPointShadowAtlasUpdatePolicy()
{
	using namespace VansGraphics;
	VansPunctualShadowManager manager(512, 128, 2);
	VansPunctualShadowBudget budget = manager.GetBudget();
	budget.atlasPageBudget = 12;
	budget.maxDirtyTexelsPerFrame = 0;
	manager.SetBudget(budget);

	VansPunctualShadowCameraData camera;
	std::vector<VansPunctualShadowLightInput> lights(2);
	for (uint32_t index = 0; index < lights.size(); ++index)
	{
		auto& light = lights[index];
		light.stableLightId = index + 1u;
		light.gpuLightIndex = index;
		light.type = VansPunctualShadowLightType::Point;
		light.position = glm::vec3(static_cast<float>(index), 0.0f, 2.0f);
		light.intensity = 10.0f;
		light.radius = 10.0f;
		light.settings.castShadows = true;
		light.settings.resolution = VansShadowResolution::R128;
		light.settings.maxShadowDistance = 100.0f;
		// 点光更新策略只由 Atlas 归属决定，不能被旧资产配置覆盖。
		light.settings.updateMode = VansShadowUpdateMode::Budgeted;
	}

	manager.PrepareFrame(camera, lights, 1);
	std::array<uint32_t, VANS_PUNCTUAL_SHADOW_ATLAS_COUNT> initialJobsPerAtlas{};
	for (const VansPunctualShadowRenderJob& job : manager.GetRenderJobs())
	{
		if (!Expect(job.atlasIndex < initialJobsPerAtlas.size() &&
			job.rendersPendingAllocation && job.atomicGroupId != 0,
			"Initial point shadow allocation was not scheduled as an atomic Atlas upload"))
			return false;
		++initialJobsPerAtlas[job.atlasIndex];
	}
	if (!Expect(initialJobsPerAtlas[VANS_PUNCTUAL_SHADOW_PRIMARY_ATLAS_INDEX] == 6 &&
		initialJobsPerAtlas[VANS_PUNCTUAL_SHADOW_SECONDARY_ATLAS_INDEX] == 6,
		"Point shadow allocation did not populate both Atlases with complete six-face groups"))
		return false;
	manager.NotifyRenderJobsSubmitted();

	manager.PrepareFrame(camera, lights, 2);
	uint32_t primaryLightId = VANS_INVALID_SHADOW_INDEX;
	uint32_t secondaryLightId = VANS_INVALID_SHADOW_INDEX;
	uint64_t secondaryLastRenderedFrame = 0;
	for (const VansPunctualShadowRuntimeDebug& light : manager.CaptureDebugSnapshot().lights)
	{
		if (!light.activeBlocks[0].IsValid())
			return Expect(false, "Resident point shadow has no valid Atlas block");
		if (light.activeBlocks[0].atlasIndex == VANS_PUNCTUAL_SHADOW_PRIMARY_ATLAS_INDEX)
			primaryLightId = light.stableLightId;
		else if (light.activeBlocks[0].atlasIndex == VANS_PUNCTUAL_SHADOW_SECONDARY_ATLAS_INDEX)
		{
			secondaryLightId = light.stableLightId;
			secondaryLastRenderedFrame = light.lastRenderedFrame;
		}
	}
	if (!Expect(primaryLightId != VANS_INVALID_SHADOW_INDEX &&
		secondaryLightId != VANS_INVALID_SHADOW_INDEX,
		"Point lights were not assigned to distinct primary and secondary Atlases"))
		return false;

	const auto& primaryJobs = manager.GetRenderJobs();
	if (!Expect(primaryJobs.size() == 6 && !manager.HasRenderJobs(VANS_PUNCTUAL_SHADOW_SECONDARY_ATLAS_INDEX),
		"Secondary Atlas point shadow was redrawn after its initial upload"))
		return false;
	const uint32_t primaryAtomicGroup = primaryJobs.front().atomicGroupId;
	for (const VansPunctualShadowRenderJob& job : primaryJobs)
	{
		if (!Expect(job.stableLightId == primaryLightId &&
			job.atlasIndex == VANS_PUNCTUAL_SHADOW_PRIMARY_ATLAS_INDEX &&
			job.atomicGroupId == primaryAtomicGroup && primaryAtomicGroup != 0 &&
			(job.dirtyReasons & VansShadowDirty_DynamicCaster) != 0,
			"Primary Atlas point shadow was not scheduled as one forced per-frame group"))
			return false;
	}

	const uint32_t secondaryMetaIndex = manager.GetShadowMetaIndex(secondaryLightId);
	if (!Expect(secondaryMetaIndex < manager.GetGPUShadowData().size(),
		"Secondary Atlas point shadow lost its metadata binding"))
		return false;
	const VansPunctualShadowGPU& secondaryGPU = manager.GetGPUShadowData()[secondaryMetaIndex];
	if (!Expect(secondaryGPU.firstView != VANS_INVALID_SHADOW_INDEX &&
		secondaryGPU.firstView < manager.GetGPUShadowViews().size(),
		"Secondary Atlas point shadow did not publish a cached view"))
		return false;
	const glm::mat4 cachedSecondaryMatrix = manager.GetGPUShadowViews()[secondaryGPU.firstView].worldToShadow;
	manager.NotifyRenderJobsSubmitted();

	manager.InvalidateAllCasters(VansShadowDirty_CasterGeometry);
	for (auto& light : lights)
		light.position.x += 4.0f;
	manager.PrepareFrame(camera, lights, 3);
	if (!Expect(manager.GetRenderJobs().size() == 6 &&
		!manager.HasRenderJobs(VANS_PUNCTUAL_SHADOW_SECONDARY_ATLAS_INDEX),
		"Secondary Atlas point shadow reacted to transform or caster invalidation"))
		return false;

	const uint32_t movedSecondaryMetaIndex = manager.GetShadowMetaIndex(secondaryLightId);
	if (!Expect(movedSecondaryMetaIndex < manager.GetGPUShadowData().size(),
		"Moved secondary Atlas point shadow lost its cached metadata"))
		return false;
	const VansPunctualShadowGPU& movedSecondaryGPU = manager.GetGPUShadowData()[movedSecondaryMetaIndex];
	if (!Expect(movedSecondaryGPU.firstView != VANS_INVALID_SHADOW_INDEX &&
		movedSecondaryGPU.firstView < manager.GetGPUShadowViews().size(),
		"Moved secondary Atlas point shadow lost its cached sampling view"))
		return false;
	const glm::mat4& movedSecondaryMatrix = manager.GetGPUShadowViews()[movedSecondaryGPU.firstView].worldToShadow;
	float maxMatrixDifference = 0.0f;
	for (uint32_t column = 0; column < 4; ++column)
	{
		for (uint32_t row = 0; row < 4; ++row)
			maxMatrixDifference = (std::max)(maxMatrixDifference,
				std::abs(cachedSecondaryMatrix[column][row] - movedSecondaryMatrix[column][row]));
	}
	if (!Expect(maxMatrixDifference <= 1e-6f,
		"Secondary Atlas reused cached depth with a newly recomputed sampling matrix"))
		return false;

	for (const VansPunctualShadowRuntimeDebug& light : manager.CaptureDebugSnapshot().lights)
	{
		if (light.stableLightId == secondaryLightId)
			return Expect(light.lastRenderedFrame == secondaryLastRenderedFrame &&
				light.dirtyFaceMask == 0,
				"Secondary Atlas point shadow did not remain clean after its initial upload");
	}

	return Expect(false, "Secondary Atlas point shadow disappeared from diagnostics");
}

bool TestGIProbeUpdateScheduleContract()
{
	using namespace VansGraphics;
    GIProbeRegionDesc desc; desc.raysPerProbe = 256;
    const auto region = ResolveGIRegion(desc);
    GIProbePlacementSettings budget;
    if (!Expect(GIProbeFixedRayCount(region.raysPerProbe) == 32u &&
        GIProbeRayCapacity(region.probeCount, region.raysPerProbe, budget) == 65536u,
        "GI full-sphere budget or fixed geometry ray count changed")) return false;

	VansGISettings temporalSettings;
	temporalSettings.irradianceHysteresis = 2.0f;
	temporalSettings.distanceHysteresis = -1.0f;
	temporalSettings.distanceSharpness = 2.0f;
	NormalizeGISettings(temporalSettings);
	if (!Expect(temporalSettings.irradianceHysteresis == 0.999f &&
		temporalSettings.distanceHysteresis == 0.0f &&
		temporalSettings.distanceSharpness == 8.0f,
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
	if (!Expect(disabledSelectedRegions.size() == 2u && disabledSelectedRegions[0]->stableId == 11u,
		"GI region order retained a disabled selected region in the atlas/SSGI binding order"))
	{
		return false;
	}

	// 首次 Editor 场景准备发生在第一份渲染帧快照之前。SSGI 的参数构造
	// 必须完整表达显式传入的场景配置，不能依赖先前快照中的默认值。
	VansGISettings sceneSettings;
	sceneSettings.regions.clear();
	GIProbeRegionDesc sceneRegion;
	sceneRegion.center = glm::vec3(14.0f, 11.0f, 28.0f);
	sceneRegion.gridDimensions = glm::uvec3(56u, 28u, 42u);
	sceneRegion.probeSpacing = 1.0f;
	sceneRegion.overrideGridDimensions = true;
	sceneRegion.maxRayDistance = 19.0f;
	sceneRegion.normalBias = 0.37f;
	sceneRegion.volumeFadeDistance = 2.5f;
	sceneRegion.priority = 4.0f;
	sceneSettings.regions.push_back(sceneRegion);
	const SSGIParamsGPU sceneParams = BuildSSGIParamsFromGISettings(
		sceneSettings, 1920u, 1080u);
	if (!Expect(sceneParams.regionInfo.x == 1.0f &&
		sceneParams.screenSize == glm::vec4(1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f) &&
		sceneParams.regions[0].volumeMin.x == -14.0f &&
		sceneParams.regions[0].volumeMin.y == -3.0f &&
		sceneParams.regions[0].volumeMin.z == 7.0f &&
		sceneParams.regions[0].volumeSizeAndBias == glm::vec4(56.0f, 28.0f, 42.0f, 0.37f) &&
		sceneParams.regions[0].traceParams.x == 19.0f &&
		sceneParams.regions[0].traceParams.z == 2.5f &&
		sceneParams.regions[0].gridDimensionsAndPriority == glm::vec4(56.0f, 28.0f, 42.0f, 4.0f),
		"SSGI parameter construction did not preserve first-load scene GI settings"))
	{
		return false;
	}

	return true;
}
}

bool TestFrameSubmitRetirementContract()
{
	using namespace VansGraphics;
	struct Calls { int submit = 0, wait = 0, reset = 0, idle = 0, created = 0, destroyed = 0, failSubmit = 0; };
	static Calls calls;
	calls = {};
	struct RestoreFunctions
	{
		PFN_vkQueueSubmit submit = VansGraphics::vkQueueSubmit;
		PFN_vkWaitForFences wait = VansGraphics::vkWaitForFences;
		PFN_vkResetFences reset = VansGraphics::vkResetFences;
		PFN_vkDeviceWaitIdle idle = VansGraphics::vkDeviceWaitIdle;
		PFN_vkCreateSemaphore create = VansGraphics::vkCreateSemaphore;
		PFN_vkDestroySemaphore destroy = VansGraphics::vkDestroySemaphore;
		~RestoreFunctions()
		{
			VansGraphics::vkQueueSubmit = submit; VansGraphics::vkWaitForFences = wait; VansGraphics::vkResetFences = reset;
			VansGraphics::vkDeviceWaitIdle = idle; VansGraphics::vkCreateSemaphore = create; VansGraphics::vkDestroySemaphore = destroy;
		}
	} restore;
	VansGraphics::vkQueueSubmit = [](VkQueue, uint32_t, const VkSubmitInfo*, VkFence) -> VkResult
	{
		return ++calls.submit == calls.failSubmit ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
	};
	VansGraphics::vkWaitForFences = [](VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) -> VkResult
	{ ++calls.wait; return VK_SUCCESS; };
	VansGraphics::vkResetFences = [](VkDevice, uint32_t, const VkFence*) -> VkResult
	{ ++calls.reset; return VK_SUCCESS; };
	VansGraphics::vkDeviceWaitIdle = [](VkDevice) -> VkResult { ++calls.idle; return VK_SUCCESS; };
	VansGraphics::vkCreateSemaphore = [](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* result) -> VkResult
	{ *result = reinterpret_cast<VkSemaphore>(uintptr_t(100 + ++calls.created)); return VK_SUCCESS; };
	VansGraphics::vkDestroySemaphore = [](VkDevice, VkSemaphore, const VkAllocationCallbacks*) { ++calls.destroyed; };
	VansFrameSubmitOrchestrator graph;
	graph.Bind(reinterpret_cast<VkDevice>(uintptr_t(1)), reinterpret_cast<VkQueue>(uintptr_t(2)), reinterpret_cast<VkQueue>(uintptr_t(3)));
	const auto build = [&]()
	{
		graph.Reset();
		VansFrameSubmitNode producer;
		producer.name = "Producer";
		producer.queue = VansQueueRole::Compute;
		producer.commandBuffers = {reinterpret_cast<VkCommandBuffer>(uintptr_t(4))};
		producer.signals = {VansSyncPoint::TileLightReady};
		producer.fence = reinterpret_cast<VkFence>(uintptr_t(6));
		graph.AddNode(std::move(producer));
		VansFrameSubmitNode consumer;
		consumer.name = "Consumer";
		consumer.commandBuffers = {reinterpret_cast<VkCommandBuffer>(uintptr_t(5))};
		consumer.waits = {{VansSyncPoint::TileLightReady, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT}};
		consumer.fence = reinterpret_cast<VkFence>(uintptr_t(7));
		graph.AddNode(std::move(consumer));
	};
	build();
	bool valid = graph.Execute() && graph.HasPendingWork() && calls.wait == 0 && calls.created == 1;
	build();
	valid = valid && !graph.Execute() && graph.HasPendingWork() && calls.destroyed == 0;
	valid = valid && graph.WaitForCompletion() && !graph.HasPendingWork() && calls.wait == 1 && calls.reset == 0;
	valid = valid && graph.Execute() && calls.created == 1; // 已完成的边才复用。
	valid = valid && graph.WaitForCompletion();
	build();
	calls.failSubmit = calls.submit + 2; // 生产者成功，消费者失败；可能已 signal 的边必须销毁。
	valid = valid && !graph.Execute() && !graph.HasPendingWork() && calls.idle == 1 && calls.destroyed == 1;
	build();
	calls.failSubmit = 0;
	valid = valid && graph.Execute() && calls.created == 2;
	graph.Shutdown(); // 关闭也必须等待最后一张提交图。
	return valid && calls.wait == 3 && calls.destroyed == 2;
}

bool TestAsyncComputeSubmitGraphContract()
{
	using namespace VansGraphics;
	std::vector<std::string> asyncCatalogErrors;
	if (!VansRenderPassCatalog::AuditAsyncMigrationContracts(asyncCatalogErrors))
		return false;

	VansQueueCapabilities capabilities;
	capabilities.graphicsFamily = 0u;
	capabilities.computeFamily = 0u;
	if (capabilities.SupportsAsyncCompute())
		return false;
	capabilities.computeFamily = 1u;
	capabilities.hasDedicatedAsyncComputeQueue = true;
	if (!capabilities.SupportsAsyncCompute())
		return false;

	Vans::VansProjectRenderSettingsData persistedSettings;
	persistedSettings.commandRecordingSettings.asyncComputeEnabled = true;
	persistedSettings.renderOutputSettings.width = 3840u;
	persistedSettings.renderOutputSettings.height = 2160u;
	const nlohmann::json encodedSettings =
		Vans::VansProjectSettingsJsonCodec::EncodeRenderSettings(persistedSettings);
	if (encodedSettings.value("schemaVersion", 0u) != 2u
		|| encodedSettings.contains("fsr")
		|| encodedSettings["upscaler"].value("backend", "") != "FSR"
		|| encodedSettings["upscaler"].value("quality", "") != "Quality"
		|| encodedSettings["outputResolution"].value("width", 0u) != 3840u
		|| encodedSettings["outputResolution"].value("height", 0u) != 2160u
		|| !encodedSettings.contains("cameraLensLimits")
		|| encodedSettings["cameraLensLimits"].value("minimumNearClip", 0.0f) != 0.1f
		|| !encodedSettings["commandRecording"].value("asyncComputeEnabled", false)
		|| encodedSettings["commandRecording"].contains("asyncComputeMode")
		|| encodedSettings["commandRecording"].contains("asyncGIEnabled"))
	{
		return false;
	}
	Vans::VansProjectRenderSettingsData decodedSettings;
	std::vector<std::string> warnings;
	std::string codecError;
	if (!Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		encodedSettings, decodedSettings, warnings, codecError))
	{
		return false;
	}
	if (!decodedSettings.commandRecordingSettings.asyncComputeEnabled
		|| decodedSettings.renderOutputSettings.width != 3840u
		|| decodedSettings.renderOutputSettings.height != 2160u)
		return false;

	nlohmann::json legacyWindowExtentSettings = encodedSettings;
	legacyWindowExtentSettings.erase("outputResolution");
	Vans::VansProjectRenderSettingsData decodedWindowExtentSettings;
	warnings.clear();
	if (!Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		legacyWindowExtentSettings,
		decodedWindowExtentSettings,
		warnings,
		codecError)
		|| !decodedWindowExtentSettings.renderOutputSettings.UsesWindowExtent())
	{
		return false;
	}

	nlohmann::json invalidOutputSettings = encodedSettings;
	invalidOutputSettings["outputResolution"]["height"] = 0u;
	if (Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		invalidOutputSettings,
		decodedWindowExtentSettings,
		warnings,
		codecError))
	{
		return false;
	}

	nlohmann::json legacySettings = encodedSettings;
	legacySettings["commandRecording"].erase("asyncComputeEnabled");
	legacySettings["commandRecording"]["asyncComputeMode"] = "Auto";
	Vans::VansProjectRenderSettingsData migratedSettings;
	warnings.clear();
	if (!Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		legacySettings, migratedSettings, warnings, codecError)
		|| migratedSettings.commandRecordingSettings.asyncComputeEnabled
		|| !warnings.empty())
	{
		return false;
	}

	nlohmann::json schemaOneSettings = encodedSettings;
	schemaOneSettings["schemaVersion"] = 1;
	if (Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		schemaOneSettings, migratedSettings, warnings, codecError))
	{
		return false;
	}
	nlohmann::json invalidUpscalerSettings = encodedSettings;
	invalidUpscalerSettings["upscaler"]["backend"] = "UnknownBackend";
	if (Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		invalidUpscalerSettings, migratedSettings, warnings, codecError))
	{
		return false;
	}
	nlohmann::json invalidCameraLensSettings = encodedSettings;
	invalidCameraLensSettings["cameraLensLimits"]["minimumNearClip"] = 0.0f;
	if (Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		invalidCameraLensSettings, migratedSettings, warnings, codecError))
	{
		return false;
	}

	const VkDevice fakeDevice = reinterpret_cast<VkDevice>(uintptr_t(1));
	const VkQueue fakeGraphicsQueue = reinterpret_cast<VkQueue>(uintptr_t(2));
	const VkQueue fakeComputeQueue = reinterpret_cast<VkQueue>(uintptr_t(3));
	const VkCommandBuffer fakeComputeCmd = reinterpret_cast<VkCommandBuffer>(uintptr_t(4));
	const VkCommandBuffer fakeGraphicsCmd = reinterpret_cast<VkCommandBuffer>(uintptr_t(5));

	VansFrameSubmitOrchestrator graph;
	graph.Bind(fakeDevice, fakeGraphicsQueue, fakeComputeQueue);
	VansFrameSubmitNode producer;
	producer.name = "TileLight";
	producer.queue = VansQueueRole::Compute;
	producer.commandBuffers = { fakeComputeCmd };
	producer.signals = { VansSyncPoint::TileLightReady };
	producer.resources = {
		{ "TileLightLists", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, false, true, false, false }
	};
	graph.AddNode(std::move(producer));

	VansFrameSubmitNode consumer;
	consumer.name = "Deferred";
	consumer.queue = VansQueueRole::Graphics;
	consumer.commandBuffers = { fakeGraphicsCmd };
	consumer.waits = {
		{ VansSyncPoint::TileLightReady, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT }
	};
	consumer.resources = {
		{ "TileLightLists", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, false, false, false, false }
	};
	consumer.fence = reinterpret_cast<VkFence>(uintptr_t(6));
	graph.AddNode(std::move(consumer));
	std::string validationError;
	if (!graph.Validate(&validationError))
		return false;
	const std::string debugSummary = graph.BuildDebugSummary();
	if (debugSummary.find("TileLightReady") == std::string::npos
		|| debugSummary.find("TileLightLists") == std::string::npos)
	{
		return false;
	}

	graph.Reset();
	VansFrameSubmitNode shadowMaps;
	shadowMaps.name = "ShadowMaps";
	shadowMaps.queue = VansQueueRole::Graphics;
	shadowMaps.commandBuffers = { fakeGraphicsCmd };
	shadowMaps.signals = { VansSyncPoint::ShadowMapsReady };
	shadowMaps.resources = {
		{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false },
		{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false }
	};
	graph.AddNode(std::move(shadowMaps));

	VansFrameSubmitNode shadowConsumer;
	shadowConsumer.name = "DualPunctualShadowConsumer";
	shadowConsumer.queue = VansQueueRole::Compute;
	shadowConsumer.commandBuffers = { fakeComputeCmd };
	shadowConsumer.waits = {
		{ VansSyncPoint::ShadowMapsReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
	};
	shadowConsumer.resources = {
		{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
		{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false }
	};
	shadowConsumer.fence = reinterpret_cast<VkFence>(uintptr_t(6));
	graph.AddNode(std::move(shadowConsumer));
	if (!graph.Validate(&validationError))
		return false;
	const std::string dualShadowSummary = graph.BuildDebugSummary();
	if (dualShadowSummary.find("ShadowMapsReady") == std::string::npos
		|| dualShadowSummary.find("PunctualShadowAtlas0") == std::string::npos
		|| dualShadowSummary.find("PunctualShadowAtlas1") == std::string::npos)
	{
		return false;
	}

	graph.Reset();
	VansFrameSubmitNode orphanCompute;
	orphanCompute.name = "OrphanCompute";
	orphanCompute.queue = VansQueueRole::Compute;
	orphanCompute.commandBuffers = { fakeComputeCmd };
	orphanCompute.signals = { VansSyncPoint::WaterWaveDone };
	graph.AddNode(std::move(orphanCompute));
	VansFrameSubmitNode uncoveredFinal;
	uncoveredFinal.name = "UncoveredFinal";
	uncoveredFinal.queue = VansQueueRole::Graphics;
	uncoveredFinal.commandBuffers = { fakeGraphicsCmd };
	uncoveredFinal.fence = reinterpret_cast<VkFence>(uintptr_t(6));
	graph.AddNode(std::move(uncoveredFinal));
	if (graph.Validate(&validationError)
		|| validationError.find("not covered by the final completion fence") == std::string::npos)
	{
		return false;
	}

	graph.Reset();
	VansFrameSubmitNode unsafeProducer;
	unsafeProducer.name = "UnsafeProducer";
	unsafeProducer.queue = VansQueueRole::Compute;
	unsafeProducer.commandBuffers = { fakeComputeCmd };
	unsafeProducer.signals = { VansSyncPoint::TileLightReady };
	unsafeProducer.resources = {
		{ "SharedBuffer", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, false }
	};
	graph.AddNode(std::move(unsafeProducer));
	VansFrameSubmitNode unsafeConsumer;
	unsafeConsumer.name = "UnsafeConsumer";
	unsafeConsumer.queue = VansQueueRole::Graphics;
	unsafeConsumer.commandBuffers = { fakeGraphicsCmd };
	unsafeConsumer.resources = {
		{ "SharedBuffer", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false }
	};
	unsafeConsumer.fence = reinterpret_cast<VkFence>(uintptr_t(6));
	graph.AddNode(std::move(unsafeConsumer));
	if (graph.Validate(&validationError)
		|| validationError.find("cross-queue resource hazard") == std::string::npos)
	{
		return false;
	}

	graph.Reset();
	VansFrameSubmitNode invalidConsumer;
	invalidConsumer.name = "InvalidConsumer";
	invalidConsumer.queue = VansQueueRole::Graphics;
	invalidConsumer.commandBuffers = { fakeGraphicsCmd };
	invalidConsumer.waits = {
		{ VansSyncPoint::GBufferMaterialReady, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT }
	};
	graph.AddNode(std::move(invalidConsumer));
	if (graph.Validate(&validationError))
		return false;

	graph.Shutdown();
	return true;
}

bool TestUnifiedUpscalerHistoryContract()
{
	using namespace VansGraphics;
	VansUpscalerHistoryState history;
	const auto contains = [](VansUpscalerResetReason value, VansUpscalerResetReason flag)
	{
		return (value & flag) != VansUpscalerResetReason::None;
	};

	if (!Expect(
		contains(history.GetPendingReasons(), VansUpscalerResetReason::FirstFrame),
		"Unified upscaler history must begin with FirstFrame reset"))
	{
		return false;
	}

	history.ObserveFrame(
		7,
		1u,
		{ 1280, 720 },
		{ 1920, 1080 },
		VansUpscalerBackend::FSR,
		VansUpscaleQualityMode::Quality);
	history.OnTemporalDispatchSucceeded();
	if (!Expect(!history.IsResetPending() &&
		contains(history.GetLastConsumedResetReasons(), VansUpscalerResetReason::FirstFrame),
		"Successful temporal dispatch must consume pending reset"))
	{
		return false;
	}

	history.ObserveFrame(
		9,
		2u,
		{ 960, 540 },
		{ 1600, 900 },
		VansUpscalerBackend::DLSS,
		VansUpscaleQualityMode::Balanced);
	const VansUpscalerResetReason changed = history.GetPendingReasons();
	if (!Expect(
		contains(changed, VansUpscalerResetReason::FrameDiscontinuity) &&
		contains(changed, VansUpscalerResetReason::CameraCut) &&
		contains(changed, VansUpscalerResetReason::RenderSizeChange) &&
		contains(changed, VansUpscalerResetReason::OutputSizeChange) &&
		contains(changed, VansUpscalerResetReason::BackendChange) &&
		contains(changed, VansUpscalerResetReason::QualityChange),
		"Unified upscaler history did not retain all discontinuity reasons"))
	{
		return false;
	}

	history.ClearForOffBackend();
	return Expect(
		contains(history.GetPendingReasons(), VansUpscalerResetReason::FirstFrame),
		"Off backend must preserve FirstFrame reset for the next temporal backend");
}

bool TestUnifiedUpscalerResolutionContract()
{
	using namespace VansGraphics;
	std::string validationError;
	if (!Expect(
		VansUpscaleResolutionPolicy::ValidateOutputExtent(
			{ 0u, 0u }, true, 0u, validationError) &&
		!VansUpscaleResolutionPolicy::ValidateOutputExtent(
			{ 0u, 1080u }, true, 0u, validationError) &&
		VansUpscaleResolutionPolicy::ValidateOutputExtent(
			{ 320u, 180u }, false, 0u, validationError) &&
		VansUpscaleResolutionPolicy::ValidateOutputExtent(
			{ 16384u, 8192u }, false, 0u, validationError) &&
		!VansUpscaleResolutionPolicy::ValidateOutputExtent(
			{ 8193u, 4320u }, false, 8192u, validationError),
		"Upscaler output extent policy did not enforce window, engine, and device budgets"))
	{
		return false;
	}

	VansUpscalerConfig config;
	config.backend = VansUpscalerBackend::FSR;
	config.quality = VansUpscaleQualityMode::Quality;
	const VansUpscaleResolution fsrQuality =
		VansUpscaleResolutionPolicy::Resolve(config, { 1920, 1080 });
	if (!Expect(
		fsrQuality.valid &&
		fsrQuality.renderExtent == VansExtent2D{ 1280, 720 } &&
		fsrQuality.outputExtent == VansExtent2D{ 1920, 1080 },
		"FSR Quality must derive render extent from the fixed output extent"))
	{
		return false;
	}

	config.backend = VansUpscalerBackend::Off;
	config.quality = VansUpscaleQualityMode::NativeAA;
	const VansUpscaleResolution off =
		VansUpscaleResolutionPolicy::Resolve(config, { 1600, 900 });
	if (!Expect(
		off.valid &&
		off.renderExtent == off.outputExtent &&
		std::abs(off.mipBias + 1.0f) < 0.0001f,
		"Off NativeAA must keep render and output extents identical"))
	{
		return false;
	}

	config.quality = VansUpscaleQualityMode::Performance;
	if (!Expect(
		!VansUpscaleResolutionPolicy::Resolve(config, { 1600, 900 }).valid,
		"Off backend must reject non-NativeAA quality"))
	{
		return false;
	}

	config.backend = VansUpscalerBackend::DLSS;
	config.quality = VansUpscaleQualityMode::Balanced;
	const VansUpscaleResolution dlss =
		VansUpscaleResolutionPolicy::Resolve(
			config,
			{ 2560, 1440 },
			{ 1485, 835 });
	return Expect(
		dlss.valid &&
		dlss.renderExtent == VansExtent2D{ 1485, 835 } &&
		!VansUpscaleResolutionPolicy::Resolve(config, { 2560, 1440 }).valid,
		"DLSS resolution must use a validated backend recommendation");
}

bool TestUnifiedUpscalerManagerContract()
{
	using namespace VansGraphics;
	VansUpscalerManager manager;
	if (!Expect(
		manager.GetDesiredConfig().backend == VansUpscalerBackend::Off &&
		manager.GetDesiredConfig().quality == VansUpscaleQualityMode::NativeAA &&
		manager.GetEffectiveConfig() == manager.GetDesiredConfig(),
		"Upscaler runtime must start neutral before project settings are applied"))
	{
		return false;
	}

	const auto qualityBit = [](VansUpscaleQualityMode quality)
	{
		return 1u << static_cast<std::uint32_t>(quality);
	};
	VansUpscalerCapabilitySet capabilities;
	capabilities.off.backend = VansUpscalerBackend::Off;
	capabilities.off.compiledIn = true;
	capabilities.off.runtimeAvailable = true;
	capabilities.off.deviceSupported = true;
	capabilities.off.supportedQualityMask = qualityBit(VansUpscaleQualityMode::NativeAA);
	capabilities.fsr.backend = VansUpscalerBackend::FSR;
	capabilities.fsr.compiledIn = true;
	capabilities.fsr.runtimeAvailable = true;
	capabilities.fsr.deviceSupported = true;
	capabilities.fsr.supportedQualityMask =
		qualityBit(VansUpscaleQualityMode::NativeAA) |
		qualityBit(VansUpscaleQualityMode::Quality) |
		qualityBit(VansUpscaleQualityMode::Balanced) |
		qualityBit(VansUpscaleQualityMode::Performance) |
		qualityBit(VansUpscaleQualityMode::UltraPerformance);
	capabilities.dlss.backend = VansUpscalerBackend::DLSS;
	capabilities.dlss.unavailableReason = "DLSS test runtime unavailable";

	VansUpscalerConfig requested;
	requested.backend = VansUpscalerBackend::DLSS;
	requested.quality = VansUpscaleQualityMode::Balanced;
	const VansUpscalerSelectionChange fallback =
		manager.RequestConfig(requested, capabilities);
	if (!Expect(
		fallback.accepted && fallback.fallbackActive &&
		manager.GetDesiredConfig().backend == VansUpscalerBackend::DLSS &&
		manager.GetEffectiveConfig().backend == VansUpscalerBackend::FSR &&
		manager.GetEffectiveConfig().quality == VansUpscaleQualityMode::Balanced &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::NotCompiled,
		"DLSS unavailable fallback must preserve desired state and select FSR deterministically"))
	{
		return false;
	}

	capabilities.dlss.compiledIn = true;
	const VansUpscalerSelectionChange runtimeUnavailable =
		manager.RequestConfig(manager.GetDesiredConfig(), capabilities);
	if (!Expect(
		runtimeUnavailable.accepted && runtimeUnavailable.fallbackActive &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::RuntimeUnavailable,
		"Compiled DLSS without an initialized runtime must report RuntimeUnavailable"))
	{
		return false;
	}

	capabilities.dlss.unavailableReasonCode =
		VansUpscalerFallbackReason::MissingRuntimeBinary;
	const VansUpscalerSelectionChange missingRuntime =
		manager.RequestConfig(manager.GetDesiredConfig(), capabilities);
	if (!Expect(
		missingRuntime.accepted && missingRuntime.fallbackActive &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::MissingRuntimeBinary,
		"Typed DLSS runtime failures must preserve MissingRuntimeBinary through fallback"))
	{
		return false;
	}

	capabilities.dlss.runtimeAvailable = true;
	capabilities.dlss.unavailableReasonCode =
		VansUpscalerFallbackReason::DriverOutOfDate;
	const VansUpscalerSelectionChange oldDriver =
		manager.RequestConfig(manager.GetDesiredConfig(), capabilities);
	if (!Expect(
		oldDriver.accepted && oldDriver.fallbackActive &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::DriverOutOfDate,
		"Typed DLSS device failures must preserve DriverOutOfDate through fallback"))
	{
		return false;
	}

	capabilities.dlss.unavailableReasonCode = VansUpscalerFallbackReason::None;
	const VansUpscalerSelectionChange unsupportedDevice =
		manager.RequestConfig(manager.GetDesiredConfig(), capabilities);
	if (!Expect(
		unsupportedDevice.accepted && unsupportedDevice.fallbackActive &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::UnsupportedDevice,
		"Initialized DLSS on an unsupported device must report UnsupportedDevice"))
	{
		return false;
	}

	capabilities.dlss.deviceSupported = true;
	capabilities.dlss.supportedQualityMask =
		qualityBit(VansUpscaleQualityMode::Quality);
	const VansUpscalerSelectionChange unsupportedQuality =
		manager.RequestConfig(manager.GetDesiredConfig(), capabilities);
	if (!Expect(
		unsupportedQuality.accepted && unsupportedQuality.fallbackActive &&
		manager.GetEffectiveConfig().backend == VansUpscalerBackend::FSR &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::UnsupportedQuality,
		"Unsupported DLSS quality must select the supported FSR mode and report its exact cause"))
	{
		return false;
	}

	capabilities.dlss.supportedQualityMask = capabilities.fsr.supportedQualityMask;
	capabilities.dlss.unavailableReason.clear();
	VansUpscalerManager directManager;
	const VansUpscalerSelectionChange directDLSS =
		directManager.RequestConfig(requested, capabilities);
	if (!Expect(
		directDLSS.accepted && directDLSS.effectiveBackendChanged &&
		!directDLSS.fallbackActive &&
		directManager.GetDesiredConfig().backend == VansUpscalerBackend::DLSS &&
		directManager.GetEffectiveConfig().backend == VansUpscalerBackend::DLSS,
		"Available project DLSS configuration must activate DLSS directly from neutral startup"))
	{
		return false;
	}

	const VansUpscalerConfig desiredAfterDiscovery = manager.GetDesiredConfig();
	const VansUpscalerSelectionChange discovered =
		manager.RequestConfig(desiredAfterDiscovery, capabilities);
	if (!Expect(
		discovered.accepted && !discovered.fallbackActive &&
		manager.GetDesiredConfig().backend == VansUpscalerBackend::DLSS &&
		manager.GetEffectiveConfig().backend == VansUpscalerBackend::DLSS &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::None,
		"Late DLSS capability discovery must restore the preserved desired backend"))
	{
		return false;
	}

	const VansUpscalerConfig beforeInvalid = manager.GetDesiredConfig();
	VansUpscalerConfig invalid = requested;
	invalid.backend = VansUpscalerBackend::Off;
	invalid.quality = VansUpscaleQualityMode::Performance;
	if (!Expect(
		!manager.RequestConfig(invalid, capabilities).accepted &&
		manager.GetDesiredConfig() == beforeInvalid,
		"Rejected upscaler configuration must not mutate manager state"))
	{
		return false;
	}
	invalid = beforeInvalid;
	invalid.backend = static_cast<VansUpscalerBackend>(255u);
	if (!Expect(
		!manager.RequestConfig(invalid, capabilities).accepted &&
		manager.GetDesiredConfig() == beforeInvalid,
		"Unknown upscaler enum must fail before capability-mask evaluation"))
	{
		return false;
	}

	manager.ActivateRuntimeFallback(
		VansUpscalerBackend::Off,
		VansUpscaleQualityMode::NativeAA,
		VansUpscalerFallbackReason::DispatchFailed,
		"test dispatch failure");
	if (!Expect(
		manager.GetDesiredConfig().backend == VansUpscalerBackend::DLSS &&
		manager.GetEffectiveConfig().backend == VansUpscalerBackend::Off &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::DispatchFailed,
		"Runtime fallback must not overwrite the persisted desired backend"))
	{
		return false;
	}

	requested.backend = VansUpscalerBackend::FSR;
	const VansUpscalerSelectionChange recovered =
		manager.RequestConfig(requested, capabilities);
	return Expect(
		recovered.accepted && !recovered.fallbackActive &&
		manager.GetFallbackReason() == VansUpscalerFallbackReason::None &&
		manager.GetEffectiveConfig().backend == VansUpscalerBackend::FSR,
		"Accepted supported selection must clear a previous runtime fallback");
}

bool TestUnifiedUpscalerJitterContract()
{
	using namespace VansGraphics;
	if (!Expect(
		VansTemporalJitterSequence::CalculatePhaseCount(1280, 1920) == 18 &&
		VansTemporalJitterSequence::CalculatePhaseCount(1920, 1920) == 8 &&
		VansTemporalJitterSequence::CalculatePhaseCount(0, 1920) == 0,
		"Temporal jitter phase count must be derived from the render-to-output ratio"))
	{
		return false;
	}

	float firstX = 0.0f;
	float firstY = 0.0f;
	float repeatedX = 0.0f;
	float repeatedY = 0.0f;
	if (!Expect(
		VansTemporalJitterSequence::Sample(0, 18, firstX, firstY) &&
		VansTemporalJitterSequence::Sample(18, 18, repeatedX, repeatedY) &&
		std::abs(firstX - repeatedX) < 0.000001f &&
		std::abs(firstY - repeatedY) < 0.000001f &&
		firstX >= -0.5f && firstX < 0.5f &&
		firstY >= -0.5f && firstY < 0.5f,
		"Temporal jitter must be deterministic, bounded, and repeat at the phase boundary"))
	{
		return false;
	}

	float invalidX = 1.0f;
	float invalidY = 1.0f;
	return Expect(
		!VansTemporalJitterSequence::Sample(0, 0, invalidX, invalidY) &&
		invalidX == 0.0f && invalidY == 0.0f,
		"Invalid temporal jitter configuration must fail closed with a zero offset");
}

bool TestFSRTemporalProjectionContract()
{
	using namespace VansGraphics;
	constexpr float width = 1920.0f;
	constexpr float height = 1080.0f;
	const glm::vec2 renderSize(width, height);
	const glm::mat4 projection = glm::perspectiveRH_NO(
		glm::radians(60.0f),
		width / height,
		0.1f,
		1000.0f);
	const glm::vec4 viewPoint(0.25f, -0.1f, -5.0f, 1.0f);

	const auto projectToFramebuffer = [&](const glm::mat4& matrix)
	{
		const glm::vec4 clip = matrix * viewPoint;
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		return glm::vec2(
			(ndc.x * 0.5f + 0.5f) * width,
			(0.5f - ndc.y * 0.5f) * height);
	};

	const glm::vec2 basePixel = projectToFramebuffer(projection);
	const glm::vec2 samples[] = {
		{ 0.25f, 0.0f },
		{ -0.25f, 0.0f },
		{ 0.0f, 0.25f },
		{ 0.0f, -0.25f },
		{ 0.25f, 0.25f },
		{ -0.25f, -0.25f },
	};
	for (const glm::vec2 sample : samples)
	{
		const VansTemporalJitter jitter = BuildVulkanTemporalJitter(sample, renderSize);
		if (!jitter.valid ||
			!ExpectNear(jitter.framebufferPixels.x, sample.x, 1.0e-6f,
				"Vulkan jitter framebuffer X must match the SDK sample") ||
			!ExpectNear(jitter.framebufferPixels.y, sample.y, 1.0e-6f,
				"Vulkan jitter framebuffer Y must match the SDK sample"))
		{
			return false;
		}

		const glm::vec2 jitteredPixel = projectToFramebuffer(
			ApplyClipSpaceJitter(projection, jitter.ndcOffset));
		const glm::vec2 measuredOffset = jitteredPixel - basePixel;
		if (!ExpectNear(measuredOffset.x, sample.x, 1.0e-4f,
				"Clip-space jitter must move framebuffer X by the SDK sample") ||
			!ExpectNear(measuredOffset.y, sample.y, 1.0e-4f,
				"Clip-space jitter must move framebuffer Y by the SDK sample"))
		{
			return false;
		}

		const VansFSRDispatchJitter dispatch =
			BuildFSRDispatchJitter(sample.x, sample.y);
		if (!ExpectNear(dispatch.x, -sample.x, 0.0f,
				"FSR dispatch jitter X must use the API-boundary sign") ||
			!ExpectNear(dispatch.y, -sample.y, 0.0f,
				"FSR dispatch jitter Y must use the API-boundary sign"))
		{
			return false;
		}
	}

	return true;
}

bool TestVulkanDeviceDepthRangeContract()
{
	using namespace VansGraphics;
	const auto projectDepth = [](const glm::mat4& projection, const float distance)
	{
		const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
		return clip.z / clip.w;
	};

	struct RangeCase
	{
		float nearClip;
		float farClip;
	};
	const RangeCase cases[] = {
		{ 0.1f, 10000.0f },
		{ 0.5f, 1000.0f },
		{ 1.0f, 10.0f },
	};

	for (const RangeCase rangeCase : cases)
	{
		const glm::mat4 projection = glm::perspectiveRH_NO(
			glm::radians(60.0f),
			16.0f / 9.0f,
			rangeCase.nearClip,
			rangeCase.farClip);
		const VansDeviceDepthRange range = ExtractVulkanDeviceDepthRange(projection);
		const float expectedNear =
			2.0f * rangeCase.nearClip * rangeCase.farClip /
			(rangeCase.farClip + rangeCase.nearClip);
		if (!range.valid || !range.finiteFar ||
			!ExpectNear(range.nearDistance, expectedNear, expectedNear * 1.0e-5f,
				"RH_NO projection must expose its effective Vulkan device near") ||
			!ExpectNear(projectDepth(projection, range.nearDistance), 0.0f, 1.0e-5f,
				"Extracted Vulkan near must project to device depth zero") ||
			!ExpectNear(projectDepth(projection, range.farDistance), 1.0f, 1.0e-5f,
				"Extracted Vulkan far must project to device depth one"))
		{
			return false;
		}
	}

	const glm::mat4 zeroToOneProjection = glm::perspectiveRH_ZO(
		glm::radians(60.0f), 16.0f / 9.0f, 0.25f, 500.0f);
	const VansDeviceDepthRange zeroToOneRange =
		ExtractVulkanDeviceDepthRange(zeroToOneProjection);
	return zeroToOneRange.valid && zeroToOneRange.finiteFar &&
		ExpectNear(zeroToOneRange.nearDistance, 0.25f, 2.5e-6f,
			"RH_ZO projection must preserve the logical near distance") &&
		ExpectNear(projectDepth(zeroToOneProjection, zeroToOneRange.nearDistance),
			0.0f, 1.0e-5f,
			"RH_ZO near must project to device depth zero") &&
		ExpectNear(projectDepth(zeroToOneProjection, zeroToOneRange.farDistance),
			1.0f, 1.0e-5f,
			"RH_ZO far must project to device depth one");
}

bool TestSkyFSRPipelineContract()
{
	using namespace VansGraphics;
	auto& shaderManager = VansShaderManager::Get();
	RegisterEngineShaders();

	const VansShaderEntry* sky = shaderManager.FindShaderEntry("SkyBox");
	const VansShaderEntry* skyMotion = shaderManager.FindShaderEntry("SkyMotionVector");
	const VansShaderEntry* gbuffer = shaderManager.FindShaderEntry("Unlit");
	const VansShaderEntry* terrain = shaderManager.FindShaderEntry("Terrain");
	const auto& pbrPasses = shaderManager.GetMaterialPassMap(VAN_PBR);
	const bool valid =
		sky == nullptr &&
		skyMotion != nullptr && skyMotion->depthTest == VK_TRUE &&
		skyMotion->depthWrite == VK_FALSE &&
		skyMotion->depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL &&
		skyMotion->cullMode == VK_CULL_MODE_NONE &&
		gbuffer != nullptr && gbuffer->colorAttachmentCount == 5 &&
		terrain != nullptr && terrain->colorAttachmentCount == 5 &&
		pbrPasses.find(VansPass::VELOCITY) == pbrPasses.end() &&
		shaderManager.FindShaderEntry("MotionVector") == nullptr &&
		shaderManager.FindShaderEntry("TerrainMotionVector") == nullptr;

	return Expect(valid,
		"GBuffer must own surface velocity while physical-sky velocity remains an independent fullscreen pass");
}

bool TestDrawSubmissionContract()
{
	using namespace VansGraphics;
	if (!Expect(
		sizeof(VansDrawInstanceDataGPU) == 16 &&
		offsetof(VansDrawInstanceDataGPU, transformIndex) == 0 &&
		offsetof(VansDrawInstanceDataGPU, materialIndex) == 4 &&
		offsetof(VansDrawInstanceDataGPU, vertexFeatureMask) == 8 &&
		offsetof(VansDrawInstanceDataGPU, passUser0) == 12,
		"Draw instance GPU record no longer matches the GLSL std430 ABI"))
	{
		return false;
	}

	std::vector<VkDescriptorSet> descriptorsA = {
		reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(1))
	};
	std::vector<VkDescriptorSet> descriptorsB = descriptorsA;
	std::vector<VkDescriptorSet> descriptorsC = {
		reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(2))
	};

	VansDrawPacket first;
	first.pipeline = reinterpret_cast<VkPipeline>(static_cast<std::uintptr_t>(1));
	first.pipelineLayout = reinterpret_cast<VkPipelineLayout>(static_cast<std::uintptr_t>(2));
	first.descriptorSets = descriptorsA;
	first.orderGroup = 7;
	first.geometry.vertexBuffer = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(3));
	first.geometry.indexBuffer = reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(4));
	first.geometry.indexCount = 36;
	first.instanceData.transformIndex = 5;
	first.instanceData.materialIndex = 9;

	VansDrawPacket compatible = first;
	compatible.descriptorSets = descriptorsB;
	compatible.instanceData.transformIndex = 6;
	compatible.instanceData.materialIndex = 10;
	if (!Expect(VansDrawSubmission::AreBatchCompatible(first, compatible),
		"Draw packets with identical state and geometry did not instance together"))
	{
		return false;
	}

	VansDrawPacket descriptorSplit = compatible;
	descriptorSplit.descriptorSets = descriptorsC;
	VansDrawPacket layoutSplit = compatible;
	layoutSplit.pipelineLayout = reinterpret_cast<VkPipelineLayout>(static_cast<std::uintptr_t>(3));
	VansDrawPacket geometrySplit = compatible;
	geometrySplit.geometry.indexCount = 12;
	VansDrawPacket orderSplit = compatible;
	orderSplit.orderGroup = 8;
	return Expect(
		!VansDrawSubmission::AreBatchCompatible(first, descriptorSplit) &&
		!VansDrawSubmission::AreBatchCompatible(first, layoutSplit) &&
		!VansDrawSubmission::AreBatchCompatible(first, geometrySplit) &&
		!VansDrawSubmission::AreBatchCompatible(first, orderSplit),
		"Draw submission merged packets across a descriptor, geometry, or order boundary");
}

bool TestLuaInspectorProjectModuleSearchPathContract()
{
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "ForestLuaInspectorModuleSearchPath";
	std::error_code cleanupError;
	fs::remove_all(root, cleanupError);
	fs::create_directories(root / "Scripts");
	{
		std::ofstream module(root / "Scripts" / "inspector_dependency.lua", std::ios::binary);
		module << "return { defaultSpeed = 3.5 }\n";
		std::ofstream script(root / "Scripts" / "inspector_behavior.lua", std::ios::binary);
		script <<
			"local dependency = require('inspector_dependency')\n"
			"return { Probe = { __fields = { speed = { type = 'float', default = dependency.defaultSpeed } } } }\n";
	}
	const Vans::LuaScriptFieldDefaultsResult result =
		Vans::VansLuaScriptInspectorService::BuildDefaultFieldData(
			root, "Scripts/inspector_behavior.lua", "Probe");
	fs::remove_all(root, cleanupError);
	return Expect(result && result.fields.size() == 1 &&
		result.fields.front().first == "speed" &&
		result.fields.front().second.kind == Vans::VansSerializedValue::Kind::Float &&
		std::abs(result.fields.front().second.floatValue - 3.5) < 1.0e-6,
		"Lua Inspector did not resolve modules from the active project's Scripts directory");
}

bool TestWaterRenderingRefactorContract()
{
	using namespace Vans;
	using namespace VansGraphics;

	const auto& builtIns = VansBuiltInAssetCatalog::Entries();
	const auto detailNormal = std::find_if(
		builtIns.begin(), builtIns.end(), [](const VansBuiltInAssetEntry& entry)
		{
			return entry.runtimeAlias != nullptr &&
				std::string(entry.runtimeAlias) == "waterDetailWaveNormal";
		});
	if (!Expect(detailNormal != builtIns.end() &&
		detailNormal->type == VansAssetType::Texture &&
		std::string(detailNormal->guid) == "6ba76755-170f-4915-8054-54699138937c" &&
		std::string(detailNormal->sourcePath) ==
			"EngineAssets/Textures/Water/DetailWaveTexture.png" &&
		VansBuiltInAssetCatalog::IsReservedRuntimeAlias("waterDetailWaveNormal"),
		"Water detail normal is not a stable built-in texture contract"))
	{
		return false;
	}

	fs::path engineRoot;
	for (fs::path cursor = fs::current_path(); !cursor.empty() && engineRoot.empty();
		cursor = cursor.parent_path())
	{
		for (const fs::path& candidate : { cursor, cursor / "ForestEngine" })
		{
			if (fs::is_regular_file(candidate / detailNormal->sourcePath))
			{
				engineRoot = candidate;
				break;
			}
		}
		if (cursor == cursor.root_path())
			break;
	}
	if (!Expect(!engineRoot.empty(),
		"Water detail normal source asset is missing from EngineAssets"))
	{
		return false;
	}
	TemporaryDirectory artifactDirectory;
	VansAssetDatabase builtInDatabase(
		engineRoot / "EngineAssets", artifactDirectory.path / "Artifacts");
	std::string assetError;
	if (!Expect(builtInDatabase.RegisterOrRefresh(
		engineRoot / detailNormal->sourcePath,
		VansAssetOperationPolicy::ReadOnly(), assetError),
		assetError.empty() ? "Water detail normal could not be indexed" : assetError.c_str()))
	{
		return false;
	}
	const std::optional<VansAssetRecord> detailRecord =
		builtInDatabase.Find(engineRoot / detailNormal->sourcePath);
	if (!Expect(detailRecord && detailRecord->type == VansAssetType::Texture &&
		detailRecord->guid.ToString() == detailNormal->guid,
		"Water detail normal meta does not match its built-in catalog entry"))
	{
		return false;
	}

	const nlohmann::json waterJson = {
		{ "detailNormal", {
			{ "enabled", true },
			{ "decodeMode", "rgReconstructZ" },
			{ "flipGreen", true },
			{ "globalStrength", 0.8 },
			{ "maxSlope", 1.7 },
			{ "mipBias", -0.25 },
			{ "anisotropy", 12.0 },
			{ "layers", nlohmann::json::array({ {
				{ "enabled", true },
				{ "tileSizeMeters", 1.25 },
				{ "direction", nlohmann::json::array({ 0.0, 1.0 }) },
				{ "speedMetersPerSecond", 0.15 },
				{ "phase", 0.4 },
				{ "strength", 0.3 },
				{ "fadeStartMeters", 4.0 },
				{ "fadeEndMeters", 90.0 }
			} }) }
		} },
		{ "effectiveRoughness", {
			{ "mode", "distanceHeuristic" },
			{ "distanceStartMeters", 30.0 },
			{ "distanceEndMeters", 200.0 },
			{ "distanceStrength", 0.12 }
		} },
		{ "colorMip", {
			{ "refractionScatterScale", 0.45 },
			{ "refractionRoughnessScale", 0.2 },
			{ "forwardScatterMipScale", 0.35 },
			{ "backgroundScatterScale", 0.3 },
			{ "lodBias", 0.5 }
		} },
		{ "shadow", {
			{ "enabled", true },
			{ "quality", 1 },
			{ "depthBias", 0.001 },
			{ "normalBias", 0.03 },
			{ "volumeStepStride", 3 }
		} },
		{ "ssr", {
			{ "enabled", true },
			{ "maxDistance", 600.0 },
			{ "maxRoughness", 0.4 },
			{ "roughnessFadeStart", 0.22 },
			{ "colorMipConeScale", 0.55 },
			{ "colorMipBias", 0.2 },
			{ "edgeFadePixels", 18.0 }
		} }
	};
	const VansSceneWaterNodeConfig decoded =
		VansSceneEnvironmentNodeConfigReader::ReadWater(
			DecodeSerializedValueJson(waterJson));
	if (!Expect(decoded.valid && decoded.detailNormal.layers.size() == 1 &&
		decoded.detailNormal.decodeMode == VansSceneWaterNormalDecodeMode::RGReconstructZ &&
		decoded.detailNormal.anisotropy.value_or(0.0f) == 12.0f &&
		decoded.effectiveRoughness.mode ==
			VansSceneWaterEffectiveRoughnessMode::DistanceHeuristic &&
		decoded.colorMip.lodBias.value_or(0.0f) == 0.5f &&
		decoded.shadow.volumeStepStride.value_or(0) == 3 &&
		decoded.ssr.colorMipConeScale.value_or(0.0f) == 0.55f,
		"Current water scene schema did not decode the rendering refactor settings"))
	{
		return false;
	}

	nlohmann::json invalidWater = waterJson;
	invalidWater["detailNormal"]["decodeMode"] = "unsupportedRgb";
	if (!Expect(!VansSceneEnvironmentNodeConfigReader::ReadWater(
		DecodeSerializedValueJson(invalidWater)).valid,
		"Water scene schema accepted an unsupported detail-normal decode mode"))
	{
		return false;
	}

	VansWaterConfig runtimeConfig;
	runtimeConfig.m_DetailNormal.m_Anisotropy = 100.0f;
	runtimeConfig.m_DetailNormal.m_Layers[0].m_Direction = glm::vec2(0.0f);
	runtimeConfig.m_DetailNormal.m_Layers[0].m_FadeStartMeters = 10.0f;
	runtimeConfig.m_DetailNormal.m_Layers[0].m_FadeEndMeters = 1.0f;
	runtimeConfig.m_Shadow.m_Quality = 5;
	runtimeConfig.m_Shadow.m_VolumeStepStride = 0;
	runtimeConfig.m_SSR.m_MaxRoughness = 0.2f;
	runtimeConfig.m_SSR.m_RoughnessFadeStart = 0.8f;
	runtimeConfig.Validate();
	return Expect(runtimeConfig.m_DetailNormal.m_Anisotropy == 16.0f &&
		runtimeConfig.m_DetailNormal.m_Layers[0].m_Direction == glm::vec2(1.0f, 0.0f) &&
		runtimeConfig.m_DetailNormal.m_Layers[0].m_FadeEndMeters > 10.0f &&
		runtimeConfig.m_Shadow.m_Quality == 1 &&
		runtimeConfig.m_Shadow.m_VolumeStepStride == 1 &&
		runtimeConfig.m_SSR.m_RoughnessFadeStart == 0.2f,
		"Water runtime validation did not enforce the current rendering contract");
}

bool TestAtmosphereMathAndDataContract()
{
	using namespace VansGraphics;
	const VkImageUsageFlags sceneColorUsage =
		VansRenderPassManager::GetSceneColorImageUsageFlags();
	const VkImageUsageFlags requiredSceneColorUsage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (!Expect(
		(sceneColorUsage & requiredSceneColorUsage) == requiredSceneColorUsage,
		"SceneColor image usage does not cover the render-graph access contract"))
	{
		return false;
	}
	if (!Expect(
		VansRenderGraphVulkanSyncMapper::MapImageUsage(
			VansRenderResourceUsage::StorageWrite) == VK_IMAGE_USAGE_STORAGE_BIT,
		"RenderGraph StorageWrite does not map to Vulkan storage-image usage"))
	{
		return false;
	}

	VansRenderFramePlan atmosphereFramePlan;
	VansRenderFeatureFrameFlags atmosphereFeatures{};
	atmosphereFeatures.hasForwardOpaquePreAtmosphere = true;
	atmosphereFeatures.hasWater = true;
	VansRenderPassCatalog::BuildCompatibilityFramePlan(
		atmosphereFramePlan, atmosphereFeatures, 1u, false);
	const VansRenderPassNodeDesc* localMedia =
		atmosphereFramePlan.FindPass(VansRenderPassNames::LocalMedia);
	const VansRenderPassNodeDesc* volumetricCloud =
		atmosphereFramePlan.FindPass(VansRenderPassNames::VolumetricCloud);
	const VansRenderPassNodeDesc* atmosphereViewLuts =
		atmosphereFramePlan.FindPass(VansRenderPassNames::AtmosphereViewLuts);
	const VansRenderPassNodeDesc* atmosphereComposite =
		atmosphereFramePlan.FindPass(VansRenderPassNames::AtmosphereComposite);
	const VansRenderPassNodeDesc* waterComposite =
		atmosphereFramePlan.FindPass(VansRenderPassNames::WaterCompositePreAtmosphere);
	const VansRenderPassNodeDesc* transparentPostProcess =
		atmosphereFramePlan.FindPass(VansRenderPassNames::TransparentPostProcess);
	const auto hasAccess = [](const std::vector<VansRenderResourceAccess>& accesses,
		const char* resourceName, VansRenderResourceUsage usage)
	{
		return std::any_of(accesses.begin(), accesses.end(),
			[&](const VansRenderResourceAccess& access)
			{
				return access.name == resourceName && access.usage == usage;
			});
	};
	if (!Expect(
		atmosphereComposite != nullptr &&
		atmosphereViewLuts != nullptr &&
		hasAccess(atmosphereViewLuts->writes,
			"AtmosphereAerialClearScattering", VansRenderResourceUsage::StorageWrite) &&
		localMedia != nullptr &&
		hasAccess(localMedia->writes, "LocalMediaInjection", VansRenderResourceUsage::StorageWrite) &&
		volumetricCloud != nullptr &&
		hasAccess(volumetricCloud->reads, "LocalMediaInjection", VansRenderResourceUsage::SampledRead) &&
		hasAccess(volumetricCloud->reads, "Depth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(volumetricCloud->writes, "CloudRadiance", VansRenderResourceUsage::StorageWrite) &&
		waterComposite != nullptr &&
		hasAccess(waterComposite->reads, "WaterGBuffer", VansRenderResourceUsage::SampledRead) &&
		hasAccess(waterComposite->writes, "RawOpaqueSceneColor", VansRenderResourceUsage::ColorAttachmentWrite) &&
		hasAccess(atmosphereComposite->reads, "RawOpaqueSceneColor", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "AtmosphereAerialScattering", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "AtmosphereAerialClearScattering", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "AtmosphereAerialOpticalDepth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "CloudRadiance", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "CloudDepth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "CloudOpticalDepth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "LocalMediaScattering", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "LocalMediaOpticalDepth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "Depth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->reads, "WaterGBuffer", VansRenderResourceUsage::SampledRead) &&
		hasAccess(atmosphereComposite->writes, "SceneColor", VansRenderResourceUsage::StorageWrite) &&
		transparentPostProcess != nullptr &&
		hasAccess(transparentPostProcess->reads, "AtmosphereAerialScattering", VansRenderResourceUsage::SampledRead) &&
		hasAccess(transparentPostProcess->reads, "AtmosphereAerialOpticalDepth", VansRenderResourceUsage::SampledRead) &&
		hasAccess(transparentPostProcess->reads, "LocalMediaScattering", VansRenderResourceUsage::SampledRead) &&
		hasAccess(transparentPostProcess->reads, "LocalMediaOpticalDepth", VansRenderResourceUsage::SampledRead),
		"AtmosphereComposite render-graph resources do not match its descriptors"))
	{
		return false;
	}
	const auto findPassIndex = [&](const char* passName)
	{
		const auto& passes = atmosphereFramePlan.GetPasses();
		for (std::size_t index = 0; index < passes.size(); ++index)
		{
			if (passes[index].name == passName)
				return index;
		}
		return passes.size();
	};
	if (!Expect(
		findPassIndex(VansRenderPassNames::AtmosphereStaticLuts) <
			findPassIndex(VansRenderPassNames::CloudShadow) &&
		findPassIndex(VansRenderPassNames::CloudShadow) <
			findPassIndex(VansRenderPassNames::AtmosphereViewLuts) &&
		findPassIndex(VansRenderPassNames::AtmosphereViewLuts) <
			findPassIndex(VansRenderPassNames::LocalMedia) &&
		findPassIndex(VansRenderPassNames::LocalMedia) <
			findPassIndex(VansRenderPassNames::RawOpaqueLighting) &&
		findPassIndex(VansRenderPassNames::RawOpaqueLighting) <
			findPassIndex(VansRenderPassNames::ForwardOpaquePreAtmosphere) &&
		findPassIndex(VansRenderPassNames::ForwardOpaquePreAtmosphere) <
			findPassIndex(VansRenderPassNames::VolumetricCloud) &&
		findPassIndex(VansRenderPassNames::VolumetricCloud) <
			findPassIndex(VansRenderPassNames::WaterGBuffer) &&
		findPassIndex(VansRenderPassNames::WaterGBuffer) <
			findPassIndex(VansRenderPassNames::WaterPreCompute) &&
		findPassIndex(VansRenderPassNames::WaterPreCompute) <
			findPassIndex(VansRenderPassNames::WaterCompositePreAtmosphere) &&
		findPassIndex(VansRenderPassNames::WaterCompositePreAtmosphere) <
			findPassIndex(VansRenderPassNames::AtmosphereComposite) &&
		findPassIndex(VansRenderPassNames::AtmosphereComposite) <
			findPassIndex(VansRenderPassNames::TransparentPostProcess),
		"Participating media is not composited between opaque and transparent rendering"))
	{
		return false;
	}

	VansDirectionalLight dayLight{};
	dayLight.m_Direction = glm::vec3(0.0f, 1.0f, 0.0f);
	dayLight.m_Color = glm::vec3(1.0f, 0.8f, 0.6f);
	dayLight.m_Intensity = 20.0f;
	const VansCelestialLightingState dayState =
		VansLightManager::ComputeCelestialLightingState(dayLight);
	VansDirectionalLight nightLight = dayLight;
	nightLight.m_Direction = glm::vec3(0.0f, -1.0f, 0.0f);
	const VansCelestialLightingState nightState =
		VansLightManager::ComputeCelestialLightingState(nightLight);
	if (!Expect(
		glm::dot(dayState.direction, dayLight.m_Direction) > 0.999f &&
		std::abs(dayState.intensity - dayLight.m_Intensity) < 1.0e-6f &&
		std::abs(dayState.skyDiffuseScale - 1.0f) < 1.0e-6f &&
		glm::dot(nightState.direction, -nightLight.m_Direction) > 0.999f &&
		std::abs(nightState.intensity - nightLight.m_Intensity * 0.035f) < 1.0e-6f &&
		nightState.skyDiffuseScale < 0.1f &&
		nightState.skyDiffuseScale < dayState.skyDiffuseScale,
		"Main-light day/night derivation no longer preserves surface and SkyBox IBL scaling"))
	{
		return false;
	}

	const glm::vec2 fallbackDirection = glm::normalize(glm::vec2(4.0f, 1.0f));
	const glm::vec2 detailUvPerMeter = glm::vec2(3.0f / 54.0f, 2.5f / 40.0f);
	const auto fallbackFlowUvOffset = [&](float timeSeconds)
	{
		const float travelledMeters = timeSeconds * 1.5f + 0.17f * 6.0f;
		const glm::vec2 offset = fallbackDirection * travelledMeters * detailUvPerMeter;
		return glm::vec2(
			offset.x - std::floor(offset.x),
			offset.y - std::floor(offset.y));
	};
	const glm::vec2 fallbackAtStart = fallbackFlowUvOffset(0.0f);
	const glm::vec2 fallbackAtOldHalfCycle = fallbackFlowUvOffset(2.0f);
	if (!Expect(
		glm::length(fallbackAtOldHalfCycle - fallbackAtStart) > 0.1f,
		"Uniform local-fog flow still returns to the same noise UV at the old half cycle"))
	{
		return false;
	}

	fs::path atmosphereAssetRoot;
	for (fs::path cursor = fs::current_path();
		!cursor.empty() && atmosphereAssetRoot.empty(); cursor = cursor.parent_path())
	{
		for (const fs::path& candidate : { cursor, cursor / "ForestEngine" })
		{
			if (fs::is_regular_file(candidate /
				"EngineAssets/Shaders/Atmosphere/AtmosphereCommon.glsl") &&
				fs::is_regular_file(candidate /
				"Source/EngineCore/EditorCore/Windows/VansLightWindow.cpp"))
			{
				atmosphereAssetRoot = candidate;
				break;
			}
		}
		if (cursor == cursor.root_path())
			break;
	}
	if (!Expect(!atmosphereAssetRoot.empty(),
		"Atmosphere shader sources are unavailable to the release contract test"))
	{
		return false;
	}
	const auto readText = [&](const fs::path& relativePath)
	{
		std::ifstream stream(atmosphereAssetRoot / relativePath, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>());
	};
	const std::string atmosphereCommon = readText(
		"EngineAssets/Shaders/Atmosphere/AtmosphereCommon.glsl");
	const std::string participatingMediaCommon = readText(
		"EngineAssets/Shaders/Atmosphere/ParticipatingMediaCommon.glsl");
	const std::string nearMediaDepthCommon = readText(
		"EngineAssets/Shaders/Atmosphere/NearMediaDepthCommon.glsl");
	const std::string transmittanceLut = readText(
		"EngineAssets/Shaders/Atmosphere/TransmittanceLUT.comp");
	const std::string multiScatteringLut = readText(
		"EngineAssets/Shaders/Atmosphere/MultiScatteringLUT.comp");
	const std::string skyViewLut = readText(
		"EngineAssets/Shaders/Atmosphere/SkyViewLUT.comp");
	const std::string mediaComposition = readText(
		"EngineAssets/Shaders/Atmosphere/AtmosphereMediaComposition.glsl");
	const std::string atmosphereCompositeShader = readText(
		"EngineAssets/Shaders/Atmosphere/AtmosphereComposite.comp");
	const std::string aerialPerspectiveShader = readText(
		"EngineAssets/Shaders/Atmosphere/AerialPerspective.comp");
	const std::string localMediaIntegrationShader = readText(
		"EngineAssets/Shaders/Atmosphere/LocalMediaIntegration.comp");
	const std::string localMediaInjectionShader = readText(
		"EngineAssets/Shaders/Atmosphere/LocalMediaInjection.comp") + readText(
		"EngineAssets/Shaders/Atmosphere/NearMediaInjectionCommon.glsl");
	const std::string nearMediaLightingShader = readText(
		"EngineAssets/Shaders/Atmosphere/NearMediaLighting.comp");
	const std::string localMediaTemporalResolveShader = readText(
		"EngineAssets/Shaders/Atmosphere/LocalMediaTemporalResolve.comp") + readText(
		"EngineAssets/Shaders/Atmosphere/NearMediaTemporalResolveCommon.glsl");
	const std::string nearMediaTemporalCommon = readText(
		"EngineAssets/Shaders/Atmosphere/NearMediaTemporalCommon.glsl");
	const std::string localFogFieldSamplingShader = readText(
		"EngineAssets/Shaders/Atmosphere/LocalFogFieldSampling.glsl");
	const std::string localFogFlowAdvectionShader = readText(
		"EngineAssets/Shaders/Atmosphere/LocalFogFlowAdvection.glsl");
	const std::string localFogMediumCommonShader = readText(
		"EngineAssets/Shaders/Atmosphere/LocalFogMediumCommon.glsl");
	const std::string nearMediaSystemSource = readText(
		"Source/EngineCore/RenderCore/AtmosphereCore/VansNearMediaSystem.cpp");
	const std::string nearMediaSystemHeader = readText(
		"Source/EngineCore/RenderCore/AtmosphereCore/VansNearMediaSystem.h");
	const std::string shaderRegistrationSource = readText(
		"Source/EngineCore/RenderCore/VansShaderRegistrations.cpp");
	const std::string rendererSource = readText(
		"Source/EngineCore/RenderCore/VulkanCore/VansVKRenderer.cpp");
	const std::string inspectorSource = readText(
		"Source/EngineCore/EditorCore/Windows/VansInspectorWindow.cpp");
	const std::string runtimeProjectionSource = readText(
		"Source/EngineCore/SceneCore/VansSceneRuntimeProjection.cpp");
	const std::string cloudRayMarchShader = readText(
		"EngineAssets/Shaders/Cloud/CloudRayMarch.comp");
	const std::string cloudShadowShader = readText(
		"EngineAssets/Shaders/Cloud/CloudShadow.comp");
	const std::string waterCompositeShader = readText(
		"EngineAssets/Shaders/Water/WaterComposite/water_composite.frag");
	const std::string waterScreenCommon = readText(
		"EngineAssets/Shaders/Water/water_screen_common.glsl");
	const std::string lightWindowSource = readText(
		"Source/EngineCore/EditorCore/Windows/VansLightWindow.cpp");
	const std::string cloudLighting = readText(
		"EngineAssets/Shaders/Common/CloudLightingHP.glsl");
	const std::string giPointLightEntry = readText(
		"EngineAssets/Shaders/GIPointLight/GIPointLight.comp");
	const std::string giPointLight = giPointLightEntry + readText(
		"EngineAssets/Shaders/GIPointLight/GIPointLightMain.glsl");
	const std::string reflectionProbeCapture = readText(
		"EngineAssets/Shaders/ReflectionProbeCapture/ReflectionProbeCapture.frag");
	const std::string reflectionProbeCaptureSky = readText(
		"EngineAssets/Shaders/ReflectionProbeCaptureSky/ReflectionProbeCaptureSky.frag");
	if (!Expect(
		atmosphereCommon.find("EvaluatePhysicalAtmosphereMedium") != std::string::npos &&
		atmosphereCommon.find("EvaluatePhysicalAerialPerspectiveSegmentMedium") != std::string::npos &&
		atmosphereCommon.find("EvaluateNearMediaSegmentMedium") != std::string::npos &&
		atmosphereCommon.find("EvaluateAtmosphericFogMedium") == std::string::npos &&
		atmosphereCommon.find("atmosphericFogTransmittance") == std::string::npos &&
		atmosphereCommon.find("AtmosphereIntegratedSegmentSource") != std::string::npos &&
		transmittanceLut.find("EvaluatePhysicalAtmosphereMedium") != std::string::npos &&
		transmittanceLut.find("EvaluateAtmosphericFogMedium") == std::string::npos &&
		transmittanceLut.find("atmosphericFogTransmittanceOutput") == std::string::npos &&
		multiScatteringLut.find("EvaluatePhysicalAtmosphereMedium") != std::string::npos &&
		multiScatteringLut.find("heightFogScattering") == std::string::npos &&
		multiScatteringLut.find("atmosphericFogRayleighScattering") == std::string::npos &&
		skyViewLut.find("EvaluateAtmosphereClearSkyScatteringSource") != std::string::npos &&
		skyViewLut.find("SampleAtmosphereCloudShadow") == std::string::npos &&
		aerialPerspectiveShader.find("aerialClearScatteringOutput") != std::string::npos &&
		aerialPerspectiveShader.find("EvaluateAtmosphereClearSkyScatteringSource") !=
			std::string::npos,
		"Physical Atmosphere is not the single transmittance/LUT owner"))
	{
		return false;
	}
	if (!Expect(
		mediaComposition.find("#include \"AtmosphereCommon.glsl\"") != std::string::npos &&
		atmosphereCompositeShader.find("environmentLightingCubemap") == std::string::npos &&
		atmosphereCommon.find("environmentLightingCubemap") == std::string::npos,
		"Media consumers are not self-contained or the atmosphere still owns an IBL cubemap"))
	{
		return false;
	}
	if (!Expect(
		atmosphereCompositeShader.find("uniform sampler2D atmosphereSceneDepth") !=
			std::string::npos &&
		atmosphereCompositeShader.find("AtmosphereHasSurfaceDepth(deviceDepth)") !=
			std::string::npos &&
		atmosphereCompositeShader.find("AtmosphereWorldPositionFromDeviceDepth") !=
			std::string::npos &&
		atmosphereCompositeShader.find("uniform sampler2D atmosphereWaterPositionDepth") !=
			std::string::npos &&
		atmosphereCompositeShader.find("WaterSurfaceValid") != std::string::npos &&
		atmosphereCompositeShader.find("BuildParticipatingMediaPath") !=
			std::string::npos &&
		atmosphereCompositeShader.find("result = path.scattering") !=
			std::string::npos &&
		atmosphereCompositeShader.find(
			"uniform sampler3D atmosphereClearAerialScattering") != std::string::npos &&
		atmosphereCompositeShader.find(
			"SampleClearPhysicalAerialScatteringAt") != std::string::npos &&
		atmosphereCompositeShader.find(
			"physicalSky - clearPhysicalScattering") != std::string::npos &&
		atmosphereCompositeShader.find(
			"SampleFarAerialOpticalDepthAt") != std::string::npos &&
		atmosphereCompositeShader.find("baselineSky - baselineAtCursor") ==
			std::string::npos &&
		atmosphereCompositeShader.find("skyTail") == std::string::npos &&
		atmosphereCompositeShader.find("atmosphereGBufferPosition") == std::string::npos &&
		atmosphereCompositeShader.find("positionDepth.w > 0.0") == std::string::npos,
		"Opaque aerial perspective is not driven by authoritative scene depth"))
	{
		return false;
	}
	if (!Expect(
		waterCompositeShader.find("CompositeAtmosphereSurfaceRadiance") ==
			std::string::npos &&
		waterScreenCommon.find("positionDepth.a < WATER_INVALID_DEPTH") ==
			std::string::npos &&
		waterScreenCommon.find("!isnan(positionDepth.a)") != std::string::npos &&
		waterScreenCommon.find("!isinf(positionDepth.a)") != std::string::npos &&
		lightWindowSource.find("ImGui::PushID(\"AtmosphericFog\")") ==
			std::string::npos &&
		lightWindowSource.find("ImGui::PushID(\"PhysicalAtmosphere\")") !=
			std::string::npos &&
		lightWindowSource.find("ImGui::PushID(\"HeightFog\")") !=
			std::string::npos &&
		lightWindowSource.find("\"Maximum Distance\"") != std::string::npos &&
		lightWindowSource.find("\"Visibility At Ground\"") != std::string::npos &&
		lightWindowSource.find("\"Density Falloff Height\"") != std::string::npos &&
		lightWindowSource.find("ImGui::PushID(\"LocalVolumetricFog\")") ==
			std::string::npos &&
		lightWindowSource.find("ImGui::PushID(\"VolumetricClouds\")") !=
			std::string::npos,
		"Water validity still drops far surfaces, owns camera-media composition, or editor IDs are not scoped"))
	{
		return false;
	}
	if (!Expect(
		participatingMediaCommon.find("IntegrateParticipatingMediaStep") != std::string::npos &&
		participatingMediaCommon.find("ComposeParticipatingMediaInterval") != std::string::npos &&
		participatingMediaCommon.find("SampleParticipatingMediaFroxel") != std::string::npos &&
		participatingMediaCommon.find("float viewDepthMeters") != std::string::npos &&
		nearMediaDepthCommon.find("NearMediaRayDistanceFromViewDepth") != std::string::npos &&
		nearMediaDepthCommon.find("NearMediaViewDepthFromRayDistance") != std::string::npos &&
		nearMediaDepthCommon.find("NearMediaSliceViewDepth") != std::string::npos &&
		nearMediaDepthCommon.find("-viewDirection.z") != std::string::npos &&
		localMediaInjectionShader.find("sliceStartViewDepth") != std::string::npos &&
		localMediaInjectionShader.find("NearMediaRayDistanceFromViewDepth") != std::string::npos &&
		nearMediaTemporalCommon.find("previousViewDepth") != std::string::npos &&
		localMediaInjectionShader.find("length(worldPosition - previousCamera)") == std::string::npos &&
		localMediaIntegrationShader.find("sliceStartViewDepth") != std::string::npos &&
		localMediaIntegrationShader.find("sliceStartDistance, sliceEndDistance") != std::string::npos &&
		mediaComposition.find("NearMediaRayDistanceFromViewDepth") != std::string::npos &&
		mediaComposition.find("NearMediaViewDepthFromRayDistance") != std::string::npos &&
		cloudRayMarchShader.find("localViewDepth") != std::string::npos &&
		cloudRayMarchShader.find("localMediaInjection, uv, localViewDepth") != std::string::npos &&
		cloudRayMarchShader.find("storedCloudOnlyOpticalDepth") != std::string::npos &&
		cloudRayMarchShader.find("uniform sampler3D localMediaInjection") != std::string::npos &&
		cloudRayMarchShader.find("uniform sampler2D cloudSceneDepth") != std::string::npos &&
		cloudRayMarchShader.find("CloudSurfaceTerminalDistance") != std::string::npos &&
		cloudRayMarchShader.find("SampleParticipatingMediaFroxel") != std::string::npos &&
		cloudRayMarchShader.find("EvaluateNearMediaSegmentMedium") != std::string::npos &&
		aerialPerspectiveShader.find("EvaluatePhysicalAerialPerspectiveSegmentMedium") != std::string::npos &&
		aerialPerspectiveShader.find("IntegrateParticipatingMediaStep") != std::string::npos &&
		localMediaIntegrationShader.find("EvaluateNearMediaSegmentMedium") != std::string::npos &&
		localMediaIntegrationShader.find("IntegrateParticipatingMediaStep") != std::string::npos &&
		cloudRayMarchShader.find("intervalStart") != std::string::npos &&
		cloudRayMarchShader.find("intervalEnd") != std::string::npos &&
		atmosphereCompositeShader.find("ReconstructCloudInterval") != std::string::npos &&
		mediaComposition.find("ExtractBaseMediaInterval") != std::string::npos &&
		atmosphereCompositeShader.find("ExtractBaseMediaInterval") != std::string::npos &&
		atmosphereCompositeShader.find("cloudOnlyOpticalDepth") != std::string::npos &&
		atmosphereCompositeShader.find("afterCloudScattering") != std::string::npos &&
		atmosphereCompositeShader.find(
			"cloudExit, terminalDistance") != std::string::npos &&
		atmosphereCompositeShader.find(
			"texture(atmosphereCloudDepth, uv)") == std::string::npos,
		"Camera-to-surface/cloud media intervals no longer preserve before/joint/after ordering"))
	{
		return false;
	}
	if (!Expect(
		localMediaInjectionShader.find("readonly buffer LocalFogVolumes") != std::string::npos &&
		localMediaInjectionShader.find("readonly buffer LocalFogTileHeaders") != std::string::npos &&
		localMediaInjectionShader.find("readonly buffer LocalFogTileIndices") != std::string::npos &&
		localMediaInjectionShader.find("localFogTileOverflow") != std::string::npos &&
		localMediaInjectionShader.find("distanceToFaceMeters") != std::string::npos &&
		localMediaInjectionShader.find("IntersectLocalFogSliceSegment") != std::string::npos &&
		localMediaInjectionShader.find("overlapDistance / sliceThickness") != std::string::npos &&
		localMediaInjectionShader.find("NearMediaMaterialAccumulation") !=
			std::string::npos &&
		localMediaInjectionShader.find("AccumulateNearMediaMaterial") !=
			std::string::npos &&
		localMediaInjectionShader.find("nearMediaScatteringExtinction") !=
			std::string::npos &&
		localMediaInjectionShader.find("SampleDirectionalGeometryShadow") ==
			std::string::npos &&
		localMediaInjectionShader.find("EvaluatePunctualLighting") ==
			std::string::npos &&
		localMediaInjectionShader.find(
			"volume.worldToLocal * vec4(worldPosition, 1.0)") == std::string::npos &&
		localMediaInjectionShader.find("EvaluateLocalFogDensityFactor") != std::string::npos &&
		localMediaInjectionShader.find("NearMediaSubFroxelJitter") != std::string::npos &&
		localMediaInjectionShader.find("localFogTileGridAndLimits.w > 0.5") != std::string::npos &&
		localMediaInjectionShader.find("mix(overlapStartDistance") != std::string::npos &&
		localMediaInjectionShader.find("localMediaHistory") == std::string::npos &&
		localMediaInjectionShader.find("densityDelta") == std::string::npos &&
		localMediaInjectionShader.find("motionConfidence") == std::string::npos &&
		localMediaInjectionShader.find(
			"SampleDirectionalShadow(sampleWorldPosition, densityFactor)") == std::string::npos &&
		nearMediaLightingShader.find("NearMediaLightTransmittance") !=
			std::string::npos &&
		nearMediaLightingShader.find("SampleCascadeShadow") != std::string::npos &&
		nearMediaLightingShader.find("EvaluatePunctualLighting") != std::string::npos &&
		nearMediaLightingShader.find("geometryVisibility *") != std::string::npos &&
		nearMediaLightingShader.find("mediumVisibility * cloudVisibility") !=
			std::string::npos &&
		nearMediaLightingShader.find("uParticles") == std::string::npos &&
		nearMediaLightingShader.find("uLocalFogVolumes") == std::string::npos &&
		nearMediaTemporalCommon.find("NEAR_MEDIA_SUB_FROXEL_SEQUENCE[16]") != std::string::npos &&
		nearMediaTemporalCommon.find("NearMediaHistoryUVW") != std::string::npos &&
		localMediaTemporalResolveShader.find("readonly image3D localMediaCurrent") != std::string::npos &&
		localMediaTemporalResolveShader.find("writeonly image3D localMediaResolved") != std::string::npos &&
		localMediaTemporalResolveShader.find(
			"VANS_NEAR_MEDIA_WITH_PARTICLE_REACTIVE_HISTORY 0") != std::string::npos &&
		localMediaTemporalResolveShader.find("NearMediaSliceViewDepth(float(voxel.z) + 0.5") != std::string::npos &&
		localMediaTemporalResolveShader.find("sumSquared / neighborhoodSampleCount") != std::string::npos &&
		localMediaTemporalResolveShader.find("clippedHistory = clamp(history") != std::string::npos &&
		localMediaTemporalResolveShader.find("responsiveHistoryWeight") != std::string::npos &&
		localMediaTemporalResolveShader.find("densityDelta") == std::string::npos &&
		localMediaIntegrationShader.find("binding = 12") != std::string::npos &&
		localFogFieldSamplingShader.find("nonuniformEXT(descriptorIndex)") != std::string::npos &&
		localFogFieldSamplingShader.find("ComputeLocalFogFieldLod") != std::string::npos &&
		localFogFlowAdvectionShader.find("BuildFlowAdvectionPhases") != std::string::npos &&
		localFogFlowAdvectionShader.find("BuildUniformFlowUvOffset") != std::string::npos &&
		localFogFlowAdvectionShader.find("OffsetSecondFlowLayerUv") != std::string::npos &&
		localFogFlowAdvectionShader.find("ApplyFlowDecodeDeadZone") != std::string::npos &&
		localFogFlowAdvectionShader.find("ClampLocalFogFlowLength") != std::string::npos &&
		localFogMediumCommonShader.find("LOCAL_FOG_SHAPE_ENABLED") != std::string::npos &&
		localFogMediumCommonShader.find("LOCAL_FOG_DETAIL_ENABLED") != std::string::npos &&
		localFogMediumCommonShader.find("LOCAL_FOG_FLOW_ENABLED") != std::string::npos &&
		localFogMediumCommonShader.find("FrameTime") != std::string::npos &&
		localFogMediumCommonShader.find("detailUv - flowUvOffset") != std::string::npos &&
		localFogMediumCommonShader.find(
			"rawDetail = mix(detail1, detail0, phases.blend)") != std::string::npos &&
		nearMediaLightingShader.find("AtmosphereCloudShadowStrength()") != std::string::npos &&
		nearMediaLightingShader.find("SampleAtmosphereMultipleScattering(") != std::string::npos &&
		nearMediaSystemSource.find("MaxLocalFogVolumes") != std::string::npos &&
		nearMediaSystemSource.find("RefreshLocalFogRegistry") != std::string::npos &&
		nearMediaSystemSource.find("GetSceneObjectCollectionGeneration") != std::string::npos &&
		nearMediaSystemSource.find("m_LocalFogTileHeadersBuffer") != std::string::npos &&
		nearMediaSystemSource.find("FindComputeShader(\"NearMediaLighting\")") != std::string::npos &&
		nearMediaSystemSource.find("FindComputeShader(\"LocalMediaTemporalResolve\")") != std::string::npos &&
		shaderRegistrationSource.find(
			"RegisterComputeShaderFile(\"NearMediaLighting\"") != std::string::npos &&
		nearMediaSystemSource.find("m_RawInjectionInitialized") != std::string::npos &&
		nearMediaSystemSource.find("m_InjectionInitialized[target]") != std::string::npos &&
		nearMediaSystemSource.find("m_Injection[target], m_HistoryValid") == std::string::npos &&
		nearMediaSystemSource.find("DispatchCompute(*m_InjectionShader") <
			nearMediaSystemSource.find("DispatchCompute(*m_LightingShader") &&
		nearMediaSystemSource.find("DispatchCompute(*m_LightingShader") <
			nearMediaSystemSource.find("DispatchCompute(*m_TemporalResolveShader") &&
		nearMediaSystemSource.find("DispatchCompute(*m_TemporalResolveShader") <
			nearMediaSystemSource.find("DispatchCompute(*m_IntegrationShader") &&
		nearMediaSystemSource.find("m_HistoryValid = false") != std::string::npos &&
		nearMediaSystemSource.find("effectiveFarDistanceMeters") != std::string::npos &&
		nearMediaSystemSource.find("environment.heightFog.maximumDistanceMeters") != std::string::npos &&
		nearMediaSystemSource.find("m_PreviousEffectiveFarDistanceMeters") != std::string::npos &&
		nearMediaSystemSource.find("VansLocalFogFieldResourceTable::RegisterScalar") != std::string::npos &&
		nearMediaSystemSource.find("VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK") != std::string::npos &&
		nearMediaSystemHeader.find("sizeof(VansNearMediaParamsGPU) == 64") != std::string::npos &&
		nearMediaSystemHeader.find("sizeof(VansLocalFogVolumeGPU) == 272") != std::string::npos &&
		nearMediaSystemHeader.find("void InvalidateHistory()") != std::string::npos &&
		rendererSource.find("nearMedia->InvalidateHistory()") != std::string::npos &&
		runtimeProjectionSource.find("FindComponent(entity, \"LocalVolumetricFog\")") != std::string::npos &&
		inspectorSource.find("if (type == \"LocalVolumetricFog\")") != std::string::npos &&
		inspectorSource.find(
			"Fallback direction continuously advects Detail Noise") != std::string::npos &&
		inspectorSource.find("\"AudioReverbZone\", \"LocalVolumetricFog\"") != std::string::npos,
		"Entity Local Volumetric Fog injection, lighting, or authoring contract regressed"))
	{
		return false;
	}
	if (!Expect(
		cloudShadowShader.find("shadowExtinctionScale") == std::string::npos &&
		cloudShadowShader.find("max(uCloud.sigmaTRef, 0.0)") != std::string::npos &&
		cloudShadowShader.find("max(uCloud.lightAbsorption, 0.0)") != std::string::npos &&
		atmosphereCommon.find("aerialPerspectiveAndVolumetricLighting.y") != std::string::npos &&
		atmosphereCommon.find(
			"EvaluateAtmosphereScatteringSourceWithPrimaryCloudTransmittance") !=
			std::string::npos &&
		cloudLighting.find("out float lightOpticalDepth") != std::string::npos &&
		cloudRayMarchShader.find("cloudSunOpticalDepth") != std::string::npos &&
		cloudRayMarchShader.find(
			"exp(-min(cloudSunOpticalDepth, 80.0))") != std::string::npos &&
		cloudRayMarchShader.find("terminatedByCloudOpacity") != std::string::npos &&
		cloudRayMarchShader.find("missingCloudOpticalDepth") != std::string::npos &&
		cloudRayMarchShader.find("OPAQUE_OPTICAL_DEPTH") != std::string::npos &&
		cloudRayMarchShader.find(
			"cloudTransmittance < TRANSMIT_CUTOFF") != std::string::npos &&
		cloudRayMarchShader.find(
			"EvaluateAtmosphereScatteringSourceWithPrimaryCloudTransmittance") !=
			std::string::npos &&
		aerialPerspectiveShader.find("uAtmosphere.aerialPerspectiveParameters.y") == std::string::npos &&
		atmosphereCompositeShader.find("uAtmosphere.aerialPerspectiveParameters.y") == std::string::npos &&
		lightWindowSource.find("Optical Depth Scale") == std::string::npos &&
		lightWindowSource.find("Main Light Scattering") != std::string::npos &&
		lightWindowSource.find("Surface Strength") == std::string::npos &&
		lightWindowSource.find("Shadow Extinction Scale") == std::string::npos,
		"Cloud shadow reintroduced a second optical-depth dial or a dead surface control"))
	{
		return false;
	}
	if (!Expect(
		atmosphereCommon.find("AtmosphereHeightFogDirectionalOpticalDepth") == std::string::npos &&
		atmosphereCommon.find("heightFogInScatteringColor") == std::string::npos &&
		atmosphereCommon.find("EvaluateHeightFogSegmentMedium") != std::string::npos &&
		atmosphereCommon.find("heightFogDistanceParameters") != std::string::npos &&
		atmosphereCommon.find("cameraWorldMetersAndMaxDistance.y") != std::string::npos &&
		atmosphereCommon.find("preparedMainLightColorAndIntensity.w") != std::string::npos &&
		atmosphereCommon.find("uAtmosphere.heightFogEmissiveAndSkyScale.xyz") != std::string::npos &&
		atmosphereCommon.find("uAtmosphere.heightFogAlbedoAndAnisotropy.w") != std::string::npos &&
		atmosphereCommon.find("AtmosphereHeightFogPhase(cosineTheta") != std::string::npos &&
		atmosphereCommon.find("AtmospherePreparedLightCelestialIrradiance") != std::string::npos &&
		atmosphereCommon.find("atmosphericFogCloudTransmittance") == std::string::npos &&
		atmosphereCommon.find("atmosphericFogRayleighScattering") == std::string::npos &&
		atmosphereCommon.find("AtmosphereAtmosphericFogMiePhase") == std::string::npos &&
		atmosphereCommon.find("AtmosphereExponentialMediumDirectionalOpticalDepth") == std::string::npos &&
		atmosphereCommon.find("SampleAtmosphereMultipleScattering(") != std::string::npos &&
		atmosphereCommon.find("heightFogPhaseShaping") == std::string::npos &&
		atmosphereCommon.find("heightFogEnvironmentLightingColor") == std::string::npos &&
		atmosphereCommon.find("softPeak") == std::string::npos,
		"Near-ground Height Fog or single Physical Atmosphere lighting contract regressed"))
	{
		return false;
	}
	if (!Expect(
		cloudLighting.find("preparedMainLightDirectionAndValidity") != std::string::npos &&
		cloudLighting.find("preparedMainLightColorAndIntensity") != std::string::npos &&
		cloudLighting.find("SampleAtmosphereTransmittance") == std::string::npos,
		"Cloud lighting no longer preserves the prepared day/night main-light contract"))
	{
		return false;
	}
	if (!Expect(
		giPointLightEntry.find("#include \"GIPointLightMain.glsl\"") != std::string::npos &&
		giPointLight.find("layout(set = 1, binding = 4) uniform samplerCube environmentMap") !=
			std::string::npos &&
		giPointLight.find("SampleSkyRadiance(environmentMap, rayDirection)") !=
			std::string::npos &&
		giPointLight.find("EvaluateGIMissSkyRadiance") ==
			std::string::npos &&
		giPointLight.find("GetSkyDiffuseCubeIntensity") == std::string::npos &&
		reflectionProbeCapture.find(
			"layout(set = 1, binding = 3) uniform samplerCube skyDiffuseEnvironment") !=
			std::string::npos &&
		reflectionProbeCapture.find(
			"#define GI_SAMPLE_SKY(N) max(SampleSkyDiffuseIrradiance(skyDiffuseEnvironment, N) / PI") != std::string::npos &&
		reflectionProbeCapture.find("GI_BlendRegionLighting(worldPosition, normal, 1.0)") != std::string::npos &&
		reflectionProbeCaptureSky.find(
			"SampleSkySpecularCube(PreConvSpecularEnvironment, direction, 0.0)") !=
			std::string::npos &&
		reflectionProbeCapture.find("atmosphericFog") == std::string::npos &&
		reflectionProbeCaptureSky.find("atmosphericFog") == std::string::npos,
		"Unified static sky source routing or unchanged atmosphere boundary regressed"))
	{
		return false;
	}

	const auto nearEqual = [](const glm::dvec3& left, const glm::dvec3& right, double tolerance)
	{
		return glm::all(glm::lessThanEqual(glm::abs(left - right), glm::dvec3(tolerance)));
	};

	const VansAtmosphereInterval a{ glm::dvec3(0.12, 0.08, 0.03), glm::dvec3(0.1, 0.2, 0.3) };
	const VansAtmosphereInterval b{ glm::dvec3(0.06, 0.07, 0.09), glm::dvec3(0.4, 0.2, 0.1) };
	const VansAtmosphereInterval c{ glm::dvec3(0.03, 0.02, 0.01), glm::dvec3(0.2, 0.5, 0.7) };
	const VansAtmosphereInterval left = ComposeAtmosphereIntervals(
		ComposeAtmosphereIntervals(a, b), c);
	const VansAtmosphereInterval right = ComposeAtmosphereIntervals(
		a, ComposeAtmosphereIntervals(b, c));
	if (!Expect(nearEqual(left.scattering, right.scattering, 1.0e-12) &&
		nearEqual(left.opticalDepth, right.opticalDepth, 1.0e-12),
		"Atmosphere interval composition is not associative"))
		return false;

	// SkyView is clear-sky radiance. Recovering its endpoint with a shadowed
	// Aerial prefix algebraically restores the missing Mie halo. The endpoint
	// baseline must therefore use the independently integrated clear prefix.
	const glm::dvec3 clearPhysicalScattering(4.0);
	const glm::dvec3 shadowedPhysicalScattering(1.0);
	const glm::dvec3 physicalTransmittance(0.5);
	const glm::dvec3 clearSkyEndpoint(10.0);
	const glm::dvec3 physicalSky = clearPhysicalScattering +
		physicalTransmittance * clearSkyEndpoint;
	const glm::dvec3 resolvedClearEndpoint =
		(physicalSky - clearPhysicalScattering) / physicalTransmittance;
	const glm::dvec3 cloudOccludedSky = shadowedPhysicalScattering +
		physicalTransmittance * resolvedClearEndpoint;
	const glm::dvec3 legacyResolvedEndpoint =
		(physicalSky - shadowedPhysicalScattering) / physicalTransmittance;
	const glm::dvec3 legacyCloudSky = shadowedPhysicalScattering +
		physicalTransmittance * legacyResolvedEndpoint;
	if (!Expect(
		nearEqual(cloudOccludedSky, glm::dvec3(6.0), 1.0e-12) &&
		nearEqual(legacyCloudSky, physicalSky, 1.0e-12) &&
		glm::all(glm::lessThan(cloudOccludedSky, legacyCloudSky)),
		"Sky endpoint extraction cancels cloud-occluded Physical Atmosphere scattering"))
		return false;

	const double highDynamicRangeSkyRadiance = 100.0;
	const double earlyExitResidual = highDynamicRangeSkyRadiance * 0.01;
	const double opaqueClosedResidual = highDynamicRangeSkyRadiance *
		std::exp(-13.815510558);
	if (!Expect(
		earlyExitResidual >= 1.0 && opaqueClosedResidual <= 1.0e-4,
		"Opaque cloud early termination still leaks high-dynamic-range Physical Atmosphere radiance"))
		return false;

	Vans::VansSceneEnvironmentSettingsConfig environment;
	environment.planet.centerWorldMeters = { 0.0, -6340000.0, 0.0 };
	environment.planet.bottomRadiusMeters = 6340000.0;
	environment.physicalAtmosphere.enabled = true;
	environment.heightFog.enabled = false;
	const glm::dvec3 horizontalDirection(1.0, 0.0, 0.0);
	const VansAtmosphereMediumSample physicalReference =
		EvaluatePhysicalAerialPerspectiveSegmentMedium(
			environment, glm::dvec3(0.0, 1000.0, 0.0),
			horizontalDirection, 0.0, 1.0);
	for (float& coefficient :
		environment.physicalAtmosphere.rayleigh.scatteringPerMeterAtGround)
		coefficient *= 2.0f;
	const VansAtmosphereMediumSample doubledRayleigh =
		EvaluatePhysicalAerialPerspectiveSegmentMedium(
			environment, glm::dvec3(0.0, 1000.0, 0.0),
			horizontalDirection, 0.0, 1.0);
	if (!Expect(
		glm::all(glm::greaterThan(doubledRayleigh.scatteringPerMeter,
			physicalReference.scatteringPerMeter)) &&
		glm::all(glm::greaterThan(doubledRayleigh.extinctionPerMeter,
			physicalReference.extinctionPerMeter)),
		"Physical Atmosphere ground coefficients no longer control Far AP"))
		return false;

	environment.physicalAtmosphere.enabled = false;
	environment.heightFog.enabled = true;
	environment.heightFog.groundHeightWorldMeters = 1000.0f;
	environment.heightFog.visibilityAtGroundMeters = 500.0f;
	environment.heightFog.densityFalloffHeightMeters = 500.0f;
	environment.heightFog.startDistanceMeters = 0.0f;
	environment.heightFog.nearFadeDistanceMeters = 0.0f;
	environment.heightFog.maximumDistanceMeters = 2000.0f;
	environment.heightFog.farFadeDistanceMeters = 0.0f;
	environment.heightFog.singleScatteringAlbedo = { 0.5f, 0.75f, 1.0f };
	const VansAtmosphereMediumSample farWithoutPhysical =
		EvaluatePhysicalAerialPerspectiveSegmentMedium(
			environment, glm::dvec3(0.0, 1000.0, 0.0),
			horizontalDirection, 0.0, 1000.0);
	const VansAtmosphereMediumSample nearAtGround =
		EvaluateNearMediaSegmentMedium(environment,
			glm::dvec3(0.0, 1000.0, 0.0), horizontalDirection, 0.0, 1000.0);
	const VansAtmosphereMediumSample nearAboveOneFalloff =
		EvaluateNearMediaSegmentMedium(environment,
			glm::dvec3(0.0, 1500.0, 0.0), horizontalDirection, 0.0, 1000.0);
	const VansAtmosphereMediumSample nearBelowGround =
		EvaluateNearMediaSegmentMedium(environment,
			glm::dvec3(0.0, 500.0, 0.0), horizontalDirection, 0.0, 1000.0);
	const VansAtmosphereMediumSample nearPastMaximum =
		EvaluateNearMediaSegmentMedium(environment,
			glm::dvec3(0.0, 1000.0, 0.0), horizontalDirection, 2001.0, 2101.0);
	if (!Expect(
		nearEqual(farWithoutPhysical.extinctionPerMeter, glm::dvec3(0.0), 1.0e-15) &&
		std::abs(nearAtGround.extinctionPerMeter.x - 0.002) < 1.0e-9 &&
		std::abs(nearAboveOneFalloff.extinctionPerMeter.x -
			0.002 / std::exp(1.0)) < 1.0e-9 &&
		std::abs(nearBelowGround.extinctionPerMeter.x - 0.002) < 1.0e-9 &&
		nearEqual(nearPastMaximum.extinctionPerMeter, glm::dvec3(0.0), 1.0e-15) &&
		nearEqual(nearAtGround.scatteringPerMeter,
			glm::dvec3(0.001, 0.0015, 0.002), 1.0e-9),
		"NearMedia does not isolate and integrate near-ground Height Fog correctly"))
		return false;

	environment.heightFog.startDistanceMeters = 100.0f;
	environment.heightFog.nearFadeDistanceMeters = 100.0f;
	environment.heightFog.maximumDistanceMeters = 800.0f;
	environment.heightFog.farFadeDistanceMeters = 100.0f;
	if (!Expect(
		std::abs(EvaluateHeightFogDistanceWeight(environment.heightFog, 100.0)) < 1.0e-12 &&
		std::abs(EvaluateHeightFogDistanceWeight(environment.heightFog, 150.0) - 0.5) < 1.0e-12 &&
		std::abs(EvaluateHeightFogDistanceWeight(environment.heightFog, 200.0) - 1.0) < 1.0e-12 &&
		std::abs(EvaluateHeightFogDistanceWeight(environment.heightFog, 750.0) - 0.5) < 1.0e-12 &&
		std::abs(EvaluateHeightFogDistanceWeight(environment.heightFog, 800.0)) < 1.0e-12,
		"Height Fog near/far distance fades are not smooth and bounded"))
		return false;

	const VansAtmosphereInterval cumulative = ComposeAtmosphereIntervals(a, b);
	const VansAtmosphereInterval extracted = ExtractAtmosphereInterval(a, cumulative);
	const VansAtmosphereInterval restored = ComposeAtmosphereIntervals(a, extracted);
	if (!Expect(nearEqual(extracted.scattering, b.scattering, 1.0e-12) &&
		nearEqual(extracted.opticalDepth, b.opticalDepth, 1.0e-12) &&
		nearEqual(restored.scattering, cumulative.scattering, 1.0e-12),
		"Atmosphere cumulative interval extraction did not reconstruct the original interval"))
		return false;
	const VansAtmosphereInterval neutral{};
	if (!Expect(nearEqual(CompositeAtmosphereInterval(glm::dvec3(0.7), neutral),
		glm::dvec3(0.7), 1.0e-12),
		"A disabled atmosphere is not an identity transform"))
		return false;
	const glm::dvec3 absorbed = CompositeAtmosphereInterval(
		glm::dvec3(1.0), { glm::dvec3(0.0), glm::dvec3(1000.0) });
	if (!Expect(std::isfinite(absorbed.x) && std::isfinite(absorbed.y) &&
		std::isfinite(absorbed.z) &&
		glm::all(glm::lessThanEqual(absorbed, glm::dvec3(1.0e-12))),
		"Large RGB optical depth produced light or a non-finite result"))
		return false;

	const auto tangent = IntersectRaySphere(glm::dvec3(1.0, 0.0, -2.0),
		glm::dvec3(0.0, 0.0, 1.0), glm::dvec3(0.0), 1.0);
	const auto outside = IntersectRaySphere(glm::dvec3(2.0, 0.0, -2.0),
		glm::dvec3(0.0, 0.0, 1.0), glm::dvec3(0.0), 1.0);
	const auto inside = IntersectRaySphere(glm::dvec3(0.0),
		glm::dvec3(0.0, 0.0, 1.0), glm::dvec3(0.0), 1.0);
	if (!Expect(tangent && std::abs(tangent->entryMeters - 2.0) < 1.0e-12 &&
		std::abs(tangent->exitMeters - 2.0) < 1.0e-12 && !outside &&
		inside && inside->entryMeters == 0.0 &&
		std::abs(inside->exitMeters - 1.0) < 1.0e-12,
		"Atmosphere ray-sphere intersection failed tangent/outside/inside coverage"))
		return false;

	for (int slice = 0; slice <= 64; ++slice)
	{
		const double normalized = static_cast<double>(slice) / 64.0;
		const double distance =
			EncodeAerialPerspectiveSliceDistance(normalized, 100000.0);
		const double decoded =
			DecodeAerialPerspectiveSliceDistance(distance, 100000.0);
		if (!Expect(std::abs(decoded - normalized) < 1.0e-12,
			"Aerial perspective slice distance encode/decode is not invertible"))
			return false;
	}

	using Value = Vans::VansSerializedValue;
	Vans::VansSceneRenderSettingsConfig decodedSettings;
	std::string error;
	if (!Expect(!Vans::VansSceneRenderSettingsConfigReader::Read(
		Value::Object({ { "heightFog", Value::Object({}) } }),
		decodedSettings, error), "Legacy top-level fog contract was accepted"))
		return false;
	Value validEnvironment = BuildValidEnvironmentSettingsForTest();
	error.clear();
	if (!Expect(Vans::VansSceneRenderSettingsConfigReader::Read(
		Value::Object({ { "environment", validEnvironment } }),
		decodedSettings, error),
		("Current atmosphere schema was rejected: " + error).c_str()))
		return false;
	if (!Expect(
		decodedSettings.environment.skyLighting.intensity == 1.0f &&
        decodedSettings.environment.physicalAtmosphere.enabled &&
		std::abs(decodedSettings.environment.physicalAtmosphere
			.aerialPerspective.distanceScale - 1.0f) < 1.0e-6f &&
		std::abs(decodedSettings.environment.heightFog.visibilityAtGroundMeters -
			500.0f) < 1.0e-6f &&
		std::abs(decodedSettings.environment.heightFog.densityFalloffHeightMeters -
			120.0f) < 1.0e-6f &&
		std::abs(decodedSettings.environment.volumetricClouds
			.shadow.atmosphereStrength - 0.75f) < 1.0e-6f,
		"Current atmosphere fields were not decoded"))
		return false;

    for (double intensity : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        Value invalid = BuildValidEnvironmentSettingsForTest();
        Vans::SetSerializedObjectField(*Vans::FindObjectField(invalid, "skyLighting"), "intensity", Value::Float(intensity));
        if (!Expect(!Vans::VansSceneRenderSettingsConfigReader::Read(Value::Object({{"environment", invalid}}), decodedSettings, error),
            "Invalid sky source intensity was accepted")) return false;
    }

	Value legacyEnvironment = BuildValidEnvironmentSettingsForTest();
	for (auto& field : legacyEnvironment.objectFields)
		if (field.first == "physicalAtmosphere")
			field.first = "physicalSky";
	error.clear();
	if (!Expect(!Vans::VansSceneRenderSettingsConfigReader::Read(
		Value::Object({ { "environment", legacyEnvironment } }),
		decodedSettings, error),
		"Removed physicalSky schema was accepted through a compatibility path"))
		return false;

	Value invalidHeightFogEnvironment = BuildValidEnvironmentSettingsForTest();
	auto* invalidHeightFog =
		Vans::FindObjectField(invalidHeightFogEnvironment, "heightFog");
	Vans::SetSerializedObjectField(*invalidHeightFog,
		"visibilityAtGroundMeters", Value::Float(0.0));
	error.clear();
	if (!Expect(!Vans::VansSceneRenderSettingsConfigReader::Read(
		Value::Object({ { "environment", invalidHeightFogEnvironment } }),
		decodedSettings, error), "Non-positive Height Fog visibility was accepted"))
		return false;

	Value invalidShadowEnvironment = BuildValidEnvironmentSettingsForTest();
	auto* invalidClouds =
		Vans::FindObjectField(invalidShadowEnvironment, "volumetricClouds");
	auto* invalidShadow = Vans::FindObjectField(*invalidClouds, "shadow");
	Vans::SetSerializedObjectField(*invalidShadow,
		"atmosphereStrength", Value::Float(1.01));
	error.clear();
	if (!Expect(!Vans::VansSceneRenderSettingsConfigReader::Read(
		Value::Object({ { "environment", invalidShadowEnvironment } }),
		decodedSettings, error),
		"Out-of-range cloud-shadow irradiance strength was accepted"))
		return false;

	Value invalidEnvironment = BuildValidEnvironmentSettingsForTest();
	auto* clouds = Vans::FindObjectField(invalidEnvironment, "volumetricClouds");
	Vans::SetSerializedObjectField(*clouds, "cloudMaxHeight", Value::Float(100.0));
	Vans::SetSerializedObjectField(*clouds, "cloudMinHeight", Value::Float(200.0));
	error.clear();
	if (!Expect(!Vans::VansSceneRenderSettingsConfigReader::Read(
		Value::Object({ { "environment", invalidEnvironment } }),
		decodedSettings, error),
		"Invalid cloud layer bounds were accepted by strict scene validation"))
	{
		return false;
	}

	Vans::VansProjectRenderSettingsData qualitySettings;
	qualitySettings.atmosphereQualitySettings.farAerialTileSize = 8u;
	qualitySettings.nearMediaQualitySettings.farDistanceMeters = 2000.0f;
	qualitySettings.nearMediaQualitySettings.historyWeight = 0.85f;
	qualitySettings.nearMediaQualitySettings.lightTransmittanceSamples = 16u;
	qualitySettings.nearMediaQualitySettings.lightTransmittanceMaxDistanceMeters = 24.0f;
	qualitySettings.cloudShadowQualitySettings.clipmapCrossFadeFraction = 0.12f;
	const nlohmann::json encodedQuality =
		Vans::VansProjectSettingsJsonCodec::EncodeRenderSettings(qualitySettings);
	if (!Expect(
		encodedQuality["atmosphereQuality"].contains("farAerialTileSize") &&
		!encodedQuality["atmosphereQuality"].contains("aerialPerspectiveTileSize") &&
		!encodedQuality["atmosphereQuality"].contains(
			"aerialPerspectiveTemporalReprojection") &&
		encodedQuality.contains("nearMediaQuality") &&
		!encodedQuality.contains("localFogQuality") &&
		encodedQuality["nearMediaQuality"].contains("historyWeight") &&
		encodedQuality["nearMediaQuality"].contains("lightTransmittanceSamples") &&
		encodedQuality["nearMediaQuality"].contains(
			"lightTransmittanceMaxDistanceMeters") &&
		encodedQuality["cloudShadowQuality"].contains(
			"clipmapCrossFadeFraction"),
		"Project quality codec did not emit the current atmosphere schema"))
	{
		return false;
	}
	Vans::VansProjectRenderSettingsData decodedQuality;
	std::vector<std::string> qualityWarnings;
	std::string qualityError;
	if (!Expect(Vans::VansProjectSettingsJsonCodec::DecodeRenderSettings(
		encodedQuality, decodedQuality, qualityWarnings, qualityError) &&
		decodedQuality.atmosphereQualitySettings.farAerialTileSize == 8u &&
		std::abs(decodedQuality.nearMediaQualitySettings.historyWeight - 0.85f) <
			1.0e-6f &&
		decodedQuality.nearMediaQualitySettings.lightTransmittanceSamples == 16u &&
		std::abs(decodedQuality.nearMediaQualitySettings.
			lightTransmittanceMaxDistanceMeters - 24.0f) < 1.0e-6f &&
		std::abs(decodedQuality.cloudShadowQualitySettings.clipmapCrossFadeFraction -
			0.12f) < 1.0e-6f,
		"Project quality current-schema round trip failed"))
	{
		return false;
	}
	return true;
}

bool TestRecentProjectsPruningContract()
{
	TemporaryDirectory temporary;
	Vans::VansScopedIOContext io(Vans::VansIODomain::UserPreference, "RecentProjects.Contract", true);
	const auto file = temporary.path / "RecentProjects.json";
	const auto first = temporary.path / "First";
	const auto second = temporary.path / "Second";
	const auto missingConfig = temporary.path / "MissingConfig";
	const auto directoryConfig = temporary.path / "DirectoryConfig";
	fs::create_directories(first);
	fs::create_directories(second);
	fs::create_directories(missingConfig);
	fs::create_directories(directoryConfig / "ForestProject.json");
	std::ofstream(first / "ForestProject.json") << "{}";
	std::ofstream(second / "ForestProject.json") << "{}";
	std::vector<Vans::RecentProjectEntry> entries;
	for (const auto& path : {first, temporary.path / "Deleted", missingConfig, directoryConfig, second})
	{
		Vans::RecentProjectEntry entry;
		entry.name = path.filename().string();
		entry.path = path.generic_string() + "/";
		entry.lastOpened = "2026-09-18T12:00:00";
		entry.engineVersion = "0.1.0";
		entries.push_back(entry);
	}
	entries.emplace_back(); // 空路径不能误指向当前工作目录。
	std::string error;
	if (!Expect(Vans::VansProjectConfigStorage::SaveRecentProjects(file.string(), entries, 20, error),
		"Could not prepare recent-projects fixture")) return false;
	if (!Expect(Vans::VansProjectConfigStorage::LoadRecentProjects(file.string(), entries, error) &&
		entries.size() == 2 && entries[0].name == "First" && entries[1].name == "Second" &&
		entries[0].lastOpened == "2026-09-18T12:00:00",
		"Recent projects did not prune missing/non-file/empty paths while retaining order and metadata")) return false;
	nlohmann::json saved;
	std::ifstream(file) >> saved;
	if (!Expect(saved["recentProjects"].size() == 2 && saved["maxRecentCount"] == 20,
		"Pruned recent projects were not persisted")) return false;
	const auto writeTime = fs::last_write_time(file);
	if (!Expect(Vans::VansProjectConfigStorage::LoadRecentProjects(file.string(), entries, error) &&
		fs::last_write_time(file) == writeTime, "Unchanged refresh rewrote recent projects")) return false;
	fs::remove(first / "ForestProject.json");
	if (!Expect(Vans::VansProjectConfigStorage::LoadRecentProjects(file.string(), entries, error) &&
		entries.size() == 1 && entries[0].name == "Second", "Deletion after first load was not pruned")) return false;
	// 解析失败时不持久化部分结果，用户文件保持原样。
	const std::string invalid = R"({"recentProjects":[{"name":"valid","path":"missing"},{"path":42}]})";
	std::ofstream(file, std::ios::trunc) << invalid;
	if (!Expect(!Vans::VansProjectConfigStorage::LoadRecentProjects(file.string(), entries, error) && entries.empty(),
		"Malformed recent projects exposed partial records")) return false;
	std::ifstream input(file);
	const std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (!Expect(actual == invalid, "Malformed preference file was overwritten")) return false;
	std::cout << "[RecentProjects] PASS: missing paths, missing/non-file configs, persistent pruning, stable refresh, deletion after load, malformed input preservation\n";
	return true;
}

bool TestProjectSettingsExplicitSaveContract()
{
	TemporaryDirectory temporary;
	const fs::path projectRoot = temporary.path / "Project";
	fs::create_directories(projectRoot / "ProjectSettings");

	Vans::VansProjectConfig config;
	config.SetDefaults("ProjectDocumentContract");
	if (!Expect(config.SaveToFile((projectRoot / "ForestProject.json").string()),
		"Could not create the project config fixture"))
		return false;

	Vans::VansProjectSettings settings;
	settings.SetDefaults();
	std::string error;
	if (!Expect(
		Vans::VansProjectSettingsStorage::SaveRenderSettings(
			(projectRoot / config.renderSettings).string(),
			settings.BuildRenderSettingsData(), error) &&
		Vans::VansProjectSettingsStorage::SavePhysicsSettings(
			(projectRoot / config.physicsSettings).string(),
			settings.BuildPhysicsSettingsData(), error) &&
		Vans::VansProjectSettingsStorage::SaveNavigationSettings(
			(projectRoot / config.navigationSettings).string(),
			settings.GetNavigationSettings(), error),
		"Could not create the project settings fixtures"))
		return false;
	VansEngine::VansCollisionLayerConfig collisionLayers;
	collisionLayers.ResetToDefaults();
	VansEngine::VansAudioMixConfig audioMix;
	audioMix.displayName = "ProjectDocumentContract Default Audio Mix";
	if (!Expect(
		VansEngine::VansCollisionLayerStorage::SaveAtomic(
			(projectRoot / config.collisionLayerSettings).string(),
			collisionLayers,
			error) &&
		VansEngine::VansAudioMixConfigStorage::SaveAtomic(
			(projectRoot / config.audioSettings).string(), audioMix, error),
		"Could not create the collision layer and audio mix fixtures"))
		return false;

	const fs::path configPath = projectRoot / "ForestProject.json";
	const fs::path renderPath = projectRoot / config.renderSettings;
	const fs::path physicsPath = projectRoot / config.physicsSettings;
	std::string configBefore;
	std::string renderBefore;
	std::string physicsBefore;
	if (!Expect(
		Vans::VansFileStorage::ReadAllBytes(configPath, configBefore, error) &&
		Vans::VansFileStorage::ReadAllBytes(renderPath, renderBefore, error) &&
		Vans::VansFileStorage::ReadAllBytes(physicsPath, physicsBefore, error),
		"Could not capture project document baselines"))
		return false;

	Vans::VansProjectManager& manager = Vans::VansProjectManager::Get();
	manager.CloseProject();
	struct CloseProjectGuard
	{
		Vans::VansProjectManager& manager;
		~CloseProjectGuard() { manager.CloseProject(); }
	} closeGuard{ manager };
	Vans::VansProjectOpenRequest openRequest;
	openRequest.m_ProjectRootPath = projectRoot.string();
	openRequest.m_Options.m_UpdateRecentProjects = false;
	openRequest.m_Options.m_ScanAssets = false;
	if (!Expect(openRequest.m_Options.m_AssetPolicy.meta == Vans::VansAssetMetaPolicy::RequireExisting,
		"Default project-open asset policy is not read-only") ||
		!Expect(manager.OpenProject(openRequest).m_Opened,
			"Could not open the project document fixture") ||
		!Expect(!manager.HasDirtyProjectDocuments(),
			"Freshly loaded project documents are dirty"))
		return false;

	error.clear();
	Vans::VansIOAudit::Reset();
	VansEngine::VansPhysicsTiming editedPhysicsTiming =
		manager.GetProjectSettings().GetPhysicsTiming();
	editedPhysicsTiming.fixedTimeStep = 1.0f / 120.0f;
	editedPhysicsTiming.maximumSubsteps = 6;
	if (!Expect(manager.SetProjectPhysicsTiming(editedPhysicsTiming, error),
		"Could not apply the physics setting to memory") ||
		!Expect(manager.HasDirtyProjectDocuments(),
			"In-memory project settings edit did not become dirty"))
		return false;
	const auto applyIOEvents = Vans::VansIOAudit::Snapshot();
	if (!Expect(std::none_of(
		applyIOEvents.begin(), applyIOEvents.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.domain == Vans::VansIODomain::Authoring &&
				event.operation == Vans::VansIOOperation::StageWrite;
		}), "Applying project settings produced an Authoring write event"))
		return false;

	std::string physicsAfterApply;
	if (!Expect(Vans::VansFileStorage::ReadAllBytes(physicsPath, physicsAfterApply, error) &&
		physicsAfterApply == physicsBefore,
		"Applying project settings wrote the authoring file before explicit save"))
		return false;

	Vans::VansIOAudit::Reset();
	if (!Expect(manager.SaveProjectDocuments(error),
		"Explicit project document save failed") ||
		!Expect(!manager.HasDirtyProjectDocuments() &&
			manager.GetProjectDocumentStateId() == manager.GetProjectDocumentSavedStateId(),
			"Explicit save did not advance the saved project document state"))
		return false;
	const auto saveIOEvents = Vans::VansIOAudit::Snapshot();
	const std::size_t authoringWrites = static_cast<std::size_t>(std::count_if(
		saveIOEvents.begin(), saveIOEvents.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.domain == Vans::VansIODomain::Authoring &&
				event.operation == Vans::VansIOOperation::StageWrite && event.success;
		}));
	if (!Expect(authoringWrites == 1,
		"Explicit physics save did not write exactly one dirty project document"))
		return false;

	std::string configAfterSave;
	std::string renderAfterSave;
	std::string physicsAfterSave;
	if (!Expect(
		Vans::VansFileStorage::ReadAllBytes(configPath, configAfterSave, error) &&
		Vans::VansFileStorage::ReadAllBytes(renderPath, renderAfterSave, error) &&
		Vans::VansFileStorage::ReadAllBytes(physicsPath, physicsAfterSave, error) &&
		configAfterSave == configBefore && renderAfterSave == renderBefore &&
		physicsAfterSave != physicsBefore,
		"Explicit physics save touched unrelated project documents or skipped its dirty file"))
		return false;

	const Vans::VansGAFProjectConfiguration* loadedGAF =
		manager.GetGAFProjectConfiguration();
	if (!Expect(loadedGAF != nullptr,
		"Project manager did not retain the loaded GAF configuration"))
		return false;
	Vans::VansGAFProjectConfiguration gafConfiguration = *loadedGAF;
	gafConfiguration.settings.performance.maximumActiveActionsPerHost = 17;
	Vans::VansIOAudit::Reset();
	if (!Expect(manager.SetGAFProjectConfiguration(gafConfiguration, error),
		"Could not apply the GAF project configuration to memory") ||
		!Expect(manager.HasDirtyProjectDocuments(),
			"In-memory GAF project configuration did not become dirty"))
		return false;
	const auto gafApplyIOEvents = Vans::VansIOAudit::Snapshot();
	if (!Expect(std::none_of(
		gafApplyIOEvents.begin(), gafApplyIOEvents.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.domain == Vans::VansIODomain::Authoring &&
				event.operation == Vans::VansIOOperation::StageWrite;
		}), "Applying the GAF project configuration produced an Authoring write event"))
		return false;
	for (const std::string_view fileName :
		Vans::VansGAFProjectConfiguration::DocumentFileNames())
	{
		if (!Expect(!fs::exists(projectRoot / "ProjectSettings" / fileName),
			"Applying the GAF project configuration created a file before explicit save"))
			return false;
	}

	Vans::VansIOAudit::Reset();
	if (!Expect(manager.SaveProjectDocuments(error),
		"Explicit GAF project configuration save failed") ||
		!Expect(!manager.HasDirtyProjectDocuments(),
			"Explicit GAF save did not clear the project document dirty state"))
		return false;
	const auto gafSaveIOEvents = Vans::VansIOAudit::Snapshot();
	const std::size_t gafAuthoringWrites = static_cast<std::size_t>(std::count_if(
		gafSaveIOEvents.begin(), gafSaveIOEvents.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.domain == Vans::VansIODomain::Authoring &&
				event.operation == Vans::VansIOOperation::StageWrite && event.success;
		}));
	if (!Expect(gafAuthoringWrites ==
		Vans::VansGAFProjectConfiguration::DocumentFileNames().size(),
		"Explicit GAF save did not publish exactly its four project documents"))
		return false;
	Vans::VansGAFProjectConfiguration persistedGAF;
	if (!Expect(Vans::VansGAFProjectConfiguration::Load(
		projectRoot / "ProjectSettings", persistedGAF, error) &&
		persistedGAF.settings.performance.maximumActiveActionsPerHost == 17,
		"Explicit GAF save did not persist the in-memory configuration"))
		return false;

	editedPhysicsTiming.fixedTimeStep = 1.0f / 90.0f;
	if (!Expect(manager.SetProjectPhysicsTiming(editedPhysicsTiming, error),
		"Could not create a second in-memory physics edit"))
		return false;
	Vans::VansProjectPhysicsSettingsData externalPhysics =
		manager.GetProjectSettings().BuildPhysicsSettingsData();
	externalPhysics.timing.fixedTimeStep = 1.0f / 30.0f;
	if (!Expect(Vans::VansProjectSettingsStorage::SavePhysicsSettings(
		physicsPath.string(), externalPhysics, error),
		"Could not create an external project-settings conflict"))
		return false;
	std::string externalPhysicsBytes;
	if (!Expect(Vans::VansFileStorage::ReadAllBytes(
		physicsPath, externalPhysicsBytes, error),
		"Could not capture the external project-settings edit"))
		return false;
	error.clear();
	if (!Expect(!manager.SaveProjectDocuments(error) &&
		manager.HasDirtyProjectDocuments(),
		"Project document save did not reject an external file conflict"))
		return false;
	std::string physicsAfterConflict;
	return Expect(
		Vans::VansFileStorage::ReadAllBytes(physicsPath, physicsAfterConflict, error) &&
		physicsAfterConflict == externalPhysicsBytes,
		"Conflict rejection overwrote the externally modified project document");
}

bool TestGAFParticleDependencyClosure()
{
    TemporaryDirectory temporary;
    const fs::path assets = temporary.path / "Assets";
    fs::create_directories(assets);
    const std::string graphGuid = "9b1d5a11-bdef-4567-8000-000000000001";
    const std::string particleGuid = "9b1d5a11-bdef-4567-8000-000000000002";
    const std::string textureGuid = "9b1d5a11-bdef-4567-8000-000000000003";
    const std::string decoyModelGuid = "9b1d5a11-bdef-4567-8000-000000000004";
    const nlohmann::json graph = {
        {"authoringGuid",decoyModelGuid},
        {"effect",particleGuid}};
    const nlohmann::json particle = {{"name","DependencySmoke"},{"global",nlohmann::json::object()},
        {"emitters",nlohmann::json::array({{{"renderer",{{"type","Ribbon"},{"textureGuid",textureGuid}}}}})}};
    std::ofstream(assets/"Shot.vactiongraph") << graph.dump();
    std::ofstream(assets/"Smoke.particle") << particle.dump();
    std::ofstream(assets/"Smoke.png") << "texture plan fixture";
    std::ofstream(assets/"Decoy.obj") << "o Decoy\n";
    for (const auto& item : std::vector<std::tuple<std::string,std::string,std::string>>{
        {"Shot.vactiongraph",graphGuid,Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::ActionGraph)},
        {"Smoke.particle",particleGuid,Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Particle)},
        {"Smoke.png",textureGuid,Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Texture)},
        {"Decoy.obj",decoyModelGuid,Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Model)}})
    {
        const nlohmann::json meta={{"guid",std::get<1>(item)},{"importer",std::get<2>(item)},
            {"version",1},{"settings",nlohmann::json::object()},{"subAssets",nlohmann::json::object()}};
        std::ofstream(assets/(std::get<0>(item)+".meta")) << meta.dump();
    }
    Vans::VansAssetDatabase database(assets,temporary.path/"Artifacts");
    const auto scan = database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
    if (!Expect(scan.errors.empty() && scan.registered == 4,"Particle closure fixture did not scan")) return false;
    Vans::VansAssetObjectRepository repository;
    const auto bootstrap = Vans::VansAssetObjectBootstrapper::Publish(database.All(),repository);
    if (!Expect(static_cast<bool>(bootstrap),bootstrap.errors.empty() ? "Particle closure bootstrap failed" : bootstrap.errors.front().c_str())) return false;
    Vans::VansAssetGuid parsedGraphGuid;
    Vans::VansAssetGuid parsedParticleGuid;
    Vans::VansAssetObjectSnapshotInfo graphInfo;
    if (!Expect(
        Vans::VansAssetGuid::TryParse(graphGuid, parsedGraphGuid) &&
        Vans::VansAssetGuid::TryParse(particleGuid, parsedParticleGuid) &&
        repository.FindInfo(parsedGraphGuid, graphInfo) &&
        graphInfo.dependencies.size() == 1 &&
        graphInfo.dependencies.front() == parsedParticleGuid,
        "Gameplay memory snapshot treated authoring provenance as an asset dependency"))
        return false;
    Vans::VansSceneData scene;
    auto sceneJson = Vans::VansSceneSchema::SerializeSceneJson(scene);
    sceneJson["nonSchemaAssetToken"] = decoyModelGuid;
    sceneJson["entities"] = nlohmann::json::array({{
        {"id", "9b1d5a11-bdef-4567-8000-000000000010"},
        {"name", "Script dependency owner"},
        {"parent", nullptr},
        {"components", nlohmann::json::array({{
            {"id", "9b1d5a11-bdef-4567-8000-000000000011"},
            {"type", "Script"},
            {"version", 1},
            {"enabled", true},
            {"data", {
                {"path", "Scripts/dependency_fixture.lua"},
                {"language", "lua"},
                {"entry", "DependencyFixture"},
                {"fields", {{"requiredGraph", {
                    {"domain", "ProjectAsset"},
                    {"assetType", "ActionGraph"},
                    {"guid", graphGuid}
                }}}}
            }}
        }})}
    }});
    const auto root = Vans::DecodeSerializedValueJson(sceneJson);
    const auto result = Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
        database,root,temporary.path/"Scenes"/"Empty.json",{},repository);
    if (!Expect(result.success && result.requiredAssets.count(particleGuid) == 1 &&
        result.requiredAssets.count(decoyModelGuid) == 0 &&
        result.requiredTextures.count(textureGuid) == 1 && result.resourcePlan.textures.size() == 1 &&
        result.resourcePlan.textures.front().assetGuid == textureGuid,
        "A GAF-only particle texture did not enter the GPU resource plan on first load")) return false;
    std::cout << "[ParticleDependencies] coldSourceGraph=1 particle=1 texture=1 gpuRequest=1\n";
    return true;
}

bool TestProjectMaterialDependencyClosure()
{
	TemporaryDirectory temporary;
	const fs::path assets = temporary.path / "Assets";
	fs::create_directories(assets);
	const std::string materialGuid = "9b1d5a11-bdef-4567-8000-000000000101";
	const std::string textureGuid = "9b1d5a11-bdef-4567-8000-000000000102";
	const nlohmann::json material = {
		{ "schemaVersion", 1 },
		{ "guid", materialGuid },
		{ "materialType", "pbr" },
		{ "parameters", {
			{ "albedo", nlohmann::json::array({ 1.0, 1.0, 1.0 }) },
			{ "metallic", 0.0 }, { "roughness", 1.0 }, { "ao", 1.0 } } },
		{ "textures", {
			{ "basecolor", { { "guid", textureGuid } } } } }
	};
	std::ofstream(assets / "GlobalMaterial.mat") << material.dump();
	std::ofstream(assets / "GlobalTexture.png") << "project material texture fixture";
	for (const auto& item : std::vector<std::tuple<std::string, std::string, std::string>>{
		{ "GlobalMaterial.mat", materialGuid,
			Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Material) },
		{ "GlobalTexture.png", textureGuid,
			Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Texture) } })
	{
		const nlohmann::json meta = {
			{ "guid", std::get<1>(item) }, { "importer", std::get<2>(item) },
			{ "version", 1 }, { "settings", nlohmann::json::object() },
			{ "subAssets", nlohmann::json::object() } };
		std::ofstream(assets / (std::get<0>(item) + ".meta")) << meta.dump();
	}

	Vans::VansAssetDatabase database(assets, temporary.path / "Artifacts");
	const auto scan = database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
	if (!Expect(scan.errors.empty() && scan.registered == 2,
		"Project material closure fixture did not scan"))
		return false;
	Vans::VansAssetObjectRepository repository;
	const auto bootstrap = Vans::VansAssetObjectBootstrapper::Publish(
		database.All(), repository);
	if (!Expect(static_cast<bool>(bootstrap), bootstrap.errors.empty()
		? "Project material closure bootstrap failed" : bootstrap.errors.front().c_str()))
		return false;

	Vans::VansSceneData scene;
	const auto root = Vans::DecodeSerializedValueJson(
		Vans::VansSceneSchema::SerializeSceneJson(scene));
	const auto result = Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
		database, root, temporary.path / "Scenes" / "Empty.json", {}, repository);
	return Expect(result.success && result.requiredMaterials.count(materialGuid) == 1 &&
		result.requiredTextures.count(textureGuid) == 1 &&
		result.resourcePlan.textures.size() == 1 &&
		result.resourcePlan.textures.front().assetGuid == textureGuid,
		"Project-global material texture did not enter the packaged GPU resource closure");
}

bool TestClothVertexPayloadMatchesRenderMeshAbi()
{
	const std::vector<float> positionsAndNormals = {
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
	const std::vector<float> texCoords = {
		0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
	const std::vector<int> triangleIndices = { 0, 1, 2 };

	for (const std::uint32_t vertexStrideBytes : {
		8u * static_cast<std::uint32_t>(sizeof(std::uint16_t)),
		12u * static_cast<std::uint32_t>(sizeof(std::uint16_t)) })
	{
		VansEngine::VansClothMeshSource source;
		source.positionsAndNormals = &positionsAndNormals;
		source.texCoords = &texCoords;
		source.triangleIndices = &triangleIndices;
		source.vertexCount = 3;
		source.vertexStrideBytes = vertexStrideBytes;
		VansEngine::VansClothMeshData mesh;
		std::string error;
		if (!Expect(VansEngine::VansClothMeshPrep::Build(
			source, 0.001f, 0.001f, 0.0f, {}, mesh, error),
			error.empty() ? "Cloth mesh ABI fixture failed" : error.c_str()))
			return false;
		const std::size_t payloadBytes = static_cast<std::size_t>(mesh.vertexCount) *
			static_cast<std::size_t>(mesh.packedVertexStride) * sizeof(std::uint16_t);
		if (!Expect(payloadBytes ==
			static_cast<std::size_t>(mesh.vertexCount) * vertexStrideBytes,
			"Cloth render payload size diverged from the target mesh vertex ABI"))
			return false;
	}
	return true;
}

bool TestSceneMemoryDependencyPlanContract()
{
	static_assert(std::is_same_v<
		decltype(VansGraphics::VansSceneAssembly::BuildObjects(
			std::declval<VansGraphics::VansScene&>(),
			std::declval<VkDevice&>(),
			std::declval<const Vans::VansSceneObjectBuildPlan&>(),
			std::declval<const std::string&>())),
		VansGraphics::VansSceneObjectBuildResult>);
	static_assert(std::is_same_v<
		decltype(VansGraphics::VansSceneAssembly::CreateEntityBatch(
			std::declval<VansGraphics::VansScene&>(),
			std::declval<VkDevice&>(),
			std::declval<const Vans::VansSceneObjectBuildPlan&>(),
			std::declval<const std::string&>())),
		Vans::VansSceneEntityBatchResult>);
	if (!TestGAFParticleDependencyClosure() || !TestProjectMaterialDependencyClosure() ||
		!TestClothVertexPayloadMatchesRenderMeshAbi())
		return false;
	TemporaryDirectory temporary;
	const fs::path assetsRoot = temporary.path / "Assets";
	const fs::path sceneSourcePath = temporary.path / "Scenes" / "MemoryScene.json";
	fs::create_directories(assetsRoot);
	fs::create_directories(sceneSourcePath.parent_path());
	Vans::VansAssetDatabase database(assetsRoot, temporary.path / "Artifacts");

	Vans::VansSceneData scene;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"11111111-2222-4333-8444-555555555555", scene.sceneGuid),
		"Could not create the in-memory scene GUID fixture"))
		return false;
	const Vans::SceneJson sceneJson = Vans::VansSceneSchema::SerializeSceneJson(scene);
	std::string error;
	{
		Vans::VansScopedIOContext fixtureWrite(
			Vans::VansIODomain::Authoring, "ContractFixture.SceneWrite", true);
		if (!Expect(Vans::VansSceneFileStorage::WriteSceneDocument(
			sceneSourcePath, sceneJson, error),
			"Could not create the scene memory-load fixture"))
			return false;
	}
	Vans::VansIOAudit::Reset();
	Vans::SceneDocumentLoadResult sceneLoad =
		Vans::VansSceneDocumentLoader::Load(sceneSourcePath);
	if (!Expect(static_cast<bool>(sceneLoad),
		"Could not load the scene memory-load fixture"))
		return false;
	{
		const auto original = sceneLoad.document->CreateSnapshot();
		const auto same = sceneLoad.document->CreateSnapshot();
		if (!Expect(&original.Root() == &same.Root(), "Read-only scene snapshots copied their root")) return false;
		const auto originalName = Vans::ReadSerializedStringField(original.Root(), "name");
		Vans::VansSceneEditService edits(*sceneLoad.document);
		if (!Expect(static_cast<bool>(edits.Set(
			Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, "/name"),
			Vans::VansSerializedValue::String("Edited snapshot"))), "Snapshot edit failed")) return false;
		const auto edited = sceneLoad.document->CreateSnapshot();
		if (!Expect(&edited.Root() != &original.Root() &&
			Vans::ReadSerializedStringField(original.Root(), "name") == originalName &&
			Vans::ReadSerializedStringField(edited.Root(), "name") == "Edited snapshot",
			"Publishing an edited document changed a live read-only snapshot")) return false;
		if (!Expect(static_cast<bool>(edits.Undo()) &&
			Vans::ReadSerializedStringField(sceneLoad.document->CreateSnapshot().Root(), "name") == originalName &&
			static_cast<bool>(edits.Redo()) &&
			Vans::ReadSerializedStringField(original.Root(), "name") == originalName &&
			Vans::ReadSerializedStringField(edited.Root(), "name") == "Edited snapshot",
			"Undo/redo invalidated retained immutable snapshots")) return false;
	}
	const auto loadIOEvents = Vans::VansIOAudit::Snapshot();
	const std::size_t sceneReads = static_cast<std::size_t>(std::count_if(
		loadIOEvents.begin(), loadIOEvents.end(),
		[&](const Vans::VansIOEvent& event)
		{
			return event.domain == Vans::VansIODomain::Authoring &&
				event.operation == Vans::VansIOOperation::Read &&
				event.path.lexically_normal() == sceneSourcePath.lexically_normal();
		}));
	if (!Expect(sceneReads == 1,
		"Loading a scene document did not perform exactly one Authoring read"))
		return false;
	const Vans::VansSerializedValue sceneDocument =
		sceneLoad.document->SerializedRootSnapshot();
	std::error_code removeError;
	fs::remove(sceneSourcePath, removeError);
	if (!Expect(!removeError, "Could not remove the loaded scene fixture before projection"))
		return false;
	Vans::VansAssetObjectRepository objectRepository;
	Vans::VansIOAudit::Reset();
	const Vans::VansSceneAssetDependencyBuildResult result =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database, sceneDocument, sceneSourcePath, {}, objectRepository);
	const auto dependencyIO = Vans::VansIOAudit::Snapshot();
	return Expect(result.success && !fs::exists(sceneSourcePath),
		"Scene dependency planning still requires the authoring scene file on disk") &&
		Expect(std::none_of(
			dependencyIO.begin(), dependencyIO.end(),
			[](const Vans::VansIOEvent& event)
			{
				return event.domain == Vans::VansIODomain::Authoring;
			}),
			"Scene dependency planning performed authoring I/O after memory bootstrap");
}

bool TestAuthoringCodecContract()
{
	std::string error;
	Vans::VansProjectConfig projectConfig;
	projectConfig.SetDefaults("Project Codec Contract");
	const nlohmann::json projectConfigEncoded =
		Vans::VansProjectConfigJsonCodec::EncodeProjectConfig(projectConfig);
	Vans::VansProjectConfig projectConfigRoundTrip;
	if (!Expect(
		!projectConfigEncoded.at("assetDatabase").contains("metaExtension") &&
		Vans::VansProjectConfigJsonCodec::DecodeProjectConfig(
			projectConfigEncoded, projectConfigRoundTrip, error) &&
		projectConfigRoundTrip.projectName == projectConfig.projectName,
		"Project configuration still serializes a configurable meta extension"))
		return false;
	nlohmann::json removedProjectConfig = projectConfigEncoded;
	removedProjectConfig["assetDatabase"]["metaExtension"] = ".legacy";
	if (!Expect(
		!Vans::VansProjectConfigJsonCodec::DecodeProjectConfig(
			removedProjectConfig, projectConfigRoundTrip, error),
		"Project configuration accepted the removed metaExtension field"))
		return false;

	Vans::VansAssetMeta assetMeta;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"10000000-0000-4000-8000-000000000001", assetMeta.guid),
		"Could not create the asset-meta Codec fixture GUID"))
		return false;
	assetMeta.importer = "TextureImporter";
	assetMeta.SetSerializedSettings(Vans::VansSerializedValue::Object({
		{ "linear", Vans::VansSerializedValue::Bool(true) },
		{ "needMip", Vans::VansSerializedValue::Bool(false) }
	}));
	Vans::VansSerializedValue assetMetaEncoded;
	Vans::VansAssetMeta assetMetaRoundTrip;
	if (!Expect(
		Vans::VansAssetMetaJsonCodec::Encode(assetMeta, assetMetaEncoded, error) &&
		Vans::VansAssetMetaJsonCodec::Decode(
			assetMetaEncoded, "Texture.png.meta", assetMetaRoundTrip, error) &&
		assetMetaRoundTrip.guid == assetMeta.guid &&
		assetMetaRoundTrip.importer == assetMeta.importer &&
		assetMetaRoundTrip.ReadBoolSetting("linear", false) &&
		!assetMetaRoundTrip.ReadBoolSetting("needMip", true),
		"Asset metadata VansSerializedValue Codec did not round-trip"))
		return false;
	Vans::VansSerializedValue invalidAssetMeta = assetMetaEncoded;
	Vans::SetSerializedObjectField(
		invalidAssetMeta, "guid", Vans::VansSerializedValue::String("invalid"));
	if (!Expect(
		!Vans::VansAssetMetaJsonCodec::Decode(
			invalidAssetMeta, "Texture.png.meta", assetMetaRoundTrip, error),
		"Asset metadata Codec accepted an invalid GUID"))
		return false;
	Vans::VansSerializedValue removedSettingAssetMeta = assetMetaEncoded;
	if (Vans::VansSerializedValue* settings =
		Vans::FindObjectField(removedSettingAssetMeta, "settings"))
	{
		Vans::SetSerializedObjectField(
			*settings, "generateMip", Vans::VansSerializedValue::Bool(true));
	}
	if (!Expect(
		!Vans::VansAssetMetaJsonCodec::Decode(
			removedSettingAssetMeta, "Texture.png.meta", assetMetaRoundTrip, error),
		"Asset metadata Codec accepted a removed settings key"))
		return false;
	TemporaryDirectory assetMetaTemporary;
	const fs::path assetMetaPath = assetMetaTemporary.path / "Texture.png.meta";
	Vans::VansAssetMeta assetMetaStorageRoundTrip;
	if (!Expect(
		Vans::VansAssetMetaStorage::SaveAtomic(assetMetaPath, assetMeta, error) &&
		Vans::VansAssetMetaStorage::Load(
			assetMetaPath, assetMetaStorageRoundTrip, error) &&
		assetMetaStorageRoundTrip.guid == assetMeta.guid &&
		assetMetaStorageRoundTrip.importer == assetMeta.importer &&
		assetMetaStorageRoundTrip.ReadBoolSetting("linear", false),
		"Asset metadata JSON storage boundary did not round-trip"))
		return false;

	const Vans::VansSerializedValue materialRoot = Vans::VansSerializedValue::Object({
		{ "guid", Vans::VansSerializedValue::String(
			"20000000-0000-4000-8000-000000000001") },
		{ "materialType", Vans::VansSerializedValue::String("pbr") },
		{ "parameters", Vans::CreatePbrMaterialAuthoringParameters(
			1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f) },
		{ "textures", Vans::VansSerializedValue::Object({
			{ "basecolor", Vans::VansSerializedValue::Object({
				{ "guid", Vans::VansSerializedValue::String(
					"30000000-0000-4000-8000-000000000001") }
			}) }
		}) }
	});
	Vans::VansMaterialAuthoringAsset materialAsset;
	if (!Expect(Vans::ReadMaterialAuthoringAsset(materialRoot, materialAsset, error),
		"Material Codec rejected a current GUID texture reference"))
		return false;
	Vans::VansSerializedValue invalidMaterial = materialRoot;
	if (Vans::VansSerializedValue* textures =
		Vans::FindObjectField(invalidMaterial, "textures"))
	{
		Vans::SetSerializedObjectField(
			*textures, "basecolor", Vans::VansSerializedValue::String("Albedo.png"));
	}
	if (!Expect(!Vans::ReadMaterialAuthoringAsset(
		invalidMaterial, materialAsset, error),
		"Material Codec still accepts a filename texture alias"))
		return false;

	Vans::VansSerializedValue currentMaterialReferences = materialRoot;
	const Vans::VansSerializedValue shaderReference = Vans::VansSerializedValue::Object({
		{ "guid", Vans::VansSerializedValue::String(
			"40000000-0000-4000-8000-000000000001") }
	});
	Vans::SetSerializedObjectField(currentMaterialReferences, "shader", shaderReference);
	Vans::SetSerializedObjectField(
		currentMaterialReferences,
		"shaderPasses",
		Vans::VansSerializedValue::Object({ { "gbuffer", shaderReference } }));
	Vans::SetSerializedObjectField(
		currentMaterialReferences,
		"customTextures",
		Vans::VansSerializedValue::Object({
			{ "detail", Vans::VansSerializedValue::Object({
				{ "guid", Vans::VansSerializedValue::String(
					"30000000-0000-4000-8000-000000000002") }
			}) }
		}));
	if (Vans::VansSerializedValue* parameters =
		Vans::FindObjectField(currentMaterialReferences, "parameters"))
	{
		Vans::SetSerializedObjectField(
			*parameters,
			"skinProfile",
			Vans::VansSerializedValue::Object({
				{ "guid", Vans::VansSerializedValue::String(
					"50000000-0000-4000-8000-000000000001") }
			}));
	}
	if (!Expect(Vans::ReadMaterialAuthoringAsset(
		currentMaterialReferences, materialAsset, error),
		"Material Codec rejected current GUID shader/profile/custom texture references"))
		return false;

	Vans::VansSerializedValue filenameShaderMaterial = currentMaterialReferences;
	Vans::SetSerializedObjectField(
		filenameShaderMaterial, "shader", Vans::VansSerializedValue::String("CustomShader"));
	if (!Expect(!Vans::ReadMaterialAuthoringAsset(
		filenameShaderMaterial, materialAsset, error),
		"Material Codec still accepts a runtime shader-name alias"))
		return false;

	Vans::VansSerializedValue stringProfileMaterial = currentMaterialReferences;
	if (Vans::VansSerializedValue* parameters =
		Vans::FindObjectField(stringProfileMaterial, "parameters"))
	{
		Vans::SetSerializedObjectField(
			*parameters,
			"skinProfile",
			Vans::VansSerializedValue::String(
				"50000000-0000-4000-8000-000000000001"));
	}
	if (!Expect(!Vans::ReadMaterialAuthoringAsset(
		stringProfileMaterial, materialAsset, error),
		"Material Codec still accepts a bare skin-profile GUID"))
		return false;

	Vans::VansSerializedValue presetProfileMaterial = materialRoot;
	if (Vans::VansSerializedValue* parameters =
		Vans::FindObjectField(presetProfileMaterial, "parameters"))
	{
		Vans::SetSerializedObjectField(
			*parameters,
			"skinProfilePreset",
			Vans::VansSerializedValue::String("fair"));
	}
	if (!Expect(Vans::ReadMaterialAuthoringAsset(
		presetProfileMaterial, materialAsset, error),
		"Material Codec rejected the separate built-in skin-profile preset field"))
		return false;

	static_assert(std::is_same_v<
		decltype(VansGraphics::VansPostProcessProfileJsonCodec::Encode(
			std::declval<const VansGraphics::VansPostProcessProfile&>())),
		Vans::VansSerializedValue>);

	VansGraphics::VansPostProcessProfile postProcess;
	postProcess.m_EnableAutoExposure = true;
	postProcess.m_ExposureCompensation = 1.25f;
	postProcess.m_BloomIntensity = 0.8f;
	postProcess.m_FStop = 4.0f;
	postProcess.m_MotionBlurSamples = 7;
	const Vans::VansSerializedValue postProcessEncoded =
		VansGraphics::VansPostProcessProfileJsonCodec::Encode(postProcess);
	VansGraphics::VansPostProcessProfile postProcessRoundTrip;
	if (!Expect(
		VansGraphics::VansPostProcessProfileJsonCodec::Decode(
			postProcessEncoded, "PostProcessContract.vpostprocess", postProcessRoundTrip, error) &&
		postProcessRoundTrip.m_EnableAutoExposure &&
		postProcessRoundTrip.m_ExposureCompensation == 1.25f &&
		postProcessRoundTrip.m_BloomIntensity == 0.8f &&
		postProcessRoundTrip.m_FStop == 4.0f &&
		postProcessRoundTrip.m_MotionBlurSamples == 7,
		"Post-process profile VansSerializedValue Codec did not round-trip"))
		return false;
	TemporaryDirectory postProcessTemporary;
	const fs::path postProcessPath =
		postProcessTemporary.path / "PostProcessContract.vpostprocess";
	VansGraphics::VansPostProcessProfile postProcessStorageRoundTrip;
	if (!Expect(
		VansGraphics::VansPostProcessProfileStorage::SaveAtomic(
			postProcessPath, postProcess, error) &&
		VansGraphics::VansPostProcessProfileStorage::Load(
			postProcessPath, postProcessStorageRoundTrip, error) &&
		postProcessStorageRoundTrip.m_EnableAutoExposure &&
		postProcessStorageRoundTrip.m_ExposureCompensation == 1.25f &&
		postProcessStorageRoundTrip.m_BloomIntensity == 0.8f &&
		postProcessStorageRoundTrip.m_FStop == 4.0f &&
		postProcessStorageRoundTrip.m_MotionBlurSamples == 7,
		"Post-process profile JSON storage boundary did not round-trip"))
		return false;
	Vans::VansSerializedValue invalidPostProcess = postProcessEncoded;
	if (Vans::VansSerializedValue* bloom =
		Vans::FindObjectField(invalidPostProcess, "bloom"))
	{
		Vans::SetSerializedObjectField(
			*bloom, "threshold", Vans::VansSerializedValue::String("invalid"));
	}
	if (!Expect(
		!VansGraphics::VansPostProcessProfileJsonCodec::Decode(
			invalidPostProcess, "PostProcessContract.vpostprocess", postProcessRoundTrip, error),
		"Post-process profile Codec accepted a non-numeric threshold"))
		return false;

	const nlohmann::json retargetRoot = {
		{ "assetKind", "retargetProfile" },
		{ "name", "RTG_Test" },
		{ "translationScale", { { "mode", "compatibleSkeleton" } } },
		{ "rootAlignment", "feetToOwner" },
		{ "targetModelSpaceAlignment", "sourceBindPose" },
		{ "limbMappings", nlohmann::json::array({ {
			{ "id", "LeftArm" },
			{ "sourceBones", { "upperarm_l", "lowerarm_l", "hand_l" } },
			{ "targetChain", "leftArm" },
			{ "positionWeight", 1.0 }
		} }) }
	};
	VansGraphics::VansRetargetProfileAsset retarget;
	nlohmann::json retargetEncoded;
	VansGraphics::VansRetargetProfileAsset retargetRoundTrip;
	if (!Expect(
		VansGraphics::VansRetargetProfileJsonCodec::Decode(
			retargetRoot, retarget, error) &&
		VansGraphics::VansRetargetProfileJsonCodec::Encode(
			retarget, retargetEncoded, error) &&
		VansGraphics::VansRetargetProfileJsonCodec::Decode(
			retargetEncoded, retargetRoundTrip, error) &&
		retargetRoundTrip.name == retarget.name &&
		retargetRoundTrip.limbChains.size() == 1,
		"Retarget profile pure Codec did not round-trip the current schema"))
		return false;
	nlohmann::json invalidRetarget = retargetRoot;
	invalidRetarget["legacyField"] = true;
	if (!Expect(!VansGraphics::VansRetargetProfileJsonCodec::Decode(
		invalidRetarget, retargetRoundTrip, error),
		"Retarget profile Codec accepted a non-current field"))
		return false;

	const nlohmann::json aiRoot = {
		{ "magic", "VAI_BEHAVIOR" },
		{ "name", "WhisperTest" },
		{ "blackboard", nlohmann::json::array({
			{
				{ "name", "Activated" }, { "type", "bool" }, { "default", false }
			},
			{
				{ "name", "Released" }, { "type", "bool" }, { "default", false }
			},
			{
				{ "name", "HasTarget" }, { "type", "bool" }, { "default", false }
			},
			{
				{ "name", "Target" }, { "type", "entity" }, { "default", nullptr }
			}
		}) },
		{ "bindings", {
			{ "activationRequested", "Activated" },
			{ "gameplayReleased", "Released" },
			{ "target", "Target" }
		} },
		{ "maxTransitionsPerUpdate", 4 },
		{ "initialState", "Idle" },
		{ "states", nlohmann::json::array({
			{
				{ "id", "Idle" }, { "task", "Hold" },
				{ "transitions", nlohmann::json::array({ {
					{ "condition", {
						{ "type", "BlackboardBool" },
						{ "key", "HasTarget" }, { "value", true }
					} },
					{ "target", "Chase" }
				} }) }
			},
			{
				{ "id", "Chase" }, { "task", "MoveToTarget" },
				{ "transitions", nlohmann::json::array() }
			}
		}) }
	};
	Vans::VansAIBehaviorAsset ai;
	nlohmann::json aiEncoded;
	Vans::VansAIBehaviorAsset aiRoundTrip;
	if (!Expect(
		Vans::VansAIBehaviorJsonCodec::Decode(aiRoot, ai, error) &&
		Vans::VansAIBehaviorJsonCodec::Encode(ai, aiEncoded, error) &&
		Vans::VansAIBehaviorJsonCodec::Decode(aiEncoded, aiRoundTrip, error) &&
		aiRoundTrip.name == ai.name && aiRoundTrip.states.size() == 2 &&
		aiRoundTrip.blackboard.size() == 4 &&
		aiRoundTrip.bindings.activationRequested == "Activated" &&
		aiRoundTrip.maxTransitionsPerUpdate == 4,
		"AI Behavior pure Codec did not round-trip the current schema"))
		return false;
	nlohmann::json invalidAi = aiRoot;
	invalidAi["bindings"]["target"] = "HasTarget";
	if (!Expect(!Vans::VansAIBehaviorJsonCodec::Decode(
		invalidAi, aiRoundTrip, error),
		"AI Behavior Codec accepted a binding with the wrong Blackboard type"))
		return false;
	invalidAi = aiRoot;
	invalidAi["maxTransitionsPerUpdate"] = 0;
	if (!Expect(!Vans::VansAIBehaviorJsonCodec::Decode(
		invalidAi, aiRoundTrip, error),
		"AI Behavior Codec accepted a zero transition limit"))
		return false;
	invalidAi = aiRoot;
	invalidAi["states"][1]["task"] = "Patrol";
	invalidAi["states"][1]["taskConfig"] = {
		{ "radius", 0.0 }, { "waitSeconds", 0.0 }
	};
	if (!Expect(!Vans::VansAIBehaviorJsonCodec::Decode(
		invalidAi, aiRoundTrip, error),
		"AI Behavior Codec accepted a non-positive Patrol radius"))
		return false;

	const nlohmann::json ragdollRoot = {
		{ "name", "Hero" },
		{ "bodies", nlohmann::json::array({ {
			{ "bone_name", "pelvis" }, { "shape_type", "box" },
			{ "box_extents", { 0.1, 0.2, 0.1 } },
			{ "mass", 10.0 }, { "layer", "CharacterInternal" }
		} }) },
		{ "joints", nlohmann::json::array() }
	};
	VansEngine::RagdollProfile ragdoll;
	nlohmann::json ragdollEncoded;
	VansEngine::RagdollProfile ragdollRoundTrip;
	if (!Expect(
		VansEngine::VansRagdollProfileJsonCodec::Decode(
			ragdollRoot, ragdoll, error) &&
		VansEngine::VansRagdollProfileJsonCodec::Encode(
			ragdoll, ragdollEncoded, error) &&
		VansEngine::VansRagdollProfileJsonCodec::Decode(
			ragdollEncoded, ragdollRoundTrip, error) &&
		ragdollRoundTrip.bodies.size() == 1 &&
		ragdollRoundTrip.bodies.front().boneName == "pelvis",
		"Ragdoll pure Codec did not round-trip the current schema"))
		return false;
	nlohmann::json legacyRagdoll = ragdollRoot;
	// 全部自身碰撞和显式关节坐标必须能往返，旧 Profile 保持默认关闭。
	if (!Expect(!ragdoll.selfCollision, "Ragdoll default self collision changed")) return false;
	ragdoll.selfCollision = true;
	ragdoll.bodies.front().inertiaScale = 16.f;
	VansEngine::RagdollJointConfig knee;
	knee.childBoneName = "pelvis"; knee.hasLocalFrames = true;
	knee.parentFramePosition = glm::vec3(0, 0.4f, 0);
	knee.childFrameRotation = glm::vec3(0, 15, 0);
	knee.swingYLimit = knee.swingZLimit = 0;
	knee.twistLowLimit = 0; knee.twistHighLimit = 135;
	knee.limitStiffness = knee.limitDamping = 0;
	ragdoll.joints.push_back(knee);
	if (!Expect(VansEngine::VansRagdollProfileJsonCodec::Encode(ragdoll, ragdollEncoded, error) &&
		VansEngine::VansRagdollProfileJsonCodec::Decode(ragdollEncoded, ragdollRoundTrip, error) &&
		ragdollRoundTrip.selfCollision && ragdollRoundTrip.bodies.front().inertiaScale == 16.f &&
		ragdollRoundTrip.joints.front().hasLocalFrames &&
		ragdollRoundTrip.joints.front().childFrameRotation.y == 15 &&
		ragdollRoundTrip.joints.front().swingYLimit == 0,
		"Ragdoll full collision and anatomical frames did not round-trip")) return false;
	ragdollEncoded["joints"][0]["twist_high_limit"] = 0;
	if (!Expect(VansEngine::VansRagdollProfileJsonCodec::Decode(ragdollEncoded,ragdollRoundTrip,error),
		"Ragdoll rejected a fully locked joint")) return false;
	ragdollEncoded["joints"][0].erase("parent_frame_rotation");
	if (!Expect(!VansEngine::VansRagdollProfileJsonCodec::Decode(ragdollEncoded,ragdollRoundTrip,error),
		"Ragdoll accepted incomplete joint frames")) return false;
	physx::PxFilterData selfA(7,1u<<7,2,123),selfB(7,1u<<7,2,123);
	if (!Expect(!VansEngine::RagdollPairSuppressed(selfA,selfB),"Full self collision suppressed body pair")) return false;
	selfB.word2=0;
	if (!Expect(VansEngine::RagdollPairSuppressed(selfA,selfB),"Disabled self collision was not suppressed")) return false;
	selfB.word3=124;
	if (!Expect(!VansEngine::RagdollPairSuppressed(selfA,selfB),"Different ragdolls were treated as one")) return false;
	legacyRagdoll["bodies"][0].erase("bone_name");
	legacyRagdoll["bodies"][0]["boneName"] = "pelvis";
	if (!Expect(!VansEngine::VansRagdollProfileJsonCodec::Decode(
		legacyRagdoll, ragdollRoundTrip, error),
		"Ragdoll Codec still accepts the removed camelCase field path"))
		return false;

	const auto& registry = Vans::VansAssetDocumentTypeRegistry::Get();
	const Vans::VansAssetType registeredDocumentTypes[] = {
		Vans::VansAssetType::Material,
		Vans::VansAssetType::Shader,
		Vans::VansAssetType::Particle,
		Vans::VansAssetType::AnimatorController,
		Vans::VansAssetType::AnimationRig,
		Vans::VansAssetType::RetargetProfile,
		Vans::VansAssetType::BoneMask,
		Vans::VansAssetType::Timeline,
		Vans::VansAssetType::AIBehavior,
		Vans::VansAssetType::ClothProfile,
		Vans::VansAssetType::SkinProfile,
		Vans::VansAssetType::PostProcessProfile,
		Vans::VansAssetType::RagdollProfile,
		Vans::VansAssetType::AudioReverbPreset,
		Vans::VansAssetType::AudioBusSnapshot,
		Vans::VansAssetType::AudioDuckingRules,
		Vans::VansAssetType::UIScreen,
		Vans::VansAssetType::UIComponent,
		Vans::VansAssetType::UIThemeTokens,
		Vans::VansAssetType::UILocalization,
		Vans::VansAssetType::VegetationConfig
	};
	for (const Vans::VansAssetType type : registeredDocumentTypes)
	{
		if (!Expect(registry.Find(type) != nullptr,
			"A configurable authoring asset type is missing its document descriptor"))
			return false;
	}
	const auto validate = [&](Vans::VansAssetType type, const nlohmann::json& root)
	{
		const fs::path path = type == Vans::VansAssetType::RetargetProfile
			? fs::path("Profile.vretarget") : fs::path{};
		return registry.ValidateBeforeSave(
			type, path, Vans::DecodeSerializedValueJson(root));
	};
	const nlohmann::json uiScreenRoot = {
		{ "schemaVersion", 1 },
		{ "guid", "ui-screen-guid" },
		{ "name", "HUD" },
		{ "xaml", { { "guid", "11111111-2222-4333-8444-555555555551" } } }
	};
	const nlohmann::json uiComponentRoot = {
		{ "schemaVersion", 1 },
		{ "guid", "ui-component-guid" },
		{ "name", "Button" },
		{ "xaml", { { "guid", "11111111-2222-4333-8444-555555555552" } } }
	};
	const nlohmann::json uiTokensRoot = {
		{ "schemaVersion", 1 },
		{ "name", "Default" },
		{ "colors", { { "accent", "#FFFFFFFF" } } }
	};
	const nlohmann::json uiLocalizationRoot = {
		{ "schemaVersion", 1 },
		{ "locale", "zh-CN" },
		{ "strings", { { "menu.play", "Play" } } }
	};
	const nlohmann::json vegetationRoot = {
		{ "name", "ContractGrass" },
		{ "regions", nlohmann::json::array() }
	};
	Vans::VansVegetationConfigAsset vegetationAsset;
	Vans::VansSerializedValue vegetationEncoded;
	Vans::VansVegetationConfigAsset vegetationRoundTrip;
	const Vans::VansSerializedValue vegetationSerialized =
		Vans::DecodeSerializedValueJson(vegetationRoot);
	const Vans::VansSerializedValue vegetationReference =
		Vans::VansSerializedValue::Object({
			{ "asset", Vans::VansSerializedValue::Object({
				{ "guid", Vans::VansSerializedValue::String(
					"5d5d1201-1ed8-4f3e-8cc6-641c5b1d0001") }
			}) }
		});
	Vans::VansPcgRecipeAsset resolvedVegetation;
	if (!Expect(
		Vans::VansVegetationConfigCodec::Decode(
			vegetationSerialized, vegetationAsset, error) &&
		Vans::VansVegetationConfigCodec::Encode(
			vegetationAsset.config, vegetationEncoded, error) &&
		Vans::VansVegetationConfigCodec::Decode(
			vegetationEncoded, vegetationRoundTrip, error) &&
		vegetationRoundTrip.config.regions.size() == vegetationAsset.config.regions.size() &&
		Vans::VansVegetationConfigCodec::ResolveReference(
			vegetationReference, vegetationAsset, resolvedVegetation, error) &&
		resolvedVegetation.name == vegetationAsset.config.name,
		"Vegetation pure Codec did not round-trip or resolve a GUID reference"))
		return false;
	const Vans::VansSerializedValue legacyVegetationReference =
		Vans::VansSerializedValue::Object({
			{ "config", Vans::VansSerializedValue::String(
				"Assets/Vegetation/MainVegetation.json") }
		});
	if (!Expect(
		Vans::VansVegetationConfigCodec::ReadReferenceGuid(
			legacyVegetationReference).empty(),
		"Vegetation Codec still accepts a path-based scene reference"))
		return false;
	return Expect(
		validate(Vans::VansAssetType::RetargetProfile, retargetRoot).empty() &&
		validate(Vans::VansAssetType::AIBehavior, aiRoot).empty() &&
		validate(Vans::VansAssetType::RagdollProfile, ragdollRoot).empty() &&
		validate(Vans::VansAssetType::UIScreen, uiScreenRoot).empty() &&
		validate(Vans::VansAssetType::UIComponent, uiComponentRoot).empty() &&
		validate(Vans::VansAssetType::UIThemeTokens, uiTokensRoot).empty() &&
		validate(Vans::VansAssetType::UILocalization, uiLocalizationRoot).empty() &&
		validate(Vans::VansAssetType::VegetationConfig, vegetationRoot).empty() &&
		Vans::VansAssetDatabase::Classify("Profile.vretarget") ==
			Vans::VansAssetType::RetargetProfile &&
		Vans::VansAssetDatabase::Classify("Profile.vragdoll") ==
			Vans::VansAssetType::RagdollProfile &&
		Vans::VansAssetDatabase::Classify("Profile.ragdoll") ==
			Vans::VansAssetType::Unknown &&
		Vans::VansAssetDatabase::Classify("HUD.vui.json") ==
			Vans::VansAssetType::UIScreen &&
		Vans::VansAssetDatabase::Classify("Button.vcomp.json") ==
			Vans::VansAssetType::UIComponent &&
		Vans::VansAssetDatabase::Classify("Default.tokens.json") ==
			Vans::VansAssetType::UIThemeTokens &&
		Vans::VansAssetDatabase::Classify("zh-CN.loc.json") ==
			Vans::VansAssetType::UILocalization &&
		Vans::VansAssetDatabase::Classify("HUD.xaml") ==
			Vans::VansAssetType::UIXaml &&
		Vans::VansAssetDatabase::Classify(
			"Assets/Vegetation/MainVegetation.json") ==
			Vans::VansAssetType::VegetationConfig,
		"Authoring Codec types are not registered under their single current schema");
}

bool TestAssetObjectRepositoryContract()
{
	struct RepositoryFixture
	{
		int value = 0;
	};
	struct WrongRepositoryFixture
	{
		int value = 0;
	};
	struct RepositoryViewFixture
	{
		int value = 0;
	};

	Vans::VansAssetGuid guid;
	Vans::VansAssetGuid dependency;
	if (!Expect(
		Vans::VansAssetGuid::TryParse(
			"11111111-2222-4333-8444-555555555555", guid) &&
		Vans::VansAssetGuid::TryParse(
			"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", dependency),
		"Could not create asset object repository GUID fixtures"))
		return false;

	Vans::VansAssetObjectRepository repository;
	std::string error;
	const auto first = repository.Publish<RepositoryFixture>(
		guid,
		Vans::VansAssetType::AIBehavior,
		10,
		std::make_shared<const RepositoryFixture>(RepositoryFixture{ 7 }),
		{ dependency },
		error);
	if (!Expect(first.IsValid() && repository.Size() == 1,
		"Could not publish the first decoded asset object"))
		return false;
	const std::shared_ptr<const RepositoryFixture> firstObject = repository.Resolve(first);
	if (!Expect(firstObject && firstObject->value == 7,
		"A current asset object handle did not resolve"))
		return false;
	const auto firstView = repository.PublishView<RepositoryViewFixture>(
		guid,
		Vans::VansAssetType::AIBehavior,
		10,
		std::make_shared<const RepositoryViewFixture>(RepositoryViewFixture{ 17 }),
		error);
	Vans::VansAssetObjectHandle<RepositoryViewFixture> latestViewHandle;
	const auto latestView =
		repository.ResolveLatest<RepositoryViewFixture>(guid, &latestViewHandle);
	if (!Expect(
		firstView.IsValid() && firstView.generation == first.generation &&
		latestView && latestView->value == 17 &&
		latestViewHandle.generation == first.generation,
		"Could not attach a typed memory view to the current asset generation"))
		return false;

	const auto unchanged = repository.Publish<RepositoryFixture>(
		guid,
		Vans::VansAssetType::AIBehavior,
		10,
		std::make_shared<const RepositoryFixture>(RepositoryFixture{ 99 }),
		{ dependency },
		error);
	if (!Expect(unchanged.generation == first.generation &&
		repository.Resolve(unchanged)->value == 7,
		"Publishing an unchanged content hash advanced or replaced its generation"))
		return false;

	const auto second = repository.Publish<RepositoryFixture>(
		guid,
		Vans::VansAssetType::AIBehavior,
		11,
		std::make_shared<const RepositoryFixture>(RepositoryFixture{ 9 }),
		{ dependency },
		error);
	Vans::VansAssetObjectHandle<RepositoryFixture> latestHandle;
	const std::shared_ptr<const RepositoryFixture> latest =
		repository.ResolveLatest<RepositoryFixture>(guid, &latestHandle);
	Vans::VansAssetObjectSnapshotInfo info;
	if (!Expect(
		second.IsValid() && second.generation != first.generation &&
		!repository.Resolve(first) && !repository.Resolve(firstView) &&
		!repository.ResolveLatest<RepositoryViewFixture>(guid) &&
		latest && latest->value == 9 &&
		latestHandle.generation == second.generation &&
		repository.FindInfo(guid, info) &&
		info.generation == second.generation && info.contentHash == 11 &&
		info.dependencies.size() == 1 && info.dependencies.front() == dependency,
		"Asset generation replacement did not atomically invalidate its primary object and typed views"))
		return false;

	const auto wrong = repository.Publish<WrongRepositoryFixture>(
		guid,
		Vans::VansAssetType::AIBehavior,
		12,
		std::make_shared<const WrongRepositoryFixture>(),
		{},
		error);
	if (!Expect(!wrong.IsValid() && !error.empty(),
		"Asset repository accepted a decoded object type change for one GUID"))
		return false;

	if (!Expect(repository.Remove(guid) && !repository.Resolve(second) &&
		repository.Size() == 0,
		"Removing an asset object did not invalidate its current handle"))
		return false;
	const auto republished = repository.Publish<RepositoryFixture>(
		guid,
		Vans::VansAssetType::AIBehavior,
		13,
		std::make_shared<const RepositoryFixture>(RepositoryFixture{ 12 }),
		{},
		error);
	if (!Expect(republished.IsValid() && republished.generation != second.generation,
		"Republishing a removed asset reused a stale generation"))
		return false;
	repository.Clear();
	if (!Expect(repository.Size() == 0 && !repository.Resolve(republished),
		"Clearing the repository did not invalidate published handles"))
		return false;

	TemporaryDirectory temporary;
	const fs::path shaderArtifactDirectory = temporary.path / "ShaderArtifact";
	const fs::path shaderAuthoringPath = shaderArtifactDirectory / "MemoryShader.vshader";
	fs::create_directories(shaderArtifactDirectory);
	{
		Vans::VansScopedIOContext fixtureWrite(
			Vans::VansIODomain::Authoring,
			"ContractFixture.PackagedShaderAuthoringWrite",
			true);
		if (!Expect(Vans::VansFileStorage::WriteAtomicBytes(
			shaderAuthoringPath,
			"{\"schemaVersion\":1,\"name\":\"MemoryShader\"}",
			error), error.c_str()))
			return false;
	}
	Vans::VansAssetGuid shaderGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"92b3650e-f18d-4af9-b7e8-d6059f77ae42", shaderGuid),
		"Packaged Shader memory fixture GUID is invalid"))
		return false;
	Vans::VansAssetRecord shaderRecord;
	shaderRecord.guid = shaderGuid;
	shaderRecord.type = Vans::VansAssetType::Shader;
	shaderRecord.state = Vans::VansAssetState::CpuReady;
	shaderRecord.sourcePath = shaderArtifactDirectory;
	shaderRecord.authoringPath = shaderAuthoringPath;
	shaderRecord.artifactPath = shaderArtifactDirectory;
	shaderRecord.artifactFormat = Vans::VansAssetArtifactFormat::Source;
	shaderRecord.sourceHash = 0x92b3650eu;
	const Vans::VansAssetObjectBootstrapResult shaderBootstrap =
		Vans::VansAssetObjectBootstrapper::Publish({ shaderRecord }, repository);
	const auto shaderObject =
		repository.ResolveLatest<Vans::VansShaderAuthoringAsset>(shaderGuid);
	return Expect(
		static_cast<bool>(shaderBootstrap) && shaderObject &&
		shaderObject->name == "MemoryShader",
		shaderBootstrap.errors.empty()
			? "Packaged Shader bootstrap did not use its indexed authoring document"
			: shaderBootstrap.errors.front().c_str());
}

bool TestAnimationClipMemoryAssetContract()
{
	using namespace VansGraphics;
	TemporaryDirectory temporary;
	const fs::path assetsRoot = temporary.path / "Assets";
	const fs::path clipPath = assetsRoot / "Animation" / "MemoryClip.vclip";
	fs::create_directories(clipPath.parent_path());

	const Skeleton skeleton = BuildLayerContractSkeleton();
	VansAnimationClip clip;
	clip.clipName = "MemoryClip";
	clip.duration = 1.0f;
	clip.boneKeyframes.resize(skeleton.bones.size());
	for (std::size_t bone = 0; bone < clip.boneKeyframes.size(); ++bone)
	{
		clip.boneKeyframes[bone].push_back({
			0.0f, glm::vec3(static_cast<float>(bone), 0.0f, 0.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
	}

	std::string bytes;
	std::string error;
	VansAnimationClip decodedClip;
	Skeleton decodedSkeleton;
	Vans::VansIOAudit::Reset();
	if (!Expect(VansAnimationClipBinaryCodec::Encode(
		clip, skeleton, bytes, error) &&
		VansAnimationClipBinaryCodec::Decode(
			bytes, decodedClip, decodedSkeleton, error) &&
		decodedClip.clipName == clip.clipName &&
		decodedSkeleton.signature == skeleton.ComputeSignature() &&
		Vans::VansIOAudit::Snapshot().empty(),
		"Animation Clip binary Codec did not round-trip without disk I/O"))
		return false;

	Vans::VansAssetGuid guid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"02e2a71c-88bf-4b04-8a02-e64c538a1ee2", guid),
		"Could not create the Animation Clip memory-asset GUID"))
		return false;
	Vans::VansAssetMeta meta;
	meta.guid = guid;
	meta.importer = Vans::VansAssetDatabase::ImporterFor(
		Vans::VansAssetType::AnimationClip);
	{
		Vans::VansScopedIOContext fixtureWrite(
			Vans::VansIODomain::Authoring, "ContractFixture.AnimationClipWrite", true);
		if (!Expect(
			Vans::VansFileStorage::WriteAtomicBytes(clipPath, bytes, error) &&
			Vans::VansAssetMetaStorage::SaveAtomic(
				Vans::VansAssetMeta::MetaPathFor(clipPath), meta, error),
			"Could not create the Animation Clip memory-asset fixture"))
			return false;
	}

	Vans::VansAssetDatabase database(
		assetsRoot, temporary.path / "Library" / "Artifacts");
	const Vans::VansAssetScanResult scan =
		database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
	if (!Expect(scan.errors.empty() && scan.registered == 1,
		"Could not register the Animation Clip memory-asset fixture"))
		return false;
	Vans::VansAssetObjectRepository repository;
	const Vans::VansAssetObjectBootstrapResult bootstrap =
		Vans::VansAssetObjectBootstrapper::Publish(database.All(), repository);
	if (!Expect(static_cast<bool>(bootstrap),
		bootstrap.errors.empty()
			? "Animation Clip memory bootstrap failed"
			: bootstrap.errors.front().c_str()))
		return false;

	std::error_code removeError;
	fs::remove(clipPath, removeError);
	if (!Expect(!removeError, "Could not remove the bootstrapped Animation Clip source"))
		return false;
	Vans::VansIOAudit::Reset();
	const auto memoryAsset = repository.ResolveLatest<VansAnimationClipAsset>(guid);
	if (!Expect(memoryAsset && memoryAsset->clip.clipName == clip.clipName &&
		memoryAsset->skeleton.signature == skeleton.ComputeSignature() &&
		!fs::exists(clipPath) && Vans::VansIOAudit::Snapshot().empty(),
		"Animation Clip runtime object still depends on its source file after bootstrap"))
		return false;

	fs::path sourceFbx;
	for (fs::path cursor = fs::current_path(); !cursor.empty() && sourceFbx.empty();
		cursor = cursor.parent_path())
	{
		for (const fs::path& candidate : { cursor, cursor / "ForestEngine" })
		{
			const fs::path model = candidate /
				"External/assimp/test/models-nonbsd/FBX/2013_BINARY/multiple_animations_test.fbx";
			if (fs::is_regular_file(model))
			{
				sourceFbx = model;
				break;
			}
		}
		if (cursor == cursor.root_path())
			break;
	}
	if (!Expect(!sourceFbx.empty(),
		"Animation Clip contract FBX fixture is unavailable"))
		return false;

	const fs::path runtimeModelPath = temporary.path / "MemoryAnimation.fbx";
	std::error_code copyError;
	fs::copy_file(sourceFbx, runtimeModelPath,
		fs::copy_options::overwrite_existing, copyError);
	if (!Expect(!copyError, "Could not copy the Animation Clip runtime fixture"))
		return false;

	Assimp::Importer importer;
	const aiScene* importedScene = importer.ReadFile(runtimeModelPath.string(),
		aiProcess_Triangulate | aiProcess_FlipUVs);
	if (!Expect(importedScene && importedScene->HasAnimations(),
		"Animation Clip runtime fixture has no imported animations"))
		return false;
	std::uint32_t totalVertices = 0;
	for (std::uint32_t mesh = 0; mesh < importedScene->mNumMeshes; ++mesh)
		totalVertices += importedScene->mMeshes[mesh]->mNumVertices;

	Vans::VansSkeletalMeshImportSettings importSettings;
	importSettings.sourceSkeletonGuid = guid.ToString();
	VansAnimationImportResult runtimeImport;
	Vans::VansIOAudit::Reset();
	if (!Expect(VansSkinnedMeshLoader::ProcessAnimatedMesh(
		importedScene,
		runtimeModelPath.string(),
		totalVertices,
		1.0f,
		runtimeImport,
		importSettings),
		"Runtime model animation extraction failed"))
		return false;
	bool createdClipFile = false;
	for (const fs::directory_entry& entry : fs::directory_iterator(temporary.path))
		createdClipFile = createdClipFile || entry.path().extension() == ".vclip";
	return Expect(!runtimeImport.clips.empty() && !createdClipFile &&
		Vans::VansIOAudit::Snapshot().empty(),
		"Runtime model animation extraction read or wrote serialized Animation Clip assets");
}

bool TestProjectAssetMemoryBootstrapContract(
	bool requireFreshDerivedResources = true,
	const char* projectFilter = nullptr)
{
	fs::path workspaceRoot;
	for (fs::path cursor = fs::current_path(); !cursor.empty(); cursor = cursor.parent_path())
	{
		if (fs::is_regular_file(cursor / "AnimationV2Project/ForestProject.json") &&
			fs::is_regular_file(cursor / "DemoHallProject/ForestProject.json") &&
			fs::is_regular_file(cursor / "DustV3Project/ForestProject.json") &&
			fs::is_regular_file(cursor / "SponzaProject/ForestProject.json") &&
			fs::is_regular_file(cursor / "TestV2Project/ForestProject.json"))
		{
			workspaceRoot = cursor;
			break;
		}
		if (cursor == cursor.root_path())
			break;
	}
	if (!Expect(!workspaceRoot.empty(),
		"Project memory-bootstrap workspace is unavailable"))
		return false;

	const char* projectNames[] = {
		"AnimationV2Project",
		"DemoHallProject",
		"DustV3Project",
		"SponzaProject",
		"TestV2Project"
	};
	const fs::path engineSourceRoot =
		workspaceRoot / "ForestEngine" / "ForestEngine";
	std::size_t validatedSceneCount = 0;
	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	projectManager.CloseProject();
	struct ProjectManagerCloseGuard
	{
		Vans::VansProjectManager& manager;
		~ProjectManagerCloseGuard() { manager.CloseProject(); }
	} projectManagerCloseGuard{ projectManager };
	for (const char* projectName : projectNames)
	{
		if (projectFilter && std::string(projectName) != projectFilter)
			continue;
		const fs::path projectRoot = workspaceRoot / projectName;
		Vans::VansProjectConfig projectConfig;
		if (!Expect(projectConfig.LoadFromFile(
			(projectRoot / "ForestProject.json").string()),
			(std::string(projectName) + " project config could not be loaded").c_str()))
			return false;
		Vans::VansAssetDatabase database(
			projectRoot / projectConfig.assetsRoot,
			projectRoot / projectConfig.importedArtifactRoot);
		Vans::VansAssetDatabase builtInDatabase(
			engineSourceRoot / "EngineAssets",
			projectRoot / projectConfig.importedArtifactRoot / "Engine");
		Vans::VansIOAudit::Reset();
		const Vans::VansAssetScanResult scan =
			database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
		if (!Expect(static_cast<bool>(scan),
			(std::string(projectName) + " read-only asset scan failed").c_str()))
			return false;

		std::vector<std::string> builtInErrors;
		if (!Expect(Vans::VansBuiltInAssetCatalog::RegisterAssets(
			builtInDatabase,
			engineSourceRoot,
			Vans::VansAssetOperationPolicy::ReadOnly(),
			builtInErrors),
			builtInErrors.empty()
				? (std::string(projectName) + " built-in asset scan failed").c_str()
				: builtInErrors.front().c_str()))
			return false;

		std::vector<Vans::VansAssetRecord> memoryRecords = database.All();
		const std::vector<Vans::VansAssetRecord> builtInRecords = builtInDatabase.All();
		memoryRecords.insert(
			memoryRecords.end(), builtInRecords.begin(), builtInRecords.end());
		Vans::VansAssetObjectRepository repository;
		const Vans::VansAssetObjectBootstrapResult bootstrap =
			Vans::VansAssetObjectBootstrapper::Publish(memoryRecords, repository);
		if (!Expect(static_cast<bool>(bootstrap),
			bootstrap.errors.empty()
				? (std::string(projectName) + " memory asset bootstrap failed").c_str()
				: bootstrap.errors.front().c_str()))
			return false;
		const std::optional<Vans::VansAssetRecord> baseThemeRecord =
			builtInDatabase.Find(
				engineSourceRoot / "EngineAssets" / "ui" / "Themes" / "BaseTheme.xaml");
		if (!Expect(baseThemeRecord &&
			baseThemeRecord->type == Vans::VansAssetType::UIXaml &&
			baseThemeRecord->guid.ToString() ==
				"18dcb044-c0d3-4bc6-a868-8b38785b82aa" &&
			repository.ResolveLatest<VansRuntime::VansUIXamlAsset>(
				baseThemeRecord->guid),
			(std::string(projectName) +
				" built-in BaseTheme XAML was not published to memory").c_str()))
			return false;

		const std::vector<Vans::VansIOEvent> events = Vans::VansIOAudit::Snapshot();
		const bool wroteStorage = std::any_of(
			events.begin(), events.end(), [](const Vans::VansIOEvent& event)
			{
				return event.operation == Vans::VansIOOperation::StageWrite;
			});
		if (!Expect(!wroteStorage && repository.Size() != 0,
			(std::string(projectName) +
				" project-open bootstrap wrote authoring data or published no memory assets").c_str()))
			return false;

		projectManager.CloseProject();
		projectManager.SetPackagedAssetRecords(memoryRecords);
		const Vans::VansAssetObjectBootstrapResult runtimeBootstrap =
			Vans::VansAssetObjectBootstrapper::Publish(
				memoryRecords, projectManager.GetAssetObjectRepository());
		if (!Expect(static_cast<bool>(runtimeBootstrap),
			runtimeBootstrap.errors.empty()
				? (std::string(projectName) + " runtime repository bootstrap failed").c_str()
				: runtimeBootstrap.errors.front().c_str()))
			return false;

		const fs::path scenesRoot = projectRoot / "Scenes";
		std::error_code sceneScanError;
		for (fs::directory_iterator iterator(scenesRoot, sceneScanError), end;
			!sceneScanError && iterator != end; iterator.increment(sceneScanError))
		{
			if (!iterator->is_regular_file() || iterator->path().extension() != ".json")
				continue;
			const fs::path scenePath = iterator->path();
			Vans::VansIOAudit::Reset();
			Vans::SceneDocumentLoadResult sceneLoad =
				Vans::VansSceneDocumentLoader::Load(scenePath);
			if (!Expect(static_cast<bool>(sceneLoad),
				(std::string(projectName) + " scene could not be loaded: " +
					scenePath.filename().string()).c_str()))
				return false;
			const std::vector<Vans::VansIOEvent> sceneLoadEvents =
				Vans::VansIOAudit::Snapshot();
			const std::size_t sceneReads = static_cast<std::size_t>(std::count_if(
				sceneLoadEvents.begin(), sceneLoadEvents.end(),
				[&](const Vans::VansIOEvent& event)
				{
					return event.domain == Vans::VansIODomain::Authoring &&
						event.operation == Vans::VansIOOperation::Read && event.success &&
						event.path.lexically_normal() == scenePath.lexically_normal();
				}));
			if (!Expect(sceneReads == 1,
				(std::string(projectName) + " scene was not read exactly once: " +
					scenePath.filename().string()).c_str()))
				return false;

			const Vans::VansSerializedValue sceneDocument =
				sceneLoad.document->SerializedRootSnapshot();
			Vans::VansIOAudit::Reset();
			const Vans::VansSceneAssetDependencyBuildResult dependencyResult =
				Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
					database,
					sceneDocument,
					scenePath,
					projectConfig.runtimeAssetBindings,
					repository,
					&builtInDatabase);
			Vans::VansSceneContentBuildPlan runtimePlan;
			std::string runtimeError;
			const bool projected =
				Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
					sceneDocument, projectRoot.string(), runtimePlan, runtimeError);
			const std::vector<Vans::VansIOEvent> runtimeEvents =
				Vans::VansIOAudit::Snapshot();
			const bool touchedAuthoringStorage = std::any_of(
				runtimeEvents.begin(), runtimeEvents.end(),
				[](const Vans::VansIOEvent& event)
				{
					return event.domain == Vans::VansIODomain::Authoring &&
						(event.operation == Vans::VansIOOperation::Read ||
							event.operation == Vans::VansIOOperation::StageWrite);
				});
			if (!Expect(projected && !touchedAuthoringStorage,
				(runtimeError.empty()
					? std::string(projectName) +
						" scene runtime planning touched authoring storage: " +
						scenePath.filename().string()
					: runtimeError).c_str()))
				return false;
			if (requireFreshDerivedResources &&
				!Expect(dependencyResult.success,
					(std::string(projectName) +
						" scene dependency plan rejected stale derived resources: " +
						scenePath.filename().string()).c_str()))
				return false;
			++validatedSceneCount;
		}
		if (!Expect(!sceneScanError,
			(std::string(projectName) + " scenes directory could not be scanned").c_str()))
			return false;
		projectManager.CloseProject();
	}
	return projectFilter
		? Expect(validatedSceneCount != 0,
			"Filtered scene memory-runtime contract covered no scenes")
		: Expect(validatedSceneCount == 6,
			"Workspace scene memory-runtime contract did not cover all six scenes");
}

bool TestVegetationMemoryAssetContract()
{
	TemporaryDirectory temporary;
	const fs::path assetsRoot = temporary.path / "Assets";
	const fs::path artifactRoot = temporary.path / "Library" / "Artifacts";
	const fs::path vegetationPath = assetsRoot / "Vegetation" / "MemoryVegetation.json";
	fs::create_directories(vegetationPath.parent_path());

	Vans::VansAssetGuid vegetationGuid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"5d5d1201-1ed8-4f3e-8cc6-641c5b1d9999", vegetationGuid),
		"Could not create the vegetation memory-asset GUID fixture"))
		return false;

	Vans::VansVegetationConfigAsset vegetationAsset;
	vegetationAsset.config.name = "MemoryVegetation";
	Vans::VansAssetMeta meta;
	meta.guid = vegetationGuid;
	meta.importer = Vans::VansAssetDatabase::ImporterFor(
		Vans::VansAssetType::VegetationConfig);
	std::string error;
	{
		Vans::VansScopedIOContext fixtureWrite(
			Vans::VansIODomain::Authoring, "ContractFixture.VegetationWrite", true);
		if (!Expect(
			Vans::VansVegetationConfigStorage::SaveAtomic(
				vegetationPath, vegetationAsset.config, error) &&
			Vans::VansAssetMetaStorage::SaveAtomic(
				Vans::VansAssetMeta::MetaPathFor(vegetationPath), meta, error),
			"Could not create the vegetation memory-asset fixture"))
			return false;
	}

	Vans::VansAssetDatabase database(assetsRoot, artifactRoot);
	const Vans::VansAssetScanResult scan =
		database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
	if (!Expect(scan.errors.empty() && scan.registered == 1,
		"Could not register the vegetation memory-asset fixture"))
		return false;

	using Value = Vans::VansSerializedValue;
	const Value sceneRoot = Value::Object({
		{ "schemaVersion", Value::Int(Vans::VansSceneSchemaVersion) },
		{ "sceneGuid", Value::String("4e89a312-9a1e-4dc3-8e04-4e06f33077a1") },
		{ "name", Value::String("VegetationMemoryAsset") },
		{ "settings", Value::Object({
			{ "environment", BuildValidEnvironmentSettingsForTest() },
			{ "vegetation", Value::Object({
				{ "asset", Value::Object({
					{ "guid", Value::String(vegetationGuid.ToString()) }
				}) }
			}) }
		}) },
		{ "entities", Value::Array({}) }
	});

	Vans::VansProjectManager& manager = Vans::VansProjectManager::Get();
	manager.CloseProject();
	struct RepositoryGuard
	{
		Vans::VansProjectManager& manager;
		~RepositoryGuard() { manager.CloseProject(); }
	} repositoryGuard{ manager };
	const Vans::VansAssetObjectBootstrapResult bootstrap =
		Vans::VansAssetObjectBootstrapper::Publish(
			database.All(), manager.GetAssetObjectRepository());
	if (!Expect(static_cast<bool>(bootstrap),
		bootstrap.errors.empty()
			? "Vegetation project-open bootstrap failed"
			: bootstrap.errors.front().c_str()))
		return false;

	const Vans::VansSceneAssetDependencyBuildResult dependencyResult =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			sceneRoot,
			temporary.path / "Scenes" / "MemoryScene.json",
			{},
			manager.GetAssetObjectRepository());
	if (!Expect(dependencyResult.success &&
		manager.GetAssetObjectRepository().ResolveLatest<
			Vans::VansVegetationConfigAsset>(vegetationGuid) != nullptr,
		"Vegetation dependency planning did not consume the project memory object"))
		return false;

	std::error_code removeError;
	fs::remove(vegetationPath, removeError);
	if (!Expect(!removeError && !fs::exists(vegetationPath),
		"Could not remove the vegetation disk source before runtime projection"))
		return false;

	Vans::VansIOAudit::Reset();
	Vans::VansSceneContentBuildPlan plan;
	const bool projected = Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
		sceneRoot, temporary.path.string(), plan, error);
	if (!Expect(projected, error.c_str()))
		return false;
	const auto projectionIOEvents = Vans::VansIOAudit::Snapshot();
	return Expect(
		plan.vegetation.has_value() &&
		plan.vegetation->name == "MemoryVegetation" &&
		std::none_of(
			projectionIOEvents.begin(), projectionIOEvents.end(),
			[&](const Vans::VansIOEvent& event)
			{
				return event.operation == Vans::VansIOOperation::Read &&
					event.path.lexically_normal() == vegetationPath.lexically_normal();
			}),
		"Runtime vegetation projection reread the deleted authoring asset instead of memory");
}

bool TestUIAssetMemoryBootstrapContract()
{
	TemporaryDirectory temporary;
	const fs::path screenPath = temporary.path / "MemoryScreen.vui.json";
	const fs::path xamlPath = temporary.path / "MemoryScreen.xaml";
	Vans::VansAssetGuid screenGuid;
	Vans::VansAssetGuid xamlGuid;
	Vans::VansAssetGuid decoyGuid;
	if (!Expect(
		Vans::VansAssetGuid::TryParse("10000000-0000-4000-8000-000000000001", screenGuid) &&
		Vans::VansAssetGuid::TryParse("10000000-0000-4000-8000-000000000002", xamlGuid) &&
		Vans::VansAssetGuid::TryParse("10000000-0000-4000-8000-000000000003", decoyGuid),
		"UI memory contract GUID constants are invalid"))
		return false;

	const std::string screenBytes =
		"{\n"
		"  \"schemaVersion\": 1,\n"
		"  \"guid\": \"memory.screen\",\n"
		"  \"name\": \"MemoryScreen\",\n"
		"  \"diagnosticCorrelationId\": \"" + decoyGuid.ToString() + "\",\n"
		"  \"xaml\": { \"guid\": \"" + xamlGuid.ToString() + "\" },\n"
		"  \"themes\": [], \"tokens\": [], \"localization\": []\n"
		"}\n";
	const std::string xamlBytes =
		"<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"/>";
	std::string error;
	if (!Expect(
		Vans::VansFileStorage::WriteAtomicBytes(screenPath, screenBytes, error) &&
		Vans::VansFileStorage::WriteAtomicBytes(xamlPath, xamlBytes, error),
		error.c_str()))
		return false;

	Vans::VansAssetRecord screenRecord;
	screenRecord.guid = screenGuid;
	screenRecord.type = Vans::VansAssetType::UIScreen;
	screenRecord.state = Vans::VansAssetState::CpuReady;
	screenRecord.sourcePath = screenPath;
	screenRecord.sourceHash = 0x1001u;
	Vans::VansAssetRecord xamlRecord;
	xamlRecord.guid = xamlGuid;
	xamlRecord.type = Vans::VansAssetType::UIXaml;
	xamlRecord.state = Vans::VansAssetState::CpuReady;
	xamlRecord.sourcePath = xamlPath;
	xamlRecord.sourceHash = 0x1002u;
	Vans::VansAssetRecord decoyRecord;
	decoyRecord.guid = decoyGuid;
	decoyRecord.type = Vans::VansAssetType::Model;
	decoyRecord.state = Vans::VansAssetState::CpuReady;
	decoyRecord.sourcePath = temporary.path / "IndexedDecoy.obj";
	decoyRecord.sourceHash = 0x1003u;

	auto& projectManager = Vans::VansProjectManager::Get();
	projectManager.CloseProject();
	projectManager.SetPackagedAssetRecords({ screenRecord, xamlRecord });
	struct ScopedPackagedUIAssets
	{
		~ScopedPackagedUIAssets()
		{
			Vans::VansProjectManager::Get().SetPackagedAssetRecords({});
			Vans::VansProjectManager::Get().GetAssetObjectRepository().Clear();
		}
	} scopedAssets;
	const Vans::VansAssetObjectBootstrapResult bootstrap =
		Vans::VansAssetObjectBootstrapper::Publish(
			{ screenRecord, xamlRecord }, projectManager.GetAssetObjectRepository(),
			{ decoyRecord });
	if (!Expect(static_cast<bool>(bootstrap),
		bootstrap.errors.empty() ? "UI memory bootstrap failed" : bootstrap.errors.front().c_str()))
		return false;

	Vans::VansAssetObjectSnapshotInfo screenInfo;
	if (!Expect(
		projectManager.GetAssetObjectRepository().FindInfo(screenGuid, screenInfo) &&
		screenInfo.dependencies.size() == 1 && screenInfo.dependencies.front() == xamlGuid &&
		std::find(screenInfo.dependencies.begin(), screenInfo.dependencies.end(), decoyGuid) ==
			screenInfo.dependencies.end(),
		"UI memory snapshot did not isolate declared references from diagnostic GUID data"))
		return false;

	std::error_code removeError;
	fs::remove(screenPath, removeError);
	fs::remove(xamlPath, removeError);
	if (!Expect(!fs::exists(screenPath) && !fs::exists(xamlPath),
		"UI memory contract could not remove authoring sources"))
		return false;

	Vans::VansIOAudit::Reset();
	std::shared_ptr<const VansRuntime::VansUIAssetDocument> document;
	std::string xamlUri;
	if (!Expect(
		VansRuntime::VansUIAssetResolver::ResolveDocument(
			screenGuid.ToString(), Vans::VansAssetType::UIScreen, document, error) &&
		VansRuntime::VansUIAssetResolver::ResolveXamlUri(
			xamlGuid.ToString(), xamlUri, error) &&
		xamlUri == "asset://" + xamlGuid.ToString() &&
		document &&
		projectManager.GetAssetObjectRepository().ResolveLatest<VansRuntime::VansUIXamlAsset>(xamlGuid),
		error.c_str()))
		return false;
	const auto events = Vans::VansIOAudit::Snapshot();
	return Expect(
		std::none_of(events.begin(), events.end(), [](const Vans::VansIOEvent& event)
		{
			return event.operation == Vans::VansIOOperation::Read ||
				event.operation == Vans::VansIOOperation::ReadRange;
		}),
		"Runtime UI asset resolution performed disk I/O after memory bootstrap");
}

bool TestSerializedDependencySchemaContract()
{
	Vans::VansAssetGuid owner;
	Vans::VansAssetGuid texture;
	Vans::VansAssetGuid shader;
	Vans::VansAssetGuid skin;
	Vans::VansAssetGuid decoy;
	if (!Expect(
		Vans::VansAssetGuid::TryParse("30000000-0000-4000-8000-000000000001", owner) &&
		Vans::VansAssetGuid::TryParse("30000000-0000-4000-8000-000000000002", texture) &&
		Vans::VansAssetGuid::TryParse("30000000-0000-4000-8000-000000000003", shader) &&
		Vans::VansAssetGuid::TryParse("30000000-0000-4000-8000-000000000004", skin) &&
		Vans::VansAssetGuid::TryParse("30000000-0000-4000-8000-000000000005", decoy),
		"Serialized dependency schema GUID constants are invalid"))
		return false;

	Vans::VansAssetRecord record;
	record.guid = owner;
	record.type = Vans::VansAssetType::Material;
	record.state = Vans::VansAssetState::CpuReady;
	record.sourcePath = "MemoryMaterial.mat";
	record.authoringPath = record.sourcePath;
	const Vans::VansSerializedValue root = Vans::VansSerializedValue::Object({
		{ "schemaVersion", Vans::VansSerializedValue::Int(1) },
		{ "guid", Vans::VansSerializedValue::String(owner.ToString()) },
		{ "materialType", Vans::VansSerializedValue::String("customShader") },
		{ "importSource", Vans::VansSerializedValue::Object({
			{ "model", Vans::VansSerializedValue::String(decoy.ToString()) },
			{ "sourceMaterial", Vans::VansSerializedValue::String(decoy.ToString()) } }) },
		{ "shader", Vans::VansSerializedValue::Object({
			{ "guid", Vans::VansSerializedValue::String(shader.ToString()) } }) },
		{ "textures", Vans::VansSerializedValue::Object({
			{ "basecolor", Vans::VansSerializedValue::Object({
				{ "guid", Vans::VansSerializedValue::String(texture.ToString()) } }) } }) },
		{ "parameters", Vans::VansSerializedValue::Object({
			{ "skinProfile", Vans::VansSerializedValue::Object({
				{ "guid", Vans::VansSerializedValue::String(skin.ToString()) } }) } }) }
	});
	Vans::VansAssetObjectRepository repository;
	std::string error;
	if (!Expect(Vans::VansAssetObjectBootstrapper::PublishSerialized(
		record, root, 0x3001u, repository, error), error.c_str()))
		return false;
	Vans::VansAssetObjectSnapshotInfo info;
	if (!Expect(repository.FindInfo(owner, info),
		"Serialized material dependency snapshot is missing"))
		return false;
	const auto contains = [&](Vans::VansAssetGuid guid)
	{
		return std::find(info.dependencies.begin(), info.dependencies.end(), guid) !=
			info.dependencies.end();
	};
	return Expect(info.dependencies.size() == 3 && contains(texture) && contains(shader) &&
		contains(skin) && !contains(owner) && !contains(decoy),
		"Material dependency snapshot mixed provenance GUIDs with declared asset references");
}

bool TestAssetWorkingCopyMemoryPublicationContract()
{
	if (!TestSerializedDependencySchemaContract()) return false;
	TemporaryDirectory temporary;
	const fs::path sourcePath = temporary.path / "WorkingSnapshot.vaudiosnapshot";
	const fs::path metaPath = Vans::VansAssetMeta::MetaPathFor(sourcePath);
	Vans::VansAssetGuid guid;
	if (!Expect(Vans::VansAssetGuid::TryParse(
		"20000000-0000-4000-8000-000000000001", guid),
		"Working-copy contract GUID is invalid"))
		return false;

	Vans::VansAudioBusSnapshotAsset fixture;
	fixture.guid = guid.ToString();
	fixture.displayName = "Working Snapshot";
	fixture.snapshot.buses.push_back({ "Master", 0.5f });
	Vans::VansAssetMeta meta;
	meta.guid = guid;
	meta.importer = Vans::VansAssetDatabase::ImporterFor(
		Vans::VansAssetType::AudioBusSnapshot);
	std::string error;
	{
		Vans::VansScopedIOContext fixtureWrite(
			Vans::VansIODomain::Authoring, "ContractFixture.WorkingCopyWrite", true);
		if (!Expect(
			Vans::VansAudioBusSnapshotAssetStorage::SaveAtomic(
				sourcePath, fixture, error) &&
			Vans::VansAssetMetaStorage::SaveAtomic(metaPath, meta, error),
			error.c_str()))
			return false;
	}

	Vans::VansAssetRecord record;
	record.guid = guid;
	record.type = Vans::VansAssetType::AudioBusSnapshot;
	record.state = Vans::VansAssetState::CpuReady;
	record.sourcePath = sourcePath;
	record.metaPath = metaPath;
	record.sourceHash = 0x2001u;
	record.metaHash = 0x2002u;
	Vans::VansAssetObjectRepository repository;
	const Vans::VansAssetObjectBootstrapResult bootstrap =
		Vans::VansAssetObjectBootstrapper::Publish({ record }, repository);
	if (!Expect(static_cast<bool>(bootstrap),
		bootstrap.errors.empty() ? "Working-copy bootstrap failed" : bootstrap.errors.front().c_str()))
		return false;

	auto& documentRegistry = Vans::VansAssetDocumentRegistry::Get();
	documentRegistry.Clear();
	struct RegistryGuard
	{
		~RegistryGuard()
		{
			Vans::VansAssetDocumentEditService::ClearAllHistories();
			auto& registry = Vans::VansAssetDocumentRegistry::Get();
			registry.ClearWorkingCopyPublisher();
			registry.Clear();
		}
	} registryGuard;
	const std::shared_ptr<Vans::VansOpenAssetDocument> documents =
		documentRegistry.GetOrOpen(sourcePath);
	if (!Expect(documents && documents->sourceDocument.IsLoaded() &&
		documents->metaDocument.IsLoaded(),
		"Working-copy authoring documents did not open"))
		return false;

	documentRegistry.SetWorkingCopyPublisher(
		[&](const Vans::VansOpenAssetDocument& openDocument, std::string& publishError)
		{
			const std::string sourceJson =
				Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
					openDocument.sourceDocument.SerializedRootSnapshot()).dump();
			const std::string metaJson =
				Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
					openDocument.metaDocument.SerializedRootSnapshot()).dump();
			Vans::VansAssetRecord workingRecord = record;
			workingRecord.sourceHash = Vans::ComputeMemoryFnv1a64(
				sourceJson.data(), sourceJson.size());
			workingRecord.metaHash = Vans::ComputeMemoryFnv1a64(
				metaJson.data(), metaJson.size());
			const std::uint64_t workingHash =
				Vans::AssetObjectContentHash(workingRecord);
			if (!Vans::VansAssetObjectBootstrapper::PublishSerialized(
				record,
				openDocument.sourceDocument.SerializedRootSnapshot(),
				workingHash,
				repository,
				publishError))
				return false;
			return Vans::VansAssetObjectBootstrapper::PublishMetadataSerialized(
				record,
				openDocument.metaDocument.SerializedRootSnapshot(),
				workingHash,
				repository,
				publishError);
		});

	std::string diskBefore;
	if (!Expect(Vans::VansFileStorage::ReadAllBytes(sourcePath, diskBefore, error), error.c_str()))
		return false;
	Vans::VansAssetObjectSnapshotInfo initialInfo;
	if (!Expect(repository.FindInfo(guid, initialInfo),
		"Working-copy initial repository snapshot is missing"))
		return false;

	Vans::VansIOAudit::Reset();
	const Vans::AssetDocumentEditResult gainEdit =
		Vans::VansAssetDocumentEditService::Set(
			documents->sourceDocument,
			Vans::MakeDocumentPropertyPath(
				Vans::DocumentPropertySpace::AssetSource, "/buses/0/gain"),
			Vans::VansSerializedValue::Float(0.2));
	Vans::VansAssetObjectSnapshotInfo editedInfo;
	const std::shared_ptr<const Vans::VansAudioBusSnapshotAsset> edited =
		repository.ResolveLatest<Vans::VansAudioBusSnapshotAsset>(guid);
	const auto editEvents = Vans::VansIOAudit::Snapshot();
	std::string diskAfterEdit;
	Vans::VansFileStorage::ReadAllBytes(sourcePath, diskAfterEdit, error);
	if (!Expect(gainEdit && documents->sourceDocument.IsDirty() && edited &&
		!edited->snapshot.buses.empty() &&
		std::abs(edited->snapshot.buses.front().gain - 0.2f) <= 0.0001f &&
		repository.FindInfo(guid, editedInfo) &&
		editedInfo.generation > initialInfo.generation &&
		diskAfterEdit == diskBefore && editEvents.empty(),
		"A valid authoring edit did not publish only to the memory repository"))
		return false;

	const std::uint64_t lastGoodGeneration = editedInfo.generation;
	const Vans::AssetDocumentEditResult invalidEdit =
		Vans::VansAssetDocumentEditService::Set(
			documents->sourceDocument,
			Vans::MakeDocumentPropertyPath(
				Vans::DocumentPropertySpace::AssetSource, "/buses"),
			Vans::VansSerializedValue::String("invalid"));
	Vans::VansAssetObjectSnapshotInfo invalidInfo;
	const std::shared_ptr<const Vans::VansAudioBusSnapshotAsset> lastGood =
		repository.ResolveLatest<Vans::VansAudioBusSnapshotAsset>(guid);
	if (!Expect(invalidEdit && !documents->lastError.empty() && lastGood &&
		!lastGood->snapshot.buses.empty() &&
		std::abs(lastGood->snapshot.buses.front().gain - 0.2f) <= 0.0001f &&
		repository.FindInfo(guid, invalidInfo) &&
		invalidInfo.generation == lastGoodGeneration,
		"An invalid intermediate edit replaced the last-good runtime snapshot"))
		return false;

	if (!Expect(Vans::VansAssetDocumentEditService::Undo(
		documents->sourceDocument) && documents->lastError.empty(),
		"Undo did not restore a publishable working snapshot"))
		return false;
	if (!Expect(static_cast<bool>(Vans::VansAssetDocumentEditService::Undo(
		documents->sourceDocument)),
		"Undo did not return the working document to its saved state"))
		return false;
	const std::shared_ptr<const Vans::VansAudioBusSnapshotAsset> restored =
		repository.ResolveLatest<Vans::VansAudioBusSnapshotAsset>(guid);
	std::string diskAfterUndo;
	Vans::VansFileStorage::ReadAllBytes(sourcePath, diskAfterUndo, error);
	return Expect(!documents->sourceDocument.IsDirty() && restored &&
		!restored->snapshot.buses.empty() &&
		std::abs(restored->snapshot.buses.front().gain - 0.5f) <= 0.0001f &&
		diskAfterUndo == diskBefore,
		"Undo did not restore memory state without changing the authoring file");
}

bool TestGeneratedMaterialMemoryBoundaryContract()
{
	TemporaryDirectory temporary;
	const fs::path assetsRoot = temporary.path / "Assets";
	fs::create_directories(assetsRoot);
	Vans::VansAssetDatabase database(
		assetsRoot, temporary.path / "Library" / "Artifacts");

	VkDevice device = VK_NULL_HANDLE;
	VansGraphics::VansRenderNode node(device, VansGraphics::OPAQUE_NODE);
	VansGraphics::VansPBRMaterial material;
	material.m_AssetName = "MemoryBoundary";
	material.m_MaterialType = VansGraphics::VAN_PBR;
	material.m_BasePBRParam.m_albedo = glm::vec3(0.2f, 0.4f, 0.6f);
	material.m_BasePBRParam.m_metallic = 0.1f;
	material.m_BasePBRParam.m_roughness = 0.7f;
	material.m_BasePBRParam.m_ao = 1.0f;
	node.m_Material = &material;
	node.m_SubmeshIndex = 3u;

	auto& documentRegistry = Vans::VansAssetDocumentRegistry::Get();
	documentRegistry.Clear();
	struct RegistryGuard
	{
		~RegistryGuard()
		{
			Vans::VansAssetDocumentEditService::ClearAllHistories();
			Vans::VansAssetDocumentRegistry::Get().Clear();
		}
	} registryGuard;

	Vans::VansIOAudit::Reset();
	const Vans::EditorAPI::RuntimeGeneratedMaterialDraft draft =
		Vans::EditorAPI::BuildRuntimeGeneratedMaterialDraft(
			"MemoryBoundaryModel", &node, database);
	if (!Expect(draft.requiresSave && !draft.guid.empty() &&
		!draft.sourcePath.empty() && !draft.sourceCanonicalJson.empty() &&
		!draft.metaCanonicalJson.empty(),
		"Runtime-generated material did not produce an in-memory authoring draft"))
		return false;
	const fs::path sourcePath = draft.sourcePath;
	const fs::path metaPath = Vans::VansAssetMeta::MetaPathFor(sourcePath);
	if (!Expect(!fs::exists(sourcePath) && !fs::exists(metaPath) &&
		Vans::VansIOAudit::Snapshot().empty(),
		"Generating a runtime material draft touched authoring storage"))
		return false;

	std::string error;
	Vans::VansAssetGuid guid;
	if (!Expect(Vans::VansAssetGuid::TryParse(draft.guid, guid),
		"Generated material draft GUID is invalid"))
		return false;
	const Vans::VansSerializedValue sourceRoot =
		Vans::DecodeSerializedValueJson(
			nlohmann::ordered_json::parse(draft.sourceCanonicalJson));
	const Vans::VansSerializedValue metaRoot =
		Vans::DecodeSerializedValueJson(
			nlohmann::ordered_json::parse(draft.metaCanonicalJson));
	const auto documents = documentRegistry.CreateInMemory(
		sourcePath, sourceRoot, metaRoot, true, error);
	if (!Expect(documents && documents->IsDirty() &&
		documentRegistry.SceneOwnedDirtyDocuments().size() == 1,
		error.empty() ? "Generated material was not registered as a scene-owned dirty document"
			: error.c_str()))
		return false;

	Vans::VansAssetRecord record;
	record.guid = guid;
	record.type = Vans::VansAssetType::Material;
	record.state = Vans::VansAssetState::CpuReady;
	record.sourcePath = sourcePath;
	record.metaPath = metaPath;
	record.sourceHash = Vans::ComputeMemoryFnv1a64(
		draft.sourceCanonicalJson.data(), draft.sourceCanonicalJson.size());
	record.metaHash = Vans::ComputeMemoryFnv1a64(
		draft.metaCanonicalJson.data(), draft.metaCanonicalJson.size());
	record.memoryOnly = true;
	Vans::VansAssetObjectRepository repository;
	const std::uint64_t contentHash = Vans::AssetObjectContentHash(record);
	if (!Expect(
		Vans::VansAssetObjectBootstrapper::PublishSerialized(
			record, sourceRoot, contentHash, repository, error) &&
		Vans::VansAssetObjectBootstrapper::PublishMetadataSerialized(
			record, metaRoot, contentHash, repository, error) &&
		database.RegisterMemoryAsset(record, error),
		error.empty() ? "Generated material could not publish as a memory asset"
			: error.c_str()))
		return false;
	const auto indexed = database.Find(guid);
	if (!Expect(indexed && indexed->memoryOnly &&
		repository.ResolveLatest<Vans::VansMaterialAuthoringAsset>(guid) &&
		!fs::exists(sourcePath) && !fs::exists(metaPath) &&
		Vans::VansIOAudit::Snapshot().empty(),
		"Generated material memory publication performed disk I/O or lost its memory-only state"))
		return false;

	Vans::VansAssetDocumentSaveStage sourceStage;
	Vans::VansAssetDocumentSaveStage metaStage;
	Vans::VansStagedFileTransaction transaction;
	if (!Expect(
		documents->sourceDocument.StageSave(sourceStage, error) &&
		documents->metaDocument.StageSave(metaStage, error),
		error.empty() ? "Generated material could not stage its explicit save"
			: error.c_str()))
		return false;
	transaction.Add({ sourceStage.targetPath, sourceStage.temporaryPath });
	transaction.Add({ metaStage.targetPath, metaStage.temporaryPath });
	if (!Expect(transaction.Publish(error) &&
		documents->sourceDocument.AdoptStagedSave(sourceStage, error) &&
		documents->metaDocument.AdoptStagedSave(metaStage, error),
		error.empty() ? "Generated material explicit save did not publish atomically"
			: error.c_str()))
		return false;
	return Expect(fs::is_regular_file(sourcePath) && fs::is_regular_file(metaPath) &&
		!documents->IsDirty(),
		"Generated material did not reach disk only at the explicit save boundary");
}

bool TestRuntimeConfigurationMemoryBoundaryContract()
{
	TemporaryDirectory temporaryDirectory;
	const fs::path syntheticEngineRoot = temporaryDirectory.path / "Engine";
	const fs::path syntheticExecutableDirectory = syntheticEngineRoot / "bin" / "Debug";
	fs::create_directories(syntheticEngineRoot / "EngineAssets");
	fs::create_directories(syntheticExecutableDirectory);
	std::string discoveredRoot;
	std::string pathError;
	if (!Expect(
		Vans::VansEnginePaths::FindEngineRoot(
			syntheticExecutableDirectory / "ForestContractTests.exe", discoveredRoot, pathError) &&
		fs::weakly_canonical(fs::path(discoveredRoot)) == fs::weakly_canonical(syntheticEngineRoot),
		pathError.empty() ? "Engine-root discovery did not select the nearest EngineAssets owner"
			: pathError.c_str()))
		return false;
	std::string rejectedRoot;
	if (!Expect(
		!Vans::VansEnginePaths::NormalizeEngineRoot(
			temporaryDirectory.path / "MissingEngineAssets", rejectedRoot, pathError) &&
		!pathError.empty(),
		"Engine-root validation accepted a directory without EngineAssets"))
		return false;

	fs::path sourceFile = fs::path(__FILE__);
	if (sourceFile.is_relative()) sourceFile = fs::absolute(sourceFile);
	const fs::path engineRoot = sourceFile.parent_path().parent_path().parent_path();
	const fs::path coreRoot = engineRoot / "Source" / "EngineCore";
	const std::vector<fs::path> runtimeRoots = {
		coreRoot / "AICore",
		coreRoot / "AnimationCore",
		coreRoot / "AudioCore",
		coreRoot / "CameraCore",
		coreRoot / "EventCore",
		coreRoot / "GameplayActionAdapters",
		coreRoot / "GameplayActionCore",
		coreRoot / "GameplayActionExecution",
		coreRoot / "GameplayActionSchema",
		coreRoot / "GameplayActionTimeline",
		coreRoot / "GameplayAttributes",
		coreRoot / "GameplayCues",
		coreRoot / "GameplayEffects",
		coreRoot / "GameplayTags",
		coreRoot / "GameplayTargeting",
		coreRoot / "NavigationCore",
		coreRoot / "ParticleCore",
		coreRoot / "PhysicsCore",
		coreRoot / "RenderCore",
		coreRoot / "RuntimeUI",
		coreRoot / "SceneCore",
		coreRoot / "SceneRuntime",
		coreRoot / "ScriptCore",
		coreRoot / "Timeline",
		coreRoot / "TimelineCore",
		coreRoot / "TimelineRuntime"
	};
	const std::vector<std::string> forbiddenCalls = {
		"IO::Load(",
		"VansJsonFileStorage::Read",
		"VansFileStorage::ReadAllBytes",
		"VansFileStorage::ReadAllText",
		"VansUIDocumentLoader::Load(",
		"VansTimelineSerialization::Load(",
		"std::ifstream",
		"IO::Save(",
		"SaveAtomic(",
		"VansJsonFileStorage::Write",
		"VansFileStorage::WriteAtomic",
		"std::ofstream"
	};
	const std::vector<std::string> excludedPathFragments = {
		"/Storage/",
		"/Serialization/",
		"/ReflectionProbeCore/",
		"/VulkanCore/"
	};
	const std::vector<std::string> persistenceFiles = {
		"VansAIBehaviorAsset.cpp", "VansAIBehaviorAsset.h",
		"VansAnimationClip.cpp", "VansAnimationClip.h",
		"VansAnimatorIO.cpp", "VansAnimatorIO.h",
		"VansAssetObjectBootstrapper.cpp", "VansAssetObjectBootstrapper.h",
		"VansAudioMixConfig.cpp", "VansAudioMixConfig.h",
		"VansGAFProjectConfiguration.cpp", "VansGAFProjectConfiguration.h",
		"VansGameplayAssetStorage.cpp", "VansGameplayAssetStorage.h",
		"VansIESProfile.cpp",
		"VansNavigationMesh.cpp", "VansNavigationMesh.h",
		"VansNoesisProviders.cpp",
		"VansPackagedResourcePlan.cpp", "VansPackagedResourcePlan.h",
		"VansSceneDocumentLoader.cpp", "VansSceneDocumentLoader.h",
		"VansTimelineSerialization.cpp", "VansTimelineSerialization.h"
	};
	auto readText = [](const fs::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		return std::string(
			std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	};
	for (const fs::path& root : runtimeRoots)
	{
		std::error_code scanError;
		for (fs::recursive_directory_iterator iterator(root, scanError), end;
			!scanError && iterator != end; iterator.increment(scanError))
		{
			if (!iterator->is_regular_file()) continue;
			const std::string extension = iterator->path().extension().string();
			if (extension != ".cpp" && extension != ".h" && extension != ".hpp") continue;
			const std::string relative = fs::relative(iterator->path(), coreRoot).generic_string();
			if (std::any_of(excludedPathFragments.begin(), excludedPathFragments.end(),
				[&](const std::string& fragment) { return relative.find(fragment) != std::string::npos; }))
				continue;
			const std::string fileName = iterator->path().filename().string();
			if (std::find(persistenceFiles.begin(), persistenceFiles.end(), fileName)
				!= persistenceFiles.end())
				continue;

			const std::string source = readText(iterator->path());
			for (const std::string& forbidden : forbiddenCalls)
			{
				if (source.find(forbidden) == std::string::npos) continue;
				std::cerr << "[RuntimeConfigBoundary] " << relative
					<< " contains forbidden authoring I/O token '" << forbidden << "'\n";
				return Expect(false,
					"A runtime configuration consumer directly accesses authoring storage");
			}
		}
		if (!Expect(!scanError,
			"Unable to scan a runtime module for authoring storage dependencies"))
			return false;
	}

	const fs::path noesisProvider = coreRoot / "RuntimeUI" / "Private" /
		"Noesis" / "VansNoesisProviders.cpp";
	const std::string noesisSource = readText(noesisProvider);
	const std::size_t xamlBegin = noesisSource.find("VansNoesisXamlProvider::LoadXaml");
	const std::size_t xamlEnd = noesisSource.find("VansNoesisTextureProvider::", xamlBegin);
	if (!Expect(xamlBegin != std::string::npos && xamlEnd != std::string::npos,
		"Noesis XAML provider boundary could not be located"))
		return false;
	const std::string xamlSection = noesisSource.substr(xamlBegin, xamlEnd - xamlBegin);
	if (!Expect(
		xamlSection.find("ResolveLatest<VansUIXamlAsset>") != std::string::npos &&
		xamlSection.find("ReadFileToBuffer") == std::string::npos &&
		xamlSection.find("ReadAllBytes") == std::string::npos,
		"Noesis XAML loading is not exclusively backed by the UI memory repository"))
		return false;

	const std::string dependencyHeader = readText(
		coreRoot / "SceneCore" / "VansSceneAssetDependencyBuilder.h");
	if (!Expect(
		dependencyHeader.find("const VansAssetObjectRepository& objectRepository")
			!= std::string::npos &&
		dependencyHeader.find("VansAssetObjectRepository* objectRepository")
			== std::string::npos,
		"Scene dependency planning does not require an in-memory asset repository"))
		return false;

	const std::string packageEntry = readText(
		engineRoot / "Source" / "Application" / "ForestPackageToolEntry.cpp");
	const std::string packageRequest = readText(
		coreRoot / "PackagingCore" / "VansGamePackageBuilder.h");
	const std::string packageBuilder = readText(
		coreRoot / "PackagingCore" / "VansGamePackageBuilder.cpp");
	return Expect(
		packageEntry.find("--legacy-assets") == std::string::npos &&
		packageEntry.find("--no-cooked-resource-plan") == std::string::npos &&
		packageRequest.find("useCookedResourcePlan") == std::string::npos &&
		packageBuilder.find("database.Scan(Vans::VansAssetOperationPolicy::Authoring())")
			== std::string::npos,
		"Packaging still exposes a second non-repository authoring path");
}

bool TestVolumetricParticleInjectionContract()
{
	using VansGraphics::VansParticleAsset;
	using VansGraphics::VansParticleAssetJsonCodec;
	using ParticleTestJson = nlohmann::json;
	const auto decodeParticle = [](const ParticleTestJson& root,
		const std::filesystem::path& path, VansParticleAsset& asset, std::string& error)
	{
		return VansParticleAssetJsonCodec::Decode(
			Vans::DecodeSerializedValueJson(root), path, asset, error);
	};
	const auto encodeParticle = [](const VansParticleAsset& asset)
	{
		return Vans::EncodeSerializedValueJson<ParticleTestJson>(
			VansParticleAssetJsonCodec::Encode(asset));
	};
	ParticleTestJson surfaceDefinition = {
		{ "name", "SurfaceParticle" },
		{ "global", {
			{ "duration", 5.0 }, { "loop", true }, { "prewarm", false },
			{ "emissionFrame", "World" } } },
		{ "emitters", ParticleTestJson::array({ {
			{ "name", "Emitter" }, { "enabled", true }, { "maxParticles", 16 },
			{ "spawn", { { "type", "RateOverTime" }, { "rate", 10.0 } } },
			{ "initialize", ParticleTestJson::array({
				{ { "module", "InitLifetime" },
				  { "lifetime", { { "mode", "Constant" }, { "value", 2.0 } } } },
				{ { "module", "InitSize" },
				  { "size", { { "mode", "Constant" }, { "value", 2.0 } } } },
				{ { "module", "InitColor" },
				  { "color", ParticleTestJson::array({ 1.0, 1.0, 1.0, 1.0 }) } }
			}) },
			{ "update", ParticleTestJson::array() },
			{ "renderer", {
				{ "type", "Billboard" }, { "simulationOrder", "Stable" },
				{ "renderSortMode", "None" } } }
		} }) }
	};

	std::string error;
	auto surfaceAssetOwner = std::make_shared<VansParticleAsset>();
	auto& surfaceAsset = *surfaceAssetOwner;
	if (!Expect(decodeParticle(
		surfaceDefinition, "surfaceDefinition.particle", surfaceAsset, error),
		"A surfaceDefinition particle asset without renderer.volumetric was rejected") ||
		!Expect(surfaceAsset.m_Emitters.size() == 1u &&
			!surfaceAsset.m_Emitters[0]->m_RendererConfig.m_Volumetric.m_Enabled,
			"Surface particle assets did not default volumetric injection to disabled"))
		return false;
	VansGraphics::VansParticleRuntime surfaceRuntime;
	surfaceRuntime.SetAsset(surfaceAssetOwner);
	surfaceRuntime.Play();
	surfaceRuntime.Update(0.11f);
	surfaceRuntime.SwapBuffers();
	if (!Expect(!surfaceRuntime.GetRenderBuffer().empty() &&
		surfaceRuntime.GetVolumetricRenderBuffer().empty() &&
		!surfaceRuntime.HasVolumetricInjectionEnabled(),
		"The disabled volumetric option changed the surfaceDefinition surface-particle path"))
		return false;

	ParticleTestJson volumetric = surfaceDefinition;
	volumetric["name"] = "VolumetricParticle";
	volumetric["emitters"][0]["renderer"]["type"] = "None";
	volumetric["emitters"][0]["renderer"]["volumetric"] = {
		{ "enabled", true },
		{ "radiusScale", 1.25 }, { "maxDistanceMeters", 80.0 },
		{ "densityMultiplier", 0.8 }, { "extinctionPerMeter", 1.5 },
		{ "singleScatteringAlbedo", { 0.7, 0.8, 0.9 } },
		{ "anisotropy", 0.35 }, { "emissivePerMeter", { 0.1, 0.05, 0.0 } },
		{ "edgeSoftness", 0.4 }, { "directLightingScale", 1.1 },
		{ "skyLightingScale", 0.3 }, { "receiveCloudShadows", true },
		{ "injectionPriority", 201 }
	};
	auto volumetricAssetOwner = std::make_shared<VansParticleAsset>();
	auto& volumetricAsset = *volumetricAssetOwner;
	error.clear();
	if (!Expect(decodeParticle(
		volumetric, "volumetric.particle", volumetricAsset, error),
		("A valid volumetric particle asset was rejected: " + error).c_str()))
		return false;
	VansGraphics::VansParticleRuntime volumetricRuntime;
	volumetricRuntime.SetAsset(volumetricAssetOwner);
	volumetricRuntime.Play();
	volumetricRuntime.Update(0.11f);
	volumetricRuntime.SwapBuffers();
	if (!Expect(volumetricRuntime.GetRenderBuffer().empty() &&
		!volumetricRuntime.GetVolumetricRenderBuffer().empty() &&
		volumetricRuntime.HasVolumetricInjectionEnabled() &&
		volumetricRuntime.AliveInstanceCount() > 0u,
		"A volume-only emitter did not suppress only its surface instances") ||
		!Expect(volumetricRuntime.GetVolumetricRenderBuffer()[0].m_Metadata.w == 201u &&
			std::abs(volumetricRuntime.GetVolumetricRenderBuffer()[0].
				m_DistanceAndPadding.x - 80.0f) < 1.0e-5f,
			"Volumetric particle candidate parameters were not baked into frame data"))
		return false;
	const ParticleTestJson encoded = encodeParticle(volumetricAsset);
	if (!Expect(encoded["emitters"][0]["renderer"].contains("volumetric") &&
		encoded["emitters"][0]["renderer"]["volumetric"]["enabled"].get<bool>() &&
		encoded["emitters"][0]["renderer"]["volumetric"]["injectionPriority"].get<unsigned>() == 201u,
		"Volumetric particle authoring parameters did not round-trip"))
		return false;
	auto disabledJson = volumetric;
    disabledJson["emitters"][0]["renderer"]["volumetric"]["enabled"] = false;
    disabledJson["emitters"][0]["renderer"]["volumetric"]["extinctionPerMeter"] = 2.25f;
    VansParticleAsset disabledAsset;
    if (!Expect(decodeParticle(disabledJson, "disabled.particle", disabledAsset, error),
        "Disabled particle definition failed to decode")) return false;
    const auto disabledTuningEncoded = encodeParticle(disabledAsset);
	if (!Expect(
		disabledTuningEncoded["emitters"][0]["renderer"].contains("volumetric") &&
		!disabledTuningEncoded["emitters"][0]["renderer"]["volumetric"]["enabled"].get<bool>() &&
		std::abs(disabledTuningEncoded["emitters"][0]["renderer"]["volumetric"]
			["extinctionPerMeter"].get<float>() - 2.25f) < 1.0e-5f,
		"Disabled volumetric tuning was discarded before the artist opted in"))
		return false;
	ParticleTestJson invalid = volumetric;
	invalid["emitters"][0]["renderer"]["volumetric"]["anisotropy"] = 1.0;
	VansParticleAsset invalidAsset;
	error.clear();
	if (!Expect(!decodeParticle(
		invalid, "invalid.particle", invalidAsset, error),
		"An invalid volumetric-particle anisotropy value was accepted"))
		return false;

	ParticleTestJson lifecycle = volumetric;
	lifecycle["name"] = "LifecycleDrivenParticle";
	lifecycle["emitters"][0]["maxParticles"] = 1u;
	lifecycle["emitters"][0]["spawn"]["rate"] = 10.0;
	lifecycle["emitters"][0]["initialize"] = ParticleTestJson::array({
		{ { "module", "InitLifetime" }, { "enabled", true },
		  { "lifetime", { { "mode", "Constant" }, { "value", 2.0 } } } },
		{ { "module", "InitVelocity" }, { "enabled", true },
		  { "mode", "Cone" }, { "angle", 0.0 }, { "speed", 1.0 } },
		{ { "module", "InitSize" }, { "enabled", true },
		  { "size", { { "mode", "Constant" }, { "value", 2.0 } } } }
	});
	lifecycle["emitters"][0]["update"] = ParticleTestJson::array({
		{ { "module", "UpdateSizeOverLifetime" }, { "enabled", true },
		  { "curve", ParticleTestJson::array({
			  { { "t", 0.0 }, { "value", 2.0 } },
			  { { "t", 1.0 }, { "value", 2.0 } } }) } },
		{ { "module", "UpdateColorOverLifetime" }, { "enabled", true },
		  { "gradient", { { "stops", ParticleTestJson::array({
			  { { "t", 0.0 }, { "color", { 1.0, 1.0, 1.0, 1.0 } } },
			  { { "t", 0.5 }, { "color", { 1.0, 1.0, 1.0, 1.0 } } },
			  { { "t", 1.0 }, { "color", { 1.0, 1.0, 1.0, 0.0 } } }
		  }) } } } }
	});
	VansParticleAsset lifecycleAsset;
	error.clear();
	if (!Expect(decodeParticle(
		lifecycle, "lifecycle.particle", lifecycleAsset, error),
		("A valid lifecycle-module particle was rejected: " + error).c_str()))
		return false;
	VansGraphics::VansParticleEmitterRuntime lifecycleEmitter(*lifecycleAsset.m_Emitters[0]);
	lifecycleEmitter.Update(0.11f, glm::mat4(1.0f));
	lifecycleEmitter.Update(0.11f, glm::mat4(1.0f));
	const auto& lifecyclePool = lifecycleEmitter.ParticlePool();
	if (!Expect(lifecyclePool.m_AliveCount == 1u &&
		std::abs(lifecyclePool.m_Size[0] - 4.0f) < 1.0e-5f,
		"Size Over Lifetime compounded the previous frame instead of the initial size") ||
		!Expect(std::abs(lifecyclePool.m_Position[0].y - 0.12f) < 1.0e-5f,
			"Initial velocity must integrate only the 0.12 seconds since the scheduled birth at 0.1 seconds"))
		return false;
	lifecycleEmitter.Update(1.4f, glm::mat4(1.0f));
	lifecycleEmitter.Update(0.1f, glm::mat4(1.0f));
	if (!Expect(lifecyclePool.m_Color[0].a > 0.0f &&
		lifecyclePool.m_Color[0].a < 0.5f,
		"Color Over Lifetime did not fade near the end of normalized lifetime"))
		return false;
	const ParticleTestJson lifecycleEncoded = encodeParticle(lifecycleAsset);
	if (!Expect(lifecycleEncoded["emitters"][0]["update"][0]
		.value("enabled", false),
		"Particle module enabled state did not round-trip"))
		return false;
	ParticleTestJson invalidCurve = lifecycle;
	invalidCurve["emitters"][0]["update"][0]["curve"][1]["t"] = 0.0;
	error.clear();
	if (!Expect(!decodeParticle(
		invalidCurve, "invalid_curve.particle", invalidAsset, error),
		"An unordered normalized lifetime curve was accepted"))
		return false;

	fs::path sourceRoot;
	for (fs::path cursor = fs::current_path(); !cursor.empty() && sourceRoot.empty();
		cursor = cursor.parent_path())
	{
		for (const fs::path& candidate : { cursor, cursor / "ForestEngine" })
		{
			if (fs::exists(candidate / "Source" / "EngineCore") &&
				fs::exists(candidate / "EngineAssets" / "Shaders"))
			{
				sourceRoot = candidate;
				break;
			}
		}
		if (cursor == cursor.root_path()) break;
	}
	if (!Expect(!sourceRoot.empty(),
		"Cannot locate the ForestEngine source root for particle injection checks"))
		return false;
	auto readText = [](const fs::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>());
	};
	const fs::path shaderRoot = sourceRoot / "EngineAssets" / "Shaders" / "Atmosphere";
	const std::string nearMedia = readText(sourceRoot / "Source" / "EngineCore" /
		"RenderCore" / "AtmosphereCore" / "VansNearMediaSystem.cpp");
	const std::string localInjection = readText(shaderRoot /
		"LocalMediaInjection.comp");
	const std::string unifiedInjection = readText(shaderRoot /
		"NearMediaUnifiedInjection.comp");
	const std::string sharedInjection = readText(shaderRoot /
		"NearMediaInjectionCommon.glsl");
	const std::string sharedLighting = readText(shaderRoot /
		"NearMediaLighting.comp");
	const std::string particleProvider = readText(shaderRoot /
		"VolumetricParticleMediumProvider.glsl");
	const std::string temporal = readText(shaderRoot /
		"VolumetricParticleTemporalResolve.comp") + readText(shaderRoot /
		"NearMediaTemporalResolveCommon.glsl");
	const std::string particleBuilder = readText(sourceRoot / "Source" /
		"EngineCore" / "RenderCore" / "SceneBuild" /
		"VansSceneParticleComponentBuilder.cpp");
	const std::string sceneSource = readText(sourceRoot / "Source" / "EngineCore" /
		"RenderCore" / "VansScene.cpp");
	const std::string inspector = readText(sourceRoot / "Source" / "EngineCore" /
		"EditorCore" / "Windows" / "VansInspectorWindow.cpp");
	const std::size_t prewarmTransform = particleBuilder.find("SetOwnerWorldTransform(");
	const std::size_t playOnAwake = particleBuilder.find("component->Play()");
	const std::size_t particleSignal = sceneSource.find("Particle::SignalUpdate");
	const std::size_t materialCapture = sceneSource.find("Material::CaptureFrameData", particleSignal);
	const std::size_t particleWait = sceneSource.find("Particle::WaitForUpdate", materialCapture);
	if (!Expect(
		nearMedia.find("if (!featureRequested)") != std::string::npos &&
		nearMedia.find("CreateVolumetricParticleResources()") != std::string::npos &&
		nearMedia.find("m_VolumetricParticleFeatureRequested &&") != std::string::npos &&
		nearMedia.find("m_NearMediaUnifiedInjectionShader") != std::string::npos &&
		nearMedia.find("BuildNearMediaPassBindings(false)") != std::string::npos &&
		nearMedia.find("BuildNearMediaPassBindings(true)") != std::string::npos &&
		nearMedia.find("{ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE") != std::string::npos &&
		nearMedia.find("{ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE") != std::string::npos &&
		nearMedia.find("bindings[index].binding == 10u") != std::string::npos &&
		localInjection.find("VANS_NEAR_MEDIA_WITH_VOLUMETRIC_PARTICLES 0") != std::string::npos &&
		unifiedInjection.find("VANS_NEAR_MEDIA_WITH_VOLUMETRIC_PARTICLES 1") != std::string::npos &&
		sharedInjection.find("NearMediaMediumSample") != std::string::npos &&
		sharedInjection.find("NearMediaMaterialAccumulation") != std::string::npos &&
		sharedInjection.find("AccumulateNearMediaMaterial") != std::string::npos &&
		sharedInjection.find("uDirectionLight") == std::string::npos &&
		sharedInjection.find("EvaluatePunctualLighting") == std::string::npos &&
		sharedLighting.find("NearMediaLightTransmittance") != std::string::npos &&
		sharedLighting.find("uParticles") == std::string::npos &&
		sharedLighting.find("uLocalFogVolumes") == std::string::npos &&
		particleProvider.find("uParticles.particles") != std::string::npos &&
		particleProvider.find("uLocalFog") == std::string::npos &&
		particleProvider.find("LocalFogField") == std::string::npos &&
		particleProvider.find("uDirectionLight") == std::string::npos &&
		particleProvider.find("SampleDirectionalShadow") == std::string::npos &&
		particleProvider.find("EvaluatePunctualLighting") == std::string::npos &&
		particleProvider.find("IntegrateParticleDensityOverFroxel") != std::string::npos &&
		particleProvider.find("froxelFootprintRadius") != std::string::npos &&
		particleProvider.find("candidateOverflow") != std::string::npos &&
		particleProvider.find("? particleCount") != std::string::npos &&
		nearMedia.find("header.y = OverflowCandidateCount") != std::string::npos &&
		particleProvider.find("subFroxelJitter") == std::string::npos &&
		inspector.find("DrawParticleModuleAddMenu") != std::string::npos &&
		inspector.find("ParticleAssetScalarLimits") != std::string::npos &&
		sharedInjection.find("ParticlePhase") == std::string::npos &&
		sharedInjection.find("EvaluateParticlePunctualLighting") == std::string::npos &&
		!fs::exists(shaderRoot / "VolumetricParticleInjection.comp") &&
		prewarmTransform != std::string::npos && playOnAwake != std::string::npos &&
		prewarmTransform < playOnAwake &&
		particleSignal != std::string::npos && materialCapture != std::string::npos &&
		particleWait != std::string::npos && particleSignal < materialCapture &&
		materialCapture < particleWait &&
		nearMedia.find("m_VolumetricParticlePassSets[target], 5") == std::string::npos &&
		nearMedia.find("m_VolumetricParticlePassSets[target], 6") == std::string::npos &&
		temporal.find("VANS_NEAR_MEDIA_WITH_PARTICLE_REACTIVE_HISTORY 1") !=
			std::string::npos &&
		temporal.find("particleReactive") != std::string::npos &&
		temporal.find("historyWeight *= 1.0 - particleReactive") != std::string::npos &&
		fs::exists(shaderRoot / "NearMediaUnifiedInjectioncomp.spv") &&
		fs::file_size(shaderRoot / "NearMediaUnifiedInjectioncomp.spv") > 0u &&
		fs::exists(shaderRoot / "NearMediaLightingcomp.spv") &&
		fs::file_size(shaderRoot / "NearMediaLightingcomp.spv") > 0u &&
		fs::file_size(shaderRoot / "VolumetricParticleTemporalResolvecomp.spv") > 0u,
		"Particle medium-provider independence or unified NearMedia lighting is incomplete"))
		return false;

	const fs::path workspaceRoot = sourceRoot.parent_path().parent_path();
	const fs::path demoAsset = workspaceRoot / "DemoHallProject" / "Assets" /
		"Particles" / "VolumetricSmokeTest.particle";
	const fs::path demoMeta = fs::path(demoAsset.string() + ".meta");
	const fs::path demoScene = workspaceRoot / "DemoHallProject" / "Scenes" / "DemoHall.json";
	if (!Expect(fs::exists(demoAsset) && fs::exists(demoMeta) && fs::exists(demoScene),
		"DemoHall volumetric-particle test files are missing"))
		return false;
	const nlohmann::json demoAssetJson = nlohmann::json::parse(readText(demoAsset));
	const nlohmann::json demoMetaJson = nlohmann::json::parse(readText(demoMeta));
	const nlohmann::json demoSceneJson = nlohmann::json::parse(readText(demoScene));
	auto decodedDemoAssetOwner = std::make_shared<VansParticleAsset>();
	auto& decodedDemoAsset = *decodedDemoAssetOwner;
	error.clear();
	if (!Expect(decodeParticle(
		demoAssetJson, demoAsset.string(), decodedDemoAsset, error),
		("DemoHall volumetric-particle asset failed runtime decoding: " + error).c_str()))
		return false;
	if (!Expect(!decodedDemoAsset.m_Prewarm && decodedDemoAsset.m_StartDelay == 2.0f,
		"Thrown smoke must start empty and delay emission for two seconds"))
		return false;
	// Keep immediate prewarm/transform coverage using an in-memory fixture. The
	// actual grenade asset intentionally waits for its post-spawn emission delay.
	decodedDemoAsset.m_Prewarm = true;
	decodedDemoAsset.m_StartDelay = 0.0f;
	VansGraphics::VansParticleManager demoManager;
	const auto demoHandle = demoManager.Create(decodedDemoAssetOwner);
	glm::mat4 demoTransform(1.0f);
	demoTransform[3] = glm::vec4(-3.0f, 1.0f, 0.0f, 1.0f);
	demoManager.SetOwnerWorldTransform(demoHandle,demoTransform);
	const auto* demoRuntime = demoManager.Resolve(demoHandle);
	demoManager.Queue(demoHandle, VansGraphics::VansParticleControl::Play);
	demoManager.TickMainThread(0); demoManager.WaitForUpdateAndSwap();
	if (!Expect(demoManager.ResimulationSteps() > 0 &&
		demoManager.ResimulationSteps() <= VansGraphics::VansParticleManager::MaxResimulationStepsPerTick &&
		demoManager.PendingResimulations() == 1,
		"Particle prewarm did not remain inside the worker resimulation budget"))
		return false;
	for (int tick = 0; tick < 32 && demoManager.PendingResimulations() != 0; ++tick)
	{
		demoManager.TickMainThread(0); demoManager.WaitForUpdateAndSwap();
	}
	if (!Expect(demoManager.PendingResimulations() == 0 &&
		!demoRuntime->GetVolumetricRenderBuffer().empty(),
		"DemoHall prewarmed test asset produced no immediately previewable volume instances") ||
		!Expect(std::abs(demoRuntime->GetVolumetricRenderBuffer()[0].
			m_WorldPositionRadius.x + 3.0f) < 2.5f &&
			std::abs(demoRuntime->GetVolumetricRenderBuffer()[0].
				m_WorldPositionRadius.z) < 2.5f,
			"Prewarmed volumetric particles were simulated before the object transform"))
		return false;
	float minimumRadius = 1000000.0f;
	float maximumRadius = 0.0f;
	float minimumHeight = 1000000.0f;
	float maximumHeight = -1000000.0f;
	float minimumExtinction = 1000000.0f;
	float maximumExtinction = 0.0f;
	for (const auto& instance : demoRuntime->GetVolumetricRenderBuffer())
	{
		minimumRadius = std::min(minimumRadius, instance.m_WorldPositionRadius.w);
		maximumRadius = std::max(maximumRadius, instance.m_WorldPositionRadius.w);
		minimumHeight = std::min(minimumHeight, instance.m_WorldPositionRadius.y);
		maximumHeight = std::max(maximumHeight, instance.m_WorldPositionRadius.y);
		minimumExtinction = std::min(
			minimumExtinction, instance.m_ScatteringAlbedoExtinction.a);
		maximumExtinction = std::max(
			maximumExtinction, instance.m_ScatteringAlbedoExtinction.a);
	}
	if (!Expect(demoRuntime->GetVolumetricRenderBuffer().size() > 180u &&
		maximumHeight - minimumHeight > 1.5f,
		"DemoHall smoke does not form a dense, continuous rising column") ||
		!Expect(maximumRadius > minimumRadius * 2.0f,
			"DemoHall smoke does not expand over normalized lifetime") ||
		!Expect(minimumExtinction < maximumExtinction * 0.6f,
			"DemoHall smoke does not fade through volumetric extinction"))
		return false;
	demoManager.TickMainThread(0); demoManager.WaitForUpdateAndSwap();
	if (!Expect(!demoRuntime->GetVolumetricRenderBuffer().empty(),
		"DemoHall prewarmed volume instances did not survive the first frame swap"))
		return false;
	VansScriptParticleComponent facadeComponent;
	VansGraphics::VansParticleManager facadeManager;
    facadeComponent.m_Manager = &facadeManager;
    facadeComponent.m_Instance = facadeManager.Create(decodedDemoAssetOwner);
	facadeComponent.Play();
	for (int tick = 0; tick < 32; ++tick)
	{
		facadeManager.TickMainThread(0); facadeManager.WaitForUpdateAndSwap();
		if (facadeManager.PendingResimulations() == 0) break;
	}
	if (!Expect(facadeComponent.IsPlaying() &&
		!facadeComponent.GetRuntime()->GetVolumetricRenderBuffer().empty(),
		"Particle component Play bypassed runtime prewarm"))
		return false;
	facadeComponent.Stop(); facadeManager.TickMainThread(0); facadeManager.WaitForUpdateAndSwap();
	if (!Expect(!facadeComponent.IsPlaying() &&
		facadeComponent.GetRuntime()->GetRenderBuffer().empty() &&
		facadeComponent.GetRuntime()->GetVolumetricRenderBuffer().empty(),
		"Particle component Stop left stale surface or volume instances"))
		return false;
	const std::string demoGuid = demoMetaJson.value("guid", "");
	bool sceneObjectFound = false;
	for (const auto& entity : demoSceneJson.value("entities", nlohmann::json::array()))
	{
		if (entity.value("name", "") != "Volumetric_Particle_Smoke_Test")
			continue;
		for (const auto& component : entity.value("components", nlohmann::json::array()))
		{
			if (component.value("type", "") == "Particle" &&
				component.value("data", nlohmann::json::object())
					.value("asset", nlohmann::json::object()).value("guid", "") == demoGuid)
				sceneObjectFound = true;
		}
	}
	const auto& demoUpdate = demoAssetJson["emitters"][0]["update"];
	const auto hasEnabledModule = [&demoUpdate](const char* moduleName)
	{
		return std::any_of(demoUpdate.begin(), demoUpdate.end(),
			[moduleName](const nlohmann::json& module)
			{
				return module.value("module", "") == moduleName &&
					module.value("enabled", false);
			});
	};
	return Expect(
		demoAssetJson["emitters"][0]["renderer"]["volumetric"]
			.value("enabled", false) &&
		demoAssetJson["emitters"][0]["renderer"]["volumetric"]
			.value("densityMultiplier", 0.0f) > 0.0f &&
		demoAssetJson["emitters"][0]["renderer"]["volumetric"]
			.value("densityMultiplier", 1000.0f) <= 1.0f &&
		hasEnabledModule("UpdateColorOverLifetime") &&
		hasEnabledModule("UpdateSizeOverLifetime") &&
		hasEnabledModule("UpdateVelocityOverLifetime") && !sceneObjectFound,
		"DemoHall smoke modules are incomplete or its standalone emitter remains");
}

bool TestTerrainAuthoringContract()
{
	using namespace Vans;
	VansTerrainAsset terrain;
	const auto parseGuid = [](const char* text)
	{
		VansAssetGuid guid;
		VansAssetGuid::TryParse(text, guid);
		return guid;
	};
	terrain.heightmap = parseGuid("20000000-0000-4000-8000-000000000001");
	terrain.splatmaps[0] = parseGuid("20000000-0000-4000-8000-000000000002");
	terrain.splatmaps[1] = parseGuid("20000000-0000-4000-8000-000000000003");
	const VansAssetGuid albedo = parseGuid("20000000-0000-4000-8000-000000000004");
	const VansAssetGuid normal = parseGuid("20000000-0000-4000-8000-000000000005");
	const VansAssetGuid roughness = parseGuid("20000000-0000-4000-8000-000000000006");
	for (std::uint32_t index = 0; index < VANS_TERRAIN_LAYER_COUNT; ++index)
	{
		VansTerrainLayerAsset layer;
		layer.id = "layer_" + std::to_string(index);
		layer.name = "Layer " + std::to_string(index + 1u);
		layer.albedo = albedo;
		layer.normal = normal;
		layer.roughness = roughness;
		layer.tiling = 16.0f + static_cast<float>(index);
		terrain.layers.push_back(std::move(layer));
	}
	terrain.settings.terrainSize = 64.0f;
	terrain.settings.maxHeight = 32.0f;
	terrain.settings.heightOffset = -4.0f;
	terrain.settings.heightDetailEnabled = false;
	terrain.settings.heightDetailStrength = 0.04f;
	terrain.settings.heightDetailFadeStart = 0.65f;
	terrain.settings.riverWetness.albedoScale = 0.63f;
	terrain.settings.riverWetness.roughness = 0.21f;
	terrain.settings.riverWetness.detailNormalScale = 0.74f;
	terrain.width = 9;
	terrain.height = 7;
	const std::size_t pixelCount = static_cast<std::size_t>(terrain.width) * terrain.height;
	terrain.heights.resize(pixelCount);
	for (std::size_t index = 0; index < pixelCount; ++index)
		terrain.heights[index] = static_cast<std::uint16_t>((index * 1009u) & 0xffffu);
	terrain.heights.front() = 0;
	terrain.heights.back() = 65535;
	for (auto& splat : terrain.splatPixels) splat.assign(pixelCount * 4u, 0);
	for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
		terrain.splatPixels[0][pixel * 4u] = 255;

	VansSerializedValue encodedDefinition;
	std::string error;
	if (!Expect(VansTerrainAssetCodec::EncodeDefinition(
		terrain, encodedDefinition, error), error.c_str()))
		return false;
	VansTerrainAsset decodedDefinition;
	if (!Expect(VansTerrainAssetCodec::DecodeDefinition(
		encodedDefinition, decodedDefinition, error), error.c_str()) ||
		!Expect(decodedDefinition.layers.size() == VANS_TERRAIN_LAYER_COUNT &&
			decodedDefinition.heightmap == terrain.heightmap &&
			decodedDefinition.splatmaps == terrain.splatmaps &&
			decodedDefinition.settings.heightDetailEnabled == terrain.settings.heightDetailEnabled &&
			decodedDefinition.settings.heightDetailStrength == terrain.settings.heightDetailStrength &&
			decodedDefinition.settings.heightDetailFadeStart == terrain.settings.heightDetailFadeStart &&
			decodedDefinition.settings.riverWetness.albedoScale == terrain.settings.riverWetness.albedoScale &&
			decodedDefinition.settings.riverWetness.roughness == terrain.settings.riverWetness.roughness &&
			decodedDefinition.settings.riverWetness.detailNormalScale == terrain.settings.riverWetness.detailNormalScale,
			"Terrain definition round trip changed required references"))
		return false;
	const VansSerializedValue* tessellation = FindObjectField(encodedDefinition, "tessellation");
	const VansSerializedValue* heightDetail = tessellation ? FindObjectField(*tessellation, "heightDetail") : nullptr;
	if (!Expect(heightDetail && heightDetail->kind == VansSerializedValue::Kind::Object &&
		heightDetail->objectFields.size() == 3u &&
		FindObjectField(*heightDetail, "enabled") && FindObjectField(*heightDetail, "strength") &&
		FindObjectField(*heightDetail, "fadeStart"),
		"Terrain height detail must serialize only enabled, strength and fadeStart"))
		return false;
	VansSerializedValue missingHeightDetail = encodedDefinition;
	auto* missingHeightTessellation = FindObjectField(missingHeightDetail, "tessellation");
	missingHeightTessellation->objectFields.erase(std::remove_if(
		missingHeightTessellation->objectFields.begin(), missingHeightTessellation->objectFields.end(),
		[](const auto& field) { return field.first == "heightDetail"; }), missingHeightTessellation->objectFields.end());
	if (!Expect(!VansTerrainAssetCodec::DecodeDefinition(missingHeightDetail, decodedDefinition, error) && !error.empty(),
		"Terrain definition accepted missing material height detail settings"))
		return false;
	for (const float invalidStrength : { -0.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
	{
		auto invalid = terrain;
		invalid.settings.heightDetailStrength = invalidStrength;
		if (!Expect(!ValidateTerrainAsset(invalid, false).empty(), "Terrain accepted invalid height detail strength"))
			return false;
	}
	for (const float invalidFade : { -0.01f, 1.0f, std::numeric_limits<float>::quiet_NaN() })
	{
		auto invalid = terrain;
		invalid.settings.heightDetailFadeStart = invalidFade;
		if (!Expect(!ValidateTerrainAsset(invalid, false).empty(), "Terrain accepted invalid height detail fade"))
			return false;
	}
	VansSerializedValue missingWetness = encodedDefinition;
	missingWetness.objectFields.erase(std::remove_if(
		missingWetness.objectFields.begin(), missingWetness.objectFields.end(),
		[](const auto& field) { return field.first == "riverWetness"; }), missingWetness.objectFields.end());
	if (!Expect(!VansTerrainAssetCodec::DecodeDefinition(
		missingWetness, decodedDefinition, error) && !error.empty(),
		"Terrain definition accepted missing river wetness settings"))
		return false;
	VansSerializedValue incompleteDefinition = encodedDefinition;
	VansSerializedValue* splatmaps = FindObjectField(incompleteDefinition, "splatmaps");
	if (!Expect(splatmaps && splatmaps->kind == VansSerializedValue::Kind::Array,
		"Terrain contract fixture has no splat array"))
		return false;
	splatmaps->arrayItems.pop_back();
	if (!Expect(!VansTerrainAssetCodec::DecodeDefinition(
		incompleteDefinition, decodedDefinition, error) && !error.empty(),
		"Terrain definition accepted a missing required splatmap"))
		return false;

	VansTerrainHeightImage heightImage{ terrain.width, terrain.height, terrain.heights };
	std::string heightPng;
	if (!Expect(VansTerrainImageCodec::EncodeHeight16(heightImage, heightPng, error), error.c_str()))
		return false;
	VansTerrainHeightImage decodedHeight;
	if (!Expect(VansTerrainImageCodec::DecodeHeight16(heightPng, decodedHeight, error), error.c_str()) ||
		!Expect(decodedHeight.width == terrain.width && decodedHeight.height == terrain.height &&
			decodedHeight.pixels == terrain.heights,
			"Terrain R16 PNG round trip lost height precision"))
		return false;

	for (std::size_t imageIndex = 0; imageIndex < terrain.splatPixels.size(); ++imageIndex)
	{
		VansTerrainWeightImage weightImage{
			terrain.width, terrain.height, terrain.splatPixels[imageIndex] };
		std::string weightPng;
		VansTerrainWeightImage decodedWeights;
		if (!Expect(VansTerrainImageCodec::EncodeWeightsRGBA8(
			weightImage, weightPng, error), error.c_str()) ||
			!Expect(VansTerrainImageCodec::DecodeWeightsRGBA8(
				weightPng, decodedWeights, error), error.c_str()) ||
			!Expect(decodedWeights.pixels == terrain.splatPixels[imageIndex],
				"Terrain RGBA8 splat PNG round trip changed weights"))
			return false;
	}

	const std::vector<std::uint16_t> uneditedHeights = terrain.heights;
	VansTerrainBrushDab raise;
	raise.operation = VansTerrainBrushOperation::Raise;
	raise.centerX = 4.0f;
	raise.centerY = 3.0f;
	raise.radius = 2.5f;
	raise.strength = 0.1f;
	raise.hardness = 0.5f;
	const VansTerrainBrushResult raised = VansTerrainBrush::Apply(terrain, raise);
	if (!Expect(raised && raised.changed && raised.dirtyRect.valid &&
		terrain.heights[3u * terrain.width + 4u] > uneditedHeights[3u * terrain.width + 4u],
		"Terrain raise brush did not modify the heightfield"))
		return false;

	VansTerrainBrushDab patternProbe = raise;
	patternProbe.centerX = 0.0f;
	patternProbe.centerY = 0.0f;
	patternProbe.radius = 10.0f;
	patternProbe.strength = 1.0f;
	patternProbe.hardness = 0.0f;
	for (const VansTerrainBrushPattern pattern : {
		VansTerrainBrushPattern::SmoothCircle,
		VansTerrainBrushPattern::LinearCircle,
		VansTerrainBrushPattern::Sphere,
		VansTerrainBrushPattern::Tip,
		VansTerrainBrushPattern::SoftSquare,
		VansTerrainBrushPattern::Ridge,
		VansTerrainBrushPattern::Crater,
		VansTerrainBrushPattern::Rocky })
	{
		patternProbe.pattern = pattern;
		const float influence = VansTerrainBrush::EvaluateInfluence(patternProbe, 5.0f, 0.0f);
		if (!Expect(std::isfinite(influence) && influence >= 0.0f && influence <= 1.0f,
			"Terrain brush pattern produced an invalid influence"))
			return false;
	}
	patternProbe.pattern = VansTerrainBrushPattern::Ridge;
	const float horizontalRidge = VansTerrainBrush::EvaluateInfluence(patternProbe, 5.0f, 0.0f);
	const float verticalRidge = VansTerrainBrush::EvaluateInfluence(patternProbe, 0.0f, 5.0f);
	patternProbe.rotationRadians = 1.57079632679489661923f;
	const float rotatedVerticalRidge = VansTerrainBrush::EvaluateInfluence(patternProbe, 0.0f, 5.0f);
	if (!Expect(horizontalRidge > verticalRidge * 10.0f &&
		rotatedVerticalRidge > verticalRidge * 10.0f,
		"Terrain ridge brush rotation did not rotate its directional mask"))
		return false;
	patternProbe.rotationRadians = 0.0f;
	patternProbe.pattern = VansTerrainBrushPattern::Crater;
	if (!Expect(
		VansTerrainBrush::EvaluateInfluence(patternProbe, 6.2f, 0.0f) >
		VansTerrainBrush::EvaluateInfluence(patternProbe, 0.0f, 0.0f),
		"Terrain crater brush no longer emphasizes its rim"))
		return false;
	patternProbe.pattern = VansTerrainBrushPattern::Rocky;
	const float rockyA = VansTerrainBrush::EvaluateInfluence(patternProbe, 3.0f, 2.0f);
	const float rockyB = VansTerrainBrush::EvaluateInfluence(patternProbe, 3.0f, 2.0f);
	if (!Expect(rockyA == rockyB,
		"Terrain rocky brush mask is not deterministic within a stroke"))
		return false;

	VansTerrainBrushDab paint = raise;
	paint.operation = VansTerrainBrushOperation::PaintLayer;
	paint.selectedLayer = 5;
	paint.weightBaseLayer = 0;
	paint.strength = 0.65f;
	const VansTerrainBrushResult painted = VansTerrainBrush::Apply(terrain, paint);
	if (!Expect(painted && painted.changed,
		"Terrain paint brush did not modify splat weights"))
		return false;
	for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
	{
		std::uint32_t total = 0;
		for (std::uint32_t layer = 0; layer < VANS_TERRAIN_LAYER_COUNT; ++layer)
			total += terrain.splatPixels[layer / 4u][pixel * 4u + layer % 4u];
		if (!Expect(total == 255u,
			"Terrain splat brush broke the normalized eight-channel weight invariant"))
			return false;
	}

	TemporaryDirectory temporary;
	const fs::path terrainPath = temporary.path / "Authoring.vterrain";
	const std::array<fs::path, 3> imagePaths{
		temporary.path / "Height.png", temporary.path / "Splat0.png", temporary.path / "Splat1.png" };
	terrain.sourcePath = terrainPath;
	const auto writeBytes = [](const fs::path& path, const std::string& bytes)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		return output.good();
	};
	const std::string definitionJson =
		EncodeSerializedValueJson<nlohmann::ordered_json>(encodedDefinition).dump(2) + "\n";
	if (!Expect(writeBytes(terrainPath, definitionJson),
		"Terrain authoring fixture could not write its documents"))
		return false;
	VansTerrainImageCodec::EncodeHeight16(
		{ terrain.width, terrain.height, terrain.heights }, heightPng, error);
	if (!Expect(writeBytes(imagePaths[0], heightPng),
		"Terrain authoring fixture could not write its height image"))
		return false;
	for (std::size_t index = 0; index < 2; ++index)
	{
		std::string png;
		if (!Expect(VansTerrainImageCodec::EncodeWeightsRGBA8(
			{ terrain.width, terrain.height, terrain.splatPixels[index] }, png, error), error.c_str()) ||
			!Expect(writeBytes(imagePaths[index + 1], png),
				"Terrain authoring fixture could not write a splat image"))
			return false;
	}

	std::string layerPng;
	if (!Expect(VansTerrainImageCodec::EncodeWeightsRGBA8(
		{ terrain.width, terrain.height, terrain.splatPixels[0] }, layerPng, error), error.c_str()))
		return false;
	const std::array<std::pair<fs::path, VansAssetGuid>, 3> layerImages{{
		{ temporary.path / "Albedo.png", albedo },
		{ temporary.path / "Normal.png", normal },
		{ temporary.path / "Roughness.png", roughness }
	}};
	for (const auto& [path, guid] : layerImages)
	{
		(void)guid;
		if (!Expect(writeBytes(path, layerPng),
			"Terrain authoring fixture could not write a layer texture"))
			return false;
	}

	const auto saveMeta = [&](const fs::path& path, VansAssetGuid guid,
		VansAssetType type, VansSerializedValue settings)
	{
		VansAssetMeta meta;
		meta.guid = guid;
		meta.importer = VansAssetDatabase::ImporterFor(type);
		meta.SetSerializedSettings(std::move(settings));
		return VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(path), meta, error);
	};
	const auto terrainTextureSettings = [](int channels, const char* precision)
	{
		return VansSerializedValue::Object({
			{ "colorSpace", VansSerializedValue::String("linear") },
			{ "normalMap", VansSerializedValue::Bool(false) },
			{ "useCompress", VansSerializedValue::Bool(false) },
			{ "needMip", VansSerializedValue::Bool(false) },
			{ "importChannel", VansSerializedValue::Int(channels) },
			{ "precision", VansSerializedValue::String(precision) },
			{ "addressMode", VansSerializedValue::String("clamp") }
		});
	};
	const VansAssetGuid terrainGuid =
		parseGuid("20000000-0000-4000-8000-000000000007");
	if (!Expect(
		saveMeta(terrainPath, terrainGuid, VansAssetType::Terrain,
			VansSerializedValue::Object({})) &&
		saveMeta(imagePaths[0], terrain.heightmap, VansAssetType::Texture,
			terrainTextureSettings(1, "mid16")) &&
		saveMeta(imagePaths[1], terrain.splatmaps[0], VansAssetType::Texture,
			terrainTextureSettings(4, "low8")) &&
		saveMeta(imagePaths[2], terrain.splatmaps[1], VansAssetType::Texture,
			terrainTextureSettings(4, "low8")),
		"Terrain authoring fixture could not write strict asset metadata"))
		return false;
	for (const auto& [path, guid] : layerImages)
	{
		if (!Expect(saveMeta(path, guid, VansAssetType::Texture,
			VansSerializedValue::Object({
				{ "colorSpace", VansSerializedValue::String("linear") },
				{ "normalMap", VansSerializedValue::Bool(guid == normal) },
				{ "useCompress", VansSerializedValue::Bool(false) },
				{ "needMip", VansSerializedValue::Bool(true) },
				{ "importChannel", VansSerializedValue::Int(4) },
				{ "precision", VansSerializedValue::String("low8") }
			})), "Terrain authoring fixture could not write layer metadata"))
			return false;
	}

	VansAssetDatabase database(temporary.path, temporary.path / "Artifacts");
	const VansAssetScanResult scan = database.Scan(VansAssetOperationPolicy::ReadOnly());
	if (!Expect(scan.errors.empty() && scan.registered == 7u,
		"Terrain asset fixture did not enter the strict asset database"))
		return false;
	VansAssetObjectRepository bootstrapRepository;
	const VansAssetObjectBootstrapResult bootstrap =
		VansAssetObjectBootstrapper::Publish(database.All(), bootstrapRepository);
	if (!Expect(static_cast<bool>(bootstrap),
		bootstrap.errors.empty() ? "Terrain memory bootstrap failed" : bootstrap.errors.front().c_str()) ||
		!Expect(bootstrapRepository.ResolveLatest<VansTerrainAsset>(terrainGuid) != nullptr &&
			bootstrapRepository.ResolveLatest<VansAssetMeta>(terrainGuid) != nullptr,
			"Terrain source snapshot and metadata view were not published together"))
		return false;

	VansSceneData dependencyScene;
	dependencyScene.sceneGuid = VansAssetGuid::New();
	auto dependencyJson = VansSceneSchema::SerializeSceneJson(dependencyScene);
	dependencyJson["terrainFixture"] = terrainGuid.ToString();
	const VansSceneAssetDependencyBuildResult dependencyResult =
		VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database, DecodeSerializedValueJson(dependencyJson),
			temporary.path / "Scenes" / "Terrain.json", {}, bootstrapRepository);
	if (!Expect(dependencyResult.success &&
		dependencyResult.requiredAssets.count(terrainGuid.ToString()) == 1u &&
		dependencyResult.requiredAssets.count(terrain.heightmap.ToString()) == 1u &&
		dependencyResult.requiredAssets.count(terrain.splatmaps[0].ToString()) == 1u &&
		dependencyResult.requiredAssets.count(terrain.splatmaps[1].ToString()) == 1u &&
		dependencyResult.requiredTextures.count(terrain.heightmap.ToString()) == 0u &&
		dependencyResult.requiredTextures.count(terrain.splatmaps[0].ToString()) == 0u &&
		dependencyResult.requiredTextures.count(terrain.splatmaps[1].ToString()) == 0u &&
		dependencyResult.requiredTextures.count(albedo.ToString()) == 1u &&
		dependencyResult.requiredTextures.count(normal.ToString()) == 1u &&
		dependencyResult.requiredTextures.count(roughness.ToString()) == 1u &&
		dependencyResult.resourcePlan.textures.size() == 3u,
		"Terrain dependency closure duplicated source maps or missed layer textures"))
		return false;

	const std::optional<VansAssetRecord> indexedTerrain = database.Find(terrainGuid);
	if (!Expect(indexedTerrain.has_value(),
		"Terrain asset fixture could not resolve its indexed record"))
		return false;
	VansAssetRecord record = *indexedTerrain;
	VansAssetObjectRepository repository;
	VansAssetDocumentRegistry::Get().Clear();
	auto session = VansTerrainAuthoringSession::Open(
		record, std::make_shared<const VansTerrainAsset>(terrain), imagePaths, repository, error);
	if (!Expect(session != nullptr, error.c_str()))
		return false;
	const std::array<float, 3> rayOrigin{ 6.25f, 100.0f, -7.75f };
	const std::array<float, 3> rayDirection{ 0.0f, -2.0f, 0.0f };
	VansTerrainSurfaceRayHit sharedHit;
	std::array<float, 3> authoringHit{};
	float authoringPixelX = 0.0f;
	float authoringPixelY = 0.0f;
	if (!Expect(VansTerrainSurfaceQuery::Raycast(
			session->WorkingAsset(), rayOrigin, rayDirection, 1000.0f, sharedHit) &&
		session->Raycast(rayOrigin, rayDirection, authoringHit, authoringPixelX, authoringPixelY) &&
		std::abs(authoringHit[0] - sharedHit.position[0]) < 1.0e-5f &&
		std::abs(authoringHit[1] - sharedHit.position[1]) < 1.0e-5f &&
		std::abs(authoringHit[2] - sharedHit.position[2]) < 1.0e-5f &&
		std::abs(authoringPixelX - sharedHit.pixelX) < 1.0e-5f &&
		std::abs(authoringPixelY - sharedHit.pixelY) < 1.0e-5f,
		"Terrain authoring raycast diverged from the shared pixel-center surface query"))
		return false;
	if (!Expect(VansTerrainAuthoringSession::Open(
		record, std::make_shared<const VansTerrainAsset>(terrain), imagePaths, repository, error) == session,
		"Reopening terrain did not reuse its document companion"))
		return false;
	auto conflictingImagePaths = imagePaths;
	conflictingImagePaths[0] += ".other";
	if (!Expect(!VansTerrainAuthoringSession::Open(
		record, std::make_shared<const VansTerrainAsset>(terrain), conflictingImagePaths, repository, error) &&
		!error.empty(), "Terrain document accepted a second pixel editing target"))
		return false;
	error.clear();
	const std::vector<std::uint16_t> strokeBaseline = session->WorkingAsset().heights;
	const bool strokeBegan = session->BeginStroke(VansTerrainBrushOperation::Raise, error);
	const VansTerrainBrushResult strokeDab = session->ApplyDab(raise);
	VansTerrainBrushDab secondRaise = raise;
	secondRaise.centerX = 1.0f;
	secondRaise.centerY = 1.0f;
	const VansTerrainBrushResult secondStrokeDab = session->ApplyDab(secondRaise);
	const bool strokeEnded = session->EndStroke(error);
	if (!Expect(strokeBegan, error.c_str()) ||
		!Expect(strokeDab.changed, "Terrain authoring stroke did not apply its dab") ||
		!Expect(secondStrokeDab.changed, "Terrain authoring stroke did not merge a second dab") ||
		!Expect(strokeEnded, error.c_str()) ||
		!Expect(session->Document()->IsDirty() &&
			VansAssetDocumentEditService::CanUndo(session->Document()->sourceDocument),
			"Terrain stroke was not registered as one undoable asset edit"))
		return false;
	if (!Expect(static_cast<bool>(VansAssetDocumentEditService::Undo(
		session->Document()->sourceDocument)), "Terrain stroke undo failed") ||
		!Expect(session->WorkingAsset().heights == strokeBaseline,
			"Terrain stroke undo did not restore height pixels") ||
		!Expect(static_cast<bool>(VansAssetDocumentEditService::Redo(
			session->Document()->sourceDocument)), "Terrain stroke redo failed") ||
		!Expect(session->WorkingAsset().heights != strokeBaseline,
			"Terrain stroke redo did not restore edited height pixels"))
		return false;

	VansAssetDocumentSaveStage definitionStage;
	std::vector<VansStagedFile> imageStages;
	if (!Expect(session->Document()->sourceDocument.StageSave(definitionStage, error), error.c_str()) ||
		!Expect(session->StageSave(imageStages, error) && imageStages.size() == 3u, error.c_str()))
		return false;
	VansStagedFileTransaction transaction;
	transaction.Add({ definitionStage.targetPath, definitionStage.temporaryPath });
	for (const VansStagedFile& stage : imageStages) transaction.Add(stage);
	if (!Expect(transaction.Publish(error), error.c_str()) ||
		!Expect(session->Document()->sourceDocument.ObservePublishedSave(definitionStage, error), error.c_str()) ||
		!Expect(session->ObservePublishedSave(error), error.c_str()))
		return false;
	session->Document()->sourceDocument.AdoptObservedSave(definitionStage);
	session->AdoptObservedSave();
	if (!Expect(!session->Document()->IsDirty(),
		"Terrain atomic save did not adopt all document and image states"))
		return false;
	VansTerrainHeightImage savedHeight;
	if (!Expect(VansTerrainImageCodec::LoadHeight16(imagePaths[0], savedHeight, error), error.c_str()) ||
		!Expect(savedHeight.pixels == session->WorkingAsset().heights,
			"Terrain atomic save did not persist the edited heightfield"))
		return false;
	const float savedLodDistance = session->WorkingAsset().settings.lodBaseDistance;
	VansTerrainAssetSettings changedSettings = session->WorkingAsset().settings;
	changedSettings.lodBaseDistance = savedLodDistance + 1.0f;
	changedSettings.heightDetailEnabled = true;
	changedSettings.heightDetailStrength = 0.08f;
	changedSettings.heightDetailFadeStart = 0.8f;
	if (!Expect(session->ApplyDefinition(changedSettings, error), error.c_str()) ||
		!Expect(session->WorkingAsset().settings.heightDetailEnabled &&
			session->WorkingAsset().settings.heightDetailStrength == 0.08f &&
			session->WorkingAsset().settings.heightDetailFadeStart == 0.8f,
			"Terrain authoring did not apply material height detail settings") ||
		!Expect(session->BeginStroke(VansTerrainBrushOperation::Lower, error), error.c_str()))
		return false;
	VansTerrainBrushDab lower = raise;
	lower.operation = VansTerrainBrushOperation::Lower;
	if (!Expect(session->ApplyDab(lower).changed,
		"Terrain discard fixture did not modify its heightfield") ||
		!Expect(session->EndStroke(error), error.c_str()) ||
		!Expect(static_cast<bool>(VansAssetDocumentEditService::RevertToSaved(
			session->Document()->sourceDocument)), "Terrain discard failed") ||
		!Expect(session->SyncDefinitionFromDocument(error), error.c_str()) ||
		!Expect(session->WorkingAsset().heights == savedHeight.pixels &&
			session->WorkingAsset().settings.lodBaseDistance == savedLodDistance &&
			session->WorkingAsset().settings.heightDetailEnabled == terrain.settings.heightDetailEnabled &&
			session->WorkingAsset().settings.heightDetailStrength == terrain.settings.heightDetailStrength &&
			session->WorkingAsset().settings.heightDetailFadeStart == terrain.settings.heightDetailFadeStart &&
			!session->Document()->IsDirty(),
			"Terrain discard did not restore definition and image payloads together"))
		return false;
	session.reset();
	VansAssetDocumentRegistry::Get().Clear();
	return true;
}

bool TestTerrainCdlodSelectionContract()
{
	using namespace VansGraphics;

	TerrainLodSettings invalidSettings;
	invalidSettings.terrainSize = 1000.0f;
	VansTerrainLodSelector invalidSelector;
	std::string error;
	if (!Expect(!invalidSelector.Configure(invalidSettings, &error) && !error.empty(),
		"Terrain CDLOD accepted a non-power-of-two quadtree extent"))
		return false;

	TerrainLodSettings settings;
	settings.terrainSize = 1024.0f;
	settings.minPatchSize = 16.0f;
	settings.minHeight = -32.0f;
	settings.maxHeight = 256.0f;
	settings.baseDistance = 64.0f;
	settings.distanceRatio = 2.0f;
	settings.morphStartRatio = 0.70f;
	VansTerrainLodSelector selector;
	if (!Expect(selector.Configure(settings, &error),
		"Terrain CDLOD rejected a valid regular-grid configuration"))
		return false;

	const std::array<glm::vec3, 4> cameraPositions = {
		glm::vec3(0.0f, 32.0f, 0.0f),
		glm::vec3(310.0f, 70.0f, -170.0f),
		glm::vec3(-505.0f, 20.0f, 505.0f),
		glm::vec3(900.0f, 80.0f, 0.0f)
	};
	constexpr uint32_t edges[] = {
		TerrainEdge_Left, TerrainEdge_Right, TerrainEdge_Top, TerrainEdge_Bottom
	};
	constexpr uint32_t oppositeEdges[] = {
		TerrainEdge_Right, TerrainEdge_Left, TerrainEdge_Bottom, TerrainEdge_Top
	};

	bool observedMultipleLevels = false;
	bool observedLodTransition = false;
	bool observedTessellationBoundary = false;
	bool observedExpectedMorphRange = false;
	for (const glm::vec3& cameraPosition : cameraPositions)
	{
		std::vector<TerrainLodPatch> patches;
		selector.Select(cameraPosition, true, 90.0f, patches);
		if (!Expect(selector.ValidateSelection(patches, &error),
			"Terrain CDLOD emitted an invalid selection"))
			return false;

		std::vector<TerrainLodPatch> repeated;
		selector.Select(cameraPosition, true, 90.0f, repeated);
		if (!Expect(repeated.size() == patches.size(),
			"Terrain CDLOD selection is not deterministic"))
			return false;
		for (size_t i = 0; i < patches.size(); ++i)
		{
			const TerrainLodPatch& a = patches[i];
			const TerrainLodPatch& b = repeated[i];
			if (!Expect(a.gridX == b.gridX && a.gridZ == b.gridZ &&
				a.level == b.level && a.edgeFlags == b.edgeFlags &&
				a.tessellated == b.tessellated,
				"Terrain CDLOD selection order or flags changed for identical input"))
				return false;
		}

		const uint32_t rootCells = selector.GetRootCellCount();
		std::vector<int> cellOwner(static_cast<size_t>(rootCells) * rootCells, -1);
		uint32_t minimumLevel = selector.GetMaxLevel();
		uint32_t maximumLevel = 0;
		for (size_t patchIndex = 0; patchIndex < patches.size(); ++patchIndex)
		{
			const TerrainLodPatch& patch = patches[patchIndex];
			const uint32_t patchCells = 1u << patch.level;
			minimumLevel = std::min(minimumLevel, patch.level);
			maximumLevel = std::max(maximumLevel, patch.level);
			if (patch.level == 1u &&
				std::abs(patch.morphStart - 108.8f) < 0.001f &&
				std::abs(patch.morphEnd - 128.0f) < 0.001f)
			{
				observedExpectedMorphRange = true;
			}

			for (uint32_t z = patch.gridZ; z < patch.gridZ + patchCells; ++z)
			for (uint32_t x = patch.gridX; x < patch.gridX + patchCells; ++x)
			{
				int& owner = cellOwner[static_cast<size_t>(z) * rootCells + x];
				if (!Expect(owner == -1, "Terrain CDLOD patches overlap"))
					return false;
				owner = static_cast<int>(patchIndex);
			}
		}
		observedMultipleLevels |= minimumLevel != maximumLevel;
		if (!Expect(std::find(cellOwner.begin(), cellOwner.end(), -1) == cellOwner.end(),
			"Terrain CDLOD left an uncovered terrain cell"))
			return false;

		auto ownerAt = [&](int64_t x, int64_t z) -> int
		{
			if (x < 0 || z < 0 || x >= rootCells || z >= rootCells)
				return -1;
			return cellOwner[static_cast<size_t>(z) * rootCells + static_cast<size_t>(x)];
		};

		for (size_t patchIndex = 0; patchIndex < patches.size(); ++patchIndex)
		{
			const TerrainLodPatch& patch = patches[patchIndex];
			const uint32_t patchCells = 1u << patch.level;
			for (size_t edgeIndex = 0; edgeIndex < std::size(edges); ++edgeIndex)
			{
				const uint32_t edge = edges[edgeIndex];
				for (uint32_t offset = 0; offset < patchCells; ++offset)
				{
					int64_t neighborX = patch.gridX;
					int64_t neighborZ = patch.gridZ;
					if (edge == TerrainEdge_Left) { neighborX -= 1; neighborZ += offset; }
					if (edge == TerrainEdge_Right) { neighborX += patchCells; neighborZ += offset; }
					if (edge == TerrainEdge_Top) { neighborX += offset; neighborZ -= 1; }
					if (edge == TerrainEdge_Bottom) { neighborX += offset; neighborZ += patchCells; }
					const int neighborIndex = ownerAt(neighborX, neighborZ);
					if (neighborIndex < 0 || neighborIndex == static_cast<int>(patchIndex))
						continue;

					const TerrainLodPatch& neighbor = patches[static_cast<size_t>(neighborIndex)];
					const int levelDelta = static_cast<int>(neighbor.level) - static_cast<int>(patch.level);
					if (!Expect(std::abs(levelDelta) <= 1,
						"Terrain CDLOD violates the 2:1 neighbor rule"))
						return false;

					const bool levelTransition = levelDelta != 0;
					const bool tessellationTransition = levelTransition ||
						patch.tessellated != neighbor.tessellated;
					const uint32_t opposite = oppositeEdges[edgeIndex];
					if (!Expect(((patch.edgeFlags >> 4u) & edge) != 0u == levelTransition &&
						((neighbor.edgeFlags >> 4u) & opposite) != 0u == levelTransition,
						"Terrain CDLOD LOD-transition flags are not symmetric"))
						return false;
					if (!Expect(((patch.edgeFlags >> 8u) & edge) != 0u == tessellationTransition &&
						((neighbor.edgeFlags >> 8u) & opposite) != 0u == tessellationTransition,
						"Terrain tessellation-boundary flags are not symmetric"))
						return false;
					if (!Expect(((patch.edgeFlags & edge) != 0u) == (levelDelta > 0),
						"Terrain fine-side coarser-neighbor flag is incorrect"))
						return false;

					observedLodTransition |= levelTransition;
					observedTessellationBoundary |= tessellationTransition;
				}
			}
		}
	}

	return Expect(observedMultipleLevels && observedLodTransition &&
		observedTessellationBoundary && observedExpectedMorphRange,
		"Terrain CDLOD test did not exercise LOD, morph, and tessellation transitions");
}

bool TestDecalRenderingContract();
bool TestImpactDecalRuntimeContract();
bool TestDecalGpuContract();
bool TestReflectionProbeSpatialIndexContract();
bool TestReflectionProbePlacementContract();
bool MeasureReflectionProbePlacement(const char*, const char*, const char*);
bool TestProbeSceneDefaultsContract(const char* workspaceRoot);
bool TestTriangleGeometryQueryContract();
bool TestMeshGeometryGpuContract(bool deviceOnly);
bool TestReflectionProbeGpuContract(const char* scenePath);
bool TestReflectionProbePagesGpuContract();
bool TestReflectionProbePublicationGpuContract();
bool TestReflectionProbeResourcesGpuContract();
bool TestGIProbeResourcesGpuContract();
bool TestGIWorldContract();
bool TestReflectionProbeCacheContract();
bool TestReflectionProbeCacheGpuContract();
bool TestGIProbeWorkContract(const Vans::VansSerializedValue& environment);
bool TestGIProbeLayoutContract();
bool TestGIProbeLayoutGpuContract();
bool TestGIProbeSamplingGpuContract();
bool TestGIReceiverVisibilityGpuContract();
bool TestGIProbeFeedbackGpuContract();
bool TestGIProbePublicationGpuContract();
bool TestGIProbeIntegrationGpuContract();
bool TestSkyLightingGpuContract();
bool TestSSGIGpuContract();
bool TestGrassLightingGpuContract();
bool TestParticleCoreContract();
bool TestDescriptorLayoutSharingContract();

bool TestLightCookieContract()
{
    using namespace VansGraphics;
    VansLightManager lights;
    VansRenderLightFrameData frame;
    lights.AddDirectionalLight(VansDirectionalLight{});
    lights.AddPointLight(VansPointLight{}); lights.AddPointLight(VansPointLight{});
    lights.AddSpotLight(VansSpotLight{}); lights.AddRectLight(VansRectLight{});
    frame.directionalLights.resize(1); frame.directionalLights[0].m_Direction = {0,0,-1};
    frame.pointLights.resize(2); frame.spotLights.resize(1); frame.rectLights.resize(1);
    frame.spotLights[0].m_OuterCutOff = glm::radians(30.0f);
    lights.BuildCookieFrame(frame);
    for (const auto& c : frame.cookies.data) if (c.options.x != 0) return false;
    for (unsigned kind = 0; kind < 4; ++kind)
    {
        auto& c = lights.Cookie(kind,0); c.enabled = true; c.textureGuid = "test-cookie";
        c.strength = 0.75f; c.sizeX = 4; c.sizeY = 6;
        glm::mat4 world = glm::translate(glm::mat4(1),glm::vec3(3,4,5));
        lights.SetCookieTransform(kind,0,world);
    }
    lights.BuildCookieFrame(frame);
    for (unsigned kind = 0; kind < 4; ++kind)
    {
        const unsigned slot = VansLightCookieOffset(kind); const auto& c = frame.cookies.data[slot];
        if (c.options.x != 0.75f || c.options.y != float(kind) || frame.cookies.textures[slot] != "test-cookie") return false;
        const auto p = c.worldToLight * glm::vec4(3,4,5,1);
        if (glm::length(glm::vec3(p)) > 1e-5f) return false;
    }
    const auto& spot = frame.cookies.data[65];
    const auto edge = spot.worldToLight * glm::vec4(3 + std::tan(glm::radians(30.0f))*5,4,0,1);
    if (std::abs(edge.x / (-edge.z * spot.projection.x) - 0.5f) > 1e-5f) return false;
    // 同一帧对象重复构建时，关闭必须清掉旧结果。
    lights.Cookie(2,0).enabled = false; lights.BuildCookieFrame(frame);
    if (frame.cookies.data[65].options.x != 0 || !frame.cookies.textures[65].empty()) return false;
    // 删除采用 swap-pop；Cookie 必须跟随被交换的灯而不是旧槽位。
    lights.Cookie(1,1).textureGuid = "second";
    if (!lights.RemovePointLight(0) || lights.Cookie(1,0).textureGuid != "second") return false;
    // Authoring may contain more lights than the rendered prefix; CPU settings must not overlap kinds.
    for (int i = 0; i < 80; ++i) lights.AddPointLight(VansPointLight{});
    lights.Cookie(1,80).enabled = true;
    if (lights.Cookie(2,0).enabled) return false;
    lights.ClearLights();
    lights.AddDirectionalLight(VansDirectionalLight{}); lights.AddPointLight(VansPointLight{});
    if (lights.Cookie(0,0).enabled || lights.Cookie(1,0).enabled) return false;
    std::cout << "LIGHT_COOKIE_CONTRACT_PASS defaults=4 transforms=4 spotEdge=1 disable=1 remove=1 clear=1 ABI=unchanged\n";
    return true;
}

bool RunModelLodContractTests();
bool RunTreeLodGpuContractTests();
bool TestEditorSceneInteractionContract();

bool RunCursorContractTests();
bool RunCursorWindowContractTests();
bool RunCursorProjectContractTests();

void RunPrefabContractTests();

int main(int argc, char** argv)
{
	std::string engineRoot;
	std::string engineRootError;
	if (!Vans::VansEnginePaths::DiscoverEngineRoot(engineRoot, engineRootError) ||
		!Vans::VansProjectManager::Get().ConfigureEngineRoot(engineRoot, engineRootError))
	{
		std::cerr << "ENGINE_ROOT_CONFIGURATION_FAIL " << engineRootError << '\n';
		return 211;
	}
    if (argc == 2 && std::string(argv[1]) == "--prefab")
    {
        VANS_INIT_MAIN_THREAD();
        try { RunPrefabContractTests(); return 0; }
        catch (const std::exception& error) { std::cerr << "PREFAB_CONTRACT_FAIL " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && std::string(argv[1]) == "--editor-scene-interaction") return TestEditorSceneInteractionContract() ? 0 : 209;
    if (argc == 2 && std::string(argv[1]) == "--light-cookie") return TestLightCookieContract() ? 0 : 1;
	if (argc == 2 && std::string(argv[1]) == "--pcg-core")
		return RunPcgCoreContractTests() ? 0 : 203;
    if (argc == 2 && std::string(argv[1]) == "--terrain-cdlod")
        return TestTerrainCdlodSelectionContract() ? 0 : 188;
    if (argc == 2 && std::string(argv[1]) == "--descriptor-layout-sharing")
        return TestDescriptorLayoutSharingContract() ? 0 : 201;
    if (argc == 2 && std::string(argv[1]) == "--particle-core")
        return TestParticleCoreContract() ? 0 : 170;
	VANS_INIT_MAIN_THREAD();
	if (argc == 2 && std::string(argv[1]) == "--gameplay-frame-order")
		return TestGameplayFrameOrder() ? 0 : 3;
    if (argc == 2 && std::string(argv[1]) == "--recent-projects") return TestRecentProjectsPruningContract() ? 0 : 1;
    if (argc == 2 && std::string(argv[1]) == "--cursor") return RunCursorContractTests() ? 0 : 1;
    if (argc == 2 && std::string(argv[1]) == "--cursor-projects") return RunCursorProjectContractTests() ? 0 : 1;
    if (argc == 2 && std::string(argv[1]) == "--cursor-window") return RunCursorWindowContractTests() ? 0 : 1;
    if(argc==2 && std::string(argv[1])=="--model-lod")return RunModelLodContractTests()?0:207;
    if(argc==2 && std::string(argv[1])=="--tree-lod-gpu")return RunTreeLodGpuContractTests()?0:208;
	if (argc == 2 && std::string(argv[1]) == "--pcg-assets")
		return RunPcgAssetContractTests() ? 0 : 204;
	if (argc == 2 && std::string(argv[1]) == "--pcg-editor-configuration")
		return RunPcgEditorConfigurationContractTests() ? 0 : 206;
	if (argc == 3 && std::string(argv[1]) == "--pcg-project")
		return RunPcgProjectContractTests(argv[2]) ? 0 : 205;
	if (argc == 2 && std::string(argv[1]) == "--terrain-authoring")
		return TestTerrainAuthoringContract() ? 0 : 202;
    if (argc == 5 && std::string(argv[1]) == "--reflection-probe-placement-scene")
        return MeasureReflectionProbePlacement(argv[2], argv[3], argv[4]) ? 0 : 186;
    if (argc == 2 && std::string(argv[1]) == "--gi-receiver-visibility-gpu")
        return TestGIReceiverVisibilityGpuContract() ? 0 : 194;
    if (argc == 2 && std::string(argv[1]) == "--grass-lighting-gpu")
        return TestGrassLightingGpuContract() ? 0 : 199;
	if (argc == 2 && std::string(argv[1]) == "--ssgi-gpu")
        return TestSSGIGpuContract() ? 0 : 199;
	if (argc == 2 && std::string(argv[1]) == "--sky-lighting-gpu")
        return TestSkyLightingGpuContract() ? 0 : 198;
	if (argc == 2 && std::string(argv[1]) == "--gi-world")
		return TestGIWorldContract() ? 0 : 201;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-resources-gpu")
		return TestGIProbeResourcesGpuContract() ? 0 : 197;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-resources-gpu")
		return TestReflectionProbeResourcesGpuContract() ? 0 : 196;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-cache")
		return TestReflectionProbeCacheContract() ? 0 : 194;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-cache-gpu")
		return TestReflectionProbeCacheGpuContract() ? 0 : 195;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-publication-gpu")
		return TestReflectionProbePublicationGpuContract() ? 0 : 192;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-pages-gpu")
		return TestReflectionProbePagesGpuContract() ? 0 : 188;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-work")
		return TestGIProbeWorkContract(BuildValidEnvironmentSettingsForTest()) ? 0 : 189;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-integration-gpu")
        return TestGIProbeIntegrationGpuContract() ? 0 : 193;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-publication-gpu")
        return TestGIProbePublicationGpuContract() ? 0 : 193;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-feedback-gpu")
        return TestGIProbeFeedbackGpuContract() ? 0 : 193;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-layout-gpu")
		return TestGIProbeLayoutGpuContract() ? 0 : 191;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-sampling-gpu")
		return TestGIProbeSamplingGpuContract() ? 0 : 191;
	if (argc == 2 && std::string(argv[1]) == "--gi-probe-layout")
		return TestGIProbeLayoutContract() ? 0 : 190;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-placement")
		return TestReflectionProbePlacementContract() ? 0 : 186;
	if (argc == 3 && std::string(argv[1]) == "--probe-scene-defaults")
		return TestProbeSceneDefaultsContract(argv[2]) ? 0 : 187;
	if (argc == 2 && std::string(argv[1]) == "--mesh-geometry-gpu")
		return TestMeshGeometryGpuContract(false) ? 0 : 185;
	if (argc == 2 && std::string(argv[1]) == "--mesh-geometry-gpu-device")
		return TestMeshGeometryGpuContract(true) ? 0 : 185;
	if (argc == 2 && std::string(argv[1]) == "--geometry-query")
		return TestTriangleGeometryQueryContract() ? 0 : 184;
	if ((argc == 2 || argc == 3) && std::string(argv[1]) == "--reflection-probe-gpu")
		return TestReflectionProbeGpuContract(argc == 3 ? argv[2] : nullptr) ? 0 : 183;
	if (argc == 2 && std::string(argv[1]) == "--reflection-probe-index")
		return TestReflectionProbeSpatialIndexContract() ? 0 : 182;
	if (argc == 2 && std::string(argv[1]) == "--decal-rendering")
		return TestDecalRenderingContract() ? 0 : 180;
	if (argc == 2 && std::string(argv[1]) == "--impact-decal")
		return TestImpactDecalRuntimeContract() ? 0 : 180;
	if (argc == 2 && std::string(argv[1]) == "--decal-gpu")
		return TestDecalGpuContract() ? 0 : 181;
	if (argc == 2 && std::string(argv[1]) == "--package-manifest")
		return TestPackageManifestRoundTrip() ? 0 : 1;
	if (argc == 2 && std::string(argv[1]) == "--packaged-resource-plan")
		return TestPackagedAudioResourcePlanRoundTrip() &&
			TestMediaComponentGuidProjection() &&
			TestSceneProjectionStrictAdmission() ? 0 : 168;
	if (argc == 2 && std::string(argv[1]) == "--animation-clip-memory-asset")
		return TestAnimationClipMemoryAssetContract() ? 0 : 165;
	if (argc == 2 && std::string(argv[1]) == "--project-asset-memory-bootstrap")
		return TestProjectAssetMemoryBootstrapContract() ? 0 : 166;
	if (argc == 2 && std::string(argv[1]) == "--demohall-scene-projection")
		return TestProjectAssetMemoryBootstrapContract(
			false, "DemoHallProject") ? 0 : 216;
	if (argc == 2 && std::string(argv[1]) == "--runtime-config-memory-boundary")
		return TestRuntimeConfigurationMemoryBoundaryContract() ? 0 : 167;
	if (argc == 2 && std::string(argv[1]) == "--vegetation-memory-asset")
		return TestVegetationMemoryAssetContract() ? 0 : 160;
	if (argc == 2 && std::string(argv[1]) == "--ui-memory-assets")
		return TestUIAssetMemoryBootstrapContract() ? 0 : 162;
	if (argc == 2 && std::string(argv[1]) == "--ui-action-event")
		return TestUIActionEventContract() && TestLuaUIActionEventContract() ? 0 : 210;
	if (argc == 2 && std::string(argv[1]) == "--lua-runtime-ui")
		return TestLuaUIActionEventContract() &&
			TestLuaUIValueCodecContract() &&
			TestLuaUIStateIsolationContract() &&
			TestLuaScriptDeclaredAssetDependencyContract() ? 0 : 211;
	if (argc == 2 && std::string(argv[1]) == "--asset-working-copy-memory")
		return TestAssetWorkingCopyMemoryPublicationContract() ? 0 : 163;
	if (argc == 2 && std::string(argv[1]) == "--generated-material-memory")
		return TestGeneratedMaterialMemoryBoundaryContract() ? 0 : 164;
	if (argc == 2 && std::string(argv[1]) == "--asset-object-repository")
		return TestAssetObjectRepositoryContract() ? 0 : 159;
	if (argc == 2 && std::string(argv[1]) == "--authoring-codecs")
		return TestAuthoringCodecContract() ? 0 : 157;
	if (argc == 2 && std::string(argv[1]) == "--project-settings-explicit-save")
		return TestProjectSettingsExplicitSaveContract() ? 0 : 155;
	if (argc == 2 && std::string(argv[1]) == "--scene-memory-load")
		return TestSceneMemoryDependencyPlanContract() &&
			TestSceneResourceFailurePropagationContract() ? 0 : 156;
	if (argc == 2 && std::string(argv[1]) == "--runtime-world-components")
		return TestRuntimeWorldComponentEnabledContract() &&
			TestRuntimeWorldComponentLifetimeContract() &&
			TestRuntimeWorldClearInvalidatesHandlesContract() &&
			TestRuntimeComponentKeyCanonicalizationContract() &&
			TestRuntimeWorldCommandBufferContract() ? 0 : 161;
	if (argc == 2 && std::string(argv[1]) == "--atmosphere")
		return TestAtmosphereMathAndDataContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--water-rendering")
		return TestWaterRenderingRefactorContract() ? 0 : 141;
	if (argc == 2 && std::string(argv[1]) == "--skin-rendering")
	{
		const auto runSkinContract = [](const char* name, bool (*test)())
		{
			if (test())
				return true;
			std::cerr << "[ForestContractTests] Skin contract failed: " << name << '\n';
			return false;
		};
		return runSkinContract("profile-json-round-trip", TestSkinProfileJsonRoundTrip) &&
			runSkinContract("profile-json-aliases", TestSkinProfileJsonAliasDecode) &&
			runSkinContract("material-profile-projection", TestSkinProfileMaterialProjectionContract) &&
			runSkinContract("profile-lut-generation", TestSkinProfileLUTGenerationContract) &&
			runSkinContract("default-textures", TestSkinDefaultTextureContract) ? 0 : 145;
	}
	if (argc == 2 && std::string(argv[1]) == "--render-system")
		return TestRenderSystemLifecycleContract() &&
			TestRenderSystemStartupFailureContract() &&
			TestRenderSystemDestructorFallbackContract() &&
			TestRenderSystemPrepareFailureContract() &&
			TestRenderSystemOneFrameLeadContract() &&
			TestRenderFramePacketContract() &&
			TestMainCameraVisibilityBackendOwnershipContract() &&
			TestPunctualShadowBackendOwnershipContract() &&
			TestRenderWorldContract() &&
			TestRenderOutcomeLedgerContract() &&
			TestDrawSubmissionContract() &&
			TestGIProbeUpdateScheduleContract() &&
			TestFramePhaseThreadLocalContract() ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--upscaler")
		return TestUnifiedUpscalerHistoryContract() &&
			TestUnifiedUpscalerResolutionContract() &&
			TestUnifiedUpscalerManagerContract() &&
			TestUnifiedUpscalerJitterContract() &&
			TestFSRTemporalProjectionContract() &&
			TestVulkanDeviceDepthRangeContract() ? 0 : 219;
	if (argc == 2 && std::string(argv[1]) == "--frame-submit")
		return TestAsyncComputeSubmitGraphContract() && TestFrameSubmitRetirementContract() ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--audio-environment")
		return TestMediaDecodeSessionContract() && TestAudioDistanceAttenuationContract() &&
			TestAudioBusContract() && TestAudioMixConfigContract() &&
			TestAudioOcclusionContract() && TestAudioDirectionalityContract() &&
			TestAudioComponentOcclusionReadContract() && TestAudioPreviewSettingsContract() &&
			TestAudioVoiceVirtualizationContract() &&
			TestAudioReverbEnvironmentContract() && TestAudioReverbZoneRuntimeProjection()
			&& TestAudioReverbPresetAssetContract() && TestAudioBusSnapshotAssetContract()
			&& TestAudioDuckingRulesAssetContract() && TestScriptLightIndexRebindFacadeContract()
			&& TestAudioSourcePoolContract() ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--profiler")
		return TestProfilerSnapshotContract() && TestProfilerStableCaptureContract()
            && TestProfilerOutOfOrderCompletionContract() ? 0 : 140;
	if (argc == 2 && std::string(argv[1]) == "--asset-policies")
		return TestAssetPolicies() ? 0 : 2;
	if (argc == 2 && std::string(argv[1]) == "--asset-type-serialization")
		return TestAssetTypeSerializationContract() ? 0 : 136;
	if (argc == 2 && std::string(argv[1]) == "--navigation-ai")
		return RunNavigationAIContractTests() ? 0 : 148;
	if (argc == 2 && std::string(argv[1]) == "--animation-project-assets")
		return TestAnimationProjectAnimatorAssetsCanonicalContract() ? 0 : 137;
	if (argc == 2 && std::string(argv[1]) == "--current-project-timelines")
		return TestCurrentProjectTimelineAssetsContract() ? 0 : 217;
	if (argc == 2 && std::string(argv[1]) == "--camera-control")
		return TestCameraControlArbiterContract() ? 0 : 218;
	if (argc == 2 && std::string(argv[1]) == "--demohall-pistol-poses")
		return TestSurvivalPistolPoseContract("DemoHallProject", "Assets") ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--demohall-pistol-overlay")
		return TestSurvivalPistolOverlayContract("DemoHallProject", "Assets", "DemoHall.json") ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--dustv3-pistol-poses")
		return TestSurvivalPistolPoseContract("DustV3Project", "Assets/Survival") ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--dustv3-pistol-overlay")
		return TestSurvivalPistolOverlayContract("DustV3Project", "Assets/Survival", "MainScene.json") ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--demohall-survival-back-axe")
		return TestDemoHallSurvivalBackAxeSceneContract() ? 0 : 138;
	if (argc == 2 && std::string(argv[1]) == "--scene-entity-factory")
		return TestEmptySceneEntityFactoryContract() ? 0 : 134;
	if (argc == 2 && std::string(argv[1]) == "--local-volumetric-fog")
		return TestLocalVolumetricFogEntityFactoryContract() &&
			TestLocalVolumetricFogFieldPreviewContract() &&
			TestLocalVolumetricFogRuntimePreviewProjectionContract() &&
			TestLocalVolumetricFogFieldDependencyContract() ? 0 : 146;
	if (argc == 2 && std::string(argv[1]) == "--volumetric-particles")
		return TestVolumetricParticleInjectionContract() ? 0 : 169;
	if (argc == 2 && std::string(argv[1]) == "--projectile-smoke")
		return TestProjectileSmokeContract() ? 0 : 170;
	if (argc == 2 && std::string(argv[1]) == "--transform-graph")
		return TestTransformGraphAnchorContract() ? 0 : 135;
	if (argc == 2 && std::string(argv[1]) == "--gaf-packaging")
		return TestGAFPackagingContract() ? 0 : 103;
	if (argc == 2 && std::string(argv[1]) == "--gaf-asset-schema")
	{
		if (!TestGAFGameplayTagsContract()) return 94;
		if (!TestGAFTargetingContract()) return 97;
		return TestGAFAssetSchemaAndCookContract() ? 0 : 102;
	}
	if (argc == 2 && std::string(argv[1]) == "--gaf-core")
	{
		if (!TestGAFDefinitionAndServiceContract())
		{
			std::cerr << "[GAF core] definition/service contract failed\n";
			return 98;
		}
		if (!TestGAFResourceLedgerAndTaskContract())
		{
			std::cerr << "[GAF core] resource/task contract failed\n";
			return 99;
		}
		if (!TestGAFExecutionGraphContract())
		{
			std::cerr << "[GAF core] execution graph contract failed\n";
			return 100;
		}
		if (!TestGAFActionHostLifecycleContract())
		{
			std::cerr << "[GAF core] Action Host lifecycle contract failed\n";
			return 101;
		}
		return 0;
	}
	if (argc == 2 && std::string(argv[1]) == "--timeline-registry")
		return TestTimelineRegistryContract() && TestTimelineSerializationContract() &&
			TestTimelinePropertyTransformContract() &&
			TestTimelineCompileEvaluateContract() ? 0 : 58;
	if (argc == 2 && std::string(argv[1]) == "--timeline-runtime")
		return TestTimelinePointAndRangeContract() && TestTimelineSessionContract() &&
			TestTimelineSessionFailureTransactionContract() &&
			TestTimelineStationaryContinuousContract() && TestTimelineSubTimelineContract() &&
			TestTimelinePreAnimatedStackContract() ? 0 : 219;
	if (argc == 2 && std::string(argv[1]) == "--timeline-event")
		return TestTimelineEventContract() ? 0 : 155;
	if (argc == 2 && std::string(argv[1]) == "--event-dispatch-mutation")
		return TestEventDispatchMutationContract() ? 0 : 156;
	if (argc == 2 && std::string(argv[1]) == "--gaf-demohall-window-break")
		return TestGAFDemoHallWindowBreakContract() ? 0 : 108;
	if (argc == 2 && std::string(argv[1]) == "--gaf-demohall-player-attack")
		return TestGAFDemoHallPlayerAttackContract() ? 0 : 140;
	if (argc == 2 && std::string(argv[1]) == "--demohall-player-throw")
		return TestDemoHallPlayerThrowContract() ? 0 : 154;
	if (argc == 2 && std::string(argv[1]) == "--gaf-demohall-pistol-hit")
		return TestGAFDemoHallPistolHitRuntimeContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--gaf-damage")
		return TestGAFDamageRuntimeContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--hit-feedback")
		return TestHitFeedbackScriptContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--death-hit-reaction")
		return TestDeathHitReactionScriptContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--weapon-death-drop")
		return TestWeaponDeathDropContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--gaf-pistol-audio")
		return TestGAFPistolAudioRuntimeContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--gaf-demohall-melee-hit")
		return TestGAFDemoHallMeleeHitRuntimeContract() ? 0 : 153;
	if (argc == 2 && std::string(argv[1]) == "--demohall-hurt-bodies")
		return TestDemoHallHurtBodiesContract() ? 0 : 153;
	if (argc == 2 && std::string(argv[1]) == "--demohall-crouch-locomotion")
		return TestDemoHallCrouchLocomotionContract() ? 0 : 147;
	if (argc == 2 && std::string(argv[1]) == "--demohall-motion-matching-movement")
		return TestDemoHallMotionMatchingMovementLibraryContract() ? 0 : 216;
	if (argc == 2 && std::string(argv[1]) == "--demohall-player-vault")
		return TestDemoHallPlayerVaultContract() ? 0 : 144;
	if (argc == 2 && std::string(argv[1]) == "--demohall-whisper-ai")
		return TestDemoHallWhisperAIContract() ? 0 : 145;
	if (argc == 2 && std::string(argv[1]) == "--character-motion")
		return TestCharacterTrajectoryGeneratorContract() ? 0 : 141;
	if (argc == 2 && std::string(argv[1]) == "--motion-matching-turn-warping")
		return (TestTurnInPlaceWarpingMathContract()
			&& TestMotionMatchingCameraFacingTurnContract()) ? 0 : 110;
	if (argc == 2 && std::string(argv[1]) == "--animation-editor-preview")
		return TestAnimationEditorPreviewPolicyContract() ? 0 : 143;
	if (argc == 2 && std::string(argv[1]) == "--animation-preview-rig-identity")
		return TestAnimatorRuntimeCompilerContract() ? 0 : 143;
	if (argc == 2 && std::string(argv[1]) == "--animation-socket-attachment-authoring")
		return TestAnimationSocketAttachmentAuthoringContract() ? 0 : 154;
	if (argc == 2 && std::string(argv[1]) == "--procedural-animation")
		return RunProceduralAnimationContractTests() ? 0 : 129;
	if (argc == 2 && std::string(argv[1]) == "--procedural-animation-integration")
		return TestAnimationTargetPostProcessContract()
			&& TestAnimationV2RetargetSceneContract()
			&& TestProjectRetargetOwnedSkeletonAndSkinningContract()
			&& TestRetargetUnmappedTargetBoneInheritanceContract()
			&& TestRetargetConfiguredLimbChainContract()
			&& RunProceduralAnimationContractTests() ? 0 : 130;
	if (argc == 2 && std::string(argv[1]) == "--target-post-process")
		return TestAnimationTargetPostProcessContract() ? 0 : 53;
	if (argc == 2 && std::string(argv[1]) == "--retarget-contracts")
		return TestProjectRetargetOwnedSkeletonAndSkinningContract()
			&& TestRetargetUnmappedTargetBoneInheritanceContract()
			&& TestRetargetConfiguredLimbChainContract() ? 0 : 120;
	if (argc == 2 && std::string(argv[1]) == "--retarget-runtime-assets")
		return TestProjectRetargetOwnedSkeletonAndSkinningContract() ? 0 : 131;
	if (argc == 2 && std::string(argv[1]) == "--animation-graph-sets")
		return TestAnimationGraphSetSwitchRuntimeContract() ? 0 : 132;
	if (argc == 2 && std::string(argv[1]) == "--animation-layer-root-frame")
		return TestAnimationLayerRootReferenceFrameContract() ? 0 : 139;
	if (argc == 2 && std::string(argv[1]) == "--animation-authoring-boundary")
		return TestAnimationAuthoringBoundaryContract() ? 0 : 191;
	if (argc == 2 && std::string(argv[1]) == "--animation-graph-sets-integration")
		return TestAnimatorCanonicalFormatContract()
			&& TestAnimationProjectAnimatorAssetsCanonicalContract()
			&& TestAnimationAuthoringBoundaryContract()
			&& TestAnimatorRuntimeCompilerContract()
			&& TestAnimationLayerStackRuntimeContract()
			&& TestAnimationLayerRootReferenceFrameContract()
			&& TestAnimationGraphSetSwitchRuntimeContract()
			&& TestAnimationSlotRuntimeContract()
			&& TestAnimationHotReloadStateTransferContract()
			&& TestAnimationMarkerSyncLayerContract()
			&& TestAnimationTargetPostProcessContract()
			&& TestAnimationSyncedGraphStateContract() ? 0 : 133;
	if (argc == 2 && std::string(argv[1]) == "--animation-v2-scene")
		return TestAnimationV2RetargetSceneContract() ? 0 : 115;
	if (argc == 2 && std::string(argv[1]) == "--point-shadow-atlas-policy")
		return TestPointShadowAtlasUpdatePolicy() ? 0 : 128;
	if (!TestProjectSettingsExplicitSaveContract())
		return 155;
	if (!TestSceneMemoryDependencyPlanContract())
		return 156;
	if (!TestAuthoringCodecContract())
		return 157;
	if (!TestAssetObjectRepositoryContract())
		return 159;
	if (!TestAnimationClipMemoryAssetContract())
		return 165;
	if (!TestRuntimeConfigurationMemoryBoundaryContract())
		return 167;
	if (!TestVegetationMemoryAssetContract())
		return 160;
	if (!TestUIAssetMemoryBootstrapContract())
		return 162;
	if (!TestAssetWorkingCopyMemoryPublicationContract())
		return 163;
	if (!TestGeneratedMaterialMemoryBoundaryContract())
		return 164;
	if (!TestPointShadowAtlasUpdatePolicy())
		return 128;
	if (!RunNavigationAIContractTests())
		return 148;
	if (!RunPcgCoreContractTests())
		return 203;
    if(!RunModelLodContractTests())return 207;
	if (!RunPcgAssetContractTests())
		return 204;
	if (!TestDecalRenderingContract())
		return 180;
	if (!TestDrawSubmissionContract())
		return 127;
	if (!TestTerrainAuthoringContract())
		return 202;
	if (!TestTerrainCdlodSelectionContract())
		return 188;
	if (!TestLuaInspectorProjectModuleSearchPathContract())
		return 125;
	if (!TestSceneResourceArtifactPrewarmContract())
		return 126;
	if (!TestUnifiedUpscalerHistoryContract())
		return 121;
	if (!TestUnifiedUpscalerResolutionContract())
		return 122;
	if (!TestUnifiedUpscalerManagerContract())
		return 123;
	if (!TestUnifiedUpscalerJitterContract())
		return 124;
	if (!TestAsyncComputeSubmitGraphContract() || !TestFrameSubmitRetirementContract())
		return 93;
	if (!TestFSRTemporalProjectionContract())
		return 116;
	if (!TestVulkanDeviceDepthRangeContract())
		return 117;
	if (!TestSkyFSRPipelineContract())
		return 118;
	if (!TestCharacterTrajectoryGeneratorContract())
		return 111;
	if (!TestMotionMatchingRootMotionRigContract())
		return 109;
	if (!TestMotionMatchingAutoBuildLocomotionMetadataContract())
		return 31;
	if (!TestDemoHallMotionMatchingMovementLibraryContract())
		return 216;
	if (!TestTurnInPlaceWarpingMathContract())
		return 142;
	if (!TestAnimationEditorPreviewPolicyContract())
		return 143;
	if (!TestMotionMatchingCameraFacingTurnContract())
		return 110;
	if (!TestCameraControlArbiterContract())
		return 82;
	if (!TestRootMotionSteeringContract())
		return 112;
	if (!TestRootMotionReconciliationContract())
		return 114;
	if (!TestMotionMatchingPivotDirectionContract())
		return 113;
	if (!TestAnimationV2RetargetSceneContract())
		return 115;
	if (!TestDemoHallSurvivalBackAxeSceneContract())
		return 138;
	if (!TestProjectRetargetOwnedSkeletonAndSkinningContract())
		return 131;
	if (!TestRetargetUnmappedTargetBoneInheritanceContract())
		return 119;
	if (!TestRetargetConfiguredLimbChainContract())
		return 120;
	if (!TestGAFGameplayTagsContract())
		return 94;
	if (!TestGAFAttributesContract())
		return 95;
	if (!TestGAFCuesAndEffectsContract())
		return 96;
	if (!TestGAFTargetingContract())
		return 97;
	if (!TestGAFDefinitionAndServiceContract())
		return 98;
	if (!TestGAFResourceLedgerAndTaskContract())
		return 99;
	if (!TestGAFExecutionGraphContract())
		return 100;
	if (!TestGAFActionHostLifecycleContract())
		return 101;
	if (!TestGAFAssetSchemaAndCookContract())
		return 102;
	if (!TestGAFDemoHallWindowBreakContract())
		return 108;
	if (!TestGAFDemoHallPlayerAttackContract())
		return 140;
	if (!TestDemoHallPlayerThrowContract())
		return 154;
	if (!TestProjectileSmokeContract())
		return 170;
	if (!TestGAFDemoHallMeleeHitRuntimeContract())
		return 153;
	if (!TestDemoHallCrouchLocomotionContract())
		return 147;
	if (!TestDemoHallPlayerVaultContract())
		return 144;
	if (!TestDemoHallWhisperAIContract())
		return 145;
	if (!TestGAFPackagingContract())
		return 103;
	if (!TestGAFDebugAndReplayContract())
		return 105;
	if (!TestGAFSampleLibraryContract())
		return 106;
	if (!TestGAFLuaBridgeContract())
		return 107;
    if (!TestPackageManifestRoundTrip())
        return 1;
	if (!TestSkinProfileJsonRoundTrip())
		return 76;
	if (!TestSkinProfileJsonAliasDecode())
		return 77;
	if (!TestSkinProfileMaterialProjectionContract())
		return 78;
	if (!TestSkinProfileLUTGenerationContract())
		return 79;
	if (!TestSkinDefaultTextureContract())
		return 145;
    if (!TestAssetPolicies())
        return 2;
	if (!TestWaterRenderingRefactorContract())
		return 141;
	if (!TestAtmosphereMathAndDataContract())
		return 144;
	if (!TestAssetTypeSerializationContract())
		return 136;
    if (!TestGameplayFrameOrder())
        return 3;
	if (!TestTimelineRegistryContract())
		return 58;
	if (!TestTimelinePropertyTransformContract())
		return 157;
	if (!TestTimelineSerializationContract())
		return 59;
	if (!TestTimelineEditorInteractionContract())
		return 76;
	if (!TestTimelineCompileEvaluateContract())
		return 72;
	if (!TestTimelineGenericExtensionContract())
		return 80;
	if (!TestTimelinePointAndRangeContract())
		return 81;
	if (!TestTimelineExternalClockContract())
		return 83;
	if (!TestTimelineSessionContract())
		return 73;
	if (!TestTimelineSessionFailureTransactionContract())
		return 85;
	if (!TestTimelineStationaryContinuousContract())
		return 86;
	if (!TestTimelinePreAnimatedStackContract())
		return 63;
	if (!TestTimelineTimeContract())
		return 65;
	if (!TestTimelineEventContract())
		return 66;
	if (!TestTimelineSubTimelineContract())
		return 67;
	if (!TestEmptySceneEntityFactoryContract())
		return 134;
	if (!TestLocalVolumetricFogEntityFactoryContract())
		return 146;
	if (!TestLocalVolumetricFogFieldPreviewContract())
		return 157;
	if (!TestLocalVolumetricFogRuntimePreviewProjectionContract())
		return 158;
	if (!TestLocalVolumetricFogFieldDependencyContract())
		return 146;
	if (!TestTransformGraphAnchorContract())
		return 135;
	if (!TestRuntimeWorldEntityLifetimeContract())
        return 23;
    if (!TestRuntimeWorldParentEditContract())
        return 27;
    if (!TestRuntimeWorldComponentEnabledContract())
        return 24;
    if (!TestRuntimeWorldComponentLifetimeContract())
        return 29;
	if (!TestRuntimeWorldClearInvalidatesHandlesContract())
		return 31;
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
	if (!TestUIActionEventContract())
		return 210;
	if (!TestLuaUIActionEventContract())
		return 210;
    if (!TestScriptObjectOwnedTransformReleaseContract())
        return 33;
    if (!TestScriptLightIndexRebindFacadeContract())
        return 34;
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
	if (!TestAnimationLayerRootReferenceFrameContract())
		return 139;
	if (!TestAnimationGraphSetSwitchRuntimeContract())
		return 132;
	if (!TestAnimationSlotRuntimeContract())
		return 51;
	if (!TestAnimationHotReloadStateTransferContract())
		return 56;
	if (!TestAnimationMarkerSyncLayerContract())
		return 52;
	if (!TestAnimationTargetPostProcessContract())
		return 53;
	if (!TestAnimationSocketAttachmentAuthoringContract())
		return 154;
	if (!RunProceduralAnimationContractTests())
		return 129;
	if (!TestAnimationSyncedGraphStateContract())
		return 54;
	if (!TestRenderSystemLifecycleContract() ||
		!TestRenderSystemStartupFailureContract() ||
		!TestRenderSystemDestructorFallbackContract() ||
		!TestRenderSystemPrepareFailureContract() ||
		!TestRenderSystemOneFrameLeadContract() ||
		!TestRenderOutcomeLedgerContract() ||
		!TestRenderFramePacketContract() ||
		!TestMainCameraVisibilityBackendOwnershipContract() ||
		!TestPunctualShadowBackendOwnershipContract() ||
		!TestRenderWorldContract() ||
		!TestFramePhaseThreadLocalContract())
		return 139;
    if (!TestMediaDecodeSessionContract())
        return 139;
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
    if (!TestAudioPreviewSettingsContract())
        return 10;
    if (!TestAudioVoiceVirtualizationContract())
        return 11;
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
	if (!TestPunctualShadowResolutionAndPendingLifecycle())
		return 76;
	if (!TestGIProbeUpdateScheduleContract())
		return 75;
    if (!TestReflectionProbeSpatialIndexContract())
        return 182;
    if (!TestTriangleGeometryQueryContract())
        return 184;
    if (!TestReflectionProbePlacementContract())
        return 186;
    if (!TestLightCookieContract()) return 1;
    if (!RunCursorContractTests()) return 1;
    std::cout << "Forest contract tests passed\n";
    return 0;
}
