#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <vector>

#include "VansVKImage.h"
#include "VansPipeline.h"

namespace VansVulkan
{
	//render pass由subpass构成，subpass使用renderpass的部分attachemtn
	struct SubpassParameters 
	{
		VkPipelineBindPoint PipelineType;
		std::vector<VkAttachmentReference> InputAttachments;
		std::vector<VkAttachmentReference> ColorAttachments;
		std::vector<VkAttachmentReference> ResolveAttachments;
		VkAttachmentReference const* DepthStencilAttachment;
		std::vector<uint32_t> PreserveAttachments;
	};

	class VansFrameBuffer
	{
		friend class VansRenderPassManager;
	public:
		void CreateFrameBuffer(VkDevice& logic_device, VkRenderPass& render_pass, const std::vector<VkImageView>& image_views, VkExtent3D framebuffer_size);

		void DestroyFrameBuffer(VkDevice& logic_device);

	private:
		VkFramebuffer m_FrameBuffer;
	};

	class VansVKRenderPass
	{
		friend class VansRenderPassManager;
	public:

		void CreateRenderPass(VkDevice& logic_device, std::vector<VkAttachmentDescription>& attachments, std::vector<SubpassParameters>& subpass_params, std::vector<VkSubpassDependency>& subpass_dependency);

		void DestroyRenderPass(VkDevice& logic_device);

		VkRenderPass GetRenderPass() { return m_RenderPass; }
	private:
		//attachments
		//render pass 使用的resources 叫做attacments
		std::vector<VkAttachmentDescription> m_AttachmentDescs;

		std::vector<VkSubpassDescription> m_SubpassDescs;

		//类似memory barrier
		//但是由于access,layout已经在subpass desc中设置，只需要这里设置依赖
		std::vector<VkSubpassDependency> m_SubpassDependencies;

		//这里只是记录一些格式上的信息，并不不包含运行时的数据，而framebuffer则是记录运行市的resources
	private:

		VkRenderPass m_RenderPass;
	};

	class VansVKCommandBuffer;
	class VansVKSurface;
	class VansRenderPassManager
	{
		friend class VansVKDevice;
	private:
		VansVKImage m_ColorImage;

		VansVKImage m_DepthImage;

		VansVKImage m_NormalImage;

		VansVKImage m_GBufferImage0; // albedo + roughness

		VansVKImage m_GBufferImage1; // metalic + materialID

	private:
		static VansRenderPassManager* instance;

		VansRenderPassManager();

		VansVKRenderPass m_VansRenderPass;

		std::vector<VansFrameBuffer> m_FrameBuffers;

		std::vector<VkClearValue> m_ClearValues;

		std::vector<VkClearValue> m_DeferredClearValues;

		VkDevice m_LogicDevice;

	public:
		static VansRenderPassManager* GetInstance();

		//framebuffer大小
		void SetupVansRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surrface);

		//延迟渲染
		void SetupVansDeferredRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surrface);

		//渲染区域大小
		void BeginRenderPass(VkCommandBuffer command_buffer, const VkRect2D& render_area, GlobalStateData& global_state_data, int swap_chain_index);

		void NextSubPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data);

		void EndRenderPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data);

		void DestroyRenderPass();

		void ResetFrameBufferImageLayout(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex);

		VansVKRenderPass& GetVansRenderPass() { return m_VansRenderPass; }

		VansVKImage& GetColor() { return m_ColorImage; }

		VansVKImage& GetDepth() { return m_DepthImage; }
	};
}