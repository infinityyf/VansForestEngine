#pragma once
#include "VansParticleSurfaceRenderer.h"
#include "../GeometryCore/VansPolylineMeshBuilder.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace VansGraphics
{
class VansVKDevice;
struct VansRenderViewSnapshot;
struct VansRenderSceneFrameSnapshot;
struct VansParticleDrawItem
{
    const VansParticleSurfaceRenderer* material = nullptr;
    glm::vec3 center{0};
    VkDeviceSize vertexOffset = 0;
    VkDeviceSize indexOffset = 0;
    VkDeviceSize instanceOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    bool ribbon = false;
};
struct VansParticleRenderDiagnostics
{
    std::uint64_t uploadBytes = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint32_t drawCount = 0;
    std::uint32_t droppedDraws = 0;
    std::uint32_t rejectedRibbonPoints = 0;
    std::uint32_t splitRibbonRuns = 0;
    double prepareMilliseconds = 0;
};
class VansParticleRenderSystem final
{
public:
    ~VansParticleRenderSystem();
    void Prepare(VansVKDevice& device, VansScene& scene,
        const VansRenderViewSnapshot& view, const VansRenderSceneFrameSnapshot& snapshot);
    void Shutdown(); // 调用方先等待 GPU；与场景既有卸载边界一致。
    const std::vector<VansParticleDrawItem>& DrawItems() const { return m_DrawItems; }
    VkBuffer UploadBuffer() const;
    VansParticleRenderDiagnostics Diagnostics() const { std::lock_guard<std::mutex> lock(m_DiagnosticsMutex); return m_PublishedDiagnostics; }
private:
    struct FrameUpload { VansVKBuffer buffer; VkDeviceSize capacity = 0; };
    struct MaterialEntry
    {
        std::shared_ptr<const VansParticleRenderAsset> asset;
        std::vector<std::unique_ptr<VansParticleSurfaceRenderer>> materials;
        bool used = false;
    };
    std::unordered_map<const VansParticleRenderAsset*, std::shared_ptr<MaterialEntry>> m_Materials;
    std::vector<std::unique_ptr<FrameUpload>> m_Uploads;
    std::vector<VansParticleDrawItem> m_DrawItems;
    std::vector<std::uint8_t> m_Staging;
    std::uint32_t m_FrameSlot = 0;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkImageView m_DepthView = VK_NULL_HANDLE;
    VansParticleRenderDiagnostics m_Diagnostics, m_PublishedDiagnostics;
    mutable std::mutex m_DiagnosticsMutex;
};
}
