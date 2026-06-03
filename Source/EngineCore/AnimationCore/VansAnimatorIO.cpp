#include "VansAnimatorIO.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
using namespace VansGraphics;

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

// ════════════════════════════════════════════════════════════════
//  Save
// ════════════════════════════════════════════════════════════════

bool VansAnimatorIO::Save(const std::string& filePath,
                           const VansAnimationController& controller,
                           const std::vector<AnimatorClipRef>& clipRefs)
{
	json root;
	root["magic"]   = VANIMATOR_MAGIC;
	root["version"] = VANIMATOR_VERSION;
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

	// ── 确保目录存在 ──
	std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();
	if (!dirPath.empty())
		std::filesystem::create_directories(dirPath);

	// ── 写文件 ──
	std::ofstream outFile(filePath);
	if (!outFile.is_open())
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Cannot open file for writing: " << filePath);
		return false;
	}

	outFile << root.dump(4);
	outFile.close();

	VANS_LOG("[VansAnimatorIO] Saved .vanimator: " << filePath);
	return true;
}

// ════════════════════════════════════════════════════════════════
//  Load
// ════════════════════════════════════════════════════════════════

bool VansAnimatorIO::Load(const std::string& filePath, AnimatorAssetData& outData)
{
	std::ifstream inFile(filePath);
	if (!inFile.is_open())
	{
		VANS_LOG_WARN("[VansAnimatorIO] Cannot open .vanimator file: " << filePath);
		return false;
	}

	json root;
	try
	{
		root = json::parse(inFile);
	}
	catch (const json::parse_error& e)
	{
		VANS_LOG_ERROR("[VansAnimatorIO] JSON parse error in " << filePath << ": " << e.what());
		return false;
	}

	// ── 校验 magic ──
	if (!root.contains("magic") || root["magic"].get<std::string>() != VANIMATOR_MAGIC)
	{
		VANS_LOG_ERROR("[VansAnimatorIO] Invalid magic in: " << filePath);
		return false;
	}

	outData.name = root.value("name", "Unnamed");
	outData.version = root.value("version", 1u);

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

	// ── v2: AnimGraph ──
	if (outData.version >= 2 && root.contains("graph") && root["graph"].is_object())
	{
		outData.animGraph = VansAnimGraph::DeserializeFromJsonObject(root["graph"]);
		if (!outData.animGraph)
		{
			VANS_LOG_WARN("[VansAnimatorIO] Failed to deserialize graph in: " << filePath);
		}
	}

	VANS_LOG("[VansAnimatorIO] Loaded .vanimator v" << outData.version << ": " << filePath
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
	std::ifstream inFile(filePath);
	if (!inFile.is_open())
		return false;

	json root;
	try
	{
		root = json::parse(inFile);
	}
	catch (const json::parse_error&)
	{
		return false;
	}

	if (!root.contains("magic") || root["magic"].get<std::string>() != VANIMATOR_MAGIC)
		return false;

	outName       = root.value("name", "Unnamed");
	outStateCount = root.contains("states") ? static_cast<uint32_t>(root["states"].size()) : 0;
	outParamCount = root.contains("parameters") ? static_cast<uint32_t>(root["parameters"].size()) : 0;

	return true;
}
