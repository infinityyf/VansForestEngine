#pragma once

#include "VansAnimatorIO.h"

#include <string>

namespace VansGraphics
{
	// Validates the in-memory animator definition without routing through its
	// persistence codec. Runtime compilation and authoring storage share this
	// boundary, while each remains responsible for its own output.
	class VansAnimatorValidator
	{
	public:
		static bool Validate(const AnimatorAssetData& data, std::string& error);
		static bool ValidateGraph(
			const VansAnimGraph& graph,
			AnimatorGraphAsset::Role role,
			const std::string& graphName,
			std::string& error);
	};
}
