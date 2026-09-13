#include "VansProceduralGraphRuntime.h"

#include "Grounding/VansGroundingRuntime.h"
#include "Solvers/VansAimConstraintSolver.h"
#include "Solvers/VansChainIKSolver.h"
#include "Solvers/VansLimbIKSolver.h"
#include "Solvers/VansRotationDistributionSolver.h"
#include "VansPoseWorkspace.h"
#include "../VansAnimGraph.h"
#include "../VansPoseMath.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;
		constexpr float kLn2 = 0.69314718055994530942f;

		enum class RuntimeNodeKind { Goal, Aim, Grounding, Limb, Chain, Checkpoint, RotationDistribution };

		int Phase(RuntimeNodeKind kind)
		{
			switch (kind)
			{
			case RuntimeNodeKind::Goal: return 0;
			case RuntimeNodeKind::Aim: return 1;
			case RuntimeNodeKind::Grounding: return 2;
			case RuntimeNodeKind::Limb:
			case RuntimeNodeKind::RotationDistribution:
			case RuntimeNodeKind::Chain: return 3;
			case RuntimeNodeKind::Checkpoint: return 3;
			}
			return -1;
		}

		VansProceduralDebugKind DebugKind(RuntimeNodeKind kind)
		{
			switch (kind)
			{
			case RuntimeNodeKind::Goal: return VansProceduralDebugKind::Goal;
			case RuntimeNodeKind::Aim: return VansProceduralDebugKind::Aim;
			case RuntimeNodeKind::Grounding: return VansProceduralDebugKind::Grounding;
			case RuntimeNodeKind::Limb: return VansProceduralDebugKind::LimbIK;
			case RuntimeNodeKind::Chain: return VansProceduralDebugKind::ChainIK;
			case RuntimeNodeKind::Checkpoint: return VansProceduralDebugKind::PoseCheckpoint;
			case RuntimeNodeKind::RotationDistribution: return VansProceduralDebugKind::RotationDistribution;
			}
			return VansProceduralDebugKind::Goal;
		}

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::quat& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z) && std::isfinite(value.w)
				&& glm::dot(value, value) > kEpsilon * kEpsilon;
		}

		float HalfLifeAlpha(float deltaTime, float halfLife)
		{
			if (deltaTime <= 0.0f) return 0.0f;
			if (halfLife <= kEpsilon) return 1.0f;
			return 1.0f - std::exp(-kLn2 * deltaTime / halfLife);
		}

		bool ReadFloat(const VansProceduralParameterAccessor& accessor,
		               const std::string& name, float& value)
		{
			return !name.empty() && accessor.readFloat
				&& accessor.readFloat(accessor.context, name, value);
		}

		struct CompiledRuntimeNode
		{
			int nodeId = -1;
			RuntimeNodeKind kind = RuntimeNodeKind::Goal;
			int goalIndex = -1;
			std::vector<int> chainIndices;
			std::vector<int> writeBones;
			int rotationProfileIndex = -1;
			VansRotationDistributionState rotationState;
			VansGraphGoalDefinition goal;
			std::string checkpointId;
			std::vector<int> checkpointBones;
			std::vector<glm::mat4> checkpointWorking, checkpointPublished;
			bool checkpointCaptured = false, checkpointValid = false;
			VansAimConstraintSettings aimSettings;
			std::string directionParameter;
			std::string directionWeightParameter;
			bool directionIsWorldSpace = true;
			VansAimConstraintState aimState;
			int pivotBoneIndex = -1;
			float targetHalfLife = 0.0f;
			VansLimbIKSettings limbSettings;
			VansChainIKSettings chainSettings;
			VansGroundingRuntime grounding;
			std::size_t debugRecordOffset = 0;
			bool smoothedTargetValid = false;
			VansAimConstraintTarget smoothedTarget;
		};

		bool IsDownstream(const VansAnimGraph& graph, int upstream, int downstream)
		{
			std::vector<int> pending{ upstream };
			std::unordered_set<int> visited;
			while (!pending.empty())
			{
				const int current = pending.back();
				pending.pop_back();
				if (current == downstream) return true;
				if (!visited.insert(current).second) continue;
				for (const AnimGraphLink& link : graph.GetLinks())
					if (link.fromNodeId == current) pending.push_back(link.toNodeId);
			}
			return false;
		}
	}

	struct VansProceduralGraphRuntime::Impl
	{
		const VansCompiledAnimationRig* rig = nullptr;
		const VansAnimGraph* graph = nullptr;
		std::vector<CompiledRuntimeNode> nodes;
		std::unordered_map<int, std::size_t> nodeIndexById;
		std::vector<int> activeNodeIds;
		std::vector<VansProceduralGoal> goals;
		std::vector<const VansGraphGoalDefinition*> goalDefinitions;
		std::unordered_map<std::string, std::size_t> checkpointNodes;
		std::unordered_map<std::string, const VansResolvedAnimationTarget*> targetsById;
		std::vector<VansProceduralDebugRecord> debugRecords;
		std::unordered_set<int> groundedChainIndices;
		VansPoseWorkspace workspace;
		VansPoseWorkspace transactionStart;
		std::vector<VansAimConstraintTarget> transactionSmoothedTargets;
		std::vector<VansAimConstraintState> transactionAimStates;
		std::vector<VansRotationDistributionState> transactionRotationStates;
		std::vector<std::uint8_t> transactionSmoothedTargetValid;
		VansAnimationExternalInputSnapshot input;
		VansProceduralParameterAccessor parameters;
		float deltaTime = 0.0f;
		std::size_t resumeIndex = 0;
		std::size_t groundingNodeIndex = static_cast<std::size_t>(-1);
		bool prepared = false;

		void SnapshotNodeState()
		{
			for (std::size_t index = 0; index < nodes.size(); ++index)
			{
				transactionSmoothedTargets[index] = nodes[index].smoothedTarget;
				transactionAimStates[index] = nodes[index].aimState;
				transactionRotationStates[index] = nodes[index].rotationState;
				transactionSmoothedTargetValid[index] = nodes[index].smoothedTargetValid ? 1u : 0u;
			}
		}

		void RollbackNodeState()
		{
			for (auto& node : nodes) node.checkpointValid = false;
			for (std::size_t index = 0; index < nodes.size(); ++index)
			{
				nodes[index].smoothedTarget = transactionSmoothedTargets[index];
				nodes[index].aimState = transactionAimStates[index];
				nodes[index].rotationState = transactionRotationStates[index];
				nodes[index].smoothedTargetValid = transactionSmoothedTargetValid[index] != 0u;
			}
		}

		glm::mat4 BoneMatrix(int bone)
		{
			VansBoneTransform value;
			value.translation = workspace.GetComponentPosition(bone);
			value.rotation = workspace.GetComponentRotation(bone);
			value.scale = workspace.GetComponentScale(bone);
			return VansPoseMath::Compose(value);
		}

		bool CheckpointMatrix(const std::string& id, int bone, glm::mat4& matrix, bool published) const
		{
			const auto found = checkpointNodes.find(id);
			if (found == checkpointNodes.end()) return false;
			const auto& node = nodes[found->second];
			if (published ? !node.checkpointValid : !node.checkpointCaptured) return false;
			const auto index = std::find(node.checkpointBones.begin(), node.checkpointBones.end(), bone);
			if (index == node.checkpointBones.end()) return false;
			matrix = (published ? node.checkpointPublished : node.checkpointWorking)[index - node.checkpointBones.begin()];
			return true;
		}

		void PublishCheckpoints()
		{
			for (auto& node : nodes)
			{
				node.checkpointValid = node.checkpointCaptured;
				if (node.checkpointValid) node.checkpointPublished = node.checkpointWorking;
			}
		}

		bool ResolveGoal(const VansGraphGoalDefinition& definition,
		                 VansProceduralGoal& outGoal, std::string* diagnostic = nullptr)
		{
			outGoal = {};
			auto fail = [&](const std::string& message) { if (diagnostic) *diagnostic = message; return false; };
			outGoal.positionModel = definition.fixedPositionModel;
			outGoal.rotationModel = glm::normalize(definition.fixedRotationModel);
			outGoal.positionWeight = definition.fixedPositionWeight;
			outGoal.rotationWeight = definition.fixedRotationWeight;
			float weight = 1.0f;
			if (!definition.weightParameter.empty() && (!ReadFloat(parameters, definition.weightParameter, weight) || !std::isfinite(weight)))
				return fail("Target weight parameter is unavailable or non-finite");
			if (weight <= 0.0f || (outGoal.positionWeight <= 0.0f && outGoal.rotationWeight <= 0.0f))
			{outGoal.positionWeight=0;outGoal.rotationWeight=0;outGoal.valid=true;return true;}
			if (definition.source == VansGraphGoalSource::Parameters)
			{
				if (!parameters.readVector3 || !parameters.readVector3(
					parameters.context, definition.positionParameter, outGoal.positionModel))
					return fail("Target position parameter is unavailable");
				if (!definition.rotationParameter.empty() && (!parameters.readQuaternion ||
					!parameters.readQuaternion(parameters.context, definition.rotationParameter, outGoal.rotationModel)))
					return fail("Target rotation parameter is unavailable");
			}
			else if (definition.source == VansGraphGoalSource::Binding)
			{
				const auto found = targetsById.find(definition.binding);
				if (found == targetsById.end()) return fail("Target binding is missing: " + definition.binding);
				const auto& target = *found->second;
				if (!target.valid) return fail(target.diagnostic.empty() ? "Target is unavailable" : target.diagnostic);
				if (target.space == VansAnimationTargetSpace::Pose)
				{
					if (!workspace.IsValidBone(target.sourceBoneIndex)) return fail("Target source bone is missing");
					glm::mat4 source(1.0f);
					if (target.poseCheckpoint.empty()) source = BoneMatrix(target.sourceBoneIndex);
					else if (!CheckpointMatrix(target.poseCheckpoint, target.sourceBoneIndex, source, false))
						return fail("Target checkpoint is missing or not upstream: " + target.poseCheckpoint);
					VansBoneTransform value;
					if (!VansPoseMath::TryDecompose(source * target.sourceLocal, value))
						return fail("Target local transform cannot be decomposed");
					outGoal.positionModel = value.translation;
					outGoal.rotationModel = value.rotation;
				}
				else
				{
					VansBoneTransform owner;
					if (!VansPoseMath::TryDecompose(input.ownerWorld, owner) ||
						glm::any(glm::lessThanEqual(owner.scale, glm::vec3(kEpsilon))))
						return fail("Target owner requires a nonsingular positive scale");
					outGoal.positionModel = glm::vec3(glm::inverse(input.ownerWorld) * glm::vec4(target.positionWorld, 1.0f));
					outGoal.rotationModel = glm::normalize(glm::inverse(owner.rotation) * target.rotationWorld);
				}
				outGoal.positionWeight *= target.positionWeight;
				outGoal.rotationWeight *= target.rotationWeight;
			}
			outGoal.positionWeight = std::clamp(outGoal.positionWeight * weight, 0.0f, 1.0f);
			outGoal.rotationWeight = std::clamp(outGoal.rotationWeight * weight, 0.0f, 1.0f);
			outGoal.valid = Finite(outGoal.positionModel) && Finite(outGoal.rotationModel);
			return outGoal.valid;
		}

		bool TargetWouldFeedBack(const VansGraphGoalDefinition& definition, std::size_t activeIndex) const
		{
			if (definition.source != VansGraphGoalSource::Binding) return false;
			const auto target = targetsById.find(definition.binding);
			if (target == targetsById.end() || target->second->space != VansAnimationTargetSpace::Pose ||
				!target->second->poseCheckpoint.empty()) return false;
			// 当前或后续消费者若修改目标祖先，最终目标就不再是求解时的目标。
			for (std::size_t index = activeIndex; index < activeNodeIds.size(); ++index)
			{
				const auto found = nodeIndexById.find(activeNodeIds[index]);
				if (found == nodeIndexById.end()) continue;
				for (int writeBone : nodes[found->second].writeBones)
						for (int bone = target->second->sourceBoneIndex; bone >= 0; bone = rig->skeleton->bones[bone].parentIndex)
							if (bone == writeBone) return true;
			}
			return false;
		}

		bool ResolveAimTarget(const CompiledRuntimeNode& node, VansAimConstraintTarget& target)
		{
			if (node.aimSettings.mode == VansAimConstraintMode::LookAtPoint)
			{
				VansProceduralGoal point;
				if (!ResolveGoal(node.goal, point)) return false;
				target = {point.positionModel, point.positionWeight, point.valid};
				return true;
			}
			float weight = 1.0f;
			if (!node.directionWeightParameter.empty() &&
				!ReadFloat(parameters, node.directionWeightParameter, weight)) return false;
			if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f) return false;
			target.weight = weight;
			if (weight <= kEpsilon) { target.valid = true; return true; }
			if (!parameters.readVector3 || !parameters.readVector3(parameters.context,
				node.directionParameter, target.valueModel) || !Finite(target.valueModel)) return false;
			if (node.directionIsWorldSpace)
			{
				const float determinant = glm::determinant(input.ownerWorld);
				if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12f) return false;
				target.valueModel = glm::vec3(glm::inverse(input.ownerWorld) * glm::vec4(target.valueModel, 0.0f));
			}
			const float length = glm::length(target.valueModel);
			if (!Finite(target.valueModel) || length <= kEpsilon) return false;
			target.valueModel /= length;
			if (node.aimSettings.mode == VansAimConstraintMode::PitchOffset)
			{
				// 俯仰不参与水平转身；先投到 Rig 的竖直平面，避免左右急转污染平滑状态。
				const float sine = std::clamp(glm::dot(target.valueModel, rig->modelUp), -1.0f, 1.0f);
				target.valueModel = rig->modelForward * std::sqrt(std::max(0.0f, 1.0f-sine*sine)) + rig->modelUp * sine;
			}
			target.valid = true;
			return true;
		}

		bool ExecuteRange(std::size_t begin, std::size_t end, std::string& error)
		{
			for (std::size_t activeIndex = begin; activeIndex < end; ++activeIndex)
			{
				const auto lookup = nodeIndexById.find(activeNodeIds[activeIndex]);
				if (lookup == nodeIndexById.end())
				{
					error = "Active procedural graph node was not compiled";
					return false;
				}
				CompiledRuntimeNode& node = nodes[lookup->second];
				if (node.kind == RuntimeNodeKind::Checkpoint)
				{
					for (std::size_t i = 0; i < node.checkpointBones.size(); ++i)
						node.checkpointWorking[i] = BoneMatrix(node.checkpointBones[i]);
					node.checkpointCaptured = true;
					debugRecords[node.debugRecordOffset].result.status = VansProceduralSolverStatus::Solved;
					continue;
				}
				if (node.kind == RuntimeNodeKind::Goal)
				{
					goalDefinitions[node.goalIndex] = &node.goal;
					debugRecords[node.debugRecordOffset].result.status = VansProceduralSolverStatus::NoEffect;
					continue;
				}
				if (node.kind == RuntimeNodeKind::Aim)
				{
					VansAimConstraintTarget target;
					if (!ResolveAimTarget(node, target) || target.weight * node.aimSettings.weight <= kEpsilon)
					{
						node.aimState = {};
						node.smoothedTargetValid = false;
						debugRecords[node.debugRecordOffset].result.status =
							VansProceduralSolverStatus::NoEffect;
						continue;
					}
					VansAimConstraintTarget solveTarget = target;
					if (!node.smoothedTargetValid && deltaTime > 0.0f)
					{
						node.smoothedTarget = target;
						node.smoothedTargetValid = true;
					}
					else if (node.smoothedTargetValid)
					{
						const float alpha = HalfLifeAlpha(deltaTime, node.targetHalfLife);
						node.smoothedTarget.valueModel = glm::mix(
							node.smoothedTarget.valueModel, target.valueModel, alpha);
						if (node.aimSettings.mode != VansAimConstraintMode::LookAtPoint &&
							glm::length(node.smoothedTarget.valueModel) <= kEpsilon)
							node.smoothedTarget.valueModel = target.valueModel;
						node.smoothedTarget.weight = target.weight;
					}
					if (node.smoothedTargetValid) solveTarget = node.smoothedTarget;
					const VansProceduralSolverResult result = VansAimConstraintSolver::Solve(
						workspace, *rig, rig->chains[static_cast<std::size_t>(node.chainIndices.front())],
						solveTarget, deltaTime, node.aimState, node.aimSettings, node.pivotBoneIndex);
					auto& debugGoal = debugRecords[node.debugRecordOffset].goal;
					debugGoal.valid = true;
					debugGoal.positionWeight = solveTarget.weight;
					debugGoal.positionModel = solveTarget.valueModel;
					if (node.aimSettings.mode != VansAimConstraintMode::LookAtPoint)
						debugGoal.positionModel = workspace.GetComponentPosition(rig->chains[
							static_cast<std::size_t>(node.chainIndices.front())].boneIndices.back()) + glm::normalize(solveTarget.valueModel);
					debugRecords[node.debugRecordOffset].result = result;
					if (result.status == VansProceduralSolverStatus::InvalidInput)
					{
						error = "Aim Constraint returned InvalidInput";
						return false;
					}
					continue;
				}
				if (node.kind == RuntimeNodeKind::Grounding)
					continue;
				if (node.kind == RuntimeNodeKind::RotationDistribution)
				{
					auto& debug = debugRecords[node.debugRecordOffset];
					VansProceduralGoal goal;
					const auto* definition = goalDefinitions[node.goalIndex];
					if (!definition) debug.diagnostic = "Rotation Distribution requires an active upstream Goal";
					else if (TargetWouldFeedBack(*definition, activeIndex))
						debug.diagnostic = "Target depends on a bone written by this or a downstream constraint; use an upstream Pose Checkpoint";
					else ResolveGoal(*definition, goal, &debug.diagnostic);
					debug.goal = goal;
					if (!goal.valid) { node.rotationState = {}; debug.result.status = VansProceduralSolverStatus::NoEffect; continue; }
					debug.result = VansRotationDistributionSolver::Solve(workspace, *rig,
						rig->rotationDistributions[node.rotationProfileIndex], goal, node.rotationState);
					if (debug.result.status == VansProceduralSolverStatus::InvalidInput)
					{ error = "Rotation Distribution returned InvalidInput"; return false; }
					if (debug.result.status == VansProceduralSolverStatus::Clamped)
						debug.diagnostic = "Rotation limited by Rig joint constraints; the solved segment endpoint is preserved";
					continue;
				}
				for (std::size_t chainOffset = 0; chainOffset < node.chainIndices.size(); ++chainOffset)
				{
					const int chainIndex = node.chainIndices[chainOffset];
					const VansCompiledRigChain& chain = rig->chains[static_cast<std::size_t>(chainIndex)];
					VansProceduralGoal& goal = goals[static_cast<std::size_t>(chain.goalIndex)];
					VansProceduralDebugRecord& debug = debugRecords[node.debugRecordOffset + chainOffset];
					if (const auto* definition = goalDefinitions[chain.goalIndex])
					{
						if (TargetWouldFeedBack(*definition, activeIndex))
						{
							goal = {};
							debug.diagnostic = "Target depends on a bone written by this or a downstream IK; use an upstream Pose Checkpoint";
						}
						else ResolveGoal(*definition, goal, &debug.diagnostic);
					}
					debug.goal = goal;
					if (!goal.valid)
					{
						debug.result.status = VansProceduralSolverStatus::NoEffect;
						continue;
					}
					const VansProceduralSolverResult result = node.kind == RuntimeNodeKind::Limb
						? VansLimbIKSolver::Solve(workspace, *rig, chain, goal, node.limbSettings)
						: VansChainIKSolver::Solve(workspace, *rig, chain, goal, node.chainSettings);
					debug.result = result;
					if (result.status == VansProceduralSolverStatus::InvalidInput)
					{
						error = "IK node returned InvalidInput for Rig chain '" + chain.id + "'";
						return false;
					}
				}
			}
			return workspace.IsFinite();
		}
	};

	VansProceduralGraphRuntime::VansProceduralGraphRuntime() : m_Impl(std::make_unique<Impl>()) {}
	VansProceduralGraphRuntime::~VansProceduralGraphRuntime() = default;
	VansProceduralGraphRuntime::VansProceduralGraphRuntime(VansProceduralGraphRuntime&&) noexcept = default;
	VansProceduralGraphRuntime& VansProceduralGraphRuntime::operator=(VansProceduralGraphRuntime&&) noexcept = default;

	bool VansProceduralGraphRuntime::Configure(
		const VansAnimGraph& graph,
		const VansCompiledAnimationRig& rig,
		const VansGroundQueryProfileResolver& queryProfileResolver,
		std::string& error)
	{
		error.clear();
		m_Impl = std::make_unique<Impl>();
		m_Impl->rig = &rig;
		m_Impl->graph = &graph;
		std::vector<int> plan;
		if (!graph.BuildExecutionPlan(plan, error)) return false;
		std::unordered_map<int, int> maxPhaseByNode;
		std::size_t groundingCount = 0;
		int groundingNodeId = -1;
		std::unordered_set<int> groundedChainIndices;
		for (int nodeId : plan)
		{
			const VansAnimGraphNode* source = graph.GetNode(nodeId);
			if (!source) continue;
			int inheritedPhase = -1;
			for (const AnimGraphLink& link : graph.GetLinks())
				if (link.toNodeId == nodeId)
					inheritedPhase = std::max(inheritedPhase, maxPhaseByNode[link.fromNodeId]);
			CompiledRuntimeNode node;
			node.nodeId = nodeId;
			bool procedural = true;
			switch (source->GetType())
			{
			case AnimGraphNodeType::PoseCheckpoint:
			{
				node.kind = RuntimeNodeKind::Checkpoint;
				const auto& checkpoint = *static_cast<const AnimGraphPoseCheckpointNode*>(source);
				node.checkpointId = checkpoint.m_CheckpointId;
				if (node.checkpointId.empty() || checkpoint.m_Bones.empty() ||
					!m_Impl->checkpointNodes.emplace(node.checkpointId, m_Impl->nodes.size()).second)
				{ error = "Pose Checkpoint requires a unique id and explicit bones"; break; }
				for (const auto& name : checkpoint.m_Bones)
				{
					const auto bone = rig.skeleton->boneNameToIndex.find(name);
					if (bone == rig.skeleton->boneNameToIndex.end() ||
						std::find(node.checkpointBones.begin(), node.checkpointBones.end(), bone->second) != node.checkpointBones.end())
					{ error = "Pose Checkpoint references a missing or duplicate bone: " + name; break; }
					node.checkpointBones.push_back(bone->second);
				}
				node.checkpointWorking.resize(node.checkpointBones.size(), glm::mat4(1.0f));
				node.checkpointPublished.resize(node.checkpointBones.size(), glm::mat4(1.0f));
				break;
			}
			case AnimGraphNodeType::Goal:
			{
				node.kind = RuntimeNodeKind::Goal;
				node.goal = static_cast<const AnimGraphGoalNode*>(source)->m_Goal;
				node.goalIndex = rig.FindGoal(node.goal.goalId);
				if (node.goalIndex < 0) error = "Goal node references missing Rig goal '" + node.goal.goalId + "'";
				break;
			}
			case AnimGraphNodeType::AimConstraint:
			{
				node.kind = RuntimeNodeKind::Aim;
				const auto* aim = static_cast<const AnimGraphAimConstraintNode*>(source);
				const int chain = rig.FindChain(aim->m_ChainId);
				if (chain < 0 || rig.chains[static_cast<std::size_t>(chain)].solver != VansRigSolverKind::Aim)
					error = "Aim Constraint requires an Aim Rig chain '" + aim->m_ChainId + "'";
				node.chainIndices.push_back(chain);
				node.goal = aim->m_Target;
				node.aimSettings = aim->m_Settings;
				node.directionParameter = aim->m_DirectionParameter;
				node.directionWeightParameter = aim->m_DirectionWeightParameter;
				node.directionIsWorldSpace = aim->m_DirectionIsWorldSpace;
				if (!aim->m_PivotBone.empty() && error.empty())
				{
					const auto pivot = rig.skeleton->boneNameToIndex.find(aim->m_PivotBone);
					if (pivot == rig.skeleton->boneNameToIndex.end() || aim->m_Settings.mode != VansAimConstraintMode::PitchOffset)
						error = "Aim pivot requires an existing bone and PitchOffset mode";
					else
					{
						node.pivotBoneIndex = pivot->second;
						int ancestor = rig.skeleton->bones[rig.chains[chain].boneIndices.front()].parentIndex;
						while (ancestor >= 0 && ancestor != node.pivotBoneIndex) ancestor = rig.skeleton->bones[ancestor].parentIndex;
						if (ancestor < 0 || rig.chains[chain].boneIndices.size() != 1)
							error = "Aim pivot requires a strict ancestor of a single-bone chain";
					}
				}
				node.targetHalfLife = aim->m_TargetHalfLife;
				break;
			}
			case AnimGraphNodeType::Grounding:
			{
				node.kind = RuntimeNodeKind::Grounding;
				++groundingCount;
				groundingNodeId = nodeId;
				VansGroundingSettings settings = static_cast<const AnimGraphGroundingNode*>(source)->m_Settings;
				if (!queryProfileResolver
					|| !queryProfileResolver(settings.query.profile, settings.query.collisionMask, error))
				{
					if (error.empty()) error = "Grounding query profile could not be resolved";
					break;
				}
				VansCompiledGroundingSettings compiled;
				if (!VansCompileGroundingSettings(settings, rig, compiled, error)
					|| !node.grounding.Configure(rig, compiled, error)) break;
				for (int contactIndex : compiled.contactIndices)
					groundedChainIndices.insert(rig.contacts[static_cast<std::size_t>(contactIndex)].chainIndex);
				break;
			}
			case AnimGraphNodeType::LimbIK:
			{
				node.kind = RuntimeNodeKind::Limb;
				const auto* limb = static_cast<const AnimGraphLimbIKNode*>(source);
				node.limbSettings = limb->m_Settings;
				for (const std::string& id : limb->m_ChainIds)
				{
					const int chain = rig.FindChain(id);
					if (chain < 0 || rig.chains[static_cast<std::size_t>(chain)].solver != VansRigSolverKind::Limb)
					{
						error = "Limb IK requires a Limb Rig chain '" + id + "'";
						break;
					}
					node.chainIndices.push_back(chain);
				}
				break;
			}
			case AnimGraphNodeType::ChainIK:
			{
				node.kind = RuntimeNodeKind::Chain;
				const auto* chainNode = static_cast<const AnimGraphChainIKNode*>(source);
				node.chainSettings = chainNode->m_Settings;
				for (const std::string& id : chainNode->m_ChainIds)
				{
					const int chain = rig.FindChain(id);
					if (chain < 0 || (rig.chains[static_cast<std::size_t>(chain)].solver != VansRigSolverKind::CCD
						&& rig.chains[static_cast<std::size_t>(chain)].solver != VansRigSolverKind::FABRIK))
					{
						error = "Chain IK requires a CCD/FABRIK Rig chain '" + id + "'";
						break;
					}
					node.chainIndices.push_back(chain);
				}
				break;
			}
			case AnimGraphNodeType::RotationDistribution:
			{
				node.kind = RuntimeNodeKind::RotationDistribution;
				const auto& id = static_cast<const AnimGraphRotationDistributionNode*>(source)->m_RotationProfileId;
				node.rotationProfileIndex = rig.FindRotationDistribution(id);
				if (node.rotationProfileIndex < 0)
				{ error = "Rotation Distribution references a missing Rig profile: " + id; break; }
				const auto& profile = rig.rotationDistributions[node.rotationProfileIndex];
				node.goalIndex = profile.goalIndex;
				node.writeBones = {profile.baseBoneIndex, profile.tipBoneIndex};
				for (const auto& recipient : profile.recipients) node.writeBones.push_back(recipient.boneIndex);
				for (const auto& previous : m_Impl->nodes)
				{
					if (previous.kind != RuntimeNodeKind::RotationDistribution) continue;
					const auto& other = rig.rotationDistributions[previous.rotationProfileIndex];
					const auto ancestor = [&](int a, int b)
					{
						for (; b >= 0; b = rig.skeleton->bones[b].parentIndex) if (a == b) return true;
						return false;
					};
					if (ancestor(profile.baseBoneIndex, other.baseBoneIndex) || ancestor(other.baseBoneIndex, profile.baseBoneIndex))
					{ error = "Rotation Distribution nodes cannot overlap base subtrees: " + id; break; }
				}
				break;
			}
			default:
				procedural = false;
				break;
			}
			if (!error.empty()) return false;
			const int nodePhase = procedural && node.kind != RuntimeNodeKind::Checkpoint ? Phase(node.kind) : inheritedPhase;
			if (procedural && nodePhase < inheritedPhase)
			{
				error = "Target Procedural Graph contains a phase-reversing connection at node "
					+ std::to_string(nodeId);
				return false;
			}
			maxPhaseByNode[nodeId] = std::max(inheritedPhase, nodePhase);
			if (procedural)
			{
				for (int chainIndex : node.chainIndices)
					for (int bone : rig.chains[chainIndex].boneIndices) node.writeBones.push_back(bone);
				node.debugRecordOffset = m_Impl->debugRecords.size();
				const std::size_t debugRecordCount =
					node.kind == RuntimeNodeKind::Limb || node.kind == RuntimeNodeKind::Chain
						? node.chainIndices.size() : 1;
				for (std::size_t debugIndex = 0; debugIndex < debugRecordCount; ++debugIndex)
				{
					VansProceduralDebugRecord debug;
					debug.nodeId = node.nodeId;
					debug.kind = DebugKind(node.kind);
					debug.goalIndex = node.goalIndex;
					if (node.kind == RuntimeNodeKind::Aim)
					{
						debug.chainIndex = node.chainIndices.front();
						debug.goalIndex = rig.chains[static_cast<std::size_t>(debug.chainIndex)].goalIndex;
					}
					else if (node.kind == RuntimeNodeKind::Limb || node.kind == RuntimeNodeKind::Chain)
					{
						debug.chainIndex = node.chainIndices[debugIndex];
						debug.goalIndex = rig.chains[static_cast<std::size_t>(debug.chainIndex)].goalIndex;
					}
					m_Impl->debugRecords.push_back(debug);
				}
				m_Impl->nodeIndexById.emplace(nodeId, m_Impl->nodes.size());
				m_Impl->nodes.push_back(std::move(node));
			}
		}
		if (groundingCount > 1)
		{
			error = "Target Procedural Graph permits at most one Grounding node";
			return false;
		}
		for (int requiredChain : groundedChainIndices)
		{
			int consumerCount = 0;
			for (const CompiledRuntimeNode& node : m_Impl->nodes)
				if (node.kind == RuntimeNodeKind::Limb
					&& node.limbSettings.weight > kEpsilon
					&& std::find(node.chainIndices.begin(), node.chainIndices.end(), requiredChain) != node.chainIndices.end()
					&& IsDownstream(graph, groundingNodeId, node.nodeId)) ++consumerCount;
			if (consumerCount != 1)
			{
				error = "Grounding contact chain '" + rig.chains[static_cast<std::size_t>(requiredChain)].id
					+ "' must have exactly one enabled downstream Limb IK consumer";
				return false;
			}
		}
		m_Impl->groundedChainIndices = std::move(groundedChainIndices);
		m_Impl->goals.assign(rig.goals.size(), VansProceduralGoal{});
		m_Impl->goalDefinitions.assign(rig.goals.size(), nullptr);
		m_Impl->transactionSmoothedTargets.resize(m_Impl->nodes.size());
		m_Impl->transactionAimStates.resize(m_Impl->nodes.size());
		m_Impl->transactionRotationStates.resize(m_Impl->nodes.size());
		m_Impl->transactionSmoothedTargetValid.resize(m_Impl->nodes.size());
		return true;
	}

	void VansProceduralGraphRuntime::TransferStateForRigReplacement(const VansProceduralGraphRuntime& source)
	{
		if (!m_Impl || !source.m_Impl || !m_Impl->rig || !source.m_Impl->rig || source.m_Impl->prepared ||
			m_Impl->graph != source.m_Impl->graph || m_Impl->rig->skeletonSignature != source.m_Impl->rig->skeletonSignature) return;
		for (auto& node : m_Impl->nodes)
		{
			const auto found = source.m_Impl->nodeIndexById.find(node.nodeId);
			if (found == source.m_Impl->nodeIndexById.end()) continue;
			const auto& previous = source.m_Impl->nodes[found->second];
			if (previous.kind != node.kind) continue;
			if (node.kind == RuntimeNodeKind::Grounding) node.grounding.TransferStateForRigReplacement(previous.grounding);
			if (node.kind == RuntimeNodeKind::RotationDistribution)
			{
				const auto& a = m_Impl->rig->rotationDistributions[node.rotationProfileIndex];
				const auto& b = source.m_Impl->rig->rotationDistributions[previous.rotationProfileIndex];
				if (a.baseBoneIndex == b.baseBoneIndex && a.tipBoneIndex == b.tipBoneIndex)
					node.rotationState = previous.rotationState;
			}
			if (node.kind == RuntimeNodeKind::Aim)
			{
				const auto& a = m_Impl->rig->chains[node.chainIndices.front()];
				const auto& b = source.m_Impl->rig->chains[previous.chainIndices.front()];
				if (a.boneIndices == b.boneIndices && a.weights == b.weights &&
					a.forwardAxisLocal == b.forwardAxisLocal && a.upAxisLocal == b.upAxisLocal &&
					m_Impl->rig->modelUp == source.m_Impl->rig->modelUp && m_Impl->rig->modelForward == source.m_Impl->rig->modelForward)
				{
					node.aimState = previous.aimState;
					node.smoothedTarget = previous.smoothedTarget;
					node.smoothedTargetValid = previous.smoothedTargetValid;
				}
			}
		}
	}

	void VansProceduralGraphRuntime::Reset(std::uint64_t resetToken)
	{
		if (!m_Impl) return;
		m_Impl->prepared = false;
		m_Impl->activeNodeIds.clear();
		std::fill(m_Impl->goals.begin(), m_Impl->goals.end(), VansProceduralGoal{});
		for (VansProceduralDebugRecord& debug : m_Impl->debugRecords)
		{
			debug.goal = {};
			debug.result = {};
			debug.diagnostic.clear();
		}
		for (CompiledRuntimeNode& node : m_Impl->nodes)
		{
			node.smoothedTargetValid = false;
			node.aimState = {};
			node.rotationState = {};
			node.checkpointValid = node.checkpointCaptured = false;
			if (node.kind == RuntimeNodeKind::Grounding) node.grounding.Reset(resetToken);
		}
	}

	bool VansProceduralGraphRuntime::Prepare(
		float deltaTime,
		const std::vector<VansBoneTransform>& localPose,
		const std::vector<int>& activeNodeIds,
		const VansProceduralParameterAccessor& parameters,
		const VansAnimationExternalInputSnapshot& input,
		std::vector<VansWorldQueryRequest>& outRequests,
		std::vector<VansBoneTransform>& outCompletedPose,
		bool& outNeedsResolve,
		std::string& error)
	{
		error.clear();
		outRequests.clear();
		outCompletedPose.clear();
		outNeedsResolve = false;
		if (!m_Impl || !m_Impl->rig || m_Impl->prepared
			|| !std::isfinite(deltaTime) || deltaTime < 0.0f
			|| !m_Impl->workspace.Initialize(*m_Impl->rig->skeleton, localPose))
		{
			if (m_Impl) for (auto& node : m_Impl->nodes) node.checkpointValid = false;
			error = "Procedural graph received an invalid pose, Rig, or delta time";
			return false;
		}
		m_Impl->activeNodeIds.assign(activeNodeIds.begin(), activeNodeIds.end());
		m_Impl->parameters = parameters;
		const bool targetRosterChanged = m_Impl->input.targets.size() != input.targets.size() ||
			!std::equal(m_Impl->input.targets.begin(), m_Impl->input.targets.end(), input.targets.begin(),
				[](const auto& a,const auto& b){return a.id==b.id;});
		m_Impl->input = input;
		if (targetRosterChanged) m_Impl->targetsById.clear();
		for (const auto& target : m_Impl->input.targets) m_Impl->targetsById.insert_or_assign(target.id, &target);
		std::fill(m_Impl->goalDefinitions.begin(), m_Impl->goalDefinitions.end(), nullptr);
		for (auto& node : m_Impl->nodes) node.checkpointCaptured = false;
		m_Impl->deltaTime = deltaTime;
		m_Impl->SnapshotNodeState();
		for (auto& node : m_Impl->nodes)
			if (node.kind == RuntimeNodeKind::RotationDistribution &&
				std::find(activeNodeIds.begin(), activeNodeIds.end(), node.nodeId) == activeNodeIds.end()) node.rotationState = {};
		std::fill(m_Impl->goals.begin(), m_Impl->goals.end(), VansProceduralGoal{});
		for (VansProceduralDebugRecord& debug : m_Impl->debugRecords)
		{
			debug.goal = {};
			debug.result = {};
			debug.diagnostic.clear();
		}
		m_Impl->prepared = false;
		m_Impl->groundingNodeIndex = static_cast<std::size_t>(-1);
		m_Impl->resumeIndex = m_Impl->activeNodeIds.size();
		std::size_t groundingActiveIndex = m_Impl->activeNodeIds.size();
		for (std::size_t index = 0; index < m_Impl->activeNodeIds.size(); ++index)
		{
			const auto found = m_Impl->nodeIndexById.find(m_Impl->activeNodeIds[index]);
			if (found != m_Impl->nodeIndexById.end()
				&& m_Impl->nodes[found->second].kind == RuntimeNodeKind::Grounding)
			{
				groundingActiveIndex = index;
				m_Impl->groundingNodeIndex = found->second;
				break;
			}
		}
		m_Impl->transactionStart = m_Impl->workspace;
		if (!m_Impl->ExecuteRange(0, groundingActiveIndex, error))
		{
			m_Impl->workspace = m_Impl->transactionStart;
			m_Impl->RollbackNodeState();
			return false;
		}
		if (groundingActiveIndex < m_Impl->activeNodeIds.size())
		{
			CompiledRuntimeNode& grounding = m_Impl->nodes[m_Impl->groundingNodeIndex];
			if (!grounding.grounding.Prepare(m_Impl->workspace, input, outRequests))
			{
				m_Impl->workspace = m_Impl->transactionStart;
				m_Impl->RollbackNodeState();
				error = "Grounding Prepare failed";
				return false;
			}
			m_Impl->resumeIndex = groundingActiveIndex + 1;
			m_Impl->prepared = true;
			outNeedsResolve = true;
			return true;
		}
		if (!m_Impl->ExecuteRange(groundingActiveIndex, m_Impl->activeNodeIds.size(), error))
		{
			m_Impl->workspace = m_Impl->transactionStart;
			m_Impl->RollbackNodeState();
			return false;
		}
		m_Impl->PublishCheckpoints();
		outCompletedPose = m_Impl->workspace.GetLocalPose();
		return true;
	}

	bool VansProceduralGraphRuntime::Resolve(
		const std::vector<VansWorldQueryResult>& results,
		std::vector<VansBoneTransform>& outCompletedPose,
		std::string& error)
	{
		error.clear();
		outCompletedPose.clear();
		if (!m_Impl || !m_Impl->prepared || m_Impl->groundingNodeIndex >= m_Impl->nodes.size())
		{
			error = "Procedural graph has no prepared Grounding query batch";
			return false;
		}
		VansProceduralSolverResult groundingResult;
		CompiledRuntimeNode& grounding = m_Impl->nodes[m_Impl->groundingNodeIndex];
		if (!grounding.grounding.Resolve(m_Impl->deltaTime, m_Impl->workspace,
			m_Impl->input, results, m_Impl->goals, groundingResult)
			|| !m_Impl->ExecuteRange(m_Impl->resumeIndex, m_Impl->activeNodeIds.size(), error))
		{
			grounding.grounding.RollbackResolvedState();
			m_Impl->workspace = m_Impl->transactionStart;
			m_Impl->RollbackNodeState();
			m_Impl->prepared = false;
			if (error.empty()) error = "Grounding Resolve or downstream IK failed transactionally";
			return false;
		}
		for (CompiledRuntimeNode& node : m_Impl->nodes)
		{
			if (node.kind != RuntimeNodeKind::Limb) continue;
			for (std::size_t chainOffset = 0; chainOffset < node.chainIndices.size(); ++chainOffset)
			{
				const int chainIndex = node.chainIndices[chainOffset];
				if (m_Impl->groundedChainIndices.find(chainIndex)
					== m_Impl->groundedChainIndices.end()) continue;
				const VansProceduralDebugRecord& debug =
					m_Impl->debugRecords[node.debugRecordOffset + chainOffset];
				if (!debug.goal.valid || debug.goal.positionWeight <= kEpsilon) continue;
				if (m_Impl->deltaTime > 0.0f)
					grounding.grounding.ReportLimbSolve(chainIndex, debug.result);
				if (debug.result.status != VansProceduralSolverStatus::Solved)
				{
					groundingResult.status = VansProceduralSolverStatus::Clamped;
					groundingResult.limitReason |= debug.result.limitReason;
				}
			}
		}
		grounding.grounding.CommitResolvedState();
		m_Impl->debugRecords[grounding.debugRecordOffset].result = groundingResult;
		m_Impl->PublishCheckpoints();
		outCompletedPose = m_Impl->workspace.GetLocalPose();
		m_Impl->prepared = false;
		return true;
	}

	bool VansProceduralGraphRuntime::TryGetCheckpointTransform(
		const std::string& id, int bone, glm::mat4& transform) const
	{
		return m_Impl && m_Impl->CheckpointMatrix(id, bone, transform, true);
	}

	bool VansProceduralGraphRuntime::HasCheckpointBone(const std::string& id, int bone) const
	{
		if (!m_Impl) return false;
		const auto found = m_Impl->checkpointNodes.find(id);
		if (found == m_Impl->checkpointNodes.end()) return false;
		const auto& bones = m_Impl->nodes[found->second].checkpointBones;
		return std::find(bones.begin(), bones.end(), bone) != bones.end();
	}

	bool VansProceduralGraphRuntime::IsConfigured() const
	{
		return m_Impl && m_Impl->rig;
	}

	bool VansProceduralGraphRuntime::HasPreparedQueries() const
	{
		return m_Impl && m_Impl->prepared;
	}

	const VansCompiledAnimationRig* VansProceduralGraphRuntime::GetRig() const
	{
		return m_Impl ? m_Impl->rig : nullptr;
	}

	const std::vector<VansProceduralDebugRecord>&
	VansProceduralGraphRuntime::GetDebugRecords() const
	{
		static const std::vector<VansProceduralDebugRecord> empty;
		return m_Impl ? m_Impl->debugRecords : empty;
	}
}
