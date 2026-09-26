#include "VansAnimationGpuBinding.h"

#include "../../Util/VansLog.h"

namespace VansGraphics
{
	VansAnimationGpuBinding::~VansAnimationGpuBinding()
	{
		Destroy();
	}

	bool VansAnimationGpuBinding::Initialize(
		VkDevice device,
		std::uint32_t framesInFlight,
		const BoneMatricesSSBO& initialPose,
		const std::string& debugName)
	{
		Destroy();
		if (device == VK_NULL_HANDLE || framesInFlight == 0)
			return false;

		m_Device = device;
		m_CurrentPoseBuffers.resize(framesInFlight);
		m_PreviousPoseBuffers.resize(framesInFlight);
		const VkDeviceSize bufferSize = sizeof(BoneMatricesSSBO);
		for (std::uint32_t frameIndex = 0; frameIndex < framesInFlight; ++frameIndex)
		{
			if (!m_CurrentPoseBuffers[frameIndex].CreatVulkanBuffer(
				m_Device, bufferSize, VK_FORMAT_R32_SFLOAT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
				!m_PreviousPoseBuffers[frameIndex].CreatVulkanBuffer(
					m_Device, bufferSize, VK_FORMAT_R32_SFLOAT,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			{
				VANS_LOG_ERROR("[AnimationGpuBinding] " << debugName
					<< ": failed to create pose buffers for frame " << frameIndex);
				Destroy();
				return false;
			}
			m_CurrentPoseBuffers[frameIndex].SetBufferData(
				&initialPose, 0, static_cast<int>(bufferSize));
			m_PreviousPoseBuffers[frameIndex].SetBufferData(
				&initialPose, 0, static_cast<int>(bufferSize));
		}
		m_HasPreviousPose = false;
		return true;
	}

	bool VansAnimationGpuBinding::UploadSkinWeights(
		const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData,
		const std::string& debugName)
	{
		if (m_Device == VK_NULL_HANDLE)
			return false;
		if (perSubmeshBoneData.empty())
			return true;

		m_BoneIdBuffers.resize(perSubmeshBoneData.size());
		m_BoneWeightBuffers.resize(perSubmeshBoneData.size());
		for (std::uint32_t submeshIndex = 0;
			submeshIndex < static_cast<std::uint32_t>(perSubmeshBoneData.size());
			++submeshIndex)
		{
			const auto& boneData = perSubmeshBoneData[submeshIndex];
			const VkDeviceSize idBufferSize = boneData.empty()
				? 64
				: sizeof(VertexBoneID) * boneData.size();
			const VkDeviceSize weightBufferSize = boneData.empty()
				? 64
				: sizeof(VertexBoneWeight) * boneData.size();
			if (!m_BoneIdBuffers[submeshIndex].CreatVulkanBuffer(
				m_Device, idBufferSize, VK_FORMAT_R32_SFLOAT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
				!m_BoneWeightBuffers[submeshIndex].CreatVulkanBuffer(
					m_Device, weightBufferSize, VK_FORMAT_R32_SFLOAT,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			{
				VANS_LOG_ERROR("[AnimationGpuBinding] " << debugName
					<< ": failed to create skin buffers for submesh " << submeshIndex);
				Destroy();
				return false;
			}

			if (boneData.empty())
				continue;
			std::vector<VertexBoneID> boneIds(boneData.size());
			std::vector<VertexBoneWeight> boneWeights(boneData.size());
			for (std::size_t vertexIndex = 0; vertexIndex < boneData.size(); ++vertexIndex)
			{
				for (std::uint32_t influenceIndex = 0;
					influenceIndex < MAX_BONE_INFLUENCE;
					++influenceIndex)
				{
					boneIds[vertexIndex].boneIDs[influenceIndex] =
						boneData[vertexIndex].boneIDs[influenceIndex];
					boneWeights[vertexIndex].weights[influenceIndex] =
						boneData[vertexIndex].weights[influenceIndex];
				}
			}
			m_BoneIdBuffers[submeshIndex].SetBufferData(
				boneIds.data(), 0, static_cast<int>(idBufferSize));
			m_BoneWeightBuffers[submeshIndex].SetBufferData(
				boneWeights.data(), 0, static_cast<int>(weightBufferSize));
		}
		return true;
	}

	void VansAnimationGpuBinding::UploadPose(
		std::uint32_t frameIndex, const BoneMatricesSSBO& pose)
	{
		if (frameIndex >= m_CurrentPoseBuffers.size() ||
			frameIndex >= m_PreviousPoseBuffers.size())
		{
			return;
		}

		const BoneMatricesSSBO& previous = m_HasPreviousPose ? m_PreviousPose : pose;
		m_PreviousPoseBuffers[frameIndex].SetBufferData(
			&previous, 0, sizeof(BoneMatricesSSBO));
		m_CurrentPoseBuffers[frameIndex].SetBufferData(
			&pose, 0, sizeof(BoneMatricesSSBO));
		m_PreviousPose = pose;
		m_HasPreviousPose = true;
	}

	void VansAnimationGpuBinding::Destroy()
	{
		if (m_Device != VK_NULL_HANDLE)
		{
			for (auto& buffer : m_CurrentPoseBuffers)
				buffer.DestroyVulkanBuffer(m_Device);
			for (auto& buffer : m_PreviousPoseBuffers)
				buffer.DestroyVulkanBuffer(m_Device);
			for (auto& buffer : m_BoneIdBuffers)
				buffer.DestroyVulkanBuffer(m_Device);
			for (auto& buffer : m_BoneWeightBuffers)
				buffer.DestroyVulkanBuffer(m_Device);
		}
		m_CurrentPoseBuffers.clear();
		m_PreviousPoseBuffers.clear();
		m_BoneIdBuffers.clear();
		m_BoneWeightBuffers.clear();
		m_HasPreviousPose = false;
		m_Device = VK_NULL_HANDLE;
	}

	std::uint32_t VansAnimationGpuBinding::GetSubmeshCount() const
	{
		return static_cast<std::uint32_t>(m_BoneIdBuffers.size());
	}

	VansVKBuffer& VansAnimationGpuBinding::GetCurrentPoseBuffer(std::uint32_t frameIndex)
	{
		return m_CurrentPoseBuffers.at(frameIndex);
	}

	VansVKBuffer& VansAnimationGpuBinding::GetPreviousPoseBuffer(std::uint32_t frameIndex)
	{
		return m_PreviousPoseBuffers.at(frameIndex);
	}

	VansVKBuffer& VansAnimationGpuBinding::GetBoneIdBuffer(std::uint32_t submeshIndex)
	{
		return m_BoneIdBuffers.at(submeshIndex);
	}

	VansVKBuffer& VansAnimationGpuBinding::GetBoneWeightBuffer(std::uint32_t submeshIndex)
	{
		return m_BoneWeightBuffers.at(submeshIndex);
	}
}
