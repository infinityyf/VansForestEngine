#include "VansAIBehaviorJsonCodec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>

namespace Vans
{
namespace
{
bool ParseValueType(const std::string& text, VansAIValueType& type)
{
	if (text == "bool") type = VansAIValueType::Bool;
	else if (text == "int") type = VansAIValueType::Int;
	else if (text == "float") type = VansAIValueType::Float;
	else if (text == "vector3") type = VansAIValueType::Vector3;
	else if (text == "entity") type = VansAIValueType::Entity;
	else return false;
	return true;
}

bool ParseBlackboardEntry(const nlohmann::json& json,
	VansAIBlackboardEntryDefinition& out, std::string& error)
{
	if (!json.is_object() || !json.contains("name") || !json["name"].is_string() ||
		!json.contains("type") || !json["type"].is_string() || !json.contains("default"))
	{
		error = "AI Blackboard entries require name, type, and default";
		return false;
	}
	out.name = json["name"].get<std::string>();
	if (!ParseValueType(json["type"].get<std::string>(), out.type))
	{
		error = "Unknown AI Blackboard value type for entry: " + out.name;
		return false;
	}
	const nlohmann::json& value = json["default"];
	switch (out.type)
	{
	case VansAIValueType::Bool:
		if (!value.is_boolean()) break;
		out.defaultValue = value.get<bool>(); return true;
	case VansAIValueType::Int:
		if (!value.is_number_integer()) break;
		out.defaultValue = value.get<std::int64_t>(); return true;
	case VansAIValueType::Float:
		if (!value.is_number()) break;
		out.defaultValue = value.get<double>(); return true;
	case VansAIValueType::Vector3:
		if (!value.is_array() || value.size() != 3u ||
			!value[0].is_number() || !value[1].is_number() || !value[2].is_number()) break;
		out.defaultValue = glm::vec3(value[0].get<float>(), value[1].get<float>(),
			value[2].get<float>()); return true;
	case VansAIValueType::Entity:
		if (!value.is_null()) break;
		out.defaultValue = VansEntityHandle{}; return true;
	default: break;
	}
	error = "AI Blackboard default value type mismatch for entry: " + out.name;
	return false;
}

bool ParseCondition(const nlohmann::json& json,
	VansAIConditionDefinition& out, std::string& error)
{
	if (!json.is_object() || !json.contains("type") || !json["type"].is_string())
	{
		error = "AI transition condition requires a type";
		return false;
	}
	const std::string type = json["type"].get<std::string>();
	if (type == "BlackboardBool")
	{
		if (!json.contains("key") || !json["key"].is_string() ||
			!json.contains("value") || !json["value"].is_boolean())
		{
			error = "BlackboardBool condition requires string key and bool value";
			return false;
		}
		out.kind = VansAIConditionKind::BlackboardBool;
		out.key = json["key"].get<std::string>();
		out.expectedBool = json["value"].get<bool>();
		return true;
	}
	if (type == "AnimationState")
	{
		if (!json.contains("value") || !json["value"].is_string())
		{
			error = "AnimationState condition requires a string value";
			return false;
		}
		out.kind = VansAIConditionKind::AnimationState;
		out.expectedString = json["value"].get<std::string>();
		return true;
	}
	error = "Unknown AI transition condition type: " + type;
	return false;
}
}

bool VansAIBehaviorJsonCodec::Decode(const nlohmann::json& root,
	VansAIBehaviorAsset& outAsset, std::string& error)
{
	if (!root.is_object() || root.value("magic", std::string()) != "VAI_BEHAVIOR" ||
		!root.contains("name") || !root["name"].is_string() ||
		!root.contains("blackboard") || !root["blackboard"].is_array() ||
		!root.contains("initialState") || !root["initialState"].is_string() ||
		!root.contains("states") || !root["states"].is_array())
	{
		error = "AI Behavior must use the VAI_BEHAVIOR schema";
		return false;
	}

	VansAIBehaviorAsset asset;
	asset.name = root["name"].get<std::string>();
	asset.initialState = root["initialState"].get<std::string>();
	std::unordered_set<std::string> blackboardNames;
	for (const nlohmann::json& item : root["blackboard"])
	{
		VansAIBlackboardEntryDefinition entry;
		if (!ParseBlackboardEntry(item, entry, error)) return false;
		if (entry.name.empty() || !blackboardNames.insert(entry.name).second)
		{
			error = "AI Behavior has an empty or duplicate Blackboard entry: " + entry.name;
			return false;
		}
		asset.blackboard.push_back(std::move(entry));
	}

	std::unordered_set<std::string> stateIds;
	for (const nlohmann::json& stateJson : root["states"])
	{
		if (!stateJson.is_object() || !stateJson.contains("id") ||
			!stateJson["id"].is_string() || !stateJson.contains("task") ||
			!stateJson["task"].is_string() || !stateJson.contains("transitions") ||
			!stateJson["transitions"].is_array())
		{
			error = "AI states require id, task, and transitions";
			return false;
		}
		VansAIStateDefinition state;
		state.id = stateJson["id"].get<std::string>();
		if (state.id.empty() || !stateIds.insert(state.id).second)
		{
			error = "AI Behavior has an empty or duplicate state: " + state.id;
			return false;
		}
		const std::string task = stateJson["task"].get<std::string>();
		if (task == "Hold") state.task = VansAITaskKind::Hold;
		else if (task == "MoveToTarget") state.task = VansAITaskKind::MoveToTarget;
		else if (task == "Patrol")
		{
			state.task = VansAITaskKind::Patrol;
			if (!stateJson.contains("taskConfig") || !stateJson["taskConfig"].is_object())
			{
				error = "Patrol task requires a taskConfig object";
				return false;
			}
			const nlohmann::json& config = stateJson["taskConfig"];
			if (!config.contains("radius") || !config["radius"].is_number() ||
				!config.contains("waitSeconds") || !config["waitSeconds"].is_number())
			{
				error = "Patrol taskConfig requires numeric radius and waitSeconds";
				return false;
			}
			VansAIPatrolTaskConfig patrol;
			patrol.radius = (std::max)(0.0f, config["radius"].get<float>());
			patrol.waitSeconds = (std::max)(0.0f, config["waitSeconds"].get<float>());
			state.patrol = patrol;
		}
		else
		{
			error = "Unknown AI task: " + task;
			return false;
		}
		for (const nlohmann::json& transitionJson : stateJson["transitions"])
		{
			if (!transitionJson.is_object() || !transitionJson.contains("target") ||
				!transitionJson["target"].is_string() ||
				!transitionJson.contains("condition"))
			{
				error = "AI transitions require condition and target";
				return false;
			}
			VansAITransitionDefinition transition;
			transition.targetState = transitionJson["target"].get<std::string>();
			if (!ParseCondition(transitionJson["condition"], transition.condition, error))
				return false;
			state.transitions.push_back(std::move(transition));
		}
		asset.states.push_back(std::move(state));
	}
	if (!asset.FindState(asset.initialState))
	{
		error = "AI Behavior initialState does not resolve: " + asset.initialState;
		return false;
	}
	for (const VansAIStateDefinition& state : asset.states)
	{
		for (const VansAITransitionDefinition& transition : state.transitions)
		{
			if (!asset.FindState(transition.targetState))
			{
				error = "AI transition target does not resolve: " + transition.targetState;
				return false;
			}
			if (transition.condition.kind == VansAIConditionKind::BlackboardBool)
			{
				const auto definition = std::find_if(asset.blackboard.begin(), asset.blackboard.end(),
					[&](const VansAIBlackboardEntryDefinition& entry)
					{ return entry.name == transition.condition.key; });
				if (definition == asset.blackboard.end() || definition->type != VansAIValueType::Bool)
				{
					error = "AI condition does not reference a bool Blackboard entry: " +
						transition.condition.key;
					return false;
				}
			}
		}
	}
	outAsset = std::move(asset);
	error.clear();
	return true;
}

bool VansAIBehaviorJsonCodec::Encode(const VansAIBehaviorAsset& asset,
	nlohmann::json& root, std::string& error)
{
	root = {
		{ "magic", "VAI_BEHAVIOR" },
		{ "name", asset.name },
		{ "blackboard", nlohmann::json::array() },
		{ "initialState", asset.initialState },
		{ "states", nlohmann::json::array() }
	};
	for (const VansAIBlackboardEntryDefinition& entry : asset.blackboard)
	{
		const char* type = nullptr;
		nlohmann::json value;
		switch (entry.type)
		{
		case VansAIValueType::Bool:
			type = "bool";
			if (const bool* typed = std::get_if<bool>(&entry.defaultValue)) value = *typed;
			else { error = "AI Blackboard bool entry has a mismatched default: " + entry.name; return false; }
			break;
		case VansAIValueType::Int:
			type = "int";
			if (const std::int64_t* typed = std::get_if<std::int64_t>(&entry.defaultValue)) value = *typed;
			else { error = "AI Blackboard int entry has a mismatched default: " + entry.name; return false; }
			break;
		case VansAIValueType::Float:
			type = "float";
			if (const double* typed = std::get_if<double>(&entry.defaultValue)) value = *typed;
			else { error = "AI Blackboard float entry has a mismatched default: " + entry.name; return false; }
			break;
		case VansAIValueType::Vector3:
			type = "vector3";
			if (const glm::vec3* typed = std::get_if<glm::vec3>(&entry.defaultValue))
				value = { typed->x, typed->y, typed->z };
			else { error = "AI Blackboard vector3 entry has a mismatched default: " + entry.name; return false; }
			break;
		case VansAIValueType::Entity:
			type = "entity";
			if (!std::holds_alternative<VansEntityHandle>(entry.defaultValue))
			{
				error = "AI Blackboard entity entry has a mismatched default: " + entry.name;
				return false;
			}
			value = nullptr;
			break;
		default:
			error = "AI Blackboard entry has an invalid value type: " + entry.name;
			return false;
		}
		root["blackboard"].push_back({
			{ "name", entry.name }, { "type", type }, { "default", std::move(value) }
		});
	}
	for (const VansAIStateDefinition& state : asset.states)
	{
		const char* task = nullptr;
		switch (state.task)
		{
		case VansAITaskKind::Hold: task = "Hold"; break;
		case VansAITaskKind::MoveToTarget: task = "MoveToTarget"; break;
		case VansAITaskKind::Patrol: task = "Patrol"; break;
		default:
			error = "AI state has an invalid task: " + state.id;
			return false;
		}
		nlohmann::json stateJson = {
			{ "id", state.id }, { "task", task }, { "transitions", nlohmann::json::array() }
		};
		if (state.task == VansAITaskKind::Patrol)
		{
			if (!state.patrol)
			{
				error = "AI Patrol state has no taskConfig: " + state.id;
				return false;
			}
			stateJson["taskConfig"] = {
				{ "radius", state.patrol->radius },
				{ "waitSeconds", state.patrol->waitSeconds }
			};
		}
		for (const VansAITransitionDefinition& transition : state.transitions)
		{
			nlohmann::json condition;
			switch (transition.condition.kind)
			{
			case VansAIConditionKind::BlackboardBool:
				condition = {
					{ "type", "BlackboardBool" },
					{ "key", transition.condition.key },
					{ "value", transition.condition.expectedBool }
				};
				break;
			case VansAIConditionKind::AnimationState:
				condition = {
					{ "type", "AnimationState" },
					{ "value", transition.condition.expectedString }
				};
				break;
			default:
				error = "AI transition has an invalid condition kind in state: " + state.id;
				return false;
			}
			stateJson["transitions"].push_back({
				{ "condition", std::move(condition) },
				{ "target", transition.targetState }
			});
		}
		root["states"].push_back(std::move(stateJson));
	}
	VansAIBehaviorAsset verified;
	return Decode(root, verified, error);
}
}
