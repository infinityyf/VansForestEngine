#include "VansProceduralGraphRuntime.h"

#include "Grounding/VansGroundingRuntime.h"
#include "Solvers/VansAimConstraintSolver.h"
#include "Solvers/VansChainIKSolver.h"
#include "Solvers/VansLimbIKSolver.h"
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

		enum class RuntimeNodeKind { Goal, Aim, Grounding, Limb, Chain };

		int Phase(RuntimeNodeKind kind)
		{
			switch (kind)
			{
			case RuntimeNodeKind::Goal: return 0;
			case RuntimeNodeKind::Aim: return 1;
			case RuntimeNodeKind::Grounding: return 2;
			case RuntimeNodeKind::Limb:
			case RuntimeNodeKind::Chain: return 3;
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
			VansGraphGoalDefinition goal;
			VansAimConstraintSettings aimSettings;
			float targetHalfLife = 0.0f;
			VansLimbIKSettings limbSettings;
			VansChainIKSettings chainSettings;
			VansGroundingRuntime grounding;
			std::size_t debugRecordOffset = 0;
			bool smoothedTargetValid = false;
			VansProceduralGoal smoothedTarget;
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
		std::vector<CompiledRuntimeNode> nodes;
		std::unordered_map<int, std::size_t> nodeIndexById;
		std::vector<int> activeNodeIds;
		std::vector<VansProceduralGoal> goals;
		std::vector<VansProceduralDebugRecord> debugRecords;
		std::unordered_set<int> groundedChainIndices;
		VansPoseWorkspace workspace;
		VansPoseWorkspace transactionStart;
		std::vector<VansProceduralGoal> transactionSmoothedTargets;
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
				transactionSmoothedTargetValid[index] = nodes[index].smoothedTargetValid ? 1u : 0u;
			}
		}

		void RollbackNodeState()
		{
			for (std::size_t index = 0; index < nodes.size(); ++index)
			{
				nodes[index].smoothedTarget = transactionSmoothedTargets[index];
				nodes[index].smoothedTargetValid = transactionSmoothedTargetValid[index] != 0u;
			}
		}

		bool ResolveGoal(const VansGraphGoalDefinition& definition,
		                 VansProceduralGoal& outGoal) const
		{
			outGoal = {};
			outGoal.positionModel = definition.fixedPositionModel;
			outGoal.rotationModel = glm::normalize(definition.fixedRotationModel);
			outGoal.positionWeight = definition.fixedPositionWeight;
			outGoal.rotationWeight = definition.fixedRotationWeight;
			if (definition.source == VansGraphGoalSource::Parameters)
			{
				if (!parameters.readVector3 || !parameters.readVector3(
					parameters.context, definition.positionParameter, outGoal.positionModel))
					return false;
				if (!definition.rotationParameter.empty()
					&& (!parameters.readQuaternion || !parameters.readQuaternion(
						parameters.context, definition.rotationParameter, outGoal.rotationModel)))
					return false;
				float weight = 1.0f;
				if (!definition.weightParameter.empty() && !ReadFloat(parameters, definition.weightParameter, weight))
					return false;
				outGoal.positionWeight *= weight;
				outGoal.rotationWeight *= weight;
			}
			else if (definition.source == VansGraphGoalSource::Binding)
			{
				const auto found = std::find_if(input.targets.begin(), input.targets.end(),
					[&](const VansResolvedAnimationTarget& target)
					{ return target.id == definition.binding && target.valid; });
				if (found == input.targets.end()) return false;
				VansBoneTransform owner;
				if (!VansPoseMath::TryDecompose(input.ownerWorld, owner)) return false;
				glm::vec3 model = glm::inverse(owner.rotation) * (found->positionWorld - owner.translation);
				for (int axis = 0; axis < 3; ++axis)
					model[axis] = std::abs(owner.scale[axis]) > kEpsilon ? model[axis] / owner.scale[axis] : 0.0f;
				outGoal.positionModel = model;
				outGoal.rotationModel = glm::normalize(glm::inverse(owner.rotation) * found->rotationWorld);
				outGoal.positionWeight *= found->positionWeight;
				outGoal.rotationWeight *= found->rotationWeight;
			}
			outGoal.positionWeight = std::clamp(outGoal.positionWeight, 0.0f, 1.0f);
			outGoal.rotationWeight = std::clamp(outGoal.rotationWeight, 0.0f, 1.0f);
			outGoal.valid = Finite(outGoal.positionModel) && Finite(outGoal.rotationModel);
			return outGoal.valid;
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
				if (node.kind == RuntimeNodeKind::Goal)
				{
					VansProceduralGoal goal;
					VansProceduralDebugRecord& debug = debugRecords[node.debugRecordOffset];
					if (ResolveGoal(node.goal, goal))
					{
						goals[static_cast<std::size_t>(node.goalIndex)] = goal;
						debug.goal = goal;
						debug.result.status = VansProceduralSolverStatus::Solved;
					}
					else
					{
						goals[static_cast<std::size_t>(node.goalIndex)] = {};
						debug.result.status = VansProceduralSolverStatus::NoEffect;
					}
					continue;
				}
				if (node.kind == RuntimeNodeKind::Aim)
				{
					VansProceduralGoal target;
					if (!ResolveGoal(node.goal, target))
					{
						debugRecords[node.debugRecordOffset].result.status =
							VansProceduralSolverStatus::NoEffect;
						continue;
					}
					VansProceduralGoal solveTarget = target;
					if (!node.smoothedTargetValid && deltaTime > 0.0f)
					{
						node.smoothedTarget = target;
						node.smoothedTargetValid = true;
					}
					else if (node.smoothedTargetValid)
					{
						const float alpha = HalfLifeAlpha(deltaTime, node.targetHalfLife);
						node.smoothedTarget.positionModel = glm::mix(
							node.smoothedTarget.positionModel, target.positionModel, alpha);
						glm::quat rotation = target.rotationModel;
						if (glm::dot(node.smoothedTarget.rotationModel, rotation) < 0.0f) rotation = -rotation;
						node.smoothedTarget.rotationModel = glm::normalize(glm::slerp(
							node.smoothedTarget.rotationModel, rotation, alpha));
						node.smoothedTarget.positionWeight = target.positionWeight;
						node.smoothedTarget.rotationWeight = target.rotationWeight;
					}
					if (node.smoothedTargetValid) solveTarget = node.smoothedTarget;
					const VansProceduralSolverResult result = VansAimConstraintSolver::Solve(
						workspace, *rig, rig->chains[static_cast<std::size_t>(node.chainIndices.front())],
						solveTarget, deltaTime, node.aimSettings);
					debugRecords[node.debugRecordOffset].goal = solveTarget;
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
				for (std::size_t chainOffset = 0; chainOffset < node.chainIndices.size(); ++chainOffset)
				{
					const int chainIndex = node.chainIndices[chainOffset];
					const VansCompiledRigChain& chain = rig->chains[static_cast<std::size_t>(chainIndex)];
					const VansProceduralGoal& goal = goals[static_cast<std::size_t>(chain.goalIndex)];
					VansProceduralDebugRecord& debug = debugRecords[node.debugRecordOffset + chainOffset];
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
			default:
				procedural = false;
				break;
			}
			if (!error.empty()) return false;
			const int nodePhase = procedural ? Phase(node.kind) : inheritedPhase;
			if (procedural && nodePhase < inheritedPhase)
			{
				error = "Target Procedural Graph contains a phase-reversing connection at node "
					+ std::to_string(nodeId);
				return false;
			}
			maxPhaseByNode[nodeId] = std::max(inheritedPhase, nodePhase);
			if (procedural)
			{
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
		m_Impl->transactionSmoothedTargets.resize(m_Impl->nodes.size());
		m_Impl->transactionSmoothedTargetValid.resize(m_Impl->nodes.size());
		return true;
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
		}
		for (CompiledRuntimeNode& node : m_Impl->nodes)
		{
			node.smoothedTargetValid = false;
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
			error = "Procedural graph received an invalid pose, Rig, or delta time";
			return false;
		}
		m_Impl->activeNodeIds.assign(activeNodeIds.begin(), activeNodeIds.end());
		m_Impl->parameters = parameters;
		m_Impl->input = input;
		m_Impl->deltaTime = deltaTime;
		m_Impl->SnapshotNodeState();
		std::fill(m_Impl->goals.begin(), m_Impl->goals.end(), VansProceduralGoal{});
		for (VansProceduralDebugRecord& debug : m_Impl->debugRecords)
		{
			debug.goal = {};
			debug.result = {};
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
		outCompletedPose = m_Impl->workspace.GetLocalPose();
		m_Impl->prepared = false;
		return true;
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
