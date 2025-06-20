#include "VansEditorWindow.h"

#include "../RenderCore/VulkanCore/VansVKDevice.h"
#include "../RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"
#include "../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../RenderCore/VansCamera.h"
#include "../VansTimer.h"
#include "Windows/VansHierachyWindow.h"
#include "Windows/VansLightWindow.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <iostream>


static void glfw_error_callback(int error, const char* description)
{
    std::cout << "GLFW Error " << error << ":" << description << std::endl;
}

static bool CheckGraphicsAPI(VansGraphics::GRAPHICS_API api)
{
    switch (api)
    {
    case VansGraphics::VULKAN:
        if (!glfwVulkanSupported())
        {
            std::cout << "GLFW: Vulkan Not Supported" << std::endl;
            return false;
        }
        return true;
        break;
    case VansGraphics::INVALIDE:
    default:
        return false;
        break;
    }
}

VansGraphics::VansBasicWindow VansGraphics::VansEditorWindow::m_VansEditorWindow;
//支持多个相机
std::vector<VansGraphics::VansCamera*> VansGraphics::VansEditorWindow::m_Cameras;

//支持多个窗口
std::vector<VansGraphics::VansBaseWindowComponent*> VansGraphics::VansEditorWindow::m_Windows;

//脚本上下文
VansScriptContext VansGraphics::VansEditorWindow::m_ScriptContext;

bool VansGraphics::VansEditorWindow::CreateVansEditorWindow(int width, int height ,GRAPHICS_API api)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        return false;
    }
        
    // Create window with Vulkan context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_VansEditorWindow.m_VansGraphicsHandle = glfwCreateWindow(width, height, "ForestEngine", nullptr, nullptr);

    if (!CheckGraphicsAPI(api))
    { 
        m_VansEditorWindow.m_VansGraphicsHandle = nullptr;
        return false;
    }

    //创建功能窗口
    CreateWindowComponents();

    return true;
}


void VansGraphics::VansEditorWindow::CreateWindowComponents()
{
    VansHierachuWindow* hierachyWindow = new VansHierachuWindow();
    m_Windows.push_back(hierachyWindow);

    VansLightWindow* lightWindow = new VansLightWindow();
    m_Windows.push_back(lightWindow);
}

void VansGraphics::VansEditorWindow::KeyBoardInputCallBack(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT) 
    {
        //将消息传递给相机
        for (auto camera : m_Cameras)
        {
			camera->HandleKeyboardInput(key, scancode, action, mods, VansGraphics::VansTimer::GetDeltaTime());
		}
    }
}


void VansGraphics::VansEditorWindow::MouseInputCallBack(GLFWwindow* window, double xpos, double ypos)
{
    static bool isInit = false;
    static double lastX = 0;
    static double lastY = 0;
    if (!isInit)
    {
        lastX = xpos;
        lastY = ypos;
        isInit = true;
    }


    double deltaX = xpos - lastX;
    double deltaY = ypos - lastY;

    lastX = xpos;
    lastY = ypos;
    //将消息传递给相机
    for (auto camera : m_Cameras)
    {
        camera->HandleMouseMovement(deltaX, deltaY);
    }
}

void VansGraphics::VansEditorWindow::DrawEditorWindows(VansVKDevice* device)
{
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();

    //绘制所有窗口
    for (VansBaseWindowComponent* window : m_Windows)
    {
		window->ShowWindow(*device);
	}

    //GUI handle rendeing
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *static_cast<VkCommandBuffer*>(device->GetNativeCommandBuffer()));

}

void VansGraphics::VansEditorWindow::StartEditorLoop(VansGraphics::VansCamera& camera)
{
    m_Cameras.clear();
    m_Cameras.push_back(&camera);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windowss
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    //初始化GUI的graphics back end
    m_GUIBackEnd->InitBackEnd(*m_GraphicsDevice, m_VansEditorWindow.m_VansGraphicsHandle);


    //注册回调
    glfwSetKeyCallback(m_VansEditorWindow.m_VansGraphicsHandle, VansGraphics::VansEditorWindow::KeyBoardInputCallBack);
    glfwSetCursorPosCallback(m_VansEditorWindow.m_VansGraphicsHandle, VansGraphics::VansEditorWindow::MouseInputCallBack);

    //初始化脚本环境
    m_ScriptContext.VansScriptSetup();

    // Main loop
    while (!glfwWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle))
    {
        glfwPollEvents();

        // Resize swap chain?
        if (m_VansEditorWindow.m_WindowStatus.swapChainRebuild)
        {
            int width, height;
            glfwGetFramebufferSize(m_VansEditorWindow.m_VansGraphicsHandle, &width, &height);
            if (width > 0 && height > 0)
            {
                //ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
                //ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
                m_VansEditorWindow.m_WindowStatus.swapChainRebuild = false;
            }
        }

        //更新时间
        VansGraphics::VansTimer::Update();

        // Rendering
        camera.Rendering();

        m_ScriptContext.VansScriptUpdate();
        //UI Pass
        DrawEditorWindows(static_cast<VansVKDevice*>(m_GraphicsDevice));


        camera.Present();

        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized)
        {

        }


        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    m_Cameras.clear();

}

void VansGraphics::VansEditorWindow::DestroyVansEditorWindow()
{
    ImGui::DestroyContext();

    glfwDestroyWindow(m_VansEditorWindow.m_VansGraphicsHandle);
    glfwTerminate();
}
