#include "VansDrawSubmission.h"

#include "VansMaterial.h"
#include "VansRenderNode.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansPipeline.h"
#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansVKCommandBuffer.h"
#include "VulkanCore/VansVKDrawInstanceArena.h"
#include "../Util/VansLog.h"
#include "../RuntimeCore/VansFramePhase.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>

namespace
{
	template<typename T>
	void HashCombine(std::uint64_t& seed, const T& value)
	{
		seed ^= static_cast<std::uint64_t>(std::hash<T>{}(value))
			+ 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
	}

	std::uint64_t BuildDescriptorHash(const std::vector<VkDescriptorSet>& descriptorSets)
	{
		std::uint64_t result = 0xcbf29ce484222325ull;
		for (VkDescriptorSet descriptorSet : descriptorSets)
			HashCombine(result, descriptorSet);
		return result;
	}

	bool DescriptorSetsLess(
		const std::vector<VkDescriptorSet>& lhs,
		const std::vector<VkDescriptorSet>& rhs)
	{
		return std::lexicographical_compare(
			lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::less<VkDescriptorSet>{});
	}

	bool GeometryLess(
		const VansGraphics::VansGeometryKey& lhs,
		const VansGraphics::VansGeometryKey& rhs)
	{
		if (lhs.vertexBuffer != rhs.vertexBuffer)
			return std::less<VkBuffer>{}(lhs.vertexBuffer, rhs.vertexBuffer);
		if (lhs.vertexOffset != rhs.vertexOffset)
			return lhs.vertexOffset < rhs.vertexOffset;
		if (lhs.indexBuffer != rhs.indexBuffer)
			return std::less<VkBuffer>{}(lhs.indexBuffer, rhs.indexBuffer);
		return std::tie(lhs.indexOffset, lhs.indexType, lhs.indexCount, lhs.firstIndex, lhs.vertexBase)
			< std::tie(rhs.indexOffset, rhs.indexType, rhs.indexCount, rhs.firstIndex, rhs.vertexBase);
	}

	bool RequiresGlobalMaterialIndex(VansGraphics::VansMaterialType materialType)
	{
		using namespace VansGraphics;
		switch (materialType)
		{
		case VAN_PBR:
		case VAN_COAT:
		case VAN_SKIN:
		case VAN_CLOTH:
		case VAN_SUBSURFACE:
		case VAN_EMISSIVE:
		case VAN_PBR_EMISSIVE:
		case VAN_DECAL:
		case VAN_CUSTOM_SHADER:
		case VAN_PBR_TRANSMISSION:
			return true;
		default:
			return false;
		}
	}

	bool StateLess(
		const VansGraphics::VansDrawPacket& lhs,
		const VansGraphics::VansDrawPacket& rhs,
		bool compareDepth)
	{
		if (lhs.orderGroup != rhs.orderGroup)
			return lhs.orderGroup < rhs.orderGroup;
		if (lhs.pipelineHash != rhs.pipelineHash)
			return lhs.pipelineHash < rhs.pipelineHash;
		if (lhs.pipeline != rhs.pipeline)
			return std::less<VkPipeline>{}(lhs.pipeline, rhs.pipeline);
		if (lhs.pipelineLayout != rhs.pipelineLayout)
			return std::less<VkPipelineLayout>{}(lhs.pipelineLayout, rhs.pipelineLayout);
		if (lhs.descriptorHash != rhs.descriptorHash)
			return lhs.descriptorHash < rhs.descriptorHash;
		if (lhs.descriptorSets != rhs.descriptorSets)
			return DescriptorSetsLess(lhs.descriptorSets, rhs.descriptorSets);
		if (!(lhs.geometry == rhs.geometry))
			return GeometryLess(lhs.geometry, rhs.geometry);
		if (compareDepth && lhs.cameraDepth != rhs.cameraDepth)
			return lhs.cameraDepth < rhs.cameraDepth;
		return lhs.stableOrder < rhs.stableOrder;
	}
}

bool VansGraphics::VansGeometryKey::operator==(const VansGeometryKey& rhs) const
{
	return vertexBuffer == rhs.vertexBuffer
		&& vertexOffset == rhs.vertexOffset
		&& indexBuffer == rhs.indexBuffer
		&& indexOffset == rhs.indexOffset
		&& indexType == rhs.indexType
		&& indexCount == rhs.indexCount
		&& firstIndex == rhs.firstIndex
		&& vertexBase == rhs.vertexBase;
}

void VansGraphics::VansDrawSubmissionList::Clear()
{
	packets.clear();
	batches.clear();
	instanceData.clear();
	stats = {};
}

bool VansGraphics::VansDrawSubmission::BuildPacket(
	VkDevice& device,
	VansRenderNode& node,
	VansGraphicsShader& shader,
	const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
	const std::vector<VkDescriptorSet>& descriptorSets,
	GlobalStateData globalState,
	std::int32_t passUser0,
	std::uint64_t orderGroup,
	std::uint64_t stableOrder,
	float cameraDepth,
	VansDrawPacket& packet)
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);
	if (node.m_Mesh == nullptr || node.m_Material == nullptr || node.m_TransfromIndex < 0)
		return false;
	if (shader.GetPushConstantSize() != 0)
	{
		VANS_LOG_ERROR("[VansDrawSubmission] Shader '" << shader.m_AssetName
			<< "' declares per-draw push constants. Submission-managed geometry must use "
				"VansDrawSubmission.glsl and descriptor-backed pass data.");
		return false;
	}
	if (descriptorSetLayouts.empty() || descriptorSetLayouts.size() != descriptorSets.size())
		return false;
	for (std::size_t index = 0; index < descriptorSets.size(); ++index)
	{
		if (descriptorSetLayouts[index] == VK_NULL_HANDLE || descriptorSets[index] == VK_NULL_HANDLE)
			return false;
	}

	globalState.vertexInputAttributeDescriptions = &node.m_Mesh->m_VertexInputAttributeDescriptions;
	globalState.vertexInputBindingDescriptions = &node.m_Mesh->m_VertexInputBindingDescriptions;
	VansVKGraphicsPipeline* pipeline = shader.GetGraphicsPipeline(device, globalState, descriptorSetLayouts);
	if (pipeline == nullptr || pipeline->GetNativePipeline() == VK_NULL_HANDLE ||
		pipeline->GetNativePipelineLayout() == VK_NULL_HANDLE)
		return false;

	const int materialIndex = node.m_Material->GetGlobalMaterialIndex();
	if (materialIndex < 0 && RequiresGlobalMaterialIndex(node.m_Material->m_MaterialType))
	{
		VANS_LOG_ERROR("[VansDrawSubmission] Material '" << node.m_Material->m_AssetName
			<< "' has no global GPU material index.");
		return false;
	}

	const VertexBufferParameters vertex = node.m_Mesh->GetVertexBufferParameter();
	const IndexBufferParameters index = node.m_Mesh->GetIndexBufferParameter();

	packet = {};
	packet.pipeline = pipeline->GetNativePipeline();
	packet.pipelineLayout = pipeline->GetNativePipelineLayout();
	packet.descriptorSets = descriptorSets;
	packet.geometry.vertexBuffer = vertex.Buffer;
	packet.geometry.vertexOffset = vertex.MemoryOffset;
	packet.geometry.indexBuffer = index.Buffer;
	packet.geometry.indexOffset = index.MemoryOffset;
	packet.geometry.indexType = index.IndexType;
	packet.geometry.indexCount = node.m_Mesh->GetIndexCount();
	packet.instanceData.transformIndex = node.m_TransfromIndex;
	packet.instanceData.materialIndex = materialIndex;
	packet.instanceData.vertexFeatureMask = node.BuildVertexFeatureMask();
	packet.instanceData.passUser0 = passUser0;
	packet.pipelineHash = pipeline->GetDescriptorKey().hash;
	packet.descriptorHash = BuildDescriptorHash(descriptorSets);
	packet.orderGroup = orderGroup;
	packet.stableOrder = stableOrder;
	packet.cameraDepth = std::isfinite(cameraDepth) ? cameraDepth : 0.0f;
	return true;
}

bool VansGraphics::VansDrawSubmission::AreBatchCompatible(
	const VansDrawPacket& lhs,
	const VansDrawPacket& rhs)
{
	return lhs.orderGroup == rhs.orderGroup
		&& lhs.pipeline == rhs.pipeline
		&& lhs.pipelineLayout == rhs.pipelineLayout
		&& lhs.geometry == rhs.geometry
		&& lhs.descriptorSets == rhs.descriptorSets;
}

bool VansGraphics::VansDrawSubmission::Finalize(
	VansVKDrawInstanceArena& uploadArena,
	VansDrawSortPolicy sortPolicy,
	VansDrawSubmissionList& submission)
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);
	submission.batches.clear();
	submission.instanceData.clear();
	if (submission.packets.empty())
	{
		submission.stats = {};
		return true;
	}

	if (sortPolicy != VansDrawSortPolicy::PreserveOrder)
	{
		const bool compareDepth = sortPolicy == VansDrawSortPolicy::StateThenFrontToBack;
		std::sort(submission.packets.begin(), submission.packets.end(),
			[compareDepth](const VansDrawPacket& lhs, const VansDrawPacket& rhs)
			{
				return StateLess(lhs, rhs, compareDepth);
			});
	}

	submission.instanceData.reserve(submission.packets.size());
	submission.batches.reserve(submission.packets.size());
	const VansDrawPacket* previous = nullptr;
	for (const VansDrawPacket& packet : submission.packets)
	{
		if (previous == nullptr || !AreBatchCompatible(*previous, packet))
		{
			VansDrawBatch batch;
			batch.pipeline = packet.pipeline;
			batch.pipelineLayout = packet.pipelineLayout;
			batch.descriptorSets = packet.descriptorSets;
			batch.geometry = packet.geometry;
			batch.firstInstance = static_cast<std::uint32_t>(submission.instanceData.size());
			submission.batches.push_back(batch);
		}
		submission.instanceData.push_back(packet.instanceData);
		++submission.batches.back().instanceCount;
		previous = &packet;
	}

	const std::uint32_t gpuBase = uploadArena.Upload(submission.instanceData);
	if (gpuBase == VansVKDrawInstanceArena::InvalidRecordOffset)
	{
		submission.batches.clear();
		return false;
	}
	for (VansDrawBatch& batch : submission.batches)
		batch.firstInstance += gpuBase;

	submission.stats.packetCount = static_cast<std::uint32_t>(submission.packets.size());
	submission.stats.batchCount = static_cast<std::uint32_t>(submission.batches.size());
	submission.stats.instanceCount = static_cast<std::uint32_t>(submission.instanceData.size());
	submission.stats.instancedBatchCount = static_cast<std::uint32_t>(std::count_if(
		submission.batches.begin(), submission.batches.end(),
		[](const VansDrawBatch& batch) { return batch.instanceCount > 1; }));
	return true;
}

void VansGraphics::VansDrawSubmission::Record(
	VansVKCommandBuffer& commandBuffer,
	const VansDrawSubmissionList& submission,
	std::size_t batchBegin,
	std::size_t batchEnd)
{
#ifdef _DEBUG
	assert(g_CurrentFramePhase == VansFramePhase::GPURecord ||
		g_CurrentFramePhase == VansFramePhase::ParallelGPURecord);
#endif
	const std::size_t clampedEnd = (std::min)(batchEnd, submission.batches.size());
	for (std::size_t batchIndex = batchBegin; batchIndex < clampedEnd; ++batchIndex)
	{
		const VansDrawBatch& batch = submission.batches[batchIndex];
		if (batch.pipeline == VK_NULL_HANDLE || batch.pipelineLayout == VK_NULL_HANDLE ||
			batch.descriptorSets.empty() ||
			batch.geometry.vertexBuffer == VK_NULL_HANDLE ||
			batch.geometry.indexBuffer == VK_NULL_HANDLE)
			continue;
		commandBuffer.BindVertexBuffers(
			0, 1, &batch.geometry.vertexBuffer, &batch.geometry.vertexOffset);
		commandBuffer.BindIndexBuffer(
			batch.geometry.indexBuffer,
			batch.geometry.indexOffset,
			batch.geometry.indexType);
		commandBuffer.BindGraphicsPipeline(batch.pipeline, batch.pipelineLayout);
		commandBuffer.BindDescriptorSets(
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			batch.pipelineLayout,
			0,
			batch.descriptorSets,
			{});
		commandBuffer.DrawIndexed(
			batch.geometry.indexCount,
			batch.instanceCount,
			batch.geometry.firstIndex,
			batch.geometry.vertexBase,
			batch.firstInstance);
	}
}
