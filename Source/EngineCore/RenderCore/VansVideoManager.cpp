#include "VansVideoManager.h"
#include "../SceneCore/VansSceneResourcePlan.h"
#include "../Util/VansLog.h"
#include <filesystem>

namespace VansGraphics
{

void VansVideoManager::Load(const std::vector<Vans::VansSceneVideoResourceRequest>& videos,
                            const std::string& projectRoot,
                            VansVKDevice* device)
{
    if (!device)
    {
        VANS_LOG_ERROR("[VansVideoManager] Load failed: device is null");
        return;
    }

    for (const auto& entry : videos)
    {
        const std::string& name = entry.name;
        const std::string& relPath = entry.path;
        const bool loop = entry.loop;
        const bool autoplay = entry.autoplay;
        const bool srgb = entry.srgb;

        if (name.empty() || relPath.empty())
        {
            VANS_LOG_WARN("[VansVideoManager] name or path is empty, skipped");
            continue;
        }

        const std::string absPath = projectRoot + relPath;

        if (!std::filesystem::exists(absPath))
        {
            VANS_LOG_ERROR("[VansVideoManager] Video file not found: " << absPath);
            continue;
        }

        if (m_Videos.count(name) > 0)
        {
            VANS_LOG_WARN("[VansVideoManager] Duplicate video name, replacing: " << name);
            m_Videos[name]->Close();
            m_Videos.erase(name);
        }

        auto videoTex = std::make_unique<VansVideoTexture>();
        if (!videoTex->Open(device, absPath, loop, autoplay, srgb))
        {
            VANS_LOG_ERROR("[VansVideoManager] Failed to open video: " << absPath);
            continue;
        }

        VANS_LOG("[VansVideoManager] Loaded video texture: name=" << name << " path=" << absPath);
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
