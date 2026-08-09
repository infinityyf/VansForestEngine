#include "VansAnimatorIO.h"

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;
using namespace VansGraphics;

namespace
{
	bool ReadAnimatorJson(const std::string& filePath, json& root)
	{
		std::string error;
		if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
		{
			VANS_LOG_WARN("[VansAnimatorIO] Cannot read .vanimator file: " << filePath << " (" << error << ")");
			return false;
		}
		return true;
	}

	bool ContainsForbiddenGenerationField(const json& value, std::string& field)
	{
		if (value.is_object())
		{
			for (const auto& item : value.items())
			{
				if (item.key() == "version" || item.key() == "schemaVersion" || item.key() == "formatVersion")
				{
					field = item.key();
					return true;
				}
				if (ContainsForbiddenGenerationField(item.value(), field))
					return true;
			}
		}
		else if (value.is_array())
		{
			for (const json& item : value)
			{
				if (ContainsForbiddenGenerationField(item, field))
					return true;
			}
		}
		return false;
	}

	bool HasOnlyFields(const json& value,
	                   std::initializer_list<const char*> allowed,
	                   std::string& unknown)
	{
		if (!value.is_object())
			return false;
		std::unordered_set<std::string> names;
		for (const char* field : allowed)
			names.insert(field);
		for (const auto& item : value.items())
		{
			if (names.find(item.key()) == names.end())
			{
				unknown = item.key();
				return false;
			}
		}
		return true;
	}

	std::string ParamTypeToString(AnimatorParamType type)
	{
		switch (type)
		{
		case AnimatorParamType::Float: return "float";
		case AnimatorParamType::Bool: return "bool";
		case AnimatorParamType::Int: return "int";
		case AnimatorParamType::Trigger: return "trigger";
		case AnimatorParamType::Vector3: return "vector3";
		case AnimatorParamType::Quaternion: return "quaternion";
		}
		return "float";
	}

	bool TryParseParamType(const std::string& value, AnimatorParamType& type)
	{
		if (value == "float") { type = AnimatorParamType::Float; return true; }
		if (value == "bool") { type = AnimatorParamType::Bool; return true; }
		if (value == "int") { type = AnimatorParamType::Int; return true; }
		if (value == "trigger") { type = AnimatorParamType::Trigger; return true; }
		if (value == "vector3") { type = AnimatorParamType::Vector3; return true; }
		if (value == "quaternion") { type = AnimatorParamType::Quaternion; return true; }
		return false;
	}

	const char* GraphRoleToString(AnimatorGraphAsset::Role role)
	{
		return role == AnimatorGraphAsset::Role::Pose ? "pose" : "targetPostProcess";
	}

	bool TryParseGraphRole(const std::string& value, AnimatorGraphAsset::Role& role)
	{
		if (value == "pose")
		{
			role = AnimatorGraphAsset::Role::Pose;
			return true;
		}
		if (value == "targetPostProcess")
		{
			role = AnimatorGraphAsset::Role::TargetPostProcess;
			return true;
		}
		return false;
	}

	const AnimGraphStateMachineNode* FindPrimaryStateMachine(const VansAnimGraph& graph)
	{
		std::vector<int> executionPlan;
		std::string error;
		if (!graph.BuildExecutionPlan(executionPlan, error))
			return nullptr;
		for (int nodeId : executionPlan)
		{
			const VansAnimGraphNode* node = graph.GetNode(nodeId);
			if (node && node->GetType() == AnimGraphNodeType::StateMachine)
				return static_cast<const AnimGraphStateMachineNode*>(node);
		}
		return nullptr;
	}

	template<typename Enum>
	bool ParseEnum(const std::string& value,
	               std::initializer_list<std::pair<const char*, Enum>> values,
	               Enum& result)
	{
		for (const auto& [name, candidate] : values)
		{
			if (value == name)
			{
				result = candidate;
				return true;
			}
		}
		return false;
	}

	const char* ToString(VansAnimationLayerKind value)
	{
		return value == VansAnimationLayerKind::Base ? "base" : "overlay";
	}

	const char* ToString(VansLayerBlendMode value)
	{
		return value == VansLayerBlendMode::Override ? "override" : "additive";
	}

	const char* ToString(VansRotationBlendSpace value)
	{
		return value == VansRotationBlendSpace::Local ? "local" : "mesh";
	}

	const char* ToString(VansAdditiveReferenceMode value)
	{
		switch (value)
		{
		case VansAdditiveReferenceMode::BindPose: return "bindPose";
		case VansAdditiveReferenceMode::FirstFrame: return "firstFrame";
		case VansAdditiveReferenceMode::ClipTime: return "clipTime";
		case VansAdditiveReferenceMode::ReferenceClip: return "referenceClip";
		}
		return "bindPose";
	}

	const char* ToString(VansLayerRootMotionMode value)
	{
		switch (value)
		{
		case VansLayerRootMotionMode::Ignore: return "ignore";
		case VansLayerRootMotionMode::Base: return "base";
		case VansLayerRootMotionMode::BlendByRootWeight: return "blendByRootWeight";
		case VansLayerRootMotionMode::Override: return "override";
		}
		return "ignore";
	}

	const char* ToString(VansLayerCurveMode value)
	{
		switch (value)
		{
		case VansLayerCurveMode::BaseOnly: return "baseOnly";
		case VansLayerCurveMode::Override: return "override";
		case VansLayerCurveMode::Blend: return "blend";
		case VansLayerCurveMode::Normalize: return "normalize";
		case VansLayerCurveMode::Min: return "min";
		case VansLayerCurveMode::Max: return "max";
		}
		return "blend";
	}

	const char* ToString(VansLayerEventMode value)
	{
		switch (value)
		{
		case VansLayerEventMode::Ignore: return "ignore";
		case VansLayerEventMode::ActiveOnly: return "activeOnly";
		case VansLayerEventMode::Always: return "always";
		}
		return "activeOnly";
	}

	const char* ToString(VansLayerNodeTrackMode value)
	{
		return value == VansLayerNodeTrackMode::Ignore ? "ignore" : "override";
	}

	const char* ToString(VansLayerSyncMode value)
	{
		switch (value)
		{
		case VansLayerSyncMode::Independent: return "independent";
		case VansLayerSyncMode::NormalizedTime: return "normalizedTime";
		case VansLayerSyncMode::MarkerSync: return "markerSync";
		case VansLayerSyncMode::SyncedGraph: return "syncedGraph";
		}
		return "independent";
	}

	const char* ToString(VansSlotConcurrency value)
	{
		switch (value)
		{
		case VansSlotConcurrency::Replace: return "replace";
		case VansSlotConcurrency::Queue: return "queue";
		case VansSlotConcurrency::Reject: return "reject";
		}
		return "replace";
	}

	json SerializeLayer(const VansAnimationLayerDefinition& layer)
	{
		return {
			{ "id", layer.id },
			{ "name", layer.name },
			{ "graphId", layer.graphId },
			{ "kind", ToString(layer.kind) },
			{ "mask", { { "guid", layer.maskGuid }, { "pathHint", layer.maskPathHint } } },
			{ "blend", {
				{ "mode", ToString(layer.blendMode) },
				{ "rotationSpace", ToString(layer.rotationSpace) },
				{ "additiveReference", {
					{ "mode", ToString(layer.additiveReference) },
					{ "clip", layer.referenceClipName },
					{ "time", layer.referenceTime }
				} }
			} },
			{ "weight", {
				{ "source", layer.useWeightParameter ? "parameter" : "constant" },
				{ "value", layer.fixedWeight },
				{ "parameter", layer.weightParameter },
				{ "smoothingTime", layer.weightSmoothingTime }
			} },
			{ "outputs", {
				{ "rootMotion", ToString(layer.rootMotion) },
				{ "curves", ToString(layer.curves) },
				{ "events", ToString(layer.events) },
				{ "nodeTracks", ToString(layer.nodeTracks) },
				{ "eventWeightThreshold", layer.eventWeightThreshold }
			} },
			{ "sync", {
				{ "mode", ToString(layer.sync) },
				{ "leaderLayerId", layer.syncLeaderLayerId }
			} },
			{ "enabled", layer.enabled },
			{ "updateWhenWeightIsZero", layer.updateWhenWeightIsZero }
		};
	}

	bool DeserializeLayer(const json& source,
	                      VansAnimationLayerDefinition& layer,
	                      std::string& error)
	{
		std::string unknown;
		if (!HasOnlyFields(source,
			{ "id", "name", "graphId", "kind", "mask", "blend", "weight", "outputs", "sync", "enabled", "updateWhenWeightIsZero" },
			unknown))
		{
			error = "Invalid or unknown Layer field: " + unknown;
			return false;
		}
		if (!source.contains("id") || !source["id"].is_string()
			|| !source.contains("name") || !source["name"].is_string()
			|| !source.contains("graphId") || !source["graphId"].is_string()
			|| !source.contains("kind") || !source["kind"].is_string()
			|| !source.contains("mask") || !source["mask"].is_object()
			|| !source.contains("blend") || !source["blend"].is_object()
			|| !source.contains("weight") || !source["weight"].is_object()
			|| !source.contains("outputs") || !source["outputs"].is_object()
			|| !source.contains("sync") || !source["sync"].is_object()
			|| !source.contains("enabled") || !source["enabled"].is_boolean()
			|| !source.contains("updateWhenWeightIsZero") || !source["updateWhenWeightIsZero"].is_boolean())
		{
			error = "Layer is missing required canonical fields";
			return false;
		}

		const json& mask = source["mask"];
		const json& blend = source["blend"];
		const json& weight = source["weight"];
		const json& outputs = source["outputs"];
		const json& sync = source["sync"];
		if (!HasOnlyFields(mask, { "guid", "pathHint" }, unknown)
			|| !HasOnlyFields(blend, { "mode", "rotationSpace", "additiveReference" }, unknown)
			|| !HasOnlyFields(weight, { "source", "value", "parameter", "smoothingTime" }, unknown)
			|| !HasOnlyFields(outputs, { "rootMotion", "curves", "events", "nodeTracks", "eventWeightThreshold" }, unknown)
			|| !HasOnlyFields(sync, { "mode", "leaderLayerId" }, unknown))
		{
			error = "Invalid or unknown nested Layer field: " + unknown;
			return false;
		}
		if (!blend.contains("additiveReference") || !blend["additiveReference"].is_object())
		{
			error = "Layer blend is missing additiveReference";
			return false;
		}
		const json& additive = blend["additiveReference"];
		if (!HasOnlyFields(additive, { "mode", "clip", "time" }, unknown))
		{
			error = "Invalid additiveReference field: " + unknown;
			return false;
		}

		try
		{
			layer.id = source["id"].get<std::string>();
			layer.name = source["name"].get<std::string>();
			layer.graphId = source["graphId"].get<std::string>();
			layer.maskGuid = mask.at("guid").get<std::string>();
			layer.maskPathHint = mask.at("pathHint").get<std::string>();
			layer.referenceClipName = additive.at("clip").get<std::string>();
			layer.referenceTime = additive.at("time").get<float>();
			layer.fixedWeight = weight.at("value").get<float>();
			layer.weightParameter = weight.at("parameter").get<std::string>();
			layer.weightSmoothingTime = weight.at("smoothingTime").get<float>();
			layer.syncLeaderLayerId = sync.at("leaderLayerId").get<std::string>();
			layer.eventWeightThreshold = outputs.at("eventWeightThreshold").get<float>();
			layer.enabled = source["enabled"].get<bool>();
			layer.updateWhenWeightIsZero = source["updateWhenWeightIsZero"].get<bool>();

			if (!ParseEnum(source["kind"].get<std::string>(),
				{ { "base", VansAnimationLayerKind::Base }, { "overlay", VansAnimationLayerKind::Overlay } }, layer.kind)
				|| !ParseEnum(blend.at("mode").get<std::string>(),
					{ { "override", VansLayerBlendMode::Override }, { "additive", VansLayerBlendMode::Additive } }, layer.blendMode)
				|| !ParseEnum(blend.at("rotationSpace").get<std::string>(),
					{ { "local", VansRotationBlendSpace::Local }, { "mesh", VansRotationBlendSpace::Mesh } }, layer.rotationSpace)
				|| !ParseEnum(additive.at("mode").get<std::string>(),
					{ { "bindPose", VansAdditiveReferenceMode::BindPose }, { "firstFrame", VansAdditiveReferenceMode::FirstFrame },
					  { "clipTime", VansAdditiveReferenceMode::ClipTime }, { "referenceClip", VansAdditiveReferenceMode::ReferenceClip } }, layer.additiveReference)
				|| !ParseEnum(outputs.at("rootMotion").get<std::string>(),
					{ { "ignore", VansLayerRootMotionMode::Ignore }, { "base", VansLayerRootMotionMode::Base },
					  { "blendByRootWeight", VansLayerRootMotionMode::BlendByRootWeight }, { "override", VansLayerRootMotionMode::Override } }, layer.rootMotion)
				|| !ParseEnum(outputs.at("curves").get<std::string>(),
					{ { "baseOnly", VansLayerCurveMode::BaseOnly }, { "override", VansLayerCurveMode::Override },
					  { "blend", VansLayerCurveMode::Blend }, { "normalize", VansLayerCurveMode::Normalize },
					  { "min", VansLayerCurveMode::Min }, { "max", VansLayerCurveMode::Max } }, layer.curves)
				|| !ParseEnum(outputs.at("events").get<std::string>(),
					{ { "ignore", VansLayerEventMode::Ignore }, { "activeOnly", VansLayerEventMode::ActiveOnly }, { "always", VansLayerEventMode::Always } }, layer.events)
				|| !ParseEnum(outputs.at("nodeTracks").get<std::string>(),
					{ { "ignore", VansLayerNodeTrackMode::Ignore }, { "override", VansLayerNodeTrackMode::Override } }, layer.nodeTracks)
				|| !ParseEnum(sync.at("mode").get<std::string>(),
					{ { "independent", VansLayerSyncMode::Independent }, { "normalizedTime", VansLayerSyncMode::NormalizedTime },
					  { "markerSync", VansLayerSyncMode::MarkerSync }, { "syncedGraph", VansLayerSyncMode::SyncedGraph } }, layer.sync))
			{
				error = "Layer contains an unknown enum value";
				return false;
			}

			const std::string weightSource = weight.at("source").get<std::string>();
			if (weightSource != "constant" && weightSource != "parameter")
			{
				error = "Layer weight source must be constant or parameter";
				return false;
			}
			layer.useWeightParameter = weightSource == "parameter";
		}
		catch (const json::exception& exception)
		{
			error = std::string("Invalid Layer value: ") + exception.what();
			return false;
		}
		return true;
	}

	bool ValidateAssetDefinition(const AnimatorAssetData& data, std::string& error)
	{
		error.clear();
		if (data.name.empty())
		{
			error = "Animator name cannot be empty";
			return false;
		}
		if (!data.editor.previewModelGuid.empty())
		{
			Vans::VansAssetGuid previewModelGuid;
			if (!Vans::VansAssetGuid::TryParse(data.editor.previewModelGuid, previewModelGuid))
			{
				error = "Animator preview model requires a valid asset GUID";
				return false;
			}
		}

		std::unordered_map<std::string, AnimatorParamType> parameters;
		for (const AnimatorParameter& parameter : data.parameters)
		{
			if (parameter.name.empty() || !parameters.emplace(parameter.name, parameter.type).second)
			{
				error = "Animator parameter names must be non-empty and unique";
				return false;
			}
			const bool finiteDefault = parameter.type == AnimatorParamType::Float
				? std::isfinite(parameter.floatVal)
				: parameter.type == AnimatorParamType::Vector3
					? std::isfinite(parameter.vec3Val.x) && std::isfinite(parameter.vec3Val.y)
						&& std::isfinite(parameter.vec3Val.z)
					: parameter.type == AnimatorParamType::Quaternion
						? std::isfinite(parameter.quatVal.x) && std::isfinite(parameter.quatVal.y)
							&& std::isfinite(parameter.quatVal.z) && std::isfinite(parameter.quatVal.w)
						: true;
			if (!finiteDefault)
			{
				error = "Animator parameter '" + parameter.name + "' has a non-finite default value";
				return false;
			}
		}
		auto hasParameter = [&](const std::string& name, AnimatorParamType type, bool optional = false)
		{
			if (name.empty()) return optional;
			const auto found = parameters.find(name);
			return found != parameters.end() && found->second == type;
		};
		auto finiteVec3 = [](const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		auto finiteQuat = [](const glm::quat& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z) && std::isfinite(value.w);
		};
		auto requiresReferenceBone = [](IKCoordinateSpace space)
		{
			return space == IKCoordinateSpace::Bone || space == IKCoordinateSpace::ParentBone;
		};

		std::unordered_set<std::string> clipNames;
		for (const AnimatorClipRef& clip : data.clipRefs)
		{
			Vans::VansAssetGuid guid;
			if (clip.name.empty() || !Vans::VansAssetGuid::TryParse(clip.assetGuid, guid)
				|| !clipNames.insert(clip.name).second)
			{
				error = "Animator Clip names must be non-empty and unique, and every Clip requires a valid asset GUID";
				return false;
			}
		}

		std::unordered_set<std::string> graphIds;
		std::unordered_map<std::string, AnimatorGraphAsset::Role> graphRoles;
		std::size_t targetPostProcessGraphCount = 0;
		for (const AnimatorGraphAsset& graph : data.graphs)
		{
			if (graph.id.empty() || graph.name.empty() || !graph.graph || !graphIds.insert(graph.id).second)
			{
				error = "Animator Graph ids/names must be non-empty and unique, and every Graph needs a definition";
				return false;
			}
			graphRoles.emplace(graph.id, graph.role);
			json graphJson;
			graph.graph->SerializeToJsonObject(graphJson);
			if (!VansAnimGraph::DeserializeFromJsonObject(graphJson))
			{
				error = "Animator Graph '" + graph.name + "' is invalid";
				return false;
			}

			std::vector<int> executionPlan;
			std::string executionError;
			if (!graph.graph->BuildExecutionPlan(executionPlan, executionError))
			{
				error = "Animator Graph '" + graph.name + "' failed compilation: " + executionError;
				return false;
			}
			std::size_t targetInputCount = 0;
			bool targetInputReachable = false;
			for (const auto& [nodeId, node] : graph.graph->GetNodes())
			{
				if (!node)
					continue;
				if (node->GetType() == AnimGraphNodeType::Clip)
				{
					const auto* clipNode = static_cast<const AnimGraphClipNode*>(node.get());
					if (clipNames.find(clipNode->m_ClipName) == clipNames.end()
						|| !std::isfinite(clipNode->m_Speed))
					{
						error = "Graph '" + graph.name + "' contains a Clip node with an invalid Clip binding or speed";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::StateMachine)
				{
					const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node.get());
					std::unordered_set<std::string> states;
					for (const AnimatorState& state : stateMachine->m_States)
					{
						if (state.name.empty() || !states.insert(state.name).second
							|| clipNames.find(state.clipName) == clipNames.end()
							|| !std::isfinite(state.speed) || !std::isfinite(state.startTime)
							|| !std::isfinite(state.endTime) || state.startTime < 0.0f
							|| (state.endTime >= 0.0f && state.endTime < state.startTime))
						{
							error = "State Machine in Graph '" + graph.name
								+ "' contains an invalid State definition";
							return false;
						}
					}
					if (states.empty() || states.find(stateMachine->m_DefaultStateName) == states.end())
					{
						error = "State Machine in Graph '" + graph.name
							+ "' requires a non-empty State set and valid default State";
						return false;
					}
					for (const AnimatorTransition& transition : stateMachine->m_Transitions)
					{
						if ((transition.fromState != "*" && states.find(transition.fromState) == states.end())
							|| states.find(transition.toState) == states.end()
							|| !std::isfinite(transition.blendDuration) || transition.blendDuration < 0.0f
							|| !std::isfinite(transition.exitTime)
							|| transition.exitTime < 0.0f || transition.exitTime > 1.0f)
						{
							error = "State Machine in Graph '" + graph.name
								+ "' contains an invalid Transition";
							return false;
						}
						for (const TransitionCondition& condition : transition.conditions)
						{
							const auto parameter = parameters.find(condition.paramName);
							if (parameter == parameters.end()
								|| parameter->second == AnimatorParamType::Vector3
								|| parameter->second == AnimatorParamType::Quaternion
								|| ((parameter->second == AnimatorParamType::Bool
									|| parameter->second == AnimatorParamType::Trigger)
									&& condition.op != CompareOp::Equal && condition.op != CompareOp::NotEqual))
							{
								error = "State Machine in Graph '" + graph.name
									+ "' contains an invalid Transition condition";
								return false;
							}
						}
					}
				}
				else if (node->GetType() == AnimGraphNodeType::Blend)
				{
					const auto* blend = static_cast<const AnimGraphBlendNode*>(node.get());
					if (!std::isfinite(blend->m_FixedAlpha) || blend->m_FixedAlpha < 0.0f
						|| blend->m_FixedAlpha > 1.0f
						|| (blend->m_UseParam && !hasParameter(blend->m_ParamName, AnimatorParamType::Float)))
					{
						error = "Blend node in Graph '" + graph.name + "' has an invalid alpha source";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::Blend1D)
				{
					const auto* blend = static_cast<const AnimGraphBlend1DNode*>(node.get());
					if (!hasParameter(blend->m_ParamName, AnimatorParamType::Float)
						|| blend->m_Thresholds.empty()
						|| !std::is_sorted(blend->m_Thresholds.begin(), blend->m_Thresholds.end())
						|| std::any_of(blend->m_Thresholds.begin(), blend->m_Thresholds.end(),
							[](float value) { return !std::isfinite(value); }))
					{
						error = "Blend1D node in Graph '" + graph.name + "' requires a float parameter and sorted finite thresholds";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::IfCondition)
				{
					const auto* condition = static_cast<const AnimGraphIfConditionNode*>(node.get());
					const auto parameter = parameters.find(condition->m_ParamName);
					if (parameter == parameters.end()
						|| parameter->second == AnimatorParamType::Vector3
						|| parameter->second == AnimatorParamType::Quaternion
						|| ((parameter->second == AnimatorParamType::Bool || parameter->second == AnimatorParamType::Trigger)
							&& condition->m_CompareOp != CompareOp::Equal
							&& condition->m_CompareOp != CompareOp::NotEqual)
						|| !std::isfinite(condition->m_FloatVal))
					{
						error = "If Condition node in Graph '" + graph.name + "' has an invalid parameter or comparison";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::Switch)
				{
					const auto* switchNode = static_cast<const AnimGraphSwitchNode*>(node.get());
					if (!hasParameter(switchNode->m_ParamName, AnimatorParamType::Int)
						|| switchNode->m_CaseCount < 1)
					{
						error = "Switch node in Graph '" + graph.name + "' requires an int parameter and at least one case";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::AdditiveBlend)
				{
					const auto* blend = static_cast<const AnimGraphAdditiveBlendNode*>(node.get());
					if (!std::isfinite(blend->m_FixedWeight) || blend->m_FixedWeight < 0.0f
						|| blend->m_FixedWeight > 1.0f
						|| (blend->m_UseParam && !hasParameter(blend->m_ParamName, AnimatorParamType::Float)))
					{
						error = "Additive Blend node in Graph '" + graph.name + "' has an invalid weight source";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::SpeedScale)
				{
					const auto* speed = static_cast<const AnimGraphSpeedScaleNode*>(node.get());
					if (!std::isfinite(speed->m_FixedSpeed)
						|| (speed->m_UseParam && !hasParameter(speed->m_ParamName, AnimatorParamType::Float)))
					{
						error = "Speed Scale node in Graph '" + graph.name + "' has an invalid speed source";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::IK)
				{
					const auto* ik = static_cast<const AnimGraphIKNode*>(node.get());
					const IKChainDefinition& chain = ik->m_Chain;
					std::unordered_set<std::string> boneNames;
					std::size_t effectorCount = 0;
					bool validBones = chain.bones.size() >= 2;
					for (const IKBoneLink& bone : chain.bones)
					{
						validBones = validBones && !bone.boneName.empty() && boneNames.insert(bone.boneName).second
							&& std::isfinite(bone.stiffnessWeight) && bone.stiffnessWeight >= 0.0f && bone.stiffnessWeight <= 1.0f
							&& finiteVec3(bone.constraint.localXAxis) && finiteVec3(bone.constraint.localYAxis)
							&& finiteVec3(bone.constraint.localZAxis) && std::isfinite(bone.constraint.minAngleX)
							&& std::isfinite(bone.constraint.maxAngleX) && bone.constraint.minAngleX <= bone.constraint.maxAngleX
							&& std::isfinite(bone.constraint.minAngleY) && std::isfinite(bone.constraint.maxAngleY)
							&& bone.constraint.minAngleY <= bone.constraint.maxAngleY
							&& std::isfinite(bone.constraint.minAngleZ) && std::isfinite(bone.constraint.maxAngleZ)
							&& bone.constraint.minAngleZ <= bone.constraint.maxAngleZ
							&& std::isfinite(bone.constraint.coneAngleDeg) && bone.constraint.coneAngleDeg >= 0.0f
							&& bone.constraint.coneAngleDeg <= 180.0f && std::isfinite(bone.constraint.stiffness)
							&& bone.constraint.stiffness >= 0.0f && bone.constraint.stiffness <= 1.0f;
						if (bone.isEffector) ++effectorCount;
					}
					const bool validTarget = ik->m_UseFixedTarget
						? finiteVec3(ik->m_FixedTargetPos) && finiteQuat(ik->m_FixedTargetRot)
							&& std::isfinite(ik->m_FixedWeight) && ik->m_FixedWeight >= 0.0f && ik->m_FixedWeight <= 1.0f
						: hasParameter(ik->m_TargetPosParamName, AnimatorParamType::Vector3)
							&& hasParameter(ik->m_TargetRotParamName, AnimatorParamType::Quaternion, !chain.enableRotationTarget)
							&& hasParameter(ik->m_WeightParamName, AnimatorParamType::Float, true);
					if (chain.chainName.empty() || !validBones || effectorCount != 1 || !chain.bones.back().isEffector
						|| chain.maxIterations <= 0 || !std::isfinite(chain.positionTolerance) || chain.positionTolerance <= 0.0f
						|| !std::isfinite(chain.rotationTolerance) || chain.rotationTolerance <= 0.0f
						|| !finiteVec3(chain.poleVector) || !std::isfinite(chain.poleWeight)
						|| chain.poleWeight < 0.0f || chain.poleWeight > 1.0f
						|| !std::isfinite(chain.rotationWeight) || chain.rotationWeight < 0.0f || chain.rotationWeight > 1.0f
						|| !std::isfinite(chain.startStretchRatio) || !std::isfinite(chain.maxStretchScale)
						|| chain.startStretchRatio < 0.0f || chain.maxStretchScale < 1.0f
						|| chain.startStretchRatio > chain.maxStretchScale || !validTarget
						|| (requiresReferenceBone(ik->m_TargetPositionSpace) && ik->m_TargetReferenceBoneName.empty())
						|| (requiresReferenceBone(chain.poleSpace) && chain.poleWeight > 0.0f && chain.poleReferenceBoneName.empty()))
					{
						error = "IK node in Graph '" + graph.name + "' has an invalid chain, target, or constraint";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::TwoBoneIK)
				{
					const auto* ik = static_cast<const AnimGraphTwoBoneIKNode*>(node.get());
					const bool validTarget = ik->m_UseFixedTarget
						? finiteVec3(ik->m_FixedTargetPos) && finiteQuat(ik->m_FixedTargetRot)
							&& std::isfinite(ik->m_FixedWeight) && ik->m_FixedWeight >= 0.0f && ik->m_FixedWeight <= 1.0f
						: hasParameter(ik->m_TargetPosParamName, AnimatorParamType::Vector3)
							&& hasParameter(ik->m_TargetRotParamName, AnimatorParamType::Quaternion, !ik->m_EnableRotationTarget)
							&& hasParameter(ik->m_WeightParamName, AnimatorParamType::Float, true);
					if (ik->m_RootBoneName.empty() || ik->m_MidBoneName.empty() || ik->m_TipBoneName.empty()
						|| ik->m_RootBoneName == ik->m_MidBoneName || ik->m_RootBoneName == ik->m_TipBoneName
						|| ik->m_MidBoneName == ik->m_TipBoneName || !validTarget
						|| !std::isfinite(ik->m_HingeMinAngle) || !std::isfinite(ik->m_HingeMaxAngle)
						|| ik->m_HingeMinAngle > ik->m_HingeMaxAngle || !std::isfinite(ik->m_ConeAngle)
						|| ik->m_ConeAngle < 0.0f || ik->m_ConeAngle > 180.0f || !finiteVec3(ik->m_PoleVector)
						|| !std::isfinite(ik->m_PoleWeight) || ik->m_PoleWeight < 0.0f || ik->m_PoleWeight > 1.0f
						|| !std::isfinite(ik->m_RotationWeight) || ik->m_RotationWeight < 0.0f || ik->m_RotationWeight > 1.0f
						|| !std::isfinite(ik->m_StartStretchRatio) || !std::isfinite(ik->m_MaxStretchScale)
						|| ik->m_StartStretchRatio < 0.0f || ik->m_MaxStretchScale < 1.0f
						|| ik->m_StartStretchRatio > ik->m_MaxStretchScale
						|| (requiresReferenceBone(ik->m_TargetPositionSpace) && ik->m_TargetReferenceBoneName.empty())
						|| (ik->m_UsePoleVector && requiresReferenceBone(ik->m_PoleSpace) && ik->m_PoleReferenceBoneName.empty()))
					{
						error = "Two Bone IK node in Graph '" + graph.name + "' has an invalid chain or target";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::LookAt)
				{
					const auto* lookAt = static_cast<const AnimGraphLookAtNode*>(node.get());
					std::unordered_set<std::string> boneNames;
					const bool validTarget = lookAt->m_UseFixedTarget
						? finiteVec3(lookAt->m_FixedTargetPos) && std::isfinite(lookAt->m_FixedWeight)
							&& lookAt->m_FixedWeight >= 0.0f && lookAt->m_FixedWeight <= 1.0f
						: hasParameter(lookAt->m_TargetPosParamName, AnimatorParamType::Vector3)
							&& hasParameter(lookAt->m_WeightParamName, AnimatorParamType::Float, true);
					if (lookAt->m_BoneNames.empty() || lookAt->m_BoneWeights.size() != lookAt->m_BoneNames.size()
						|| std::any_of(lookAt->m_BoneNames.begin(), lookAt->m_BoneNames.end(),
							[&](const std::string& name) { return name.empty() || !boneNames.insert(name).second; })
						|| std::any_of(lookAt->m_BoneWeights.begin(), lookAt->m_BoneWeights.end(),
							[](float weight) { return !std::isfinite(weight) || weight < 0.0f || weight > 1.0f; })
						|| !std::isfinite(lookAt->m_MaxAnglePerBoneDeg) || lookAt->m_MaxAnglePerBoneDeg < 0.0f
						|| lookAt->m_MaxAnglePerBoneDeg > 180.0f || !finiteVec3(lookAt->m_ForwardAxis)
						|| !finiteVec3(lookAt->m_WorldForward) || !finiteVec3(lookAt->m_ModelUp)
						|| !std::isfinite(lookAt->m_UpWeight) || lookAt->m_UpWeight < 0.0f || lookAt->m_UpWeight > 1.0f
						|| !validTarget || (requiresReferenceBone(lookAt->m_TargetPositionSpace)
							&& lookAt->m_TargetReferenceBoneName.empty()))
					{
						error = "Look At node in Graph '" + graph.name + "' has an invalid chain or target";
						return false;
					}
				}
				else if (node->GetType() == AnimGraphNodeType::FootPlacement)
				{
					const FootPlacementSettings& settings =
						static_cast<const AnimGraphFootPlacementNode*>(node.get())->m_Settings;
					const float numericValues[] = { settings.probeOriginHeight, settings.probeLength,
						settings.footHalfLength, settings.footHalfWidth, settings.ankleHeight,
						settings.fullContactHeight, settings.contactFadeHeight, settings.maxStepUp,
						settings.maxStepDown, settings.maxSlopeDeg, settings.pelvisMaxDrop,
						settings.pelvisSmoothTime, settings.offsetSmoothTime, settings.normalSmoothTime,
						settings.weightSmoothTime, settings.globalWeightSmoothTime, settings.ikWeight,
						settings.rotationWeight, settings.maxLegExtensionRatio, settings.poleSmoothTime,
						settings.kneePoleModelWeight };
					const bool invalidNumeric = std::any_of(std::begin(numericValues), std::end(numericValues),
						[](float value) { return !std::isfinite(value) || value < 0.0f; });
					if (invalidNumeric || settings.contactFadeHeight < settings.fullContactHeight
						|| settings.maxSlopeDeg > 89.0f || settings.ikWeight > 1.0f
						|| settings.rotationWeight > 1.0f || settings.kneePoleModelWeight > 1.0f
						|| !finiteVec3(settings.kneePoleModelDir)
						|| !hasParameter(settings.airborneParameter, AnimatorParamType::Bool, true)
						|| settings.bones.pelvis.empty() || settings.bones.leftHip.empty()
						|| settings.bones.leftKnee.empty() || settings.bones.leftFoot.empty()
						|| settings.bones.rightHip.empty() || settings.bones.rightKnee.empty()
						|| settings.bones.rightFoot.empty())
					{
						error = "Foot Placement node in Graph '" + graph.name + "' has invalid settings";
						return false;
					}
				}
				if (node->GetType() == AnimGraphNodeType::TargetPoseInput)
				{
					++targetInputCount;
					targetInputReachable = std::find(executionPlan.begin(), executionPlan.end(), nodeId)
						!= executionPlan.end();
				}
			}
			if (graph.role == AnimatorGraphAsset::Role::Pose)
			{
				if (targetInputCount != 0)
				{
					error = "Pose Graph '" + graph.name + "' cannot contain Target Pose Input";
					return false;
				}
				for (const auto& [nodeId, node] : graph.graph->GetNodes())
				{
					if (!node) continue;
					if (node->GetType() == AnimGraphNodeType::IK
						|| node->GetType() == AnimGraphNodeType::TwoBoneIK
						|| node->GetType() == AnimGraphNodeType::LookAt
						|| node->GetType() == AnimGraphNodeType::FootPlacement)
					{
						error = "Pose Graph '" + graph.name
							+ "' cannot contain Target Post Process IK or Foot Placement nodes";
						return false;
					}
				}
			}
			else
			{
				++targetPostProcessGraphCount;
				if (targetInputCount != 1 || !targetInputReachable)
				{
					error = "Target Post Process Graph '" + graph.name
						+ "' requires exactly one reachable Target Pose Input";
					return false;
				}
				for (const auto& [nodeId, node] : graph.graph->GetNodes())
				{
					if (!node)
						continue;
					switch (node->GetType())
					{
					case AnimGraphNodeType::Entry:
					case AnimGraphNodeType::Clip:
					case AnimGraphNodeType::SpeedScale:
					case AnimGraphNodeType::StateMachine:
					case AnimGraphNodeType::MotionMatching:
					case AnimGraphNodeType::Slot:
						error = "Target Post Process Graph '" + graph.name
							+ "' contains a pose-source or playback node";
						return false;
					default:
						break;
					}
				}
			}
		}
		if (targetPostProcessGraphCount > 1)
		{
			error = "Animator can contain at most one Target Post Process Graph";
			return false;
		}

		if (data.layers.empty())
		{
			error = "Animator requires a Layer Stack";
			return false;
		}
		std::unordered_set<std::string> layerIds;
		std::size_t baseCount = 0;
		for (std::size_t index = 0; index < data.layers.size(); ++index)
		{
			const VansAnimationLayerDefinition& layer = data.layers[index];
			if (layer.id.empty() || layer.name.empty() || !layerIds.insert(layer.id).second)
			{
				error = "Animator Layer ids/names must be non-empty and Layer ids must be unique";
				return false;
			}
			if (graphIds.find(layer.graphId) == graphIds.end())
			{
				error = "Layer '" + layer.name + "' references an unknown Graph id";
				return false;
			}
			if (graphRoles.at(layer.graphId) != AnimatorGraphAsset::Role::Pose)
			{
				error = "Layer '" + layer.name + "' must reference a Pose Graph";
				return false;
			}
			if (layer.kind == VansAnimationLayerKind::Base)
			{
				++baseCount;
				if (index != 0)
				{
					error = "The Base Layer must be the first Layer";
					return false;
				}
			}
			else
			{
				Vans::VansAssetGuid maskGuid;
				if (!Vans::VansAssetGuid::TryParse(layer.maskGuid, maskGuid))
				{
					error = "Overlay Layer '" + layer.name + "' requires a valid Bone Mask asset GUID";
					return false;
				}
			}
			if (!std::isfinite(layer.fixedWeight) || !std::isfinite(layer.weightSmoothingTime)
				|| !std::isfinite(layer.referenceTime) || !std::isfinite(layer.eventWeightThreshold)
				|| layer.fixedWeight < 0.0f || layer.fixedWeight > 1.0f
				|| layer.weightSmoothingTime < 0.0f
				|| layer.eventWeightThreshold < 0.0f || layer.eventWeightThreshold > 1.0f)
			{
				error = "Layer '" + layer.name + "' contains an invalid numeric policy";
				return false;
			}
			if (layer.useWeightParameter)
			{
				auto parameter = parameters.find(layer.weightParameter);
				if (parameter == parameters.end() || parameter->second != AnimatorParamType::Float)
				{
					error = "Layer '" + layer.name + "' requires an existing float weight parameter";
					return false;
				}
			}
			if (layer.additiveReference == VansAdditiveReferenceMode::ReferenceClip
				&& clipNames.find(layer.referenceClipName) == clipNames.end())
			{
				error = "Layer '" + layer.name + "' references an unknown additive reference clip";
				return false;
			}
		}
		if (baseCount != 1)
		{
			error = "Animator Layer Stack requires exactly one Base Layer";
			return false;
		}
		for (std::size_t index = 0; index < data.layers.size(); ++index)
		{
			const VansAnimationLayerDefinition& layer = data.layers[index];
			if (layer.sync != VansLayerSyncMode::Independent
				&& (layer.syncLeaderLayerId.empty() || layerIds.find(layer.syncLeaderLayerId) == layerIds.end()
					|| layer.syncLeaderLayerId == layer.id))
			{
				error = "Layer '" + layer.name + "' has an invalid sync leader";
				return false;
			}
			if (layer.sync == VansLayerSyncMode::Independent)
				continue;
			std::size_t leaderIndex = data.layers.size();
			for (std::size_t candidate = 0; candidate < index; ++candidate)
				if (data.layers[candidate].id == layer.syncLeaderLayerId)
				{
					leaderIndex = candidate;
					break;
				}
			if (leaderIndex == data.layers.size())
			{
				error = "Layer '" + layer.name + "' requires an earlier sync leader";
				return false;
			}
			if (layer.sync == VansLayerSyncMode::SyncedGraph)
			{
				const VansAnimGraph* leaderGraph = data.FindGraph(data.layers[leaderIndex].graphId);
				const VansAnimGraph* followerGraph = data.FindGraph(layer.graphId);
				const AnimGraphStateMachineNode* leaderStateMachine = leaderGraph
					? FindPrimaryStateMachine(*leaderGraph) : nullptr;
				const AnimGraphStateMachineNode* followerStateMachine = followerGraph
					? FindPrimaryStateMachine(*followerGraph) : nullptr;
				if (!leaderStateMachine || !followerStateMachine)
				{
					error = "Synced Graph Layer '" + layer.name
						+ "' requires primary State Machine nodes on both Layers";
					return false;
				}
				std::unordered_set<std::string> leaderStates;
				std::unordered_set<std::string> followerStates;
				for (const AnimatorState& state : leaderStateMachine->m_States)
					leaderStates.insert(state.name);
				for (const AnimatorState& state : followerStateMachine->m_States)
					followerStates.insert(state.name);
				if (leaderStates != followerStates)
				{
					error = "Synced Graph Layer '" + layer.name
						+ "' must expose the same logical State names as its leader";
					return false;
				}
			}
		}

		std::unordered_set<std::string> slotIds;
		std::unordered_set<std::string> boundSlotNodes;
		for (const VansAnimationSlotDefinition& slot : data.slots)
		{
			if (slot.id.empty() || slot.name.empty() || !slotIds.insert(slot.id).second
				|| layerIds.find(slot.layerId) == layerIds.end() || slot.slotNodeId < 0
				|| !std::isfinite(slot.defaultBlendIn) || slot.defaultBlendIn < 0.0f
				|| !std::isfinite(slot.defaultBlendOut) || slot.defaultBlendOut < 0.0f)
			{
				error = "Animator contains an invalid Slot definition";
				return false;
			}
			const VansAnimationLayerDefinition* ownerLayer = nullptr;
			for (const VansAnimationLayerDefinition& layer : data.layers)
				if (layer.id == slot.layerId) { ownerLayer = &layer; break; }
			const VansAnimGraph* ownerGraph = ownerLayer ? data.FindGraph(ownerLayer->graphId) : nullptr;
			const VansAnimGraphNode* node = ownerGraph ? ownerGraph->GetNode(slot.slotNodeId) : nullptr;
			if (!node || node->GetType() != AnimGraphNodeType::Slot
				|| static_cast<const AnimGraphSlotNode*>(node)->m_SlotId != slot.id)
			{
				error = "Slot '" + slot.name + "' does not match its bound Slot Graph node";
				return false;
			}
			boundSlotNodes.insert(slot.layerId + "#" + std::to_string(slot.slotNodeId));
		}
		for (const VansAnimationLayerDefinition& layer : data.layers)
		{
			const VansAnimGraph* graph = data.FindGraph(layer.graphId);
			if (!graph)
				continue;
			for (const auto& [nodeId, node] : graph->GetNodes())
			{
				if (!node || node->GetType() != AnimGraphNodeType::Slot)
					continue;
				const auto* slotNode = static_cast<const AnimGraphSlotNode*>(node.get());
				if (slotNode->m_SlotId.empty()
					|| boundSlotNodes.find(layer.id + "#" + std::to_string(nodeId)) == boundSlotNodes.end())
				{
					error = "Slot node in Layer '" + layer.name
						+ "' requires exactly one matching Slot definition";
					return false;
				}
			}
		}
		return true;
	}

	bool BuildAnimatorJson(const AnimatorAssetData& data, json& root, std::string& error)
	{
		if (!ValidateAssetDefinition(data, error))
			return false;

		root = json::object();
		root["magic"] = VANIMATOR_MAGIC;
		root["name"] = data.name;

		json parameterArray = json::array();
		std::vector<const AnimatorParameter*> sortedParameters;
		for (const AnimatorParameter& parameter : data.parameters)
			sortedParameters.push_back(&parameter);
		std::sort(sortedParameters.begin(), sortedParameters.end(),
			[](const AnimatorParameter* lhs, const AnimatorParameter* rhs) { return lhs->name < rhs->name; });
		for (const AnimatorParameter* parameter : sortedParameters)
		{
			json value = { { "name", parameter->name }, { "type", ParamTypeToString(parameter->type) } };
			switch (parameter->type)
			{
			case AnimatorParamType::Float: value["default"] = parameter->floatVal; break;
			case AnimatorParamType::Bool: value["default"] = parameter->boolVal; break;
			case AnimatorParamType::Int: value["default"] = parameter->intVal; break;
			case AnimatorParamType::Trigger: break;
			case AnimatorParamType::Vector3: value["default"] = { parameter->vec3Val.x, parameter->vec3Val.y, parameter->vec3Val.z }; break;
			case AnimatorParamType::Quaternion: value["default"] = { parameter->quatVal.x, parameter->quatVal.y, parameter->quatVal.z, parameter->quatVal.w }; break;
			}
			parameterArray.push_back(std::move(value));
		}
		root["parameters"] = std::move(parameterArray);

		json clipArray = json::array();
		std::vector<AnimatorClipRef> sortedClips = data.clipRefs;
		std::sort(sortedClips.begin(), sortedClips.end(), [](const AnimatorClipRef& lhs, const AnimatorClipRef& rhs)
			{ return lhs.name != rhs.name ? lhs.name < rhs.name : lhs.assetGuid < rhs.assetGuid; });
		for (const AnimatorClipRef& clip : sortedClips)
			clipArray.push_back({ { "name", clip.name },
				{ "asset", { { "guid", clip.assetGuid }, { "pathHint", clip.pathHint } } } });
		root["clips"] = std::move(clipArray);

		json graphArray = json::array();
		for (const AnimatorGraphAsset& graph : data.graphs)
		{
			json graphDefinition;
			graph.graph->SerializeToJsonObject(graphDefinition);
			graphArray.push_back({ { "id", graph.id }, { "name", graph.name },
				{ "role", GraphRoleToString(graph.role) }, { "graph", std::move(graphDefinition) } });
		}
		root["graphs"] = std::move(graphArray);

		json layerArray = json::array();
		for (const VansAnimationLayerDefinition& layer : data.layers)
			layerArray.push_back(SerializeLayer(layer));
		root["layers"] = std::move(layerArray);

		json slotArray = json::array();
		for (const VansAnimationSlotDefinition& slot : data.slots)
		{
			slotArray.push_back({
				{ "id", slot.id }, { "name", slot.name }, { "layerId", slot.layerId },
				{ "nodeId", slot.slotNodeId }, { "concurrency", ToString(slot.concurrency) },
				{ "maxQueueDepth", slot.maxQueueDepth }, { "defaultBlendIn", slot.defaultBlendIn },
				{ "defaultBlendOut", slot.defaultBlendOut }, { "interruptible", slot.interruptible }
			});
		}
		root["slots"] = std::move(slotArray);
		root["editor"] = {
			{ "previewModel", {
				{ "guid", data.editor.previewModelGuid },
				{ "pathHint", data.editor.previewModelPathHint }
			} }
		};
		return true;
	}

	bool ValidateAnimatorRoot(const json& root, const std::string& filePath)
	{
		if (!root.is_object())
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Animator root must be an object: " << filePath);
			return false;
		}
		std::string forbidden;
		if (ContainsForbiddenGenerationField(root, forbidden))
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Forbidden generation field '" << forbidden
				<< "' in canonical animator: " << filePath);
			return false;
		}
		std::string unknown;
		if (!HasOnlyFields(root, { "magic", "name", "parameters", "clips", "graphs", "layers", "slots", "editor" }, unknown))
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Unknown root field '" << unknown << "' in canonical animator: " << filePath);
			return false;
		}
		if (!root.contains("magic") || !root["magic"].is_string() || root["magic"].get<std::string>() != VANIMATOR_MAGIC
			|| !root.contains("name") || !root["name"].is_string()
			|| !root.contains("parameters") || !root["parameters"].is_array()
			|| !root.contains("clips") || !root["clips"].is_array()
			|| !root.contains("graphs") || !root["graphs"].is_array()
			|| !root.contains("layers") || !root["layers"].is_array()
			|| !root.contains("slots") || !root["slots"].is_array())
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Animator is missing required canonical fields: " << filePath);
			return false;
		}
		if (root.contains("editor") && !root["editor"].is_object())
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Animator editor field must be an object: " << filePath);
			return false;
		}
		if (root.contains("editor"))
		{
			std::string editorUnknown;
			const json& editor = root["editor"];
			if (!HasOnlyFields(editor, { "previewModel" }, editorUnknown))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Unknown Animator editor field '"
					<< editorUnknown << "': " << filePath);
				return false;
			}
			if (editor.contains("previewModel"))
			{
				const json& model = editor["previewModel"];
				std::string modelUnknown;
				if (!model.is_object()
					|| !HasOnlyFields(model, { "guid", "pathHint" }, modelUnknown)
					|| !model.contains("guid") || !model["guid"].is_string()
					|| !model.contains("pathHint") || !model["pathHint"].is_string())
				{
					VANS_LOG_ERROR("[VansAnimatorIO] Invalid previewModel editor reference: " << filePath);
					return false;
				}
				const std::string guidText = model["guid"].get<std::string>();
				Vans::VansAssetGuid guid;
				if (!guidText.empty() && !Vans::VansAssetGuid::TryParse(guidText, guid))
				{
					VANS_LOG_ERROR("[VansAnimatorIO] Invalid previewModel GUID: " << filePath);
					return false;
				}
			}
		}
		return true;
	}
}

VansAnimGraph* AnimatorAssetData::FindGraph(const std::string& graphId)
{
	for (AnimatorGraphAsset& graph : graphs)
	{
		if (graph.id == graphId)
			return graph.graph.get();
	}
	return nullptr;
}

const VansAnimGraph* AnimatorAssetData::FindGraph(const std::string& graphId) const
{
	for (const AnimatorGraphAsset& graph : graphs)
	{
		if (graph.id == graphId)
			return graph.graph.get();
	}
	return nullptr;
}

VansAnimGraph* AnimatorAssetData::FindTargetPostProcessGraph()
{
	for (AnimatorGraphAsset& graph : graphs)
		if (graph.role == AnimatorGraphAsset::Role::TargetPostProcess)
			return graph.graph.get();
	return nullptr;
}

const VansAnimGraph* AnimatorAssetData::FindTargetPostProcessGraph() const
{
	for (const AnimatorGraphAsset& graph : graphs)
		if (graph.role == AnimatorGraphAsset::Role::TargetPostProcess)
			return graph.graph.get();
	return nullptr;
}

bool VansAnimatorIO::SerializeToJsonObject(const AnimatorAssetData& data,
	                                        AnimGraphJson& outJson,
	                                        std::string& error)
{
	return BuildAnimatorJson(data, outJson, error);
}

bool VansAnimatorIO::Save(const std::string& filePath,
	                       const AnimatorAssetData& data,
	                       std::string& error)
{
	json root;
	if (!BuildAnimatorJson(data, root, error))
		return false;
	if (!Vans::VansJsonFileStorage::WriteAtomic(filePath, root, error))
		return false;
	VANS_LOG("[VansAnimatorIO] Saved .vanimator: " << filePath);
	return true;
}

static bool DeserializeAnimatorRoot(
	const json& root,
	const std::string& filePath,
	AnimatorAssetData& outData)
{
	outData = AnimatorAssetData{};
	if (!ValidateAnimatorRoot(root, filePath))
		return false;

	AnimatorAssetData parsed;
	std::string error;
	try
	{
		parsed.name = root["name"].get<std::string>();
		for (const json& source : root["parameters"])
		{
			std::string unknown;
			if (!HasOnlyFields(source, { "name", "type", "default" }, unknown)
				|| !source.contains("name") || !source["name"].is_string()
				|| !source.contains("type") || !source["type"].is_string())
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid parameter entry in: " << filePath);
				return false;
			}
			AnimatorParameter parameter;
			parameter.name = source["name"].get<std::string>();
			if (!TryParseParamType(source["type"].get<std::string>(), parameter.type))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Unknown parameter type in: " << filePath);
				return false;
			}
			if (source.contains("default"))
			{
				switch (parameter.type)
				{
				case AnimatorParamType::Float: parameter.floatVal = source["default"].get<float>(); break;
				case AnimatorParamType::Bool: parameter.boolVal = source["default"].get<bool>(); break;
				case AnimatorParamType::Int: parameter.intVal = source["default"].get<int>(); break;
				case AnimatorParamType::Trigger: break;
				case AnimatorParamType::Vector3:
					if (!source["default"].is_array() || source["default"].size() != 3) return false;
					parameter.vec3Val = { source["default"][0].get<float>(), source["default"][1].get<float>(), source["default"][2].get<float>() };
					break;
				case AnimatorParamType::Quaternion:
					if (!source["default"].is_array() || source["default"].size() != 4) return false;
					parameter.quatVal = { source["default"][3].get<float>(), source["default"][0].get<float>(), source["default"][1].get<float>(), source["default"][2].get<float>() };
					break;
				}
			}
			parsed.parameters.push_back(parameter);
		}

		for (const json& source : root["clips"])
		{
			std::string unknown;
			if (!HasOnlyFields(source, { "name", "asset" }, unknown)
				|| !source.contains("name") || !source["name"].is_string()
				|| !source.contains("asset") || !source["asset"].is_object())
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid clip reference in: " << filePath);
				return false;
			}
			const json& asset = source["asset"];
			if (!HasOnlyFields(asset, { "guid", "pathHint" }, unknown)
				|| !asset.contains("guid") || !asset["guid"].is_string()
				|| !asset.contains("pathHint") || !asset["pathHint"].is_string())
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Clip asset reference in: " << filePath);
				return false;
			}
			parsed.clipRefs.push_back({ source["name"].get<std::string>(),
				asset["guid"].get<std::string>(), asset["pathHint"].get<std::string>() });
		}

		for (const json& source : root["graphs"])
		{
			std::string unknown;
			if (!HasOnlyFields(source, { "id", "name", "role", "graph" }, unknown)
				|| !source.contains("id") || !source["id"].is_string()
				|| !source.contains("name") || !source["name"].is_string()
				|| !source.contains("role") || !source["role"].is_string()
				|| !source.contains("graph") || !source["graph"].is_object())
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Graph entry in: " << filePath);
				return false;
			}
			AnimatorGraphAsset graph;
			graph.id = source["id"].get<std::string>();
			graph.name = source["name"].get<std::string>();
			if (!TryParseGraphRole(source["role"].get<std::string>(), graph.role))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Unknown Graph role in: " << filePath);
				return false;
			}
			graph.graph = VansAnimGraph::DeserializeFromJsonObject(source["graph"]);
			if (!graph.graph)
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Graph definition in: " << filePath);
				return false;
			}
			parsed.graphs.push_back(std::move(graph));
		}

		for (const json& source : root["layers"])
		{
			VansAnimationLayerDefinition layer;
			if (!DeserializeLayer(source, layer, error))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] " << error << ": " << filePath);
				return false;
			}
			parsed.layers.push_back(std::move(layer));
		}

		for (const json& source : root["slots"])
		{
			std::string unknown;
			if (!HasOnlyFields(source,
				{ "id", "name", "layerId", "nodeId", "concurrency", "maxQueueDepth", "defaultBlendIn", "defaultBlendOut", "interruptible" },
				unknown))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Slot field '" << unknown << "' in: " << filePath);
				return false;
			}
			VansAnimationSlotDefinition slot;
			slot.id = source.at("id").get<std::string>();
			slot.name = source.at("name").get<std::string>();
			slot.layerId = source.at("layerId").get<std::string>();
			slot.slotNodeId = source.at("nodeId").get<int>();
			slot.maxQueueDepth = source.at("maxQueueDepth").get<std::uint32_t>();
			slot.defaultBlendIn = source.at("defaultBlendIn").get<float>();
			slot.defaultBlendOut = source.at("defaultBlendOut").get<float>();
			slot.interruptible = source.at("interruptible").get<bool>();
			if (!ParseEnum(source.at("concurrency").get<std::string>(),
				{ { "replace", VansSlotConcurrency::Replace }, { "queue", VansSlotConcurrency::Queue },
				  { "reject", VansSlotConcurrency::Reject } }, slot.concurrency))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Unknown Slot concurrency in: " << filePath);
				return false;
			}
			parsed.slots.push_back(std::move(slot));
		}

		if (root.contains("editor") && root["editor"].contains("previewModel"))
		{
			const json& previewModel = root["editor"]["previewModel"];
			parsed.editor.previewModelGuid = previewModel["guid"].get<std::string>();
			parsed.editor.previewModelPathHint = previewModel["pathHint"].get<std::string>();
		}
	}
	catch (const json::exception& exception)
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Invalid canonical animator data in " << filePath << ": " << exception.what());
		return false;
	}

	if (!ValidateAssetDefinition(parsed, error))
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Invalid animator definition in " << filePath << ": " << error);
		return false;
	}
	outData = std::move(parsed);
	VANS_LOG("[VansAnimatorIO] Loaded .vanimator: " << filePath
		<< " (" << outData.parameters.size() << " params, " << outData.graphs.size()
		<< " Graphs, " << outData.layers.size() << " Layers)");
	return true;
}

bool VansAnimatorIO::DeserializeFromJsonObject(
	const AnimGraphJson& root,
	AnimatorAssetData& outData,
	std::string& error)
{
	error.clear();
	if (DeserializeAnimatorRoot(root, "<Animator document>", outData))
		return true;
	error = "Animator document failed canonical decoding or validation";
	return false;
}

bool VansAnimatorIO::Load(const std::string& filePath, AnimatorAssetData& outData)
{
	json root;
	if (!ReadAnimatorJson(filePath, root))
	{
		outData = AnimatorAssetData{};
		return false;
	}
	return DeserializeAnimatorRoot(root, filePath, outData);
}

bool VansAnimatorIO::Peek(const std::string& filePath,
	                       std::string& outName,
	                       uint32_t& outStateCount,
	                       uint32_t& outParamCount)
{
	outName.clear();
	outStateCount = 0;
	outParamCount = 0;
	json root;
	if (!ReadAnimatorJson(filePath, root) || !ValidateAnimatorRoot(root, filePath))
		return false;
	try
	{
		outName = root["name"].get<std::string>();
		outParamCount = static_cast<uint32_t>(root["parameters"].size());
		for (const json& graphEntry : root["graphs"])
		{
			if (!graphEntry.is_object() || !graphEntry.contains("graph") || !graphEntry["graph"].is_object())
				return false;
			const json& graph = graphEntry["graph"];
			if (!graph.contains("nodes") || !graph["nodes"].is_array())
				return false;
			for (const json& node : graph["nodes"])
			{
				if (!node.is_object() || node.value("type", "") != "StateMachine")
					continue;
				if (node.contains("properties") && node["properties"].is_object()
					&& node["properties"].contains("states") && node["properties"]["states"].is_array())
					outStateCount += static_cast<uint32_t>(node["properties"]["states"].size());
			}
		}
	}
	catch (const json::exception& exception)
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Invalid animator metadata in " << filePath << ": " << exception.what());
		return false;
	}
	return true;
}
