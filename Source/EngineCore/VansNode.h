#pragma once

namespace VansGraphics
{
    // ─────────────────────────────────────────────────────────────────
    //  VansNode — 所有引擎功能 Node 的共同基类
    //
    //  提供统一的 enable/disable/destroy 生命周期接口。
    //  各功能 Node（Render / Physics / Animation / Audio / Cloth / …）
    //  继承此类并重写虚函数以实现各自的开关语义。
    //
    //  位置：Source/EngineCore/VansNode.h（引擎根目录）
    //  原因：PhysicsCore / AudioCore 等模块需继承此基类，
    //        放在 RenderCore 会产生反向依赖，破坏模块分层。
    // ─────────────────────────────────────────────────────────────────
    class VansNode
    {
    public:
        VansNode() = default;
        virtual ~VansNode() = default;

        // ── 开关控制 ──────────────────────────────────────────────
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

        // ── 销毁 ──────────────────────────────────────────────────
        void Destroy()
        {
            OnDestroy();
            m_Enabled = false;
        }

    protected:
        // 子类重写以定义各自的开关行为
        virtual void OnEnable()  {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}

        bool m_Enabled = true;  // protected: 允许派生类构造函数直接初始化
    };

} // namespace VansGraphics
