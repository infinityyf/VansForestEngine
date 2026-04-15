#pragma once

#include "VansAnimationController.h"
#include <string>
#include <vector>

namespace VansGraphics
{
	constexpr uint32_t VANIMATOR_VERSION = 1;
	constexpr char     VANIMATOR_MAGIC[] = "VANIMATOR";

	// .vanimator 中记录的 clip 引用（名称 + 相对路径）
	struct AnimatorClipRef
	{
		std::string name;   // clip 名称 (= AnimatorState::clipName)
		std::string path;   // .vclip 文件的相对路径
	};

	// 从 .vanimator 文件加载得到的完整定义
	struct AnimatorAssetData
	{
		std::string                    name;
		std::vector<AnimatorParameter> parameters;
		std::vector<AnimatorClipRef>   clipRefs;
		std::vector<AnimatorState>     states;
		std::vector<AnimatorTransition> transitions;
		std::string                    defaultStateName;
	};

	// ────────────────────────────────────────────────────────────────
	//  VansAnimatorIO
	//
	//  Handles .vanimator file serialization / deserialization (pure JSON).
	// ────────────────────────────────────────────────────────────────

	class VansAnimatorIO
	{
	public:
		// 保存 Controller 定义 + clip 引用列表到 .vanimator 文件
		static bool Save(const std::string& filePath,
		                 const VansAnimationController& controller,
		                 const std::vector<AnimatorClipRef>& clipRefs);

		// 加载 .vanimator 文件 → AnimatorAssetData
		static bool Load(const std::string& filePath,
		                 AnimatorAssetData& outData);

		// 仅读取元信息（名称、状态数、参数数），不加载完整拓扑
		static bool Peek(const std::string& filePath,
		                 std::string& outName,
		                 uint32_t& outStateCount,
		                 uint32_t& outParamCount);
	};

}  // namespace VansGraphics
