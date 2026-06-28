#pragma once
#include "GLFW/glfw3.h"
#include "EngineCore/Interfaces/INativeWindowProvider.h"
namespace VansGraphics
{
	struct WindowStatus
	{
		bool	swapChainRebuild : 1;
	};

	//基础窗口
	class VansBasicWindow : public INativeWindowProvider
	{
	public:
		VansBasicWindow() : m_VansGraphicsHandle(nullptr)
		{
			m_WindowStatus.swapChainRebuild = false;
		}

		void* GetNativeWindowHandle() const override
		{
			return m_VansGraphicsHandle;
		}

	public:
		GLFWwindow* m_VansGraphicsHandle;

	public:
		WindowStatus m_WindowStatus;
	};
}
