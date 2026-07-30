#pragma once

#include <NsGui/IView.h>
#include "../../../EventCore/VansEventConnection.h"
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
///             Never read platform input APIs or editor UI state directly.
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

    /// Set the screen-space rect occupied by the scene image
    /// and the Noesis view dimensions, so that raw cursor coords are transformed
    /// into Noesis view-local coordinates before being sent to IView::MouseMove / MouseButtonDown.
    /// Call every frame after the host has resolved the scene viewport rectangle.
    void SetSceneViewport(float screenX, float screenY,
                          float screenW, float screenH,
                          float noesisW, float noesisH);

    void TransformMouseToView(double rawX, double rawY,
                              double& outX, double& outY) const;
    void GetViewSize(double& outW, double& outH) const;

private:
    // Transform a raw cursor position to Noesis view-local integer coords
    void TransformMouse(double rawX, double rawY, int& outX, int& outY) const;
    // Internal event handlers registered with VansInputManager
    void OnKeyEvent(int key, int scancode, int action, int mods);
    void OnMouseMove(double x, double y);
    void OnMouseClick(int button, int action, int mods);
    void OnScroll(double xOffset, double yOffset);

    // GLFW key to Noesis Key conversion
    static Noesis::Key ConvertGLFWKey(int glfwKey);

    // GLFW mouse button index to Noesis MouseButton
    static Noesis::MouseButton ConvertGLFWMouseButton(int glfwButton);

private:
    std::vector<Noesis::IView*> m_Views;
    bool  m_Initialized   = false;
    bool  m_WantsMouse    = false;
    bool  m_WantsKeyboard = false;

    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;

    // Scene-image viewport in screen coords
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

    Vans::VansScopedEventConnections m_InputConnections;
};

} // namespace VansRuntime
