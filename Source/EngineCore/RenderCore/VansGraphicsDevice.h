#pragma once
#include "../ScriptCore/VansCommonUtils.h"
#include "VansRenderFrame.h"
#include "VansRenderThreadTransaction.h"

#include <memory>

struct ImDrawData;

namespace VansGraphics
{
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
		//初始化渲染资源
		virtual void BeforeRendering() = 0;
		virtual void PrepareRenderingFrame() {}
		virtual VansRenderSubmissionPrepareResult PrepareRenderSubmission(
			const VansRenderFrameSubmission&)
		{
			return { VansRenderSubmissionPrepareStatus::Ready, {} };
		}

		virtual void Rendering() = 0;
		// True only while the current frame owns a valid presentation image and
		// accepts GPU command recording. CPU-side UI construction may continue
		// when false, but no commands may be appended to the frame.
		virtual bool CanRecordCurrentFrame() const { return true; }

		virtual void Present() = 0;

		//释放渲染资源
		virtual void AfterRendering() = 0;

		virtual bool WaitForIdle() { return true; }

		virtual void OnWindowResize(uint32_t width, uint32_t height) {}

		virtual void BeginUIRenderPass() {}

		virtual void EndUIRenderPass() {}

		virtual void InitializeGpuProfiler() {}

		virtual void EndGpuProfilerFrame() {}

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
		virtual void ShutdownBackEnd() = 0;
	};

}

extern VansGraphics::VansGraphicsDevice* m_GraphicsDevice;
extern VansGraphics::VansGUIBackEnd* m_GUIBackEnd;
