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
		const FootPlacementDebugData& GetDebugData() const { return m_DebugData; }

		void Solve(float deltaTime,
		           const Skeleton& skeleton,
		           const glm::mat4& ownerWorldTransform,
		           std::vector<glm::mat4>& localTransforms);

	private:
		struct LegSolve
		{
			SurfaceContact contact;
			IKTarget target;
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

		float m_CurrentWeight = 0.0f;
		float m_PelvisOffsetModel = 0.0f;
		bool m_Configured = false;

		SurfaceContact ProbeFoot(const glm::mat4& ownerWorldTransform,
		                         const std::vector<glm::mat4>& modelTransforms,
		                         int hipIndex,
		                         int footIndex,
		                         float legLength,
		                         FootPlacementDebugLeg* debugLeg) const;

		LegSolve BuildLegSolve(float deltaTime,
		                        const glm::mat4& ownerWorldTransform,
		                        const std::vector<glm::mat4>& modelTransforms,
		                        int footIndex,
		                        FootPlacementFootState& state,
		                        const SurfaceContact& contact) const;

		void ApplyPelvisOffset(float deltaTime,
		                       const Skeleton& skeleton,
		                       const glm::mat4& ownerWorldTransform,
		                       const std::vector<glm::mat4>& modelTransforms,
		                       const LegSolve& left,
		                       const LegSolve& right,
		                       std::vector<glm::mat4>& localTransforms);

		void SolveLeg(float deltaTime,
		              const Skeleton& skeleton,
		              const IKChainDefinition& chain,
		              const IKTarget& target,
		              FootPlacementFootState& state,
		              std::vector<glm::mat4>& localTransforms,
		              std::vector<glm::mat4>& modelTransforms);

		static std::vector<glm::mat4> BuildModelSpaceTransforms(const Skeleton& skeleton,
		                                                        const std::vector<glm::mat4>& localTransforms);
		static float EvaluateContactQuality(const FootPlacementSettings& settings, const SurfaceContact& contact);
		static float LegLength(const std::vector<glm::mat4>& modelTransforms, int hipIndex, int kneeIndex, int footIndex);
		static void PopulateLegDebug(FootPlacementDebugLeg& debugLeg,
		                             const glm::mat4& ownerWorldTransform,
		                             const std::vector<glm::mat4>& modelTransforms,
		                             int hipIndex,
		                             int kneeIndex,
		                             int footIndex,
		                             const LegSolve& solve);
		static glm::quat BuildFootRotation(const glm::mat4& ownerWorldTransform,
		                                  const glm::mat4& currentFootModel,
		                                  const glm::vec3& worldNormal);
	};
}
