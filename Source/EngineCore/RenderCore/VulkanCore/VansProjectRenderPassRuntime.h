#pragma once
#include "../VansProjectRenderPassState.h"
#include "VansFrameSubmitOrchestrator.h"
#include "VansVKImage.h"
#include <memory>
#include <mutex>

namespace VansGraphics
{
class VansScene;
class VansVKDevice;
struct VansProjectRenderTextureInfo
{
    std::string resourceKey, passId, targetName, exportChannel;
    std::uint64_t generation = 0;
    std::uint32_t width = 0, height = 0, frameSlots = 0;
    Vans::VansRenderTargetFormat format = Vans::VansRenderTargetFormat::RGBA8;
    std::uint32_t usage = 0;
    bool history = false, hasContents = false;
    std::uint32_t previousSlot = UINT32_MAX;
    std::uint32_t currentSlot = UINT32_MAX;
    std::optional<glm::vec4> worldRect;
};
struct VansProjectRenderPassFrameDiagnostic
{
    std::string passId;
    std::vector<std::string> shaders;
    VansProjectRenderPassFilterStats filter;
    bool enabled = false, submitted = false;
    std::uint32_t commandSlot = UINT32_MAX, targetSlot = UINT32_MAX;
    std::uint32_t batches = 0, instances = 0, imageBarriers = 0;
    std::uint64_t uploadedBytes = 0, payloadBytes = 0, shaderMask = 0;
};
struct VansProjectRenderPassRuntimeSnapshot
{
    std::vector<VansProjectRenderTextureInfo> targets;
    std::vector<VansProjectRenderPassFrameDiagnostic> passes;
    std::uint64_t revision = 0, generation = 0, allocatedBytes = 0;
    std::uint32_t passCount = 0, draws = 0;
    std::string error;
};
// RenderThread owns the generation. Main may read only the published metadata snapshot.
class VansProjectRenderPassRuntime final
{
public:
    VansProjectRenderPassRuntime();
    ~VansProjectRenderPassRuntime();
    // 在有序 RenderThread 维护事务中准备候选，成功后 Main 才发布对应配置。
    bool Prepare(VansVKDevice& device, VansScene& scene, const VansProjectRenderPassFrameInput& input,
        std::uint32_t frameSlots, VkExtent2D resolution, std::uint64_t sceneEpoch, std::string& error);
    bool Record(VansVKDevice& device, VansScene& scene, const VansProjectRenderPassFrameInput& input,
        VansVKCommandBuffer& command, std::uint32_t slot, std::uint32_t frameSlots,
        VkExtent2D resolution, std::uint64_t sceneEpoch, std::string& error);
    void ResolveSubmission(bool submitted);
    const std::vector<VansSubmitResourceAccess>& RecordedResources() const;
    bool HasRecordedWork() const { return !m_RecordedPasses.empty(); }
    VansProjectRenderPassRuntimeSnapshot CaptureSnapshot() const;
    VansVKImage* ResolveTarget(const std::string& key, std::uint64_t generation, std::uint32_t frameSlot, bool requireInitialized = true);
    // 只更新已退役的 Scene 帧槽；未产生内容的通道绑定中性纹理。
    bool BindOutputDescriptors(VkDevice device, VkDescriptorSet globalSet, VkDescriptorImageInfo neutral) const;
    std::vector<VansSubmitResourceAccess> ExportedReadResources() const;
    void Release(VansVKDevice& device);
    // 调用者已经等待设备空闲；关闭阶段必须早于 DescriptorPool 销毁。
    void ShutdownAtIdle();
    bool ValidateShaderCandidate(const std::string& name,
        const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stages, std::string& error) const;
private:
    struct Owner;
    std::shared_ptr<Owner> m_Owner;
    std::uint64_t m_Generation = 0;
    std::vector<std::size_t> m_RecordedPasses;
    std::vector<VansSubmitResourceAccess> m_RecordedResources;
    mutable std::mutex m_SnapshotMutex;
    VansProjectRenderPassRuntimeSnapshot m_Snapshot;
    void PublishSnapshot(const std::string& error = {});
};
}
