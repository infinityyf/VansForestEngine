#pragma once
#include "VansParticleData.h"
#include "VansParticleInstanceData.h"
#include "Modules/VansInitModules.h"
#include "Modules/VansUpdateModules.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <vector>
#include <memory>
#include <string>

namespace VansGraphics
{
    // ============================================================
    // 发射配置
    // ============================================================

    enum class VansSpawnType
    {
        RateOverTime,       // 按速率持续发射
        Burst,              // 爆发式发射
        RateOverDistance,   // 按移动距离发射（暂为桩）
    };

    struct BurstConfig
    {
        float    time     = 0.f;  // 在 systemTime 等于该值时触发
        uint32_t count    = 10;   // 一次爆发数量
        uint32_t cycles   = 1;    // 循环次数（-1 = 无限）
        float    interval = 0.1f; // 多次爆发的间隔（秒）

        // 运行时状态（不参与序列化）
        uint32_t cyclesDone = 0;
        float    nextTime   = -1.f;
    };

    struct VansParticleSpawnConfig
    {
        VansSpawnType       m_Type = VansSpawnType::RateOverTime;
        float               m_Rate = 30.f;               // 每秒发射数
        std::vector<BurstConfig> m_Bursts;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
    };

    // ============================================================
    // 渲染配置
    // ============================================================

    enum class VansParticleRendererType  { Billboard, StretchedBillboard, Mesh };
    enum class VansParticleBlendMode     { Alpha, Additive, Multiply };
    enum class VansParticleSortMode      { None, ByDistance, OldestFirst, NewestFirst };

    struct VansParticleRendererConfig
    {
        VansParticleRendererType  m_Type      = VansParticleRendererType::Billboard;
        std::string               m_Texture;
        VansParticleBlendMode     m_BlendMode = VansParticleBlendMode::Additive;

        // Sprite Sheet
        bool     m_SpriteSheetEnabled = false;
        int      m_SpriteColumns      = 4;
        int      m_SpriteRows         = 4;

        VansParticleSortMode      m_SortMode  = VansParticleSortMode::None;
        bool m_CastShadows    = false;
        bool m_ReceiveShadows = false;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
    };

    // ============================================================
    // VansParticleEmitter — 单个发射器
    // ============================================================
    class VansParticleEmitter
    {
    public:
        // ── 基本配置 ─────────────────────────────────────────────
        std::string m_Name;
        bool        m_Enabled      = true;
        uint32_t    m_MaxParticles = 1000;

        // ── Spawn ─────────────────────────────────────────────────
        VansParticleSpawnConfig   m_SpawnConfig;
        float                     m_SpawnAccum = 0.f;   // 发射余量累加

        // ── 模块 Stack ────────────────────────────────────────────
        std::vector<std::unique_ptr<VansParticleModule>> m_InitModules;
        std::vector<std::unique_ptr<VansParticleModule>> m_UpdateModules;

        // ── 渲染配置 ──────────────────────────────────────────────
        VansParticleRendererConfig m_RendererConfig;

        // ── 粒子池 ────────────────────────────────────────────────
        VansParticlePool           m_ParticlePool;

        // ── 接口 ─────────────────────────────────────────────────

        // 初始化粒子池（Resize）
        void Initialize();

        // 每帧更新：Spawn + Init + Update + Age
        void Update(float deltaTime, const glm::mat4& localToWorld);

        // 将存活粒子写入 GPU 实例数据缓冲
        void FillInstanceData(std::vector<VansParticleInstanceData>& outBuffer) const;

        // 序列化
        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);

    private:
        // 发射新粒子并执行 Init 模块
        void SpawnParticles(uint32_t count, const glm::mat4& localToWorld);

        // 从 JSON 模块名反序列化单个模块
        static std::unique_ptr<VansParticleModule> CreateModuleFromJson(
            const nlohmann::json& j);
    };

} // namespace VansGraphics
