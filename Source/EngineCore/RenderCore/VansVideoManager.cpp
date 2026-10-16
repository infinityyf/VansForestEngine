#include "VansVideoManager.h"
#include "VansRenderRuntimeConfig.h"
#include "VansRenderSceneSnapshot.h"
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
const std::vector<VansVideoFrameUpload>& VansVideoManager::RecordPendingUploads(
    VansVKCommandBuffer& cmd, uint32_t frameSlot, VansTexture* rectArray,
    const std::vector<VansRenderRectLightVideoFrameData>& bindings)
{
    if (frameSlot >= VansCommandRecordingConfig::MaximumFramesInFlight)
        throw std::invalid_argument("Video upload frame slot exceeds the engine ring");
    // 先校验整个有界目标集合，再录制；同一视频一次发布到所有目标层。
    uint32_t assignedLayers = 0;
    for (const auto& binding : bindings)
    {
        if (!GetByRuntimeIndex(binding.videoRuntimeIndex) || binding.rectLightLayer < 0 ||
            binding.rectLightLayer >= VANS_MAX_RECT_LIGHTS)
            throw std::invalid_argument("Invalid rect-light video binding");
        const uint32_t bit = 1u << binding.rectLightLayer;
        if (assignedLayers & bit)
            throw std::invalid_argument("Multiple video bindings write one rect-light layer");
        assignedLayers |= bit;
    }
    m_RecordedFrameUploads.clear();
    m_RecordedFrameUploads.reserve(m_RuntimeVideos.size());
    for (uint32_t i = 0; i < m_RuntimeVideos.size(); ++i)
    {
        auto* video = m_RuntimeVideos[i];
        if (!video || !video->IsReady()) continue;
        uint32_t layers = 0;
        for (const auto& binding : bindings)
            if (binding.videoRuntimeIndex == i) layers |= 1u << binding.rectLightLayer;
        const auto result = video->RecordPendingUploads(cmd, frameSlot, rectArray, layers);
        if (result.recorded) m_RecordedFrameUploads.push_back({ i, result });
    }
    return m_RecordedFrameUploads;
}

void VansVideoManager::AppendFrameResources(VansFrameSubmitNode& node, VansTexture* rectArray,
    VkPipelineStageFlags stages, bool includeUploads) const
{
    const auto append = [&](const std::string& name, VkImage image, bool upload)
    {
        if (!image) return;
        const bool write = includeUploads && upload;
        node.resources.push_back({ name, stages | (write ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0u),
            VK_ACCESS_SHADER_READ_BIT | (write ? VK_ACCESS_TRANSFER_WRITE_BIT : 0u),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, write, true, false,
            VansPhysicalResourceIdentity::ForImage(image) });
    };
    uint32_t writtenLayers = 0;
    for (const auto& upload : m_RecordedFrameUploads) writtenLayers |= upload.result.arrayLayers;
    size_t uploadIndex = 0;
    for (uint32_t i = 0; i < m_RuntimeVideos.size(); ++i)
    {
        auto* video = m_RuntimeVideos[i];
        if (!video || !video->IsReady()) continue;
        const bool written = uploadIndex < m_RecordedFrameUploads.size() &&
            m_RecordedFrameUploads[uploadIndex].videoRuntimeIndex == i;
        if (written) ++uploadIndex;
        append("VideoTexture" + std::to_string(i), video->GetTexture()->GetImage().GetImage(), written);
    }
    if (rectArray) append("RectLightVideoArray", rectArray->GetImage().GetImage(), writtenLayers != 0);
}

void VansVideoManager::ResolveRecordedUploads(bool submitted)
{
    // 包括中途录制失败、尚未来得及加入结果表的视频。
    for (auto* video : m_RuntimeVideos)
        if (video) video->ResolveRecordedUploads(submitted);
    m_RecordedFrameUploads.clear();
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
// 注意：不要在 UnloadScene() 中调用——视频是项目级资源（与 mesh/texture 相同），
// 应随项目生命周期管理，而非场景生命周期。
// ===========================================================================
void VansVideoManager::Clear()
{
    // unique_ptr 析构时会自动调用 ~VansVideoTexture() → Close()，
    // 无需在此处显式调用 Close()，避免双重关闭。
    m_RecordedFrameUploads.clear();
    m_VideosByAssetGuid.clear();
	m_RuntimeVideos.clear();
    m_Videos.clear();
    VANS_LOG("[VansVideoManager] 所有视频纹理已清理");
}

} // namespace VansGraphics
