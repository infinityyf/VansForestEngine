#pragma once

#include "VansAnimationTypes.h"
#include "../ScriptCore/VansTransform.h"
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

		// ─── Linked RenderNode(s) ───
		// Single-mesh path: SetRenderNode()
		// Multi-mesh path:  SetRenderNodes() links all expanded child nodes
		void SetRenderNode(VansRenderNode* renderNode);
		void SetRenderNodes(const std::vector<VansRenderNode*>& nodes);
		VansRenderNode* GetRenderNode() const { return m_RenderNodes.empty() ? nullptr : m_RenderNodes[0]; }
		const std::vector<VansRenderNode*>& GetRenderNodes() const { return m_RenderNodes; }

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
		float GetSpeed() const { return m_PlaySettings.speed; }

		// ─── Events ───
		void AddEvent(const std::string& clipName, AnimationEvent event);

		// ─── Root Motion ───
		void EnableRootMotion(bool enable);
		bool IsRootMotionEnabled() const { return m_RootMotionEnabled; }
		void SetTransformID(uint32_t transformID);
		uint32_t GetTransformID() const { return m_TransformID; }
		void SetRootBone(const std::string& boneName);
		glm::vec3 GetRootMotionDelta() const { return m_LastRootMotionDelta; }
		glm::quat GetRootRotationDelta() const { return m_LastRootRotationDelta; }

		// ─── Bone Overrides (IK / Procedural) ───
		void SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform);
		void ClearBoneOverride(const std::string& boneName);

		// ─── Per-Frame Update (called by VansScene) ───
		void Update(float deltaTime);

		// ─── GPU Resource Management ───
		bool InitGPUResources(VkDevice device, uint32_t framesInFlight);
		void DestroyGPUResources();
		void UploadBoneMatrices(uint32_t frameIndex);

		// Upload per-submesh bone ID and weight buffers to GPU.
		// Each submesh gets its own pair of buffers (no offset needed in shader).
		// Must be called after InitGPUResources so m_Device is valid.
		void UploadPerSubmeshBoneBuffers(const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData);

		VansVKBuffer& GetBoneBuffer(uint32_t frameIndex) { return m_BoneBuffers[frameIndex]; }
		VansVKBuffer& GetBoneIDBuffer(uint32_t submeshIndex) { return m_PerSubmeshBoneIDBuffers[submeshIndex]; }
		VansVKBuffer& GetBoneWeightBuffer(uint32_t submeshIndex) { return m_PerSubmeshBoneWeightBuffers[submeshIndex]; }
		uint32_t GetSubmeshBufferCount() const { return static_cast<uint32_t>(m_PerSubmeshBoneIDBuffers.size()); }

		// ─── Accessors ───
		std::string GetName() const { return m_Name; }
		const BoneMatricesSSBO& GetBoneSSBO() const { return m_BoneMatricesSSBO; }

	private:
		std::string m_Name;

		// ─── Linked render node(s) ───
		// Vector supports both single-mesh (1 node) and multi-mesh (N nodes)
		std::vector<VansRenderNode*> m_RenderNodes;

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

		// ─── Root Motion ───
		bool     m_RootMotionEnabled     = false;
		int      m_RootBoneIndex         = -1;     // auto-detected or user-set
		uint32_t m_TransformID           = 0;      // shared transform from render node group
		bool     m_HasTransformID        = false;  // true once SetTransformID() called
		bool     m_LoopJustWrapped       = false;  // set by AdvanceTime on loop wrap
		glm::vec3 m_PrevRootPosition     = glm::vec3(0.0f);
		glm::quat m_PrevRootRotation     = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		bool     m_RootMotionInitialized = false;   // false until first frame samples root
		glm::vec3 m_LastRootMotionDelta  = glm::vec3(0.0f);   // exposed for physics
		glm::quat m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

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
		// Per-submesh bone ID and weight buffers (parallel vectors, one entry per submesh).
		// Each submesh gets its own GPU buffer so the vertex shader indexes directly
		// with gl_VertexIndex — no offset needed.
		std::vector<VansVKBuffer> m_PerSubmeshBoneIDBuffers;
		std::vector<VansVKBuffer> m_PerSubmeshBoneWeightBuffers;

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
		void ExtractRootMotion(std::vector<glm::mat4>& localTransforms);
		void UpdateHierarchy(std::vector<glm::mat4>& localTransforms);
		void BuildFinalMatrices();
		int  DetectRootBoneIndex() const;

		void InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
		                          float time,
		                          glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);
	};

}  // namespace VansGraphics
