#pragma once

#include "../VansScene.h"
#include "../../SceneCore/VansSceneResourcePlan.h"

namespace VansGraphics
{
	class VansSceneProjectResourceBuilder
	{
	public:
		static void LoadMeshes(
			VansScene& scene,
			const std::vector<Vans::VansSceneMeshResourceRequest>& meshes,
			const std::string& pathPrefix,
			VkDevice& device,
			VansVKDevice* vkDevice);

		static void LoadShadersFromRegistry(
			VansScene& scene,
			const std::string& pathPrefix,
			VkDevice& device);

		static void RegisterShaders(
			VansScene& scene,
			const std::vector<Vans::VansSceneShaderResourceRequest>& shaders,
			const std::string& pathPrefix,
			VkDevice& device,
			bool loadRegisteredShaders = true);

		static void LoadTextures(
			VansScene& scene,
			const std::vector<Vans::VansSceneTextureResourceRequest>& textures,
			const std::string& pathPrefix,
			const std::string& enginePrefix,
			VansVKDevice* vkDevice,
			bool includeDefaultTextureSet = true);

		static void ImportDefaultTexture(
			VansScene& scene,
			const std::string& path,
			const std::string& name,
			VansVKDevice* vkDevice,
			bool isSRGB);

		static VansTexture* LoadOrGetTexture(
			VansScene& scene,
			const std::string& absPath,
			bool isSRGB);

		static void LoadShaderFromEntry(
			VansScene& scene,
			const VansShaderEntry& entry,
			const std::string& pathPrefix,
			VkDevice& device);
	};
}
