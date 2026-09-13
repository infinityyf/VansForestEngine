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
    enum class VansParticleEmissionFrame { Source, World };
    class VansParticleAsset
    {
    public:
        // ── 元信息 ───────────────────────────────────────────────
        std::string m_Name;

        // ── 全局配置 ─────────────────────────────────────────────
        float       m_Duration  = 5.f;
        bool        m_Loop      = true;
        bool        m_Prewarm   = false;
        float       m_StartDelay = 0.0f;
        VansParticleEmissionFrame m_EmissionFrame = VansParticleEmissionFrame::Source;
        float m_FixedStep = 0.0f; // 0 为帧步长，其余为受预算限制的固定步长。
        uint32_t m_MaxSubsteps = 8;
        float m_DrainFade = 0.0f; // 0 表示仅由点寿命决定结束；正值为停止后的淡出时长。

        // ── Emitter 列表 ─────────────────────────────────────────
        std::vector<std::unique_ptr<const VansParticleEmitter>> m_Emitters;
        std::vector<Vans::VansAssetGuid> TextureDependencies() const;

    };

} // namespace VansGraphics
