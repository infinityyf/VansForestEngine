#pragma once
#include "../ScriptCore/VansCommonUtils.h"
#include "VansRenderFrame.h"
#include "VansRenderThreadTransaction.h"

#include <functional>
#include <memory>
#include <cstdint>

struct ImDrawData;

namespace VansGraphics
{
	struct VansGpuMemorySnapshot
	{
		bool available = false;
		std::uint64_t allocationCount = 0;
		std::uint64_t blockCount = 0;
		std::uint64_t allocationBytes = 0;
		std::uint64_t blockBytes = 0;
		std::uint64_t deviceLocalAllocationBytes = 0;
		std::uint64_t deviceLocalBlockBytes = 0;
	};

	enum GRAPHICS_API
	{
		INVALIDE = 0,
		VULKAN = 1,
	};

	class VansGraphicsDevice
	{
	public:
		GRAPHICS_API m_GraphicsAPI;

	protected:

		uint32_t m_RenderWidth;
		uint32_t m_RenderHeight;

	public :
		virtual ~VansGraphicsDevice() = default;

		//初始化渲染资源
		virtual bool BeforeRendering() = 0;
		virtual void PrepareRenderingFrame() {}
		virtual VansRenderSubmissionPrepareResult PrepareRenderSubmission(
			VansRenderFrameSubmission&)
		{
			return { VansRenderSubmissionPrepareStatus::Ready, {} };
		}

		virtual void Rendering() = 0;
		// True only while the current frame owns a valid presentation image and
		// accepts GPU command recording. CPU-side UI construction may continue
		// when false, but no commands may be appended to the frame.
		virtual bool CanRecordCurrentFrame() const { return true; }
		// 当前帧因表面过期而无法呈现时，由帧终态请求宿主走现有 resize 事务。
		virtual bool CurrentFrameRequiresSurfaceResize() const = 0;
		// Device loss is terminal for the current backend; the render outcome
		// must not treat it as a recoverable frame or surface resize.
		virtual bool HasDeviceLost() const = 0;

		virtual void Present() = 0;

		//释放渲染资源
		virtual void AfterRendering() = 0;

		virtual bool WaitForIdle() { return true; }
		// Called on RenderThread after the frame using this resource has been
		// submitted. The backend retires it only after that frame completes.
		virtual void RetireAfterCurrentFrame(std::function<void()> destroy) = 0;

		virtual bool OnWindowResize(uint32_t width, uint32_t height) = 0;

		virtual void BeginUIRenderPass() {}

		virtual void EndUIRenderPass() {}

		virtual void InitializeGpuProfiler() {}
		// 低频诊断：由 RenderThread 在有序事务中读取 backend 管理的内存。
		virtual VansGpuMemorySnapshot QueryGpuMemorySnapshot() const { return {}; }

		//获取底层device指针
		virtual void* GetNativeGraphicsDevice() = 0;

		virtual void* GetNativeCommandBuffer() = 0;

		float GetAspectRatio();

		float GetNativeRenderWidth() { return static_cast<float>(m_RenderWidth); }

		float GetNativeRenderHeight() { return static_cast<float>(m_RenderHeight); }

		// Active temporal-upscale backend jitter in pixel space. Off returns false
		// and the camera must use zero jitter rather than a hidden fallback sequence.
		virtual bool GetTemporalUpscaleJitterOffset(
			uint32_t frameIndex,
			float& outPixelX,
			float& outPixelY) { return false; }

		// Material texture LOD bias selected by the active temporal-upscale mode.
		virtual float GetTemporalUpscaleMipBias() const { return 0.0f; }
	};

	class IVansRenderFrameTextureBinding
	{
	public:
		virtual ~IVansRenderFrameTextureBinding() = default;
		// 返回该帧可用的 opaque texture；无资源时返回 null，消费者跳过绘制。
		virtual void* Resolve(VansGraphicsDevice& backend) = 0;
		virtual void Finish(VansGraphicsDevice& backend) {}
	};
	class VansGUIBackEnd
	{
	public:
		virtual ~VansGUIBackEnd() = default;
		// Main owns the window-system frontend; native renderer resources are
		// created and destroyed by the transactions below on RenderThread.
		virtual void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) = 0;
		virtual std::unique_ptr<IVansRenderThreadTransaction>
			CreateRenderThreadInitialization() = 0;
		virtual std::unique_ptr<IVansRenderThreadTransaction>
			CreateRenderThreadShutdown() = 0;
		virtual void BeginFrame() = 0;
		virtual std::unique_ptr<IVansRenderFrameOverlay> CaptureDrawData(ImDrawData* drawData) = 0;
		// The preview owner supplies the image behind an ImGui descriptor. The
		// captured overlay uses this identity to declare its actual GPU reads.
		virtual void RegisterTextureResource(void* texture, void* image, int layout) = 0;
		virtual void UnregisterTextureResource(void* texture) = 0;
		virtual bool RegisterFrameTextureBinding(void* texture, std::shared_ptr<IVansRenderFrameTextureBinding> binding) = 0;
		virtual void UnregisterFrameTextureBinding(void* texture) = 0;
		// The dedicated preview's second physical target is selected by its frame overlay.
		virtual bool RegisterFrameTextureVariant(void* primary, void* alternate) = 0;
		virtual void UnregisterFrameTextureVariant(void* primary) = 0;
		virtual void ShutdownBackEnd() = 0;
	};

}

extern VansGraphics::VansGraphicsDevice* m_GraphicsDevice;
extern VansGraphics::VansGUIBackEnd* m_GUIBackEnd;
