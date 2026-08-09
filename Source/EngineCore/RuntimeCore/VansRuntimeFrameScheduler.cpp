#include "VansRuntimeFrameScheduler.h"

#include "VansFramePhase.h"
#include "../EventCore/VansEventBus.h"

namespace Vans
{
void VansRuntimeFrameScheduler::RunGameplay(const VansRuntimeGameplayFrame& frame)
{
    VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
    if (!frame.sceneReady)
        return;

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

    if (frame.simulationRunning && frame.flushCharacterControllerTransforms)
        frame.flushCharacterControllerTransforms();

	if (frame.gameplayActive && frame.updateTimelinesPostScript)
		frame.updateTimelinesPostScript(frame.deltaSeconds);

    if (frame.gameplayActive && frame.updateCameraScripts)
        frame.updateCameraScripts();

	if (frame.gameplayActive && frame.updateTimelinesCamera)
		frame.updateTimelinesCamera(frame.deltaSeconds);
}
}
