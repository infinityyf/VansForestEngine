#pragma once

#include "../VansRenderBounds.h"

#include <cstdint>
#include <vector>

namespace VansGraphics
{
    enum class VansLodSelectionMode : std::uint8_t
    {
        AutoScreenError,
        ScreenRelativeHeight
    };

    struct VansLodLevelMetric final
    {
        // AutoScreenError: selected projected object-space error in output pixels.
        // ScreenRelativeHeight: threshold at which this level becomes active.
        float value = 0.0f;
        bool available = true;
    };

    struct VansLodSelectionInput final
    {
        VansRenderBounds bounds;
        glm::mat4 view{ 1.0f };
        glm::mat4 projection{ 1.0f };
        glm::vec2 viewportSize{ 0.0f };
        float nearPlane = 0.1f;
        float pixelErrorBudget = 1.0f;
        float qualityBias = 1.0f;
        float hysteresis = 0.1f;
        VansLodSelectionMode mode = VansLodSelectionMode::AutoScreenError;
        std::vector<VansLodLevelMetric> levels;
        std::int32_t previousLevel = -1;
        bool resetHistory = false;
    };

    struct VansLodSelectionResult final
    {
        std::int32_t level = 0;
        float projectedErrorPixels = 0.0f;
        float screenHeight = 0.0f;
        bool usedFallback = false;
        bool valid = false;
    };

    // Pure view selection. It does not load assets, mutate render nodes, or access
    // ray-tracing resources. Acceleration-structure ownership stays with the
    // source geometry binding and is intentionally outside this contract.
    VansLodSelectionResult SelectLod(const VansLodSelectionInput& input);
}
