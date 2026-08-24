#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#include "VansVKDevice.h"
#include "../VansGraphicsDevice.h"

namespace VansGraphics
{
	class VansGraphicsGUIBackEnd : public VansGUIBackEnd
	{
	private:
		class RendererInitializationTransaction;
		class RendererShutdownTransaction;

		bool InitializeRendererOnRenderThread(VansGraphicsDevice& backend);
		bool ShutdownRendererOnRenderThread(VansGraphicsDevice& backend);

		VansVKDevice* m_VkDevice = nullptr;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorPool m_ImGUIPool = VK_NULL_HANDLE;
		bool m_PlatformInitialized = false;
		bool m_RendererInitialized = false;
	public:
		~VansGraphicsGUIBackEnd() override;
		void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) override;
		std::unique_ptr<IVansRenderThreadTransaction>
			CreateRenderThreadInitialization() override;
		std::unique_ptr<IVansRenderThreadTransaction>
			CreateRenderThreadShutdown() override;
		void BeginFrame() override;
		std::unique_ptr<IVansRenderFrameOverlay> CaptureDrawData(ImDrawData* drawData) override;
		void ShutdownBackEnd() override;
	};
	
}
