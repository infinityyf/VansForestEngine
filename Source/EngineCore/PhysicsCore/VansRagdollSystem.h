#pragma once

#include "VansRagdollTypes.h"

#include <../../GLM/glm.hpp>

#include <string>
#include <vector>

namespace VansGraphics
{
	class VansAnimationNode;
	struct Skeleton;
}

namespace VansEngine
{
	class VansRagdollSystem
	{
	public:
		static VansRagdollSystem& GetInstance();

		void Initialize();
		void Shutdown();

		// ── 生命周期 ──────────────────────────────────────────────────
		bool CreateRagdoll(VansGraphics::VansAnimationNode* animNode, const RagdollProfile& profile);
		void DestroyRagdoll(VansGraphics::VansAnimationNode* animNode);
		bool HasRagdoll(VansGraphics::VansAnimationNode* animNode) const;

		// ── 驱动模式 ─────────────────────────────────────────────────
		void SetDriveMode(VansGraphics::VansAnimationNode* animNode,
		                  RagdollDriveMode mode,
		                  const glm::vec3& initialVelocity = glm::vec3(0.0f));
		RagdollDriveMode GetDriveMode(VansGraphics::VansAnimationNode* animNode) const;

		void SetBlendWeight(VansGraphics::VansAnimationNode* animNode, float weight);
		float GetBlendWeight(VansGraphics::VansAnimationNode* animNode) const;
		int GetBodyCount(VansGraphics::VansAnimationNode* animNode) const;
		int GetJointCount(VansGraphics::VansAnimationNode* animNode) const;
		std::vector<std::string> GetBodyBoneNames(VansGraphics::VansAnimationNode* animNode) const;

		void ApplyImpulse(VansGraphics::VansAnimationNode* animNode,
		                  const std::string& boneName,
		                  const glm::vec3& worldImpulse);

		void PostAnimationUpdate(VansGraphics::VansAnimationNode* animNode);

	private:
		VansRagdollSystem() = default;
		~VansRagdollSystem() = default;
		VansRagdollSystem(const VansRagdollSystem&) = delete;
		VansRagdollSystem& operator=(const VansRagdollSystem&) = delete;

		// ── 每帧同步 ─────────────────────────────────────────────────
		void SyncBodiesToAnimPose(RagdollInstance& inst);
		void SyncAnimToPhysicsPose(RagdollInstance& inst);
		void BlendAndApplyPose(RagdollInstance& inst);

		// ── 模式切换 ─────────────────────────────────────────────────
		void WarmStartBodies(RagdollInstance& inst, const glm::vec3& initialVelocity);
		void ReenableKinematic(RagdollInstance& inst);

		// ── 资源释放 ─────────────────────────────────────────────────
		void ReleaseInstance(RagdollInstance& inst);
		RagdollInstance* FindInstance(VansGraphics::VansAnimationNode* animNode);
		const RagdollInstance* FindInstance(VansGraphics::VansAnimationNode* animNode) const;

		// ── 创建辅助 ─────────────────────────────────────────────────
		static glm::mat4 MakeTRS(const glm::vec3& pos,
		                         const glm::vec3& rotDeg,
		                         const glm::vec3& scale);
		static int FindNearestParentEntry(const RagdollInstance& inst,
		                                 const VansGraphics::Skeleton& skeleton,
		                                 int childBoneIndex);
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
