#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../VansScene.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "VansAnimationWorldQueryBatch.h"
#include "../../SceneRuntime/Animation/VansAnimationTargetResolver.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
std::uint32_t VansScene::FindAnimationTargetTransform(const std::string& entityGuid) const
{
	if (!m_RuntimeWorld) return UINT32_MAX;
	const auto entity = m_RuntimeWorld->Entities().FindByGuid(entityGuid);
	const auto component = m_RuntimeWorld->FindComponentOwnedBy(entity, Vans::VansRuntimeComponentType_Transform);
	const auto* storage = m_RuntimeWorld->FindStorage<Vans::VansRuntimeTransformComponent>(
		Vans::VansRuntimeComponentType_Transform);
	const auto* transform = storage ? storage->Get(component) : nullptr;
	return transform ? transform->transformStoreId : UINT32_MAX;
}

std::vector<VansAnimationNode*> VansScene::GetAnimationTargetDependencies(const VansAnimationNode& node) const
{
	std::vector<VansAnimationNode*> dependencies;
	auto visit = [&](std::uint32_t transform)
	{
		std::unordered_set<std::uint32_t> visited;
		while (transform != UINT32_MAX && visited.insert(transform).second)
		{
			const auto* link = m_TransformGraph.GetLink(transform);
			if (!link) break;
			if (link->usesAnchor)
			{
				auto* dependency = m_SkeletonAnchorRegistry.FindAnimationNode(link->anchor);
				if (dependency && dependency != &node &&
					std::find(dependencies.begin(), dependencies.end(), dependency) == dependencies.end())
					dependencies.push_back(dependency);
			}
			transform = link->parentTransformId;
		}
	};
	visit(node.GetTransformID());
	for (const auto& binding : node.GetTargetBindings()) visit(FindAnimationTargetTransform(binding.targetEntityGuid));
	return dependencies;
}

void VansScene::ResolveAnimationTargetBindings(VansAnimationNode& node)
{
	auto* controller = node.GetController();
	if (!controller) return;
	auto input = controller->GetAnimationExternalInput();
	input.targets.clear();
	input.targets.reserve(node.GetTargetBindings().size());
	for (const auto& binding : node.GetTargetBindings())
	{
		VansResolvedAnimationTarget target;
		target.id = binding.id;
		const auto transform = FindAnimationTargetTransform(binding.targetEntityGuid);
		if (transform == UINT32_MAX) target.diagnostic = "Target entity or Transform is missing: " + binding.targetEntityGuid;
		else Vans::VansAnimationTargetResolver::Resolve(m_TransformGraph, transform,
			[&](const Vans::VansTransformGraphLink& link, Vans::VansAnimationTargetAnchor& anchor)
			{
				auto* source = m_SkeletonAnchorRegistry.FindAnimationNode(link.anchor);
				if (!source) return false;
				anchor.sameSkeleton = source == &node;
				const auto* rig = source->GetController() ? source->GetController()->GetAnimationRig() : nullptr;
				if (link.anchor.kind == Vans::VansTransformAnchorKind::Bone)
				{
					const auto bone = source->GetSkeleton().boneGuidToIndex.find(link.anchor.anchorGuid);
					if (bone == source->GetSkeleton().boneGuidToIndex.end()) return false;
					anchor.boneIndex = bone->second;
				}
				else
				{
					const int socket = rig ? rig->FindSocketByGuid(link.anchor.anchorGuid) : -1;
					if (socket < 0) return false;
					anchor.boneIndex = rig->sockets[socket].boneIndex;
					anchor.boneToAnchor = rig->sockets[socket].localTransform;
				}
				if (!link.anchor.poseCheckpoint.empty() && (!source->GetController() ||
					!source->GetController()->HasPoseCheckpointBone(link.anchor.poseCheckpoint, anchor.boneIndex))) return false;
				if (anchor.sameSkeleton) return true;
				std::uint64_t revision = 0;
				glm::mat4 model(1.0f);
				if (!m_SkeletonAnchorRegistry.ResolveModelSpaceTransform(link.anchor, model, revision)) return false;
				anchor.world = Vans::VansTransformStore::Read(link.parentTransformId).GetModelMatrix() * model;
				return true;
			}, target);
		input.targets.push_back(std::move(target));
	}
	controller->SetAnimationExternalInput(std::move(input));
}

bool VansScene::EvaluateAnimationBatch(const std::vector<VansAnimationNode*>& nodes, float deltaTime, bool gameplay)
{
	// 按依赖入度分层，不在每一层重新扫描全部动画组件。
	std::unordered_map<VansAnimationNode*, std::size_t> indices;
	indices.reserve(nodes.size());
	for (std::size_t index = 0; index < nodes.size(); ++index) indices.emplace(nodes[index], index);
	std::vector<std::vector<std::size_t>> dependents(nodes.size());
	std::vector<std::size_t> pending(nodes.size(), 0);
	std::vector<bool> completed(nodes.size(), false);
	std::vector<std::size_t> level;
	level.reserve(nodes.size());
	for (std::size_t index = 0; index < nodes.size(); ++index)
	{
		const auto dependencies = GetAnimationTargetDependencies(*nodes[index]);
		pending[index] = dependencies.size();
		for (auto* dependency : dependencies)
		{
			const auto source = indices.find(dependency);
			if (source != indices.end()) dependents[source->second].push_back(index);
		}
		if (pending[index] == 0) level.push_back(index);
	}
	std::size_t completedCount = 0;
	std::vector<std::size_t> nextLevel;
	nextLevel.reserve(nodes.size());
	const VansAnimationFrameContext context{ gameplay ? VansAnimationEvaluationPurpose::Gameplay :
		VansAnimationEvaluationPurpose::EditorPreview, deltaTime };
	while (completedCount < nodes.size())
	{
		if (level.empty())
		{
			// 无法排序的组件不读取历史目标。仍评估输入动画，失效约束有明确诊断。
			for (std::size_t index = 0; index < nodes.size(); ++index)
			{
				auto* node = nodes[index];
				if (completed[index] || !node->GetController()) continue;
				auto input = node->GetController()->GetAnimationExternalInput();
				input.targets.clear();
				for (const auto& binding : node->GetTargetBindings())
				{
					VansResolvedAnimationTarget target;
					target.id = binding.id;
					target.diagnostic = "Animation target dependency cycle or unavailable source component";
					input.targets.push_back(std::move(target));
				}
				node->GetController()->SetAnimationExternalInput(std::move(input));
				node->PrepareAnimationFrame(context);
				node->GatherAnimationWorldQueries();
				std::vector<VansWorldQueryResult> results;
				VansAnimationWorldQueryBatch::Execute(node->GetAnimationWorldQueries(), results);
				node->ResolveAnimationWorldQueries(results);
				if (gameplay) ApplyRagdollPose(*node);
			}
			return false;
		}
		m_AnimationWorldQueryRequests.clear();
		m_AnimationWorldQueryResults.clear();
		for (const auto index : level)
		{
			auto* node = nodes[index];
			ResolveAnimationTargetBindings(*node);
			node->PrepareAnimationFrame(context);
			node->GatherAnimationWorldQueries();
			const auto& queries = node->GetAnimationWorldQueries();
			m_AnimationWorldQueryRequests.insert(m_AnimationWorldQueryRequests.end(), queries.begin(), queries.end());
		}
		VansAnimationWorldQueryBatch::Execute(m_AnimationWorldQueryRequests, m_AnimationWorldQueryResults);
		nextLevel.clear();
		for (const auto index : level)
		{
			auto* node = nodes[index];
			node->ResolveAnimationWorldQueries(m_AnimationWorldQueryResults);
			if (gameplay) ApplyRagdollPose(*node);
			completed[index] = true;
			++completedCount;
			for (const auto dependent : dependents[index])
				if (--pending[dependent] == 0) nextLevel.push_back(dependent);
		}
		// 仅在依赖层之间刷新附件，使下游动画组件读取同帧 owner 变换。
		if (completedCount < nodes.size()) m_TransformGraph.Resolve();
		level.swap(nextLevel);
	}
	return true;
}
}
