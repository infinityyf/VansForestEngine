#pragma once

#include "VansAnimationTypes.h"
#include "VansAnimationLayer.h"
#include "VansPoseTypes.h"
#include "Procedural/VansProceduralGraphRuntime.h"
#include "Runtime/VansAnimationSlotRuntime.h"
#include "../RuntimeCore/VansCharacterMotion.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

namespace VansGraphics
{
	// 前向声明 (VansAnimGraph.h 包含本头文件，避免循环依赖)
	class VansAnimGraph;
	class VansAnimGraphInstance;
	class VansMotionMatchingRuntime;
	struct MotionMatchingSettings;
	struct MotionMatchingDebugData;

	struct VansAnimationLayerSetup
	{
		VansAnimationLayerDefinition definition;
		std::optional<VansBoneMaskAsset> mask;
	};

	struct VansAnimationGraphBindingSetup
	{
		VansAnimationGraphBindingDefinition definition;
		std::unique_ptr<VansAnimGraph> graph;
	};

	struct VansAnimationGraphSetSetup
	{
		VansAnimationGraphSetDefinition definition;
		std::vector<VansAnimationGraphBindingSetup> bindings;
	};

	enum class VansGraphSetSwitchResult
	{
		Started,
		Completed,
		AlreadyActive,
		Queued,
		Rejected,
		UnknownGraphSet,
		StateHandoffFailed
	};

	enum class VansExternalPoseEvaluationMode
	{
		DirectFinalPose,
		TargetPostProcess
	};

	// ─── 参数类型 ───

	enum class AnimatorParamType
	{
		Float,
		Bool,
		Int,
		Trigger,    // 一次性触发器，触发后自动重置为 false
		Vector3,    // 用于 IK 目标位置等
		Quaternion  // 用于 IK 目标旋转等
	};

	// ─── 参数 ───

	struct AnimatorParameter
	{
		std::string       name;
		AnimatorParamType type     = AnimatorParamType::Float;
		float             floatVal = 0.0f;
		bool              boolVal  = false;
		int               intVal   = 0;
		glm::vec3         vec3Val  = glm::vec3(0.0f);
		glm::quat         quatVal  = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		// Trigger 内部用 boolVal 表示是否已触发
	};

	// ─── 比较操作符 ───

	enum class CompareOp
	{
		Greater,       // >
		Less,          // <
		Equal,         // ==
		NotEqual,      // !=
		GreaterEqual,  // >=
		LessEqual      // <=
	};

	// ─── 过渡条件 ───

	struct TransitionCondition
	{
		std::string paramName;
		CompareOp   op       = CompareOp::Equal;
		float       floatVal = 0.0f;
		bool        boolVal  = false;
		int         intVal   = 0;
	};

	// ─── 状态（State）───

	struct AnimatorState
	{
		std::string        name;             // 状态名（唯一标识）
		std::string        clipName;         // 引用的 VansAnimationClip 名称
		float              speed       = 1.0f;
		bool               loop        = true;
		bool               rootMotion  = false;
		float              startTime   = 0.0f;
		float              endTime     = -1.0f;  // -1 = 完整 clip
	};

	// ─── 过渡（Transition）───

	struct AnimatorTransition
	{
		std::string fromState;          // 源状态名，"*" 表示 Any State
		std::string toState;            // 目标状态名

		float blendDuration = 0.2f;     // 混合时长（秒）
		bool  hasExitTime   = false;    // 是否等到 exitTime 才触发
		float exitTime      = 1.0f;     // 归一化时间 (0~1)

		// 所有条件必须同时满足才触发过渡（AND 逻辑）
		std::vector<TransitionCondition> conditions;
	};

	// ─── 控制器混合状态 ───

	enum class ControllerBlendState
	{
		Idle,       // 停在当前 state，无混合
		Blending    // 正在从 m_PrevState 过渡到 m_CurrentState
	};

	// ────────────────────────────────────────────────────────────────
	//  VansAnimationController
	//
	//  State-machine driven controller between VansAnimationNode and
	//  VansAnimationClip.  Manages clips, states, transitions, parameters,
	//  playback, blending, root motion, and outputs final bone matrices.
	// ────────────────────────────────────────────────────────────────

	class VansAnimationController
	{
	public:
		VansAnimationController();
		~VansAnimationController();

		// ─── 参数管理 ─────────────────────────────────────────────────
		void AddParameter(const std::string& name, AnimatorParamType type);
		void RemoveParameter(const std::string& name);
		bool HasParameter(const std::string& name) const;

		void SetFloat(const std::string& name, float value);
		void SetBool(const std::string& name, bool value);
		void SetInt(const std::string& name, int value);
		void SetTrigger(const std::string& name);
		void ResetTrigger(const std::string& name);
		void SetVector3(const std::string& name, const glm::vec3& value);
		void SetQuaternion(const std::string& name, const glm::quat& value);

		float GetFloat(const std::string& name) const;
		bool  GetBool(const std::string& name) const;
		int   GetInt(const std::string& name) const;
		bool  IsTriggerSet(const std::string& name) const;
		glm::vec3 GetVector3(const std::string& name) const;
		glm::quat GetQuaternion(const std::string& name) const;

		const std::unordered_map<std::string, AnimatorParameter>& GetParameters() const;

		// ─── Clip 管理（Controller 直接持有 clip 数据）────────────────
		void AddClip(const std::string& name, VansAnimationClip&& clip);
		void AddClip(const std::string& name, const VansAnimationClip& clip);
		void RemoveClip(const std::string& name);
		VansAnimationClip* GetClip(const std::string& name);
		const VansAnimationClip* GetClip(const std::string& name) const;
		const std::unordered_map<std::string, VansAnimationClip>& GetClipsMap() const;
		std::vector<std::string> GetClipNames() const;

		// ─── 播放控制 ──────────────────────────────────────────────────
		void Play();                                  // 从默认状态开始播放
		void Play(const std::string& stateName);      // 强制跳转到指定状态
		void Pause();
		void Resume();
		void Stop();
		void Reset();

		void SetSpeed(float speed);
		float GetSpeed() const;

		// ─── 状态查询 ──────────────────────────────────────────────────
		std::string GetCurrentStateName() const;
		AnimationState GetPlaybackState() const;
		float GetCurrentPlayTime() const;
		float GetCurrentDuration() const;
		float GetNormalizedTime() const;
		bool SeekNormalizedTime(float normalizedTime);

		struct LayerRuntimeDebugInfo
		{
			std::string id;
			std::string name;
			std::string state;
			std::string clip;
			float weight = 0.0f;
			float playbackTime = 0.0f;
			float normalizedTime = 0.0f;
			bool enabled = false;
			VansAnimationLayerKind kind = VansAnimationLayerKind::Base;
			VansLayerBlendMode blendMode = VansLayerBlendMode::Override;
			std::vector<float> boneWeights;
			float evaluationMilliseconds = 0.0f;
		};
		std::vector<LayerRuntimeDebugInfo> GetLayerRuntimeDebugInfo() const;
		void EnableDebugMetrics(bool enabled) { m_DebugMetricsEnabled = enabled; }

		// ─── Root Motion ──────────────────────────────────────────────
		void EnableRootMotion(bool enable);
		bool IsRootMotionEnabled() const;
		void SetRootMotionApplyToOwner(bool apply) { m_RootMotionApplyToOwner = apply; }
		bool ShouldApplyRootMotionToOwner() const { return m_RootMotionApplyToOwner; }
		glm::vec3 GetRootMotionDelta() const;
		glm::quat GetRootRotationDelta() const;
		bool HasRootMotionDelta() const { return m_LastRootMotionValid; }
		const VansAnimationFrameVector<VansAnimationEventSample>& GetSampledEvents() const { return m_SampledEvents; }
		const VansAnimationFrameVector<VansAnimationCurveSample>& GetSampledCurves() const { return m_SampledCurves; }
		const VansAnimationSyncState& GetSyncState() const { return m_SyncState; }
		void SetRootBoneIndex(int index) { m_RootBoneIndex = index; }

		// ─── Bone Overrides (由 Node 层设置) ─────────────────────────
		void SetBoneOverrides(const std::unordered_map<std::string, glm::mat4>* overrides);

		// ─── 核心更新（每帧调用）──────────────────────────────────────
		void Update(float deltaTime, const Skeleton& skeleton);
		void PrepareFrame(float deltaTime, const Skeleton& skeleton);
		bool GatherPreparedWorldQueries(const Skeleton& skeleton);
		bool ResolvePreparedWorldQueries(
			const std::vector<VansWorldQueryResult>& results,
			const Skeleton& skeleton);
		bool HasPreparedWorldQueries() const;
		const std::vector<VansWorldQueryRequest>& GetPreparedWorldQueries() const
		{
			return m_PreparedWorldQueries;
		}

		// ─── 输出 ─────────────────────────────────────────────────────
		const BoneMatricesSSBO& GetBoneMatricesSSBO() const { return m_BoneMatricesSSBO; }
		const glm::mat4& GetCachedGlobalTransform(int boneIndex) const;
		const std::vector<glm::mat4>& GetCachedGlobalTransforms() const { return m_CachedGlobalTransforms; }
		VansSkeletonPoseView GetFinalPoseView(const Skeleton& skeleton) const
		{
			VansSkeletonPoseView view{
				&skeleton, &m_CachedLocalTransforms, &m_CachedGlobalTransforms, m_FinalPoseRevision };
			return view.IsValid() ? view : VansSkeletonPoseView{};
		}
		const VansAnimationFrameVector<SampledNodeTransform>& GetSampledNodeTransforms() const { return m_SampledNodeTransforms; }
		std::size_t GetLastFrameScratchAllocations() const { return m_FramePool.GetLastFrameUpstreamAllocations(); }
		std::size_t GetLastFrameScratchAllocatedBytes() const { return m_FramePool.GetLastFrameUpstreamBytes(); }
		bool SubmitExternalModelPose(const std::vector<glm::mat4>& modelSpaceTransforms,
		                             const Skeleton& skeleton,
		                             float deltaTime,
		                             VansExternalPoseEvaluationMode mode,
		                             bool prepareWorldQueries = false);

		// ─── 序列化 ──────────────────────────────────────────────────
		std::string GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		// ─── AnimGraph ───────────────────────────────────────────
		bool SetAnimationGraphSets(
			std::vector<VansAnimationLayerSetup> layers,
			std::vector<VansAnimationGraphSetSetup> graphSets,
			std::string defaultGraphSetId,
			VansGraphSetTransitionPolicy defaultTransition,
			std::vector<VansGraphSetTransitionRule> transitionRules,
			std::string& error);
		bool SetTargetPostProcessGraph(std::unique_ptr<VansAnimGraph> graph,
		                               std::string& error);
		bool SetAnimationRig(VansCompiledAnimationRig rig,
		                     VansGroundQueryProfileResolver queryProfileResolver,
		                     std::string& error);
		bool BindAnimationRigSkeleton(const Skeleton& skeleton, std::string& error);
		const VansCompiledAnimationRig* GetAnimationRig() const
		{
			return m_AnimationRig ? &*m_AnimationRig : nullptr;
		}
		void ClearTargetPostProcessGraph();
		bool HasTargetPostProcessGraph() const { return m_TargetPostProcessInstance != nullptr; }
		bool HasGraphSets() const { return !m_GraphSetRuntimes.empty(); }
		std::size_t GetLayerCount() const { return m_LayerRuntimes.size(); }
		VansGraphSetSwitchResult SwitchGraphSet(const std::string& graphSetId);
		const std::string& GetActiveGraphSetId() const;
		const std::string& GetIncomingGraphSetId() const;
		bool IsGraphSetTransitioning() const;
		float GetGraphSetTransitionProgress() const;
		bool SetSlots(std::vector<VansAnimationSlotDefinition> slots, std::string& error);
		bool TransferRuntimeStateFrom(
			const VansAnimationController& previous,
			const Skeleton& skeleton,
			std::string& diagnostic);
		VansSlotPlaybackHandle PlaySlot(const std::string& slotId, const VansSlotPlayRequest& request);
		bool StopSlot(VansSlotPlaybackHandle handle, float blendOut, bool force = false);
		bool DriveSlot(VansSlotPlaybackHandle handle, float playbackTime, float weight);
		VansSlotPlaybackStatus GetSlotStatus(VansSlotPlaybackHandle handle) const;
		bool IsSlotActive(const std::string& slotId) const;
		const VansAnimationSlotDefinition* FindSlotDefinition(const std::string& slotId) const;
		const std::vector<VansSlotLifecycleEvent>& GetSlotLifecycleEvents() const;

		bool ConfigureMotionMatching(const MotionMatchingSettings& settings, std::string& error);
		bool IsMotionMatchingConfigured() const { return m_MotionMatching != nullptr; }
		const MotionMatchingDebugData* GetMotionMatchingDebugData() const;
		const MotionMatchingSettings* GetMotionMatchingSettings() const;
		const std::vector<VansProceduralDebugRecord>* GetProceduralDebugRecords() const;
		void SetCharacterTrajectory(const Vans::VansCharacterTrajectory* trajectory) { m_CharacterTrajectory = trajectory; }
		bool MotionMatchingPrefersRootMotion() const;

		void SetAnimationExternalInput(VansAnimationExternalInputSnapshot input)
		{
			m_ExternalInput = std::move(input);
			m_OwnerWorldTransform = m_ExternalInput.ownerWorld;
		}
		const VansAnimationExternalInputSnapshot& GetAnimationExternalInput() const { return m_ExternalInput; }
		void SetOwnerWorldTransform(const glm::mat4& transform)
		{
			m_OwnerWorldTransform = transform;
			m_ExternalInput.ownerWorld = transform;
		}
		void SetOwnerStableId(std::uint64_t ownerId) { m_ExternalInput.ownerId = ownerId; }

	private:
		std::string m_Name;

		// ─── 参数表 ───
		std::unordered_map<std::string, AnimatorParameter> m_Parameters;

		// ─── Clip 数据（Controller 直接持有）───
		std::unordered_map<std::string, VansAnimationClip> m_Clips;

		// ─── 运行时状态 ───
		AnimationState       m_PlaybackState  = AnimationState::Stopped;
		float                m_GlobalSpeed    = 1.0f;
		// ─── Root Motion ───
		bool      m_RootMotionEnabled     = false;
		bool      m_RootMotionApplyToOwner = true;
		int       m_RootBoneIndex         = -1;
		glm::vec3 m_LastRootMotionDelta   = glm::vec3(0.0f);
		glm::quat m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		bool      m_LastRootMotionValid = false;
		VansAnimationFrameVector<VansAnimationEventSample> m_SampledEvents;
		VansAnimationFrameVector<VansAnimationCurveSample> m_SampledCurves;
		VansAnimationSyncState m_SyncState;

		// ─── Bone Overrides（外部指针，不持有生命周期）───
		const std::unordered_map<std::string, glm::mat4>* m_BoneOverrides = nullptr;

		// ─── 输出 ───
		BoneMatricesSSBO m_BoneMatricesSSBO;
		std::vector<glm::mat4> m_CachedLocalTransforms;
		std::vector<glm::mat4> m_CachedGlobalTransforms;
		std::uint64_t m_FinalPoseRevision = 0;
		mutable std::vector<glm::mat4> m_LocalTransformScratch;
		VansAnimationFrameVector<SampledNodeTransform> m_SampledNodeTransforms;
		VansAnimationFramePool m_FramePool;

		// ─── AnimGraph ───
		struct LayerRuntime
		{
			VansAnimationLayerDefinition definition;
			std::optional<VansBoneMaskAsset> maskAsset;
			VansCompiledBoneMask compiledMask;
			VansAnimationLayerRuntimeState state;
		};
		struct GraphBindingRuntime
		{
			VansAnimationGraphBindingDefinition definition;
			std::unique_ptr<VansAnimGraph> graph;
			std::unique_ptr<VansAnimGraphInstance> instance;
			std::unordered_map<std::string, AnimatorParameter> parameterScratch;
			float lastEvaluationMilliseconds = 0.0f;
			int syncLeaderIndex = -1;
		};
		struct GraphSetRuntime
		{
			VansAnimationGraphSetDefinition definition;
			std::vector<GraphBindingRuntime> bindings;
			std::vector<VansAnimationSyncState> evaluatedSync;
		};
		std::vector<LayerRuntime> m_LayerRuntimes;
		std::vector<GraphSetRuntime> m_GraphSetRuntimes;
		std::unordered_map<std::string, std::size_t> m_GraphSetById;
		std::size_t m_ActiveGraphSetIndex = static_cast<std::size_t>(-1);
		std::size_t m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
		VansGraphSetTransitionPolicy m_DefaultGraphSetTransition;
		std::vector<VansGraphSetTransitionRule> m_GraphSetTransitionRules;
		VansGraphSetTransitionPolicy m_CurrentGraphSetTransition;
		float m_GraphSetTransitionElapsed = 0.0f;
		std::string m_QueuedGraphSetId;
		std::unique_ptr<VansAnimGraph> m_TargetPostProcessGraph;
		std::unique_ptr<VansAnimGraphInstance> m_TargetPostProcessInstance;
		VansAnimationSlotRuntime m_SlotRuntime;
		std::unordered_map<std::string, VansPosePayload> m_SlotPayloads;
		std::unique_ptr<VansMotionMatchingRuntime> m_MotionMatching;
		std::unique_ptr<VansMotionMatchingRuntime> m_IncomingMotionMatching;
		const Vans::VansCharacterTrajectory* m_CharacterTrajectory = nullptr;
		std::optional<VansCompiledAnimationRig> m_AnimationRig;
		VansGroundQueryProfileResolver m_QueryProfileResolver;
		std::unique_ptr<VansProceduralGraphRuntime> m_ProceduralRuntime;
		VansAnimationExternalInputSnapshot m_ExternalInput;
		glm::mat4 m_OwnerWorldTransform = glm::mat4(1.0f);
		bool m_DebugMetricsEnabled = false;
		std::vector<glm::mat4> m_PreparedLocalTransforms;
		std::vector<VansBoneTransform> m_PreparedProceduralPose;
		std::vector<int> m_PreparedProceduralNodeIds;
		std::vector<VansWorldQueryRequest> m_PreparedWorldQueries;
		std::vector<VansBoneTransform> m_ProceduralCompletedPoseScratch;
		std::string m_ProceduralErrorScratch;
		float m_PreparedDeltaTime = 0.0f;
		bool m_HasPreparedFrame = false;

		// ─── 内部方法 ───
		void ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms,
		                       const Skeleton& skeleton);
		void NormalizeRootTransform(std::vector<glm::mat4>& localTransforms,
		                            const Skeleton& skeleton);
		void UpdateHierarchy(std::vector<glm::mat4>& localTransforms,
		                     const Skeleton& skeleton);
		void BuildFinalMatrices(const std::vector<glm::mat4>& globalTransforms,
		                        const Skeleton& skeleton);
		void EnsureMotionMatchingGraphNode();
		void RefreshExternalMotionState();
		void PublishMotionMatchingContacts();
		void RebuildGraphSetInstances();
		bool PrepareLayerStack(float deltaTime, const Skeleton& skeleton);
		bool EvaluateGraphSet(GraphSetRuntime& graphSet,
		                      VansMotionMatchingRuntime* motionMatching,
		                      float deltaTime,
		                      const Skeleton& skeleton,
		                      VansPosePayload& outPayload);
		const VansGraphSetTransitionPolicy& ResolveGraphSetTransitionPolicy(
			const std::string& fromGraphSetId,
			const std::string& toGraphSetId) const;
		bool ApplyGraphSetPhaseHandoff(
			const GraphSetRuntime& source,
			GraphSetRuntime& target,
			const VansGraphSetTransitionPolicy& policy);
		void CompleteGraphSetTransition();
		GraphSetRuntime* GetActiveGraphSetRuntime();
		const GraphSetRuntime* GetActiveGraphSetRuntime() const;
		VansMotionMatchingRuntime* GetOutputMotionMatchingRuntime();
		const VansMotionMatchingRuntime* GetOutputMotionMatchingRuntime() const;
		bool EvaluateTargetPostProcess(float deltaTime, const Skeleton& skeleton,
		                               const VansPosePayload& input,
		                               VansPosePayload& output);
		bool FinalizeLocalPose(float deltaTime, const Skeleton& skeleton,
		                       VansPosePayload pose,
		                       bool normalizeRoot,
		                       bool publishAnimationOutputs,
		                       bool deferWorldSpacePostProcess);
		void UpdateInternal(float deltaTime, const Skeleton& skeleton,
		                    bool deferWorldSpacePostProcess);
		bool ConvertModelPoseToLocalPayload(const std::vector<glm::mat4>& modelSpaceTransforms,
		                                    const Skeleton& skeleton,
		                                    VansPosePayload& outPayload) const;
		void ResolveLayerReferencePose(const LayerRuntime& layer,
		                               const GraphBindingRuntime& binding,
		                               const Skeleton& skeleton,
		                               VansAnimationFrameVector<VansBoneTransform>& outPose) const;

		int  DetectRootBoneIndex(const Skeleton& skeleton) const;
	};

}  // namespace VansGraphics
