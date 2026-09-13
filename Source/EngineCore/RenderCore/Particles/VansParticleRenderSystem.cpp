#include <chrono>
#include "VansParticleRenderSystem.h"
#include "../VansScene.h"
#include "../VansRenderFrame.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>

namespace VansGraphics
{
namespace
{
constexpr std::size_t MaxUploadBytes = 16u*1024u*1024u;
VkDeviceSize AppendBytes(std::vector<std::uint8_t>& staging, const void* bytes, std::size_t count)
{
    const std::size_t offset = (staging.size()+15u)&~std::size_t(15u);
    staging.resize(offset+count);
    if (count) std::memcpy(staging.data()+offset,bytes,count);
    return offset;
}
struct Bounds
{
    glm::vec3 minimum{std::numeric_limits<float>::max()}, maximum{-std::numeric_limits<float>::max()};
    void Add(const glm::vec3& position, float radius)
    { minimum = glm::min(minimum,position-glm::vec3(radius)); maximum = glm::max(maximum,position+glm::vec3(radius)); }
    glm::vec3 Center() const { return (minimum+maximum)*0.5f; }
};
}
VansParticleRenderSystem::~VansParticleRenderSystem() { Shutdown(); }
void VansParticleRenderSystem::Shutdown()
{
    m_DrawItems.clear(); m_Materials.clear(); m_Staging.clear();
    for (auto& upload : m_Uploads) if (upload && upload->buffer.GetNativeBuffer()) upload->buffer.DestroyVulkanBuffer(m_Device);
    m_Uploads.clear(); m_Device = VK_NULL_HANDLE; m_DepthView = VK_NULL_HANDLE;
    m_Diagnostics = {};
    std::lock_guard<std::mutex> lock(m_DiagnosticsMutex); m_PublishedDiagnostics = {};
}
VkBuffer VansParticleRenderSystem::UploadBuffer() const
{ return m_FrameSlot < m_Uploads.size() && m_Uploads[m_FrameSlot] ? m_Uploads[m_FrameSlot]->buffer.GetNativeBuffer() : VK_NULL_HANDLE; }
void VansParticleRenderSystem::Prepare(VansVKDevice& device, VansScene& scene,
    const VansRenderViewSnapshot& view, const VansRenderSceneFrameSnapshot& snapshot)
{
    const auto started = std::chrono::steady_clock::now();
    m_Device = device.GetLogicDevice(); m_FrameSlot = device.GetFrameResourceSlot();
    m_DrawItems.clear(); m_Staging.clear(); m_Diagnostics = {};
    const auto depthView = VansRenderPassManager::GetInstance()->GetDepth().GetImageView();
    if (m_DepthView != depthView)
    {
        for (auto& entry : m_Materials) device.EnqueueDeferredDelete([retired=std::move(entry.second)] {});
        m_Materials.clear(); m_DepthView = depthView;
    }
    for (auto& entry : m_Materials) entry.second->used = false;
    // 同一上传区包含静态四边形、Billboard 实例和 Ribbon 网格，按真正的帧槽隔离。
    const float quad[] = {-0.5f,-0.5f,0,1, 0.5f,-0.5f,1,1, 0.5f,0.5f,1,0, -0.5f,0.5f,0,0};
    const std::uint32_t quadIndices[] = {0,1,2,2,3,0};
    VkDeviceSize quadVertexOffset = 0, quadIndexOffset = 0;
    bool hasQuad = false;
    for (const auto& frame : snapshot.particles)
    {
        if (!frame.asset || !frame.asset->definition) continue;
        const auto& definition = *frame.asset->definition;
        const auto& data = frame.data;
        auto found = m_Materials.find(frame.asset.get());
        if (found == m_Materials.end())
        {
            auto entry = std::make_shared<MaterialEntry>(); entry->asset = frame.asset;
            entry->materials.resize(definition.m_Emitters.size());
            for (std::size_t i=0; i<definition.m_Emitters.size(); ++i)
            {
                if (!definition.m_Emitters[i] || i >= frame.asset->textures.size()) continue;
                const auto& config = definition.m_Emitters[i]->m_RendererConfig;
                if (config.m_Type == VansParticleRendererType::None) continue;
                auto material = std::make_unique<VansParticleSurfaceRenderer>();
                if (material->Initialize(scene,config,frame.asset->textures[i])) entry->materials[i] = std::move(material);
            }
            found = m_Materials.emplace(frame.asset.get(),std::move(entry)).first;
        }
        auto& entry = *found->second; entry.used = true;
        for (const auto& range : data.emitters)
        {
            if (range.emitterIndex >= entry.materials.size() || !entry.materials[range.emitterIndex]) continue;
            const auto& config = definition.m_Emitters[range.emitterIndex]->m_RendererConfig;
            VansParticleDrawItem item; item.material = entry.materials[range.emitterIndex].get();
            if (range.surfaceCount && range.surfaceFirst <= data.instances.size() && range.surfaceCount <= data.instances.size()-range.surfaceFirst)
            {
                const std::size_t bytes = sizeof(VansParticleInstanceData)*range.surfaceCount;
                if (m_Staging.size()+bytes+128 > MaxUploadBytes) { ++m_Diagnostics.droppedDraws; continue; }
                if (!hasQuad)
                {
                    quadVertexOffset = AppendBytes(m_Staging,quad,sizeof(quad));
                    quadIndexOffset = AppendBytes(m_Staging,quadIndices,sizeof(quadIndices)); hasQuad = true;
                }
                std::vector<VansParticleInstanceData> instances;
                if (config.m_SortMode == VansParticleSortMode::ByDistance)
                {
                    std::vector<std::uint32_t> order(range.surfaceCount);
                    std::iota(order.begin(),order.end(),range.surfaceFirst);
                    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b) {
                        return (view.view*glm::vec4(data.instances[a].m_WorldPosition,1)).z < (view.view*glm::vec4(data.instances[b].m_WorldPosition,1)).z;
                    });
                    instances.reserve(order.size());
                    for (auto index : order) instances.push_back(data.instances[index]);
                }
                else instances.assign(data.instances.begin()+range.surfaceFirst,data.instances.begin()+range.surfaceFirst+range.surfaceCount);
                Bounds bounds;
                for (const auto& instance : instances) bounds.Add(instance.m_WorldPosition,instance.m_Size*0.5f);
                item.center = bounds.Center(); item.vertexOffset = quadVertexOffset; item.indexOffset = quadIndexOffset;
                item.instanceOffset = AppendBytes(m_Staging,instances.data(),bytes);
                item.indexCount = 6; item.instanceCount = range.surfaceCount;
                m_DrawItems.push_back(item);
            }
            if (range.ribbonFirst > data.ribbons.size() || range.ribbonCount > data.ribbons.size()-range.ribbonFirst) continue;
            for (std::uint32_t i=range.ribbonFirst; i<range.ribbonFirst+range.ribbonCount; ++i)
            {
                const auto& strip = data.ribbons[i];
                if (strip.points.size() < 2) continue;
                // 每点最多两对顶点与十二个索引，构网前检查上界，预算超限整条拒绝。
                if (m_Staging.size()+32 >= MaxUploadBytes || strip.points.size() > (MaxUploadBytes-m_Staging.size()-32)/240)
                { ++m_Diagnostics.droppedDraws; continue; }
                std::vector<VansPolylinePoint> points; points.reserve(strip.points.size());
                Bounds bounds;
                for (const auto& point : strip.points)
                { points.push_back({point.position,point.width,point.color,point.u}); bounds.Add(point.position,point.width); }
                VansPolylineMesh mesh;
                VansPolylineMeshBuilder::Append(points,view.position,view.right,view.up,mesh);
                if (mesh.indices.empty()) continue;
                item.ribbon = true; item.center = bounds.Center(); item.instanceCount = 1;
                item.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
                item.vertexOffset = AppendBytes(m_Staging,mesh.vertices.data(),mesh.vertices.size()*sizeof(VansPolylineVertex));
                item.indexOffset = AppendBytes(m_Staging,mesh.indices.data(),mesh.indices.size()*sizeof(std::uint32_t));
                m_DrawItems.push_back(item);
            }
        }
    }
    for (auto iterator=m_Materials.begin(); iterator!=m_Materials.end();)
    {
        if (iterator->second->used) { ++iterator; continue; }
        device.EnqueueDeferredDelete([retired=std::move(iterator->second)] {});
        iterator = m_Materials.erase(iterator);
    }
    if (!m_Staging.empty())
    {
        if (m_Uploads.size() <= m_FrameSlot) m_Uploads.resize(m_FrameSlot+1);
        if (!m_Uploads[m_FrameSlot]) m_Uploads[m_FrameSlot] = std::make_unique<FrameUpload>();
        auto& upload = *m_Uploads[m_FrameSlot];
        if (upload.capacity < m_Staging.size())
        {
            // Prepare 的调用点已经等待本槽 fence。其他在途槽完全不变。
            if (upload.buffer.GetNativeBuffer()) upload.buffer.DestroyVulkanBuffer(m_Device);
            upload.capacity = 65536;
            while (upload.capacity < m_Staging.size()) upload.capacity *= 2;
            if (!upload.buffer.CreatVulkanBuffer(m_Device,upload.capacity,VK_FORMAT_UNDEFINED,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || !upload.buffer.PersistentMap())
            {
                upload.buffer.DestroyVulkanBuffer(m_Device); upload.capacity = 0; m_DrawItems.clear();
                m_Diagnostics.droppedDraws++;
                std::lock_guard<std::mutex> lock(m_DiagnosticsMutex); m_PublishedDiagnostics = m_Diagnostics;
                VANS_LOG_ERROR("[Particle] Frame upload allocation failed"); return;
            }
        }
        upload.buffer.UpdateMapped(m_Staging.data(),0,m_Staging.size());
    }
    m_Diagnostics.uploadBytes = m_Staging.size(); m_Diagnostics.drawCount = static_cast<std::uint32_t>(m_DrawItems.size());
    for (const auto& upload : m_Uploads) if (upload) m_Diagnostics.allocatedBytes += upload->capacity;
    m_Diagnostics.prepareMilliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    std::lock_guard<std::mutex> lock(m_DiagnosticsMutex); m_PublishedDiagnostics = m_Diagnostics;
}
}
