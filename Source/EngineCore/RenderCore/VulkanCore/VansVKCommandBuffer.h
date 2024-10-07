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

namespace VansVulkan
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

		VkCommandBuffer& GetVKCommandBuffer() { return m_VansVKCommandBuffer; }

		VansVKCommandBuffer(){ }

	public :
		void ClearColor(VansVKImage& image, const VkClearColorValue& value);

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

		void ExecuteSecondaryCommandBuffer(std::vector<VkCommandBuffer>& secondary_command_buffers);

		void BindDescriptorSets(VkPipelineBindPoint pipeline_type,
			VansGraphicsShader& shader,
			int index_for_first_set,
			const std::vector<VkDescriptorSet>& descriptor_sets,
			const std::vector<uint32_t>& dynamic_offsets);


		static bool SubmitCommands(VkQueue& queue, VkDevice& device, const std::vector<VkCommandBuffer>& command_buffers, const std::vector<VansVulkan::WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores);

		//同步fence
		static VkFence m_CommandBufferFinishSubmitFence;

	private:
		VkCommandPool m_VansVKCommandPool;

		VkCommandBuffer m_VansVKCommandBuffer;

		VkDevice m_VansVKDevice;

		void BindGraphicsPipeline(VansVKGraphicsPipeline& graphicsPipeline);

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