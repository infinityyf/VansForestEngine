#include "VansRuntimeRenderTextureCatalog.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../Util/VansWorldTextureWindow.h"
#include <algorithm>
#include <unordered_set>

namespace VansGraphics
{
bool VansRuntimeRenderTextureCatalog::RegisterSource(Source source,std::string& error)
{
    VANS_ASSERT_RENDER_THREAD();
    if (source.key.rfind("external:",0)!=0 || source.key.size()<=9 || source.key.size()>512 || source.name.empty() ||
        source.owner.empty() || !source.ownerGeneration || source.images.empty() || source.images.size()>2 ||
        static_cast<unsigned>(source.slots)>static_cast<unsigned>(VansRenderTextureSlotBinding::GBuffer) ||
        (source.slots==VansRenderTextureSlotBinding::Single && source.images.size()!=1) ||
        (source.currentSlot!=UINT32_MAX && source.currentSlot>=source.images.size()) ||
        (source.previousSlot!=UINT32_MAX && source.previousSlot>=source.images.size()) ||
        (source.history && source.images.size()<2) ||
        (!source.history && source.previousSlot!=UINT32_MAX) ||
        (source.history && source.currentSlot!=UINT32_MAX && source.currentSlot==source.previousSlot) ||
        source.key.find('\0')!=std::string::npos)
    { error="External preview source requires a unique external: key, owner generation and valid frame slots"; return false; }
    if (m_RegisteredSources.count(source.key)) {error="Preview source key already registered; unregister the old generation first";return false;}
    if (source.worldRect)
    {
        Vans::VansWorldTextureWindow window;window.worldRect=*source.worldRect;
        glm::vec4 rectangle;std::array<std::int64_t,2> origin;
        if (!Vans::ResolveWorldTextureWindow(window,{},rectangle,origin))
        {error="Runtime texture world rectangle must be finite and positive";return false;}
    }
    for (auto* image:source.images) if (!image || !image->GetImage()) {error="Preview source has no allocated image";return false;}
    const auto& first=source.images.front()->GetImageCreateInfo();
    std::unordered_set<VkImage> identities;
    for (auto* image:source.images)
    {
        const auto& value=image->GetImageCreateInfo();
        if (!identities.insert(image->GetImage()).second || value.format!=first.format || value.usage!=first.usage ||
            value.imageType!=first.imageType || value.extent.width!=first.extent.width || value.extent.height!=first.extent.height ||
            value.extent.depth!=first.extent.depth || value.arrayLayers!=first.arrayLayers || value.mipLevels!=first.mipLevels || value.samples!=first.samples)
        {error="Preview physical slots must be distinct images with the same format, extent, layers, mips, samples and usage";return false;}
    }
    const auto key=source.key; m_RegisteredSources.emplace(key,std::move(source)); error.clear(); return true;
}
void VansRuntimeRenderTextureCatalog::UnregisterSource(const std::string& key)
{
    VANS_ASSERT_RENDER_THREAD();
    m_RegisteredSources.erase(key); m_Entries.erase(key);
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Snapshot.erase(std::remove_if(m_Snapshot.begin(),m_Snapshot.end(),[&](const auto& info){return info.key==key;}),m_Snapshot.end());
}
void VansRuntimeRenderTextureCatalog::Publish(std::vector<Source> sources, std::uint64_t epoch)
{
    VANS_ASSERT_RENDER_THREAD();
    for (const auto& [key,source]:m_RegisteredSources) sources.push_back(source);
    std::unordered_set<std::string> seen;
    std::vector<VansRuntimeRenderTextureInfo> snapshot;
    for (auto& source : sources)
    {
        if (source.key.empty() || !seen.insert(source.key).second) continue;
        auto& entry = m_Entries[source.key];
        std::vector<VkImage> identities;
        for (auto* image : source.images) identities.push_back(image ? image->GetImage() : VK_NULL_HANDLE);
        const bool changed = entry.identities != identities || entry.epoch != epoch || entry.source.ownerGeneration != source.ownerGeneration;
        const auto generation = changed || !entry.info.generation ? ++m_Generation : entry.info.generation;
        entry.source = std::move(source); entry.identities = std::move(identities); entry.epoch = epoch;
        entry.info = {};
        auto& info = entry.info; const auto& value = entry.source;
        info.key = value.key; info.name = value.name; info.category = value.category; info.owner = value.owner;
        info.generation = generation; info.frameSlots = static_cast<std::uint32_t>(value.images.size()); info.history = value.history;
        info.previousAvailable = value.history && value.initialized && value.previousSlot < value.images.size() &&
            value.images[value.previousSlot] && value.images[value.previousSlot]->GetImageLayout()!=VK_IMAGE_LAYOUT_UNDEFINED;
        for (auto* image : value.images) if (image && image->GetImage())
        {
            const auto& create = image->GetImageCreateInfo();
            info.width = create.extent.width; info.height = create.extent.height;
            info.layers = create.arrayLayers; info.mips = create.mipLevels; info.samples = create.samples;
            info.format = create.format; info.usage = create.usage;
            info.initialized = value.initialized && image->GetImageLayout() != VK_IMAGE_LAYOUT_UNDEFINED;
            if (info.initialized) break;
        }
        info.status = !info.width ? "notCreated" : !info.initialized ? "notProduced" :
            !(info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) ? "usageDisallowsPreview" : "available";
        snapshot.push_back(info);
    }
    for (auto it = m_Entries.begin(); it != m_Entries.end();) if (!seen.count(it->first)) it = m_Entries.erase(it); else ++it;
    std::sort(snapshot.begin(),snapshot.end(),[](const auto& a,const auto& b) { return a.key < b.key; });
    std::lock_guard<std::mutex> lock(m_Mutex); m_Snapshot = std::move(snapshot);
}
std::vector<VansRuntimeRenderTextureInfo> VansRuntimeRenderTextureCatalog::CaptureSnapshot() const
{ std::lock_guard<std::mutex> lock(m_Mutex); return m_Snapshot; }
VansVKImage* VansRuntimeRenderTextureCatalog::Resolve(const std::string& key,std::uint64_t generation,std::uint32_t frameSlot,std::uint32_t gbufferSlot,bool previousFrame)
{
    VANS_ASSERT_RENDER_THREAD();
    const auto found = m_Entries.find(key);
    if (found == m_Entries.end() || found->second.info.generation != generation || !found->second.info.initialized) return nullptr;
    const auto& source = found->second.source;
    if (previousFrame && !found->second.info.previousAvailable) return nullptr;
    const auto slot = previousFrame ? source.previousSlot : source.currentSlot != UINT32_MAX ? source.currentSlot :
        source.slots == VansRenderTextureSlotBinding::Single ? 0u : source.slots == VansRenderTextureSlotBinding::GBuffer ? gbufferSlot : frameSlot;
    if (slot >= source.images.size()) return nullptr;
    auto* image = source.images[slot];
    return image && image->GetImage() && image->GetImageLayout() != VK_IMAGE_LAYOUT_UNDEFINED ? image : nullptr;
}
VansResolvedRenderTexture VansRuntimeRenderTextureCatalog::ResolveSampled(const std::string& key,
    std::uint32_t frameSlot, std::uint32_t gbufferSlot)
{
    VANS_ASSERT_RENDER_THREAD();
    const auto found=m_Entries.find(key);
    if (found==m_Entries.end()) return {};
    const auto& entry=found->second;
    auto* image=Resolve(key,entry.info.generation,frameSlot,gbufferSlot);
    if (!image) return {};
    const auto& create=image->GetImageCreateInfo();
    if (!(create.usage&VK_IMAGE_USAGE_SAMPLED_BIT) || create.samples!=VK_SAMPLE_COUNT_1_BIT ||
        create.imageType!=VK_IMAGE_TYPE_2D || create.arrayLayers!=1 ||
        image->GetImageAspect()!=VK_IMAGE_ASPECT_COLOR_BIT || !image->GetSampler() ||
        (image->GetImageLayout()!=VK_IMAGE_LAYOUT_GENERAL && image->GetImageLayout()!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) return {};
    const auto& source=entry.source;
    const auto slot=source.currentSlot!=UINT32_MAX?source.currentSlot:
        source.slots==VansRenderTextureSlotBinding::Single?0u:
        source.slots==VansRenderTextureSlotBinding::GBuffer?gbufferSlot:frameSlot;
    return {image,key+"/slot"+std::to_string(slot),source.worldRect};
}
void VansRuntimeRenderTextureCatalog::Clear()
{
    m_Entries.clear(); m_RegisteredSources.clear(); std::lock_guard<std::mutex> lock(m_Mutex); m_Snapshot.clear();
}
}
