#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AssetCore/VansAssetResolver.h"
#include "../EngineCore/AssetCore/VansMaterialAuthoringAsset.h"
#include "../EngineCore/AssetCore/VansSkinProfile.h"
#include "../EngineCore/AssetCore/VansSkeletalMeshImportSettings.h"
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
#include "../EngineCore/AudioCore/VansAudioPreviewPlayer.h"
#include "../EngineCore/AudioCore/VansAudioVirtualization.h"
#include "../EngineCore/AudioCore/VansAudioWaveform.h"
#include "../EngineCore/CameraCore/VansCameraCore.h"
#include "../EngineCore/RenderCore/VansPostProcessProfile.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VansDrawSubmission.h"
#include "../EngineCore/RenderCore/VansCameraControlArbiter.h"
#include "../EngineCore/RenderCore/Timeline/VansVirtualCameraParameterStore.h"
#include "../EngineCore/RenderCore/GICore/VansGISettings.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVideoThumbnail.h"
#include "../EngineCore/RenderCore/VulkanCore/VansFrameSubmitOrchestrator.h"
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
#include "../EngineCore/RenderCore/VulkanCore/VansMainCameraVisibilityState.h"
#include "../EngineCore/RenderCore/VansRenderFrame.h"
#include "../EngineCore/RenderCore/VansRenderSystem.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneResourceArtifactPrewarmer.h"
#include "../EngineCore/RenderCore/VansTemporalProjection.h"
#include "../EngineCore/RenderCore/ShadowCore/VansPunctualShadowManager.h"
#include "../EngineCore/RenderCore/ShadowCore/VansPunctualShadowFrameState.h"
#include "../EngineCore/RenderCore/BRDFData/VansLight.h"
#include "../EngineCore/RuntimeCore/VansPackageManifest.h"
#include "../EngineCore/RuntimeCore/VansCharacterMotion.h"
#include "../EngineCore/RuntimeCore/VansCharacterTrajectoryGenerator.h"
#include "../EngineCore/RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../EngineCore/RuntimeCore/VansFramePhase.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/Transform/VansTransformGraph.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include "../EngineCore/SceneCore/VansSceneCameraMediaComponentReader.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../EngineCore/SceneCore/VansSceneEntityFactory.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeComponentKey.h"
#include "../EngineCore/SceneCore/VansSceneRenderSettingsConfigReader.h"
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
#include "../EngineCore/EditorCore/Timeline/VansTimelineEditService.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineCommandMap.h"
#include "../EngineCore/EditorCore/VansAssetDocumentRegistry.h"
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
#include "../EngineCore/AnimationCore/VansAnimationSampler.h"
#include "../EngineCore/AnimationCore/VansPoseMath.h"
#include "../EngineCore/AnimationCore/VansPosePayloadMixer.h"
#include "../EngineCore/AnimationCore/VansAnimationLayer.h"
#include "../EngineCore/AnimationCore/VansSkinnedMeshLoader.h"
#include "../EngineCore/AnimationCore/Retargeting/VansRetargetProcessor.h"
#include "../EngineCore/AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansRetargetProfileStorage.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../EngineCore/EngineAPILayer/Private/AnimationAuthoringBridge.h"
#include "../EngineCore/ParticleCore/VansParticleRuntime.h"
#include "TimelineRefactorContractTests.h"
#include "GAFContractTests.h"
#include "ProceduralAnimationContractTests.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/ProjectSystem/VansProjectSettingsData.h"
#include "../EngineCore/ProjectSystem/Serialization/VansProjectSettingsJsonCodec.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/ScriptCore/VansLuaScriptInspectorService.h"
#include "../EngineCore/ScriptCore/VansTransform.h"

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
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
class TestRenderSystemDevice final : public VansGraphics::VansGraphicsDevice
{
public:
	TestRenderSystemDevice()
	{
		m_RenderWidth = 1280;
		m_RenderHeight = 720;
	}

	void BeforeRendering() override
	{
		beforeThread = std::this_thread::get_id();
		++beforeCount;
	}
	void PrepareRenderingFrame() override
	{
		prepareThread = std::this_thread::get_id();
		prepareFrontendOrder = NextOrder();
	}
	VansGraphics::VansRenderSubmissionPrepareResult PrepareRenderSubmission(
		const VansGraphics::VansRenderFrameSubmission& submission) override
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
	void EndGpuProfilerFrame() override { ++profilerEndCount; }
	void* GetNativeGraphicsDevice() override { return nullptr; }
	void* GetNativeCommandBuffer() override { return nullptr; }

	std::uint32_t beforeCount = 0;
	std::uint32_t renderCount = 0;
	std::uint32_t presentCount = 0;
	std::uint32_t afterCount = 0;
	std::uint32_t waitCount = 0;
	std::uint32_t resizeCount = 0;
	std::uint32_t profilerInitializeCount = 0;
	std::uint32_t profilerEndCount = 0;
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
	TestRenderSystemDevice device;
	TestRenderFrameSource frameSource;
	int frameSequence = 0;
	device.sequence = &frameSequence;
	frameSource.sequence = &frameSequence;
	VansGraphics::VansRenderSystem renderSystem(device, frameSource);
	const std::thread::id mainThread = std::this_thread::get_id();
	if (!renderSystem.InitializeFrameExecution() ||
		renderSystem.GetState() != VansGraphics::VansRenderSystemState::Running ||
		device.beforeCount != 1)
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
		transactionThread == mainThread)
	{
		return false;
	}

	renderSystem.InitializeGpuProfiler();
	renderSystem.EndGpuProfilerFrame();
	if (device.profilerInitializeCount != 1 || device.profilerEndCount != 1)
		return false;

	if (!renderSystem.Quiesce() ||
		renderSystem.GetState() != VansGraphics::VansRenderSystemState::Quiesced ||
		device.waitCount != 1 || device.waitThread != device.beforeThread)
	{
		return false;
	}

	renderSystem.ShutdownFrameExecution();
	if (renderSystem.GetState() != VansGraphics::VansRenderSystemState::Stopped ||
		device.afterCount != 1 || device.afterThread != device.beforeThread)
	{
		return false;
	}

	return renderSystem.WaitForIdle() && device.waitCount == 1;
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
	VansRenderSystem renderSystem(device, frameSource, 1);
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
	return packet.has_value() &&
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
		first.punctualShadowJobs.front().casterHandles.front() != VansRenderProxyHandle{ 3u, 2u })
	{
		return false;
	}
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
	unloaded.sceneEpoch = 10;
	if (!backendState.PrepareFrame(unloaded, 3))
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
	std::thread worker([&workerObservedOwnPhase]
	{
		VANS_INIT_RENDER_THREAD();
		g_CurrentFramePhase = VansFramePhase::GPURecord;
		workerObservedOwnPhase =
			g_CurrentThreadRole == VansThreadRole::Render &&
			g_CurrentFramePhase == VansFramePhase::GPURecord;
	});
	worker.join();
	return workerObservedOwnPhase &&
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
		{ "skinProfile", Value::String(profileGuid.ToString()) },
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
	profileRecord.sourcePath = profilePath;
	profileRecord.authoringPath = profilePath;

	Vans::VansAssetRecord materialRecord;
	materialRecord.guid = materialGuid;
	materialRecord.type = Vans::VansAssetType::Material;
	materialRecord.state = Vans::VansAssetState::CpuReady;
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

	const Value sceneRoot = Value::Object({
		{ "schemaVersion", Value::Int(Vans::VansSceneSchemaVersion) },
		{ "settings", Value::Object({}) },
		{ "entities", Value::Array({}) }
	});

	Vans::VansSceneContentBuildPlan plan;
	if (!Expect(Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
		sceneRoot,
		temporary.path.string(),
		plan,
		error), error.c_str()))
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
	return ExpectSerializedVec3Field(runtimeMaterial, "profileScatterRadius", glm::vec3(1.31f, 1.08f, 0.82f),
		"Skin profile RGB scatter radius was not projected into runtime material");
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

bool TestAssetPolicies()
{
    TemporaryDirectory temporary;
    const fs::path assetsRoot = temporary.path / "Assets";
    const fs::path artifactRoot = temporary.path / "Library" / "Artifacts";
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
    if (!Expect(Vans::VansAssetDatabase::Classify("Probe.vtimeline") == Vans::VansAssetType::Timeline,
        "Timeline asset extension is not classified canonically"))
        return false;
	if (!Expect(Vans::VansAssetDatabase::Classify("Hero.skinprofile") == Vans::VansAssetType::SkinProfile,
		"Skin profile asset extension is not classified canonically"))
		return false;
	if (!Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::SkinProfile) == "SkinProfileImporter",
		"Skin profile assets are not owned by the canonical SkinProfile importer"))
		return false;
    return Expect(Vans::VansAssetDatabase::ImporterFor(Vans::VansAssetType::Timeline) == "TimelineImporter",
        "Timeline assets are not owned by the canonical Timeline importer");
}

bool TestAssetTypeSerializationContract()
{
    constexpr Vans::VansAssetType types[] = {
        Vans::VansAssetType::Model,
        Vans::VansAssetType::Texture,
        Vans::VansAssetType::Material,
        Vans::VansAssetType::Shader,
        Vans::VansAssetType::Audio,
        Vans::VansAssetType::Video,
        Vans::VansAssetType::Scene,
        Vans::VansAssetType::Particle,
        Vans::VansAssetType::AnimationClip,
        Vans::VansAssetType::AnimatorController,
        Vans::VansAssetType::AnimationRig,
        Vans::VansAssetType::BoneMask,
        Vans::VansAssetType::Timeline,
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
        Vans::VansAssetType::AudioDuckingRules
    };

    for (const Vans::VansAssetType type : types)
    {
        const std::string_view name = Vans::VansAssetDatabase::SerializedTypeName(type);
        if (!Expect(name != "unknown", "A registered asset type has no serialized name"))
            return false;
        if (!Expect(Vans::VansAssetDatabase::ParseSerializedType(name) == type,
            "An asset type did not round-trip through its serialized name"))
            return false;
    }

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

bool TestGameplayFrameOrder()
{
    std::vector<std::string> trace;
    Vans::VansRuntimeGameplayFrame frame;
    frame.sceneReady = true;
    frame.simulationRunning = true;
    frame.gameplayActive = true;
	frame.cameraControlActive = true;
    frame.syncPhysicsTransforms = [&] { trace.push_back("physics"); };
	frame.updateNonCameraScripts = [&] { trace.push_back("scripts"); };
	frame.updateActionsEarly = [&](double) { trace.push_back("actions-early"); };
	frame.prepareCharacterLocomotion = [&](double) { trace.push_back("locomotion"); };
	frame.flushCharacterControllerTransforms = [&] { trace.push_back("cct"); };
	frame.updateTimelinesPostScript = [&](double) { trace.push_back("timeline-post"); };
	frame.updateAdditionalPostScriptControllers = [&](double) { trace.push_back("post-extra"); };
	frame.runTimelineLateContinuation = [&] { trace.push_back("timeline-late"); };
	frame.runActionLateContinuation = [&] { trace.push_back("actions-late"); };
	frame.beginCameraControlFrame = [&] { trace.push_back("camera-begin"); };
	frame.updateCameraScripts = [&] { trace.push_back("camera"); };
	frame.captureCameraControlBase = [&] { trace.push_back("camera-base"); };
	frame.updateTimelinesCamera = [&](double) { trace.push_back("timeline-camera"); };
	frame.updateAdditionalCameraControllers = [&](double) { trace.push_back("camera-extra"); };
	frame.resolveCameraControlFrame = [&] { trace.push_back("camera-resolve"); };
	Vans::VansRuntimeFrameScheduler::RunGameplay(frame);

	const std::vector<std::string> expected{ "camera-begin", "physics", "scripts", "actions-early", "locomotion", "cct",
		"timeline-post", "post-extra", "timeline-late", "actions-late", "camera", "camera-base", "timeline-camera",
		"camera-extra", "camera-resolve" };
    if (!Expect(trace == expected, "Gameplay frame callback order changed"))
        return false;

    trace.clear();
    frame.sceneReady = false;
    Vans::VansRuntimeFrameScheduler::RunGameplay(frame);
    return Expect(trace.empty(), "Gameplay callbacks ran without a ready scene");
}

bool TestCameraControlArbiterContract()
{
	using namespace VansGraphics;
	VansCameraControlArbiter arbiter;
	VansCameraControlPose base;
	base.position = { 10.0f, 0.0f, 0.0f };
	base.rotationDegrees = { 0.0f, 90.0f, 0.0f };
	base.fieldOfView = 60.0f;
	arbiter.CaptureBase(base);
	const auto timelineDomain = VansCameraControlArbiter::TimelineDomain();
	const auto gameplayDomain = Vans::VansMakeStableId<VansCameraControlDomainTag>(
		"CameraControl.ContractGameplay");
	const Vans::VansGenerationHandle sameHandle{ 7, 3 };
	VansCameraControlPose gameplayPose = base;
	gameplayPose.fieldOfView = 50.0f;
	if (!arbiter.Submit({ { gameplayDomain, sameHandle }, gameplayPose,
		VansCameraControlMode::Exclusive, VansCameraControlSpace::World,
		10, 0, 1.0f, 0x04u })) return false;
	if (!Expect(!arbiter.IsUserLookSuppressed(),
		"camera contributions changed gameplay look behavior without opting in")) return false;
	VansCameraControlPose timelinePose = base;
	timelinePose.fieldOfView = 40.0f;
	if (!arbiter.Submit({ { timelineDomain, sameHandle }, timelinePose,
		VansCameraControlMode::Weighted, VansCameraControlSpace::World,
		1000, 0, 0.5f, 0x04u, true })) return false;
	if (!Expect(arbiter.IsUserLookSuppressed(),
		"Timeline camera contribution did not suppress gameplay look input")) return false;
	VansCameraControlPose shake;
	shake.position = { 0.0f, 0.0f, 1.0f };
	if (!arbiter.Submit({ { timelineDomain, { 8, 3 } }, shake,
		VansCameraControlMode::Additive, VansCameraControlSpace::CameraLocal,
		1100, 0, 1.0f, 0x01u })) return false;
	if (!Expect(!arbiter.Submit({ { gameplayDomain, { 9, 3 } }, gameplayPose,
		VansCameraControlMode::Exclusive, VansCameraControlSpace::World,
		VansCameraControlArbiter::TimelinePriority, 0, 1.0f, 0x04u }),
		"non-Timeline controller entered the reserved Timeline priority range")) return false;
	const VansCameraControlPose resolved = arbiter.ResolvePose();
	if (!Expect(arbiter.ContributionCount() == 3,
		"camera control owner domains collided on equal generation handles")) return false;
	if (!Expect(std::abs(resolved.fieldOfView - 45.0f) < 0.001f &&
		glm::length(resolved.position - glm::vec3(11.0f, 0.0f, 0.0f)) < 0.001f,
		"camera priority, weighting, or camera-local additive resolution is wrong")) return false;
	VansCameraControlPose lastResolved;
	if (!Expect(arbiter.GetLastResolvedPose(lastResolved) &&
		glm::length(lastResolved.position - resolved.position) < 0.001f &&
		std::abs(lastResolved.rotationDegrees.y - resolved.rotationDegrees.y) < 0.001f,
		"camera arbiter did not retain the authoritative resolved gameplay view")) return false;
	arbiter.ReleaseDomain(timelineDomain);
	if (!Expect(!arbiter.IsUserLookSuppressed(),
		"Timeline camera domain release did not restore gameplay look input")) return false;
	const VansCameraControlPose gameplayOnly = arbiter.ResolvePose();
	if (!Expect(arbiter.ContributionCount() == 1 &&
		std::abs(gameplayOnly.fieldOfView - 50.0f) < 0.001f,
		"camera control domain release removed another controller domain")) return false;

	VansVirtualCameraParameterStore virtualCameras;
	const Vans::VansEntityHandle virtualCamera{ 12, 4 };
	if (!Expect(virtualCameras.Set(virtualCamera, { 72.0f, 0.03f, 2000.0f }),
		"virtual camera parameter store rejected a valid Transform entity")) return false;
	const VansVirtualCameraParameters* parameters = virtualCameras.Find(virtualCamera);
	if (!Expect(parameters && parameters->fieldOfView == 72.0f &&
		parameters->nearClip == 0.03f && parameters->farClip == 2000.0f,
		"virtual camera parameters were not recorded without a Camera component")) return false;
	if (!Expect(virtualCameras.Remove(virtualCamera) && virtualCameras.Size() == 0,
		"virtual camera parameter lifetime did not restore cleanly")) return false;

	Vans::VansCameraRuntime cameraRuntime;
	Vans::VansCameraViewSnapshot coreBase;
	coreBase.pose.position = { 1.0f, 2.0f, 3.0f };
	coreBase.pose.rotationDegrees = { 0.0f, 90.0f, 0.0f };
	coreBase.lens.fieldOfView = 60.0f;
	std::string error;
	if (!cameraRuntime.SetBaseView(Vans::VansCameraRuntime::MainView(), coreBase, error))
		return Expect(false, error.c_str());
	const auto actionDomain =
		Vans::VansMakeStableId<Vans::VansCameraContributionDomainIdTag>("Camera.GAF.Contract");
	Vans::VansCameraContribution firstContribution;
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
	Vans::VansCameraContribution secondContribution = firstContribution;
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
	solvedRig.follow.mode = "SpringArm";
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
	return Expect(cameraRuntime.UnregisterRig(solvedRigHandle) &&
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

    const Vans::VansSerializedValue sceneRoot = Vans::VansSerializedValue::Object({
        { "schemaVersion", Vans::VansSerializedValue::Int(Vans::VansSceneSchemaVersion) },
        { "settings", Vans::VansSerializedValue::Object({}) },
        { "entities", Vans::VansSerializedValue::Array({ entity }) }
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
    if (!Expect(plan.objects.objects.size() == 1 && plan.objects.objects.front().transform.has_value(),
        "Transform-only empty object did not project into the runtime scene plan"))
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

bool TestTransformGraphAnchorContract()
{
	struct TransformLease
	{
		std::vector<std::uint32_t> ids;
		~TransformLease()
		{
			for (auto it = ids.rbegin(); it != ids.rend(); ++it)
				VansGraphics::VansTransformStore::FreeTransform(*it);
		}
		std::uint32_t Allocate(const glm::vec3& position)
		{
			const std::uint32_t id = VansGraphics::VansTransformStore::AllocateTransform();
			ids.push_back(id);
			auto& transform = VansGraphics::VansTransformStore::GetTransform(id);
			transform.m_Position = position;
			transform.m_Rotation = glm::vec3(0.0f);
			transform.m_Scale = glm::vec3(1.0f);
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
	Vans::VansTransformGraph graph(&provider);
	if (!Expect(graph.SetParent(child, owner, Vans::VansTransformReparentMode::KeepWorld)
		&& graph.Resolve(),
		"Transform graph could not create an entity parent link"))
	{
		return false;
	}
	if (!ExpectNear(VansGraphics::VansTransformStore::GetTransform(child).m_Position.x,
		12.0f, 0.0001f, "KeepWorld entity reparent changed child world position"))
	{
		return false;
	}
	VansGraphics::VansTransformStore::GetTransform(owner).m_Position.x = 20.0f;
	if (!Expect(graph.Resolve(), "Transform graph failed after its entity parent moved")
		|| !ExpectNear(VansGraphics::VansTransformStore::GetTransform(child).m_Position.x,
			22.0f, 0.0001f, "Entity child did not follow its parent transform"))
	{
		return false;
	}
	if (!Expect(!graph.SetParent(owner, child, Vans::VansTransformReparentMode::KeepLocal),
		"Transform graph accepted an entity hierarchy cycle"))
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
		VansGraphics::VansTransformStore::GetTransform(attachment).m_Position;
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
		VansGraphics::VansTransformStore::GetTransform(attachment).m_Position;
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
		VansGraphics::VansTransformStore::GetTransform(snappedAttachment).m_Position;
	if (!ExpectNear(snapped.x, 25.0f, 0.0001f,
		"Snap did not reset the attachment local X")
		|| !ExpectNear(snapped.y, 8.0f, 0.0001f,
			"Snap did not reset the attachment local Y"))
	{
		return false;
	}
	return Expect(graph.ClearParent(snappedAttachment,
		Vans::VansTransformReparentMode::Snap),
		"Transform graph could not clear a snapped parent")
		&& ExpectNear(VansGraphics::VansTransformStore::GetTransform(
			snappedAttachment).m_Position.x, 0.0f, 0.0001f,
			"Snap detach did not reset world X")
		&& ExpectNear(VansGraphics::VansTransformStore::GetTransform(
			snappedAttachment).m_Position.y, 0.0f, 0.0001f,
			"Snap detach did not reset world Y");
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
		7,
		3,
		true);
	world.FlushCommands();
	Vans::VansComponentHandle animationComponent = world.FindComponentByGuid(
		"queued-animation-guid",
		Vans::VansRuntimeComponentType_Animation);
	auto* animationStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Animation));
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
	return true;
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

    for (const int turnSign : { 1, -1 })
    {
        VansMotionMatchingRuntime runtime;
        runtime.Configure(settings);
        VansPosePayload payload;
        Vans::VansCharacterTrajectory trajectory = buildTrajectory(37.0f);
        if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
            return Expect(false, "Motion matching rejected the camera-facing idle fixture");

        // A 60-degree request must choose the 90-degree authored arc and enter
        // it at the matching Pose Search sample. Choosing the nearest 45-degree
        // clip would leave an unresolved 15-degree error at the clip end.
        trajectory.desiredFacingYaw = 37.0f + static_cast<float>(turnSign) * 60.0f;
        for (auto& future : trajectory.future)
            future.facingYaw = trajectory.desiredFacingYaw;

        bool selectedExpectedTurn = false;
        bool emittedTurnRootRotation = false;
        for (int frame = 0; frame < 20; ++frame)
        {
            if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
                return Expect(false, "Motion matching stopped during a camera-facing turn");
            const MotionMatchingDebugData& debug = runtime.GetDebugData();
            const std::string expectedToken = turnSign > 0 ? "Turn_L_090" : "Turn_R_090";
            selectedExpectedTurn = selectedExpectedTurn ||
                debug.activeClip.find(expectedToken) != std::string::npos;
            emittedTurnRootRotation = emittedTurnRootRotation ||
                (payload.rootMotion.valid &&
                 std::abs(glm::degrees(glm::eulerAngles(payload.rootMotion.rotation)).z) > 0.01f);
            if (!Expect(debug.facingTurnRequested &&
                        debug.facingTurnDirectionSign == turnSign &&
                        debug.facingTurnBucketDelta == 2,
                "Camera facing error did not produce the expected turn request"))
                return false;
        }
        if (!Expect(selectedExpectedTurn,
            "Camera facing error did not select the matching left/right turn clip"))
            return false;
        if (!Expect(emittedTurnRootRotation,
            "Selected camera-facing turn did not emit Root Motion rotation"))
            return false;

        // A non-loop Turn with unresolved facing must replan directly into a
        // covering Turn arc. An intermediate Idle frame exposes the character's
        // front while the camera keeps moving and is not part of the locomotion
        // contract. Replaying the same authored arc is valid only after the
        // previous one-shot has completed.
        bool observedDirectTurnReplan = false;
        std::string previousClip = runtime.GetDebugData().activeClip;
        float previousTime = runtime.GetDebugData().activeTime;
        int frozenTurnFrames = 0;
        for (int frame = 0; frame < 70; ++frame)
        {
            if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
                return Expect(false, "Motion matching stopped while completing an idle turn");
            const MotionMatchingDebugData& debug = runtime.GetDebugData();
            const bool activeTurn = debug.activeClip.find("Turn") != std::string::npos;
            if (activeTurn && debug.activeClip == previousClip &&
                std::abs(debug.activeTime - previousTime) <= 0.00001f)
                ++frozenTurnFrames;
            else
                frozenTurnFrames = 0;
            if (!Expect(frozenTurnFrames < 2,
                "Turn-in-place remained frozen on a non-loop clip frame"))
                return false;
            if (activeTurn && debug.activeClip == previousClip &&
                debug.activeTime + 0.01f < previousTime)
                observedDirectTurnReplan = true;
            if (activeTurn && payload.rootMotion.valid &&
                !ExpectNear(debug.appliedRootYawDeltaDegrees,
                    debug.authoredRootYawDeltaDegrees, 0.001f,
                    "Turn Root Motion reconciliation changed the authored yaw integral"))
                return false;
            previousClip = debug.activeClip;
            previousTime = debug.activeTime;
        }
        if (!Expect(observedDirectTurnReplan,
            "Persistent facing intent did not directly replan a completed Turn"))
            return false;

        // Idle camera rate may keep the currently playing Turn coherent, but it
        // must not authorize replay once the actual facing error is resolved.
        trajectory.desiredFacingYaw = trajectory.currentFacingYaw +
            static_cast<float>(turnSign) * 2.0f;
        trajectory.desiredFacingYawRate = static_cast<float>(turnSign) * 30.0f;
        for (auto& future : trajectory.future)
            future.facingYaw = trajectory.desiredFacingYaw;
        bool resolvedIdleTurn = false;
        for (int frame = 0; frame < 40; ++frame)
        {
            if (!runtime.Update(0.033f, skeleton, clips, parameters, &trajectory, payload))
                return Expect(false, "Motion matching stopped while resolving turn-in-place");
            if (!runtime.GetDebugData().facingTurnRequested)
            {
                resolvedIdleTurn = true;
                break;
            }
        }
        if (!Expect(resolvedIdleTurn,
            "Idle camera angular velocity replayed a resolved non-loop turn"))
            return false;
    }

    // 移动中转相机不得把 one-shot Turn 当成连续转向控制器。未来轨迹继续
    // 参与 Pose Search，剩余朝向误差由 Root Motion Steering 平滑施加。
    VansMotionMatchingRuntime movingTurnRuntime;
    movingTurnRuntime.Configure(settings);
    VansPosePayload movingTurnPayload;
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
    for (int frame = 0; frame < 16; ++frame)
        if (!movingTurnRuntime.Update(
            0.033f, skeleton, clips, parameters, &movingTurnTrajectory, movingTurnPayload))
            return Expect(false, "Motion matching stopped before the moving-turn fixture stabilized");

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
            0.033f, skeleton, clips, parameters, &movingTurnTrajectory, movingTurnPayload))
            return Expect(false, "Motion matching stopped during continuous moving-camera turn");
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
		glm::radians(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	const RootMotionReconciliationResult first = reconciler.Apply(
		dt, translation, rotation);
	if (!Expect(first.active &&
		std::abs(first.appliedVelocityAnimation.y - 4.0f) < 0.001f &&
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
            const bool animationAsset = type == Vans::VansAssetType::AnimationClip
                || type == Vans::VansAssetType::AnimatorController
				|| type == Vans::VansAssetType::AnimationRig
                || type == Vans::VansAssetType::BoneMask;
            const bool timelineAsset = type == Vans::VansAssetType::Timeline
                || type == Vans::VansAssetType::Audio
                || type == Vans::VansAssetType::Video
                || type == Vans::VansAssetType::Particle;
            const bool retargetSourceModel = type == Vans::VansAssetType::Model
                && iterator->path().filename() == "SKM_UEFN_Mannequin.fbx";
			const bool demoHallBackAxeAsset = projectName == "DemoHallProject"
				&& iterator->path().generic_string().find("/Assets/Imported/Fire_Axe/")
					!= std::string::npos
				&& (type == Vans::VansAssetType::Model
					|| type == Vans::VansAssetType::Material
					|| type == Vans::VansAssetType::Texture);
            if (!animationAsset && !timelineAsset && !retargetSourceModel && !demoHallBackAxeAsset)
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
                validation.runtimeValidation = false;
				validation.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
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
            const Vans::VansSceneAssetDependencyBuildResult dependencies =
                Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(database, iterator->path(), {});
            if (!Expect(dependencies.success,
                "Project scene animation or Timeline dependency closure is incomplete or contains path/cross-project references"))
                return false;
			if (projectName == "AnimationV2Project" && !Expect(
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

bool TestAnimationV2RetargetMotionMatchingSceneContract()
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
				&& animation.contains("motion_matching")
				&& animation["motion_matching"].contains("motion_model")
				&& animation["motion_matching"].contains("root_motion_steering")
				&& animation["motion_matching"].contains("root_motion_reconciliation")
				&& animation["motion_matching"].contains("search_groups")
				&& animation["motion_matching"].contains("contacts")
				&& !animation.contains("foot_placement"),
				"AnimationV2 retargeted character is missing Motion Matching configuration blocks"))
			{
				return false;
			}

			const nlohmann::json& retarget = animation.at("retarget");
			const nlohmann::json& motionMatching = animation.at("motion_matching");
			const nlohmann::json& motionModel = motionMatching.at("motion_model");
			const nlohmann::json& contacts = motionMatching.at("contacts");
			std::unordered_set<std::string> searchGroupNames;
			for (const nlohmann::json& group : motionMatching.at("search_groups"))
				searchGroupNames.insert(group.value("name", ""));
			if (!Expect(animation.value("root_motion", false)
				&& retarget.value("enabled", false)
				&& !retarget.contains("runtime_mode")
				&& !retarget.contains("cache_policy")
				&& motionMatching.value("enabled", false)
				&& contacts.value("provider", "") == "locomotion"
				&& contacts.value("channels", nlohmann::json::array()).size() == 2
				&& motionModel.value("drive_mode", "") == "root_motion"
				&& motionMatching.value("non_loop_sampling_end_margin", 0.0f) > 0.0f
				&& motionMatching.at("root_motion_steering").value("enabled", false)
				&& motionMatching.at("root_motion_reconciliation").value("enabled", false)
				&& motionMatching.at("search_groups").size() >= 15
				&& searchGroupNames.count("StandWalkPivot") > 0
				&& searchGroupNames.count("StandRunPivot") > 0
				&& searchGroupNames.count("CrouchPivot") > 0,
				"AnimationV2 retargeted character is missing the Root Motion Motion Matching contract"))
			{
				return false;
			}
			validatedCharacters.insert(name);
		}
	}
	if (!Expect(validatedCharacters.size() == 3,
		"AnimationV2 must configure TwinBlast, SWAT, and Survival for retargeted Motion Matching"))
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
	const auto socket = std::find_if(survivalRig.sockets.begin(), survivalRig.sockets.end(),
		[](const VansRigSocketDefinition& value) { return value.name == "Back_Axe"; });
	if (!Expect(socket != survivalRig.sockets.end()
		&& socket->guid == kBackAxeSocketGuid
		&& socket->boneGuid == "0aab6b03-caaa-5326-8521-c0f4f380e3bd"
		&& glm::length(socket->positionLocal
			- glm::vec3(-99.915901f, 19.805099f, 49.349899f)) <= 1.0e-4f
		&& glm::length(socket->scaleLocal - glm::vec3(1.0f)) <= 1.0e-5f,
		"DemoHall Survival Back_Axe Socket must resolve to spine_04 with the validated back offset"))
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
		&& Expect(scriptText.find("attachments = { \"Survival_Back_Axe_metal-low\" }")
			!= std::string::npos
			&& scriptText.find("set_character_attachments_enabled(option, enabled)")
				!= std::string::npos,
			"DemoHall character switching must toggle the Survival back axe renderer");
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
		VansAnimationController targetController;
		auto sourceController = std::make_unique<VansAnimationController>();
		if (!sourceController->SetAnimationRig(std::move(compiledSourceRig), {}, error)
			|| !targetController.SetAnimationRig(std::move(compiledTargetRig), {}, error))
		{
			return Expect(false, (std::string(fixture.label)
				+ " rejected its compiled Animation Rig: " + error).c_str());
		}
		targetNode.SetSkeleton(importedTargetSkeleton);
		if (!targetNode.SetController(&targetController)
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
			&& targetController.GetAnimationRig()->skeleton == &targetNodeSkeleton,
			(std::string(fixture.label)
				+ " controllers retained imported or temporary Skeleton pointers").c_str()))
			return false;

		VansRetargetProcessor processor;
		if (!Expect(processor.Build(
			sourceNodeSkeleton, targetNodeSkeleton,
			*targetController.GetAnimationRig(), desc),
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
			const glm::mat4 sourceTranslation = glm::translate(
				glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
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
				&& glm::length(footCenter(translatedTarget) - bindFootCenter) > 1.0f,
				(std::string(fixture.label)
					+ " feetToOwner incorrectly removed animated/root horizontal displacement").c_str()))
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

		if (!Expect(targetController.SubmitExternalModelPose(
			targetModelTransforms, targetNodeSkeleton, 1.0f / 60.0f,
			VansExternalPoseEvaluationMode::DirectFinalPose),
			(std::string(fixture.label) + " rejected the final target pose").c_str()))
			return false;
		float skinningDifference = 0.0f;
		const BoneMatricesSSBO& skinning = targetController.GetBoneMatricesSSBO();
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

		VansCompiledAnimationRig incompatibleRig = *targetController.GetAnimationRig();
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
	skeleton.sourceSkeletonGuid = "test-skeleton";
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
	VansAnimationRigAsset rig;
	rig.name = "RuntimeCompilerRig";
	rig.skeletonGuid = "88888888-8888-4888-8888-888888888888";
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
	VansAnimatorRuntimeCompileOptions runtimeOptions;
	runtimeOptions.rigResolver = [&](const std::string& guid, fs::path& path, std::string& resolveError)
	{
		if (guid != asset.animationRigGuid)
		{
			resolveError = "Unexpected Animation Rig GUID";
			return false;
		}
		path = rigPath;
		return true;
	};
    auto controller = VansAnimatorRuntimeCompiler::Compile(asset, skeleton,
        clipResolver, maskResolver, runtimeOptions, error);
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
        asset, skeleton, {}, maskResolver, runtimeOptions, error),
        "Full-graph runtime compilation accepted a missing Clip resolver"))
        return false;

    bool clipResolverInvoked = false;
    bool maskResolverInvoked = false;
    VansAnimatorRuntimeCompileOptions targetOptions;
    targetOptions.mode = VansAnimatorRuntimeCompileMode::ExternalPoseTarget;
	targetOptions.rigResolver = runtimeOptions.rigResolver;
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
	if (!Expect(targetController->GetClipNames().empty() && !targetController->HasGraphSets()
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
			VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
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
	return Expect(controller.SwitchGraphSet("missing")
		== VansGraphSetSwitchResult::UnknownGraphSet,
		"Graph Set runtime accepted an unknown stable ID");
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
        const int baseOutput = baseGraph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        baseGraph->AddLink(stateMachineId, 0, baseOutput, 0);

        auto slotGraph = std::make_unique<VansAnimGraph>();
        auto slotNode = std::make_unique<AnimGraphSlotNode>();
        slotNode->m_SlotId = "slot-upper";
        slotNode->m_EnableFallbackInput = false;
        const int slotNodeId = slotGraph->AddNode(std::move(slotNode));
        const int slotOutput = slotGraph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
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
        const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
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
            VansAnimGraph::CreateNodeByType(AnimGraphNodeType::TargetPoseInput));
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
            VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
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
        VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
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
            VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
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
	if (!Expect(first.fullUpdateCycleFrameCount == 128u, "GI scheduler changed the 128-frame complete update period"))
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

	const VkDevice fakeDevice = reinterpret_cast<VkDevice>(uintptr_t(1));
	const VkQueue fakeGraphicsQueue = reinterpret_cast<VkQueue>(uintptr_t(2));
	const VkQueue fakeComputeQueue = reinterpret_cast<VkQueue>(uintptr_t(3));
	const VkCommandBuffer fakeComputeCmd = reinterpret_cast<VkCommandBuffer>(uintptr_t(4));
	const VkCommandBuffer fakeGraphicsCmd = reinterpret_cast<VkCommandBuffer>(uintptr_t(5));
	const VkQueue fakeShadowQueue = reinterpret_cast<VkQueue>(uintptr_t(6));
	const VkCommandBuffer fakeSSAOCommandBuffer = reinterpret_cast<VkCommandBuffer>(uintptr_t(7));

	VansFrameSubmitOrchestrator graph;
	graph.Bind(fakeDevice, fakeGraphicsQueue, fakeComputeQueue, fakeShadowQueue);
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
	consumer.waitForCompletion = true;
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
	VansFrameSubmitNode ssaoRawProducer;
	ssaoRawProducer.name = "SSAORaw";
	ssaoRawProducer.queue = VansQueueRole::Graphics;
	ssaoRawProducer.commandBuffers = { fakeGraphicsCmd };
	ssaoRawProducer.signals = { VansSyncPoint::SSAORawReady };
	ssaoRawProducer.resources = {
		{ "SSAORaw", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, true, true, false, false }
	};
	graph.AddNode(std::move(ssaoRawProducer));

	VansFrameSubmitNode asyncSSAO;
	asyncSSAO.name = "AsyncSSAO";
	asyncSSAO.queue = VansQueueRole::Shadow;
	asyncSSAO.commandBuffers = { fakeSSAOCommandBuffer };
	asyncSSAO.waits = {
		{ VansSyncPoint::SSAORawReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
	};
	asyncSSAO.signals = { VansSyncPoint::SSAOReady };
	asyncSSAO.resources = {
		{ "SSAORaw", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, true, false, false, false },
		{ "SSAO", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, true, true, false, false }
	};
	graph.AddNode(std::move(asyncSSAO));

	VansFrameSubmitNode ssaoConsumer;
	ssaoConsumer.name = "DeferredSSAOConsumer";
	ssaoConsumer.queue = VansQueueRole::Graphics;
	ssaoConsumer.commandBuffers = { fakeGraphicsCmd };
	ssaoConsumer.waits = {
		{ VansSyncPoint::SSAOReady, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT }
	};
	ssaoConsumer.resources = {
		{ "SSAO", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, true, false, false, false }
	};
	ssaoConsumer.waitForCompletion = true;
	graph.AddNode(std::move(ssaoConsumer));
	if (!graph.Validate(&validationError))
		return false;
	const std::string ssaoDebugSummary = graph.BuildDebugSummary();
	if (ssaoDebugSummary.find("AsyncSSAO") == std::string::npos
		|| ssaoDebugSummary.find("SSAORawReady") == std::string::npos
		|| ssaoDebugSummary.find("SSAOReady") == std::string::npos
		|| ssaoDebugSummary.find("queue=Shadow") == std::string::npos)
	{
		return false;
	}

	graph.Reset();
	VansFrameSubmitNode shadowMaps;
	shadowMaps.name = "ShadowMaps";
	shadowMaps.queue = VansQueueRole::Shadow;
	shadowMaps.commandBuffers = { fakeSSAOCommandBuffer };
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
	shadowConsumer.waitForCompletion = true;
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
	orphanCompute.signals = { VansSyncPoint::CloudReady };
	graph.AddNode(std::move(orphanCompute));
	VansFrameSubmitNode uncoveredFinal;
	uncoveredFinal.name = "UncoveredFinal";
	uncoveredFinal.queue = VansQueueRole::Graphics;
	uncoveredFinal.commandBuffers = { fakeGraphicsCmd };
	uncoveredFinal.waitForCompletion = true;
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
	unsafeConsumer.waitForCompletion = true;
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
	if (!Expect(!history.IsResetPending(),
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

	capabilities.dlss.runtimeAvailable = true;
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
	const auto& skyPasses = shaderManager.GetMaterialPassMap(VAN_SKY_BOX);
	const auto& pbrPasses = shaderManager.GetMaterialPassMap(VAN_PBR);
	const auto velocity = skyPasses.find(VansPass::VELOCITY);
	const bool valid =
		sky != nullptr && sky->depthTest == VK_TRUE && sky->depthWrite == VK_FALSE &&
		sky->depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL &&
		skyMotion != nullptr && skyMotion->depthTest == VK_TRUE &&
		skyMotion->depthWrite == VK_FALSE &&
		skyMotion->depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL &&
		skyMotion->cullMode == VK_CULL_MODE_NONE &&
		velocity != skyPasses.end() && velocity->second == "SkyMotionVector" &&
		gbuffer != nullptr && gbuffer->colorAttachmentCount == 5 &&
		terrain != nullptr && terrain->colorAttachmentCount == 5 &&
		pbrPasses.find(VansPass::VELOCITY) == pbrPasses.end() &&
		shaderManager.FindShaderEntry("MotionVector") == nullptr &&
		shaderManager.FindShaderEntry("TerrainMotionVector") == nullptr;

	return Expect(valid,
		"GBuffer must own surface velocity while sky remains the only velocity-mapped overlay");
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

int main(int argc, char** argv)
{
	VANS_INIT_MAIN_THREAD();
	if (argc == 2 && std::string(argv[1]) == "--render-system")
		return TestRenderSystemLifecycleContract() &&
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
	if (argc == 2 && std::string(argv[1]) == "--asset-type-serialization")
		return TestAssetTypeSerializationContract() ? 0 : 136;
	if (argc == 2 && std::string(argv[1]) == "--animation-project-assets")
		return TestAnimationProjectAnimatorAssetsCanonicalContract() ? 0 : 137;
	if (argc == 2 && std::string(argv[1]) == "--demohall-survival-back-axe")
		return TestDemoHallSurvivalBackAxeSceneContract() ? 0 : 138;
	if (argc == 2 && std::string(argv[1]) == "--scene-entity-factory")
		return TestEmptySceneEntityFactoryContract() ? 0 : 134;
	if (argc == 2 && std::string(argv[1]) == "--transform-graph")
		return TestTransformGraphAnchorContract() ? 0 : 135;
	if (argc == 2 && std::string(argv[1]) == "--gaf-packaging")
		return TestGAFPackagingContract() ? 0 : 103;
	if (argc == 2 && std::string(argv[1]) == "--timeline-demohall")
		return TestTimelineDemoHallAssetContract() ? 0 : 84;
	if (argc == 2 && std::string(argv[1]) == "--gaf-demohall-window-break")
		return TestGAFDemoHallWindowBreakContract() ? 0 : 108;
	if (argc == 2 && std::string(argv[1]) == "--procedural-animation")
		return RunProceduralAnimationContractTests() ? 0 : 129;
	if (argc == 2 && std::string(argv[1]) == "--procedural-animation-integration")
		return TestAnimationTargetPostProcessContract()
			&& TestAnimationV2RetargetMotionMatchingSceneContract()
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
	if (argc == 2 && std::string(argv[1]) == "--animation-graph-sets-integration")
		return TestAnimatorCanonicalFormatContract()
			&& TestAnimationProjectAnimatorAssetsCanonicalContract()
			&& TestAnimationAuthoringBoundaryContract()
			&& TestAnimatorRuntimeCompilerContract()
			&& TestAnimationLayerStackRuntimeContract()
			&& TestAnimationGraphSetSwitchRuntimeContract()
			&& TestAnimationSlotRuntimeContract()
			&& TestAnimationHotReloadStateTransferContract()
			&& TestAnimationMarkerSyncLayerContract()
			&& TestAnimationTargetPostProcessContract()
			&& TestAnimationSyncedGraphStateContract() ? 0 : 133;
	if (argc == 2 && std::string(argv[1]) == "--animation-v2-scene")
		return TestAnimationV2RetargetMotionMatchingSceneContract() ? 0 : 115;
	if (argc == 2 && std::string(argv[1]) == "--point-shadow-atlas-policy")
		return TestPointShadowAtlasUpdatePolicy() ? 0 : 128;
	if (!TestPointShadowAtlasUpdatePolicy())
		return 128;
	if (!TestDrawSubmissionContract())
		return 127;
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
	if (!TestAsyncComputeSubmitGraphContract())
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
	if (!TestAnimationV2RetargetMotionMatchingSceneContract())
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
	if (!TestGAFPackagingContract())
		return 103;
	if (!TestGAFNetworkContract())
		return 104;
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
    if (!TestAssetPolicies())
        return 2;
	if (!TestAssetTypeSerializationContract())
		return 136;
    if (!TestGameplayFrameOrder())
        return 3;
	if (!TestTimelineRegistryContract())
		return 58;
	if (!TestTimelineSerializationContract())
		return 59;
	if (!TestTimelineEditorInteractionContract())
		return 76;
	if (!TestTimelineDemoHallAssetContract())
		return 84;
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
	if (!RunProceduralAnimationContractTests())
		return 129;
	if (!TestAnimationSyncedGraphStateContract())
		return 54;
	if (!TestRenderSystemLifecycleContract() ||
		!TestRenderSystemPrepareFailureContract() ||
		!TestRenderSystemOneFrameLeadContract() ||
		!TestRenderOutcomeLedgerContract() ||
		!TestRenderFramePacketContract() ||
		!TestMainCameraVisibilityBackendOwnershipContract() ||
		!TestPunctualShadowBackendOwnershipContract() ||
		!TestRenderWorldContract() ||
		!TestFramePhaseThreadLocalContract())
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
	if (!TestPunctualShadowResolutionAndPendingLifecycle())
		return 76;
	if (!TestGIProbeUpdateScheduleContract())
		return 75;
    std::cout << "Forest contract tests passed\n";
    return 0;
}
