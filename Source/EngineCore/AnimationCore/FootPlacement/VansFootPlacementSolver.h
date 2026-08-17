#pragma once

#include "VansFootGroundProbe.h"
#include "VansFootPlacementTypes.h"
#include "../IK/VansIKTypes.h"

#include <../../GLM/glm.hpp>

#include <vector>

namespace VansGraphics
{
	class VansFootPlacementSolver
	{
	public:
		bool Configure(const FootPlacementSettings& settings, const Skeleton& skeleton);
		void SetEnabled(bool enabled) { m_Settings.enabled = enabled; }
		void SetDebugVisualization(bool enabled) { m_Settings.debugVisualization = enabled; }
		bool IsConfigured() const { return m_Configured; }
		const FootPlacementSettings& GetSettings() const { return m_Settings; }

		void SetRuntimeState(const FootPlacementRuntimeState& state) { m_RuntimeState = state; }
		void ResetTransientState();
		const FootPlacementDebugData& GetDebugData() const { return m_DebugData; }

		void Solve(float deltaTime,
		           const Skeleton& skeleton,
		           const glm::mat4& ownerWorldTransform,
		           std::vector<glm::mat4>& localTransforms);

	private:
		struct LegTarget
		{
			FootPlacementContact contact;
			IKTarget ikTarget;
			glm::vec3 animatedFootWorld = glm::vec3(0.0f);
			glm::vec3 targetWorld = glm::vec3(0.0f);
			bool valid = false;
		};

		FootPlacementSettings m_Settings;
		FootPlacementRuntimeState m_RuntimeState;
		FootPlacementFootState m_LeftState;
		FootPlacementFootState m_RightState;
		FootPlacementDebugData m_DebugData;
		VansFootGroundProbe m_GroundProbe;
		IKChainDefinition m_LeftLegChain;
		IKChainDefinition m_RightLegChain;

		int m_PelvisIndex = -1;
		int m_LeftHipIndex = -1;
		int m_LeftKneeIndex = -1;
		int m_LeftFootIndex = -1;
		int m_RightHipIndex = -1;
		int m_RightKneeIndex = -1;
		int m_RightFootIndex = -1;
		glm::vec3 m_LeftFootLocalUp = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 m_RightFootLocalUp = glm::vec3(0.0f, 1.0f, 0.0f);

		float m_GlobalWeight = 0.0f;
		float m_GlobalWeightVelocity = 0.0f;
		float m_PelvisOffsetWorld = 0.0f;
		float m_PelvisVelocity = 0.0f;
		bool m_Configured = false;

		FootPlacementContact ProbeFoot(const glm::mat4& ownerWorldTransform,
		                                   const std::vector<glm::mat4>& modelTransforms,
		                                   int footIndex,
		                                   FootPlacementDebugLeg* debugLeg) const;

		LegTarget UpdateLegTarget(float deltaTime,
		                          const glm::mat4& ownerWorldTransform,
		                          const std::vector<glm::mat4>& modelTransforms,
		                          int footIndex,
		                          const glm::vec3& footLocalUp,
		                          FootPlacementFootState& state,
		                          const FootPlacementContact& contact,
		                          float animationPlantWeight) const;

		void ApplyPelvisOffset(float deltaTime,
		                       const Skeleton& skeleton,
		                       const glm::mat4& ownerWorldTransform,
		                       const LegTarget& left,
		                       const LegTarget& right,
		                       std::vector<glm::mat4>& localTransforms);

		void SolveLeg(float deltaTime,
		              const Skeleton& skeleton,
		              const IKChainDefinition& chain,
		              const LegTarget& legTarget,
		              FootPlacementFootState& state,
		              std::vector<glm::mat4>& localTransforms,
		              std::vector<glm::mat4>& modelTransforms);

		static void PopulateLegDebug(FootPlacementDebugLeg& debugLeg,
		                             const glm::mat4& ownerWorldTransform,
		                             const std::vector<glm::mat4>& modelTransforms,
		                             int hipIndex,
		                             int kneeIndex,
		                             int footIndex,
		                             const LegTarget& target,
		                             const FootPlacementFootState& state,
		                             float animationPlantWeight);
	};
}
