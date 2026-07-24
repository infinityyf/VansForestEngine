#pragma once

#include "../VansAnimationTypes.h"
#include "../VansAnimationController.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	struct MotionMatchingRigMap
	{
		std::string root;
		std::string trajectoryRoot;
		std::string pelvis;
		std::string leftFoot;
		std::string rightFoot;
		std::string head;
		glm::vec3 forwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);

		bool HasExplicitMapping() const
		{
			return !root.empty() || !trajectoryRoot.empty() || !pelvis.empty() ||
			       !leftFoot.empty() || !rightFoot.empty() || !head.empty();
		}
	};

	struct MotionMatchingResolvedRig
	{
		int root = -1;
		int trajectoryRoot = -1;
		int pelvis = -1;
		int leftFoot = -1;
		int rightFoot = -1;
		int head = -1;
		glm::vec3 forwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);

		bool IsValid() const
		{
			return root >= 0 && trajectoryRoot >= 0 && pelvis >= 0 &&
			       leftFoot >= 0 && rightFoot >= 0;
		}
	};

	// 12 trajectory channels, 16 pose channels and two continuous foot-contact
	// channels.  Contacts are part of the searchable feature vector so a switch
	// cannot silently exchange a planted foot for a swinging foot.
	static constexpr int MotionFeatureDim = 30;
	using MotionFeatureVector = std::array<float, MotionFeatureDim>;

	struct MotionFeatureSchema
	{
		static constexpr int FutureTimeCount = 3;
		std::array<float, FutureTimeCount> futureTimes = { 0.25f, 0.50f, 1.00f };
	};

	struct MotionMatchingSearchGroup
	{
		std::string name;
		std::string stance = "Any";
		std::string phase = "Any";
		std::vector<int> moveStates;
		std::vector<std::string> includeClipNameTokens;
		std::vector<std::string> excludeClipNameTokens;
	};

	struct MotionMatchingParameterMap
	{
		std::string enabled = "UseMotionMatching";
		std::string speed = "Speed";
		std::string direction = "Direction";
		std::string crouching = "IsCrouching";
		std::string airborne = "IsAirborne";
		std::string moveState = "MoveState";
	};

	struct MotionMatchingStateSemantics
	{
		int idleState = 0;
		int crouchState = 4;
		float idleSpeedThreshold = 0.05f;
		std::vector<int> movingStates = { 1, 2, 3, 4 };
		std::vector<int> paceTransitionStates = { 1, 2, 3 };
		std::vector<int> stanceStates = { 0, 4 };
	};

	struct MotionMatchingClipMetadata
	{
		std::string name;
		std::vector<std::string> matchTokens;
		bool hasLoopLike = false;
		bool loopLike = false;
		bool hasIdleLike = false;
		bool idleLike = false;
		bool hasTransitionLike = false;
		bool transitionLike = false;
		bool hasStartLike = false;
		bool startLike = false;
		bool hasStopLike = false;
		bool stopLike = false;
		bool hasTurnLike = false;
		bool turnLike = false;
		bool hasPaceTransitionLike = false;
		bool paceTransitionLike = false;
		bool hasSourceMoveState = false;
		int sourceMoveState = 0;
		bool hasTargetMoveState = false;
		int targetMoveState = 0;
		bool hasDirectionBucket = false;
		int directionBucket = -1;
		bool hasTurnDirectionSign = false;
		int turnDirectionSign = 0;
		bool hasTurnBucketDelta = false;
		int turnBucketDelta = 0;
	};

	struct MotionMatchingSettings
	{
		bool enabled = false;
		bool autoBuild = true;
		// The owner is moved by gameplay/CCT rather than by animation root motion.
		// Such locomotion must match loop poses directly; authored transition
		// displacement is not consumed and would otherwise create severe foot lag.
		bool externallyDriven = false;
		float sampleRate = 30.0f;
		float searchThrottle = 0.15f;
		float minSwitchCostImprovement = 0.02f;
		float minSwitchCostRatio = 0.10f;
		float minSwitchInterval = 0.25f;
		float blendInterruptFraction = 0.75f;
		float continuationBias = 0.10f;
		float loopBias = 0.04f;
		float transitionBias = 0.08f;
		float desiredSpeedScale = 650.0f;
		// Owner/world transforms use metres while imported character animation
		// tracks use centimetres.  Convert measured gameplay velocity into the
		// animation feature domain before trajectory matching or speed matching.
		float worldToAnimationScale = 1.0f;
		bool enableSpeedMatching = true;
		float minPlaybackRate = 0.75f;
		float maxPlaybackRate = 1.25f;
		float playbackRateSmoothing = 12.0f;
		float trajectoryResponsiveness = 8.0f;
		float inertializationHalfLife = 0.10f;
		float inertializationMaxDuration = 0.45f;
		float trajectoryWeight = 1.0f;
		float poseWeight = 0.7f;
		float contactWeight = 2.0f;
		// Contact phase is authored automatically per clip from normalized foot
		// height.  Velocity remains a confidence term instead of deciding contact
		// on its own, because a root-moving clip can have large model-space ankle
		// velocity while the foot is visually planted.
		float contactHeightFullFraction = 0.08f;
		float contactHeightFadeFraction = 0.32f;
		float contactVelocityConfidenceFloor = 0.75f;
		int topCandidateCount = 8;
		MotionMatchingRigMap rig;
		bool allowLegacyBoneDetection = true;
		MotionFeatureSchema schema;
		MotionMatchingParameterMap parameters;
		MotionMatchingStateSemantics states;
		std::vector<std::string> includeClipNameTokens;
		std::vector<std::string> excludeClipNameTokens;
		std::vector<MotionMatchingSearchGroup> searchGroups;
		std::vector<MotionMatchingClipMetadata> clipMetadata;
	};

	struct MotionMatchingCandidateDebug
	{
		std::string clipName;
		float time = 0.0f;
		float totalCost = 0.0f;
		float trajectoryCost = 0.0f;
		float poseCost = 0.0f;
		float contactCost = 0.0f;
		float biasCost = 0.0f;
	};

	struct MotionMatchingDebugData
	{
		bool enabled = false;
		bool databaseReady = false;
		bool usedThisFrame = false;
		bool rigReady = false;
		std::string rigStatus;
		std::string activeClip;
		std::string selectedClip;
		float activeTime = 0.0f;
		float selectedTime = 0.0f;
		float currentCost = 0.0f;
		float trajectoryCost = 0.0f;
		float poseCost = 0.0f;
		float contactCost = 0.0f;
		float biasCost = 0.0f;
		float querySpeed = 0.0f;
		float queryDirection = 0.0f;
		float playbackRate = 1.0f;
		int sampleCount = 0;
		int clipCount = 0;
		int switches = 0;
		std::vector<MotionMatchingCandidateDebug> topCandidates;
	};

	class VansMotionMatchingRuntime
	{
	public:
		void Configure(const MotionMatchingSettings& settings);
		const MotionMatchingSettings& GetSettings() const { return m_Settings; }

		void MarkDatabaseDirty();
		bool IsDatabaseReady() const { return m_DatabaseReady; }
		bool BuildDatabase(const std::unordered_map<std::string, VansAnimationClip>& clips,
		                   const Skeleton& skeleton);

		bool Update(float deltaTime,
		            const Skeleton& skeleton,
		            const std::unordered_map<std::string, VansAnimationClip>& clips,
		            const std::unordered_map<std::string, AnimatorParameter>& parameters,
		            const glm::mat4& ownerWorldTransform,
		            std::vector<glm::mat4>& outLocalTransforms);

		const MotionMatchingDebugData& GetDebugData() const { return m_DebugData; }
		bool WasUsedThisFrame() const { return m_DebugData.usedThisFrame; }
		bool IsTransitioning() const { return m_Blending; }
		float GetLeftFootPlantWeight() const { return m_LeftFootPlantWeight; }
		float GetRightFootPlantWeight() const { return m_RightFootPlantWeight; }

	private:
		static constexpr int FeatureDim = MotionFeatureDim;
		static constexpr int kTrajectoryBegin = 0;
		static constexpr int kTrajectoryEnd = 12;
		static constexpr int kPoseBegin = 12;
		static constexpr int kPoseEnd = 28;
		static constexpr int kContactBegin = 28;
		static constexpr int kContactEnd = FeatureDim;
		using FeatureVector = MotionFeatureVector;

		struct Sample
		{
			std::string clipName;
			float time = 0.0f;
			FeatureVector rawFeature{};
			FeatureVector feature{};
			bool loopLike = false;
			bool idleLike = false;
			bool transitionLike = false;
			bool startLike = false;
			bool stopLike = false;
			bool turnLike = false;
			bool paceTransitionLike = false;
			int sourceMoveState = -1;
			int targetMoveState = 0;
			int directionBucketFromName = -1;
			int turnDirectionSign = 0;
			int turnBucketDelta = 0;
			float trajectorySpeed = 0.0f;
		};

		struct MatchResult
		{
			int sampleIndex = -1;
			float totalCost = 0.0f;
			float trajectoryCost = 0.0f;
			float poseCost = 0.0f;
			float contactCost = 0.0f;
			float biasCost = 0.0f;
		};

		MotionMatchingSettings m_Settings;
		MotionMatchingDebugData m_DebugData;
		std::vector<Sample> m_Samples;
		std::unordered_map<std::string, std::vector<int>> m_ClipSampleIndices;
		FeatureVector m_Mean{};
		FeatureVector m_Std{};
		MotionMatchingResolvedRig m_Rig;
		bool m_DatabaseReady = false;
		bool m_DatabaseDirty = true;

		int m_CurrentSample = -1;
		float m_CurrentTime = 0.0f;
		float m_TimeSinceSearch = 999.0f;
		float m_TimeSinceSwitch = 999.0f;
		float m_CurrentCost = 1.0e30f;
		int m_SwitchCount = 0;
		bool m_HasLastSearchContext = false;
		int m_LastMoveState = -1;
		int m_LastDirectionBucket = -1;
		int m_SourceDirectionBucketForSearch = -1;
		bool m_DirectionChangedForSearch = false;
		bool m_LastCrouching = false;
		bool m_LastAirborne = false;
		bool m_LastMoving = false;

		bool m_Blending = false;
		float m_BlendElapsed = 0.0f;
		struct InertialBoneState
		{
			glm::vec3 positionOffset = glm::vec3(0.0f);
			glm::vec3 positionVelocity = glm::vec3(0.0f);
			glm::vec3 rotationOffset = glm::vec3(0.0f);
			glm::vec3 angularVelocity = glm::vec3(0.0f);
		};
		std::vector<InertialBoneState> m_InertialState;
		std::vector<glm::mat4> m_LastOutputLocalPose;
		std::vector<glm::mat4> m_PreviousOutputLocalPose;
		std::vector<glm::mat4> m_PreviousQueryModelPose;
		glm::vec3 m_CurrentLeftFootVelocity = glm::vec3(0.0f);
		glm::vec3 m_CurrentRightFootVelocity = glm::vec3(0.0f);
		glm::vec3 m_CurrentPelvisVelocity = glm::vec3(0.0f);
		bool m_HasQueryVelocity = false;
		float m_CurrentPlaybackRate = 1.0f;
		glm::vec3 m_CurrentTrajectoryVelocityRoot = glm::vec3(0.0f);
		float m_LeftFootPlantWeight = 0.0f;
		float m_RightFootPlantWeight = 0.0f;
		float m_LeftContactOffset = 0.0f;
		float m_RightContactOffset = 0.0f;
		bool m_ContactTransitionActive = false;
		bool m_OwnerTransformInitialized = false;
		glm::vec3 m_PreviousOwnerWorldPosition = glm::vec3(0.0f);
		glm::vec3 m_ActualOwnerVelocityRoot = glm::vec3(0.0f);
		bool m_HasActualOwnerVelocity = false;

		bool ShouldIncludeClip(const std::string& clipName) const;
		const MotionMatchingClipMetadata* FindClipMetadata(const std::string& clipName) const;
		void BuildFootContactPhases(const std::vector<int>& clipSampleIndices);
		bool SearchGroupAllowsSample(const Sample& sample,
		                             const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		float ReadSpeedParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		float ReadDirectionParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		bool ReadCrouchingParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		bool ReadAirborneParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		int ReadMoveStateParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		int ResolveDesiredMoveState(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		bool IsMovingState(int state) const;
		bool IsMovingPlaybackSample(const Sample& sample) const;
		bool IsPaceTransitionState(int state) const;
		bool IsStanceState(int state) const;
		int ResolveBoneIndex(const Skeleton& skeleton, const std::string& name) const;
		MotionMatchingResolvedRig ResolveRig(const Skeleton& skeleton);
		MotionMatchingResolvedRig DetectLegacyRig(const Skeleton& skeleton) const;
		bool ValidateRig(const MotionMatchingResolvedRig& rig, std::string& outReason) const;
		FeatureVector ExtractDatabaseFeature(const VansAnimationClip& clip,
		                                     float time,
		                                     bool loopLike,
		                                     const Skeleton& skeleton,
		                                     const MotionMatchingResolvedRig& rig) const;
		FeatureVector BuildQueryFeature(const std::unordered_map<std::string, AnimatorParameter>& parameters,
		                                const std::vector<glm::mat4>& currentLocalPose,
		                                const Skeleton& skeleton,
		                                const MotionMatchingResolvedRig& rig) const;
		void NormalizeFeature(FeatureVector& feature) const;
		float ComputeCost(const FeatureVector& query,
		                  const FeatureVector& candidate,
		                  float& outTrajectory,
		                  float& outPose,
		                  float& outContact) const;
		MatchResult FindBestMatch(const FeatureVector& query,
		                          const std::unordered_map<std::string, AnimatorParameter>& parameters);
		bool ShouldConsiderSampleForParameters(const Sample& sample,
		                                       const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		void SamplePose(const VansAnimationClip& clip,
		                float time,
		                const Skeleton& skeleton,
		                std::vector<glm::mat4>& outLocalTransforms) const;
		void BuildModelSpacePose(const std::vector<glm::mat4>& localTransforms,
		                         const Skeleton& skeleton,
		                         std::vector<glm::mat4>& outModelTransforms) const;
		glm::vec3 TransformPointToRootSpace(const glm::mat4& rootModel, const glm::vec3& point) const;
		glm::vec3 TransformVectorToRootSpace(const glm::mat4& rootModel, const glm::vec3& vector) const;
		glm::vec3 ExtractRootForward(const glm::mat4& rootModel, const MotionMatchingResolvedRig& rig) const;
		glm::vec3 BuildDesiredVelocityRoot(const std::unordered_map<std::string, AnimatorParameter>& parameters,
		                                   const MotionMatchingResolvedRig& rig) const;
		float WrapClipTime(const VansAnimationClip& clip, float time) const;
		float ResolveClipTime(const VansAnimationClip& clip, float time, bool loopLike) const;
		void WriteVec3(FeatureVector& feature, int& offset, const glm::vec3& value) const;
		bool SampleContactWeights(int sampleIndex, float time, float& outLeft, float& outRight) const;
		void AdvanceContactWeights(float deltaTime, float targetLeft, float targetRight);
		void BeginContactTransition(float sourceLeft, float sourceRight, float targetLeft, float targetRight);
		void UpdateActualOwnerVelocity(float deltaTime, const glm::mat4& ownerWorldTransform);
		void BeginInertialTransition(const std::vector<glm::mat4>& target,
		                             const std::vector<glm::mat4>& targetFuture,
		                             float velocityDeltaTime);
		void ApplyInertialization(float deltaTime,
		                          const std::vector<glm::mat4>& target,
		                          std::vector<glm::mat4>& out);
		void PushCandidateDebug(const MatchResult& result);
	};
}
