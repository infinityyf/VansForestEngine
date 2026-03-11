#pragma once

#include "VansAnimationTypes.h"
#include "../RenderCore/VulkanCore/VansVKBuffer.h"

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux
#endif
#include "vulkan/vulkan.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace VansGraphics
{
	class VansRenderNode;

	// ────────────────────────────────────────────────────────────────
	//  VansAnimationNode
	//
	//  Central animation entity in the scene. Stored in VansScene::m_AnimationNodes[].
	//  Each node links to a VansRenderNode (skinned mesh), owns a skeleton + clips,
	//  handles playback/crossfade, computes bone matrices on CPU, and uploads to GPU.
	// ────────────────────────────────────────────────────────────────

	class VansAnimationNode
	{
	public:
		// ─── Construction & Destruction ───
		VansAnimationNode(const std::string& name);
		~VansAnimationNode();

		// ─── Linked RenderNode ───
		void SetRenderNode(VansRenderNode* renderNode);
		VansRenderNode* GetRenderNode() const { return m_RenderNode; }

		// ─── Skeleton ───
		void SetSkeleton(const Skeleton& skeleton);
		const Skeleton& GetSkeleton() const { return m_Skeleton; }

		// ─── Clip Management ───
		void AddClip(const VansAnimationClip& clip);
		void AddClip(VansAnimationClip&& clip);
		bool RemoveClip(const std::string& clipName);
		const VansAnimationClip* GetClip(const std::string& clipName) const;
		std::vector<std::string> GetClipNames() const;

		// ─── Playback Control ───
		void Play(const std::string& clipName, const AnimationPlaySettings& settings = {});
		void CrossFade(const std::string& clipName, float blendDuration = 0.2f,
		               const AnimationPlaySettings& settings = {});
		void Pause();
		void Resume();
		void Stop();
		void SetTime(float time);
		void SetSpeed(float speed);
		void SetLoop(bool loop);

		// ─── State Queries ───
		AnimationState GetState() const { return m_State; }
		float GetCurrentTime() const { return m_CurrentTime; }
		float GetDuration() const;
		float GetNormalizedTime() const;
		std::string GetCurrentClipName() const { return m_CurrentClipName; }

		// ─── Events ───
		void AddEvent(const std::string& clipName, AnimationEvent event);

		// ─── Bone Overrides (IK / Procedural) ───
		void SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform);
		void ClearBoneOverride(const std::string& boneName);

		// ─── Per-Frame Update (called by VansScene) ───
		void Update(float deltaTime);

		// ─── GPU Resource Management ───
		bool InitGPUResources(VkDevice device, uint32_t framesInFlight);
		void DestroyGPUResources();
		void UploadBoneMatrices(uint32_t frameIndex);

		VansVKBuffer& GetBoneBuffer(uint32_t frameIndex) { return m_BoneBuffers[frameIndex]; }

		// ─── Accessors ───
		std::string GetName() const { return m_Name; }
		const BoneMatricesSSBO& GetBoneSSBO() const { return m_BoneMatricesSSBO; }

	private:
		std::string m_Name;

		// ─── Linked render node ───
		VansRenderNode* m_RenderNode = nullptr;

		// ─── Skeleton & Clips ───
		Skeleton m_Skeleton;
		std::unordered_map<std::string, VansAnimationClip> m_Clips;

		// ─── Playback State ───
		AnimationState m_State = AnimationState::Stopped;
		std::string m_CurrentClipName;
		std::string m_PreviousClipName;     // for crossfade source
		float m_CurrentTime   = 0.0f;
		float m_PreviousTime  = 0.0f;       // time in previous clip (crossfade)
		float m_BlendAlpha    = 0.0f;
		float m_BlendDuration = 0.2f;
		bool  m_PingPongReversing = false;
		AnimationPlaySettings m_PlaySettings;

		// ─── Bone Overrides ───
		std::unordered_map<std::string, glm::mat4> m_BoneOverrides;

		// ─── Events ───
		std::unordered_map<std::string, std::vector<AnimationEvent>> m_Events;
		float m_LastEventTime = 0.0f;   // tracks which events have fired this playthrough

		// ─── Computed Bone Matrices (CPU side) ───
		BoneMatricesSSBO m_BoneMatricesSSBO;

		// ─── GPU Buffers (one per frame-in-flight) ───
		VkDevice m_Device = VK_NULL_HANDLE;
		std::vector<VansVKBuffer> m_BoneBuffers;
		uint32_t m_FramesInFlight = 0;

		// ─── Internal Methods ───
		void AdvanceTime(float dt);
		void FireEvents();
		void ComputeBoneTransforms(const std::string& clipName, float time,
		                           std::vector<glm::mat4>& outLocalTransforms);
		void BlendTransforms(const std::vector<glm::mat4>& a,
		                     const std::vector<glm::mat4>& b,
		                     float alpha,
		                     std::vector<glm::mat4>& outBlended);
		void ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms);
		void UpdateHierarchy(std::vector<glm::mat4>& localTransforms);
		void BuildFinalMatrices();

		void InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
		                          float time,
		                          glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);
	};

}  // namespace VansGraphics
