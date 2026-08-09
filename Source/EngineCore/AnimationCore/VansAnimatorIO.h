#pragma once

#include "VansAnimationController.h"
#include "VansAnimGraph.h"
#include <string>
#include <vector>
#include <memory>

namespace VansGraphics
{
	constexpr char     VANIMATOR_MAGIC[] = "VANIMATOR";

	// .vanimator 中的 Clip 引用。GUID 是唯一运行时标识，pathHint 仅供编辑器显示和诊断。
	struct AnimatorClipRef
	{
		std::string name;
		std::string assetGuid;
		std::string pathHint;
	};

	// 从 .vanimator 文件加载得到的完整定义
	struct AnimatorGraphAsset
	{
		enum class Role
		{
			Pose,
			TargetPostProcess
		};

		std::string id;
		std::string name;
		Role role = Role::Pose;
		std::unique_ptr<VansAnimGraph> graph;
	};

	struct AnimatorAssetData
	{
		struct EditorSettings
		{
			std::string previewModelGuid;
			std::string previewModelPathHint;
		};

		std::string                    name;
		std::vector<AnimatorParameter> parameters;
		std::vector<AnimatorClipRef>   clipRefs;

		// 逻辑图数据
		std::vector<AnimatorGraphAsset> graphs;
		std::vector<VansAnimationLayerDefinition> layers;
		std::vector<VansAnimationSlotDefinition> slots;
		EditorSettings editor;

		VansAnimGraph* FindGraph(const std::string& graphId);
		const VansAnimGraph* FindGraph(const std::string& graphId) const;
		VansAnimGraph* FindTargetPostProcessGraph();
		const VansAnimGraph* FindTargetPostProcessGraph() const;
	};

	// ────────────────────────────────────────────────────────────────
	//  VansAnimatorIO
	//
	//  Handles .vanimator file serialization / deserialization (pure JSON).
	// ────────────────────────────────────────────────────────────────

	class VansAnimatorIO
	{
	public:
		// 将类型化工作副本编码为唯一现行 JSON；不访问文件系统。
		static bool SerializeToJsonObject(const AnimatorAssetData& data,
		                                  AnimGraphJson& outJson,
		                                  std::string& error);
		static bool DeserializeFromJsonObject(const AnimGraphJson& json,
		                                    AnimatorAssetData& outData,
		                                    std::string& error);

		// 保存 Controller 定义 + clip 引用列表到 .vanimator 文件
		static bool Save(const std::string& filePath,
		                 const AnimatorAssetData& data,
		                 std::string& error);

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
