#pragma once

#include "VansRagdollTypes.h"

#include <../../GLM/glm.hpp>

#include <string>
#include <vector>

namespace VansGraphics { class VansScene; }

namespace VansEngine
{
	class VansRagdollSystem
	{
	public:
		static VansRagdollSystem& GetInstance();

		// ── 生命周期 ──────────────────────────────────────────────────
		bool CreateRagdoll(const Vans::VansRagdollKey& key,
		                   const RagdollProfile& profile,
		                   const Vans::VansRagdollSkeletonBinding& binding,
		                   const Vans::VansRagdollPoseView& pose);
		void DestroyRagdoll(const Vans::VansRagdollKey& key);
		bool HasRagdoll(const Vans::VansRagdollKey& key) const;

		// ── 驱动模式 ─────────────────────────────────────────────────
		bool SetDriveMode(const Vans::VansRagdollKey& key,
		                  RagdollDriveMode mode,
		                  const Vans::VansRagdollPoseView& animationPose,
		                  const glm::vec3& initialVelocity = glm::vec3(0.0f));
		RagdollDriveMode GetDriveMode(const Vans::VansRagdollKey& key) const;

		void SetBlendWeight(const Vans::VansRagdollKey& key, float weight);
		float GetBlendWeight(const Vans::VansRagdollKey& key) const;
		int GetBodyCount(const Vans::VansRagdollKey& key) const;
		int GetJointCount(const Vans::VansRagdollKey& key) const;
		RagdollDiagnostics GetDiagnostics(const Vans::VansRagdollKey& key) const;
		std::vector<std::string> GetBodyBoneNames(const Vans::VansRagdollKey& key) const;

		void ApplyImpulse(const Vans::VansRagdollKey& key,
		                  const std::string& boneName,
		                  const glm::vec3& worldImpulse);
		// 在世界命中点给指定刚体叠加速度；角速度增量单独限幅，单位 rad/s。
		bool AddLinearVelocity(const Vans::VansRagdollKey& key, const glm::vec3& delta);
		bool AddVelocityAtPosition(const Vans::VansRagdollKey& key,
		                          const std::string& boneName, const glm::vec3& worldVelocityDelta,
		                          const glm::vec3& worldPosition, float maxAngularVelocityDelta);

		bool ResolvePose(const Vans::VansRagdollKey& key,
		                 const Vans::VansRagdollPoseView& animationPose,
		                 Vans::VansRagdollPose& outPose);

	private:
		friend class VansGraphics::VansScene;
		friend class VansCharacterControllerNode;

		VansRagdollSystem() = default;
		~VansRagdollSystem() = default;
		VansRagdollSystem(const VansRagdollSystem&) = delete;
		VansRagdollSystem& operator=(const VansRagdollSystem&) = delete;

		// ── 每帧同步 ─────────────────────────────────────────────────
		void SyncBodiesToAnimationPose(
			RagdollInstance& inst, const Vans::VansRagdollPoseView& animationPose);
		bool BuildPhysicsPose(
			const RagdollInstance& inst,
			const Vans::VansRagdollPoseView& animationPose,
			std::vector<glm::mat4>& outModelTransforms) const;
		bool BuildBlendedPose(
			const RagdollInstance& inst,
			const Vans::VansRagdollPoseView& animationPose,
			std::vector<glm::mat4>& outModelTransforms) const;

		// ── 模式切换 ─────────────────────────────────────────────────
		bool WarmStartBodies(RagdollInstance& inst,
		                     const Vans::VansRagdollPoseView& animationPose,
		                     const glm::vec3& initialVelocity);
		void ReenableKinematic(RagdollInstance& inst);

		// ── 资源释放 ─────────────────────────────────────────────────
		void ShutdownLocked();
		void DestroyRagdollLocked(const Vans::VansRagdollKey& key);
		void ReleaseInstance(RagdollInstance& inst);
		bool GetFollowBoneWorldPositionLocked(const Vans::VansRagdollKey& key,
		                                      const std::string& boneName,
		                                      glm::vec3& outPos) const;
		RagdollInstance* FindInstanceLocked(const Vans::VansRagdollKey& key);
		const RagdollInstance* FindInstanceLocked(const Vans::VansRagdollKey& key) const;

		// ── 创建辅助 ─────────────────────────────────────────────────
		static glm::mat4 MakeTRS(const glm::vec3& pos,
		                         const glm::vec3& rotDeg,
		                         const glm::vec3& scale);
		static int FindNearestParentEntry(
			const RagdollInstance& inst, int childBoneIndex);
		static void BlendModelTransforms(const std::vector<glm::mat4>& a,
		                                const std::vector<glm::mat4>& b,
		                                float t,
		                                std::vector<glm::mat4>& out);

		static glm::mat4 PxToGlm(const physx::PxTransform& t);
		static physx::PxTransform GlmToPx(const glm::mat4& m);

	private:
		std::vector<RagdollInstance> m_Instances;
	};
}
