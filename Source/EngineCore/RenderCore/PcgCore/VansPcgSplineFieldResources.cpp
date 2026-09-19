#include "VansPcgSplineFieldResources.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/gtc/packing.hpp>
#include <cstring>
#include <set>

namespace VansGraphics
{
namespace
{
constexpr std::uint32_t AtlasColumns=16;
constexpr VkFormat Formats[]={VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R8_UNORM,VK_FORMAT_R32_SFLOAT};
constexpr std::size_t BytesPerPixel[]={8,4,4,16,1,4};
std::uint32_t Bits(float value) {std::uint32_t bits;std::memcpy(&bits,&value,4);return bits;}
template<class T> std::vector<std::uint8_t> Bytes(const std::vector<T>& values)
{
    std::vector<std::uint8_t> bytes(values.size()*sizeof(T));
    if (!bytes.empty()) std::memcpy(bytes.data(),values.data(),bytes.size());
    return bytes;
}
}

VansPcgSplineFieldResources::VansPcgSplineFieldResources(VansVKDevice& device):m_Device(device) {}
VansPcgSplineFieldResources::~VansPcgSplineFieldResources()
{
    auto* manager=VansVKDescriptorManager::GetInstance();
    if (m_Set) { std::vector<VkDescriptorSet> sets{m_Set}; manager->DestroyDescriptorSet(sets); }
    if (m_Layout) manager->ReleaseDescriptorSetLayout(m_Layout);
    if (m_Metadata) m_Metadata->DestroyVulkanBuffer(m_Device.GetLogicDevice());
}

bool VansPcgSplineFieldResources::ResizeAtlas(std::size_t image,std::uint32_t pages,std::string& error)
{
    if (m_Capacities[image]>=pages) return true;
    std::uint32_t capacity=AtlasColumns;
    while (capacity<pages) capacity*=2;
    const auto width=AtlasColumns*Vans::VANS_SPLINE_TILE_EXTENT;
    const auto height=(capacity/AtlasColumns)*Vans::VANS_SPLINE_TILE_EXTENT;
    if (height>16384) {error="Spline field atlas exceeds the image dimension budget.";return false;}
    auto texture=std::make_shared<VansTexture>();
    std::vector<std::uint8_t> zeros(std::size_t(width)*height*BytesPerPixel[image]);
    texture->LoadFromMemory(m_Device.GetCommandBuffer(),zeros.data(),zeros.size(),width,height,Formats[image],VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    auto old=m_Images[image];
    if (old) m_Device.EnqueueDeferredDelete([old]{});
    m_Images[image]=std::move(texture);m_Capacities[image]=capacity;return true;
}

void VansPcgSplineFieldResources::UpdateDescriptors(const std::vector<glm::uvec4>& metadata)
{
    auto buffer=std::make_shared<VansVKBuffer>();
    buffer->CreatVulkanBuffer(m_Device.GetLogicDevice(),metadata.size()*sizeof(glm::uvec4),VK_FORMAT_R32_UINT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buffer->SetBufferData(metadata.data(),0,metadata.size()*sizeof(glm::uvec4));
    auto* manager=VansVKDescriptorManager::GetInstance();
    VkDescriptorSetLayout layout=VK_NULL_HANDLE;std::vector<VkDescriptorSet> sets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_PcgSplineField(layout,sets,1);
    manager->BeginDescriptorUpdate();
    manager->WriteBufferDescriptor(sets[0],0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{buffer->GetNativeBuffer(),0,metadata.size()*sizeof(glm::uvec4)}});
    for (std::size_t i=0;i<m_Images.size();++i)
        manager->WriteImageDescriptor(sets[0],static_cast<std::uint32_t>(i+1),VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{m_Images[i]->GetImage().GetSampler(),m_Images[i]->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
    manager->CommitDescriptorUpdates();
    const auto oldSet=m_Set;auto oldLayout=m_Layout;auto oldBuffer=m_Metadata;VkDevice device=m_Device.GetLogicDevice();
    if (oldSet || oldBuffer) m_Device.EnqueueDeferredDelete([oldSet,oldLayout,oldBuffer,device]() mutable {
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        if(oldSet){std::vector<VkDescriptorSet> sets{oldSet};descriptors->DestroyDescriptorSet(sets);}
        if(oldLayout)descriptors->ReleaseDescriptorSetLayout(oldLayout);
        if(oldBuffer)oldBuffer->DestroyVulkanBuffer(device);
    });
    m_Set=sets[0];m_Layout=layout;m_Metadata=std::move(buffer);
}

bool VansPcgSplineFieldResources::Prepare(std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> field,std::string& error)
{
    if (m_Set && field==m_Field) return true;
    // 页地址在一次驻留生命周期内稳定，删除后复用空闲页，避免长时间编辑单调增长。
    std::set<std::uint64_t> liveTiles;
    if (field) for (const auto& [key,tile]:field->tiles)
    {
        liveTiles.insert(key);
    }
    const auto assign=[](auto& pages,const auto& live) {
        for(auto it=pages.begin();it!=pages.end();)if(!live.count(it->first))it=pages.erase(it);else ++it;
        std::set<std::uint32_t> used;for(const auto& [key,page]:pages)used.insert(page);
        std::uint32_t next=0;
        for(const auto& key:live)if(!pages.count(key)){while(used.count(next))++next;pages[key]=next;used.insert(next);}
    };
    assign(m_TilePages,liveTiles);
    std::uint32_t corePages=1;
    for(const auto& [key,page]:m_TilePages)corePages=std::max(corePages,page+1);
    bool reallocated=false;
    for(std::size_t i=0;i<m_Images.size();++i)
    {
        const auto old=m_Capacities[i];
        if(!ResizeAtlas(i,corePages,error))return false;
        reallocated=reallocated || old!=m_Capacities[i];
    }
    const std::uint32_t tiles=field?(field->resolution+Vans::VANS_SPLINE_TILE_SIZE-1)/Vans::VANS_SPLINE_TILE_SIZE:0;
    std::vector<glm::uvec4> metadata(2+std::size_t(tiles)*tiles,glm::uvec4(0));
    metadata[0]={field?Bits(field->worldSize):0,field?field->resolution:0,tiles,AtlasColumns};
    metadata[1]={Vans::VANS_SPLINE_TILE_SIZE,Vans::VANS_SPLINE_TILE_EXTENT,Vans::VANS_SPLINE_TILE_BORDER,0};
    if (field) for(const auto& [key,tile]:field->tiles)
    {
        const auto page=m_TilePages.at(key);
        const auto row=2+std::size_t(tile->z)*tiles+tile->x;
        metadata[row]={page+1,0,0,0};
        const auto old=m_Field?m_Field->tiles.find(key):field->tiles.end();
        if(!reallocated && m_Field && old!=m_Field->tiles.end() && old->second==tile)continue;
        m_Uploads.push_back({0,page,Bytes(tile->heights)});
        std::vector<glm::u16vec2> flow;flow.reserve(tile->velocities.size());
        for(const auto v:tile->velocities)flow.push_back({glm::packHalf1x16(v.x),glm::packHalf1x16(v.y)});
        m_Uploads.push_back({1,page,Bytes(flow)});
        std::vector<glm::u8vec4> masks;masks.reserve(tile->coverage.size());
        for(const auto v:tile->coverage)masks.push_back(glm::u8vec4(glm::round(glm::clamp(v,0.0f,1.0f)*255.0f)));
        m_Uploads.push_back({2,page,Bytes(masks)});
        std::vector<std::uint8_t> exclusion;exclusion.reserve(tile->vegetationExclusion.size());
        for (float value:tile->vegetationExclusion) exclusion.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(value,0.f,1.f)*255.f)));
        m_Uploads.push_back({4,page,Bytes(exclusion)});
        m_Uploads.push_back({3,page,Bytes(tile->riverProperties)});
        m_Uploads.push_back({5,page,Bytes(tile->waterBlend)});

    }
    UpdateDescriptors(metadata);m_Field=std::move(field);return true;
}

bool VansPcgSplineFieldResources::RecordUploads(VansVKCommandBuffer& command)
{
    for (const auto& upload:m_Uploads)
    {
        const auto x=(upload.page%AtlasColumns)*Vans::VANS_SPLINE_TILE_EXTENT;
        const auto y=(upload.page/AtlasColumns)*Vans::VANS_SPLINE_TILE_EXTENT;
        if(!m_Device.RecordDeviceImageData(m_Images[upload.image]->GetImage(),command,upload.bytes.data(),static_cast<int>(upload.bytes.size()),
            {static_cast<int>(x),static_cast<int>(y),0},{Vans::VANS_SPLINE_TILE_EXTENT,Vans::VANS_SPLINE_TILE_EXTENT,1},0,0,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT|
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT))return false;
    }
    m_Uploads.clear();return true;
}
}
