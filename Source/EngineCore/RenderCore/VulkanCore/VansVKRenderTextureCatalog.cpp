#include "VansVKDevice.h"
#include "../../RuntimeCore/VansThreadContract.h"

namespace VansGraphics
{
void VansVKDevice::RefreshRuntimeRenderTextureCatalog()
{
    VANS_ASSERT_RENDER_THREAD();
    using Source = VansRuntimeRenderTextureCatalog::Source;
    using Slots = VansRenderTextureSlotBinding;
    std::vector<Source> sources;
    const auto project = m_ProjectRenderPassRuntime.CaptureSnapshot();
    for (const auto& target : project.targets)
    {
        std::vector<VansVKImage*> images;
        for (std::uint32_t i = 0; i < target.frameSlots; ++i) images.push_back(m_ProjectRenderPassRuntime.ResolveTarget(target.resourceKey,target.generation,i,false));
        Source source{target.resourceKey,target.passId+" / "+target.targetName,"project",target.passId,std::move(images),Slots::FrameContext};
        source.history = target.history; source.initialized = target.hasContents; source.ownerGeneration = target.generation;
        source.previousSlot = target.previousSlot;
        source.currentSlot = target.currentSlot;
        source.worldRect = target.worldRect;
        sources.push_back(std::move(source));
    }
    m_RenderTextureCatalog.Publish(std::move(sources),m_CurrentRenderSceneSnapshot.sceneEpoch);
    m_RenderTextureCatalogRequested.store(false,std::memory_order_release);
}
}
