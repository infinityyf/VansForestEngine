#pragma once

#include "../Interfaces/INativeWindowProvider.h"

#include "GLFW/glfw3.h"

#include <string>

namespace Vans
{
	class VansRuntimeWindow final : public VansGraphics::INativeWindowProvider
	{
	public:
		~VansRuntimeWindow();

		bool Create(int width, int height, const char* title, std::string& error);
		void Destroy();
		void PollEvents();
		bool ShouldClose() const;
		void RequestClose();
		bool ConsumeFramebufferResize();
		GLFWwindow* GetGLFWWindow() const { return m_Window; }
		void* GetNativeWindowHandle() const override { return m_Window; }

	private:
		static void ErrorCallback(int error, const char* description);
		static void FramebufferResizeCallback(GLFWwindow* window, int, int);

		GLFWwindow* m_Window = nullptr;
		bool m_ResizeRequested = false;
		bool m_InitializedGLFW = false;
	};
}
