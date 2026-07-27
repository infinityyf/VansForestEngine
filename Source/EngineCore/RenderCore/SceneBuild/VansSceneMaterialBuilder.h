#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneMaterialConfig.h"

namespace VansGraphics
{
	class VansSceneMaterialBuilder
	{
	public:
		static VansMaterialType ParseMaterialType(const std::string& typeValue, const std::string& materialName);
		static VansMaterial* CreateMaterialForType(VansMaterialType materialType);
		static void PopulateMaterialPassShaders(VansScene& scene, VansMaterial* material, VansMaterialType materialType);
		static void ApplyMaterialShaderOverrides(VansScene& scene, VansMaterial* material);
		static VansTexture* ResolveMaterialTexture(VansScene& scene, const Vans::VansSceneMaterialConfig& sceneMaterial, const char* key);
		static VansTexture* ResolveMaterialTextureWithFallback(VansScene& scene, const Vans::VansSceneMaterialConfig& sceneMaterial, const char* key, const char* fallback);
		static VansTexture* ResolveMaterialTextureOrDefault(VansScene& scene, const Vans::VansSceneMaterialConfig& sceneMaterial, const char* key, const char* fallback);
		static void PopulateMaterial(VansScene& scene, VansMaterial* material, VansMaterialType materialType, const Vans::VansSceneMaterialConfig& sceneMaterial);
		static void LoadMaterials(VansScene& scene, const Vans::VansSceneMaterialConfigs& materialData);
	};
}
