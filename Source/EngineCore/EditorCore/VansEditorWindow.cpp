#include "VansEditorWindow.h"
#include "../RenderCore/VulkanCore/VansVKDevice.h"
#include "../RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"
#include "../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansScene.h"
#include "../RenderCore/VulkanCore/VansRenderPass.h"
#include "../VansTimer.h"
#include "../PhysicsCore/VansPhysics.h"
#include "Windows/VansHierachyWindow.h"
#include "Windows/VansLightWindow.h"
#include "Windows/VansProjectWindow.h"
#include "Windows/VansSceneWindow.h"
#include "Windows/VansInspectorWindow.h"
#include "Windows/VansGBufferWindow.h"

#include "../Util/VansJobSystem.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <iostream>
#include <string>


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

bool VansGraphics::VansEditorWindow::m_GBufferWindowOpen = false;

bool VansGraphics::VansEditorWindow::m_WireframeMode = false;

VansGraphics::VansBasicWindow VansGraphics::VansEditorWindow::m_VansEditorWindow;
//支持多个相机
std::vector<VansGraphics::VansCamera*> VansGraphics::VansEditorWindow::m_Cameras;

//支持多个窗口
std::vector<VansGraphics::VansBaseWindowComponent*> VansGraphics::VansEditorWindow::m_Windows;

VansGraphics::VansHierachuWindow* VansGraphics::VansEditorWindow::m_HierachyWindow;

VansGraphics::VansLightWindow* VansGraphics::VansEditorWindow::m_LightWindow;

VansGraphics::VansProjectWindow* VansGraphics::VansEditorWindow::m_ProjectWindow;

VansGraphics::VansSceneWindow* VansGraphics::VansEditorWindow::m_SceneWindow;

VansGraphics::VansInspectorWindow* VansGraphics::VansEditorWindow::m_InspectorWindow;

VansGraphics::VansGBufferWindow* VansGraphics::VansEditorWindow::m_GBufferWindow;

//脚本上下文
VansScriptContext VansGraphics::VansEditorWindow::m_ScriptContext;

bool VansGraphics::VansEditorWindow::CreateVansEditorWindow(int width, int height, GRAPHICS_API api)
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

    // Register Physics Pre-Step Callback for Vehicle
    VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback([](float dt) {
        if (m_Scene && m_Scene->m_Vehicle)
        {
            // This runs on the physics thread!
            // Thread safety note: ensure m_Scene->m_Vehicle is not deleted while this runs.
            // Since shutdown stops physics first, this should be safe.
            m_Scene->m_Vehicle->Step(dt);
        }
    });

    //创建功能窗口
    CreateWindowComponents();

    return true;
}


void VansGraphics::VansEditorWindow::CreateWindowComponents()
{
    m_HierachyWindow = new VansHierachuWindow();
    m_Windows.push_back(m_HierachyWindow);

    m_LightWindow = new VansLightWindow();
    m_Windows.push_back(m_LightWindow);

    m_ProjectWindow = new VansProjectWindow();
    m_Windows.push_back(m_ProjectWindow);

    m_SceneWindow = new VansSceneWindow();
    m_Windows.push_back(m_SceneWindow);

    m_InspectorWindow = new VansInspectorWindow();
    m_Windows.push_back(m_InspectorWindow);

    m_GBufferWindow = new VansGBufferWindow();
    m_Windows.push_back(m_GBufferWindow);
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

void VansGraphics::VansEditorWindow::MouseClickCallBack(GLFWwindow* window, int button, int action, int mods)
{
    bool isDown = (action == GLFW_PRESS);
    //将消息传递给相机
    for (auto camera : m_Cameras)
    {
        camera->SetRightMouseDown(false);
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            camera->SetRightMouseDown(isDown);
        }
    }
}

void VansGraphics::VansEditorWindow::DrawEditorWindows(VansVKDevice* device)
{
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // -------------------------------------------------------------------------
    // 2. 设置主 DockSpace (类似 Unity 的根布局容器)
    // -------------------------------------------------------------------------
    {
        static bool opt_fullscreen = true;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // 设置主窗口标志：无标题栏、无调整大小、无移动、不可停靠（作为容器）
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        // 开始主容器窗口
        ImGui::Begin("ForestEngine Editor", nullptr, window_flags);
        ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        // 提交 DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        // 顶部菜单栏
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("GbufferWindow"))
                {
                    m_GBufferWindowOpen = !m_GBufferWindowOpen;
                }
                ImGui::EndMenu();
            }
            // 新增 View 菜单用于控制线框模式
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Wireframe", nullptr, &m_WireframeMode))
                {
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        //绘制所有窗口
        for (VansBaseWindowComponent* window : m_Windows)
        {
            window->ShowWindow(*device);
        }

        ImGui::End();
    }



    //GUI handle rendeing
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();

    device->BeginUIRenderPass();
    ImGui_ImplVulkan_RenderDrawData(draw_data, *static_cast<VkCommandBuffer*>(device->GetNativeCommandBuffer()));
    device->EndUIRenderPass();
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

        Vans::VansJobSystem::Get().ProcessMainThreadJobs();

        //更新时间
        VansGraphics::VansTimer::Update();

        // Step Vehicle Physics - MOVED TO PHYSICS THREAD via Callback
        if (m_Scene && m_Scene->m_Vehicle)
        {
            // Basic vehicle control inputs (hardcoded for test)
            // Inputs are lightweight and can be set from main thread (atomic/simple types)
            if (glfwGetKey(m_VansEditorWindow.m_VansGraphicsHandle, GLFW_KEY_W) == GLFW_PRESS)
                m_Scene->m_Vehicle->SetInputs(1.0f, 0.0f, 0.0f, 0.0f);
            else if (glfwGetKey(m_VansEditorWindow.m_VansGraphicsHandle, GLFW_KEY_S) == GLFW_PRESS)
                m_Scene->m_Vehicle->SetInputs(0.0f, 0.5f, 0.0f, 0.0f); // Brake
            else
                m_Scene->m_Vehicle->SetInputs(0.0f, 0.0f, 0.0f, 0.0f); // Coast (no brake, no throttle)
        }

        // Synchronize physics transforms to render transforms
        // IMPORTANT: This uses PxSceneReadLock internally to prevent race conditions
        // with the background physics simulation thread
        VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();
        if (physics.IsSimulationRunning() && m_Scene)
        {
            m_Scene->UpdatePhysicsTransforms();
        }

        // Rendering, 这里会结束renderpass
        camera.Rendering();

        //m_ScriptContext.VansScriptUpdate();
        //UI Pass
        m_SceneWindow->RegistCamera(&camera);
        DrawEditorWindows(static_cast<VansVKDevice*>(m_GraphicsDevice));

        //结束录制
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
    // Unregister Physics Callback on shutdown to avoid calling into destroyed objects
    VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);
    VansEngine::VansPhysicsSystem::GetInstance().StopSimulation(); // Ensure thread stops

    ImGui::DestroyContext();

    glfwDestroyWindow(m_VansEditorWindow.m_VansGraphicsHandle);
    glfwTerminate();
}
