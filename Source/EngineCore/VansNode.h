#pragma once

namespace VansGraphics
{
    // Common base for runtime engine nodes.
    //
    // Provides a unified enable/disable/destroy lifecycle used by render,
    // physics, animation, audio, cloth, and other node types.
    class VansNode
    {
    public:
        VansNode() = default;
        virtual ~VansNode() = default;

        // Enable state control.
        void SetEnabled(bool enabled)
        {
            if (m_Enabled == enabled) return;
            m_Enabled = enabled;
            if (enabled)
                OnEnable();
            else
                OnDisable();
        }

        bool IsEnabled() const { return m_Enabled; }

        // Destroy hook.
        void Destroy()
        {
            OnDestroy();
            m_Enabled = false;
        }

    protected:
        // Derived nodes override these hooks to implement their own lifecycle behavior.
        virtual void OnEnable()  {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}

        bool m_Enabled = true;
    };

} // namespace VansGraphics
