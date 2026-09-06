#pragma once

#include <string>

namespace VansGraphics
{
class VansRenderNode;
}

namespace Vans
{
class VansAssetDatabase;
}

namespace Vans::EditorAPI
{
struct RuntimeGeneratedMaterialDraft
{
	std::string guid;
	std::string sourcePath;
	std::string sourceCanonicalJson;
	std::string metaCanonicalJson;
	bool requiresSave = false;
};

std::string SanitizeRuntimeGeneratedMaterialText(std::string value);

RuntimeGeneratedMaterialDraft BuildRuntimeGeneratedMaterialDraft(
	const std::string& rootName,
	VansGraphics::VansRenderNode* node,
	Vans::VansAssetDatabase& database);
}
