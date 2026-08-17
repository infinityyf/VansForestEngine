#include "VansInputManager.h"
#include "VansInputEvents.h"
#include "VansLog.h"
#include "../EventCore/VansEventBus.h"
#include "GLFW/glfw3.h"

#include <iostream>

namespace Vans
{
    // -------------------------------------------------------------------------
    // Singleton
    // -------------------------------------------------------------------------
    VansInputManager& VansInputManager::Get()
    {
        static VansInputManager instance;
        return instance;
    }

    VansInputManager::VansInputManager()  = default;
    VansInputManager::~VansInputManager() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------
    void VansInputManager::Initialize(GLFWwindow* window)
    {
        if (m_Initialized)
        {
            VANS_LOG_WARN("[VansInputManager] Already initialized.");
            return;
        }

        m_Window = window;
        m_Initialized = true;
        m_FirstMouseUpdate = true;

        // Install GLFW callbacks
        glfwSetKeyCallback(window, GLFWKeyCallback);
        glfwSetCursorPosCallback(window, GLFWMousePosCallback);
        glfwSetMouseButtonCallback(window, GLFWMouseButtonCallback);
        glfwSetScrollCallback(window, GLFWScrollCallback);

        VANS_LOG("[VansInputManager] Initialized.");
    }

    void VansInputManager::Shutdown()
    {
        if (!m_Initialized) return;

        SetCursorCaptureEnabled(false);

        // Remove callbacks
        if (m_Window)
        {
            glfwSetKeyCallback(m_Window, nullptr);
            glfwSetCursorPosCallback(m_Window, nullptr);
            glfwSetMouseButtonCallback(m_Window, nullptr);
            glfwSetScrollCallback(m_Window, nullptr);
        }

        m_KeyStates.clear();
        m_MouseButtonStates.clear();
        m_ActionBindings.clear();
        m_AxisBindings.clear();
        m_Window = nullptr;
        m_Initialized = false;
        m_CursorCaptureAllowed = false;
        m_CursorCaptureRequested = false;
        m_CursorCaptureEnabled = false;

        VANS_LOG("[VansInputManager] Shutdown.");
    }

    void VansInputManager::SetCursorCaptureAllowed(bool allowed)
    {
        m_CursorCaptureAllowed = allowed;
        SetCursorCaptureEnabled(m_CursorCaptureRequested);
    }

    void VansInputManager::SetCursorCaptureEnabled(bool enabled)
    {
        m_CursorCaptureRequested = enabled;

        if (!m_Initialized || !m_Window)
        {
            m_CursorCaptureEnabled = false;
            return;
        }

        const bool effectiveEnabled = enabled && m_CursorCaptureAllowed;
        if (m_CursorCaptureEnabled == effectiveEnabled)
            return;

        m_CursorCaptureEnabled = effectiveEnabled;
        glfwSetInputMode(
            m_Window,
            GLFW_CURSOR,
            effectiveEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        if (glfwRawMouseMotionSupported())
        {
            glfwSetInputMode(
                m_Window,
                GLFW_RAW_MOUSE_MOTION,
                effectiveEnabled ? GLFW_TRUE : GLFW_FALSE);
        }

        glfwGetCursorPos(m_Window, &m_MouseX, &m_MouseY);
        m_LastMouseX = m_MouseX;
        m_LastMouseY = m_MouseY;
        m_MouseDeltaX = 0.0;
        m_MouseDeltaY = 0.0;
        m_FirstMouseUpdate = true;

        VANS_LOG("[VansInputManager] Cursor capture "
            << (effectiveEnabled ? "enabled" : "disabled"));
    }

    // -------------------------------------------------------------------------
    // Per-frame update — call ONCE at the beginning of each frame
    // -------------------------------------------------------------------------
    void VansInputManager::Update()
    {
        if (!m_Initialized || !m_Window)
            return;

        // Carry forward: for keys NOT updated this frame, copy isDown -> wasDown
        for (auto& [key, state] : m_KeyStates)
        {
            state.wasDown = state.isDown;
        }
        for (auto& [btn, state] : m_MouseButtonStates)
        {
            state.wasDown = state.isDown;
        }

        m_KeysUpdatedThisFrame.clear();
        m_MouseButtonsUpdatedThisFrame.clear();

		// Update 只开启新输入帧；窗口事件由调用方随后 PollEvents，再统一调用
		// RefreshPolledState。若在 PollEvents 前先读取一次光标位置，普通可见光标
		// 的位移会在第二次刷新时被覆盖为 0，导致编辑器 Play 无法驱动相机。
		m_MouseDeltaX = 0.0;
		m_MouseDeltaY = 0.0;
		m_ScrollDeltaX = 0.0;
		m_ScrollDeltaY = 0.0;
    }

    void VansInputManager::RefreshPolledState()
    {
        if (!m_Initialized || !m_Window)
            return;

        // GLFW callbacks are still the event source for listeners, but polling keeps
        // gameplay input robust if a held key starts before a callback is observed.
        for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
        {
            const int state = glfwGetKey(m_Window, key);
            if (state == GLFW_PRESS || state == GLFW_RELEASE)
                m_KeyStates[key].isDown = (state == GLFW_PRESS);
        }
        for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
        {
            const int state = glfwGetMouseButton(m_Window, button);
            if (state == GLFW_PRESS || state == GLFW_RELEASE)
                m_MouseButtonStates[button].isDown = (state == GLFW_PRESS);
        }

        // Mouse delta
        glfwGetCursorPos(m_Window, &m_MouseX, &m_MouseY);
        if (m_FirstMouseUpdate)
        {
            m_LastMouseX = m_MouseX;
            m_LastMouseY = m_MouseY;
            m_MouseDeltaX = 0.0;
            m_MouseDeltaY = 0.0;
            m_FirstMouseUpdate = false;
        }
        else
        {
            m_MouseDeltaX = m_MouseX - m_LastMouseX;
            m_MouseDeltaY = m_MouseY - m_LastMouseY;
            m_LastMouseX = m_MouseX;
            m_LastMouseY = m_MouseY;
        }

        // Scroll — consume accumulated values
        m_ScrollDeltaX = m_ScrollAccumX;
        m_ScrollDeltaY = m_ScrollAccumY;
        m_ScrollAccumX = 0.0;
        m_ScrollAccumY = 0.0;
    }

    // -------------------------------------------------------------------------
    // Raw Key Queries
    // -------------------------------------------------------------------------
    bool VansInputManager::IsKeyDown(int glfwKey) const
    {
        auto it = m_KeyStates.find(glfwKey);
        if (it != m_KeyStates.end())
            return it->second.isDown;
        return false;
    }

    bool VansInputManager::IsKeyPressed(int glfwKey) const
    {
        auto it = m_KeyStates.find(glfwKey);
        if (it != m_KeyStates.end())
            return it->second.isDown && !it->second.wasDown;
        return false;
    }

    bool VansInputManager::IsKeyReleased(int glfwKey) const
    {
        auto it = m_KeyStates.find(glfwKey);
        if (it != m_KeyStates.end())
            return !it->second.isDown && it->second.wasDown;
        return false;
    }

    // -------------------------------------------------------------------------
    // Raw Mouse Queries
    // -------------------------------------------------------------------------
    bool VansInputManager::IsMouseButtonDown(MouseButton button) const
    {
        int btn = static_cast<int>(button);
        auto it = m_MouseButtonStates.find(btn);
        if (it != m_MouseButtonStates.end())
            return it->second.isDown;
        return false;
    }

    bool VansInputManager::IsMouseButtonPressed(MouseButton button) const
    {
        int btn = static_cast<int>(button);
        auto it = m_MouseButtonStates.find(btn);
        if (it != m_MouseButtonStates.end())
            return it->second.isDown && !it->second.wasDown;
        return false;
    }

    bool VansInputManager::IsMouseButtonReleased(MouseButton button) const
    {
        int btn = static_cast<int>(button);
        auto it = m_MouseButtonStates.find(btn);
        if (it != m_MouseButtonStates.end())
            return !it->second.isDown && it->second.wasDown;
        return false;
    }

    void VansInputManager::GetMousePosition(double& outX, double& outY) const
    {
        outX = m_MouseX;
        outY = m_MouseY;
    }

    void VansInputManager::GetMouseDelta(double& outDX, double& outDY) const
    {
        outDX = m_MouseDeltaX;
        outDY = m_MouseDeltaY;
    }

    void VansInputManager::GetScrollDelta(double& outX, double& outY) const
    {
        outX = m_ScrollDeltaX;
        outY = m_ScrollDeltaY;
    }

    // -------------------------------------------------------------------------
    // Action / Axis System
    // -------------------------------------------------------------------------
    void VansInputManager::RegisterAction(const std::string& name, int glfwKey)
    {
        m_ActionBindings[name] = glfwKey;
    }

    void VansInputManager::UnregisterAction(const std::string& name)
    {
        m_ActionBindings.erase(name);
    }

    bool VansInputManager::IsActionDown(const std::string& name) const
    {
        auto it = m_ActionBindings.find(name);
        if (it == m_ActionBindings.end()) return false;
        return IsKeyDown(it->second);
    }

    bool VansInputManager::IsActionPressed(const std::string& name) const
    {
        auto it = m_ActionBindings.find(name);
        if (it == m_ActionBindings.end()) return false;
        return IsKeyPressed(it->second);
    }

    bool VansInputManager::IsActionReleased(const std::string& name) const
    {
        auto it = m_ActionBindings.find(name);
        if (it == m_ActionBindings.end()) return false;
        return IsKeyReleased(it->second);
    }

    void VansInputManager::RegisterAxis(const std::string& name, int positiveKey, int negativeKey)
    {
        m_AxisBindings[name] = { name, positiveKey, negativeKey };
    }

    void VansInputManager::UnregisterAxis(const std::string& name)
    {
        m_AxisBindings.erase(name);
    }

    float VansInputManager::GetAxis(const std::string& name) const
    {
        auto it = m_AxisBindings.find(name);
        if (it == m_AxisBindings.end()) return 0.0f;

        float value = 0.0f;
        if (IsKeyDown(it->second.positiveKey))  value += 1.0f;
        if (IsKeyDown(it->second.negativeKey))  value -= 1.0f;
        return value;
    }

    // -------------------------------------------------------------------------
    // GLFW Callback Trampolines
    // -------------------------------------------------------------------------
    void VansInputManager::GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        // Let ImGui process the event first
        // (ImGui backend hooks are installed separately via ImGui_ImplGlfw_InitForVulkan)

        VansInputManager* self = &VansInputManager::Get();
        if (!self->m_Initialized || self->m_Window != window) return;

        // Update key state
        auto& state = self->m_KeyStates[key];
        if (action == GLFW_PRESS)
        {
            state.isDown = true;
        }
        else if (action == GLFW_RELEASE)
        {
            state.isDown = false;
        }
        // GLFW_REPEAT — isDown stays true, no state change needed

        self->m_KeysUpdatedThisFrame.insert(key);

        VansEventBus::Get().PublishNow(VansKeyEvent{ key, scancode, action, mods });
    }

    void VansInputManager::GLFWMousePosCallback(GLFWwindow* window, double xpos, double ypos)
    {
        VansInputManager* self = &VansInputManager::Get();
        if (!self->m_Initialized || self->m_Window != window) return;

        const double previousX = self->m_MouseX;
        const double previousY = self->m_MouseY;
        self->m_MouseX = xpos;
        self->m_MouseY = ypos;

        VansEventBus::Get().PublishNow(VansMouseMoveEvent{
            xpos,
            ypos,
            xpos - previousX,
            ypos - previousY
        });
    }

    void VansInputManager::GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        VansInputManager* self = &VansInputManager::Get();
        if (!self->m_Initialized || self->m_Window != window) return;

        auto& state = self->m_MouseButtonStates[button];
        state.isDown = (action == GLFW_PRESS);

        self->m_MouseButtonsUpdatedThisFrame.insert(button);

        VansEventBus::Get().PublishNow(VansMouseButtonEvent{ button, action, mods });
    }

    void VansInputManager::GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        VansInputManager* self = &VansInputManager::Get();
        if (!self->m_Initialized || self->m_Window != window) return;

        self->m_ScrollAccumX += xoffset;
        self->m_ScrollAccumY += yoffset;

        VansEventBus::Get().PublishNow(VansMouseScrollEvent{ xoffset, yoffset });
    }
}
