#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VansGraphics
{
	class VansGraphicsShader;
	class VansRenderNode;
	class VansVKCommandBuffer;
	class VansVKDrawInstanceArena;
	struct GlobalStateData;

	// CPU/GPU 共用的逐实例提交记录。字段与 Common/VansDrawSubmission.glsl 保持一致。
	struct alignas(16) VansDrawInstanceDataGPU
	{
		std::int32_t transformIndex = -1;
		std::int32_t materialIndex = -1;
		std::uint32_t vertexFeatureMask = 0;
		std::int32_t passUser0 = 0;
	};

	static_assert(sizeof(VansDrawInstanceDataGPU) == 16,
		"VansDrawInstanceDataGPU must match the std430 GLSL record stride");

	struct VansGeometryKey
	{
		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkDeviceSize vertexOffset = 0;
		VkBuffer indexBuffer = VK_NULL_HANDLE;
		VkDeviceSize indexOffset = 0;
		VkIndexType indexType = VK_INDEX_TYPE_UINT32;
		std::uint32_t indexCount = 0;
		std::uint32_t firstIndex = 0;
		std::int32_t vertexBase = 0;

		bool operator==(const VansGeometryKey& rhs) const;
	};

	enum class VansDrawSortPolicy
	{
		State,
		StateThenFrontToBack,
		PreserveOrder,
	};

	struct VansDrawPacket
	{
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> descriptorSets;
		VansGeometryKey geometry;
		VansDrawInstanceDataGPU instanceData;
		std::uint64_t pipelineHash = 0;
		std::uint64_t descriptorHash = 0;
		std::uint64_t orderGroup = 0;
		std::uint64_t stableOrder = 0;
		float cameraDepth = 0.0f;
	};

	struct VansDrawBatch
	{
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> descriptorSets;
		VansGeometryKey geometry;
		std::uint32_t firstInstance = 0;
		std::uint32_t instanceCount = 0;
	};

	struct VansDrawSubmissionStats
	{
		std::uint32_t packetCount = 0;
		std::uint32_t batchCount = 0;
		std::uint32_t instancedBatchCount = 0;
		std::uint32_t instanceCount = 0;
	};

	struct VansDrawSubmissionList
	{
		std::vector<VansDrawPacket> packets;
		std::vector<VansDrawBatch> batches;
		std::vector<VansDrawInstanceDataGPU> instanceData;
		VansDrawSubmissionStats stats;

		void Clear();
	};

	class VansDrawSubmission
	{
	public:
		static bool BuildPacket(
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
			VansDrawPacket& packet);

		static bool Finalize(
			VansVKDrawInstanceArena& uploadArena,
			VansDrawSortPolicy sortPolicy,
			VansDrawSubmissionList& submission);

		static void Record(
			VansVKCommandBuffer& commandBuffer,
			const VansDrawSubmissionList& submission,
			std::size_t batchBegin,
			std::size_t batchEnd);

		static bool AreBatchCompatible(const VansDrawPacket& lhs, const VansDrawPacket& rhs);
	};
}
