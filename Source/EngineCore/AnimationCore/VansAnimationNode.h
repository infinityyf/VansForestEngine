#pragma once

#include "../VansNode.h"
#include "VansAnimationTypes.h"
#include "VansAnimationController.h"
#include "Retargeting/VansRetargetProcessor.h"
#include "../ScriptCore/VansTransform.h"
#include "../RenderCore/VulkanCore/VansVKBuffer.h"

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux
#endif
#include "vulkan/vulkan.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	class VansRenderNode;

	// Scene animation entity stored in VansScene::m_AnimationNodes.
	// A node owns skeleton/GPU state, binds one or more VansRenderNode meshes,
	// and delegates clip/state-machine playback to VansAnimationController.
	class VansAnimationNode : public VansNode
	{
	public:
		// Construction
		VansAnimationNode(const std::string& name);
		~VansAnimationNode();

		// Render node binding
		void SetRenderNode(VansRenderNode* renderNode);
		void SetRenderNodes(const std::vector<VansRenderNode*>& nodes);
		VansRenderNode* GetRenderNode() const { return m_RenderNodes.empty() ? nullptr : m_RenderNodes[0]; }
		const std::vector<VansRenderNode*>& GetRenderNodes() const { return m_RenderNodes; }

		// Skeleton
		void SetSkeleton(const Skeleton& skeleton);
		const Skeleton& GetSkeleton() const { return m_Skeleton; }

		// Controller binding
		void SetController(VansAnimationController* controller);
		VansAnimationController* GetController() const { return m_Controller; }
		void ConfigureRetargetSource(const Skeleton& sourceSkeleton,
		                             std::unique_ptr<VansAnimationController> sourceController,
		                             const VansRetargetRuntimeDesc& desc);
		bool IsRetargetEnabled() const { return m_RetargetEnabled; }

		// Playback control, delegated to the active controller.
		void Play();
		void Play(const std::string& stateName);
		void Pause();
		void Resume();
		void Stop();

		// State queries
		AnimationState GetState() const;
		float GetCurrentPlayTime() const;
		float GetDuration() const;
		float GetNormalizedTime() const;
		std::string GetCurrentStateName() const;
		float GetSpeed() const;

		// Events
		void AddEvent(const std::string& clipName, AnimationEvent event);

		// Root motion
		void EnableRootMotion(bool enable);
		bool IsRootMotionEnabled() const;
		void SetTransformID(uint32_t transformID);
		uint32_t GetTransformID() const { return m_TransformID; }
		void SetRootBone(const std::string& boneName);
		glm::vec3 GetRootMotionDelta() const;
		glm::quat GetRootRotationDelta() const;

		// Bone overrides for IK/procedural animation.
		void SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform);
		void ClearBoneOverride(const std::string& boneName);

		// Per-frame update, called by VansScene.
		void Update(float deltaTime);

		// GPU resources
		bool InitGPUResources(VkDevice device, uint32_t framesInFlight);
		void DestroyGPUResources();
		void UploadBoneMatrices(uint32_t frameIndex);
		void UploadPerSubmeshBoneBuffers(const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData);

		VansVKBuffer& GetBoneBuffer(uint32_t frameIndex) { return m_BoneBuffers[frameIndex]; }
		VansVKBuffer& GetBoneIDBuffer(uint32_t submeshIndex) { return m_PerSubmeshBoneIDBuffers[submeshIndex]; }
		VansVKBuffer& GetBoneWeightBuffer(uint32_t submeshIndex) { return m_PerSubmeshBoneWeightBuffers[submeshIndex]; }
		uint32_t GetSubmeshBufferCount() const { return static_cast<uint32_t>(m_PerSubmeshBoneIDBuffers.size()); }

		// Accessors
		std::string GetName() const { return m_Name; }
		const BoneMatricesSSBO& GetBoneSSBO() const;

		// .vanimator file path for editor/runtime diagnostics.
		void SetAnimatorFilePath(const std::string& path) { m_AnimatorFilePath = path; }
		std::string GetAnimatorFilePath() const { return m_AnimatorFilePath; }

	private:
		std::string m_Name;

		// Bound render node(s)
		std::vector<VansRenderNode*> m_RenderNodes;

		// Target skeleton driven by this node.
		Skeleton m_Skeleton;

		// Scene-owned target controller plus optional source-proxy controller.
		VansAnimationController* m_Controller = nullptr;
		std::unique_ptr<VansAnimationController> m_SourceController;
		Skeleton m_SourceSkeleton;
		VansRetargetRuntimeDesc m_RetargetDesc;
		VansRetargetProcessor m_RetargetProcessor;
		bool m_RetargetEnabled = false;
		bool m_RetargetPoseAuditLogged = false;
		int m_LastRetargetSourceMMSwitchCount = -1;
		std::string m_LastRetargetSourceMMActiveClip;
		std::string m_LastRetargetSourceMMSelectedClip;

		// .vanimator file path
		std::string m_AnimatorFilePath;

		// Root motion application
		uint32_t m_TransformID           = 0;
		bool     m_HasTransformID        = false;

		// Bone overrides
		std::unordered_map<std::string, glm::mat4> m_BoneOverrides;

		// Events
		std::unordered_map<std::string, std::vector<AnimationEvent>> m_Events;
		float m_LastEventTime = 0.0f;

		// CPU-side fallback bone matrix storage used when no controller exists.
		BoneMatricesSSBO m_BoneMatricesSSBO;

		// GPU buffers
		VkDevice m_Device = VK_NULL_HANDLE;
		std::vector<VansVKBuffer> m_BoneBuffers;
		uint32_t m_FramesInFlight = 0;
		std::vector<VansVKBuffer> m_PerSubmeshBoneIDBuffers;
		std::vector<VansVKBuffer> m_PerSubmeshBoneWeightBuffers;

		// Internal helpers
		void ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms);
		void ApplyRootMotionToTransform(const glm::vec3& deltaPos, const glm::quat& deltaRot);
		void FireEvents();
		void SyncRetargetParameters();
	};

}  // namespace VansGraphics
