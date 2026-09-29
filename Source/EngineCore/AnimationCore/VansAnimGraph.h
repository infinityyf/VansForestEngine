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
#include "VansInertialization.h"
#include "VansAnimationController.h"
#include "VansAnimGraphNodeType.h"
#include "Procedural/Grounding/VansGroundingTypes.h"
#include "Procedural/Solvers/VansAimConstraintSolver.h"
#include "Procedural/Solvers/VansChainIKSolver.h"
#include "Procedural/Solvers/VansLimbIKSolver.h"
#include "../RuntimeCore/VansCharacterMotion.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace VansGraphics
{
	// 前向声明
	struct AnimatorParameter;
	class VansAnimGraph;
	class VansAnimGraphInstance;
	class VansAnimationController;
	class VansMotionMatchingRuntime;
	class AnimGraphStateMachineNode;
	struct VansAnimGraphClipRuntimeState;
	struct VansAnimGraphStateMachineRuntimeState;

	// Narrow graph capability for Motion Matching. Graph nodes can request a
	// pose without acquiring the mutable runtime or its database/debug controls.
	class VansAnimGraphMotionMatchingPort
	{
	public:
		bool Evaluate(
			float deltaTime,
			const Skeleton& skeleton,
			const std::unordered_map<std::string, VansAnimationClip>& clips,
			const std::unordered_map<std::string, AnimatorParameter>& parameters,
			const Vans::VansCharacterTrajectory* trajectory,
			VansPosePayload& outPayload) const;

	private:
		explicit VansAnimGraphMotionMatchingPort(VansMotionMatchingRuntime* runtime)
			: m_Runtime(runtime) {}

		VansMotionMatchingRuntime* m_Runtime = nullptr;
		friend class VansAnimationController;
	};

	// ─────────────────────────────────────────────────────────────
	//  节点类型枚举
	// ─────────────────────────────────────────────────────────────

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
		// Frame time before SpeedScale: input filters and authored transition
		// durations advance in real graph-update time, not clip playback time.
		float                                                      inputDeltaTime = -1.0f;
		bool stagedPlayback = false;
		bool preparePlayback = false;
		const Skeleton*                                            skeleton   = nullptr;
		std::unordered_map<std::string, AnimatorParameter>*        parameters = nullptr;
		const std::unordered_map<std::string, VansAnimationClip>*  clips      = nullptr;
		const VansAnimGraphMotionMatchingPort*                     motionMatching = nullptr;
		const Vans::VansCharacterTrajectory*                       characterTrajectory = nullptr;
		const std::unordered_map<std::string, VansSlotPoseInputs>*     slotPayloads = nullptr;
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
		VansAnimGraphNodeType  GetType()    const { return m_Type; }
		const std::string& GetName()    const { return m_Name; }
		void               SetName(const std::string& name) { m_Name = name; }

		// Editor-only authoring layout; runtime evaluation never reads it.
		VansAnimGraphNodeLayout m_EditorLayout;

		// 获取该节点的所有 Pin 定义
		virtual std::vector<AnimGraphPin> GetPins() const = 0;

		// 求值：递归拉取输入，计算输出 Pose
		virtual AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                               VansAnimGraphInstance& instance) const = 0;

		// 获取节点类型名称字符串（序列化用）
		static const char* TypeToString(VansAnimGraphNodeType type);

	protected:
		static AnimGraphPose EvaluateInputPose(
			VansAnimGraphInstance& instance,
			int nodeId,
			int inputPinIndex,
			const AnimGraphContext& ctx, float weight = 1.0f);
		static AnimGraphPose EvaluateNodePose(
			VansAnimGraphInstance& instance,
			int nodeId,
			const AnimGraphContext& ctx, int parentId = -1, float weight = 1.0f);
		static VansAnimGraphClipRuntimeState& ResolveClipState(
			VansAnimGraphInstance& instance, int nodeId);
		static VansAnimGraphStateMachineRuntimeState& ResolveStateMachineState(
			VansAnimGraphInstance& instance,
			int nodeId,
			const AnimGraphStateMachineNode& definition);
		static void StoreCachedPose(
			VansAnimGraphInstance& instance,
			const std::string& name,
			const AnimGraphPose& pose);
		static const AnimGraphPose* ResolveCachedPose(
			const VansAnimGraphInstance& instance,
			const std::string& name);
		static AnimGraphPose AppendProceduralPose(
			VansAnimGraphInstance& instance,
			int nodeId,
			const AnimGraphContext& ctx);

		int               m_NodeId = -1;
		std::string       m_Name;
		VansAnimGraphNodeType m_Type   = VansAnimGraphNodeType::Entry;

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
		std::string m_SyncGroup;
		float       m_Speed     = 1.0f;
		bool        m_Loop      = true;
		// Bool 参数控制当前轮是否允许继续循环；关闭时从当前相位播到端点。
		std::string m_LoopParameter;
		bool ResolveLoop(const AnimGraphContext& ctx) const;
		void AdvancePlayback(VansAnimGraphClipRuntimeState& clock, float deltaTime,
		                     const AnimGraphContext& ctx, bool normalizeTime = false) const;
		// 非空时按参数指定的秒数采样；不推进时钟，也不产生穿越事件或根运动。
		std::string m_SampleTimeParameter;
		float m_SampleTime = -1.0f;
		float m_StartPosition = 0.0f; // 只决定初始化位置，不裁剪播放区间。
		bool HasExplicitSampleTime() const { return m_SampleTime >= 0 || !m_SampleTimeParameter.empty(); }
		float m_AdditiveReferenceTime = -1.0f;
		std::string m_AdditiveReferenceClip;
		VansAdditivePoseMode m_AdditiveMode = VansAdditivePoseMode::LocalSpherical;
		// Animation assets can retain extracted root tracks while a graph node
		// decides whether this pose is allowed to drive the owner.  Locomotion
		// samples therefore keep their authored tracks available for actions but
		// explicitly suppress them at the node boundary.
		bool        m_RootMotion = true;

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
		bool m_MapAlpha = false, m_InterpolateAlpha = false;
		bool m_LinearRotationBlend = false;
		float m_AlphaInMin = 0, m_AlphaInMax = 1, m_AlphaOutMin = 0, m_AlphaOutMax = 1;
		float m_AlphaSpeedIncreasing = 0, m_AlphaSpeedDecreasing = 0;
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

	// ─── BlendSpace2DNode ───────────────────────────────────────
	//  2D 混合空间：根据两个 float 参数在采样点构成的三角形内插值。
	//  每个采样点对应一个输入 Pose Pin，采样点顺序与 Pins 保持一致。
	//  参数落在采样网格外时，使用最近的三个采样点进行稳定的距离权重退化。
	struct AnimGraphBlendSpaceSample
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct VansBlendSpaceGridInfluence
	{
		int sampleIndex = -1;
		float weight = 0;
	};
	struct VansBlendSpaceSampleGrid
	{
		int columns = 0, rows = 0;
		float minX = 0, maxX = 1, minY = 0, maxY = 1;
		std::vector<std::vector<VansBlendSpaceGridInfluence>> cells;
		bool IsEnabled() const {return columns!=0 || rows!=0 || !cells.empty();}
	};

	class AnimGraphBlendSpace2DNode : public VansAnimGraphNode
	{
	public:
		AnimGraphBlendSpace2DNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;

		std::string m_XParamName;
		std::string m_YParamName;
		bool m_BilinearGrid = false;
		float m_CubicFilterWindowX = 0.0f;
		float m_CubicFilterWindowY = 0.0f;
		// 同步模式由混合空间统一管理直接 Clip 输入的播放区间。
		bool m_SynchronizeSamples = false;
		std::string m_SyncGroup;
		float m_StartPosition = 0.0f;
		std::vector<AnimGraphBlendSpaceSample> m_Samples;
		VansBlendSpaceSampleGrid m_SampleGrid;
		bool ValidateSampleGrid(std::string& error) const;
	private:
		bool SynchronizeSampleTimes(const AnimGraphContext& ctx, VansAnimGraphInstance& instance,
			const VansAnimationFrameVector<float>& weights, bool allowMarkers = true) const;
		friend class VansAnimGraphInstance;
	};

	// Independent normalized pose weights; rotations accumulate before normalization.
	class AnimGraphMultiWayBlendNode : public VansAnimGraphNode
	{
	public:
		AnimGraphMultiWayBlendNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;
		std::vector<std::string> m_WeightParameters;
	};

	struct VansAnimGraphCurveValue
	{
		std::string name;
		float value = 0;
		std::string parameter;
	};

	// 只修改姿态负载中的命名曲线，骨骼、事件和根运动保持输入值。
	class AnimGraphModifyCurveNode : public VansAnimGraphNode
	{
	public:
		AnimGraphModifyCurveNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		VansCurveModifyMode m_Mode = VansCurveModifyMode::Blend;
		float m_Alpha = 1;
		std::string m_AlphaParameter;
		std::vector<VansAnimGraphCurveValue> m_Curves;
	};

	// Multiply one bone's component-space scale, then blend the component
	// transform by alpha before converting it back to its parent-local pose.
	class AnimGraphComponentBoneScaleNode : public VansAnimGraphNode
	{
	public:
		AnimGraphComponentBoneScaleNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::string m_BoneName;
		glm::vec3 m_Scale{1.0f};
		float m_Alpha = 1.0f;
		std::string m_AlphaParameter;
	};

	// Parameter-driven component/world-space bone translation and rotation.
	// Runs in the procedural phase so an upstream PoseCheckpoint can preserve
	// a target before this node changes the working pose.
	class AnimGraphComponentBoneTransformNode : public VansAnimGraphNode
	{
	public:
		AnimGraphComponentBoneTransformNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::string m_BoneName;
		std::string m_PositionParameter;
		std::string m_RotationParameter;
		std::string m_AlphaParameter;
		float m_Alpha = 1.0f;
		bool m_PositionAdditive = false;
		bool m_RotationAdditive = false;
		bool m_WorldSpace = false;
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
		VansGraphics::VansAdditivePoseMode m_AdditiveMode = VansGraphics::VansAdditivePoseMode::LocalSpherical;
		std::string m_ParamName;        // 驱动 weight 的参数名（Float 类型）
		float       m_FixedWeight = 1.0f;
		bool        m_UseParam    = false;
	};

	// ─── SpeedScaleNode ─────────────────────────────────────────
	//  速度缩放节点：对下游节点的 AdvanceTime 施加速度倍率。
	//  Input 0: Pose（传递求值）
	//  速度由参数驱动或固定值。

	class AnimGraphInertializationNode : public VansAnimGraphNode
	{
	public:
		AnimGraphInertializationNode();
		float m_TeleportDistance = 3.0f;
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
	};

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
		bool m_SkipFirstUpdateTransition = false;
		int m_MaxTransitionsPerFrame = 1;
		bool m_LinearRotationBlend = false;

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
		static AnimGraphPose BlendInputs(const AnimGraphPose& fallback,
			const VansSlotPoseInputs* inputs, const Skeleton* skeleton,
			bool linearRotationBlend);

		std::string m_SlotId;
		bool m_EnableFallbackInput = true;
		bool m_LinearRotationBlend = false;
	};

	class AnimGraphSaveCachedPoseNode : public VansAnimGraphNode
	{
	public:
		AnimGraphSaveCachedPoseNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;
		std::string m_CacheName;
	};

	class AnimGraphUseCachedPoseNode : public VansAnimGraphNode
	{
	public:
		AnimGraphUseCachedPoseNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;
		std::string m_CacheName;
	};

	class AnimGraphLayeredBlendPerBoneNode : public VansAnimGraphNode
	{
	public:
		AnimGraphLayeredBlendPerBoneNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;
		VansBoneMaskAsset m_Mask;
		VansLayerBlendMode m_BlendMode = VansLayerBlendMode::Override;
		VansRotationBlendSpace m_RotationSpace = VansRotationBlendSpace::Mesh;
		// 仅旋转在模型空间做线性四元数混合，平移与缩放仍在局部空间。
		bool m_MeshSpaceRotationOnly = false;
		std::string m_WeightParameter;
		float m_FixedWeight = 1.0f;
		bool m_UseWeightParameter = false;
		bool m_ApplyAdditiveInput = false;
	};

	// The only legal pose source for a Target Post Process Graph. It makes the
	// execution boundary explicit: Layer composition or Retarget produces the
	// input payload; the Target Procedural Graph then schedules Goal, Grounding,
	// Limb/Chain IK, and Aim nodes against the target skeleton.
	class AnimGraphTargetPoseInputNode : public VansAnimGraphNode
	{
	public:
		AnimGraphTargetPoseInputNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx,
		                       VansAnimGraphInstance& instance) const override;
	};

	enum class VansGraphGoalSource { Binding, Parameters, Fixed, PoseBone };

	struct VansGraphGoalDefinition
	{
		std::string goalId;
		VansGraphGoalSource source = VansGraphGoalSource::Binding;
		std::string binding;
		std::string boneName;
		std::string poleBoneName;
		glm::vec3 poleOffsetLocal{ 0.0f };
		std::string positionParameter;
		std::string rotationParameter;
		std::string weightParameter;
		std::string weightCurve;
		std::string offsetWeightCurve;
		glm::vec3 fixedPositionModel{ 0.0f };
		glm::quat fixedRotationModel{ 1.0f, 0.0f, 0.0f, 0.0f };
		float fixedPositionWeight = 1.0f;
		float fixedRotationWeight = 0.0f;
	};

	class AnimGraphPoseCheckpointNode : public VansAnimGraphNode
	{
	public:
		AnimGraphPoseCheckpointNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::string m_CheckpointId;
		std::vector<std::string> m_Bones;
	};

	class AnimGraphGoalNode : public VansAnimGraphNode
	{
	public:
		AnimGraphGoalNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		VansGraphGoalDefinition m_Goal;
	};

	class AnimGraphAimConstraintNode : public VansAnimGraphNode
	{
	public:
		AnimGraphAimConstraintNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::string m_ChainId;
		VansGraphGoalDefinition m_Target;
		std::string m_DirectionParameter;
		std::string m_DirectionWeightParameter;
		bool m_DirectionIsWorldSpace = true;
		std::string m_PivotBone;
		VansAimConstraintSettings m_Settings;
		float m_TargetHalfLife = 0.08f;
	};

	class AnimGraphGroundingNode : public VansAnimGraphNode
	{
	public:
		AnimGraphGroundingNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		VansGroundingSettings m_Settings;
	};

	class AnimGraphLimbIKNode : public VansAnimGraphNode
	{
	public:
		AnimGraphLimbIKNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::vector<std::string> m_ChainIds;
		VansLimbIKSettings m_Settings;
	};

	class AnimGraphRotationDistributionNode : public VansAnimGraphNode
	{
	public:
		AnimGraphRotationDistributionNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::string m_RotationProfileId;
	};

	class AnimGraphChainIKNode : public VansAnimGraphNode
	{
	public:
		AnimGraphChainIKNode();
		std::vector<AnimGraphPin> GetPins() const override;
		AnimGraphPose Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const override;
		std::vector<std::string> m_ChainIds;
		VansChainIKSettings m_Settings;
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
		// Typed definition copy used when one authored graph needs an independent
		// runtime owner. This deliberately bypasses the persistence codec.
		std::unique_ptr<VansAnimGraph> Clone() const;

		// 获取所有节点
		const std::unordered_map<int, std::unique_ptr<VansAnimGraphNode>>& GetNodes() const
		{
			return m_Nodes;
		}

		// ─── 序列化 ─────────────────────────────────────────────
		// Canonical JSON codec used by VansAnimatorIO.
		void SerializeToJsonObject(nlohmann::json& outJson) const;
		static std::unique_ptr<VansAnimGraph> DeserializeFromJsonObject(const nlohmann::json& j);

		// ─── 工厂辅助 ───────────────────────────────────────────
		// 根据类型名创建空节点实例
		static std::unique_ptr<VansAnimGraphNode> CreateNodeByType(VansAnimGraphNodeType type);
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
		bool looping = true;
		bool startPositionPending = false;
	};

	struct VansAnimGraphTransitionRuntimeState
	{
		int definitionIndex = -1;
		std::string previousStateName, nextStateName;
		float elapsedTime = 0.0f, duration = 0.0f, alpha = 0.0f;
		bool linearRotationBlend = false;
		std::vector<AnimatorTransitionCurveKey> blendCurve;
		std::unordered_map<std::string, float> boneBlendFactors;
	};

	struct VansAnimGraphStateMachineRuntimeState
	{
		bool firstUpdate = true;
		float recordedWeight = 0.0f;
		float currentStateElapsedTime = 0.0f;
		std::unordered_map<std::string, float> relevantClipRemaining;
		std::string currentStateName;
		std::vector<VansAnimGraphTransitionRuntimeState> activeTransitions;
		std::vector<float> inertialRequests;
		float GetStateWeight(const std::string& name) const;
		std::unordered_map<std::string, float> stateTimes;
		std::unordered_map<std::string, float> previousStateTimes;
	};

	struct VansAnimGraphInputFilterState
	{
		struct Sample { float value = 0.0f, time = 0.0f; };
		float window = 0.0f, time = 0.0f, output = 0.0f;
		std::size_t writeIndex = 0;
		std::vector<Sample> samples;
		float Update(float input, float deltaTime, float duration);
	};

	struct VansAnimGraphBlendSpaceRuntimeState
	{
		VansAnimGraphInputFilterState x, y;
		bool playbackInitialized = false;
		float normalizedTime = 0.0f;
		int markerLeader = -1;
		std::vector<int> activeSamples;
	};

	struct VansAnimGraphBlendAlphaState
	{
		bool initialized = false;
		float value = 0;
	};

	struct VansAnimGraphSyncGroupState
	{
		int leaderNode = -1;
		std::string leaderClip;
		float previousTime = 0, currentTime = 0;
	};

	struct VansAnimGraphRuntimeStateSnapshot
	{
		std::unordered_map<int, VansAnimGraphClipRuntimeState> clipStates;
		std::unordered_map<int, VansAnimGraphStateMachineRuntimeState> stateMachineStates;
		std::unordered_map<int, VansAnimGraphBlendSpaceRuntimeState> blendSpaceStates;
		std::unordered_map<int, VansAnimGraphBlendAlphaState> blendAlphaStates;
		std::unordered_map<int, VansInertializationState> inertializationStates;
		std::unordered_map<int, bool> activeNodes;
		std::unordered_set<int> initializedNodes;
		std::unordered_set<int> updatedNodes;
		std::unordered_map<std::string, VansAnimGraphSyncGroupState> syncGroups;
	};

	// 可变播放状态、活动节点和帧缓存只属于实例；VansAnimGraph 保持定义数据。
	class VansAnimGraphInstance
	{
	public:
		explicit VansAnimGraphInstance(const VansAnimGraph& definition);
		~VansAnimGraphInstance();

		const VansAnimGraph& GetDefinition() const { return m_Definition; }
		bool IsCompiled() const { return m_CompileError.empty(); }
		void QueueStateMachineEvent(int nodeId, std::string_view name);
		const std::string& GetCompileError() const { return m_CompileError; }
		const std::vector<int>& GetExecutionPlan() const { return m_ExecutionPlan; }

		AnimGraphPose Evaluate(const AnimGraphContext& ctx);
		AnimGraphPose EvaluateFrame(const AnimGraphContext& ctx);
		void Reset();
		bool PlayState(const std::string& stateName);
		std::string GetCurrentStateName() const;
		// Returns every active embedded state machine in execution order. This
		// lets debug/UI consumers distinguish grounded and airborne FSMs without
		// guessing which one a graph exposes as its primary state.
		std::string GetActiveStatePath() const;
		float GetPrimaryPlaybackTime() const;
		const std::string& GetPrimaryClipName() const;
		bool SetPrimaryPlaybackTime(float time, const std::string& stateName = {});
		const std::unordered_map<std::string, VansAnimGraphSyncGroupState>& GetActiveSyncGroups() const
		{ return m_SyncGroups; }
		void SetExternalSyncGroups(const std::unordered_map<std::string, VansAnimGraphSyncGroupState>& groups)
		{ m_ExternalSyncGroups = groups; }
		bool SynchronizePrimaryStateMachineFrom(
			const VansAnimGraphInstance& leader,
			const std::unordered_map<std::string, VansAnimationClip>& clips);
		VansAnimGraphRuntimeStateSnapshot CaptureRuntimeState() const;
		bool RestoreRuntimeState(const VansAnimGraphRuntimeStateSnapshot& snapshot);

	private:
		AnimGraphPose EvaluateNode(int nodeId, const AnimGraphContext& ctx);
		AnimGraphPose EvaluateInput(int nodeId, int inputPinIndex, const AnimGraphContext& ctx, float weight = 1.0f);
		AnimGraphPose EvaluateWeightedNode(int parentId, int nodeId, float weight, const AnimGraphContext& ctx);
		void TickSyncGroups(const AnimGraphContext& ctx);
		void RouteInertializationRequests();
		void InitializeSubgraph(int nodeId);
		void AdvanceTime(float deltaTime, const AnimGraphContext& ctx);
		void SetCachedPose(const std::string& name, const AnimGraphPose& pose);
		const AnimGraphPose* FindCachedPose(const std::string& name) const;
		VansAnimGraphClipRuntimeState& GetClipState(int nodeId);
		VansAnimGraphStateMachineRuntimeState& GetStateMachineState(
			int nodeId, const AnimGraphStateMachineNode& definition);

		struct VansLayeredBlendRuntimeState
		{
			std::uint64_t skeletonSignature = 0;
			VansCompiledBoneMask mask;
			VansAnimationFrameVector<VansBoneTransform> bindPose{
				std::pmr::new_delete_resource() };
			bool initialized = false;
		};

		const VansLayeredBlendRuntimeState& ResolveLayeredBlendRuntime(
			int nodeId, const VansBoneMaskAsset& mask, const Skeleton& skeleton);
		friend class AnimGraphLayeredBlendPerBoneNode;
		friend class AnimGraphBlendSpace2DNode;
		friend class AnimGraphBlendNode;
		friend class AnimGraphStateMachineNode;
		friend class AnimGraphInertializationNode;
		friend class VansAnimGraphNode;
		friend class VansAnimationController;

		const VansAnimGraph& m_Definition;
		std::vector<int> m_ExecutionPlan;
		std::string m_CompileError;
		std::unordered_map<int, VansAnimGraphClipRuntimeState> m_ClipStates;
		std::unordered_map<int, VansAnimGraphStateMachineRuntimeState> m_StateMachineStates;
		std::unordered_map<int, VansAnimGraphBlendSpaceRuntimeState> m_BlendSpaceStates;
		std::unordered_map<int, VansAnimGraphBlendAlphaState> m_BlendAlphaStates;
		std::unordered_map<int, VansInertializationState> m_InertializationStates;
		std::unordered_map<int, int> m_SynchronizedSampleOwners;
		bool m_UsesStagedPlayback = false;
		struct PlaybackEdge { int child; float weight; };
		std::unordered_map<int, std::vector<PlaybackEdge>> m_PlaybackEdges;
		std::unordered_map<int, float> m_PlaybackWeights;
		std::unordered_map<int, float> m_PlayerDeltaTimes;
		std::unordered_map<int, std::vector<float>> m_PreparedSampleWeights;
		std::unordered_map<std::string, VansAnimGraphSyncGroupState> m_SyncGroups;
		std::unordered_map<std::string, VansAnimGraphSyncGroupState> m_ExternalSyncGroups;
		std::unordered_map<int, bool> m_SyncFollowers;
		std::unordered_map<int, AnimGraphPose> m_EvaluationCache;
		VansAnimationFrameVector<VansAnimationEventSample> m_StateMachineEvents{std::pmr::new_delete_resource()};
		std::unordered_map<std::string, AnimGraphPose> m_CachedPoses;
		std::unordered_map<int, bool> m_EvaluatedNodes;
		std::unordered_map<int, bool> m_EvaluatingNodes;
		std::unordered_map<int, bool> m_PreviousActiveNodes;
		std::unordered_set<int> m_InitializedNodes;
		std::unordered_set<int> m_UpdatedNodes;
		std::unordered_set<int> m_InitializedThisFrame;
		std::unordered_map<int, float> m_ActiveTimeScales;
		std::unordered_map<int, bool> m_HasActiveTimeScale;
		std::unordered_map<int, VansLayeredBlendRuntimeState> m_LayeredBlendRuntimes;
	};

}  // namespace VansGraphics
