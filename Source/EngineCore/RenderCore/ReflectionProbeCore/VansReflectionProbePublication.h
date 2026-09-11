#pragma once
#include <vulkan/vulkan.h>

namespace VansGraphics
{
    // 在现有图形队列命令中整体发布一个已完成捕获/过滤的 cubemap。
    // 从工作图像指定 mip 起发布；两张图像均以 SHADER_READ_ONLY 开始并结束。
    // 只更新目标 cube 的完整 mip 链，工作图像更大的 mip 保持不变。
    // 不提交、不等待，也不修改描述符。调用者须在原帧同步点使用。
    bool RecordReflectionProbePublication(VkCommandBuffer command,
        VkImage source, const VkImageCreateInfo& sourceInfo, uint32_t sourceBaseMip,
        VkImage destination, const VkImageCreateInfo& destinationInfo, uint32_t destinationCube);
}
