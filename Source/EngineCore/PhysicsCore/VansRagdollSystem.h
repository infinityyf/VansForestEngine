#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VansRagdollTypes.h"
#include "../AnimationCore/VansAnimationNode.h"

#include <vector>

namespace VansEngine
{
	// ════════════════════════════════════════════════════════════════
	//  VansRagdollSystem
	//
	//  Singleton system that manages PhysX D6-joint ragdolls for
	//  skeletal animation nodes.
	//
	//  Typical integration per frame:
	//    1. animNode->Update(dt)             ← animation pose computed
	//    2. VansRagdollSystem::PostAnimationUpdate(animNode)
	//       • Animation mode  → kinematically drive bodies to anim pose
	//       • Physics mode    → read body poses → override bone matrices
	//       • Blend mode      → blend anim and physics transforms
	//    3. animNode->UploadBoneMatrices(0)  ← upload final matrices to GPU
	//
	//  JSON scene format (inside object.components.animation):
	//  {
	//    "ragdoll": {
	//      "profile": "Assets/Characters/hero.vragdoll",
	//      "drive_mode": "animation"   // "animation" | "physics" | "blend"
	//    }
	//  }
	// ════════════════════════════════════════════════════════════════

	class VansRagdollSystem
	{
	public:
		static VansRagdollSystem& GetInstance();

		// ─── Lifecycle ─────────────────────────────────────────────────
		void Initialize();
		void Shutdown();

		// ─── Ragdoll management ────────────────────────────────────────
		// Create bodies and joints; bodies start in kinematic mode.
		// Must be called after animNode->Update() has run at least once so
		// GetCachedGlobalTransforms() returns a valid bind pose.
		bool CreateRagdoll(VansGraphics::VansAnimationNode* animNode,
		                   const RagdollProfile& profile);

		void DestroyRagdoll(VansGraphics::VansAnimationNode* animNode);

		bool HasRagdoll(VansGraphics::VansAnimationNode* animNode) const;

		// ─── Drive mode ────────────────────────────────────────────────
		void           SetDriveMode(VansGraphics::VansAnimationNode* animNode, RagdollDriveMode mode);
		RagdollDriveMode GetDriveMode(VansGraphics::VansAnimationNode* animNode) const;

		// blendWeight: 0.0 = full animation, 1.0 = full physics (used in Blend mode)
		void  SetBlendWeight(VansGraphics::VansAnimationNode* animNode, float weight);
		float GetBlendWeight(VansGraphics::VansAnimationNode* animNode) const;

		// ─── Physics impulse ───────────────────────────────────────────
		// Apply a world-space linear impulse to a named bone's physics body.
		void ApplyImpulse(VansGraphics::VansAnimationNode* animNode,
		                  const std::string& boneName,
		                  const glm::vec3& impulse);

		// ─── Per-frame hook (called inside VansScene::UpdateAnimations) ──
		// Must be called AFTER animNode->Update(dt) and BEFORE
		// animNode->UploadBoneMatrices(0).
		void PostAnimationUpdate(VansGraphics::VansAnimationNode* animNode);

	private:
		VansRagdollSystem()  = default;
		~VansRagdollSystem() = default;
		VansRagdollSystem(const VansRagdollSystem&)            = delete;
		VansRagdollSystem& operator=(const VansRagdollSystem&) = delete;

		// ─── Instance lookup ───────────────────────────────────────────
		RagdollInstance*       FindInstance(VansGraphics::VansAnimationNode* animNode);
		const RagdollInstance* FindInstance(VansGraphics::VansAnimationNode* animNode) const;

		// ─── Per-mode update helpers ───────────────────────────────────
		// Animation mode: kinematically drive all bodies to current animation pose.
		void SyncBodiesToAnimPose(RagdollInstance& inst);

		// Physics mode: read body poses and feed them into the controller.
		void SyncAnimToPhysicsPose(RagdollInstance& inst);

		// Blend mode: blend animation global transforms with physics body poses.
		void BlendAndApplyPose(RagdollInstance& inst);

		// ─── Shared helpers ────────────────────────────────────────────
		// Get the root character world matrix for a ragdoll instance.
		static glm::mat4 GetRootWorldMatrix(const RagdollInstance& inst);

		// Convert PxTransform ↔ glm::mat4
		static glm::mat4   PxToGlm(const PxTransform& t);
		static PxTransform GlmToPx(const glm::mat4& m);

		// Blend two sets of model-space bone transforms (per-bone lerp/slerp).
		static void BlendModelTransforms(const std::vector<glm::mat4>& a,
		                                 const std::vector<glm::mat4>& b,
		                                 float t,
		                                 std::vector<glm::mat4>& out);

		// ─── Storage ───────────────────────────────────────────────────
		std::vector<RagdollInstance> m_Instances;
	};

}  // namespace VansEngine
