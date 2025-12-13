#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <vector>

#include "VansVKImage.h"
#include "VansPipeline.h"

namespace VansGraphics
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

		void CreateRenderPass(VkDevice& logic_device, std::vector<VkAttachmentDescription>& attachments, std::vector<SubpassParameters>& subpass_params, std::vector<VkSubpassDependency>& subpass_dependency, const VkExtent2D& resolution);

		void DestroyRenderPass(VkDevice& logic_device);

		VkRenderPass GetRenderPass() { return m_RenderPass; }

	private:

		VkViewport m_RenderPassViewport;

		VkRect2D m_RenderPassScissor;

	private:

		std::vector<VansFrameBuffer> m_FrameBuffers;

		std::vector<VkClearValue> m_ClearValues;

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

		VansVKImage m_MotionVectorImage;

		//后处理之后需要一张新的图，用于给FSR处理
		VansVKImage m_ColorAfterPostProcessImage;

		VansVKImage m_ShadowMapImage;

		VansVKImage m_ShadowMapDepthImage;

		VansVKImage m_PunctualShadowMapImage;

		VansVKImage m_PunctualShadowMapDepthImage;


		VansVKImage m_NormalImage;

		VansVKImage m_GBufferImage0; // albedo + roughness

		VansVKImage m_GBufferImage1; // metalic + ao + materialID

		VansVKImage m_GBufferImage2; // worldposition + linear depth

	private:
		static VansRenderPassManager* instance;

		VansRenderPassManager();

		VansVKRenderPass m_VansRenderPass;

		VansVKRenderPass m_VansShadowPass;

		VansVKRenderPass m_VansPunctualShadowPass;

		VansVKRenderPass m_VansUIPass;

		VkDevice m_LogicDevice;

	public:
		static VansRenderPassManager* GetInstance();

		////framebuffer大小
		//void SetupVansRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surrface);

		//延迟渲染
		void SetupVansDeferredRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, const VkExtent2D& renderResolution);

		//阴影渲染
		void SetupVansShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue);

		//精确阴影渲染
		void SetupVansPunctualShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue);

		//uipass
		void SetupVansUIRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution);

		//渲染区域大小
		void BeginRenderPass(VansVKRenderPass& renderPass, VkCommandBuffer command_buffer, GlobalStateData& global_state_data, int swap_chain_index = 0);

		void NextSubPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data);

		void EndRenderPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data);

		void BlitToSwapChainImage(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex, const VkExtent2D& renderResolution);

		void DestroyRenderPass();

		void ResetFrameBufferImageLayout(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex);

		VansVKRenderPass& GetVansRenderPass() { return m_VansRenderPass; }

		VansVKImage& GetShadowMap() { return m_ShadowMapImage; }

		VansVKImage& GetPunctualShadowMap() { return m_PunctualShadowMapImage; }

		VansVKImage& GetColor() { return m_ColorImage; }

		VansVKImage& GetDepth() { return m_DepthImage; }

		VansVKImage& GetMotionVector() { return m_MotionVectorImage; }

		VansVKImage& GetColorAfterPostProcess() { return m_ColorAfterPostProcessImage; }

		VansVKImage& GetNormal() { return m_NormalImage; }

		VansVKImage& GetGbuffer0() { return m_GBufferImage0; }

		VansVKImage& GetGbuffer1() { return m_GBufferImage1; }

		VansVKImage& GetGbuffer2() { return m_GBufferImage2; }
	};
}