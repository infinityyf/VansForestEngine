#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansAnimGraph — 动画逻辑图系统
//
//  以有向无环图（DAG）描述动画的求值流程：
//    Entry → 各种逻辑/混合节点 → Output
//
//  Controller 每帧通过 Evaluate() 从 Output 节点向上游 pull 求值；
//  图级缓存保证共享子图每帧只求值一次，并对异常递归进行防护。
//  参数（float/bool/int/trigger）由 Controller 管理，作为 Context 传入。
// ────────────────────────────────────────────────────────────────────

#include "VansAnimationTypes.h"
#include "VansPoseTypes.h"
#include "VansAnimationController.h"
#include "VansAnimGraphJson.h"
#include "IK/VansIKTypes.h"
#include "FootPlacement/VansFootPlacementTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace VansGraphics
{
	// 前向声明
	struct AnimatorParameter;
	class VansAnimGraph;
	class VansAnimGraphInstance;
	class VansIKSolver;
	class VansMotionMatchingRuntime;

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
		AdditiveBlend,   // 叠加混合：将相对参考姿态的 local TRS delta 施加到 base
		SpeedScale,      // 播放速度缩放（套在 Clip 输入上）
		StateMachine,    // 嵌入式状态机节点
		MotionMatching,  // Motion Matching pose source with fallback input
		Slot,            // Gameplay one-shot source with optional fallback input
		TargetPoseInput, // Composed/retargeted target-skeleton pose entering a post-process Graph
		IK,              // 通用 IK 节点（CCD/FABRIK 求解人体或非关节链）
		TwoBoneIK,       // 双骨骼 IK 节点（人体四肢快捷配置）
		LookAt,          // 朝向/瞄准节点
		FootPlacement    // 足部贴地后处理请求（在 Root Motion 之后执行）
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
		std::unordered_map<std::string, AnimatorParameter>*        parameters = nullptr;
		const std::unordered_map<std::string, VansAnimationClip>*  clips      = nullptr;
		VansMotionMatchingRuntime*                                 motionMatching = nullptr;
		const std::unordered_map<std::string, VansPosePayload>*     slotPayloads = nullptr;
		const VansPosePayload*                                     targetPoseInput = nullptr;
		bool                                                       synchronizedStateFollower = false;
		glm::mat4                                                  ownerWorldTransform = glm::mat4(1.0f);
	};

	// Graph、State Machine、Motion Matching 与后续 Layer 统一传递正式 Payload。
	using AnimGraphPose = VansPosePayload;

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
		                               VansAnimGraphInstance& instance) const = 0;

		// 获取节点类型名称字符串（序列化用）
		static const char* TypeToString(AnimGraphNodeType type);

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
		                       VansAnimGraphInstance& instance) const override;
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
		                       VansAnimGraphInstance& instance) const override;
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
		                       VansAnimGraphInstance& instance) const override;

		// 配置
		std::string m_ClipName;
		float       m_Speed     = 1.0f;
		bool        m_Loop      = true;

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
		                       VansAnimGraphInstance& instance) const override;

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
		                       VansAnimGraphInstance& instance) const override;

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
		                       VansAnimGraphInstance& instance) const override;

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
		                       VansAnimGraphInstance& instance) const override;

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
		                       VansAnimGraphInstance& instance) const override;

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
		                       VansAnimGraphInstance& instance) const override;

		// 配置
		std::string m_ParamName;
		float       m_FixedSpeed = 1.0f;
		bool        m_UseParam   = false;
	};

	// ─── StateMachineNode ───────────────────────────────────────
	//  嵌入式状态机节点：复用现有 FSM 逻辑（States + Transitions）。
	//  每个 State 内部引用一个 Clip，状态之间按条件过渡。
	//  Output 一个混合后的 Pose。
	//  graph 节点是状态机配置的唯一事实源。

	class AnimGraphStateMachineNode : public VansAnimGraphNode
	{
	public:
		AnimGraphStateMachineNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;

		// State Machine 配置（从 graph 节点加载）
		std::vector<AnimatorState>      m_States;
		std::vector<AnimatorTransition> m_Transitions;
		std::string                     m_DefaultStateName;

	};

	class AnimGraphMotionMatchingNode : public VansAnimGraphNode
	{
	public:
		AnimGraphMotionMatchingNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;

		bool m_EnableFallbackInput = true;
	};

	class AnimGraphSlotNode : public VansAnimGraphNode
	{
	public:
		AnimGraphSlotNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;

		std::string m_SlotId;
		bool m_EnableFallbackInput = true;
	};

	// The only legal pose source for a Target Post Process Graph. It makes the
	// execution boundary explicit: Layer composition or Retarget produces the
	// input payload, and target-skeleton IK/LookAt/Foot Placement consume it.
	class AnimGraphTargetPoseInputNode : public VansAnimGraphNode
	{
	public:
		AnimGraphTargetPoseInputNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;
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
		                       VansAnimGraphInstance& instance) const override;

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
		IKCoordinateSpace m_TargetPositionSpace = IKCoordinateSpace::Model;
		IKCoordinateSpace m_TargetRotationSpace = IKCoordinateSpace::Model;
		std::string m_TargetReferenceBoneName;

	};

	// ─── TwoBoneIKNode ──────────────────────────────────────────
	//  人体四肢专用快捷节点：root + mid + tip 三骨骼
	//  内部构建 IKChainDefinition + analytic two-bone solver

	class AnimGraphTwoBoneIKNode : public VansAnimGraphNode
	{
	public:
		AnimGraphTwoBoneIKNode();
		~AnimGraphTwoBoneIKNode() override;
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;

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
		IKCoordinateSpace m_TargetPositionSpace = IKCoordinateSpace::Model;
		IKCoordinateSpace m_TargetRotationSpace = IKCoordinateSpace::Model;
		std::string m_TargetReferenceBoneName;
		IKCoordinateSpace m_PoleSpace = IKCoordinateSpace::Model;
		std::string m_PoleReferenceBoneName;
		bool        m_MaintainEffectorGlobalRotation = false;
		bool        m_AllowStretch = false;
		float       m_StartStretchRatio = 1.0f;
		float       m_MaxStretchScale = 1.2f;

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
		                       VansAnimGraphInstance& instance) const override;

		// 配置
		std::vector<std::string> m_BoneNames;     // 从根到末端
		std::vector<float>       m_BoneWeights;
		float                    m_MaxAnglePerBoneDeg = 80.0f;
		glm::vec3                m_ForwardAxis = glm::vec3(0.0f, 0.0f, -1.0f);
		// 角色 root-local 参考前向；非零时优先于 m_ForwardAxis（按绑定姿态自动推导每骨 local 轴）
		glm::vec3                m_WorldForward = glm::vec3(0.0f);
		glm::vec3                m_ModelUp = glm::vec3(0.0f, 1.0f, 0.0f);
		float                    m_UpWeight = 1.0f;

		std::string m_TargetPosParamName;
		std::string m_WeightParamName;
		bool        m_UseFixedTarget = false;
		glm::vec3   m_FixedTargetPos = glm::vec3(0.0f);
		float       m_FixedWeight    = 1.0f;
		IKCoordinateSpace m_TargetPositionSpace = IKCoordinateSpace::Model;
		std::string m_TargetReferenceBoneName;

	};

	// ─── FootPlacementNode ───────────────────────────────────────
	//  声明一个延迟执行的足部放置后处理。节点本身不进行物理查询；Controller
	//  会在骨骼覆盖、Root Motion 提取/归一化之后执行请求，保证管线顺序正确。

	class AnimGraphFootPlacementNode : public VansAnimGraphNode
	{
	public:
		AnimGraphFootPlacementNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;

		FootPlacementSettings m_Settings;
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
		// Authoring/import boundary: preserves the stable node identity stored in
		// the canonical document while keeping runtime construction encapsulated.
		bool AddNodeWithId(std::unique_ptr<VansAnimGraphNode> node, int nodeId);
		void RemoveNode(int nodeId);
		VansAnimGraphNode* GetNode(int nodeId);
		const VansAnimGraphNode* GetNode(int nodeId) const;

		int  AddLink(int fromNodeId, int fromPinIndex, int toNodeId, int toPinIndex);
		bool AddLinkWithId(int linkId, int fromNodeId, int fromPinIndex,
		                   int toNodeId, int toPinIndex);
		void RemoveLink(int linkId);
		const std::vector<AnimGraphLink>& GetLinks() const { return m_Links; }

		// ─── 查询 ────────────────────────────────────────────────
		int GetEntryNodeId()  const { return m_EntryNodeId; }
		int GetOutputNodeId() const { return m_OutputNodeId; }

		// 获取连接到某节点某个输入 Pin 的上游节点
		const VansAnimGraphNode* GetInputNode(int nodeId, int inputPinIndex) const;

		// 生成只包含 Output 可达节点的确定性拓扑执行计划。
		bool BuildExecutionPlan(std::vector<int>& outPlan, std::string& outError) const;

		// 获取所有节点
		const std::unordered_map<int, std::unique_ptr<VansAnimGraphNode>>& GetNodes() const
		{
			return m_Nodes;
		}

		// ─── 序列化 ─────────────────────────────────────────────
		// Canonical JSON codec used by VansAnimatorIO.
		void SerializeToJsonObject(AnimGraphJson& outJson) const;
		static std::unique_ptr<VansAnimGraph> DeserializeFromJsonObject(const AnimGraphJson& j);

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

	struct VansAnimGraphClipRuntimeState
	{
		float previousTime = 0.0f;
		float currentTime = 0.0f;
	};

	struct VansAnimGraphStateMachineRuntimeState
	{
		std::string currentStateName;
		std::string previousStateName;
		float blendAlpha = 0.0f;
		float blendDuration = 0.0f;
		ControllerBlendState blendState = ControllerBlendState::Idle;
		std::unordered_map<std::string, float> stateTimes;
		std::unordered_map<std::string, float> previousStateTimes;
	};

	struct VansAnimGraphRuntimeStateSnapshot
	{
		std::unordered_map<int, VansAnimGraphClipRuntimeState> clipStates;
		std::unordered_map<int, VansAnimGraphStateMachineRuntimeState> stateMachineStates;
	};

	struct VansAnimGraphIKRuntimeState
	{
		const Skeleton* skeleton = nullptr;
		IKChainDefinition chain;
		int targetReferenceBoneIndex = -1;
		std::vector<glm::mat4> localMatrices;
		std::vector<glm::mat4> globalMatrices;
	};

	// 可变播放状态、活动节点和帧缓存只属于实例；VansAnimGraph 保持定义数据。
	class VansAnimGraphInstance
	{
	public:
		explicit VansAnimGraphInstance(const VansAnimGraph& definition);
		~VansAnimGraphInstance();

		const VansAnimGraph& GetDefinition() const { return m_Definition; }
		bool IsCompiled() const { return m_CompileError.empty(); }
		const std::string& GetCompileError() const { return m_CompileError; }
		const std::vector<int>& GetExecutionPlan() const { return m_ExecutionPlan; }

		AnimGraphPose Evaluate(const AnimGraphContext& ctx);
		AnimGraphPose EvaluateNode(int nodeId, const AnimGraphContext& ctx);
		AnimGraphPose EvaluateInput(int nodeId, int inputPinIndex, const AnimGraphContext& ctx);
		void AdvanceTime(float deltaTime, const AnimGraphContext& ctx);
		void Reset();
		bool PlayState(const std::string& stateName);
		std::string GetCurrentStateName() const;
		float GetPrimaryPlaybackTime() const;
		const std::string& GetPrimaryClipName() const;
		bool SetPrimaryPlaybackTime(float time, const std::string& stateName = {});
		bool SynchronizePrimaryStateMachineFrom(
			const VansAnimGraphInstance& leader,
			const std::unordered_map<std::string, VansAnimationClip>& clips);
		VansAnimGraphRuntimeStateSnapshot CaptureRuntimeState() const;
		bool RestoreRuntimeState(const VansAnimGraphRuntimeStateSnapshot& snapshot);

		float GetClipTime(int nodeId) const;
		VansAnimGraphClipRuntimeState& GetClipState(int nodeId);
		VansAnimGraphStateMachineRuntimeState& GetStateMachineState(
			int nodeId, const AnimGraphStateMachineNode& definition);
		VansIKSolver* GetIKSolver(int nodeId, IKSolverType type);
		VansAnimGraphIKRuntimeState& GetIKRuntimeState(int nodeId);

	private:
		const VansAnimGraph& m_Definition;
		std::vector<int> m_ExecutionPlan;
		std::string m_CompileError;
		std::unordered_map<int, VansAnimGraphClipRuntimeState> m_ClipStates;
		std::unordered_map<int, VansAnimGraphStateMachineRuntimeState> m_StateMachineStates;
		std::unordered_map<int, std::unique_ptr<VansIKSolver>> m_IKSolverStates;
		std::unordered_map<int, IKSolverType> m_IKSolverTypes;
		std::unordered_map<int, VansAnimGraphIKRuntimeState> m_IKRuntimeStates;
		std::unordered_map<int, AnimGraphPose> m_EvaluationCache;
		std::unordered_map<int, bool> m_EvaluatedNodes;
		std::unordered_map<int, bool> m_EvaluatingNodes;
		std::unordered_map<int, bool> m_PreviousActiveNodes;
		std::unordered_map<int, float> m_ActiveTimeScales;
		std::unordered_map<int, bool> m_HasActiveTimeScale;
	};

}  // namespace VansGraphics
