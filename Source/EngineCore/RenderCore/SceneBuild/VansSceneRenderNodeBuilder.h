#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneRenderNodeConfig.h"

namespace VansGraphics
{
	class VansSceneRenderNodeBuilder
	{
	public:
		static VansRenderNode* LoadSingleRenderNode(VansScene& scene, VkDevice& device, const Vans::VansSceneRenderNodeConfig& config);
		static void LoadRenderNodes(VansScene& scene, VkDevice& device, const Vans::VansSceneRenderNodeConfigs& renderNodes);
		static void AddDeferredNode(VansScene& scene, VkDevice& device);
		static void AddScreenSpaceFeatureNode(VansScene& scene, VkDevice& device);
		static void ExpandMultiMeshToRenderNodes(
			VansScene& scene,
			VkDevice& device,
			VansMesh* multiMesh,
			const std::string& parentName,
			const std::string& parentEntityGuid,
			const glm::vec3& position,
			const glm::vec3& rotation,
			const glm::vec3& scale,
			bool supportShadow,
			uint32_t shadowCasterMask,
			VansMaterial* materialOverride = nullptr);

		static void ExpandMultiMeshToRenderNodes(
			VansScene& scene,
			VkDevice& device,
			VansMesh* multiMesh,
			const std::string& parentName,
			const std::string& parentEntityGuid,
			const glm::vec3& position,
			const glm::vec3& rotation,
			const glm::vec3& scale,
			bool supportShadow,
			uint32_t shadowCasterMask,
			VansMaterial* materialOverride,
			const std::unordered_map<std::string, std::string>& submeshMaterialOverrides);
	};
}
