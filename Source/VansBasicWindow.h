#pragma once
#include "GLFW/glfw3.h"
namespace VansGraphics
{
	struct WindowStatus
	{
		bool	swapChainRebuild : 1;
	};

	//基础窗口
	class VansBasicWindow
	{
	public:
		VansBasicWindow() : m_VansGraphicsHandle(nullptr) {}

	public:
		GLFWwindow* m_VansGraphicsHandle;

	public:
		WindowStatus m_WindowStatus;
	};
}
