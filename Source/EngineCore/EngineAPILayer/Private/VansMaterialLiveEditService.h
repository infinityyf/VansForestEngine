#pragma once

#include "../Public/EngineDTOs.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Vans { enum class VansClothAlphaMode; }

namespace VansGraphics
{
class VansScene;
class VansRenderSystem;

struct VansMaterialOverrideBatchResult
{
	bool transactionSucceeded = false;
	bool anyApplied = false;
	bool anyFailed = false;
};

class VansMaterialLiveEditService
{
public:
	bool ApplyMaterialPreviewChange(
		VansScene* scene,
		const std::filesystem::path& assetPath,
		const std::vector<Vans::EditorAPI::RuntimeMaterialParameterEdit>& parameterEdits,
		const std::vector<Vans::EditorAPI::RuntimeMaterialTextureEdit>& textureEdits,
		std::optional<Vans::VansClothAlphaMode> pendingClothAlphaMode = std::nullopt);
	bool ApplyMaterialParameter(
		VansScene* scene,
		const std::string& materialName,
		const std::string& parameterPath,
		const Vans::EditorAPI::PropertyValue& value);
	bool ApplyMaterialTexture(
		VansScene* scene,
		const std::string& materialName,
		const std::string& slot,
		const std::string& textureGuid);
	bool ApplyRendererMaterialOverride(
		VansScene* scene,
		const Vans::EditorAPI::RuntimeRendererMaterialOverrideEdit& edit);
	VansMaterialOverrideBatchResult ApplyRendererMaterialOverrideBatch(
		VansRenderSystem& renderSystem,
		VansScene& scene,
		std::vector<Vans::EditorAPI::RuntimeRendererMaterialOverrideEdit> edits);
};
}
