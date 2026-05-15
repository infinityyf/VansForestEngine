#include "VansParticleEmitter.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
    // ──────────────────────────────────────────────────────────────────────
    // VansParticleSpawnConfig 序列化
    // ──────────────────────────────────────────────────────────────────────

    nlohmann::json VansParticleSpawnConfig::Serialize() const
    {
        nlohmann::json j;
        switch (m_Type)
        {
        case VansSpawnType::RateOverTime:
            j["type"] = "RateOverTime";
            j["rate"] = m_Rate;
            break;
        case VansSpawnType::Burst:
        {
            j["type"] = "Burst";
            auto bursts = nlohmann::json::array();
            for (auto& b : m_Bursts)
            {
                bursts.push_back({
                    {"time",     b.time},
                    {"count",    b.count},
                    {"cycles",   b.cycles},
                    {"interval", b.interval}
                });
            }
            j["bursts"] = bursts;
            break;
        }
        default:
            j["type"] = "RateOverTime";
            j["rate"] = m_Rate;
            break;
        }
        return j;
    }

    void VansParticleSpawnConfig::Deserialize(const nlohmann::json& j)
    {
        std::string type = j.value("type", "RateOverTime");
        if (type == "Burst")
        {
            m_Type = VansSpawnType::Burst;
            m_Bursts.clear();
            if (j.contains("bursts"))
            {
                for (auto& b : j["bursts"])
                {
                    BurstConfig bc;
                    bc.time     = b.value("time",     0.f);
                    bc.count    = b.value("count",    10u);
                    bc.cycles   = b.value("cycles",   1u);
                    bc.interval = b.value("interval", 0.1f);
                    m_Bursts.push_back(bc);
                }
            }
        }
        else
        {
            m_Type = VansSpawnType::RateOverTime;
            m_Rate = j.value("rate", 30.f);
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansParticleRendererConfig 序列化
    // ──────────────────────────────────────────────────────────────────────

    nlohmann::json VansParticleRendererConfig::Serialize() const
    {
        auto typeToStr = [](VansParticleRendererType t) -> std::string {
            switch (t)
            {
            case VansParticleRendererType::StretchedBillboard: return "StretchedBillboard";
            case VansParticleRendererType::Mesh:               return "Mesh";
            default:                                            return "Billboard";
            }
        };
        auto blendToStr = [](VansParticleBlendMode b) -> std::string {
            switch (b)
            {
            case VansParticleBlendMode::Alpha:    return "Alpha";
            case VansParticleBlendMode::Multiply: return "Multiply";
            default:                               return "Additive";
            }
        };
        auto sortToStr = [](VansParticleSortMode s) -> std::string {
            switch (s)
            {
            case VansParticleSortMode::ByDistance:   return "ByDistance";
            case VansParticleSortMode::OldestFirst:  return "OldestFirst";
            case VansParticleSortMode::NewestFirst:  return "NewestFirst";
            default:                                  return "None";
            }
        };

        nlohmann::json j;
        j["type"]      = typeToStr(m_Type);
        j["texture"]   = m_Texture;
        j["blendMode"] = blendToStr(m_BlendMode);
        j["spriteSheet"] = {
            {"enabled", m_SpriteSheetEnabled},
            {"columns", m_SpriteColumns},
            {"rows",    m_SpriteRows}
        };
        j["sortMode"]      = sortToStr(m_SortMode);
        j["castShadows"]   = m_CastShadows;
        j["receiveShadows"] = m_ReceiveShadows;
        return j;
    }

    void VansParticleRendererConfig::Deserialize(const nlohmann::json& j)
    {
        std::string typeStr = j.value("type", "Billboard");
        if (typeStr == "StretchedBillboard")       m_Type = VansParticleRendererType::StretchedBillboard;
        else if (typeStr == "Mesh")                m_Type = VansParticleRendererType::Mesh;
        else                                       m_Type = VansParticleRendererType::Billboard;

        m_Texture = j.value("texture", "");

        std::string blend = j.value("blendMode", "Additive");
        if (blend == "Alpha")         m_BlendMode = VansParticleBlendMode::Alpha;
        else if (blend == "Multiply") m_BlendMode = VansParticleBlendMode::Multiply;
        else                          m_BlendMode = VansParticleBlendMode::Additive;

        if (j.contains("spriteSheet"))
        {
            auto& sp = j["spriteSheet"];
            m_SpriteSheetEnabled = sp.value("enabled", false);
            m_SpriteColumns      = sp.value("columns", 4);
            m_SpriteRows         = sp.value("rows",    4);
        }

        std::string sort = j.value("sortMode", "None");
        if (sort == "ByDistance")       m_SortMode = VansParticleSortMode::ByDistance;
        else if (sort == "OldestFirst") m_SortMode = VansParticleSortMode::OldestFirst;
        else if (sort == "NewestFirst") m_SortMode = VansParticleSortMode::NewestFirst;
        else                            m_SortMode = VansParticleSortMode::None;

        m_CastShadows    = j.value("castShadows",    false);
        m_ReceiveShadows = j.value("receiveShadows", false);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansParticleEmitter 实现
    // ──────────────────────────────────────────────────────────────────────

    void VansParticleEmitter::Initialize()
    {
        m_ParticlePool.Resize(m_MaxParticles);
    }

    void VansParticleEmitter::SpawnParticles(uint32_t count,
                                              const glm::mat4& localToWorld)
    {
        if (count == 0) return;

        uint32_t available = m_MaxParticles - m_ParticlePool.m_AliveCount;
        count = std::min(count, available);
        if (count == 0) return;

        uint32_t startIndex = m_ParticlePool.m_AliveCount;
        uint32_t endIndex   = startIndex + count;

        // 将新粒子标记为存活
        for (uint32_t i = startIndex; i < endIndex; ++i)
            m_ParticlePool.m_Flags[i] = VansParticlePool::FLAG_ALIVE;

        m_ParticlePool.m_AliveCount = endIndex;

        // 执行所有 Init 模块
        for (auto& mod : m_InitModules)
        {
            if (mod && mod->m_Enabled)
                mod->ExecuteInit(m_ParticlePool, startIndex, endIndex, localToWorld);
        }

        // 也对 Update 模块调用 ExecuteInit（角速度、帧索引初始化等）
        for (auto& mod : m_UpdateModules)
        {
            if (mod && mod->m_Enabled)
                mod->ExecuteInit(m_ParticlePool, startIndex, endIndex, localToWorld);
        }
    }

    void VansParticleEmitter::Update(float deltaTime, const glm::mat4& localToWorld)
    {
        if (!m_Enabled) return;

        // ── 1. 计算本帧发射数量 ────────────────────────────────────
        uint32_t spawnCount = 0;

        if (m_SpawnConfig.m_Type == VansSpawnType::RateOverTime)
        {
            m_SpawnAccum += m_SpawnConfig.m_Rate * deltaTime;
            spawnCount    = static_cast<uint32_t>(m_SpawnAccum);
            m_SpawnAccum -= static_cast<float>(spawnCount);
        }
        else if (m_SpawnConfig.m_Type == VansSpawnType::Burst)
        {
            // Burst 触发逻辑（此处简化：仅在 cyclesDone 为 0 时触发第一次）
            for (auto& burst : m_SpawnConfig.m_Bursts)
            {
                if (burst.cyclesDone < burst.cycles || burst.cycles == 0)
                {
                    if (burst.nextTime < 0.f) burst.nextTime = burst.time;
                    spawnCount += burst.count;
                    ++burst.cyclesDone;
                    burst.nextTime += burst.interval;
                }
            }
        }

        // ── 2. 发射新粒子 ─────────────────────────────────────────
        if (spawnCount > 0)
            SpawnParticles(spawnCount, localToWorld);

        // ── 3. 执行 Update 模块 ───────────────────────────────────
        for (auto& mod : m_UpdateModules)
        {
            if (mod && mod->m_Enabled)
                mod->Execute(m_ParticlePool, deltaTime, localToWorld);
        }

        // ── 4. 更新 Age / NormalizedAge，剔除死亡粒子 ────────────
        uint32_t i = 0;
        while (i < m_ParticlePool.m_AliveCount)
        {
            m_ParticlePool.m_Age[i] += deltaTime;
            float lifeTime = m_ParticlePool.m_LifeTime[i];
            if (lifeTime > 0.f)
                m_ParticlePool.m_NormalizedAge[i] = m_ParticlePool.m_Age[i] / lifeTime;

            if (m_ParticlePool.m_Age[i] >= lifeTime)
            {
                // 粒子死亡：用最后一个粒子填充此槽
                m_ParticlePool.SwapRemoveAt(i);
                // 不递增 i：新的第 i 个粒子也要检查
            }
            else
            {
                ++i;
            }
        }
    }

    void VansParticleEmitter::FillInstanceData(
        std::vector<VansParticleInstanceData>& outBuffer) const
    {
        for (uint32_t i = 0; i < m_ParticlePool.m_AliveCount; ++i)
        {
            VansParticleInstanceData inst;
            inst.m_WorldPosition = m_ParticlePool.m_Position[i];
            inst.m_Size          = m_ParticlePool.m_Size[i];
            inst.m_Color         = m_ParticlePool.m_Color[i];
            inst.m_Rotation      = glm::radians(m_ParticlePool.m_Rotation[i]);
            inst.m_FrameIndex    = m_ParticlePool.m_FrameIndex.empty()
                                    ? 0.f
                                    : m_ParticlePool.m_FrameIndex[i];
            inst.m_Padding       = glm::vec2(0.f);
            outBuffer.push_back(inst);
        }
    }

    // ── 从 JSON 名称创建模块 ───────────────────────────────────────────────

    std::unique_ptr<VansParticleModule> VansParticleEmitter::CreateModuleFromJson(
        const nlohmann::json& j)
    {
        std::string name = j.value("module", "");
        std::unique_ptr<VansParticleModule> mod;

        // Init 模块
        if      (name == "InitLifetime")      mod = std::make_unique<VansInitLifetimeModule>();
        else if (name == "InitVelocity")      mod = std::make_unique<VansInitVelocityModule>();
        else if (name == "InitSize")          mod = std::make_unique<VansInitSizeModule>();
        else if (name == "InitColor")         mod = std::make_unique<VansInitColorModule>();
        else if (name == "InitRotation")      mod = std::make_unique<VansInitRotationModule>();
        else if (name == "InitPositionShape") mod = std::make_unique<VansInitPositionModule>();
        // Update 模块
        else if (name == "UpdateGravity")             mod = std::make_unique<VansUpdateGravityModule>();
        else if (name == "UpdateColorOverLifetime")   mod = std::make_unique<VansUpdateColorOverLifetime>();
        else if (name == "UpdateSizeOverLifetime")    mod = std::make_unique<VansUpdateSizeOverLifetime>();
        else if (name == "UpdateVelocityOverLifetime") mod = std::make_unique<VansUpdateVelocityOverLifetime>();
        else if (name == "UpdateRotationOverLifetime") mod = std::make_unique<VansUpdateRotationOverLifetime>();
        else if (name == "UpdateSpriteAnim")          mod = std::make_unique<VansUpdateSpriteAnimModule>();

        if (mod)
            mod->Deserialize(j);

        return mod;
    }

    // ── Serialize / Deserialize ────────────────────────────────────────────

    nlohmann::json VansParticleEmitter::Serialize() const
    {
        nlohmann::json j;
        j["name"]         = m_Name;
        j["enabled"]      = m_Enabled;
        j["maxParticles"] = m_MaxParticles;
        j["spawn"]        = m_SpawnConfig.Serialize();

        auto initArr = nlohmann::json::array();
        for (auto& mod : m_InitModules)
            if (mod) initArr.push_back(mod->Serialize());
        j["initialize"] = initArr;

        auto updateArr = nlohmann::json::array();
        for (auto& mod : m_UpdateModules)
            if (mod) updateArr.push_back(mod->Serialize());
        j["update"] = updateArr;

        j["renderer"] = m_RendererConfig.Serialize();
        return j;
    }

    void VansParticleEmitter::Deserialize(const nlohmann::json& j)
    {
        m_Name         = j.value("name",         "Emitter");
        m_Enabled      = j.value("enabled",      true);
        m_MaxParticles = j.value("maxParticles",  1000u);

        if (j.contains("spawn"))
            m_SpawnConfig.Deserialize(j["spawn"]);

        m_InitModules.clear();
        if (j.contains("initialize") && j["initialize"].is_array())
        {
            for (auto& modJson : j["initialize"])
            {
                auto mod = CreateModuleFromJson(modJson);
                if (mod) m_InitModules.push_back(std::move(mod));
            }
        }

        m_UpdateModules.clear();
        if (j.contains("update") && j["update"].is_array())
        {
            for (auto& modJson : j["update"])
            {
                auto mod = CreateModuleFromJson(modJson);
                if (mod) m_UpdateModules.push_back(std::move(mod));
            }
        }

        if (j.contains("renderer"))
            m_RendererConfig.Deserialize(j["renderer"]);

        // 初始化粒子池
        Initialize();
    }

} // namespace VansGraphics
