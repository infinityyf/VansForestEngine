#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansReflectionProbePublication.h"
#include <algorithm>
#include <array>
#include <vector>

namespace VansGraphics
{
    bool RecordReflectionProbePublication(VkCommandBuffer command,
        VkImage source, const VkImageCreateInfo& sourceInfo, uint32_t sourceBaseMip,
        VkImage destination, const VkImageCreateInfo& destinationInfo, uint32_t destinationCube)
    {
        if (!command || !source || !destination || source == destination ||
            sourceInfo.imageType != VK_IMAGE_TYPE_2D || destinationInfo.imageType != VK_IMAGE_TYPE_2D ||
            sourceInfo.arrayLayers != 6 || destinationCube >= destinationInfo.arrayLayers / 6u ||
            sourceInfo.extent.width == 0 || sourceInfo.extent.width != sourceInfo.extent.height || sourceInfo.extent.depth != 1 ||
            sourceBaseMip >= 32 || sourceBaseMip >= sourceInfo.mipLevels ||
            (sourceInfo.extent.width >> sourceBaseMip) != destinationInfo.extent.width ||
            (sourceInfo.extent.height >> sourceBaseMip) != destinationInfo.extent.height ||
            sourceInfo.extent.depth != destinationInfo.extent.depth || sourceInfo.format != destinationInfo.format ||
            sourceInfo.samples != VK_SAMPLE_COUNT_1_BIT || destinationInfo.samples != VK_SAMPLE_COUNT_1_BIT ||
            !(sourceInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !(destinationInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return false;
        uint32_t sourceMipCount = 1;
        for (uint32_t size = sourceInfo.extent.width; size > 1; size >>= 1u) ++sourceMipCount;
        const uint32_t fullMipCount = sourceMipCount - sourceBaseMip;
        if (sourceInfo.mipLevels != sourceMipCount || destinationInfo.mipLevels != fullMipCount) return false;

        std::vector<VkImageCopy> copies(fullMipCount);
        for (uint32_t mip = 0; mip < fullMipCount; ++mip)
        {
            copies[mip].srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sourceBaseMip + mip, 0, 6};
            copies[mip].dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, destinationCube * 6u, 6};
            copies[mip].extent = {(std::max)(1u, destinationInfo.extent.width >> mip), (std::max)(1u, destinationInfo.extent.height >> mip), 1};
        }
        std::array<VkImageMemoryBarrier, 2> barriers{};
        for (uint32_t i = 0; i < barriers.size(); ++i)
        {
            auto& b = barriers[i]; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            b.dstAccessMask = i == 0 ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = i == 0 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = i == 0 ? source : destination;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i == 0 ? sourceBaseMip : 0u,
                fullMipCount, i == 0 ? 0u : destinationCube * 6u, 6};
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, uint32_t(barriers.size()), barriers.data());
        vkCmdCopyImage(command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            uint32_t(copies.size()), copies.data());
        for (auto& b : barriers)
        {
            b.srcAccessMask = b.dstAccessMask; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = b.newLayout; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, uint32_t(barriers.size()), barriers.data());
        return true;
    }
}
