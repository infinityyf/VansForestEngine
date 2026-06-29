#pragma once

#include "../VansAnimationTypes.h"
#include "../VansAnimationController.h"

#include <array>
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

	static constexpr int MotionFeatureDim = 28;
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

	struct MotionMatchingSettings
	{
		bool enabled = false;
		bool autoBuild = true;
		float sampleRate = 30.0f;
		float searchThrottle = 0.15f;
		float blendDuration = 0.18f;
		float minSwitchCostImprovement = 0.02f;
		float minSwitchInterval = 0.25f;
		float blendInterruptFraction = 0.75f;
		float continuationBias = 0.10f;
		float loopBias = 0.04f;
		float transitionBias = 0.08f;
		float desiredSpeedScale = 650.0f;
		float trajectoryWeight = 1.0f;
		float poseWeight = 0.7f;
		int topCandidateCount = 8;
		MotionMatchingRigMap rig;
		bool allowLegacyBoneDetection = true;
		MotionFeatureSchema schema;
		std::vector<std::string> includeClipNameTokens;
		std::vector<std::string> excludeClipNameTokens;
		std::vector<MotionMatchingSearchGroup> searchGroups;
	};

	struct MotionMatchingCandidateDebug
	{
		std::string clipName;
		float time = 0.0f;
		float totalCost = 0.0f;
		float trajectoryCost = 0.0f;
		float poseCost = 0.0f;
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
		float biasCost = 0.0f;
		float querySpeed = 0.0f;
		float queryDirection = 0.0f;
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
		            std::vector<glm::mat4>& outLocalTransforms);

		const MotionMatchingDebugData& GetDebugData() const { return m_DebugData; }

	private:
		static constexpr int FeatureDim = MotionFeatureDim;
		static constexpr int kTrajectoryBegin = 0;
		static constexpr int kTrajectoryEnd = 12;
		static constexpr int kPoseBegin = 12;
		static constexpr int kPoseEnd = FeatureDim;
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
		};

		struct MatchResult
		{
			int sampleIndex = -1;
			float totalCost = 0.0f;
			float trajectoryCost = 0.0f;
			float poseCost = 0.0f;
			float biasCost = 0.0f;
		};

		MotionMatchingSettings m_Settings;
		MotionMatchingDebugData m_DebugData;
		std::vector<Sample> m_Samples;
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
		std::vector<glm::mat4> m_BlendSource;
		std::vector<glm::mat4> m_LastOutputLocalPose;
		std::vector<glm::mat4> m_PreviousQueryModelPose;
		glm::vec3 m_CurrentLeftFootVelocity = glm::vec3(0.0f);
		glm::vec3 m_CurrentRightFootVelocity = glm::vec3(0.0f);
		glm::vec3 m_CurrentPelvisVelocity = glm::vec3(0.0f);
		bool m_HasQueryVelocity = false;

		bool ShouldIncludeClip(const std::string& clipName) const;
		bool SearchGroupAllowsSample(const Sample& sample,
		                             const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
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
		                  float& outPose) const;
		MatchResult FindBestMatch(const FeatureVector& query,
		                          const std::unordered_map<std::string, AnimatorParameter>& parameters,
		                          const std::unordered_map<std::string, VansAnimationClip>& clips);
		bool ShouldConsiderSampleForParameters(const Sample& sample,
		                                       const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		bool IsSamePlaybackNeighborhood(const Sample& sample) const;
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
		void BlendPose(const std::vector<glm::mat4>& from,
		               const std::vector<glm::mat4>& to,
		               float alpha,
		               std::vector<glm::mat4>& out) const;
		void PushCandidateDebug(const MatchResult& result);
	};
}
