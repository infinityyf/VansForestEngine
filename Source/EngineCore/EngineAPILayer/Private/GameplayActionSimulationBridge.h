#pragma once

#include "../Public/EngineDTOs.h"

namespace Vans::EditorAPI
{
class GameplayActionSimulationBridge
{
public:
	static GAFSimulationResult Simulate(const GAFSimulationRequest& request);
};
}
