#pragma once

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

namespace VansGraphics
{
	struct RootMotionReconciliationSettings
	{
		bool enabled = true;
		float linearVelocityHalfLife = 0.055f;
		float angularVelocityHalfLife = 0.045f;
		float maxDuration = 0.22f;
		float maxLinearVelocityCorrection = 1200.0f;
		float maxAngularVelocityCorrectionDegreesPerSecond = 720.0f;
	};

	struct RootMotionReconciliationResult
	{
		bool active = false;
		glm::vec3 targetVelocityAnimation{ 0.0f };
		glm::vec3 appliedVelocityAnimation{ 0.0f };
		float targetYawRateDegreesPerSecond = 0.0f;
		float appliedYawRateDegreesPerSecond = 0.0f;
	};

	// Pose inertialization and root motion must cross a clip boundary as one
	// transition. This reconciler preserves the outgoing linear/angular root
	// velocity, then decays only that velocity offset onto the destination clip.
	class VansRootMotionReconciler
	{
	public:
		void Configure(const RootMotionReconciliationSettings& settings);
		void Reset();
		void RequestTransition(const glm::vec3& outgoingVelocityAnimation,
		                       float outgoingYawRateDegreesPerSecond);
		RootMotionReconciliationResult Apply(float deltaTime,
		                                         glm::vec3& inOutTranslation,
		                                         glm::quat& inOutRotation);

	private:
		RootMotionReconciliationSettings m_Settings;
		glm::vec3 m_OutgoingVelocityAnimation{ 0.0f };
		glm::vec3 m_LinearVelocityOffset{ 0.0f };
		float m_OutgoingYawRateDegreesPerSecond = 0.0f;
		float m_AngularVelocityOffsetDegreesPerSecond = 0.0f;
		float m_Elapsed = 0.0f;
		bool m_Pending = false;
		bool m_Active = false;
	};
}
