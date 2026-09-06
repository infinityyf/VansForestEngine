#pragma once
#include "VansParticleEmitter.h"
#include <string>
#include <vector>
#include <memory>

namespace VansGraphics
{
    // ============================================================
    // VansParticleAsset — 粒子资产（对应 .particle JSON 文件）
    // 一个 Asset 包含若干 Emitter，可被多个
    // VansScriptParticleComponent 引用（运行时每个 Component 有
    // 独立的 VansParticleRuntime 拷贝，不共享状态）。
    // ============================================================
    class VansParticleAsset
    {
    public:
        // ── 元信息 ───────────────────────────────────────────────
        std::string m_Name;
        std::string m_FilePath;       // .particle 文件路径
        int         m_Version  = 1;

        // ── 全局配置 ─────────────────────────────────────────────
        float       m_Duration  = 5.f;
        bool        m_Loop      = true;
        bool        m_Prewarm   = false;
        float       m_StartDelay = 0.0f;
        bool        m_WorldAligned = false; // 跟随对象位置，发射方向和尺寸使用世界轴与米制。
        std::string m_SimSpace  = "Local";  // "Local" | "World"

        // ── Emitter 列表 ─────────────────────────────────────────
        std::vector<std::unique_ptr<VansParticleEmitter>> m_Emitters;

    };

} // namespace VansGraphics
