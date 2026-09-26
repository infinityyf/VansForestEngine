#pragma once
#include "../AssetCore/VansAssetGuid.h"
#include "VansParticleData.h"
#include "VansParticleInstanceData.h"
#include "VansParticleSortPolicy.h"
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
    };

    struct BurstConfig
    {
        float    time     = 0.f;  // 在 systemTime 等于该值时触发
        uint32_t count    = 10;   // 一次爆发数量
        uint32_t cycles   = 1;    // 循环次数，0 为无限。
        float    interval = 0.1f; // 多次爆发的间隔（秒）

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

    enum class VansParticleRendererType  { None, Billboard, Ribbon };
    enum class VansParticleLightingMode  { UnlitFlipbook, SixWayLit };

    enum class VansRibbonRootMode { None, FollowSource };
    enum class VansRibbonStopAttachment { KeepUntilInvisible, DetachOnStop };
    struct VansParticleRibbonConfig
    {
        VansRibbonRootMode rootMode = VansRibbonRootMode::None;
        VansRibbonStopAttachment stopAttachment = VansRibbonStopAttachment::KeepUntilInvisible;
        float rootWidth = 0.008f;
        glm::vec4 rootColor{0.55f, 0.55f, 0.55f, 0.25f};
        float maxSegmentLength = 0.5f;
        float uvFlowSpeed = 1.0f;
        float softIntersection = 0.02f;
        float tipFadeDistance = 0.0f;
    };

    struct VansParticleSixWayLightingConfig
    {
        std::string m_PositiveAxesTextureGuid;
        std::string m_NegativeAxesTextureGuid;
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
        std::string               m_TextureGuid = "34a4e372-93c5-44a1-a985-768a099c3c95";

        // Sprite Sheet
        bool     m_SpriteSheetEnabled = false;
        int      m_SpriteColumns      = 4;
        int      m_SpriteRows         = 4;

        VansParticleSimulationOrder m_SimulationOrder = VansParticleSimulationOrder::Stable;
        VansParticleRenderSortMode m_RenderSortMode = VansParticleRenderSortMode::None;
        VansParticleLightingMode   m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
        VansParticleSixWayLightingConfig m_SixWayLighting;
        VansParticleVolumetricConfig m_Volumetric;
        VansParticleRibbonConfig m_Ribbon;

    };

    // 不可变资产定义。解码阶段不创建模拟池；共享定义内没有运行时状态。
    class VansParticleEmitter
    {
    public:
        std::string m_Name;
        bool m_Enabled = true;
        uint32_t m_MaxParticles = 1000;
        VansParticleSpawnConfig m_SpawnConfig;
        std::vector<std::unique_ptr<const VansParticleModule>> m_InitModules;
        std::vector<std::unique_ptr<const VansParticleModule>> m_UpdateModules;
        VansParticleRendererConfig m_RendererConfig;
    };
}
