#pragma once

#include "../VansAnimationTypes.h"
#include "../VansAnimationController.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	struct MotionMatchingSettings
	{
		bool enabled = false;
		bool autoBuild = true;
		float sampleRate = 30.0f;
		float searchThrottle = 0.15f;
		float blendDuration = 0.18f;
		float minSwitchCostImprovement = 0.02f;
		float continuationBias = 0.10f;
		float loopBias = 0.04f;
		float desiredSpeedScale = 650.0f;
		int topCandidateCount = 8;
		std::vector<std::string> includeClipNameTokens;
		std::vector<std::string> excludeClipNameTokens;
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
		static constexpr int FeatureDim = 16;
		using FeatureVector = std::array<float, FeatureDim>;

		struct Sample
		{
			std::string clipName;
			const VansAnimationClip* clip = nullptr;
			float time = 0.0f;
			FeatureVector feature{};
			bool loopLike = false;
		};

		struct BoneMap
		{
			int root = -1;
			int pelvis = -1;
			int leftFoot = -1;
			int rightFoot = -1;
			int head = -1;
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
		BoneMap m_Bones;
		bool m_DatabaseReady = false;
		bool m_DatabaseDirty = true;

		int m_CurrentSample = -1;
		float m_CurrentTime = 0.0f;
		float m_TimeSinceSearch = 999.0f;
		float m_CurrentCost = 1.0e30f;
		int m_SwitchCount = 0;

		bool m_Blending = false;
		float m_BlendElapsed = 0.0f;
		std::vector<glm::mat4> m_BlendSource;

		BoneMap DetectBones(const Skeleton& skeleton) const;
		bool ShouldIncludeClip(const std::string& clipName) const;
		FeatureVector ExtractFeature(const VansAnimationClip& clip,
		                             float time,
		                             const Skeleton& skeleton,
		                             const BoneMap& bones) const;
		FeatureVector BuildQueryFeature(const std::unordered_map<std::string, AnimatorParameter>& parameters) const;
		void NormalizeFeature(FeatureVector& feature) const;
		MatchResult FindBestMatch(const FeatureVector& query);
		void SamplePose(const VansAnimationClip& clip,
		                float time,
		                const Skeleton& skeleton,
		                std::vector<glm::mat4>& outLocalTransforms) const;
		void BlendPose(const std::vector<glm::mat4>& from,
		               const std::vector<glm::mat4>& to,
		               float alpha,
		               std::vector<glm::mat4>& out) const;
		void PushCandidateDebug(const MatchResult& result);
	};
}
