#pragma once

#include "../../AnimationCore/Procedural/VansProceduralTypes.h"

#include <vector>

namespace VansGraphics
{
	// Render/Scene boundary adapter. AnimationCore emits engine-neutral query
	// contracts; this adapter is the only layer that translates them to PhysX.
	class VansAnimationWorldQueryBatch
	{
	public:
		static void Execute(
			const std::vector<VansWorldQueryRequest>& requests,
			std::vector<VansWorldQueryResult>& results);
	};
}
