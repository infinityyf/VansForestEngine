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
		VkDescriptorPool m_ImGUIPool;
	public:
		void InitBackEnd(VansGraphicsDevice& device, GLFWwindow* window) override;
	};
	
}
