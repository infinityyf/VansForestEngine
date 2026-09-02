#pragma once

#include <functional>

namespace Vans
{
struct VansRuntimeGameplayFrame
{
    bool sceneReady = false;
	bool simulationRunning = false;
	bool gameplayActive = false;
	bool cameraControlActive = false;
	double deltaSeconds = 0.0;
    std::function<void()> syncPhysicsTransforms;
    std::function<void()> updateNonCameraScripts;
	std::function<void(double)> updateActionsEarly;
	std::function<void(double)> updateAI;
	std::function<void(double)> prepareCharacterLocomotion;
    std::function<void()> flushCharacterControllerTransforms;
	std::function<void(double)> updateTimelinesPostScript;
	std::function<void(double)> updateAdditionalPostScriptControllers;
	std::function<void()> runTimelineLateContinuation;
	std::function<void()> runActionLateContinuation;
	std::function<void()> beginCameraControlFrame;
    std::function<void()> updateCameraScripts;
	std::function<void()> captureCameraControlBase;
	std::function<void(double)> updateTimelinesCamera;
	std::function<void(double)> updateAdditionalCameraControllers;
	std::function<void()> resolveCameraControlFrame;
};

class VansRuntimeFrameScheduler
{
public:
    static void RunGameplay(const VansRuntimeGameplayFrame& frame);
};
}
