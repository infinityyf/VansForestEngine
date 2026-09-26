#pragma once

#include "../../AnimationCore/VansAnimationTypes.h"
#include "../VulkanCore/VansVKBuffer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	// RenderCore owns all Vulkan resources used to bind one animation pose to
	// skinned render nodes. AnimationCore only publishes CPU pose data.
	class VansAnimationGpuBinding
	{
	public:
		~VansAnimationGpuBinding();

		bool Initialize(
			VkDevice device,
			std::uint32_t framesInFlight,
			const BoneMatricesSSBO& initialPose,
			const std::string& debugName);
		bool UploadSkinWeights(
			const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData,
			const std::string& debugName);
		void UploadPose(std::uint32_t frameIndex, const BoneMatricesSSBO& pose);
		void Destroy();

		std::uint32_t GetSubmeshCount() const;
		VansVKBuffer& GetCurrentPoseBuffer(std::uint32_t frameIndex);
		VansVKBuffer& GetPreviousPoseBuffer(std::uint32_t frameIndex);
		VansVKBuffer& GetBoneIdBuffer(std::uint32_t submeshIndex);
		VansVKBuffer& GetBoneWeightBuffer(std::uint32_t submeshIndex);

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		std::vector<VansVKBuffer> m_CurrentPoseBuffers;
		std::vector<VansVKBuffer> m_PreviousPoseBuffers;
		std::vector<VansVKBuffer> m_BoneIdBuffers;
		std::vector<VansVKBuffer> m_BoneWeightBuffers;
		BoneMatricesSSBO m_PreviousPose{};
		bool m_HasPreviousPose = false;
	};
}
