#pragma once

#include <NsGui/IView.h>
#include <vector>
#include <string>

namespace Vans
{
    class VansInputManager;
}

namespace VansRuntime
{

/// ─────────────────────────────────────────────────────────────────────────────
/// VansNoesisInputAdapter
///
/// Bridges VansInputManager events → Noesis IView input API.
///
/// Usage:
///   1. Call Initialize() once after the IViews are created.
///   2. Call AddView() / RemoveView() as documents are loaded/unloaded.
///   3. Call Update() every frame (sends mouse move + scroll).
///   4. Call Shutdown() on teardown.
///
/// Convention: All input MUST go through VansInputManager::Get().
///             Never read glfwGetKey or ImGui IO directly.
/// ─────────────────────────────────────────────────────────────────────────────
class VansNoesisInputAdapter
{
public:
    VansNoesisInputAdapter();
    ~VansNoesisInputAdapter();

    // Lifecycle
    void Initialize();
    void Shutdown();

    /// Called every frame to push mouse-move and scroll to all registered views
    void Update();

    // View management — does NOT take ownership
    void AddView(Noesis::IView* view);
    void RemoveView(Noesis::IView* view);

    /// Returns true if Noesis consumed mouse input this frame (any view handled it)
    bool WantsMouse() const { return m_WantsMouse; }

    /// Returns true if Noesis consumed keyboard input this frame (any view handled it)
    bool WantsKeyboard() const { return m_WantsKeyboard; }

    /// Set the screen-space rect occupied by the scene image (ImGui viewport coordinates)
    /// and the Noesis view dimensions, so that raw GLFW cursor coords are transformed
    /// into Noesis view-local coordinates before being sent to IView::MouseMove / MouseButtonDown.
    /// Call every frame from VansSceneWindow after ImGui::Image().
    void SetSceneViewport(float screenX, float screenY,
                          float screenW, float screenH,
                          float noesisW, float noesisH);

private:
    // Transform a raw GLFW cursor position to Noesis view-local integer coords
    void TransformMouse(double rawX, double rawY, int& outX, int& outY) const;
    // Internal event handlers registered with VansInputManager
    void OnKeyEvent(int key, int scancode, int action, int mods);
    void OnMouseMove(double x, double y);
    void OnMouseClick(int button, int action, int mods);
    void OnScroll(double xOffset, double yOffset);

    // GLFW key → Noesis Key conversion
    static Noesis::Key ConvertGLFWKey(int glfwKey);

    // GLFW mouse button index → Noesis MouseButton
    static Noesis::MouseButton ConvertGLFWMouseButton(int glfwButton);

private:
    std::vector<Noesis::IView*> m_Views;
    bool  m_Initialized   = false;
    bool  m_WantsMouse    = false;
    bool  m_WantsKeyboard = false;

    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;

    // Scene-image viewport in screen (GLFW window) coords
    float m_ViewportX  = 0.0f;
    float m_ViewportY  = 0.0f;
    float m_ViewportW  = 1.0f;
    float m_ViewportH  = 1.0f;
    // Noesis IView dimensions (set once at init via SetSceneViewport)
    float m_NoesisW    = 1920.0f;
    float m_NoesisH    = 1080.0f;

    // Scroll accumulator — cleared each Update()
    double m_ScrollAccumX = 0.0;
    double m_ScrollAccumY = 0.0;

    // Listener IDs registered with VansInputManager
    static constexpr const char* k_KeyListenerID        = "VansNoesisInputAdapter_Key";
    static constexpr const char* k_MouseMoveListenerID  = "VansNoesisInputAdapter_MouseMove";
    static constexpr const char* k_MouseClickListenerID = "VansNoesisInputAdapter_MouseClick";
    static constexpr const char* k_ScrollListenerID     = "VansNoesisInputAdapter_Scroll";
};

} // namespace VansRuntime
