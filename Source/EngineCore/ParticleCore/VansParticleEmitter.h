#pragma once
#include "VansParticleData.h"
#include "VansParticleInstanceData.h"
#include "Modules/VansInitModules.h"
#include "Modules/VansUpdateModules.h"
#include <glm/glm.hpp>
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

    };

    // ============================================================
    // 渲染配置
    // ============================================================

    enum class VansParticleRendererType  { Billboard, StretchedBillboard, Mesh };
    enum class VansParticleBlendMode     { Alpha, Additive, Multiply };
    enum class VansParticleSortMode      { None, ByDistance, OldestFirst, NewestFirst };
    enum class VansParticleLightingMode  { UnlitFlipbook, SixWayLit };

    struct VansParticleSixWayLightingConfig
    {
        bool        m_Enabled               = false;
        std::string m_PositiveAxesTexture;
        std::string m_NegativeAxesTexture;
        int         m_Columns               = 1;
        int         m_Rows                  = 1;
        float       m_FPS                   = 0.f;
        bool        m_AlphaFromPositiveA    = true;
        bool        m_EmissiveFromNegativeA = true;
        float       m_LightIntensity        = 1.f;
        float       m_AmbientIntensity      = 0.25f;
        float       m_EmissiveIntensity     = 1.f;
        float       m_AbsorptionStrength    = 0.f;
        float       m_LightmapRemapMin      = 0.f;
        float       m_LightmapRemapMax      = 1.f;

    };

    // 可选的 Froxel 介质注入。默认关闭，关闭时所有字段均不参与 Runtime/GPU。
    struct VansParticleVolumetricConfig
    {
        bool m_Enabled = false;
        bool m_KeepSurfaceRenderer = false;
        float m_RadiusScale = 1.0f;
        float m_MaxDistanceMeters = 100.0f;
        float m_DensityMultiplier = 1.0f;
        float m_ExtinctionPerMeter = 0.1f;
        glm::vec3 m_SingleScatteringAlbedo{ 0.9f };
        float m_Anisotropy = 0.0f;
        glm::vec3 m_EmissivePerMeter{ 0.0f };
        float m_EdgeSoftness = 0.35f;
        float m_DirectLightingScale = 1.0f;
        float m_SkyLightingScale = 1.0f;
        bool m_ReceiveCloudShadows = true;
        std::uint32_t m_InjectionPriority = 128u;
    };

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
        VansParticleLightingMode   m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
        VansParticleSixWayLightingConfig m_SixWayLighting;
        VansParticleVolumetricConfig m_Volumetric;
        bool m_CastShadows    = false;
        bool m_ReceiveShadows = false;

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
		void ResetSimulation();
		void SetRandomSeed(uint32_t seed);
		void EmitBurst(uint32_t count, const glm::mat4& localToWorld);

        // 每帧更新：Spawn + Init + Update + Age
        void Update(float deltaTime, const glm::mat4& localToWorld);

        // 将存活粒子写入 GPU 实例数据缓冲
        void FillInstanceData(std::vector<VansParticleInstanceData>& outBuffer) const;
        void FillVolumetricInstanceData(
            std::vector<VansVolumetricParticleInstanceData>& outBuffer) const;

    private:
        // 发射新粒子并执行 Init 模块
        void SpawnParticles(uint32_t count, const glm::mat4& localToWorld);
		uint32_t NextRandomSeed();

		uint32_t m_RandomSeed = 0x9e3779b9u;
		uint32_t m_RandomState = 0x9e3779b9u;
    };

} // namespace VansGraphics
