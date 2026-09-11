#pragma once
#include "../VulkanCore/VansVKBuffer.h"
#include <glm/glm.hpp>
#include <vector>
namespace VansGraphics
{
class VansComputeShader;
// 拥有视图历史、锚点映射和定容表面缓存，独立于光照历史及世界 probe 更新调度。
struct VansGIReceiverVisibility
{
    struct alignas(16) Record
    {
        glm::vec4 surface;
        glm::uvec4 metadata;
        glm::uvec4 probes[4];
        glm::vec4 anchor;
    };
    static_assert(sizeof(Record) == 112);
    static constexpr uint32_t RayBudget = 65536;
    static constexpr VkDeviceSize WorkBytes = 16 + VkDeviceSize(RayBudget) * 8;
    static constexpr uint32_t WorldCapacity = 65536;
    static constexpr VkDeviceSize WorldBytes = 16 + VkDeviceSize(WorldCapacity) * sizeof(Record);
    // 只为没有可见 probe 的屏幕接收点补算；每帧最多 2048 个点、每点 4 条射线。
    static constexpr uint32_t TransportJobBudget = 2048;
    struct alignas(16) TransportJob
    {
        glm::vec4 origin;
        glm::vec4 directions[4];
        glm::vec4 screenRadiance;
        glm::uvec4 destination;
    };
    static_assert(sizeof(TransportJob) == 112);
    static constexpr VkDeviceSize TransportBytes = 16 + VkDeviceSize(TransportJobBudget) * sizeof(TransportJob);
    // 当前帧几何偏移：每个 4x4 锚点 32 字节，不进入遮挡历史。
    VansVKBuffer bias;
    VansVKBuffer transportWork;
    VansVKBuffer current;
    VansVKBuffer history;
    VansVKBuffer work;
    VansVKBuffer anchors;
    VansVKBuffer world;
    VansVKBuffer worldClaims;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets;
    VansComputeShader* prepareShader = nullptr;
    VansComputeShader* reprojectShader = nullptr;
    VansComputeShader* worldShader = nullptr;
    uint32_t frame = 0;
    uint64_t geometryRevision = 0;
    bool enabled = true;
};
}
