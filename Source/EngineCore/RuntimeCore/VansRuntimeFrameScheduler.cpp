#include "VansRuntimeFrameScheduler.h"

#include "VansFramePhase.h"
#include "../EventCore/VansEventBus.h"

namespace Vans
{
void VansRuntimeFrameScheduler::RunGameplay(const VansRuntimeGameplayFrame& frame)
{
    VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	VansEventBus::Get().BeginFrame();
    if (!frame.sceneReady)
        return;
	if (frame.cameraControlActive && frame.beginCameraControlFrame)
		frame.beginCameraControlFrame();

    if (frame.simulationRunning)
    {
        VansEventBus::Get().Flush(VansEventLane::Physics);
        if (frame.syncPhysicsTransforms)
            frame.syncPhysicsTransforms();
    }

    if (frame.gameplayActive)
    {
        VansEventBus::Get().Flush(VansEventLane::Script);
        if (frame.updateNonCameraScripts)
            frame.updateNonCameraScripts();
    }
	if (frame.gameplayActive)
	{
		VansEventBus::Get().Flush(VansEventLane::GameLogic);
		if (frame.updateActionsEarly)
			frame.updateActionsEarly(frame.deltaSeconds);
		if (frame.updateAI)
			frame.updateAI(frame.deltaSeconds);
	}
	if (frame.gameplayActive && frame.prepareCharacterLocomotion)
		frame.prepareCharacterLocomotion(frame.deltaSeconds);

    if (frame.simulationRunning && frame.flushCharacterControllerTransforms)
        frame.flushCharacterControllerTransforms();

	if (frame.gameplayActive && frame.updateTimelinesPostScript)
		frame.updateTimelinesPostScript(frame.deltaSeconds);
	if (frame.cameraControlActive && frame.updateAdditionalPostScriptControllers)
		frame.updateAdditionalPostScriptControllers(frame.deltaSeconds);
	if (frame.gameplayActive || frame.cameraControlActive)
	{
		VansEventBus::Get().Flush(VansEventLane::GameLogic);
		VansEventBus::Get().Flush(VansEventLane::Script);
		VansEventBus::Get().Flush(VansEventLane::MainThread);
		if (frame.runTimelineLateContinuation)
			frame.runTimelineLateContinuation();
		if (frame.runActionLateContinuation)
			frame.runActionLateContinuation();
	}

    if (frame.gameplayActive && frame.updateCameraScripts)
        frame.updateCameraScripts();
	if (frame.cameraControlActive && frame.captureCameraControlBase)
		frame.captureCameraControlBase();

	if (frame.gameplayActive && frame.updateTimelinesCamera)
		frame.updateTimelinesCamera(frame.deltaSeconds);
	if (frame.cameraControlActive && frame.updateAdditionalCameraControllers)
		frame.updateAdditionalCameraControllers(frame.deltaSeconds);
	if (frame.cameraControlActive && frame.resolveCameraControlFrame)
		frame.resolveCameraControlFrame();
	if (frame.cameraControlActive)
	{
		VansEventBus::Get().Flush(VansEventLane::RenderPrep);
		VansEventBus::Get().Flush(VansEventLane::Diagnostics);
	}
}
}
