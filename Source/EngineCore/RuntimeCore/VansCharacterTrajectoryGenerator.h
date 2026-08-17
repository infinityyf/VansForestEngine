#pragma once

#include "VansCharacterMotion.h"

#include <deque>

namespace Vans
{
	// 持久化轨迹生成器。输入只提供目标，生成器跨帧保存计划速度、参考方向和
	// Facing 状态；Root Motion/CCT 的实际结果仅以低频误差反馈参与下一帧规划。
	class VansCharacterTrajectoryGenerator
	{
	public:
		void Reset(const glm::vec3& positionWorld, float facingYaw);
		void RecordResolvedMotion(float deltaTime,
		                         const glm::vec3& positionWorld,
		                         const glm::vec3& actualVelocityWorld,
		                         const glm::vec3& requestedVelocityWorld);
		void Update(float deltaTime,
		            const VansCharacterMotionIntent& intent,
		            const VansCharacterMotionSettings& settings,
		            const glm::vec3& positionWorld,
		            float currentFacingYaw);

		const VansCharacterTrajectory& GetTrajectory() const { return m_Trajectory; }
		const glm::vec3& GetPlannedVelocityWorld() const { return m_PlannedVelocityWorld; }
		float GetPlannedFacingYaw() const { return m_PlannedFacingYaw; }

	private:
		struct HistorySample
		{
			float time = 0.0f;
			glm::vec3 positionWorld{ 0.0f };
		};

		glm::vec3 ResolveDesiredVelocity(const VansCharacterMotionIntent& intent,
		                                 float referenceYaw) const;
		glm::vec3 AdvanceVelocity(const glm::vec3& velocity,
		                         const glm::vec3& target,
		                         float deltaTime,
		                         const VansCharacterMotionSettings& settings) const;
		glm::vec3 SampleHistoryPosition(float secondsAgo,
		                                const glm::vec3& fallbackPosition) const;

		VansCharacterTrajectory m_Trajectory;
		glm::vec3 m_PlannedVelocityWorld{ 0.0f };
		glm::vec3 m_ActualVelocityWorld{ 0.0f };
		glm::vec3 m_RequestedVelocityWorld{ 0.0f };
		float m_MotionConsumptionRatio = 1.0f;
		glm::vec2 m_PreviousMoveInputLocal{ 0.0f };
		float m_FilteredReferenceYaw = 0.0f;
		float m_PreviousReferenceYaw = 0.0f;
		float m_ReferenceYawRate = 0.0f;
		float m_PlannedFacingYaw = 0.0f;
		float m_PreviousDesiredFacingYaw = 0.0f;
		float m_DesiredFacingYawRate = 0.0f;
		float m_HistoryClock = 0.0f;
		bool m_HasActualVelocity = false;
		bool m_HasPreviousInput = false;
		bool m_HasPreviousReferenceYaw = false;
		bool m_HasPreviousDesiredFacing = false;
		bool m_Initialized = false;
		std::deque<HistorySample> m_History;
	};
}
