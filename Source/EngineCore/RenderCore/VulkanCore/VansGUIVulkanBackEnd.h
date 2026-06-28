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
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorPool m_ImGUIPool = VK_NULL_HANDLE;
		bool m_Initialized = false;
	public:
		~VansGraphicsGUIBackEnd() override;
		void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) override;
		void ShutdownBackEnd() override;
	};
	
}
