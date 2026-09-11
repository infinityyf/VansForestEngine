#pragma once

#include "VansReflectionProbe.h"
#include "../GeometryCore/VansSceneGeometrySnapshot.h"

namespace VansGraphics
{
    struct VansReflectionProbePlacementResult
    {
        std::vector<VansReflectionProbeDesc> probes;
        uint32_t receiverCount = 0;
        uint32_t coveredReceiverCount = 0;
        uint32_t rejectedCaptureCount = 0;
        uint32_t candidateCount = 0;
        float reachableSurfaceFraction = 0.0f;
        float coveredSurfaceFraction = 0.0f;
        bool budgetExhausted = false;
    };

    // GI 不引用该求解器；反射布局的采样密度、捕获有效性和预算独立处理。
    class VansReflectionProbePlacement
    {
    public:
        static bool Generate(const VansSceneGeometrySnapshot& geometry,
            const ReflectionProbePlacementSettings& settings,
            const std::vector<VansReflectionProbeDesc>& overrides,
            VansReflectionProbePlacementResult& result, std::string& error);
    };
}
