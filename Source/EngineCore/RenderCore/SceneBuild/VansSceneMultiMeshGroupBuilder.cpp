#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "VansSceneMultiMeshGroupBuilder.h"

#include "../VansRenderNode.h"
#include "../VansScene.h"
#include "../VulkanCore/VansMesh.h"
#include "../../ScriptCore/VansScriptContext.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace VansGraphics
{
namespace
{
	VansSceneMultiMeshGroupBuildPlan Failure(std::string error)
	{
		VansSceneMultiMeshGroupBuildPlan result;
		result.error = std::move(error);
		return result;
	}

	bool IsRenderableSubmesh(VansMesh* mesh)
	{
		return mesh && mesh->GetMeshVertexCount() > 0 && mesh->GetIndexCount() >= 3;
	}
}

VansSceneMultiMeshGroupBuildPlan VansSceneMultiMeshGroupBuilder::Prepare(
	VansScene& scene,
	const Vans::VansSceneObjectBuildConfig& root,
	const std::unordered_set<std::uint32_t>& vehicleDrivenTransformIds)
{
	if (!root.multiMeshRoot)
		return Failure("Multi-mesh group preparation requires a MultiMeshRoot component");
	if (root.entityGuid.empty() || root.name.empty())
		return Failure("MultiMeshRoot requires a stable entity GUID and name");
	if (scene.HasMultiMeshGroup(root.entityGuid))
		return Failure("MultiMeshRoot '" + root.name + "' conflicts with an existing group");

	const Vans::VansSceneMultiMeshRootConfig& config = *root.multiMeshRoot;
	auto* sourceMesh = static_cast<VansMesh*>(scene.FindMeshAsset(config.modelGuid));
	if (!sourceMesh)
		return Failure("MultiMeshRoot '" + root.name + "' could not resolve Model '" +
			config.modelGuid + "'");
	if (!sourceMesh->m_IsMultiMesh)
		return Failure("MultiMeshRoot '" + root.name + "' requires a multi-mesh Model");
	if (sourceMesh->m_SubMeshes.size() >
		static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
		return Failure("MultiMeshRoot '" + root.name + "' exceeds the supported submesh count");
	if (config.submeshCount != sourceMesh->m_SubMeshes.size())
	{
		return Failure("MultiMeshRoot '" + root.name + "' declares " +
			std::to_string(config.submeshCount) + " submeshes but Model contains " +
			std::to_string(sourceMesh->m_SubMeshes.size()));
	}

	VansScriptObject* parentObject = scene.FindObjectByGuid(root.entityGuid);
	if (!parentObject || parentObject->m_TransformID == UINT32_MAX ||
		!Vans::VansTransformStore::IsAllocated(parentObject->m_TransformID))
		return Failure("MultiMeshRoot '" + root.name + "' has no valid root Transform");

	std::vector<bool> expectedRenderable(config.submeshCount, false);
	for (std::size_t index = 0; index < sourceMesh->m_SubMeshes.size(); ++index)
		expectedRenderable[index] = IsRenderableSubmesh(sourceMesh->m_SubMeshes[index]);

	VansSceneMultiMeshGroupBuildPlan result;
	result.parentName = root.name;
	result.parentEntityGuid = root.entityGuid;
	result.sourceMesh = sourceMesh;
	result.sharedTransformId = parentObject->m_TransformID;
	std::unordered_map<std::uint32_t, VansRenderNode*> nodesBySubmesh;

	for (VansScriptObject* childObject : scene.GetSceneObjects())
	{
		if (!childObject || childObject->m_EntityGuid.empty())
			continue;
		VansScriptRenderComponent* render =
			childObject->GetComponent<VansScriptRenderComponent>();
		if (!render)
			continue;
		const std::vector<VansRenderNode*>& renderNodes = render->m_RenderNodes;
		for (VansRenderNode* node : renderNodes)
		{
			if (!node || node->m_ParentEntityGuid != root.entityGuid)
				continue;
			if (node->m_SourceMesh != sourceMesh)
				return Failure("MultiMeshRoot '" + root.name +
					"' has a child bound to a different source Model");
			if (node->m_SubmeshIndex == UINT32_MAX ||
				node->m_SubmeshIndex >= config.submeshCount)
				return Failure("MultiMeshRoot '" + root.name +
					"' has a child with an invalid submesh index");
			if (!expectedRenderable[node->m_SubmeshIndex])
				return Failure("MultiMeshRoot '" + root.name + "' has a child for non-renderable submesh " +
					std::to_string(node->m_SubmeshIndex));
			if (!nodesBySubmesh.emplace(node->m_SubmeshIndex, node).second)
				return Failure("MultiMeshRoot '" + root.name + "' has duplicate submesh index " +
					std::to_string(node->m_SubmeshIndex));
			if (childObject->m_TransformID != node->m_TransformID ||
				childObject->m_OwnsTransform ||
				node->m_TransformID == result.sharedTransformId ||
				!Vans::VansTransformStore::IsAllocated(node->m_TransformID))
				return Failure("MultiMeshRoot '" + root.name + "' has inconsistent child Transform ownership");

			VansSceneMultiMeshChildBinding binding;
			binding.object = childObject;
			binding.renderNode = node;
			binding.transformId = node->m_TransformID;
			binding.keepsIndependentTransform = sourceMesh->m_HasNodeTransformAnimation ||
				vehicleDrivenTransformIds.count(node->m_TransformID) > 0;
			result.children.push_back(binding);
		}
	}

	for (std::uint32_t index = 0; index < config.submeshCount; ++index)
	{
		if (expectedRenderable[index] && nodesBySubmesh.find(index) == nodesBySubmesh.end())
			return Failure("MultiMeshRoot '" + root.name + "' is missing renderable submesh " +
				std::to_string(index));
	}
	if (result.children.empty())
		return Failure("MultiMeshRoot '" + root.name + "' produced no renderable children");

	std::sort(result.children.begin(), result.children.end(),
		[](const VansSceneMultiMeshChildBinding& lhs,
			const VansSceneMultiMeshChildBinding& rhs)
		{
			return lhs.renderNode->m_SubmeshIndex < rhs.renderNode->m_SubmeshIndex;
		});
	result.success = true;
	return result;
}

void VansSceneMultiMeshGroupBuilder::Commit(
	VansScene& scene,
	const VansSceneMultiMeshGroupBuildPlan& plan)
{
	MultiMeshGroup& group = scene.GetOrCreateMultiMeshGroup(plan.parentEntityGuid);
	group.parentName = plan.parentName;
	group.parentEntityGuid = plan.parentEntityGuid;
	group.sourceMesh = plan.sourceMesh;
	group.sharedTransformID = plan.sharedTransformId;
	group.ownsSharedTransform = false;
	group.childNodes.clear();
	group.childNodes.reserve(plan.children.size());

	for (const VansSceneMultiMeshChildBinding& binding : plan.children)
	{
		if (scene.GetParentTransformID(binding.transformId) != UINT32_MAX)
			scene.ClearTransformParentID(binding.transformId);
		if (!binding.keepsIndependentTransform)
		{
			binding.renderNode->ShareTransform(plan.sharedTransformId);
			binding.object->m_TransformID = plan.sharedTransformId;
			binding.object->m_OwnsTransform = false;
		}
		binding.renderNode->m_ParentGroupKey = plan.parentEntityGuid;
		group.childNodes.push_back(binding.renderNode);
	}
}
}
