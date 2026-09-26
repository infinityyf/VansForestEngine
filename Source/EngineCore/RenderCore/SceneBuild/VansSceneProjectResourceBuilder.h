#pragma once

#include "../VansScene.h"
#include "../../SceneCore/VansSceneResourceLoadContext.h"
#include "../../SceneCore/VansSceneResourcePlan.h"

namespace VansGraphics
{
	class VansSceneProjectResourceBuilder
	{
	public:
		static bool LoadMeshes(
			VansScene& scene,
			const std::vector<Vans::VansSceneMeshResourceRequest>& meshes,
			const Vans::VansSceneResourceLoadContext& loadContext,
			VkDevice& device,
			VansVKDevice* vkDevice);

		static bool LoadShadersFromRegistry(
			VansScene& scene,
			const std::string& pathPrefix,
			VkDevice& device);

		static bool RegisterShaders(
			VansScene& scene,
			const std::vector<Vans::VansSceneShaderResourceRequest>& shaders,
			const Vans::VansSceneResourceLoadContext& loadContext,
			VkDevice& device,
			bool loadRegisteredShaders = true);

		static bool LoadTextures(
			VansScene& scene,
			const std::vector<Vans::VansSceneTextureResourceRequest>& textures,
			const Vans::VansSceneResourceLoadContext& loadContext,
			VansVKDevice* vkDevice,
			bool includeDefaultTextureSet = true);

		static VansTexture* LoadOrGetTexture(
			VansScene& scene,
			const std::string& absPath,
			bool isSRGB);

	private:
		static void ImportDefaultTexture(
			VansScene& scene,
			const std::string& name,
			VansVKDevice& vkDevice,
			bool isSRGB);
	};
}
