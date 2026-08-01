#pragma once

#include <functional>

namespace Vans
{
struct VansRuntimeGameplayFrame
{
    bool sceneReady = false;
    bool simulationRunning = false;
    bool gameplayActive = false;
    std::function<void()> syncPhysicsTransforms;
    std::function<void()> updateNonCameraScripts;
    std::function<void()> flushCharacterControllerTransforms;
    std::function<void()> updateCameraScripts;
};

class VansRuntimeFrameScheduler
{
public:
    static void RunGameplay(const VansRuntimeGameplayFrame& frame);
};
}
