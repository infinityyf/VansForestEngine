#pragma once

#include "../VansScene.h"

namespace VansGraphics
{
	class VansSceneRenderNodeBuilder
	{
	public:
		static VansRenderNode* LoadSingleRenderNode(VansScene& scene, VkDevice& device, const json& renderNodeJson);
		static void LoadRenderNodes(VansScene& scene, VkDevice& device, json& renderNodes);
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
			VansMaterial* materialOverride = nullptr);
	};
}
