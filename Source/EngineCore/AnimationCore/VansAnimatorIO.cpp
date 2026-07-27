#include "VansAnimatorIO.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
#include <memory>

using json = nlohmann::json;
using namespace VansGraphics;

static bool ReadAnimatorJson(const std::string& filePath, json& root)
{
	std::string error;
	if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
	{
		VANS_LOG_WARN("[VansAnimatorIO] Cannot read .vanimator file: " << filePath << " (" << error << ")");
		return false;
	}
	return true;
}

// ════════════════════════════════════════════════════════════════
//  辅助函数: 参数类型字符串转换
// ════════════════════════════════════════════════════════════════

static std::string ParamTypeToString(AnimatorParamType type)
{
	switch (type)
	{
	case AnimatorParamType::Float:      return "float";
	case AnimatorParamType::Bool:       return "bool";
	case AnimatorParamType::Int:        return "int";
	case AnimatorParamType::Trigger:    return "trigger";
	case AnimatorParamType::Vector3:    return "vector3";
	case AnimatorParamType::Quaternion: return "quaternion";
	}
	return "float";
}

static AnimatorParamType StringToParamType(const std::string& str)
{
	if (str == "float")      return AnimatorParamType::Float;
	if (str == "bool")       return AnimatorParamType::Bool;
	if (str == "int")        return AnimatorParamType::Int;
	if (str == "trigger")    return AnimatorParamType::Trigger;
	if (str == "vector3")    return AnimatorParamType::Vector3;
	if (str == "quaternion") return AnimatorParamType::Quaternion;
	return AnimatorParamType::Float;
}

// ════════════════════════════════════════════════════════════════
//  辅助函数: CompareOp 字符串转换
// ════════════════════════════════════════════════════════════════

static std::string CompareOpToString(CompareOp op)
{
	switch (op)
	{
	case CompareOp::Greater:      return ">";
	case CompareOp::Less:         return "<";
	case CompareOp::Equal:        return "==";
	case CompareOp::NotEqual:     return "!=";
	case CompareOp::GreaterEqual: return ">=";
	case CompareOp::LessEqual:    return "<=";
	}
	return "==";
}

static CompareOp StringToCompareOp(const std::string& str)
{
	if (str == ">")  return CompareOp::Greater;
	if (str == "<")  return CompareOp::Less;
	if (str == "==") return CompareOp::Equal;
	if (str == "!=") return CompareOp::NotEqual;
	if (str == ">=") return CompareOp::GreaterEqual;
	if (str == "<=") return CompareOp::LessEqual;
	return CompareOp::Equal;
}

static void ReadTransitionConditionValue(const json& source, TransitionCondition& cond)
{
	if (source.contains("floatVal"))
		cond.floatVal = source["floatVal"].get<float>();
	if (source.contains("boolVal"))
		cond.boolVal = source["boolVal"].get<bool>();
	if (source.contains("intVal"))
		cond.intVal = source["intVal"].get<int>();

	if (!source.contains("value"))
		return;

	const json& value = source["value"];
	if (value.is_boolean())
	{
		cond.boolVal = value.get<bool>();
		cond.floatVal = cond.boolVal ? 1.0f : 0.0f;
		cond.intVal = cond.boolVal ? 1 : 0;
	}
	else if (value.is_number_integer())
	{
		cond.intVal = value.get<int>();
		cond.floatVal = static_cast<float>(cond.intVal);
		cond.boolVal = cond.intVal != 0;
	}
	else if (value.is_number_float())
	{
		cond.floatVal = value.get<float>();
		cond.intVal = static_cast<int>(cond.floatVal);
		cond.boolVal = cond.floatVal != 0.0f;
	}
}

static std::unique_ptr<VansAnimGraph> BuildGraphFromTopLevelStateMachine(const json& root)
{
	if (!root.contains("states") || !root["states"].is_array() || root["states"].empty())
		return nullptr;

	auto stateMachine = std::make_unique<AnimGraphStateMachineNode>();
	stateMachine->m_DefaultStateName = root.value("defaultState", std::string{});
	for (const auto& stateJson : root["states"])
	{
		AnimatorState state;
		state.name = stateJson.value("name", "");
		state.clipName = stateJson.value("clip", stateJson.value("clipName", ""));
		state.speed = stateJson.value("speed", 1.0f);
		state.loop = stateJson.value("loop", true);
		state.rootMotion = stateJson.value("rootMotion", false);
		state.startTime = stateJson.value("startTime", 0.0f);
		state.endTime = stateJson.value("endTime", -1.0f);

		if (state.name.empty())
			state.name = state.clipName;
		if (!state.name.empty())
			stateMachine->m_States.push_back(state);
	}

	if (stateMachine->m_States.empty())
		return nullptr;

	if (stateMachine->m_DefaultStateName.empty())
		stateMachine->m_DefaultStateName = stateMachine->m_States.front().name;
	stateMachine->m_CurrentStateName = stateMachine->m_DefaultStateName;

	if (root.contains("transitions") && root["transitions"].is_array())
	{
		for (const auto& transitionJson : root["transitions"])
		{
			AnimatorTransition transition;
			transition.fromState = transitionJson.value("from", "");
			transition.toState = transitionJson.value("to", "");
			transition.blendDuration = transitionJson.value("blendDuration", 0.2f);
			transition.hasExitTime = transitionJson.value("hasExitTime", false);
			transition.exitTime = transitionJson.value("exitTime", 1.0f);

			if (transitionJson.contains("conditions") && transitionJson["conditions"].is_array())
			{
				for (const auto& conditionJson : transitionJson["conditions"])
				{
					TransitionCondition condition;
					condition.paramName = conditionJson.value("param", conditionJson.value("paramName", ""));
					condition.op = StringToCompareOp(conditionJson.value("op", "=="));
					ReadTransitionConditionValue(conditionJson, condition);
					if (!condition.paramName.empty())
						transition.conditions.push_back(condition);
				}
			}

			if (!transition.fromState.empty() && !transition.toState.empty())
				stateMachine->m_Transitions.push_back(transition);
		}
	}

	auto graph = std::make_unique<VansAnimGraph>();
	const int stateMachineId = graph->AddNode(std::move(stateMachine));
	const int outputId = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
	if (stateMachineId >= 0 && outputId >= 0)
		graph->AddLink(stateMachineId, 0, outputId, 0);
	return graph;
}

// ════════════════════════════════════════════════════════════════
//  Save
// ════════════════════════════════════════════════════════════════

bool VansAnimatorIO::Save(const std::string& filePath,
                           const VansAnimationController& controller,
                           const std::vector<AnimatorClipRef>& clipRefs)
{
	json root;
	root["magic"]   = VANIMATOR_MAGIC;
	root["name"]    = controller.GetName();

	// ── 参数 ──
	json paramArray = json::array();
	for (const auto& [name, param] : controller.GetParameters())
	{
		json p;
		p["name"] = param.name;
		p["type"] = ParamTypeToString(param.type);

		switch (param.type)
		{
		case AnimatorParamType::Float:   p["default"] = param.floatVal; break;
		case AnimatorParamType::Bool:    p["default"] = param.boolVal;  break;
		case AnimatorParamType::Int:     p["default"] = param.intVal;   break;
		case AnimatorParamType::Trigger: break;
		case AnimatorParamType::Vector3:
			p["default"] = { param.vec3Val.x, param.vec3Val.y, param.vec3Val.z };
			break;
		case AnimatorParamType::Quaternion:
			p["default"] = { param.quatVal.x, param.quatVal.y, param.quatVal.z, param.quatVal.w };
			break;
		}
		paramArray.push_back(p);
	}
	root["parameters"] = paramArray;

	// ── Clips 引用 ──
	json clipArray = json::array();
	for (const auto& ref : clipRefs)
	{
		json c;
		c["name"] = ref.name;
		c["path"] = ref.path;
		clipArray.push_back(c);
	}
	root["clips"] = clipArray;

	// ── 默认状态 ──
	root["defaultState"] = controller.GetDefaultStateName();

	// ── States ──
	json stateArray = json::array();
	for (const auto& stateName : controller.GetStateNames())
	{
		const AnimatorState* state = controller.GetState(stateName);
		if (!state) continue;

		json s;
		s["name"]       = state->name;
		s["clip"]       = state->clipName;
		s["speed"]      = state->speed;
		s["loop"]       = state->loop;
		s["rootMotion"] = state->rootMotion;

		if (state->startTime > 0.0f)
			s["startTime"] = state->startTime;
		if (state->endTime >= 0.0f)
			s["endTime"] = state->endTime;

		stateArray.push_back(s);
	}
	root["states"] = stateArray;

	// ── Transitions ──
	json transArray = json::array();
	for (const auto& t : controller.GetTransitions())
	{
		json tr;
		tr["from"]          = t.fromState;
		tr["to"]            = t.toState;
		tr["blendDuration"] = t.blendDuration;

		if (t.hasExitTime)
		{
			tr["hasExitTime"] = true;
			tr["exitTime"]    = t.exitTime;
		}

		json condArray = json::array();
		for (const auto& cond : t.conditions)
		{
			json c;
			c["param"] = cond.paramName;
			c["op"]    = CompareOpToString(cond.op);

			// 根据参数类型序列化不同的值类型
			// 简化处理: 先尝试写 float 值，bool 和 int 用不同字段
			if (cond.boolVal || cond.op == CompareOp::Equal || cond.op == CompareOp::NotEqual)
			{
				// 如果是 bool 值，JSON 会正确处理
				// 但需要根据实际参数类型判断
				// 这里统一用 value 字段，加载时根据参数类型解析
				if (cond.floatVal != 0.0f)
					c["value"] = cond.floatVal;
				else if (cond.intVal != 0)
					c["value"] = cond.intVal;
				else
					c["value"] = cond.boolVal;
			}
			else
			{
				if (cond.floatVal != 0.0f)
					c["value"] = cond.floatVal;
				else
					c["value"] = cond.intVal;
			}
			condArray.push_back(c);
		}
		tr["conditions"] = condArray;
		transArray.push_back(tr);
	}
	root["transitions"] = transArray;
	if (const VansAnimGraph* graph = controller.GetGraph())
	{
		json graphJson;
		graph->SerializeToJsonObject(graphJson);
		root["graph"] = graphJson;
	}
	else
	{
		VANS_LOG_WARN("[VansAnimatorIO] Saving animator without graph: " << filePath);
	}

	// ── 确保目录存在 ──
	std::string error;
	if (!Vans::VansJsonFileStorage::WriteAtomic(filePath, root, error))
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Cannot save .vanimator file: " << filePath << " (" << error << ")");
		return false;
	}

	// ── 写文件 ──
	VANS_LOG("[VansAnimatorIO] Saved .vanimator: " << filePath);
	return true;
}

// ════════════════════════════════════════════════════════════════
//  Load
// ════════════════════════════════════════════════════════════════

bool VansAnimatorIO::Load(const std::string& filePath, AnimatorAssetData& outData)
{
	json root;
	if (!ReadAnimatorJson(filePath, root))
		return false;

	if (!root.contains("magic") || root["magic"].get<std::string>() != VANIMATOR_MAGIC)
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Invalid magic in: " << filePath);
		return false;
	}

	outData.name = root.value("name", "Unnamed");

	// ── 参数 ──
	if (root.contains("parameters") && root["parameters"].is_array())
	{
		for (const auto& p : root["parameters"])
		{
			AnimatorParameter param;
			param.name = p.value("name", "");
			param.type = StringToParamType(p.value("type", "float"));

			if (p.contains("default"))
			{
				switch (param.type)
				{
				case AnimatorParamType::Float:   param.floatVal = p["default"].get<float>(); break;
				case AnimatorParamType::Bool:    param.boolVal  = p["default"].get<bool>();  break;
				case AnimatorParamType::Int:     param.intVal   = p["default"].get<int>();   break;
				case AnimatorParamType::Trigger: break;
				case AnimatorParamType::Vector3:
					if (p["default"].is_array() && p["default"].size() >= 3)
					{
						param.vec3Val.x = p["default"][0].get<float>();
						param.vec3Val.y = p["default"][1].get<float>();
						param.vec3Val.z = p["default"][2].get<float>();
					}
					break;
				case AnimatorParamType::Quaternion:
					if (p["default"].is_array() && p["default"].size() >= 4)
					{
						param.quatVal.x = p["default"][0].get<float>();
						param.quatVal.y = p["default"][1].get<float>();
						param.quatVal.z = p["default"][2].get<float>();
						param.quatVal.w = p["default"][3].get<float>();
					}
					break;
				}
			}
			outData.parameters.push_back(param);
		}
	}

	// ── Clips 引用 ──
	if (root.contains("clips") && root["clips"].is_array())
	{
		for (const auto& c : root["clips"])
		{
			AnimatorClipRef ref;
			ref.name = c.value("name", "");
			ref.path = c.value("path", "");
			outData.clipRefs.push_back(ref);
		}
	}

	// ── AnimGraph ──
	if (root.contains("graph") && root["graph"].is_object())
	{
		outData.animGraph = VansAnimGraph::DeserializeFromJsonObject(root["graph"]);
		if (!outData.animGraph)
		{
			VANS_LOG_WARN("[VansAnimatorIO] Failed to deserialize graph in: " << filePath);
		}
	}
	else
	{
		outData.animGraph = BuildGraphFromTopLevelStateMachine(root);
		if (outData.animGraph)
		{
			VANS_LOG_WARN("[VansAnimatorIO] Upgraded top-level states/transitions to graph in memory: " << filePath);
		}
		else
		{
			VANS_LOG_WARN("[VansAnimatorIO] .vanimator has no graph and cannot be upgraded: " << filePath);
		}
	}

	VANS_LOG("[VansAnimatorIO] Loaded .vanimator: " << filePath
	         << " (" << outData.parameters.size() << " params"
	         << (outData.animGraph ? ", has graph" : "") << ")");
	return true;
}

// ════════════════════════════════════════════════════════════════
//  Peek (快速读取元信息)
// ════════════════════════════════════════════════════════════════

bool VansAnimatorIO::Peek(const std::string& filePath,
                           std::string& outName,
                           uint32_t& outStateCount,
                           uint32_t& outParamCount)
{
	json root;
	if (!ReadAnimatorJson(filePath, root))
		return false;

	if (!root.contains("magic") || root["magic"].get<std::string>() != VANIMATOR_MAGIC)
		return false;

	outName       = root.value("name", "Unnamed");
	outStateCount = 0;
	if (root.contains("graph") && root["graph"].is_object()
	    && root["graph"].contains("nodes") && root["graph"]["nodes"].is_array())
	{
		for (const auto& node : root["graph"]["nodes"])
		{
			if (node.value("type", "") != "StateMachine")
				continue;
			if (!node.contains("properties") || !node["properties"].is_object())
				continue;

			const json& props = node["properties"];
			if (props.contains("states") && props["states"].is_array())
			{
				outStateCount += static_cast<uint32_t>(props["states"].size());
			}
		}
	}
	if (outStateCount == 0 && root.contains("states") && root["states"].is_array())
	{
		outStateCount = static_cast<uint32_t>(root["states"].size());
	}
	outParamCount = root.contains("parameters") ? static_cast<uint32_t>(root["parameters"].size()) : 0;

	return true;
}
