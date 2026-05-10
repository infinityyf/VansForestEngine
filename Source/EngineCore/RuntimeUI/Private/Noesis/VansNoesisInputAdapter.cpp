#include "VansNoesisInputAdapter.h"

#include "../../../Util/VansInputManager.h"
#include "../../../Util/VansLog.h"

#include <NsGui/IView.h>
#include <NsGui/InputEnums.h>
#include <GLFW/glfw3.h>
#include "imgui.h"

namespace VansRuntime
{

// ─────────────────────────────────────────────────────────────────────────────

VansNoesisInputAdapter::VansNoesisInputAdapter()  = default;
VansNoesisInputAdapter::~VansNoesisInputAdapter() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisInputAdapter::Initialize()
{
    if (m_Initialized) return;

    auto& input = Vans::VansInputManager::Get();

    // Key events
    input.AddKeyListener(k_KeyListenerID,
        [this](int key, int scancode, int action, int mods)
        {
            OnKeyEvent(key, scancode, action, mods);
        });

    // Mouse move
    input.AddMouseMoveListener(k_MouseMoveListenerID,
        [this](double x, double y)
        {
            OnMouseMove(x, y);
        });

    // Mouse button clicks
    input.AddMouseClickListener(k_MouseClickListenerID,
        [this](int button, int action, int mods)
        {
            OnMouseClick(button, action, mods);
        });

    // Scroll
    input.AddScrollListener(k_ScrollListenerID,
        [this](double xOffset, double yOffset)
        {
            // Accumulate for the Update() flush
            m_ScrollAccumX += xOffset;
            m_ScrollAccumY += yOffset;
        });

    // Cache initial mouse position
    double mx = 0.0, my = 0.0;
    input.GetMousePosition(mx, my);
    m_LastMouseX = mx;
    m_LastMouseY = my;

    m_Initialized = true;
}

void VansNoesisInputAdapter::Shutdown()
{
    if (!m_Initialized) return;

    auto& input = Vans::VansInputManager::Get();
    input.RemoveKeyListener(k_KeyListenerID);
    input.RemoveMouseMoveListener(k_MouseMoveListenerID);
    input.RemoveMouseClickListener(k_MouseClickListenerID);
    input.RemoveScrollListener(k_ScrollListenerID);

    m_Views.clear();
    m_Initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame update — flush scroll and reset per-frame flags
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisInputAdapter::Update()
{
    m_WantsMouse    = false;
    m_WantsKeyboard = false;

    if (m_ScrollAccumY != 0.0 || m_ScrollAccumX != 0.0)
    {
        // 使用 ImGui 鼠标坐标，与 SetSceneViewport 的坐标系一致
        ImVec2 imPos = ImGui::GetMousePos();
        double mx = imPos.x, my = imPos.y;
        int ix = 0, iy = 0;
        TransformMouse(mx, my, ix, iy);

        // Noesis uses 120 units per scroll notch (same as Windows WHEEL_DELTA)
        const int rotationY = static_cast<int>(m_ScrollAccumY * 120.0);
        const int rotationX = static_cast<int>(m_ScrollAccumX * 120.0);

        for (auto* view : m_Views)
        {
            if (view == nullptr) continue;
            if (rotationY != 0)
                m_WantsMouse |= view->MouseWheel(ix, iy, rotationY);
            if (rotationX != 0)
                m_WantsMouse |= view->MouseHWheel(ix, iy, rotationX);
        }

        m_ScrollAccumX = 0.0;
        m_ScrollAccumY = 0.0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// View management
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisInputAdapter::AddView(Noesis::IView* view)
{
    if (view == nullptr) return;
    for (auto* v : m_Views)
    {
        if (v == view) return; // avoid duplicates
    }
    m_Views.push_back(view);
}

void VansNoesisInputAdapter::RemoveView(Noesis::IView* view)
{
    m_Views.erase(
        std::remove(m_Views.begin(), m_Views.end(), view),
        m_Views.end());
}

// ─────────────────────────────────────────────────────────────────────────────
// Viewport transform
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisInputAdapter::SetSceneViewport(float screenX, float screenY,
                                               float screenW, float screenH,
                                               float noesisW, float noesisH)
{
    m_ViewportX = screenX;
    m_ViewportY = screenY;
    m_ViewportW = screenW  > 0.0f ? screenW  : 1.0f;
    m_ViewportH = screenH  > 0.0f ? screenH  : 1.0f;
    m_NoesisW   = noesisW  > 0.0f ? noesisW  : 1.0f;
    m_NoesisH   = noesisH  > 0.0f ? noesisH  : 1.0f;
}

void VansNoesisInputAdapter::TransformMouse(double rawX, double rawY,
                                             int& outX, int& outY) const
{
    // X: 屏幕像素坐标线性映射到 Noesis 视图像素坐标（无翻转）
    const float nx = (static_cast<float>(rawX) - m_ViewportX) * (m_NoesisW / m_ViewportW);

    // Y: 同向映射。Noesis 使用 clipSpaceYInverted=true（Vulkan 原生 Y-down）
    // 配合 SceneUI pass 的标准正向 viewport，UI 方向正确；鼠标 Y 无需额外翻转。
    const float ny = (static_cast<float>(rawY) - m_ViewportY) * (m_NoesisH / m_ViewportH);

    outX = static_cast<int>(nx);
    outY = static_cast<int>(ny);
}

// ─────────────────────────────────────────────────────────────────────────────
// Event handlers
// ─────────────────────────────────────────────────────────────────────────────

void VansNoesisInputAdapter::OnKeyEvent(int glfwKey, int /*scancode*/, int action, int /*mods*/)
{
    const Noesis::Key noesisKey = ConvertGLFWKey(glfwKey);
    if (noesisKey == Noesis::Key_None) return;

    for (auto* view : m_Views)
    {
        if (view == nullptr) continue;
        if (action == GLFW_PRESS || action == GLFW_REPEAT)
            m_WantsKeyboard |= view->KeyDown(noesisKey);
        else if (action == GLFW_RELEASE)
            m_WantsKeyboard |= view->KeyUp(noesisKey);
    }
}

void VansNoesisInputAdapter::OnMouseMove(double /*x*/, double /*y*/)
{
    // 使用 ImGui 鼠标坐标，与 SetSceneViewport 传入的 GetItemRectMin() 坐标系一致
    ImVec2 imPos = ImGui::GetMousePos();
    m_LastMouseX = imPos.x;
    m_LastMouseY = imPos.y;

    int ix = 0, iy = 0;
    TransformMouse(imPos.x, imPos.y, ix, iy);

    for (auto* view : m_Views)
    {
        if (view == nullptr) continue;
        m_WantsMouse |= view->MouseMove(ix, iy);
    }
}

void VansNoesisInputAdapter::OnMouseClick(int glfwButton, int action, int /*mods*/)
{
    const Noesis::MouseButton noesisButton = ConvertGLFWMouseButton(glfwButton);

    // 使用 ImGui 鼠标坐标，与 SetSceneViewport 传入的 GetItemRectMin() 坐标系一致
    ImVec2 imPos = ImGui::GetMousePos();
    double mx = imPos.x;
    double my = imPos.y;
    int ix = 0, iy = 0;
    TransformMouse(mx, my, ix, iy);

    VANS_LOG("[NoesisInput] Click btn=" << glfwButton
        << " action=" << action
        << " raw=(" << (int)mx << "," << (int)my << ")"
        << " noesis=(" << ix << "," << iy << ")"
        << " viewport=(" << (int)m_ViewportX << "," << (int)m_ViewportY
        << " " << (int)m_ViewportW << "x" << (int)m_ViewportH << ")"
        << " noesisSize=(" << (int)m_NoesisW << "x" << (int)m_NoesisH << ")"
        << " inViewport=" << (mx >= m_ViewportX && mx <= m_ViewportX + m_ViewportW
                           && my >= m_ViewportY && my <= m_ViewportY + m_ViewportH ? "YES" : "NO"));

    for (auto* view : m_Views)
    {
        if (view == nullptr) continue;
        if (action == GLFW_PRESS)
            m_WantsMouse |= view->MouseButtonDown(ix, iy, noesisButton);
        else if (action == GLFW_RELEASE)
            m_WantsMouse |= view->MouseButtonUp(ix, iy, noesisButton);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GLFW → Noesis conversion tables
// ─────────────────────────────────────────────────────────────────────────────

Noesis::Key VansNoesisInputAdapter::ConvertGLFWKey(int glfwKey)
{
    switch (glfwKey)
    {
    case GLFW_KEY_SPACE:         return Noesis::Key_Space;
    case GLFW_KEY_APOSTROPHE:    return Noesis::Key_OemQuotes;
    case GLFW_KEY_COMMA:         return Noesis::Key_OemComma;
    case GLFW_KEY_MINUS:         return Noesis::Key_OemMinus;
    case GLFW_KEY_PERIOD:        return Noesis::Key_OemPeriod;
    case GLFW_KEY_SLASH:         return Noesis::Key_OemQuestion;
    case GLFW_KEY_0:             return Noesis::Key_D0;
    case GLFW_KEY_1:             return Noesis::Key_D1;
    case GLFW_KEY_2:             return Noesis::Key_D2;
    case GLFW_KEY_3:             return Noesis::Key_D3;
    case GLFW_KEY_4:             return Noesis::Key_D4;
    case GLFW_KEY_5:             return Noesis::Key_D5;
    case GLFW_KEY_6:             return Noesis::Key_D6;
    case GLFW_KEY_7:             return Noesis::Key_D7;
    case GLFW_KEY_8:             return Noesis::Key_D8;
    case GLFW_KEY_9:             return Noesis::Key_D9;
    case GLFW_KEY_SEMICOLON:     return Noesis::Key_OemSemicolon;
    case GLFW_KEY_EQUAL:         return Noesis::Key_OemPlus;
    case GLFW_KEY_A:             return Noesis::Key_A;
    case GLFW_KEY_B:             return Noesis::Key_B;
    case GLFW_KEY_C:             return Noesis::Key_C;
    case GLFW_KEY_D:             return Noesis::Key_D;
    case GLFW_KEY_E:             return Noesis::Key_E;
    case GLFW_KEY_F:             return Noesis::Key_F;
    case GLFW_KEY_G:             return Noesis::Key_G;
    case GLFW_KEY_H:             return Noesis::Key_H;
    case GLFW_KEY_I:             return Noesis::Key_I;
    case GLFW_KEY_J:             return Noesis::Key_J;
    case GLFW_KEY_K:             return Noesis::Key_K;
    case GLFW_KEY_L:             return Noesis::Key_L;
    case GLFW_KEY_M:             return Noesis::Key_M;
    case GLFW_KEY_N:             return Noesis::Key_N;
    case GLFW_KEY_O:             return Noesis::Key_O;
    case GLFW_KEY_P:             return Noesis::Key_P;
    case GLFW_KEY_Q:             return Noesis::Key_Q;
    case GLFW_KEY_R:             return Noesis::Key_R;
    case GLFW_KEY_S:             return Noesis::Key_S;
    case GLFW_KEY_T:             return Noesis::Key_T;
    case GLFW_KEY_U:             return Noesis::Key_U;
    case GLFW_KEY_V:             return Noesis::Key_V;
    case GLFW_KEY_W:             return Noesis::Key_W;
    case GLFW_KEY_X:             return Noesis::Key_X;
    case GLFW_KEY_Y:             return Noesis::Key_Y;
    case GLFW_KEY_Z:             return Noesis::Key_Z;
    case GLFW_KEY_LEFT_BRACKET:  return Noesis::Key_OemOpenBrackets;
    case GLFW_KEY_BACKSLASH:     return Noesis::Key_OemBackslash;
    case GLFW_KEY_RIGHT_BRACKET: return Noesis::Key_OemCloseBrackets;
    case GLFW_KEY_GRAVE_ACCENT:  return Noesis::Key_OemTilde;
    case GLFW_KEY_ESCAPE:        return Noesis::Key_Escape;
    case GLFW_KEY_ENTER:         return Noesis::Key_Return;
    case GLFW_KEY_TAB:           return Noesis::Key_Tab;
    case GLFW_KEY_BACKSPACE:     return Noesis::Key_Back;
    case GLFW_KEY_INSERT:        return Noesis::Key_Insert;
    case GLFW_KEY_DELETE:        return Noesis::Key_Delete;
    case GLFW_KEY_RIGHT:         return Noesis::Key_Right;
    case GLFW_KEY_LEFT:          return Noesis::Key_Left;
    case GLFW_KEY_DOWN:          return Noesis::Key_Down;
    case GLFW_KEY_UP:            return Noesis::Key_Up;
    case GLFW_KEY_PAGE_UP:       return Noesis::Key_PageUp;
    case GLFW_KEY_PAGE_DOWN:     return Noesis::Key_PageDown;
    case GLFW_KEY_HOME:          return Noesis::Key_Home;
    case GLFW_KEY_END:           return Noesis::Key_End;
    case GLFW_KEY_CAPS_LOCK:     return Noesis::Key_CapsLock;
    case GLFW_KEY_SCROLL_LOCK:   return Noesis::Key_Scroll;
    case GLFW_KEY_NUM_LOCK:      return Noesis::Key_NumLock;
    case GLFW_KEY_PRINT_SCREEN:  return Noesis::Key_PrintScreen;
    case GLFW_KEY_PAUSE:         return Noesis::Key_Pause;
    case GLFW_KEY_F1:            return Noesis::Key_F1;
    case GLFW_KEY_F2:            return Noesis::Key_F2;
    case GLFW_KEY_F3:            return Noesis::Key_F3;
    case GLFW_KEY_F4:            return Noesis::Key_F4;
    case GLFW_KEY_F5:            return Noesis::Key_F5;
    case GLFW_KEY_F6:            return Noesis::Key_F6;
    case GLFW_KEY_F7:            return Noesis::Key_F7;
    case GLFW_KEY_F8:            return Noesis::Key_F8;
    case GLFW_KEY_F9:            return Noesis::Key_F9;
    case GLFW_KEY_F10:           return Noesis::Key_F10;
    case GLFW_KEY_F11:           return Noesis::Key_F11;
    case GLFW_KEY_F12:           return Noesis::Key_F12;
    case GLFW_KEY_KP_0:          return Noesis::Key_NumPad0;
    case GLFW_KEY_KP_1:          return Noesis::Key_NumPad1;
    case GLFW_KEY_KP_2:          return Noesis::Key_NumPad2;
    case GLFW_KEY_KP_3:          return Noesis::Key_NumPad3;
    case GLFW_KEY_KP_4:          return Noesis::Key_NumPad4;
    case GLFW_KEY_KP_5:          return Noesis::Key_NumPad5;
    case GLFW_KEY_KP_6:          return Noesis::Key_NumPad6;
    case GLFW_KEY_KP_7:          return Noesis::Key_NumPad7;
    case GLFW_KEY_KP_8:          return Noesis::Key_NumPad8;
    case GLFW_KEY_KP_9:          return Noesis::Key_NumPad9;
    case GLFW_KEY_KP_DECIMAL:    return Noesis::Key_Decimal;
    case GLFW_KEY_KP_DIVIDE:     return Noesis::Key_Divide;
    case GLFW_KEY_KP_MULTIPLY:   return Noesis::Key_Multiply;
    case GLFW_KEY_KP_SUBTRACT:   return Noesis::Key_Subtract;
    case GLFW_KEY_KP_ADD:        return Noesis::Key_Add;
    case GLFW_KEY_KP_ENTER:      return Noesis::Key_Return;
    case GLFW_KEY_LEFT_SHIFT:    return Noesis::Key_LeftShift;
    case GLFW_KEY_LEFT_CONTROL:  return Noesis::Key_LeftCtrl;
    case GLFW_KEY_LEFT_ALT:      return Noesis::Key_LeftAlt;
    case GLFW_KEY_LEFT_SUPER:    return Noesis::Key_LWin;
    case GLFW_KEY_RIGHT_SHIFT:   return Noesis::Key_RightShift;
    case GLFW_KEY_RIGHT_CONTROL: return Noesis::Key_RightCtrl;
    case GLFW_KEY_RIGHT_ALT:     return Noesis::Key_RightAlt;
    case GLFW_KEY_RIGHT_SUPER:   return Noesis::Key_RWin;
    default:                     return Noesis::Key_None;
    }
}

Noesis::MouseButton VansNoesisInputAdapter::ConvertGLFWMouseButton(int glfwButton)
{
    switch (glfwButton)
    {
    case GLFW_MOUSE_BUTTON_LEFT:   return Noesis::MouseButton_Left;
    case GLFW_MOUSE_BUTTON_RIGHT:  return Noesis::MouseButton_Right;
    case GLFW_MOUSE_BUTTON_MIDDLE: return Noesis::MouseButton_Middle;
    default:                       return Noesis::MouseButton_Left;
    }
}

} // namespace VansRuntime
