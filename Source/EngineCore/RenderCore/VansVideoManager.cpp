#include "VansVideoManager.h"
#include "../Util/VansLog.h"
#include <filesystem>

namespace VansGraphics
{

// ===========================================================================
// LoadFromJson — 解析 JSON video_textures 数组并批量打开视频
// ===========================================================================
void VansVideoManager::LoadFromJson(const json& videoArray,
                                    const std::string& projectRoot,
                                    VansVKDevice* device)
{
    if (!device)
    {
        VANS_LOG_ERROR("[VansVideoManager] LoadFromJson 失败：device 为空");
        return;
    }

    for (const auto& entry : videoArray)
    {
        if (!entry.contains("name") || !entry.contains("path"))
        {
            VANS_LOG_WARN("[VansVideoManager] video_textures 条目缺少 name 或 path 字段，已跳过");
            continue;
        }

        const std::string name     = entry["name"];
        const std::string relPath  = entry["path"];
        const bool        loop     = entry.value("loop",     true);
        const bool        autoplay = entry.value("autoplay", true);
        const bool        srgb     = entry.value("srgb",     true);

        if (name.empty() || relPath.empty())
        {
            VANS_LOG_WARN("[VansVideoManager] name 或 path 为空，已跳过");
            continue;
        }

        // 拼接绝对路径
        const std::string absPath = projectRoot + relPath;

        if (!std::filesystem::exists(absPath))
        {
            VANS_LOG_ERROR("[VansVideoManager] 视频文件不存在: " << absPath);
            continue;
        }

        // 检查是否重名
        if (m_Videos.count(name) > 0)
        {
            VANS_LOG_WARN("[VansVideoManager] 视频名称重复，将覆盖: " << name);
            m_Videos[name]->Close();
        }

        auto videoTex = std::make_unique<VansVideoTexture>();
        if (!videoTex->Open(device, absPath, loop, autoplay, srgb))
        {
            VANS_LOG_ERROR("[VansVideoManager] 打开视频失败: " << absPath);
            continue;
        }

        VANS_LOG("[VansVideoManager] 加载视频纹理: name=" << name << " path=" << absPath);
        m_Videos.emplace(name, std::move(videoTex));
    }
}

// ===========================================================================
// Get — 按名称查找视频纹理
// ===========================================================================
VansVideoTexture* VansVideoManager::Get(const std::string& name) const
{
    auto it = m_Videos.find(name);
    if (it != m_Videos.end())
        return it->second.get();
    return nullptr;
}

// ===========================================================================
// TickAll — 逐帧推进所有处于播放状态的视频
// ===========================================================================
void VansVideoManager::TickAll(double deltaTime)
{
    for (auto& [name, videoTex] : m_Videos)
    {
        if (videoTex && videoTex->IsReady())
            videoTex->Tick(deltaTime);
    }
}

// ===========================================================================
// RecordPendingUploads — 录制所有视频的待上传帧，不做独立提交或 fence 等待
// ===========================================================================
void VansVideoManager::RecordPendingUploads(VansVKCommandBuffer& cmd)
{
    for (auto& [name, videoTex] : m_Videos)
    {
        if (videoTex && videoTex->IsReady())
            videoTex->RecordPendingUpload(cmd);
    }
}

// ===========================================================================
// PauseAll — 暂停所有视频播放（场景切换时调用，保留 GPU 纹理资源）
// 视频 GPU 纹理仍然有效，下次场景加载时可直接重新绑定到材质。
// ===========================================================================
void VansVideoManager::PauseAll()
{
    for (auto& [name, videoTex] : m_Videos)
    {
        if (videoTex)
            videoTex->Pause();
    }
    VANS_LOG("[VansVideoManager] 所有视频已暂停（GPU 纹理保留）");
}

// ===========================================================================
// PlayAll — 恢复所有视频播放（场景加载完成后调用）
// ===========================================================================
void VansVideoManager::PlayAll()
{
    for (auto& [name, videoTex] : m_Videos)
    {
        if (videoTex)
            videoTex->Play();
    }
    VANS_LOG("[VansVideoManager] 所有视频已恢复播放");
}

// ===========================================================================
// Clear — 停止所有线程，释放全部 FFmpeg 和 GPU 资源（项目卸载时调用）
// 注意：不要在 UnLoadScene() 中调用——视频是项目级资源（与 mesh/texture 相同），
// 应随项目生命周期管理，而非场景生命周期。
// ===========================================================================
void VansVideoManager::Clear()
{
    // unique_ptr 析构时会自动调用 ~VansVideoTexture() → Close()，
    // 无需在此处显式调用 Close()，避免双重关闭。
    m_Videos.clear();
    VANS_LOG("[VansVideoManager] 所有视频纹理已清理");
}

} // namespace VansGraphics
