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
#include "../ShadowCore/VansPunctualShadowTypes.h"

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
	struct VansRenderPassRuntimeInfo
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkViewport viewport = {};
		VkRect2D scissor = {};
		uint32_t subpass = 0;
	};

	class VansRenderPassManager
	{
		friend class VansVKDevice;
	private:
		VansVKImage m_ColorImage;

		// Previous frame's diffuse exitant radiance.  This deliberately excludes
		// specular, fog and all later composition so SSGI never feeds the final
		// scene color back into the indirect-light estimate.
		VansVKImage m_DiffuseExitantRadianceHistoryImage;

		// Deferred / forward-opaque scene color snapshot used by transmission
		// glass during the later transparent pass.
		VansVKImage m_OpaqueSceneColorImage;

		VansVKImage m_DepthImage;

		VansVKImage m_MotionVectorImage;

		// Dedicated depth for the motion-vector pass so the previous-frame
		// m_DepthImage is preserved for compute passes (HZB / GI / SSR).
		VansVKImage m_MotionVectorDepthImage;

		// Display-resolution result after post-processing the unified HDR upscaler output.
		VansVKImage m_FinalDisplayColorImage;
		VansVKImage* m_DisplayPostProcessInput = nullptr;

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
		VkImageView m_PunctualShadowLayerViews[VANS_PUNCTUAL_SHADOW_ATLAS_COUNT] = {};


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

		VansVKRenderPass m_VansTransparentPass;
		VansVKRenderPass m_VansDisplayPostProcessPass;

		VansVKRenderPass m_VansShadowPass;

		VansVKRenderPass m_VansPunctualShadowPass;

		VansVKRenderPass m_VansMotionVectorPass;

		VansVKRenderPass m_VansUIPass;

		// Scene UI pass composites Noesis onto FinalDisplayColor.
		// 最终布局为 SHADER_READ_ONLY_OPTIMAL，供 ImGui 场景窗口采样
		VansVKRenderPass m_VansSceneUIPass;

		// 贴花 pass：只写 Normal / GBuffer0 / GBuffer1（LOAD 现有内容，alpha blend 叠写）
		VansVKRenderPass m_VansDecalPass;

		// ── 水面 GBuffer pass ─────────────────────────────────────────────
		// 设计文档 Pass 7：在 Deferred 之后、Transparent 之前执行
		// 输出：normal/roughness, scatter/thickness, absorption/foam, world position/depth.
		// 复用主场景深度（只读）：opaque custom 先写入，水面覆盖率随后接受硬件深度测试。
		VansVKRenderPass m_VansWaterGBufferPass;
		VansVKImage m_WaterGBufNormalImage;
		VansVKImage m_WaterGBufScatterImage;
		VansVKImage m_WaterGBufAbsorptionImage;
		VansVKImage m_WaterGBufLinearDepthImage;

		// Deferred + SkyBox pass, separate from the transparent-only pass.
		VansVKRenderPass m_VansDeferredSkyboxPass;

		// Screen-space raw feature pass. Currently runs SSAO at half resolution
		// and writes only storage images, so it has no framebuffer attachments.
		VansVKRenderPass m_VansScreenSpaceEffectsPass;

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

		void SetupVansSceneUIRenderPass(
			VkDevice& logicDevice,
			VkImageView finalDisplayImageView,
			const VkExtent2D& displayExtent);
		void DestroySceneUIRenderPass();
		void SetupVansDisplayPostProcessPass(
			VkDevice& logic_device,
			VansVKImage& hdrInput,
			const VkExtent2D& displayExtent);
		void DestroyDisplayPostProcessPass();

		// 贴花 pass：引用现有 GBuffer 图像（Normal/GBuffer0/GBuffer1），LOAD 内容并 alpha blend 叠写
		void SetupVansDecalRenderPass(VkDevice& logic_device, const VkExtent2D& renderResolution);

		// ── 水面 GBuffer pass ──────────────────────────────────────────────
		// 须在 SetupVansDeferredRenderPass 之后调用（依赖已创建的 m_DepthImage）
		void SetupVansWaterGBufferPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansScreenSpaceEffectsPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansHairVisibilityPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansHairLightingPass(VkDevice& logic_device, const VkExtent2D& renderResolution);
		void SetupVansHairDeepOpacityPass(VkDevice& logic_device, const VkExtent2D& renderResolution);

		// 水面 GBuffer 纹理访问器（供 VansWaterSystem / 描述符写入使用）
		VansVKImage& GetWaterGBufNormal()      { return m_WaterGBufNormalImage; }
		VansVKImage& GetWaterGBufScatter()     { return m_WaterGBufScatterImage; }
		VansVKImage& GetWaterGBufAbsorption()  { return m_WaterGBufAbsorptionImage; }
		VansVKImage& GetWaterGBufLinearDepth() { return m_WaterGBufLinearDepthImage; }

		// Deferred + SkyBox pass 访问器
		VansVKRenderPass& GetVansDeferredSkyboxPass() { return m_VansDeferredSkyboxPass; }
		VansVKRenderPass& GetVansScreenSpaceEffectsPass() { return m_VansScreenSpaceEffectsPass; }

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
		VansRenderPassRuntimeInfo GetRenderPassRuntimeInfo(VansVKRenderPass& renderPass, int swap_chain_index = 0, uint32_t subpass = 0);

		void BeginRenderPass(VansVKRenderPass& renderPass, VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data, int swap_chain_index = 0);
		void BeginRenderPass(VansVKRenderPass& renderPass, VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data, int swap_chain_index, VkSubpassContents contents);

		void EndRenderPass(VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data);

		void DestroyRenderPass();
		void DestroySceneResolutionRenderPasses();

		void RecordFrameBufferImageLayoutReset(VansVKCommandBuffer& command_buffer);

		VansVKRenderPass& GetVansGBufferPass() { return m_VansGBufferPass; }
		VansVKRenderPass& GetVansUIRenderPass() { return m_VansUIPass; }

		VansVKImage& GetShadowMap() { return m_CascadeShadowMapImage; }

		VkImageView GetCascadeShadowArrayView() { return m_CascadeShadowArrayView; }

		VkSampler GetCascadeShadowSampler() { return m_CascadeShadowSampler; }

		VkImageView GetCascadeShadowLayerView(int layer) { return m_CascadeColorLayerViews[layer]; }

		VansVKImage& GetPunctualShadowMap() { return m_PunctualShadowMapImage; }

		VansVKImage& GetColor() { return m_ColorImage; }

		VansVKImage& GetDiffuseExitantRadianceHistory() { return m_DiffuseExitantRadianceHistoryImage; }

		VansVKImage& GetOpaqueSceneColor() { return m_OpaqueSceneColorImage; }

		VansVKImage& GetDepth() { return m_DepthImage; }

		VansVKImage& GetMotionVector() { return m_MotionVectorImage; }

		VansVKImage& GetFinalDisplayColor() { return m_FinalDisplayColorImage; }
		VansVKImage* GetDisplayPostProcessInput() const { return m_DisplayPostProcessInput; }

		VansVKImage& GetNormal() { return m_NormalImage; }

		VansVKImage& GetGbuffer0() { return m_GBufferImage0; }

		VansVKImage& GetGbuffer1() { return m_GBufferImage1; }

		VansVKImage& GetGbuffer2() { return m_GBufferImage2; }

		VansVKRenderPass& GetVansDecalPass() { return m_VansDecalPass; }

	};
}
