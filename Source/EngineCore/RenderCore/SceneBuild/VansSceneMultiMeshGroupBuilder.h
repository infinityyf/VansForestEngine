#pragma once

#include "../../SceneCore/VansSceneObjectBuildPlan.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class VansScriptObject;

namespace VansGraphics
{
	class VansMesh;
	class VansRenderNode;
	class VansScene;

	struct VansSceneMultiMeshChildBinding
	{
		VansScriptObject* object = nullptr;
		VansRenderNode* renderNode = nullptr;
		std::uint32_t transformId = UINT32_MAX;
		bool keepsIndependentTransform = false;
	};

	struct VansSceneMultiMeshGroupBuildPlan
	{
		bool success = false;
		std::string error;
		std::string parentName;
		std::string parentEntityGuid;
		VansMesh* sourceMesh = nullptr;
		std::uint32_t sharedTransformId = UINT32_MAX;
		std::vector<VansSceneMultiMeshChildBinding> children;
	};

	class VansSceneMultiMeshGroupBuilder
	{
	public:
		static VansSceneMultiMeshGroupBuildPlan Prepare(
			VansScene& scene,
			const Vans::VansSceneObjectBuildConfig& root,
			const std::unordered_set<std::uint32_t>& vehicleDrivenTransformIds);

		static void Commit(
			VansScene& scene,
			const VansSceneMultiMeshGroupBuildPlan& plan);
	};
}
