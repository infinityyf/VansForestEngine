#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <vector>

#include "VansVKImage.h"
#include "VansVKBuffer.h"
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

		VkFramebuffer GetFrameBuffer() const { return m_FrameBuffer; }

	private:
		VkFramebuffer m_FrameBuffer = VK_NULL_HANDLE;
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

		VkRenderPass m_RenderPass = VK_NULL_HANDLE;
	};

	class VansVKCommandBuffer;
	class VansVKSurface;
	class VansRenderPassManager
	{
		friend class VansVKDevice;
	private:
		VansVKImage m_ColorImage;

		// Deferred / forward-opaque scene color snapshot used by transmission
		// glass during the later transparent pass.
		VansVKImage m_OpaqueSceneColorImage;

		VansVKImage m_DepthImage;

		VansVKImage m_MotionVectorImage;

		// Dedicated depth for the motion-vector pass so the previous-frame
		// m_DepthImage is preserved for compute passes (HZB / GI / SSR).
		VansVKImage m_MotionVectorDepthImage;

		//后处理之后需要一张新的图，用于给FSR处理
		VansVKImage m_ColorAfterPostProcessImage;

		VansVKImage m_ShadowMapImage;

		VansVKImage m_ShadowMapDepthImage;

		// Cascade shadow map (four configuration-sized array layers).
		VansVKImage m_CascadeShadowMapImage;       // R32_SFLOAT linear depth for PCSS
		VansVKImage m_CascadeShadowMapDepthImage;  // D32_SFLOAT visibility/depth test
		VkImageView m_CascadeColorLayerViews[4];   // per-layer views for framebuffers
		VkImageView m_CascadeDepthLayerViews[4];   // per-layer views for framebuffers
		VkImageView m_CascadeShadowArrayView;      // 2D_ARRAY view for sampling
		VkSampler   m_CascadeShadowSampler;        // reuse sampler for cascade array

		VansVKImage m_PunctualShadowMapImage;

		VansVKImage m_PunctualShadowMapDepthImage;


		VansVKImage m_NormalImage;

		VansVKImage m_GBufferImage0; // albedo + roughness

		VansVKImage m_GBufferImage1; // metalic + ao + materialID

		VansVKImage m_GBufferImage2; // worldposition + linear depth

		VansVKImage m_HairVis0Image;
		VansVKImage m_HairVis1Image;
		VansVKImage m_HairVis2Image;
		VansVKImage m_HairVis3Image;
		VansVKImage m_HairDepthImage;
		VansVKImage m_HairCoverageImage;
		VansVKImage m_HairColorImage;
		VansVKImage m_HairDeepOpacityImage;
		VansVKImage m_HairOITHeadImage;
		VansVKBuffer m_HairOITNodeBuffer;
		VansVKBuffer m_HairOITCounterBuffer;
		uint32_t m_HairOITMaxNodes = 0;

	private:
		static VansRenderPassManager* instance;

		VansRenderPassManager();

		VansVKRenderPass m_VansGBufferPass;

		VansVKRenderPass m_VansRenderPass;

		VansVKRenderPass m_VansShadowPass;

		VansVKRenderPass m_VansPunctualShadowPass;

		VansVKRenderPass m_VansMotionVectorPass;

		VansVKRenderPass m_VansUIPass;

		// 场景 UI pass：Noesis 运行时 UI 合成到 FSR 输出图像上
		// 最终布局为 SHADER_READ_ONLY_OPTIMAL，供 ImGui 场景窗口采样
		VansVKRenderPass m_VansSceneUIPass;

		// 贴花 pass：只写 Normal / GBuffer0 / GBuffer1（LOAD 现有内容，alpha blend 叠写）
		VansVKRenderPass m_VansDecalPass;

		// ── 水面 GBuffer pass ─────────────────────────────────────────────
		// 设计文档 Pass 7：在 Deferred 之后、Transparent 之前执行
		// 输出：WaterGBuf_Normal（RGBA16F）+ WaterGBuf_WorldPosDepth（RGBA16F）
		// RGB=世界空间法线/位置, A=预留/线性深度
		// 复用主场景深度（只读）：opaque custom 先写入，水面覆盖率随后接受硬件深度测试。
		VansVKRenderPass m_VansWaterGBufferPass;
		VansVKImage m_WaterGBufNormalImage;       // 世界空间法线 XYZ + 预留 A（RGBA16F）
		VansVKImage m_WaterGBufLinearDepthImage;  // 世界空间位置 RGB + 线性深度 A（RGBA16F）

		// ── Deferred + SkyBox 专用 pass（从 m_VansRenderPass 中拆出）──────
		// m_VansRenderPass 拆分后仅保留 Transparent + PostProcess；
		// 此 pass 执行 ScreenSpaceFeature + DeferredLighting + SkyBox
		VansVKRenderPass m_VansDeferredSkyboxPass;

		// Forward opaque pass after deferred lighting and before transparent.
		VansVKRenderPass m_VansForwardOpaqueAfterDeferredPass;

		VansVKRenderPass m_VansHairVisibilityPass;
		VansVKRenderPass m_VansHairLightingPass;
		VansVKRenderPass m_VansHairDeepOpacityPass;

		VkDevice m_LogicDevice;

	public:
		static VansRenderPassManager* GetInstance();

		////framebuffer大小
		//void SetupVansRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surrface);

		//延迟渲染资源初始化：拆分为 GBuffer pass + Deferred/PostProcess pass
		void SetupVansDeferredRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, const VkExtent2D& renderResolution);

		//阴影渲染
		void SetupVansShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue);

		//精确阴影渲染
		void SetupVansPunctualShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue);

		// Motion vector pass — writes per-pixel screen-space velocity to m_MotionVectorImage.
		// Uses a dedicated depth attachment (m_MotionVectorDepthImage) so the previous-frame
		// m_DepthImage is preserved for HZB / GI / SSR compute passes.
		void SetupVansMotionVectorRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, const VkExtent2D& renderResolution);

		//uipass（ImGui 编辑器面板 → swapchain）
		void SetupVansUIRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution);

		// scene ui pass（Noesis 运行时 UI → FSR 输出图像，格式 R16G16B16A16_SFLOAT）
		// fsrImageView：FSR 输出图像的 ImageView；displayExtent：显示分辨率
		void SetupVansSceneUIRenderPass(VkDevice& logic_device, VkImageView fsrImageView, const VkExtent2D& displayExtent);
		void DestroySceneUIRenderPass();

		// 贴花 pass：引用现有 GBuffer 图像（Normal/GBuffer0/GBuffer1），LOAD 内容并 alpha blend 叠写
		void SetupVansDecalRenderPass(VkDevice& logic_device, const VkExtent2D& renderResolution);

		// ── 水面 GBuffer pass ──────────────────────────────────────────────
		// 须在 SetupVansDeferredRenderPass 之后调用（依赖已创建的 m_DepthImage）
		void SetupVansWaterGBufferPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansHairVisibilityPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansHairLightingPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansHairDeepOpacityPass(VkDevice& logic_device, const VkExtent2D& renderResolution);

		// 水面 GBuffer 纹理访问器（供 VansWaterSystem / 描述符写入使用）
		VansVKImage& GetWaterGBufNormal()      { return m_WaterGBufNormalImage; }
		VansVKImage& GetWaterGBufLinearDepth() { return m_WaterGBufLinearDepthImage; }

		// Deferred + SkyBox pass 访问器
		VansVKRenderPass& GetVansDeferredSkyboxPass() { return m_VansDeferredSkyboxPass; }

		VansVKRenderPass& GetVansForwardOpaqueAfterDeferredPass() { return m_VansForwardOpaqueAfterDeferredPass; }
		VansVKRenderPass& GetVansHairVisibilityPass() { return m_VansHairVisibilityPass; }
		VansVKRenderPass& GetVansHairLightingPass() { return m_VansHairLightingPass; }
		VansVKRenderPass& GetVansHairDeepOpacityPass() { return m_VansHairDeepOpacityPass; }
		VansVKImage& GetHairVis0() { return m_HairVis0Image; }
		VansVKImage& GetHairVis1() { return m_HairVis1Image; }
		VansVKImage& GetHairVis2() { return m_HairVis2Image; }
		VansVKImage& GetHairVis3() { return m_HairVis3Image; }
		VansVKImage& GetHairDepth() { return m_HairDepthImage; }
		VansVKImage& GetHairCoverage() { return m_HairCoverageImage; }
		VansVKImage& GetHairColor() { return m_HairColorImage; }
		VansVKImage& GetHairDeepOpacity() { return m_HairDeepOpacityImage; }
		VansVKImage& GetHairOITHead() { return m_HairOITHeadImage; }
		VansVKBuffer& GetHairOITNodeBuffer() { return m_HairOITNodeBuffer; }
		VansVKBuffer& GetHairOITCounterBuffer() { return m_HairOITCounterBuffer; }
		uint32_t GetHairOITMaxNodes() const { return m_HairOITMaxNodes; }

		// Water GBuffer pass 访问器
		VansVKRenderPass& GetVansWaterGBufferPass() { return m_VansWaterGBufferPass; }

		// 销毁UI pass（用于窗口resize）
		void DestroyUIRenderPass();

		// 重建 UI pass（resize后调用）
		void RecreateUIRenderPass(VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution);

		//渲染区域大小
		void BeginRenderPass(VansVKRenderPass& renderPass, VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data, int swap_chain_index = 0);

		void NextSubPass(VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data);

		void EndRenderPass(VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data);

		void BlitToSwapChainImage(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex, const VkExtent2D& renderResolution);

		void DestroyRenderPass();

		void ResetFrameBufferImageLayout(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex);

		VansVKRenderPass& GetVansRenderPass() { return m_VansRenderPass; }

		VansVKRenderPass& GetVansGBufferPass() { return m_VansGBufferPass; }

		VansVKImage& GetShadowMap() { return m_CascadeShadowMapImage; }

		VkImageView GetCascadeShadowArrayView() { return m_CascadeShadowArrayView; }

		VkSampler GetCascadeShadowSampler() { return m_CascadeShadowSampler; }

		VkImageView GetCascadeShadowLayerView(int layer) { return m_CascadeColorLayerViews[layer]; }

		VansVKImage& GetPunctualShadowMap() { return m_PunctualShadowMapImage; }

		VansVKImage& GetColor() { return m_ColorImage; }

		VansVKImage& GetOpaqueSceneColor() { return m_OpaqueSceneColorImage; }

		VansVKImage& GetDepth() { return m_DepthImage; }

		VansVKImage& GetMotionVector() { return m_MotionVectorImage; }

		VansVKImage& GetColorAfterPostProcess() { return m_ColorAfterPostProcessImage; }

		VansVKImage& GetNormal() { return m_NormalImage; }

		VansVKImage& GetGbuffer0() { return m_GBufferImage0; }

		VansVKImage& GetGbuffer1() { return m_GBufferImage1; }

		VansVKImage& GetGbuffer2() { return m_GBufferImage2; }

		VansVKRenderPass& GetVansDecalPass() { return m_VansDecalPass; }

	};
}
