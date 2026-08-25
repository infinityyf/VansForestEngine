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

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	class VansRenderNode;

	enum class VansAnimationEvaluationPurpose : std::uint8_t
	{
		Gameplay,
		EditorPreview
	};

	struct VansAnimationFrameContext
	{
		VansAnimationEvaluationPurpose purpose;
		float deltaTime;

		bool AllowsOwnerMotion() const
		{
			return purpose == VansAnimationEvaluationPurpose::Gameplay;
		}
	};

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
		bool SetController(VansAnimationController* controller);
		VansAnimationController* GetController() const { return m_Controller; }
		VansSkeletonPoseView GetFinalPoseView() const
		{
			return m_Controller ? m_Controller->GetFinalPoseView(m_Skeleton)
				: VansSkeletonPoseView{};
		}
		// Locomotion is evaluated on the source controller for source-proxy
		// retargeting, while the target controller only post-processes the
		// retargeted pose. Character motion must query the same controller that
		// owns Motion Matching and Root Motion.
		VansAnimationController* GetLocomotionController() const
		{
			return m_RetargetEnabled && m_SourceController
				? m_SourceController.get()
				: m_Controller;
		}
		bool ConfigureRetargetSource(const Skeleton& sourceSkeleton,
		                             std::unique_ptr<VansAnimationController> sourceController,
		                             const VansRetargetRuntimeDesc& desc,
		                             std::string& error);
		bool IsRetargetEnabled() const { return m_RetargetEnabled; }
		const Skeleton& GetRetargetSourceSkeleton() const { return m_SourceSkeleton; }
		VansAnimationController* GetRetargetSourceController() { return m_SourceController.get(); }
		const VansAnimationController* GetRetargetSourceController() const { return m_SourceController.get(); }
		const VansRetargetRuntimeDesc& GetRetargetRuntimeDesc() const { return m_RetargetDesc; }
		bool ReplaceRetargetSourceController(std::unique_ptr<VansAnimationController> controller);

		// Playback control, delegated to the active controller.
		void Play(VansAnimationEvaluationPurpose purpose);
		void Play(const std::string& stateName,
			VansAnimationEvaluationPurpose purpose);
		void Pause();
		void Resume();
		void Stop();
		VansGraphSetSwitchResult SwitchGraphSet(const std::string& graphSetId);
		const std::string& GetActiveGraphSetId() const;
		const std::string& GetIncomingGraphSetId() const;
		bool IsGraphSetTransitioning() const;
		float GetGraphSetTransitionProgress() const;

		// State queries
		AnimationState GetState() const;
		float GetCurrentPlayTime() const;
		float GetDuration() const;
		float GetNormalizedTime() const;
		std::string GetCurrentStateName() const;
		float GetSpeed() const;

		const VansAnimationFrameVector<VansAnimationEventSample>& GetSampledEvents() const;

		// Root motion
		void EnableRootMotion(bool enable);
		bool IsRootMotionEnabled() const;
		void SetTransformID(uint32_t transformID);
		uint32_t GetTransformID() const { return m_TransformID; }
		void SetRootBone(const std::string& boneName);
		glm::vec3 GetRootMotionDelta() const;
		glm::quat GetRootRotationDelta() const;
		bool HasRootMotionDelta() const;

		// Bone overrides for IK/procedural animation.
		void SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform);
		bool TryGetBoneLocalTransform(const std::string& boneName, glm::mat4& transform) const;
		bool TryGetCurrentBoneLocalTransform(const std::string& boneName, glm::mat4& transform) const;
		void ClearBoneOverride(const std::string& boneName);

		// Per-frame update, called by VansScene.
		void Update(const VansAnimationFrameContext& context);
		void PrepareAnimationFrame(const VansAnimationFrameContext& context);
		void GatherAnimationWorldQueries();
		void ResolveAnimationWorldQueries(const std::vector<VansWorldQueryResult>& results);
		bool HasAnimationWorldQueries() const;
		const std::vector<VansWorldQueryRequest>& GetAnimationWorldQueries() const;
		void PrepareLocomotionFrame(float deltaTime, const Vans::VansCharacterTrajectory& trajectory);

		// GPU resources
		bool InitGPUResources(VkDevice device, uint32_t framesInFlight);
		void DestroyGPUResources();
		void UploadBoneMatrices(uint32_t frameIndex);
		void UploadBoneMatrices(
			uint32_t frameIndex,
			const BoneMatricesSSBO& boneMatrices);
		void UploadPerSubmeshBoneBuffers(const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData);

		VansVKBuffer& GetBoneBuffer(uint32_t frameIndex) { return m_BoneBuffers[frameIndex]; }
		VansVKBuffer& GetPreviousBoneBuffer(uint32_t frameIndex) { return m_PreviousBoneBuffers[frameIndex]; }
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
		Skeleton m_SourceSkeleton;
		std::unique_ptr<VansAnimationController> m_SourceController;
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
		bool     m_LocomotionFramePrepared = false;

		// Bone overrides
		std::unordered_map<std::string, glm::mat4> m_BoneOverrides;

		struct NodeTransformBinding
		{
			std::string nodeName;
			std::string nodePath;
			uint32_t transformID = UINT32_MAX;
			VansRenderNode* renderNode = nullptr;
		};
		std::vector<NodeTransformBinding> m_NodeTransformBindings;

		// CPU-side fallback bone matrix storage used when no controller exists.
		BoneMatricesSSBO m_BoneMatricesSSBO;

		// GPU buffers
		VkDevice m_Device = VK_NULL_HANDLE;
		std::vector<VansVKBuffer> m_BoneBuffers;
		std::vector<VansVKBuffer> m_PreviousBoneBuffers;
		BoneMatricesSSBO m_PreviousBoneMatricesSSBO{};
		bool m_HasUploadedBoneMatrices = false;
		uint32_t m_FramesInFlight = 0;
		std::vector<VansVKBuffer> m_PerSubmeshBoneIDBuffers;
		std::vector<VansVKBuffer> m_PerSubmeshBoneWeightBuffers;

		// Internal helpers
		void ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms);
		void ApplyRootMotionToTransform(const glm::vec3& deltaPos, const glm::quat& deltaRot);
		void RebuildNodeTransformBindings();
		void ApplySampledNodeTransforms();
		void SyncRetargetParameters();
	};

}  // namespace VansGraphics
