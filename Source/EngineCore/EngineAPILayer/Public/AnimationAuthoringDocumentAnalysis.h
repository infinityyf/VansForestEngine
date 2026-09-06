#pragma once

#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	struct AnimationAuthoringDocumentAnalysis
	{
		bool success = false;
		std::string message;
		std::vector<std::string> dependencies;

		explicit operator bool() const { return success; }
	};

	AnimationAuthoringDocumentAnalysis AnalyzeAnimationAuthoringDocument(
		const std::string& sourcePath,
		const std::string& canonicalJson);
}
