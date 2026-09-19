#include "VansLodSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VansGraphics
{
namespace
{
constexpr float kEpsilon = 1.0e-6f;

bool Finite(float value)
{
    return std::isfinite(value);
}

float ClampHysteresis(float value)
{
    return std::clamp(Finite(value) ? value : 0.0f, 0.0f, 0.49f);
}

std::int32_t FirstAvailable(const std::vector<VansLodLevelMetric>& levels)
{
    for (std::size_t index = 0; index < levels.size(); ++index)
        if (levels[index].available)
            return static_cast<std::int32_t>(index);
    return -1;
}

std::int32_t NearestAvailable(const std::vector<VansLodLevelMetric>& levels, std::int32_t requested)
{
    if (levels.empty())
        return -1;
    requested = std::clamp(requested, 0, static_cast<std::int32_t>(levels.size() - 1));
    for (std::int32_t distance = 0; distance < static_cast<std::int32_t>(levels.size()); ++distance)
    {
        const std::int32_t left = requested - distance;
        if (left >= 0 && levels[static_cast<std::size_t>(left)].available)
            return left;
        const std::int32_t right = requested + distance;
        if (right < static_cast<std::int32_t>(levels.size()) && levels[static_cast<std::size_t>(right)].available)
            return right;
    }
    return -1;
}

float SafeBudget(const VansLodSelectionInput& input)
{
    const float quality = std::max(kEpsilon, Finite(input.qualityBias) ? input.qualityBias : 1.0f);
    const float budget = Finite(input.pixelErrorBudget) ? input.pixelErrorBudget : 1.0f;
    return std::max(kEpsilon, budget / quality);
}

float ProjectedErrorPixels(const VansLodSelectionInput& input, float objectSpaceError, float& screenHeight)
{
    screenHeight = 0.0f;
    if (!input.bounds.IsValid() || input.viewportSize.x <= 0.0f || input.viewportSize.y <= 0.0f ||
        !Finite(input.viewportSize.x) || !Finite(input.viewportSize.y) || !Finite(objectSpaceError) || objectSpaceError < 0.0f)
        return std::numeric_limits<float>::infinity();

    VansProjectedBounds projected;
    if (!ProjectRenderBoundsToScreen(input.bounds, input.view, input.projection, input.viewportSize,
        Finite(input.nearPlane) ? input.nearPlane : 0.1f, projected) || !projected.valid)
        return std::numeric_limits<float>::infinity();

    const float heightPixels = std::max(0.0f, (projected.uvMax.y - projected.uvMin.y) * input.viewportSize.y);
    screenHeight = heightPixels / input.viewportSize.y;

    // The local error is converted through the same conservative viewport scale
    // used by the projection matrix. Near-plane ambiguity remains conservative:
    // it returns infinity and therefore keeps the finest available level.
    const float depth = projected.nearestLinearDepth;
    if (!Finite(depth) || depth <= std::max(kEpsilon, input.nearPlane))
        return std::numeric_limits<float>::infinity();
    const float fy = std::abs(input.projection[1][1]) * input.viewportSize.y * 0.5f;
    const float scale = fy / depth;
    return std::max(0.0f, objectSpaceError) * std::max(0.0f, scale);
}

float ProjectionScale(const VansLodSelectionInput& input, float& screenHeight)
{
    const float unitError = ProjectedErrorPixels(input, 1.0f, screenHeight);
    return Finite(unitError) ? unitError : std::numeric_limits<float>::infinity();
}

std::int32_t SelectByError(const VansLodSelectionInput& input, float projectionScale, float budget)
{
    // LOD0 is the finest level. Select the coarsest available level whose
    // measured error fits the budget; invalid/non-monotonic metrics are handled
    // conservatively by retaining the finest available level.
    std::int32_t selected = FirstAvailable(input.levels);
    if (selected < 0)
        return -1;
    for (std::size_t index = static_cast<std::size_t>(selected); index < input.levels.size(); ++index)
    {
        const auto& level = input.levels[index];
        if (level.available && Finite(level.value) &&
            Finite(projectionScale) && level.value * projectionScale <= budget)
            selected = static_cast<std::int32_t>(index);
    }

    if (!Finite(projectionScale))
        return FirstAvailable(input.levels);
    return selected;
}

std::int32_t SelectByScreenHeight(const VansLodSelectionInput& input, float screenHeight)
{
    std::int32_t selected = FirstAvailable(input.levels);
    if (selected < 0)
        return -1;
    for (std::size_t index = static_cast<std::size_t>(selected); index < input.levels.size(); ++index)
    {
        const auto& level = input.levels[index];
        if (level.available && Finite(level.value) && screenHeight < level.value)
            selected = static_cast<std::int32_t>(index);
    }
    return selected;
}
}

VansLodSelectionResult SelectLod(const VansLodSelectionInput& input)
{
    VansLodSelectionResult result;
    if (input.levels.empty())
    {
        result.usedFallback = true;
        result.valid = false;
        return result;
    }

    float screenHeight = 0.0f;
    const float projectionScale = ProjectionScale(input, screenHeight);
    result.projectedErrorPixels = projectionScale;
    result.screenHeight = screenHeight;

    std::int32_t candidate = 0;
    if (input.mode == VansLodSelectionMode::ScreenRelativeHeight)
        candidate = SelectByScreenHeight(input, screenHeight);
    else
        candidate = SelectByError(input, projectionScale, SafeBudget(input));

    const std::int32_t finest = FirstAvailable(input.levels);
    if (candidate < 0 || finest < 0)
    {
        result.level = 0;
        result.usedFallback = true;
        result.valid = false;
        return result;
    }

    const std::int32_t previous = input.resetHistory || input.previousLevel < 0
        ? -1
        : NearestAvailable(input.levels, input.previousLevel);
    if (previous >= 0 && previous != candidate)
    {
        const float hysteresis = ClampHysteresis(input.hysteresis);
        if (input.mode == VansLodSelectionMode::ScreenRelativeHeight)
        {
            const float threshold = input.levels[static_cast<std::size_t>(std::max(previous, candidate))].value;
            if (Finite(threshold) && previous < candidate && screenHeight >= threshold * (1.0f - hysteresis))
                candidate = previous;
            else if (Finite(threshold) && previous > candidate && screenHeight < threshold * (1.0f + hysteresis))
                candidate = previous;
        }
        else
        {
            const float budget = SafeBudget(input);
            const float metric = input.levels[static_cast<std::size_t>(previous)].value;
            if (Finite(metric) && Finite(projectionScale))
            {
                if (previous < candidate && metric * projectionScale <= budget * (1.0f + hysteresis))
                    candidate = previous;
                else if (previous > candidate && metric * projectionScale >= budget * (1.0f - hysteresis))
                    candidate = previous;
            }
        }
    }

    result.level = NearestAvailable(input.levels, candidate);
    if (input.mode == VansLodSelectionMode::AutoScreenError && result.level >= 0 &&
        Finite(projectionScale) && Finite(input.levels[static_cast<std::size_t>(result.level)].value))
        result.projectedErrorPixels = input.levels[static_cast<std::size_t>(result.level)].value * projectionScale;
    result.usedFallback = result.level != candidate || !Finite(projectionScale);
    result.valid = result.level >= 0;
    if (!result.valid)
        result.level = finest;
    return result;
}
}
