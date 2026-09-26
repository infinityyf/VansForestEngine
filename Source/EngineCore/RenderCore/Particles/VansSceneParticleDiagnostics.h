#pragma once
#include "VansParticleRenderSystem.h"
#include "../../ParticleCore/VansParticleFrameData.h"
#include "../../RuntimeCore/VansGenerationPool.h"

namespace VansGraphics
{
struct VansParticleEffectDiagnostics
{
    Vans::VansGenerationHandle instance;
    std::string effectGuid, sourceGuid, state;
    glm::vec3 sourcePosition{0};
    float playTime = 0;
    bool detached = false;
    std::uint64_t alivePoints = 0, droppedSpawns = 0, breaks = 0, substepOverruns = 0;
    std::vector<VansParticleRibbonStrip> ribbons;
};
struct VansSceneParticleDiagnostics
{
    std::uint64_t activeInstances = 0, pointCapacity = 0, rejectedInstances = 0;
    std::uint32_t resimulationSteps = 0, pendingResimulations = 0;
    double simulationMilliseconds = 0, resimulationMilliseconds = 0;
    double mainThreadOverlapMilliseconds = 0, waitMilliseconds = 0;
    VansParticleRenderDiagnostics rendering;
    std::vector<VansParticleEffectDiagnostics> effects;
};
}
