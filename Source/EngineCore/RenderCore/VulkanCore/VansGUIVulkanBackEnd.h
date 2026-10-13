#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#include "VansVKDevice.h"
#include "../VansGraphicsDevice.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
	struct VansGUITextureResource
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	class VansGraphicsGUIBackEnd : public VansGUIBackEnd
	{
	private:
		class RendererInitializationTransaction;
		class RendererShutdownTransaction;

		bool InitializeRendererOnRenderThread(VansGraphicsDevice& backend);
		bool ShutdownRendererOnRenderThread();

		VansVKDevice* m_VkDevice = nullptr;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorPool m_ImGUIPool = VK_NULL_HANDLE;
		bool m_PlatformInitialized = false;
		bool m_RendererInitialized = false;
		std::mutex m_FrameTextureMutex;
		std::unordered_map<void*, void*> m_FrameTextureVariants;
		std::unordered_map<void*, VansGUITextureResource> m_TextureResources;
		std::unordered_map<void*, std::shared_ptr<IVansRenderFrameTextureBinding>> m_FrameTextureBindings;
		std::unordered_set<void*> m_FrameTextureBindingsPendingRemoval;
	public:
		~VansGraphicsGUIBackEnd() override;
		void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) override;
		std::unique_ptr<IVansRenderThreadTransaction>
			CreateRenderThreadInitialization() override;
		std::unique_ptr<IVansRenderThreadTransaction>
			CreateRenderThreadShutdown() override;
		void BeginFrame() override;
		std::unique_ptr<IVansRenderFrameOverlay> CaptureDrawData(ImDrawData* drawData) override;
		void RegisterTextureResource(void* texture, void* image, int layout) override;
		void UnregisterTextureResource(void* texture) override;
		bool RegisterFrameTextureBinding(void* texture, std::shared_ptr<IVansRenderFrameTextureBinding> binding) override;
		void UnregisterFrameTextureBinding(void* texture) override;
		bool RegisterFrameTextureVariant(void* primary, void* alternate) override;
		void UnregisterFrameTextureVariant(void* primary) override;
		void ShutdownBackEnd() override;
	};
	
}
