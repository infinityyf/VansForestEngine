#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneRenderNodeConfig.h"

namespace VansGraphics
{
	struct VansRenderNodeBuildResult
	{
		bool success = false;
		std::vector<VansRenderNode*> nodes;
		std::vector<VansMaterial*> createdMaterials;
		std::vector<VansMesh*> registeredSubMeshes;
		std::string error;

		VansRenderNode* GetPrimaryNode() const
		{
			return nodes.empty() ? nullptr : nodes.front();
		}
	};

	class VansSceneRenderNodeBuilder
	{
	public:
		static VansRenderNodeBuildResult BuildRenderNode(
			VansScene& scene,
			VkDevice& device,
			const Vans::VansSceneRenderNodeConfig& config);
		static bool BuildRenderNodes(
			VansScene& scene,
			VkDevice& device,
			const Vans::VansSceneRenderNodeConfigs& renderNodes,
			std::string& error);
		static bool BuildDeferredNode(VansScene& scene, VkDevice& device, std::string& error);
		static bool BuildScreenSpaceFeatureNodes(VansScene& scene, VkDevice& device, std::string& error);

	private:
		static VansRenderNodeBuildResult ExpandMultiMeshToRenderNodes(
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
