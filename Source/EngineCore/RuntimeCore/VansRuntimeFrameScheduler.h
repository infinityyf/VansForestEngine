#pragma once

#include <functional>

namespace Vans
{
struct VansRuntimeGameplayFrame
{
    bool sceneReady = false;
    bool simulationRunning = false;
    bool gameplayActive = false;
	double deltaSeconds = 0.0;
    std::function<void()> syncPhysicsTransforms;
    std::function<void()> updateNonCameraScripts;
    std::function<void()> flushCharacterControllerTransforms;
	std::function<void(double)> updateTimelinesPostScript;
    std::function<void()> updateCameraScripts;
	std::function<void(double)> updateTimelinesCamera;
};

class VansRuntimeFrameScheduler
{
public:
    static void RunGameplay(const VansRuntimeGameplayFrame& frame);
};
}
