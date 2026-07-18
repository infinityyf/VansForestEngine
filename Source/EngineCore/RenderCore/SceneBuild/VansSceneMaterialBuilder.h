#pragma once

#include "../VansScene.h"

namespace VansGraphics
{
	class VansSceneMaterialBuilder
	{
	public:
		static VansMaterialType ParseMaterialType(const json& typeValue, const std::string& materialName);
		static VansMaterial* CreateMaterialForType(VansMaterialType materialType);
		static void PopulateMaterialPassShaders(VansScene& scene, VansMaterial* material, VansMaterialType materialType);
		static void ApplyMaterialShaderOverrides(VansScene& scene, VansMaterial* material);
		static VansTexture* ResolveMaterialTexture(VansScene& scene, const json& sceneMaterial, const char* key);
		static VansTexture* ResolveMaterialTextureWithFallback(VansScene& scene, const json& sceneMaterial, const char* key, const char* fallback);
		static VansTexture* ResolveMaterialTextureOrDefault(VansScene& scene, const json& sceneMaterial, const char* key, const char* fallback);
		static void PopulateMaterialFromJson(VansScene& scene, VansMaterial* material, VansMaterialType materialType, const json& sceneMaterial);
		static void LoadMaterialsFromJson(VansScene& scene, const json& materialData);
	};
}
