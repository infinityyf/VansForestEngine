#pragma once

#include "../Public/EngineDTOs.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansScene;

class VansMaterialLiveEditService
{
public:
	bool ApplyMaterialPreviewChange(
		VansScene* scene,
		const std::filesystem::path& assetPath,
		const std::vector<Vans::EditorAPI::RuntimeMaterialParameterEdit>& parameterEdits,
		const std::vector<Vans::EditorAPI::RuntimeMaterialTextureEdit>& textureEdits);
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
};
}
