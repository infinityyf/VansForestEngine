#include "VansAudioManager.h"
#include "VansAudioDecoder.h"
#include "VansAudioSystem.h"
#include "../Util/VansLog.h"

namespace VansEngine
{

// ===========================================================================
// 析构函数（定义在此处使 unique_ptr<VansAudioDecoder> 析构时类型完整）
// ===========================================================================
VansAudioManager::~VansAudioManager() = default;

// ===========================================================================
// LoadFromJson — 解析 resource.json["audio"] 数组，创建并 Open 每个 VansAudioNode
// ===========================================================================
void VansAudioManager::LoadFromJson(const json& audioArray, const std::string& assetPrefix)
{
    if (!audioArray.is_array())
    {
        VANS_LOG_WARN("[VansAudioManager] LoadFromJson: audioArray 不是数组，跳过");
        return;
    }

    for (const auto& entry : audioArray)
    {
        if (!entry.contains("name") || !entry.contains("path"))
        {
            VANS_LOG_WARN("[VansAudioManager] LoadFromJson: 条目缺少 name 或 path 字段，跳过");
            continue;
        }

        std::string name = entry["name"].get<std::string>();
        std::string rel  = entry["path"].get<std::string>();

        if (name.empty() || rel.empty())
        {
            VANS_LOG_WARN("[VansAudioManager] LoadFromJson: name 或 path 为空，跳过");
            continue;
        }

        if (m_Nodes.count(name))
        {
            VANS_LOG_WARN("[VansAudioManager] LoadFromJson: 重复的音频名 '" << name << "'，跳过");
            continue;
        }

        // 拼接绝对路径（与 mesh / texture / video 加载策略一致）
        std::string fullPath = assetPrefix + "/" + rel;

        // 构建 AudioNodeProperties
        AudioNodeProperties props;
        props.m_Name     = name;
        props.m_FilePath = fullPath;

        // play_mode: "static" (default) | "streaming"
        if (entry.contains("play_mode") && entry["play_mode"].is_string())
        {
            std::string pm = entry["play_mode"].get<std::string>();
            props.m_PlayMode = (pm == "streaming") ? AudioPlayMode::Streaming : AudioPlayMode::Static;
        }

        if (entry.contains("loop")      && entry["loop"].is_boolean())
            props.m_Loop     = entry["loop"].get<bool>();
        if (entry.contains("auto_play") && entry["auto_play"].is_boolean())
            props.m_AutoPlay = entry["auto_play"].get<bool>();
        if (entry.contains("volume")    && entry["volume"].is_number())
            props.m_Volume   = entry["volume"].get<float>();
        if (entry.contains("pitch")     && entry["pitch"].is_number())
            props.m_Pitch    = entry["pitch"].get<float>();
        if (entry.contains("spatial")   && entry["spatial"].is_boolean())
            props.m_Spatial  = entry["spatial"].get<bool>();
        if (entry.contains("ref_dist")  && entry["ref_dist"].is_number())
            props.m_RefDist  = entry["ref_dist"].get<float>();
        if (entry.contains("max_dist")  && entry["max_dist"].is_number())
            props.m_MaxDist  = entry["max_dist"].get<float>();
        if (entry.contains("roll_off")  && entry["roll_off"].is_number())
            props.m_RollOff  = entry["roll_off"].get<float>();

        auto node = std::make_unique<VansAudioNode>();
        if (!node->Open(props))
        {
            VANS_LOG_ERROR("[VansAudioManager] VansAudioNode::Open 失败: " << fullPath);
            continue;
        }

        VANS_LOG("[VansAudioManager] 加载音频: " << name << " <- " << rel);
        m_Nodes[name] = std::move(node);
    }
}

// ===========================================================================
// ApplySceneConfig — 解析 scene.json["audio_sources"] 数组，应用运行时配置覆盖
// ===========================================================================
void VansAudioManager::ApplySceneConfig(const json& audioSourcesArray)
{
    if (!audioSourcesArray.is_array()) return;

    for (const auto& entry : audioSourcesArray)
    {
        if (!entry.contains("name") || !entry["name"].is_string()) continue;

        std::string name = entry["name"].get<std::string>();
        VansAudioNode* node = Get(name);
        if (!node)
        {
            VANS_LOG_WARN("[VansAudioManager] ApplySceneConfig: 找不到音频节点 '" << name << "'");
            continue;
        }

        // 应用可选覆盖字段
        if (entry.contains("volume")    && entry["volume"].is_number())
            node->SetVolume(entry["volume"].get<float>());
        if (entry.contains("pitch")     && entry["pitch"].is_number())
            node->SetPitch(entry["pitch"].get<float>());
        if (entry.contains("loop")      && entry["loop"].is_boolean())
            node->SetLoop(entry["loop"].get<bool>());
        if (entry.contains("spatial")   && entry["spatial"].is_boolean())
            node->SetSpatial(entry["spatial"].get<bool>());
        if (entry.contains("ref_dist")  && entry["ref_dist"].is_number())
            node->SetRefDistance(entry["ref_dist"].get<float>());
        if (entry.contains("max_dist")  && entry["max_dist"].is_number())
            node->SetMaxDistance(entry["max_dist"].get<float>());

        // 场景级 auto_play 覆盖
        if (entry.contains("auto_play") && entry["auto_play"].get<bool>())
            node->Play();
    }
}

// ===========================================================================
// Get — 按名称查找节点
// ===========================================================================
VansAudioNode* VansAudioManager::Get(const std::string& name) const
{
    auto it = m_Nodes.find(name);
    return (it != m_Nodes.end()) ? it->second.get() : nullptr;
}

// ===========================================================================
// TickAll — 更新 Listener 位置，驱动所有 Streaming 节点的 Buffer 补充
// ===========================================================================
void VansAudioManager::TickAll(double /*deltaTime*/,
                                float camPosX, float camPosY, float camPosZ,
                                float camFwdX, float camFwdY, float camFwdZ,
                                float camUpX,  float camUpY,  float camUpZ)
{
    // 同步 OpenAL Listener 到当前摄像机变换
    VansAudioSystem::GetInstance().UpdateListener(
        camPosX, camPosY, camPosZ,
        camFwdX, camFwdY, camFwdZ,
        camUpX,  camUpY,  camUpZ);

    // 逐节点 Tick（Streaming 节点补充 Buffer；Static 节点此调用为空操作）
    for (auto& [name, node] : m_Nodes)
    {
        if (node) node->Tick();
    }
}

// ===========================================================================
// StopAll — 场景卸载时停止所有节点播放（不释放资源）
// ===========================================================================
void VansAudioManager::StopAll()
{
    for (auto& [name, node] : m_Nodes)
    {
        if (node) node->Stop();
    }
}

// ===========================================================================
// Clear — 项目卸载时释放全部音频资源
// ===========================================================================
void VansAudioManager::Clear()
{
    for (auto& [name, node] : m_Nodes)
    {
        if (node) node->Close();
    }
    m_Nodes.clear();
}

} // namespace VansEngine
