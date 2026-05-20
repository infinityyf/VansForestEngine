#pragma once
#include <vector>
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include "VansPipeline.h"
#include <functional>
#include <thread>

namespace VansGraphics
{
	struct CommandBufferCreateParams
	{
		VkCommandPoolCreateFlags pool_params;
		VkCommandBufferLevel commandbuffer_level;
		uint32_t commandbuffer_count;
	};

	struct WaitSemaphoreInfo 
	{
		VkSemaphore semaphore;
		VkPipelineStageFlags waiting_stage;
	};

	//用于多线程record commandbuffer
	struct CommandBufferRecordingThreadParameters 
	{
		VkCommandBuffer CommandBuffer;
		std::function<bool(VkCommandBuffer)> RecordingFunction;
	};

	struct BufferTransition;
	struct ImageTransition;
	class VansVKBuffer;
	class VansVKImage;
	class VansMesh;
	class VansVKDevice;
	class VansGraphicsShader;
	class VansComputeShader;
	class VansVKGraphicsPipeline;



	class VansVKCommandBuffer
	{
		friend class VansVKMemoryManager;

	public:
		bool CreateVulkanCommandBuffer(VansVKDevice& device, uint32_t queue_family, CommandBufferCreateParams& buffer_create_info);
	
		void DestroyVulkanCommandBuffer(VkDevice& logical_device);

		bool BeginCommandBufferRecord(VkCommandBufferUsageFlagBits commandBufferUsage);

		bool EndCommandBufferRecord();

		bool ResetCommandBuffer(bool release_buffer_memory);

		bool ResetEvent(VkEvent eventHandle);

		void SetEvent(VkEvent eventHandle, VkPipelineStageFlags stageMask);

		void WaitEvents(
			const std::vector<VkEvent>& events,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			const std::vector<VkMemoryBarrier>& memoryBarriers = {},
			const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers = {},
			const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers = {});

		VkCommandBuffer& GetVKCommandBuffer() { return m_VansVKCommandBuffer; }

		VansVKCommandBuffer()
			: m_CommandBufferFinishSubmitFence(VK_NULL_HANDLE)
			, m_VansVKCommandPool(VK_NULL_HANDLE)
			, m_VansVKCommandBuffer(VK_NULL_HANDLE)
			, m_VansVKDevice(VK_NULL_HANDLE)
		{ }

	public :
		void ClearColor(VansVKImage& image, const VkClearColorValue& value);

		void ClearMRTColor(const std::vector<VansVKImage>& images, const std::vector<VkClearColorValue>& values);

		void ClearDepthStencil(VansVKImage& image, const VkClearDepthStencilValue& value);

		//用于subpass中对attachment进行clear,只能应用于renderpass中
		void ClearAttachment(std::vector<VkClearAttachment>& attachments, std::vector<VkClearRect>& rests);


		void BindMesh(VansMesh& mesh, uint32_t fist_bind, GlobalStateData& global_state_data);

		//确保pipeLine已经创建
		void EnsureGraphicsShader(VansGraphicsShader& shader, GlobalStateData& global_state_data,const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);

		void EnsureComputeShader(VansComputeShader& shader, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);

		void UpdatePushConstants(VansVKGraphicsPipeline& pipeline, VkShaderStageFlags flags, uint32_t offset, uint32_t size, void* data);

		void SetViewport(uint32_t first_viewport,const std::vector<VkViewport>& viewports);

		//设置在viewport中的裁剪区域
		//The viewport defines a part of an attachment (image) to which the clip's space will be
		//mapped.The scissor test allows us to additionally confine a drawing to the specified
		//rectangle within the specified viewport dimensions
		void SetScissor(uint32_t first_scissor, const std::vector<VkRect2D>& scissors);

		void SetLineWidth(float line_width);

		//Depth bias modifies the calculated depth value--the value used during the depth test and
		//stored in a depth
		void SetDepthBias(float constant_factor, float clamp, float slope_factor);

		//如果pipeline是dynamic的从外部设置
		void SetBlendConstants(float blend_constants[4]);

		//绘制
		void DrawMesh(VansMesh& mesh, VansGraphicsShader& shader, uint32_t instance_count);

		//dispatch
		void DispatchCompute(VansComputeShader& shader, uint32_t x_size, uint32_t y_size, uint32_t z_size, const std::vector<VkDescriptorSet>& descriptor_sets);

		//blit
		void BlitImage(VansVKImage& source, int source_mip, VansVKImage& target, int target_mip);

		// GPU 图像区域拷贝/缩放，供数组层或指定 mip 更新使用。
		void CopyImageRegions(VansVKImage& source, VkImageLayout sourceLayout,
			VansVKImage& target, VkImageLayout targetLayout,
			const std::vector<VkImageCopy>& copyRegions);

		void BlitImageRegions(VansVKImage& source, VkImageLayout sourceLayout,
			VansVKImage& target, VkImageLayout targetLayout,
			const std::vector<VkImageBlit>& blitRegions,
			VkFilter filter = VK_FILTER_LINEAR);

		void ExecuteSecondaryCommandBuffer(std::vector<VkCommandBuffer>& secondary_command_buffers);

		void BuildAccelerationStructures(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, const VkAccelerationStructureBuildRangeInfoKHR* rangeInfo);
		
		void BindGraphicsPipeline(VansVKGraphicsPipeline& graphicsPipeline);

		void BindDescriptorSets(VkPipelineBindPoint pipeline_type,
			VansGraphicsShader& shader,
			int index_for_first_set,
			const std::vector<VkDescriptorSet>& descriptor_sets,
			const std::vector<uint32_t>& dynamic_offsets);

		// ── Standalone draw / bind helpers (for non-mesh draw like fullscreen passes) ──
		void BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* buffers, const VkDeviceSize* offsets);

		void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);

		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);

		void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

		// ── Indirect draw ──
		void DrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);

		// ── Buffer copy (GPU-side) ──
		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size);

		// ── Pipeline barrier ──
		void PipelineBarrier(
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			const std::vector<VkMemoryBarrier>& memoryBarriers = {},
			const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers = {},
			const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers = {});

		static bool WaitForFence(VkDevice& device, const VkFence& fence);
		static bool SubmitCommands(VkQueue& queue, VkDevice& device, const std::vector<VkCommandBuffer>& command_buffers, const std::vector<VansGraphics::WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores, const VkFence& fence, bool wait_fence = true);

		//同步fence
		//应该每个commandbuffer对象对应一个
		VkFence m_CommandBufferFinishSubmitFence;
	private:
		VkCommandPool m_VansVKCommandPool;

		VkCommandBuffer m_VansVKCommandBuffer;

		VkDevice m_VansVKDevice;


	};

	class VansMultiThreadCommandBufferMangaer
	{
	private:
		std::vector<CommandBufferRecordingThreadParameters> m_CommandBufferRecordingThreadParameters;

		std::vector<std::thread> m_CommandBufferRecordingThreads;
		//vk中不要在多线中中对同一个object进行修改，例如
		//1. allocate command buffers from a single pool
		//2. update a descriptor set

	public:

		void InitCommandRecordThreads(std::vector<VansVKCommandBuffer>& vans_command_buffers);

		void SubmitMultiCommands(VkQueue& queue, VkDevice& device, const std::vector<WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores, VkFence& fence);

	};
}