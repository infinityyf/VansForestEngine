#pragma once
#include "VansParticleEmitter.h"
#include <nlohmann/json.hpp>
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
        std::string m_SimSpace  = "Local";  // "Local" | "World"

        // ── Emitter 列表 ─────────────────────────────────────────
        std::vector<std::unique_ptr<VansParticleEmitter>> m_Emitters;

        // ── 接口 ─────────────────────────────────────────────────

        // 从文件加载：失败返回 false
        bool LoadFromFile(const std::string& filePath);

        // 序列化到文件：失败返回 false
        bool SaveToFile(const std::string& filePath) const;

        // 序列化到 JSON 对象
        nlohmann::json Serialize() const;

        // 从 JSON 对象反序列化（会清空现有数据）
        void Deserialize(const nlohmann::json& j);
    };

} // namespace VansGraphics
