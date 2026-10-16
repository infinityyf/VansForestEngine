#pragma once
#include "VansVKBuffer.h"
#include "../GICore/VansGIReceiverVisibility.h"
#include <vector>
#include <memory>

namespace VansGraphics
{
class VansComputeShader;
    class VansGIMemoryPublication;
class VansVKDevice;
class VansVKCommandBuffer;

// 统一管理接收者可见性的原生资源；创建和释放只能在已有的设备维护安全点执行。
struct VansVKGIReceiverVisibility
{
    void Initialize(VansVKDevice& device, VansVKCommandBuffer& command, uint32_t width, uint32_t height);
    void Destroy(VkDevice device);

    VansGIReceiverVisibility state;
    // 每个 4x4 锚点的当前帧偏移占 32 字节，不进入遮挡历史。
    VansVKBuffer bias, current, history, work, anchors, world, worldClaims;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets;
    VansComputeShader* prepareShader = nullptr;
    VansComputeShader* reprojectShader = nullptr;
    VansComputeShader* worldShader = nullptr;
    bool enabled = true;

    template <typename Visitor>
    void ForEachMutableBuffer(Visitor&& visit) const
    {
        visit("GIReceiverVisibilityBias", bias.GetNativeBuffer());
        visit("GIReceiverVisibilityCurrent", current.GetNativeBuffer());
        visit("GIReceiverVisibilityHistory", history.GetNativeBuffer());
        visit("GIReceiverVisibilityWork", work.GetNativeBuffer());
        visit("GIReceiverVisibilityAnchors", anchors.GetNativeBuffer());
        visit("GIReceiverVisibilityWorld", world.GetNativeBuffer());
        visit("GIReceiverVisibilityWorldClaims", worldClaims.GetNativeBuffer());
    }
private:
    void RefreshMemory() const;
    std::shared_ptr<VansGIMemoryPublication> m_MemoryPublication;
};
}
