#pragma once
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <vector>
#include <mutex>

struct GLFWwindow;

namespace Vans
{
    // Mouse button identifiers (mirrors GLFW values)
    enum class MouseButton : int
    {
        Left   = 0,
        Right  = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7
    };

    // Key state for frame-based queries
    struct KeyState
    {
        bool isDown      = false;   // Currently held
        bool wasDown     = false;   // Held last frame
    };

    // Action binding: map a human-readable name to a key
    struct InputAction
    {
        std::string name;
        int         keyCode;        // GLFW key code
    };

    // Axis binding: map a name to positive/negative keys
    struct InputAxis
    {
        std::string name;
        int         positiveKey;
        int         negativeKey;
    };

    //----------------------------------------------------------------------
    // VansInputManager — Centralized input system
    //
    // Usage:
    //   1. Call Initialize(window) once after GLFW window creation
    //   2. Call Update() once per frame BEFORE any input queries
    //   3. Query state with IsKeyDown / IsKeyPressed / IsKeyReleased etc.
    //   4. Optionally register named actions/axes for gameplay input
    //   5. Call Shutdown() on cleanup
    //----------------------------------------------------------------------
    class VansInputManager
    {
    public:
        // Singleton access
        static VansInputManager& Get();

        // Lifecycle
        void Initialize(GLFWwindow* window);
        void Shutdown();

        // Call once per frame at the start of the update loop
        void Update();

        // -------- Raw Key Queries --------

        /// Key is currently held down
        bool IsKeyDown(int glfwKey) const;

        /// Key was pressed this frame (down now, was up last frame)
        bool IsKeyPressed(int glfwKey) const;

        /// Key was released this frame (up now, was down last frame)
        bool IsKeyReleased(int glfwKey) const;

        // -------- Raw Mouse Queries --------

        /// Mouse button is currently held down
        bool IsMouseButtonDown(MouseButton button) const;

        /// Mouse button pressed this frame
        bool IsMouseButtonPressed(MouseButton button) const;

        /// Mouse button released this frame
        bool IsMouseButtonReleased(MouseButton button) const;

        /// Current mouse position in screen coordinates
        void GetMousePosition(double& outX, double& outY) const;

        /// Mouse movement delta since last frame
        void GetMouseDelta(double& outDX, double& outDY) const;

        /// Mouse scroll delta this frame
        void GetScrollDelta(double& outX, double& outY) const;

        // -------- Action / Axis System --------

        /// Register a named action bound to a GLFW key code
        void RegisterAction(const std::string& name, int glfwKey);

        /// Unregister a named action
        void UnregisterAction(const std::string& name);

        /// Is the action's key currently held
        bool IsActionDown(const std::string& name) const;

        /// Was the action's key pressed this frame
        bool IsActionPressed(const std::string& name) const;

        /// Was the action's key released this frame
        bool IsActionReleased(const std::string& name) const;

        /// Register a named axis with positive/negative keys. Returns -1, 0, or 1.
        void RegisterAxis(const std::string& name, int positiveKey, int negativeKey);

        /// Unregister a named axis
        void UnregisterAxis(const std::string& name);

        /// Get axis value: -1.0, 0.0, or 1.0
        float GetAxis(const std::string& name) const;

        // -------- Callback Registration --------

        using KeyCallback   = std::function<void(int key, int scancode, int action, int mods)>;
        using MouseCallback = std::function<void(double x, double y)>;
        using ClickCallback = std::function<void(int button, int action, int mods)>;
        using ScrollCallback = std::function<void(double xOffset, double yOffset)>;

        /// Add a listener that receives raw key events (GLFW-style)
        void AddKeyListener(const std::string& id, KeyCallback callback);
        void RemoveKeyListener(const std::string& id);

        /// Add a listener that receives raw mouse move events
        void AddMouseMoveListener(const std::string& id, MouseCallback callback);
        void RemoveMouseMoveListener(const std::string& id);

        /// Add a listener for mouse button clicks
        void AddMouseClickListener(const std::string& id, ClickCallback callback);
        void RemoveMouseClickListener(const std::string& id);

        /// Add a listener for scroll events
        void AddScrollListener(const std::string& id, ScrollCallback callback);
        void RemoveScrollListener(const std::string& id);

        // -------- Utility --------

        GLFWwindow* GetWindow() const { return m_Window; }

    private:
        VansInputManager();
        ~VansInputManager();

        VansInputManager(const VansInputManager&) = delete;
        VansInputManager& operator=(const VansInputManager&) = delete;

        // GLFW callback trampolines (static)
        static void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void GLFWMousePosCallback(GLFWwindow* window, double xpos, double ypos);
        static void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    private:
        GLFWwindow* m_Window = nullptr;
        bool        m_Initialized = false;

        // Key states: glfwKey -> state
        mutable std::unordered_map<int, KeyState> m_KeyStates;

        // Mouse button states
        mutable std::unordered_map<int, KeyState> m_MouseButtonStates;

        // Mouse position
        double m_MouseX     = 0.0;
        double m_MouseY     = 0.0;
        double m_LastMouseX  = 0.0;
        double m_LastMouseY  = 0.0;
        double m_MouseDeltaX = 0.0;
        double m_MouseDeltaY = 0.0;
        bool   m_FirstMouseUpdate = true;

        // Scroll
        double m_ScrollDeltaX = 0.0;
        double m_ScrollDeltaY = 0.0;
        double m_ScrollAccumX = 0.0;
        double m_ScrollAccumY = 0.0;

        // Keys that received events this frame (for state transitions)
        std::unordered_set<int> m_KeysUpdatedThisFrame;
        std::unordered_set<int> m_MouseButtonsUpdatedThisFrame;

        // Action bindings
        std::unordered_map<std::string, int> m_ActionBindings;

        // Axis bindings
        std::unordered_map<std::string, InputAxis> m_AxisBindings;

        // Listeners
        std::unordered_map<std::string, KeyCallback>    m_KeyListeners;
        std::unordered_map<std::string, MouseCallback>  m_MouseMoveListeners;
        std::unordered_map<std::string, ClickCallback>  m_MouseClickListeners;
        std::unordered_map<std::string, ScrollCallback> m_ScrollListeners;
    };
}
