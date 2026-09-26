#include "VansAnimatorIO.h"
#include "VansAnimatorValidator.h"

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

	const char* ToString(VansLayerActivationCurve value)
	{
		return value == VansLayerActivationCurve::Linear ? "linear" : "smoothStep";
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

	const char* ToString(VansGraphSetBlendCurve value)
	{
		return value == VansGraphSetBlendCurve::Linear ? "linear" : "smoothStep";
	}

	const char* ToString(VansGraphSetPhasePolicy value)
	{
		switch (value)
		{
		case VansGraphSetPhasePolicy::Restart: return "restart";
		case VansGraphSetPhasePolicy::MatchNormalizedTime: return "matchNormalizedTime";
		case VansGraphSetPhasePolicy::MatchMarker: return "matchMarker";
		}
		return "matchNormalizedTime";
	}

	const char* ToString(VansGraphSetEventPolicy value)
	{
		return value == VansGraphSetEventPolicy::DominantSource
			? "dominantSource" : "weightedBoth";
	}

	const char* ToString(VansGraphSetRootMotionPolicy value)
	{
		switch (value)
		{
		case VansGraphSetRootMotionPolicy::Blend: return "blend";
		case VansGraphSetRootMotionPolicy::DominantSource: return "dominantSource";
		case VansGraphSetRootMotionPolicy::IncomingOnly: return "incomingOnly";
		}
		return "blend";
	}

	const char* ToString(VansGraphSetInterruptionPolicy value)
	{
		switch (value)
		{
		case VansGraphSetInterruptionPolicy::QueueLatest: return "queueLatest";
		case VansGraphSetInterruptionPolicy::Reject: return "reject";
		case VansGraphSetInterruptionPolicy::Force: return "force";
		}
		return "queueLatest";
	}

	json SerializeGraphSetTransitionPolicy(const VansGraphSetTransitionPolicy& policy)
	{
		return {
			{ "duration", policy.duration },
			{ "curve", ToString(policy.curve) },
			{ "phase", ToString(policy.phase) },
			{ "events", ToString(policy.events) },
			{ "rootMotion", ToString(policy.rootMotion) },
			{ "interruption", ToString(policy.interruption) },
			{ "requireStateMatch", policy.requireStateMatch }
		};
	}

	bool DeserializeGraphSetTransitionPolicy(
		const json& source,
		VansGraphSetTransitionPolicy& policy,
		std::string& error)
	{
		std::string unknown;
		if (!HasOnlyFields(source,
			{ "duration", "curve", "phase", "events", "rootMotion", "interruption", "requireStateMatch" },
			unknown))
		{
			error = "Invalid Graph Set transition policy field: " + unknown;
			return false;
		}
		try
		{
			policy.duration = source.at("duration").get<float>();
			policy.requireStateMatch = source.at("requireStateMatch").get<bool>();
			if (!ParseEnum(source.at("curve").get<std::string>(),
				{ { "linear", VansGraphSetBlendCurve::Linear },
				  { "smoothStep", VansGraphSetBlendCurve::SmoothStep } }, policy.curve)
				|| !ParseEnum(source.at("phase").get<std::string>(),
					{ { "restart", VansGraphSetPhasePolicy::Restart },
					  { "matchNormalizedTime", VansGraphSetPhasePolicy::MatchNormalizedTime },
					  { "matchMarker", VansGraphSetPhasePolicy::MatchMarker } }, policy.phase)
				|| !ParseEnum(source.at("events").get<std::string>(),
					{ { "dominantSource", VansGraphSetEventPolicy::DominantSource },
					  { "weightedBoth", VansGraphSetEventPolicy::WeightedBoth } }, policy.events)
				|| !ParseEnum(source.at("rootMotion").get<std::string>(),
					{ { "blend", VansGraphSetRootMotionPolicy::Blend },
					  { "dominantSource", VansGraphSetRootMotionPolicy::DominantSource },
					  { "incomingOnly", VansGraphSetRootMotionPolicy::IncomingOnly } }, policy.rootMotion)
				|| !ParseEnum(source.at("interruption").get<std::string>(),
					{ { "queueLatest", VansGraphSetInterruptionPolicy::QueueLatest },
					  { "reject", VansGraphSetInterruptionPolicy::Reject },
					  { "force", VansGraphSetInterruptionPolicy::Force } }, policy.interruption))
			{
				error = "Graph Set transition policy contains an unknown enum value";
				return false;
			}
		}
		catch (const json::exception& exception)
		{
			error = std::string("Invalid Graph Set transition policy: ") + exception.what();
			return false;
		}
		return true;
	}

	json SerializeLayer(const VansAnimationLayerDefinition& layer)
	{
		return {
			{ "id", layer.id },
			{ "name", layer.name },
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
				{ "smoothingTime", layer.weightSmoothingTime },
				{ "curve", layer.weightCurve },
				{ "curveDefault", layer.weightCurveDefault }
			} },
			{ "activation", {
				{ "blendInSeconds", layer.activationBlendInSeconds },
				{ "blendOutSeconds", layer.activationBlendOutSeconds },
				{ "curve", ToString(layer.activationCurve) },
				{ "restartOnRise", layer.restartOnActivation }
			} },
			{ "dynamicAdditive", {
				{ "enabled", layer.dynamicAdditive },
				{ "weight", layer.dynamicAdditiveWeight }
			} },
			{ "inertialization", {
				{ "halfLife", layer.inertializationHalfLife },
				{ "maxDuration", layer.inertializationMaxDuration }
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
			{ "updateWhenWeightIsZero", layer.updateWhenWeightIsZero }
		};
	}

	bool DeserializeLayer(const json& source,
	                      VansAnimationLayerDefinition& layer,
	                      std::string& error)
	{
		std::string unknown;
		if (!HasOnlyFields(source,
			{ "id", "name", "kind", "mask", "blend", "weight", "activation", "dynamicAdditive",
			  "inertialization", "outputs", "sync", "updateWhenWeightIsZero" },
			unknown))
		{
			error = "Invalid or unknown Layer field: " + unknown;
			return false;
		}
		if (!source.contains("id") || !source["id"].is_string()
			|| !source.contains("name") || !source["name"].is_string()
			|| !source.contains("kind") || !source["kind"].is_string()
			|| !source.contains("mask") || !source["mask"].is_object()
			|| !source.contains("blend") || !source["blend"].is_object()
			|| !source.contains("weight") || !source["weight"].is_object()
			|| !source.contains("outputs") || !source["outputs"].is_object()
			|| !source.contains("sync") || !source["sync"].is_object()
			|| !source.contains("updateWhenWeightIsZero") || !source["updateWhenWeightIsZero"].is_boolean())
		{
			error = "Layer is missing required canonical fields";
			return false;
		}

		const json& mask = source["mask"];
		const json& blend = source["blend"];
		const json& weight = source["weight"];
		const json activation = source.value("activation", json::object());
		const json dynamicAdditive = source.value("dynamicAdditive", json::object());
		const json inertialization = source.value("inertialization", json::object());
		const json& outputs = source["outputs"];
		const json& sync = source["sync"];
		if (!HasOnlyFields(mask, { "guid", "pathHint" }, unknown)
			|| !HasOnlyFields(blend, { "mode", "rotationSpace", "additiveReference" }, unknown)
			|| !HasOnlyFields(weight, { "source", "value", "parameter", "smoothingTime", "curve", "curveDefault" }, unknown)
			|| !HasOnlyFields(activation, { "blendInSeconds", "blendOutSeconds", "curve", "restartOnRise" }, unknown)
			|| !HasOnlyFields(dynamicAdditive, { "enabled", "weight" }, unknown)
			|| !HasOnlyFields(inertialization, { "halfLife", "maxDuration" }, unknown)
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
			layer.maskGuid = mask.at("guid").get<std::string>();
			layer.maskPathHint = mask.at("pathHint").get<std::string>();
			layer.referenceClipName = additive.at("clip").get<std::string>();
			layer.referenceTime = additive.at("time").get<float>();
			layer.fixedWeight = weight.at("value").get<float>();
			layer.weightParameter = weight.at("parameter").get<std::string>();
			layer.weightSmoothingTime = weight.at("smoothingTime").get<float>();
			layer.weightCurve = weight.value("curve", "");
			layer.weightCurveDefault = weight.value("curveDefault", 1.0f);
			layer.activationBlendInSeconds = activation.value("blendInSeconds", 0.0f);
			layer.activationBlendOutSeconds = activation.value("blendOutSeconds", 0.0f);
			layer.restartOnActivation = activation.value("restartOnRise", false);
			layer.dynamicAdditive = dynamicAdditive.value("enabled", false);
			layer.dynamicAdditiveWeight = dynamicAdditive.value("weight", 0.0f);
			layer.inertializationHalfLife = inertialization.value("halfLife", 0.0f);
			layer.inertializationMaxDuration = inertialization.value("maxDuration", 0.0f);
			layer.syncLeaderLayerId = sync.at("leaderLayerId").get<std::string>();
			layer.eventWeightThreshold = outputs.at("eventWeightThreshold").get<float>();
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
			const std::string activationCurve = activation.value("curve", "smoothStep");
			if (!ParseEnum(activationCurve,
				{ { "linear", VansLayerActivationCurve::Linear },
				  { "smoothStep", VansLayerActivationCurve::SmoothStep } }, layer.activationCurve))
			{
				error = "Layer activation contains an unknown curve";
				return false;
			}

			const std::string weightSource = weight.at("source").get<std::string>();
			if (weightSource != "constant" && weightSource != "parameter")
			{
				error = "Layer weight source must be constant or parameter";
				return false;
			}
			layer.useWeightParameter = weightSource == "parameter";
			const bool finiteActivation = std::isfinite(layer.activationBlendInSeconds)
				&& std::isfinite(layer.activationBlendOutSeconds)
				&& layer.activationBlendInSeconds >= 0.0f
				&& layer.activationBlendOutSeconds >= 0.0f;
			const bool finiteDynamicAdditive = std::isfinite(layer.dynamicAdditiveWeight)
				&& layer.dynamicAdditiveWeight >= 0.0f
				&& layer.dynamicAdditiveWeight <= 1.0f;
			const bool finiteInertialization = std::isfinite(layer.inertializationHalfLife)
				&& std::isfinite(layer.inertializationMaxDuration)
				&& layer.inertializationHalfLife >= 0.0f
				&& layer.inertializationMaxDuration >= 0.0f;
			const bool finiteCurveWeight = std::isfinite(layer.weightCurveDefault)
				&& layer.weightCurveDefault >= 0.0f && layer.weightCurveDefault <= 1.0f;
			if (!finiteActivation || !finiteDynamicAdditive || !finiteInertialization || !finiteCurveWeight)
			{
				error = "Layer activation, dynamic additive, and inertialization values must be finite and non-negative";
				return false;
			}
		}
		catch (const json::exception& exception)
		{
			error = std::string("Invalid Layer value: ") + exception.what();
			return false;
		}
		return true;
	}


	bool BuildAnimatorJson(const AnimatorAssetData& data, json& root, std::string& error)
	{
		if (!VansAnimatorValidator::Validate(data, error))
			return false;

		root = json::object();
		root["magic"] = VANIMATOR_MAGIC;
		root["name"] = data.name;
		root["animationRigGuid"] = data.animationRigGuid;

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

		json graphSetArray = json::array();
		for (const VansAnimationGraphSetDefinition& graphSet : data.graphSets)
		{
			json bindings = json::array();
			for (const VansAnimationGraphBindingDefinition& binding : graphSet.bindings)
				bindings.push_back({ { "layerId", binding.layerId },
					{ "graphId", binding.graphId }, { "enabled", binding.enabled } });
			graphSetArray.push_back({ { "id", graphSet.id }, { "name", graphSet.name },
				{ "bindings", std::move(bindings) } });
		}
		root["graphSets"] = std::move(graphSetArray);
		root["defaultGraphSetId"] = data.defaultGraphSetId;
		json transitionRules = json::array();
		for (const VansGraphSetTransitionRule& rule : data.graphSetTransitionRules)
			transitionRules.push_back({ { "from", rule.fromGraphSetId },
				{ "to", rule.toGraphSetId },
				{ "policy", SerializeGraphSetTransitionPolicy(rule.policy) } });
		root["graphSetTransitions"] = {
			{ "default", SerializeGraphSetTransitionPolicy(data.defaultGraphSetTransition) },
			{ "rules", std::move(transitionRules) }
		};

		json slotArray = json::array();
		for (const VansAnimationSlotDefinition& slot : data.slots)
		{
			slotArray.push_back({
				{ "id", slot.id }, { "name", slot.name }, { "layerId", slot.layerId }, { "group", slot.group },
				{ "concurrency", ToString(slot.concurrency) },
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
		if (!HasOnlyFields(root, { "magic", "name", "animationRigGuid", "parameters", "clips", "graphs", "layers", "graphSets", "defaultGraphSetId", "graphSetTransitions", "slots", "editor" }, unknown))
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Unknown root field '" << unknown << "' in canonical animator: " << filePath);
			return false;
		}
		if (!root.contains("magic") || !root["magic"].is_string() || root["magic"].get<std::string>() != VANIMATOR_MAGIC
			|| !root.contains("name") || !root["name"].is_string()
			|| !root.contains("animationRigGuid") || !root["animationRigGuid"].is_string()
			|| !root.contains("parameters") || !root["parameters"].is_array()
			|| !root.contains("clips") || !root["clips"].is_array()
			|| !root.contains("graphs") || !root["graphs"].is_array()
			|| !root.contains("layers") || !root["layers"].is_array()
			|| !root.contains("graphSets") || !root["graphSets"].is_array()
			|| !root.contains("defaultGraphSetId") || !root["defaultGraphSetId"].is_string()
			|| !root.contains("graphSetTransitions") || !root["graphSetTransitions"].is_object()
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
	                                        nlohmann::json& outJson,
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
		parsed.animationRigGuid = root["animationRigGuid"].get<std::string>();
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

		for (const json& source : root["graphSets"])
		{
			std::string unknown;
			if (!HasOnlyFields(source, { "id", "name", "bindings" }, unknown)
				|| !source.contains("id") || !source["id"].is_string()
				|| !source.contains("name") || !source["name"].is_string()
				|| !source.contains("bindings") || !source["bindings"].is_array())
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Graph Set entry in: " << filePath);
				return false;
			}
			VansAnimationGraphSetDefinition graphSet;
			graphSet.id = source["id"].get<std::string>();
			graphSet.name = source["name"].get<std::string>();
			for (const json& bindingSource : source["bindings"])
			{
				if (!HasOnlyFields(bindingSource, { "layerId", "graphId", "enabled" }, unknown)
					|| !bindingSource.contains("layerId") || !bindingSource["layerId"].is_string()
					|| !bindingSource.contains("graphId") || !bindingSource["graphId"].is_string()
					|| !bindingSource.contains("enabled") || !bindingSource["enabled"].is_boolean())
				{
					VANS_LOG_ERROR("[VansAnimatorIO] Invalid Graph Set binding in: " << filePath);
					return false;
				}
				graphSet.bindings.push_back({
					bindingSource["layerId"].get<std::string>(),
					bindingSource["graphId"].get<std::string>(),
					bindingSource["enabled"].get<bool>() });
			}
			parsed.graphSets.push_back(std::move(graphSet));
		}
		parsed.defaultGraphSetId = root["defaultGraphSetId"].get<std::string>();
		const json& transitions = root["graphSetTransitions"];
		std::string transitionUnknown;
		if (!HasOnlyFields(transitions, { "default", "rules" }, transitionUnknown)
			|| !transitions.contains("default") || !transitions["default"].is_object()
			|| !transitions.contains("rules") || !transitions["rules"].is_array()
			|| !DeserializeGraphSetTransitionPolicy(
				transitions["default"], parsed.defaultGraphSetTransition, error))
		{
			VANS_LOG_ERROR("[VansAnimatorIO] Invalid Graph Set transitions in "
				<< filePath << ": " << error);
			return false;
		}
		for (const json& ruleSource : transitions["rules"])
		{
			if (!HasOnlyFields(ruleSource, { "from", "to", "policy" }, transitionUnknown)
				|| !ruleSource.contains("from") || !ruleSource["from"].is_string()
				|| !ruleSource.contains("to") || !ruleSource["to"].is_string()
				|| !ruleSource.contains("policy") || !ruleSource["policy"].is_object())
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Graph Set transition rule in: " << filePath);
				return false;
			}
			VansGraphSetTransitionRule rule;
			rule.fromGraphSetId = ruleSource["from"].get<std::string>();
			rule.toGraphSetId = ruleSource["to"].get<std::string>();
			if (!DeserializeGraphSetTransitionPolicy(ruleSource["policy"], rule.policy, error))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] " << error << ": " << filePath);
				return false;
			}
			parsed.graphSetTransitionRules.push_back(std::move(rule));
		}

		for (const json& source : root["slots"])
		{
			std::string unknown;
			if (!HasOnlyFields(source,
				{ "id", "name", "layerId", "group", "concurrency", "maxQueueDepth", "defaultBlendIn", "defaultBlendOut", "interruptible" },
				unknown))
			{
				VANS_LOG_ERROR("[VansAnimatorIO] Invalid Slot field '" << unknown << "' in: " << filePath);
				return false;
			}
			VansAnimationSlotDefinition slot;
			slot.id = source.at("id").get<std::string>();
			slot.name = source.at("name").get<std::string>();
			slot.layerId = source.at("layerId").get<std::string>();
			slot.group = source.value("group", "");
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

	if (!VansAnimatorValidator::Validate(parsed, error))
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
	const nlohmann::json& root,
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
