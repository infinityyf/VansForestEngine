#include "VansRuntimeWindow.h"

#include "../Util/VansLog.h"

namespace Vans
{
	VansRuntimeWindow::~VansRuntimeWindow()
	{
		Destroy();
	}

	bool VansRuntimeWindow::Create(int width, int height, const char* title, std::string& error)
	{
		error.clear();
		if (m_Window)
			return true;

		glfwSetErrorCallback(ErrorCallback);
		if (!glfwInit())
		{
			error = "glfwInit failed";
			return false;
		}
		m_InitializedGLFW = true;

		if (!glfwVulkanSupported())
		{
			error = "GLFW Vulkan is not supported";
			Destroy();
			return false;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_Window = glfwCreateWindow(width, height, title ? title : "ForestGame", nullptr, nullptr);
		if (!m_Window)
		{
			error = "glfwCreateWindow failed";
			Destroy();
			return false;
		}

		glfwSetWindowUserPointer(m_Window, this);
		glfwSetFramebufferSizeCallback(m_Window, FramebufferResizeCallback);
		return true;
	}

	void VansRuntimeWindow::Destroy()
	{
		if (m_Window)
		{
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
		}
		if (m_InitializedGLFW)
		{
			glfwTerminate();
			m_InitializedGLFW = false;
		}
	}

	void VansRuntimeWindow::PollEvents()
	{
		if (m_Window)
			glfwPollEvents();
	}

	bool VansRuntimeWindow::ShouldClose() const
	{
		return m_Window && glfwWindowShouldClose(m_Window);
	}

	void VansRuntimeWindow::RequestClose()
	{
		if (m_Window)
			glfwSetWindowShouldClose(m_Window, true);
	}

	bool VansRuntimeWindow::ConsumeFramebufferResize()
	{
		const bool value = m_ResizeRequested;
		m_ResizeRequested = false;
		return value;
	}

	void VansRuntimeWindow::ErrorCallback(int error, const char* description)
	{
		VANS_LOG_ERROR("[RuntimeWindow] GLFW error " << error << ": " << (description ? description : ""));
	}

	void VansRuntimeWindow::FramebufferResizeCallback(GLFWwindow* window, int, int)
	{
		if (auto* runtimeWindow = static_cast<VansRuntimeWindow*>(glfwGetWindowUserPointer(window)))
			runtimeWindow->m_ResizeRequested = true;
	}
}
