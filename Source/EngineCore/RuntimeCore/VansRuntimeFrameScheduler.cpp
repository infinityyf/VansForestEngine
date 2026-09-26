#include "VansRuntimeFrameScheduler.h"

#include "../EventCore/VansEventBus.h"
#include "VansFramePhase.h"
#include "VansThreadContract.h"

namespace Vans
{
void VansRuntimeFrameScheduler::RunGameplay(
	IVansRuntimeFramePort& runtimePort,
	IVansRuntimeFramePreviewPort* previewPort,
	const VansRuntimeFramePolicy& policy,
	const VansRuntimeFrameContext& context)
{
	VANS_ASSERT_MAIN_THREAD();
	VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	VansEventBus::Get().BeginFrame();
	if (!policy.m_IsSceneReady)
		return;

	if (policy.m_IsCameraControlActive)
		runtimePort.BeginCameraControlFrame(context);

	if (policy.m_IsSimulationRunning)
	{
		VansEventBus::Get().Flush(VansEventLane::Physics);
		runtimePort.SyncPhysicsTransforms(context);
	}

	if (policy.m_IsGameplayActive)
	{
		VansEventBus::Get().Flush(VansEventLane::Script);
		runtimePort.UpdateNonCameraScripts(context);

		VansEventBus::Get().Flush(VansEventLane::GameLogic);
	}
	if (policy.m_IsCameraControlActive)
		runtimePort.AdvanceCameraRuntime(context);
	if (policy.m_IsGameplayActive)
	{
		runtimePort.UpdateActionsEarly(context);
		runtimePort.UpdateAI(context);
		runtimePort.PrepareCharacterLocomotion(context);
	}

	if (policy.m_IsSimulationRunning)
		runtimePort.FlushCharacterControllerTransforms(context);

	if (policy.m_IsGameplayActive)
		runtimePort.UpdateTimelinesPostScript(context);
	if (policy.m_IsCameraControlActive && previewPort)
		previewPort->UpdatePostScriptControllers(context);

	if (policy.m_IsGameplayActive || policy.m_IsCameraControlActive)
	{
		VansEventBus::Get().Flush(VansEventLane::GameLogic);
		VansEventBus::Get().Flush(VansEventLane::Script);
		VansEventBus::Get().Flush(VansEventLane::MainThread);
		runtimePort.RunActionLateContinuation(context);
	}

	if (policy.m_IsGameplayActive)
		runtimePort.UpdateCameraScripts(context);
	if (policy.m_IsCameraControlActive)
		runtimePort.CaptureCameraControlBase(context);

	if (policy.m_IsGameplayActive)
		runtimePort.UpdateTimelinesCamera(context);
	if (policy.m_IsCameraControlActive)
	{
		if (previewPort)
			previewPort->UpdateCameraControllers(context);
		runtimePort.ResolveCameraControlFrame(context);
		VansEventBus::Get().Flush(VansEventLane::RenderPrep);
		VansEventBus::Get().Flush(VansEventLane::Diagnostics);
	}
}
} // namespace Vans
