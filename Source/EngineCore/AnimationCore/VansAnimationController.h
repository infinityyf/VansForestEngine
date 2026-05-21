#pragma once

#include "VansAnimationTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace VansGraphics
{
	// 前向声明 (VansAnimGraph.h 包含本头文件，避免循环依赖)
	class VansAnimGraph;

	// ─── 参数类型 ───

	enum class AnimatorParamType
	{
		Float,
		Bool,
		Int,
		Trigger    // 一次性触发器，触发后自动重置为 false
	};

	// ─── 参数 ───

	struct AnimatorParameter
	{
		std::string       name;
		AnimatorParamType type     = AnimatorParamType::Float;
		float             floatVal = 0.0f;
		bool              boolVal  = false;
		int               intVal   = 0;
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

		// 运行时字段（不序列化）
		VansAnimationClip* clip              = nullptr;
		float              currentTime       = 0.0f;
		bool               pingPongReversing = false;
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

		float GetFloat(const std::string& name) const;
		bool  GetBool(const std::string& name) const;
		int   GetInt(const std::string& name) const;
		bool  IsTriggerSet(const std::string& name) const;

		const std::unordered_map<std::string, AnimatorParameter>& GetParameters() const;

		// ─── State 管理 ───────────────────────────────────────────────
		void AddState(const AnimatorState& state);
		void RemoveState(const std::string& stateName);
		AnimatorState* GetState(const std::string& stateName);
		const AnimatorState* GetState(const std::string& stateName) const;
		std::vector<std::string> GetStateNames() const;

		void SetDefaultState(const std::string& stateName);
		std::string GetDefaultStateName() const { return m_DefaultStateName; }

		// ─── Transition 管理 ──────────────────────────────────────────
		void AddTransition(const AnimatorTransition& transition);
		void RemoveTransition(const std::string& fromState, const std::string& toState);
		const std::vector<AnimatorTransition>& GetTransitions() const;

		// ─── Clip 管理（Controller 直接持有 clip 数据）────────────────
		void AddClip(const std::string& name, VansAnimationClip&& clip);
		void AddClip(const std::string& name, const VansAnimationClip& clip);
		void RemoveClip(const std::string& name);
		VansAnimationClip* GetClip(const std::string& name);
		const VansAnimationClip* GetClip(const std::string& name) const;
		const std::unordered_map<std::string, VansAnimationClip>& GetClipsMap() const;
		std::vector<std::string> GetClipNames() const;

		// 将 State 的 clip 指针绑定到 m_Clips 中的实际数据
		void BindStateClips();

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
		float GetBlendAlpha() const { return m_BlendAlpha; }

		// ─── Root Motion ──────────────────────────────────────────────
		void EnableRootMotion(bool enable);
		bool IsRootMotionEnabled() const;
		glm::vec3 GetRootMotionDelta() const;
		glm::quat GetRootRotationDelta() const;
		void SetRootBoneIndex(int index) { m_RootBoneIndex = index; }

		// ─── Bone Overrides (由 Node 层设置) ─────────────────────────
		void SetBoneOverrides(const std::unordered_map<std::string, glm::mat4>* overrides);

		// ─── 核心更新（每帧调用）──────────────────────────────────────
		void Update(float deltaTime, const Skeleton& skeleton);

		// ─── 输出 ─────────────────────────────────────────────────────
		const BoneMatricesSSBO& GetBoneMatricesSSBO() const { return m_BoneMatricesSSBO; }
		const glm::mat4& GetCachedGlobalTransform(int boneIndex) const;
		const std::vector<glm::mat4>& GetCachedGlobalTransforms() const { return m_CachedGlobalTransforms; }

		// ─── 序列化 ──────────────────────────────────────────────────
		std::string GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		// ─── AnimGraph (v2) ───────────────────────────────────────────
		void SetGraph(std::unique_ptr<VansAnimGraph> graph);
		VansAnimGraph* GetGraph() const { return m_Graph.get(); }
		bool HasGraph() const { return m_Graph != nullptr; }

	private:
		std::string m_Name;

		// ─── 参数表 ───
		std::unordered_map<std::string, AnimatorParameter> m_Parameters;

		// ─── 状态表 ───
		std::unordered_map<std::string, AnimatorState> m_States;
		std::string m_DefaultStateName;

		// ─── 过渡表 ───
		std::vector<AnimatorTransition> m_Transitions;

		// ─── Clip 数据（Controller 直接持有）───
		std::unordered_map<std::string, VansAnimationClip> m_Clips;

		// ─── 运行时状态 ───
		AnimationState       m_PlaybackState  = AnimationState::Stopped;
		std::string          m_CurrentStateName;
		std::string          m_PrevStateName;
		float                m_BlendAlpha     = 0.0f;
		float                m_BlendDuration  = 0.0f;
		ControllerBlendState m_BlendState     = ControllerBlendState::Idle;
		float                m_GlobalSpeed    = 1.0f;
		bool                 m_LoopJustWrapped = false;

		// ─── Root Motion ───
		bool      m_RootMotionEnabled     = false;
		int       m_RootBoneIndex         = -1;
		glm::vec3 m_PrevRootPosition      = glm::vec3(0.0f);
		glm::quat m_PrevRootRotation      = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		bool      m_RootMotionInitialized = false;
		glm::vec3 m_LastRootMotionDelta   = glm::vec3(0.0f);
		glm::quat m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

		// ─── Bone Overrides（外部指针，不持有生命周期）───
		const std::unordered_map<std::string, glm::mat4>* m_BoneOverrides = nullptr;

		// ─── 输出 ───
		BoneMatricesSSBO m_BoneMatricesSSBO;
		std::vector<glm::mat4> m_CachedGlobalTransforms;

		// ─── AnimGraph (v2) ───
		std::unique_ptr<VansAnimGraph> m_Graph;

		// ─── 内部方法 ───
		void AdvanceStateTime(AnimatorState& state, float dt);
		void EvaluateTransitions();
		bool CheckConditions(const AnimatorTransition& transition) const;
		bool CompareParam(const AnimatorParameter& param, const TransitionCondition& cond) const;
		void ConsumeTriggers(const AnimatorTransition& transition);
		void StartTransition(const AnimatorTransition& transition);

		void ComputeBoneTransforms(const AnimatorState& state,
		                           const Skeleton& skeleton,
		                           std::vector<glm::mat4>& outLocalTransforms);
		void BlendTransforms(const std::vector<glm::mat4>& a,
		                     const std::vector<glm::mat4>& b,
		                     float alpha,
		                     std::vector<glm::mat4>& outBlended);
		void ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms,
		                       const Skeleton& skeleton);
		void ExtractRootMotion(std::vector<glm::mat4>& localTransforms,
		                       const Skeleton& skeleton);
		void UpdateHierarchy(std::vector<glm::mat4>& localTransforms,
		                     const Skeleton& skeleton);
		void BuildFinalMatrices(const std::vector<glm::mat4>& globalTransforms,
		                        const Skeleton& skeleton);

		void InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
		                          float time,
		                          glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);
		int  DetectRootBoneIndex(const Skeleton& skeleton) const;

		template<typename T>
		static bool CompareValue(T a, CompareOp op, T b);

		// 获取当前 state 的归一化时间
		float GetStateNormalizedTime(const AnimatorState& state) const;
	};

}  // namespace VansGraphics
