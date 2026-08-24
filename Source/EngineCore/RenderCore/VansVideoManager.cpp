#include "VansVideoManager.h"
#include "../SceneCore/VansSceneResourcePlan.h"
#include "../Util/VansLog.h"
#include <algorithm>
#include <filesystem>
#include <iterator>

namespace VansGraphics
{

void VansVideoManager::Load(const std::vector<Vans::VansSceneVideoResourceRequest>& videos,
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

        const std::string absPath = std::filesystem::path(relPath).lexically_normal().string();

        if (!std::filesystem::exists(absPath))
        {
            VANS_LOG_ERROR("[VansVideoManager] Video file not found: " << absPath);
            continue;
        }

        if (m_Videos.count(name) > 0)
        {
            VANS_LOG_WARN("[VansVideoManager] Duplicate video name, replacing: " << name);
            VansVideoTexture* replaced = m_Videos[name].get();
			auto runtimeIt = std::find(
				m_RuntimeVideos.begin(), m_RuntimeVideos.end(), replaced);
            for (auto it = m_VideosByAssetGuid.begin(); it != m_VideosByAssetGuid.end();)
                it = it->second == replaced ? m_VideosByAssetGuid.erase(it) : std::next(it);
            m_Videos[name]->Close();
            m_Videos.erase(name);
			if (runtimeIt != m_RuntimeVideos.end())
				*runtimeIt = nullptr;
        }

        auto videoTex = std::make_unique<VansVideoTexture>();
        if (!videoTex->Open(device, absPath, loop, autoplay, srgb))
        {
            VANS_LOG_ERROR("[VansVideoManager] Failed to open video: " << absPath);
            continue;
        }

        VANS_LOG("[VansVideoManager] Loaded video texture: name=" << name << " path=" << absPath);
        VansVideoTexture* loaded = videoTex.get();
        m_Videos.emplace(name, std::move(videoTex));
		auto emptyRuntimeIt = std::find(
			m_RuntimeVideos.begin(), m_RuntimeVideos.end(), nullptr);
		if (emptyRuntimeIt == m_RuntimeVideos.end())
			m_RuntimeVideos.push_back(loaded);
		else
			*emptyRuntimeIt = loaded;
        if (!entry.assetGuid.empty())
            m_VideosByAssetGuid[entry.assetGuid] = loaded;
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

VansVideoTexture* VansVideoManager::GetByAssetGuid(const std::string& assetGuid) const
{
    const auto it = m_VideosByAssetGuid.find(assetGuid);
    return it == m_VideosByAssetGuid.end() ? nullptr : it->second;
}

std::uint32_t VansVideoManager::FindRuntimeIndex(const VansVideoTexture* video) const
{
	const auto it = std::find(m_RuntimeVideos.begin(), m_RuntimeVideos.end(), video);
	return it == m_RuntimeVideos.end()
		? InvalidRuntimeIndex
		: static_cast<std::uint32_t>(std::distance(m_RuntimeVideos.begin(), it));
}

VansVideoTexture* VansVideoManager::GetByRuntimeIndex(std::uint32_t index) const
{
	return index < m_RuntimeVideos.size() ? m_RuntimeVideos[index] : nullptr;
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
    m_VideosByAssetGuid.clear();
	m_RuntimeVideos.clear();
    m_Videos.clear();
    VANS_LOG("[VansVideoManager] 所有视频纹理已清理");
}

} // namespace VansGraphics
