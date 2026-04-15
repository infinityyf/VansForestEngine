#pragma once

#include "VansAnimationTypes.h"
#include "VansAnimationController.h"
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
#include <memory>

namespace VansGraphics
{
	class VansRenderNode;

	// ────────────────────────────────────────────────────────────────
	//  VansAnimationNode
	//
	//  场景中的动画实体（存储在 VansScene::m_AnimationNodes[]）。
	//  每个 Node 关联一组 VansRenderNode（蒙皮网格），持有骨骼数据，
	//  通过绑定的 VansAnimationController 驱动状态机和混合逻辑，
	//  在 CPU 侧完成骨骼矩阵计算后上传到 GPU。
	//
	//  Controller 管理: clips / states / transitions / parameters / playback
	//  Node 管理: skeleton / GPU 资源 / bone overrides / root motion 应用
	// ────────────────────────────────────────────────────────────────

	class VansAnimationNode
	{
	public:
		// ─── 构造 & 析构 ───
		VansAnimationNode(const std::string& name);
		~VansAnimationNode();

		// ─── 关联 RenderNode ───
		void SetRenderNode(VansRenderNode* renderNode);
		void SetRenderNodes(const std::vector<VansRenderNode*>& nodes);
		VansRenderNode* GetRenderNode() const { return m_RenderNodes.empty() ? nullptr : m_RenderNodes[0]; }
		const std::vector<VansRenderNode*>& GetRenderNodes() const { return m_RenderNodes; }

		// ─── 骨骼 ───
		void SetSkeleton(const Skeleton& skeleton);
		const Skeleton& GetSkeleton() const { return m_Skeleton; }

		// ─── Controller 绑定 ───
		void SetController(VansAnimationController* controller);
		VansAnimationController* GetController() const { return m_Controller; }

		// ─── 播放控制（委托给 Controller）───
		void Play();
		void Play(const std::string& stateName);
		void Pause();
		void Resume();
		void Stop();

		// ─── 状态查询（委托给 Controller）───
		AnimationState GetState() const;
		float GetCurrentPlayTime() const;
		float GetDuration() const;
		float GetNormalizedTime() const;
		std::string GetCurrentStateName() const;
		float GetSpeed() const;

		// ─── Events ───
		void AddEvent(const std::string& clipName, AnimationEvent event);

		// ─── Root Motion ───
		void EnableRootMotion(bool enable);
		bool IsRootMotionEnabled() const;
		void SetTransformID(uint32_t transformID);
		uint32_t GetTransformID() const { return m_TransformID; }
		void SetRootBone(const std::string& boneName);
		glm::vec3 GetRootMotionDelta() const;
		glm::quat GetRootRotationDelta() const;

		// ─── Bone Overrides (IK / Procedural) ───
		void SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform);
		void ClearBoneOverride(const std::string& boneName);

		// ─── 每帧更新（VansScene 调用）───
		void Update(float deltaTime);

		// ─── GPU 资源管理 ───
		bool InitGPUResources(VkDevice device, uint32_t framesInFlight);
		void DestroyGPUResources();
		void UploadBoneMatrices(uint32_t frameIndex);
		void UploadPerSubmeshBoneBuffers(const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData);

		VansVKBuffer& GetBoneBuffer(uint32_t frameIndex) { return m_BoneBuffers[frameIndex]; }
		VansVKBuffer& GetBoneIDBuffer(uint32_t submeshIndex) { return m_PerSubmeshBoneIDBuffers[submeshIndex]; }
		VansVKBuffer& GetBoneWeightBuffer(uint32_t submeshIndex) { return m_PerSubmeshBoneWeightBuffers[submeshIndex]; }
		uint32_t GetSubmeshBufferCount() const { return static_cast<uint32_t>(m_PerSubmeshBoneIDBuffers.size()); }

		// ─── 访问器 ───
		std::string GetName() const { return m_Name; }
		const BoneMatricesSSBO& GetBoneSSBO() const;

	private:
		std::string m_Name;

		// ─── 关联 render node(s) ───
		std::vector<VansRenderNode*> m_RenderNodes;

		// ─── 骨骼 ───
		Skeleton m_Skeleton;

		// ─── 动画控制器（外部持有，Node 不拥有生命周期）───
		VansAnimationController* m_Controller = nullptr;

		// ─── Root Motion 应用 ───
		uint32_t m_TransformID           = 0;
		bool     m_HasTransformID        = false;

		// ─── Bone Overrides ───
		std::unordered_map<std::string, glm::mat4> m_BoneOverrides;

		// ─── Events ───
		std::unordered_map<std::string, std::vector<AnimationEvent>> m_Events;
		float m_LastEventTime = 0.0f;

		// ─── CPU 侧骨骼矩阵（无 Controller 时使用的 fallback）───
		BoneMatricesSSBO m_BoneMatricesSSBO;

		// ─── GPU Buffers ───
		VkDevice m_Device = VK_NULL_HANDLE;
		std::vector<VansVKBuffer> m_BoneBuffers;
		uint32_t m_FramesInFlight = 0;
		std::vector<VansVKBuffer> m_PerSubmeshBoneIDBuffers;
		std::vector<VansVKBuffer> m_PerSubmeshBoneWeightBuffers;

		// ─── 内部方法 ───
		void ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms);
		void ApplyRootMotionToTransform(const glm::vec3& deltaPos, const glm::quat& deltaRot);
		void FireEvents();
	};

}  // namespace VansGraphics
