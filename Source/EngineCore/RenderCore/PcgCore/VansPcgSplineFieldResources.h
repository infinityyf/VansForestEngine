#pragma once
#include "../../PcgCore/VansPcgSplineField.h"
#include "../VulkanCore/VansTexture.h"
#include <array>

namespace VansGraphics
{
class VansVKDevice;
class VansPcgSplineFieldResources
{
public:
    explicit VansPcgSplineFieldResources(VansVKDevice& device);
    ~VansPcgSplineFieldResources();
    bool Prepare(std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> field, std::string& error);
    bool RecordUploads(VansVKCommandBuffer& command);
    VkDescriptorSetLayout Layout() const { return m_Layout; }
    VkDescriptorSet DescriptorSet() const { return m_Set; }
    const Vans::VansPcgSplineFieldSnapshot* Snapshot() const { return m_Field.get(); }
    VansTexture* Texture(std::size_t index) const { return index < m_Images.size()?m_Images[index].get():nullptr; }
private:
    struct Upload
    {
        std::size_t image = 0;
        std::uint32_t page = 0;
        std::vector<std::uint8_t> bytes;
    };
    bool ResizeAtlas(std::size_t image, std::uint32_t pages, std::string& error);
    void UpdateDescriptors(const std::vector<glm::uvec4>& metadata);
    VansVKDevice& m_Device;
    std::array<std::shared_ptr<VansTexture>,6> m_Images;
    std::array<std::uint32_t,6> m_Capacities{};
    std::shared_ptr<VansVKBuffer> m_Metadata;
    VkDescriptorSetLayout m_Layout=VK_NULL_HANDLE;
    VkDescriptorSet m_Set=VK_NULL_HANDLE;
    std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> m_Field;
    std::vector<Upload> m_Uploads;
    std::map<std::uint64_t,std::uint32_t> m_TilePages;
    std::map<std::pair<std::uint64_t,std::string>,std::uint32_t> m_DomainPages;
};
}
