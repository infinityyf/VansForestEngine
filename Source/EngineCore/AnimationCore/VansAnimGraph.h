#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansAnimGraph — 动画逻辑图系统
//
//  以有向无环图（DAG）描述动画的求值流程：
//    Entry → 各种逻辑/混合节点 → Output
//
//  Controller 每帧通过 Evaluate() 从 Output 节点向上游 pull 求值，
//  各节点递归拉取输入，最终由 ClipNode 采样关键帧产生骨骼 Pose。
//  参数（float/bool/int/trigger）由 Controller 管理，作为 Context 传入。
// ────────────────────────────────────────────────────────────────────

#include "VansAnimationTypes.h"
#include "VansAnimationController.h"
#include "IK/VansIKTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include <nlohmann/json_fwd.hpp>

namespace VansGraphics
{
	// 前向声明
	struct AnimatorParameter;
	class VansAnimGraph;
	class VansIKSolver;

	// ─────────────────────────────────────────────────────────────
	//  节点类型枚举
	// ─────────────────────────────────────────────────────────────

	enum class AnimGraphNodeType
	{
		Entry,           // 入口节点（图的起点，不产生 Pose）
		Output,          // 输出节点（图的终点，收集最终 Pose）
		Clip,            // 播放单个 AnimationClip，输出 Pose
		Blend,           // 双输入线性混合（alpha 参数驱动）
		Blend1D,         // 1D 混合空间（float 参数映射到多个 Clip 权重）
		IfCondition,     // 条件选择：根据参数比较结果选择两路 Pose 之一
		Switch,          // 多路选择：根据 int 参数选择 N 路 Pose 之一
		AdditiveBlend,   // 叠加混合：base + additive * weight
		SpeedScale,      // 播放速度缩放（套在 Clip 输入上）
		StateMachine,    // 嵌入式状态机节点（适配旧有 FSM 逻辑）
		IK,              // 通用 IK 节点（CCD/FABRIK 求解人体或非关节链）
		TwoBoneIK,       // 双骨骼 IK 节点（人体四肢快捷配置）
		LookAt           // 朝向/瞄准节点
	};

	// ─────────────────────────────────────────────────────────────
	//  Pin 定义（节点的输入/输出端口）
	// ─────────────────────────────────────────────────────────────

	enum class AnimGraphPinType
	{
		Pose,    // 骨骼姿态数据
		Float,   // 浮点参数
		Bool,    // 布尔参数
		Int      // 整数参数
	};

	enum class AnimGraphPinKind
	{
		Input,
		Output
	};

	struct AnimGraphPin
	{
		int                pinIndex = 0;   // 在节点内的局部索引
		std::string        name;
		AnimGraphPinType   type  = AnimGraphPinType::Pose;
		AnimGraphPinKind   kind  = AnimGraphPinKind::Input;
	};

	// ─────────────────────────────────────────────────────────────
	//  连线（Link）
	// ─────────────────────────────────────────────────────────────

	struct AnimGraphLink
	{
		int linkId      = -1;
		int fromNodeId  = -1;
		int fromPinIndex = 0;   // 源节点的输出 pin 索引
		int toNodeId    = -1;
		int toPinIndex  = 0;    // 目标节点的输入 pin 索引
	};

	// ─────────────────────────────────────────────────────────────
	//  求值上下文（每帧由 Controller 构建，传递给图）
	// ─────────────────────────────────────────────────────────────

	struct AnimGraphContext
	{
		float                                                      deltaTime  = 0.0f;
		const Skeleton*                                            skeleton   = nullptr;
		const std::unordered_map<std::string, AnimatorParameter>*  parameters = nullptr;
		const std::unordered_map<std::string, VansAnimationClip>*  clips      = nullptr;
	};

	// ─────────────────────────────────────────────────────────────
	//  Pose 输出（节点的求值结果）
	// ─────────────────────────────────────────────────────────────

	struct AnimGraphPose
	{
		std::vector<glm::mat4> localTransforms;   // 每根骨骼的局部变换
		bool                   valid = false;
	};

	// ─────────────────────────────────────────────────────────────
	//  节点基类
	// ─────────────────────────────────────────────────────────────

	class VansAnimGraphNode
	{
	public:
		VansAnimGraphNode() = default;
		virtual ~VansAnimGraphNode() = default;

		int                GetNodeId()  const { return m_NodeId; }
		AnimGraphNodeType  GetType()    const { return m_Type; }
		const std::string& GetName()    const { return m_Name; }
		void               SetName(const std::string& name) { m_Name = name; }

		// 节点在编辑器中的位置（仅编辑器用，不影响运行时）
		float m_EditorPosX = 0.0f;
		float m_EditorPosY = 0.0f;

		// 获取该节点的所有 Pin 定义
		virtual std::vector<AnimGraphPin> GetPins() const = 0;

		// 求值：递归拉取输入，计算输出 Pose
		virtual AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                               VansAnimGraph& graph) = 0;

		// 每帧推进内部时间（ClipNode 等需要 tick）
		virtual void AdvanceTime(float dt) {}

		// 重置内部状态（播放时间等）
		virtual void Reset() {}

		// 获取节点类型名称字符串（序列化用）
		static const char* TypeToString(AnimGraphNodeType type);
		static AnimGraphNodeType StringToType(const std::string& str);

	protected:
		int               m_NodeId = -1;
		std::string       m_Name;
		AnimGraphNodeType m_Type   = AnimGraphNodeType::Entry;

		friend class VansAnimGraph;
	};

	// ═════════════════════════════════════════════════════════════
	//  具体节点类型实现
	// ═════════════════════════════════════════════════════════════

	// ─── EntryNode ──────────────────────────────────────────────
	//  图的入口标记，只有一个 Output Pin（Pose），不产生实际数据。
	//  Entry 连接到图中第一个处理节点。

	class AnimGraphEntryNode : public VansAnimGraphNode
	{
	public:
		AnimGraphEntryNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;
	};

	// ─── OutputNode ─────────────────────────────────────────────
	//  图的终点，只有一个 Input Pin（Pose）。
	//  Evaluate 时直接拉取输入连接的 Pose 作为最终结果。

	class AnimGraphOutputNode : public VansAnimGraphNode
	{
	public:
		AnimGraphOutputNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;
	};

	// ─── ClipNode ───────────────────────────────────────────────
	//  播放一个 AnimationClip，每帧采样关键帧输出骨骼 Pose。
	//  自行维护播放时间（currentTime），支持 loop/speed。

	class AnimGraphClipNode : public VansAnimGraphNode
	{
	public:
		AnimGraphClipNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;
		void AdvanceTime(float dt) override;
		void Reset() override;

		// 配置
		std::string m_ClipName;
		float       m_Speed     = 1.0f;
		bool        m_Loop      = true;

		// 运行时状态（不序列化）
		float       m_CurrentTime = 0.0f;

	private:
		void InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
		                          float time,
		                          glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);
	};

	// ─── BlendNode ──────────────────────────────────────────────
	//  双输入线性混合。
	//  Input 0: Pose A
	//  Input 1: Pose B
	//  混合权重由 m_ParamName 指定的参数动态控制（0.0=A, 1.0=B）。
	//  也可直接设置固定 m_FixedAlpha。

	class AnimGraphBlendNode : public VansAnimGraphNode
	{
	public:
		AnimGraphBlendNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::string m_ParamName;       // 驱动 alpha 的参数名（Float 类型）
		float       m_FixedAlpha = 0.5f;  // 无参数时使用的固定 alpha
		bool        m_UseParam   = true;  // true=用参数, false=用固定值
	};

	// ─── Blend1DNode ────────────────────────────────────────────
	//  1D 混合空间：根据 float 参数值在 N 个 Pose 之间插值。
	//  每个输入 Pose 对应一个阈值（threshold），按阈值升序排列。
	//  参数值落在两个阈值之间时，线性混合相邻两个 Pose。
	//
	//  例: thresholds = [0.0, 0.5, 1.0]
	//       Input 0 = Idle,  Input 1 = Walk,  Input 2 = Run
	//       param = 0.3 → 混合 Idle(40%) 与 Walk(60%)

	class AnimGraphBlend1DNode : public VansAnimGraphNode
	{
	public:
		AnimGraphBlend1DNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::string        m_ParamName;    // 驱动混合的 Float 参数名
		std::vector<float> m_Thresholds;   // 每个输入 Pin 对应的阈值（升序）
	};

	// ─── IfConditionNode ────────────────────────────────────────
	//  条件选择节点。
	//  Input 0: True Pose（条件满足时输出）
	//  Input 1: False Pose（条件不满足时输出）
	//  条件：m_ParamName [m_CompareOp] m_CompareValue

	class AnimGraphIfConditionNode : public VansAnimGraphNode
	{
	public:
		AnimGraphIfConditionNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 条件配置
		std::string m_ParamName;
		CompareOp   m_CompareOp    = CompareOp::Greater;
		float       m_FloatVal     = 0.0f;
		bool        m_BoolVal      = false;
		int         m_IntVal       = 0;
	};

	// ─── SwitchNode ─────────────────────────────────────────────
	//  多路选择节点。
	//  根据 int 参数值选择 N 路输入 Pose 之一。
	//  Input 0 ~ Input (N-1): 各路 Pose
	//  参数值超出范围时 clamp 到 [0, N-1]。

	class AnimGraphSwitchNode : public VansAnimGraphNode
	{
	public:
		AnimGraphSwitchNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::string m_ParamName;      // 驱动选择的 Int 参数名
		int         m_CaseCount = 2;  // 输入 Pose 数量
	};

	// ─── AdditiveBlendNode ──────────────────────────────────────
	//  叠加混合节点。
	//  Input 0: Base Pose
	//  Input 1: Additive Pose（叠加层）
	//  weight 控制叠加强度（0.0=纯 base, 1.0=完全叠加）

	class AnimGraphAdditiveBlendNode : public VansAnimGraphNode
	{
	public:
		AnimGraphAdditiveBlendNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::string m_ParamName;        // 驱动 weight 的参数名（Float 类型）
		float       m_FixedWeight = 1.0f;
		bool        m_UseParam    = false;
	};

	// ─── SpeedScaleNode ─────────────────────────────────────────
	//  速度缩放节点：对下游节点的 AdvanceTime 施加速度倍率。
	//  Input 0: Pose（传递求值）
	//  速度由参数驱动或固定值。

	class AnimGraphSpeedScaleNode : public VansAnimGraphNode
	{
	public:
		AnimGraphSpeedScaleNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::string m_ParamName;
		float       m_FixedSpeed = 1.0f;
		bool        m_UseParam   = false;
	};

	// ─── StateMachineNode ───────────────────────────────────────
	//  嵌入式状态机节点：复用现有 FSM 逻辑（States + Transitions）。
	//  每个 State 内部引用一个 Clip，状态之间按条件过渡。
	//  Output 一个混合后的 Pose。
	//  适配旧有 .vanimator 数据。

	class AnimGraphStateMachineNode : public VansAnimGraphNode
	{
	public:
		AnimGraphStateMachineNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;
		void AdvanceTime(float dt) override;
		void Reset() override;

		// State Machine 配置（从 .vanimator 中的 states/transitions 加载）
		std::vector<AnimatorState>      m_States;
		std::vector<AnimatorTransition> m_Transitions;
		std::string                     m_DefaultStateName;

		// 运行时状态
		std::string          m_CurrentStateName;
		std::string          m_PrevStateName;
		float                m_BlendAlpha    = 0.0f;
		float                m_BlendDuration = 0.0f;
		ControllerBlendState m_BlendState    = ControllerBlendState::Idle;

	private:
		void EvaluateTransitions(const AnimGraphContext& ctx);
		bool CheckConditions(const AnimatorTransition& trans,
		                     const AnimGraphContext& ctx) const;
		void StartTransition(const AnimatorTransition& trans);
		AnimGraphPose ComputeStatePose(const AnimatorState& state,
		                               const AnimGraphContext& ctx);
		AnimatorState* GetState(const std::string& name);

		void InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
		                          float time,
		                          glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);
	};

	// ─── IKNode ─────────────────────────────────────────────────
	//  通用 IK 节点：使用配置好的 IKChainDefinition + 求解器
	//  Input 0: Pose (上游动画)
	//  Output 0: Pose (IK 修正后)
	//  目标位置/旋转通过 Vector3/Quaternion 参数驱动。

	class AnimGraphIKNode : public VansAnimGraphNode
	{
	public:
		AnimGraphIKNode();
		~AnimGraphIKNode() override;
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// IK 链配置
		IKChainDefinition m_Chain;

		// 目标驱动参数名（必须为 Vector3/Quaternion 类型）
		std::string m_TargetPosParamName;
		std::string m_TargetRotParamName;
		std::string m_WeightParamName;       // Float 类型，0~1

		// 当未指定参数时使用的固定目标
		bool        m_UseFixedTarget = false;
		glm::vec3   m_FixedTargetPos = glm::vec3(0.0f);
		glm::quat   m_FixedTargetRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		float       m_FixedWeight    = 1.0f;

	private:
		std::unique_ptr<VansIKSolver> m_Solver;
		IKSolverType                  m_SolverKind = IKSolverType::CCD;

		void EnsureSolver();
	};

	// ─── TwoBoneIKNode ──────────────────────────────────────────
	//  人体四肢专用快捷节点：root + mid + tip 三骨骼
	//  内部构建 IKChainDefinition + CCDSolver

	class AnimGraphTwoBoneIKNode : public VansAnimGraphNode
	{
	public:
		AnimGraphTwoBoneIKNode();
		~AnimGraphTwoBoneIKNode() override;
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::string m_RootBoneName;     // 肩 / 髋
		std::string m_MidBoneName;      // 肘 / 膝
		std::string m_TipBoneName;      // 手 / 脚
		bool        m_UseLegProfile  = false;
		bool        m_IsRightSide    = true;
		float       m_HingeMinAngle  = 0.0f;
		float       m_HingeMaxAngle  = 150.0f;
		float       m_ConeAngle      = 60.0f;

		bool        m_UsePoleVector  = false;
		glm::vec3   m_PoleVector     = glm::vec3(0.0f, 0.0f, -1.0f);
		float       m_PoleWeight     = 1.0f;

		std::string m_TargetPosParamName;
		std::string m_TargetRotParamName;
		std::string m_WeightParamName;
		bool        m_UseFixedTarget = false;
		glm::vec3   m_FixedTargetPos = glm::vec3(0.0f);
		glm::quat   m_FixedTargetRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		float       m_FixedWeight    = 1.0f;
		bool        m_EnableRotationTarget = false;
		float       m_RotationWeight = 1.0f;

	private:
		std::unique_ptr<VansIKSolver> m_Solver;
		IKChainDefinition             m_CachedChain;
		bool                          m_ChainBuilt = false;

		void BuildChain(const Skeleton& skeleton);
	};

	// ─── LookAtNode ─────────────────────────────────────────────
	//  让一根或多根骨骼朝向目标点。

	class AnimGraphLookAtNode : public VansAnimGraphNode
	{
	public:
		AnimGraphLookAtNode();
		~AnimGraphLookAtNode() override;
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraph& graph) override;

		// 配置
		std::vector<std::string> m_BoneNames;     // 从根到末端
		std::vector<float>       m_BoneWeights;
		float                    m_MaxAnglePerBoneDeg = 80.0f;
		glm::vec3                m_ForwardAxis = glm::vec3(0.0f, 0.0f, -1.0f);
		// 角色 root-local 参考前向；非零时优先于 m_ForwardAxis（按绑定姿态自动推导每骨 local 轴）
		glm::vec3                m_WorldForward = glm::vec3(0.0f);

		std::string m_TargetPosParamName;
		std::string m_WeightParamName;
		bool        m_UseFixedTarget = false;
		glm::vec3   m_FixedTargetPos = glm::vec3(0.0f);
		float       m_FixedWeight    = 1.0f;

	private:
		std::unique_ptr<VansIKSolver> m_Solver;
		IKChainDefinition             m_CachedChain;
		bool                          m_ChainBuilt = false;

		void BuildChain(const Skeleton& skeleton);
	};

	// ═════════════════════════════════════════════════════════════
	//  VansAnimGraph — 动画逻辑图
	// ═════════════════════════════════════════════════════════════

	class VansAnimGraph
	{
	public:
		VansAnimGraph();
		~VansAnimGraph();

		// ─── 构建 ────────────────────────────────────────────────
		int  AddNode(std::unique_ptr<VansAnimGraphNode> node);
		void RemoveNode(int nodeId);
		VansAnimGraphNode* GetNode(int nodeId) const;

		int  AddLink(int fromNodeId, int fromPinIndex, int toNodeId, int toPinIndex);
		void RemoveLink(int linkId);
		const std::vector<AnimGraphLink>& GetLinks() const { return m_Links; }

		// ─── 求值（每帧调用）───────────────────────────────────
		//  从 OutputNode 开始 pull 求值，返回最终 Pose
		AnimGraphPose Evaluate(const AnimGraphContext& ctx);

		// 推进所有节点的内部时间
		void AdvanceTime(float dt);

		// 重置所有节点的运行时状态
		void ResetAll();

		// ─── 查询 ────────────────────────────────────────────────
		int GetEntryNodeId()  const { return m_EntryNodeId; }
		int GetOutputNodeId() const { return m_OutputNodeId; }

		// 获取连接到某节点某个输入 Pin 的上游节点
		VansAnimGraphNode* GetInputNode(int nodeId, int inputPinIndex) const;

		// 获取所有节点
		const std::unordered_map<int, std::unique_ptr<VansAnimGraphNode>>& GetNodes() const
		{
			return m_Nodes;
		}

		// ─── 序列化 ─────────────────────────────────────────────
		std::string SerializeToJson() const;
		static std::unique_ptr<VansAnimGraph> DeserializeFromJson(const std::string& jsonStr);

		// 直接操作 nlohmann::json 的版本（供 VansAnimatorIO 使用）
		void SerializeToJsonObject(nlohmann::json& outJson) const;
		static std::unique_ptr<VansAnimGraph> DeserializeFromJsonObject(const nlohmann::json& j);

		// ─── 工厂辅助 ───────────────────────────────────────────
		// 根据类型名创建空节点实例
		static std::unique_ptr<VansAnimGraphNode> CreateNodeByType(AnimGraphNodeType type);
		static std::unique_ptr<VansAnimGraphNode> CreateNodeByTypeName(const std::string& typeName);

	private:
		std::unordered_map<int, std::unique_ptr<VansAnimGraphNode>> m_Nodes;
		std::vector<AnimGraphLink> m_Links;

		int m_EntryNodeId  = -1;
		int m_OutputNodeId = -1;
		int m_NextNodeId   = 1;
		int m_NextLinkId   = 1;
	};

}  // namespace VansGraphics
