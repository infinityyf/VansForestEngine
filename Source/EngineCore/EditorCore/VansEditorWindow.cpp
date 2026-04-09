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
#include "Windows/VansRenderDebugWindow.h"
#include "Windows/VansScriptorWindow.h"
#include "Windows/VansConsoleWindow.h"
#include "Windows/VansProfilerWindow.h"

#include "../Util/VansProfiler.h"
#include "../Util/VansJobSystem.h"
#include "../Util/VansInputManager.h"
#include "../Util/VansLog.h"

#include "../ProjectSystem/VansProjectManager.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <iostream>
#include <string>
#include <filesystem>


static void glfw_error_callback(int error, const char* description)
{
    VANS_LOG_ERROR("GLFW Error " << error << ":" << description);
}

static bool CheckGraphicsAPI(VansGraphics::GRAPHICS_API api)
{
    switch (api)
    {
    case VansGraphics::VULKAN:
        if (!glfwVulkanSupported())
        {
            VANS_LOG_ERROR("GLFW: Vulkan Not Supported");
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

bool VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen = false;

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

VansGraphics::VansRenderDebugWindow* VansGraphics::VansEditorWindow::m_RenderDebugWindow;

VansGraphics::VansScriptorWindow* VansGraphics::VansEditorWindow::m_ScriptorWindow;

VansGraphics::VansConsoleWindow* VansGraphics::VansEditorWindow::m_ConsoleWindow;

VansGraphics::VansProfilerWindow* VansGraphics::VansEditorWindow::m_ProfilerWindow;

//脚本上下文
VansScriptContext VansGraphics::VansEditorWindow::m_ScriptContext;

// Project selector overlay
Vans::VansProjectSelector* VansGraphics::VansEditorWindow::m_ProjectSelector = nullptr;
bool VansGraphics::VansEditorWindow::m_ProjectLoaded = false;
std::string VansGraphics::VansEditorWindow::m_PendingScenePath;
std::string VansGraphics::VansEditorWindow::m_PendingResourcePath;

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

    // Initialize input manager — must be BEFORE ImGui GLFW init so ImGui can chain
    Vans::VansInputManager::Get().Initialize(m_VansEditorWindow.m_VansGraphicsHandle);

    // Register framebuffer resize callback — sets the rebuild flag for the main loop
    glfwSetFramebufferSizeCallback(m_VansEditorWindow.m_VansGraphicsHandle, [](GLFWwindow*, int, int) {
        m_VansEditorWindow.m_WindowStatus.swapChainRebuild = true;
    });

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
    // Create the project selector overlay (shown before a project is loaded)
    m_ProjectSelector = new Vans::VansProjectSelector();

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

    m_RenderDebugWindow = new VansRenderDebugWindow();
    m_Windows.push_back(m_RenderDebugWindow);

    m_ScriptorWindow = new VansScriptorWindow();
    m_Windows.push_back(m_ScriptorWindow);

    m_ConsoleWindow = new VansConsoleWindow();
    m_Windows.push_back(m_ConsoleWindow);

    m_ProfilerWindow = new VansProfilerWindow();
    m_Windows.push_back(m_ProfilerWindow);
}

void VansGraphics::VansEditorWindow::RegisterCameraInputListeners()
{
    Vans::VansInputManager& input = Vans::VansInputManager::Get();

    // Forward keyboard events to all cameras
    input.AddKeyListener("EditorCamera_Key", [](int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT)
        {
            for (auto camera : m_Cameras)
            {
                camera->HandleKeyboardInput(key, scancode, action, mods, VansGraphics::VansTimer::GetDeltaTime());
            }
        }
    });

    // Forward mouse move deltas to all cameras
    input.AddMouseMoveListener("EditorCamera_Move", [](double x, double y) {
        double dx, dy;
        Vans::VansInputManager::Get().GetMouseDelta(dx, dy);
        for (auto camera : m_Cameras)
        {
            camera->HandleMouseMovement(static_cast<float>(dx), static_cast<float>(dy));
        }
    });

    // Forward mouse button events to cameras (right-click for look)
    input.AddMouseClickListener("EditorCamera_Click", [](int button, int action, int mods) {
        bool isDown = (action == GLFW_PRESS);
        for (auto camera : m_Cameras)
        {
            camera->SetRightMouseDown(false);
            if (button == GLFW_MOUSE_BUTTON_RIGHT)
            {
                camera->SetRightMouseDown(isDown);
            }
        }
    });
}

void VansGraphics::VansEditorWindow::UnregisterCameraInputListeners()
{
    Vans::VansInputManager& input = Vans::VansInputManager::Get();
    input.RemoveKeyListener("EditorCamera_Key");
    input.RemoveMouseMoveListener("EditorCamera_Move");
    input.RemoveMouseClickListener("EditorCamera_Click");
}

void VansGraphics::VansEditorWindow::DrawEditorWindows(VansVKDevice* device)
{
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ── Project Selector Overlay ──────────────────────────────────────────
    // When no project is loaded yet, show the full-screen selector instead
    // of the normal editor windows.
    if (!m_ProjectLoaded)
    {
        auto result = m_ProjectSelector->Render();

        // Helper: after a project is successfully loaded, check for a default
        // scene in ForestProject.json and auto-load it for rendering.
        auto tryLoadDefaultScene = [&device]()
        {
            auto& mgr = Vans::VansProjectManager::Get();

            // Queue resource loading if resource.json is configured
            const std::string& resourceFile = mgr.GetConfig().resourceFile;
            if (!resourceFile.empty())
            {
                std::string absResourcePath = mgr.GetProjectRootPath() + resourceFile;
                if (std::filesystem::exists(absResourcePath))
                {
                    VANS_LOG("[Editor] Deferring resource load: " << absResourcePath);
                    m_PendingResourcePath = absResourcePath;
                }
                else
                {
                    VANS_LOG_WARN("[Editor] Resource file not found: " << absResourcePath);
                }
            }

            // Queue default scene loading
            const std::string& defaultScene = mgr.GetConfig().defaultScene;
            if (defaultScene.empty())
                return;

            // Resolve to absolute path:  projectRoot + defaultScene
            std::string absScenePath = mgr.GetProjectRootPath() + defaultScene;

            if (!std::filesystem::exists(absScenePath))
            {
                VANS_LOG_WARN("[Editor] Default scene not found on disk: " << absScenePath);
                return;
            }

            VANS_LOG("[Editor] Deferring default scene load: " << absScenePath);
            m_PendingScenePath = absScenePath;
        };

        switch (result)
        {
        case Vans::ProjectSelectorResult::OpenExisting:
        {
            const std::string& path = m_ProjectSelector->GetSelectedProjectPath();
            VANS_LOG("[Editor] Opening project: " << path);
            if (Vans::VansProjectManager::Get().OpenProject(path))
            {
                m_ProjectLoaded = true;
                VANS_LOG("[Editor] Project opened successfully");
                tryLoadDefaultScene();
            }
            else
            {
                VANS_LOG_ERROR("[Editor] Failed to open project at " << path);
            }
            break;
        }
        case Vans::ProjectSelectorResult::CreateNew:
        {
            const std::string& path = m_ProjectSelector->GetSelectedProjectPath();
            const std::string& name = m_ProjectSelector->GetNewProjectName();
            VANS_LOG("[Editor] Creating project '" << name << "' at " << path);
            if (Vans::VansProjectManager::Get().CreateProject(path, name))
            {
                m_ProjectLoaded = true;
                VANS_LOG("[Editor] Project created successfully");
                tryLoadDefaultScene();
            }
            else
            {
                VANS_LOG_ERROR("[Editor] Failed to create project!");
            }
            break;
        }
        case Vans::ProjectSelectorResult::Cancelled:
            glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
            break;
        default:
            break;
        }

        // Render the ImGui frame (project selector only)
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        device->BeginUIRenderPass();
        ImGui_ImplVulkan_RenderDrawData(draw_data, *static_cast<VkCommandBuffer*>(device->GetNativeCommandBuffer()));
        device->EndUIRenderPass();
        return;
    }

    // ── Normal Editor Windows ─────────────────────────────────────────────
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
                if (ImGui::MenuItem("RenderDebugWindow"))
                {
                    m_RenderDebugWindowOpen = !m_RenderDebugWindowOpen;
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

void VansGraphics::VansEditorWindow::SetupImGuiStyle()
{
    ImGuiIO& io = ImGui::GetIO();

    // --- Font: Consolas Bold ---
    ImFont* consolasFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 15.0f);
    if (!consolasFont)
        io.Fonts->AddFontDefault();
    io.Fonts->Build();

    // --- Base theme ---
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // --- Shape / Layout ---
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;

    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 12.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowTitleAlign   = ImVec2(0.02f, 0.50f);
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

    // --- Colors (UE5 charcoal + slate blue accent) ---
    ImVec4* c = style.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]           = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.082f, 0.082f, 0.090f, 0.98f);
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.055f, 0.055f, 0.055f, 1.00f);

    // Borders
    c[ImGuiCol_Border]             = ImVec4(0.16f, 0.16f, 0.18f, 0.50f);
    c[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame (input boxes, sliders, checkboxes)
    c[ImGuiCol_FrameBg]            = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.10f, 0.28f, 0.50f, 0.80f);

    // Title bar
    c[ImGuiCol_TitleBg]            = ImVec4(0.047f, 0.047f, 0.047f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.059f, 0.059f, 0.059f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.047f, 0.047f, 0.047f, 0.75f);

    // Tabs
    c[ImGuiCol_Tab]                = ImVec4(0.067f, 0.067f, 0.075f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
    c[ImGuiCol_TabActive]          = ImVec4(0.12f, 0.28f, 0.48f, 1.00f);
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.055f, 0.055f, 0.060f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);

    // Buttons
    c[ImGuiCol_Button]             = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.15f, 0.33f, 0.55f, 1.00f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.11f, 0.27f, 0.48f, 1.00f);

    // Headers
    c[ImGuiCol_Header]             = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.12f, 0.28f, 0.48f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.42f, 0.42f, 0.44f, 1.00f);

    // Slider grab
    c[ImGuiCol_SliderGrab]         = ImVec4(0.22f, 0.46f, 0.73f, 1.00f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.28f, 0.52f, 0.80f, 1.00f);

    // Check mark
    c[ImGuiCol_CheckMark]          = ImVec4(0.28f, 0.56f, 0.88f, 1.00f);

    // Separator
    c[ImGuiCol_Separator]          = ImVec4(0.22f, 0.22f, 0.24f, 0.50f);
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.18f, 0.38f, 0.62f, 0.78f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.14f, 0.34f, 0.58f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.22f, 0.46f, 0.73f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.22f, 0.46f, 0.73f, 0.67f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.22f, 0.46f, 0.73f, 0.95f);

    // Docking
    c[ImGuiCol_DockingPreview]     = ImVec4(0.15f, 0.35f, 0.60f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]     = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);

    // Text
    c[ImGuiCol_Text]              = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.46f, 0.46f, 0.48f, 1.00f);
    c[ImGuiCol_TextSelectedBg]    = ImVec4(0.18f, 0.40f, 0.68f, 0.43f);

    // Nav / misc
    c[ImGuiCol_NavHighlight]      = ImVec4(0.22f, 0.46f, 0.73f, 1.00f);
    c[ImGuiCol_DragDropTarget]    = ImVec4(0.22f, 0.46f, 0.73f, 0.90f);
    c[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.00f, 0.00f, 0.00f, 0.58f);
    c[ImGuiCol_TableHeaderBg]     = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
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

    SetupImGuiStyle();

    ImVec4 clear_color = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

    //初始化GUI的graphics back end
    m_GUIBackEnd->InitBackEnd(*m_GraphicsDevice, m_VansEditorWindow.m_VansGraphicsHandle);

    // Initialize GPU profiler
#if VANS_PROFILER_ENABLED
    {
        auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
        Vans::VansGpuProfiler::Get().Init(
            vkDev->GetLogicDevice(),
            vkDev->GetPhysicalDevice(),
            vkDev->GetGraphicsQueueFamilyIndex());
    }
#endif

    //初始化脚本环境
    m_ScriptContext.VansScriptSetup();

    // Register camera input listeners through InputManager
    RegisterCameraInputListeners();

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
                auto* vkDevice = static_cast<VansVKDevice*>(m_GraphicsDevice);
                vkDevice->OnWindowResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

                // NOTE: internal render resolution is unchanged, so camera aspect ratio
                // and all SSGI/SSR/GBuffer render targets are unaffected.

                m_VansEditorWindow.m_WindowStatus.swapChainRebuild = false;
            }
            else
            {
                // Window minimized — skip rendering this frame
                continue;
            }
        }

        Vans::VansJobSystem::Get().ProcessMainThreadJobs();

        //更新输入管理器 (must be before any input queries this frame)
        Vans::VansInputManager& input = Vans::VansInputManager::Get();
        input.Update();

        //更新时间
        VansGraphics::VansTimer::Update();

        // Step Vehicle Physics - MOVED TO PHYSICS THREAD via Callback
        if (m_Scene && m_Scene->IsSceneReady() && m_Scene->m_Vehicle)
        {
            // Vehicle control inputs via InputManager
            if (input.IsKeyDown(GLFW_KEY_W))
                m_Scene->m_Vehicle->SetInputs(20.0f, 0.0f, 0.0f, 0.0f);
            else if (input.IsKeyDown(GLFW_KEY_S))
                m_Scene->m_Vehicle->SetInputs(0.0f, 100.0f, 0.0f, 0.0f); // Brake
            else if (input.IsKeyDown(GLFW_KEY_A))
                m_Scene->m_Vehicle->SetInputs(0.0f, 0.0f, -1.0f, 0.0f);
            else if (input.IsKeyDown(GLFW_KEY_D))
                m_Scene->m_Vehicle->SetInputs(0.0f, 0.0f, 1.0f, 0.0f);
            else
                m_Scene->m_Vehicle->SetInputs(0.0f, 0.0f, 0.0f, 0.0f); // Coast
        }

        // Synchronize physics transforms to render transforms
        // IMPORTANT: This uses PxSceneReadLock internally to prevent race conditions
        // with the background physics simulation thread
        VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();
        if (physics.IsSimulationRunning() && m_Scene && m_Scene->IsSceneReady())
        {
            m_Scene->UpdatePhysicsTransforms();
        }

        // ── Deferred resource & scene loading ───────────────────────────
        // Process pending loads BEFORE command buffer recording.

        // 1) Load project resources (mesh/texture/shader) from resource.json
        if (!m_PendingResourcePath.empty())
        {
            auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
            VANS_LOG("[Editor] Loading deferred resources: " << m_PendingResourcePath);
            m_Scene->LoadProjectResources(m_PendingResourcePath.c_str(), vkDev);
            m_PendingResourcePath.clear();
        }

        // 2) Load scene content (materials + nodes)
        if (!m_PendingScenePath.empty())
        {
            auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
            VANS_LOG("[Editor] Loading deferred scene: " << m_PendingScenePath);
            m_Scene->LoadSceneForRendering(m_PendingScenePath.c_str(), vkDev);

            // Update scene manager current scene (best-effort relative path)
            auto& projectMgr = Vans::VansProjectManager::Get();
            if (projectMgr.IsProjectLoaded())
            {
                std::string rel = projectMgr.MakeRelativePath(m_PendingScenePath);
                if (!rel.empty())
                    projectMgr.GetSceneManager().SetCurrentScene(rel);
            }

            m_PendingScenePath.clear();
        }

        // --- Profiler: begin frame ---
        VANS_PROFILER_BEGIN_FRAME();

        // Rendering, 这里会结束renderpass
        camera.Rendering();

        m_ScriptContext.SetScene(m_Scene);
        // 场景切换过程中跳过脚本更新，避免访问已卸载的数据
        if (m_Scene && m_Scene->IsSceneReady())
        {
            m_ScriptContext.VansScriptUpdate();
        }
        //UI Pass
        m_SceneWindow->RegistCamera(&camera);
        m_SceneWindow->RegistScene(m_Scene);
        DrawEditorWindows(static_cast<VansVKDevice*>(m_GraphicsDevice));

        // --- Profiler: end frame (resolve GPU, merge, compute FPS) ---
        {
            auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
            VANS_PROFILER_END_FRAME(vkDev->GetLogicDevice());
        }

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

    // Unregister camera input listeners
    UnregisterCameraInputListeners();

    m_Cameras.clear();

}

void VansGraphics::VansEditorWindow::DestroyVansEditorWindow()
{
    // Destroy GPU profiler
#if VANS_PROFILER_ENABLED
    Vans::VansGpuProfiler::Get().Destroy();
#endif

    // Unregister Physics Callback on shutdown to avoid calling into destroyed objects
    VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);
    VansEngine::VansPhysicsSystem::GetInstance().StopSimulation(); // Ensure thread stops

    // Shutdown input manager
    Vans::VansInputManager::Get().Shutdown();

    ImGui::DestroyContext();

    glfwDestroyWindow(m_VansEditorWindow.m_VansGraphicsHandle);
    glfwTerminate();
}
