#pragma once

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansRenderNode;
}

namespace Vans::EditorAPI
{
std::string SanitizeRuntimeGeneratedMaterialText(std::string value);

std::string EnsureRuntimeGeneratedMaterialAsset(
	const std::string& rootName,
	VansGraphics::VansRenderNode* node,
	const std::filesystem::path& assetsRoot);
}
