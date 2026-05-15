#include "VansParticleRenderNode.h"
#include "VulkanCore/VansVKBuffer.h"
#include "../Util/VansLog.h"
#include <array>

namespace VansGraphics
{
    // Billboard Quad 顶点数据（局部空间 [-0.5, 0.5]，XY 平面）
    struct ParticleQuadVertex
    {
        float x, y;   // 局部位置
        float u, v;   // UV 坐标
    };

    // 4 个顶点：左下、右下、右上、左上
    static const std::array<ParticleQuadVertex, 4> QUAD_VERTICES = { {
        { -0.5f, -0.5f,  0.f, 1.f },
        {  0.5f, -0.5f,  1.f, 1.f },
        {  0.5f,  0.5f,  1.f, 0.f },
        { -0.5f,  0.5f,  0.f, 0.f }
    } };

    // 6 个索引（两个三角形）
    static const std::array<uint16_t, 6> QUAD_INDICES = { 0, 1, 2,  2, 3, 0 };

    // ── 构造 / 析构 ───────────────────────────────────────────────────────

    VansParticleRenderNode::VansParticleRenderNode(VkDevice& device)
        : VansRenderNode(device, RenderNodeType::PARTICLE_NODE)
        , m_Device(device)
    {
    }

    VansParticleRenderNode::~VansParticleRenderNode()
    {
        if (m_InstanceBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
            m_InstanceBuffer.DestroyVulkanBuffer(m_Device);

        if (m_QuadVertexBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
            m_QuadVertexBuffer.DestroyVulkanBuffer(m_Device);

        if (m_QuadIndexBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
            m_QuadIndexBuffer.DestroyVulkanBuffer(m_Device);
    }

    // ── InitQuadBuffers ───────────────────────────────────────────────────

    bool VansParticleRenderNode::InitQuadBuffers(VkDevice& device)
    {
        // 顶点缓冲（HOST_VISIBLE，一次写入，后续只读）
        VkDeviceSize vtxSize = sizeof(QUAD_VERTICES);
        bool okVtx = m_QuadVertexBuffer.CreatVulkanBuffer(
            device,
            vtxSize,
            VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (!okVtx)
        {
            VANS_LOG_ERROR("[ParticleRenderNode] Quad 顶点缓冲分配失败");
            return false;
        }
        m_QuadVertexBuffer.PersistentMap();
        m_QuadVertexBuffer.UpdateMapped(QUAD_VERTICES.data(), 0, vtxSize);

        // 索引缓冲
        VkDeviceSize idxSize = sizeof(QUAD_INDICES);
        bool okIdx = m_QuadIndexBuffer.CreatVulkanBuffer(
            device,
            idxSize,
            VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (!okIdx)
        {
            VANS_LOG_ERROR("[ParticleRenderNode] Quad 索引缓冲分配失败");
            return false;
        }
        m_QuadIndexBuffer.PersistentMap();
        m_QuadIndexBuffer.UpdateMapped(QUAD_INDICES.data(), 0, idxSize);

        return true;
    }

    // ── UpdateInstanceBuffer ───────────────────────────────────────────────

    void VansParticleRenderNode::UpdateInstanceBuffer(
        VkDevice& device,
        const std::vector<VansParticleInstanceData>& data)
    {
        m_InstanceCount = static_cast<uint32_t>(data.size());

        if (m_InstanceCount == 0) return;

        VkDeviceSize requiredSize = m_InstanceCount * sizeof(VansParticleInstanceData);

        // 若容量不足则重建缓冲
        if (m_InstanceCount > m_InstanceCapacity)
        {
            if (m_InstanceBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
                m_InstanceBuffer.DestroyVulkanBuffer(device);

            // 分配 HOST_VISIBLE | HOST_COHERENT 缓冲（持久映射，每帧直接写入）
            bool ok = m_InstanceBuffer.CreatVulkanBuffer(
                device,
                requiredSize,
                VK_FORMAT_UNDEFINED,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (!ok)
            {
                VANS_LOG_ERROR("[ParticleRenderNode] 实例缓冲分配失败");
                return;
            }

            m_InstanceBuffer.PersistentMap();
            m_InstanceCapacity = m_InstanceCount;
        }

        // 直接写入持久映射地址
        m_InstanceBuffer.UpdateMapped(data.data(), 0, requiredSize);
    }

    // ── Draw ──────────────────────────────────────────────────────────────

    void VansParticleRenderNode::Draw(VansVKCommandBuffer& cmd,
                                       GlobalStateData& globalStateData)
    {
        if (m_InstanceCount == 0) return;
        if (!m_Shader)
        {
            VANS_LOG_WARN("[ParticleRenderNode] m_Shader 未设置，跳过绘制");
            return;
        }
        if (m_QuadVertexBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
            m_QuadIndexBuffer.GetNativeBuffer()  == VK_NULL_HANDLE)
        {
            VANS_LOG_WARN("[ParticleRenderNode] Quad 缓冲未初始化，跳过绘制");
            return;
        }

        // 绑定粒子专用 Pipeline
        VansVKGraphicsPipeline* pipeline = m_Shader->GetGraphicsPipeline();
        if (!pipeline) return;
        cmd.BindGraphicsPipeline(*pipeline);

        // 绑定描述符集（Camera UBO + 粒子纹理）
        if (m_DescriptorSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> descSets = { m_DescriptorSet };
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   *m_Shader, 0, descSets, {});
        }

        // 绑定 Quad 顶点缓冲（binding 0）+ 实例缓冲（binding 1）
        VkBuffer     buffers[] = {
            m_QuadVertexBuffer.GetNativeBuffer(),
            m_InstanceBuffer.GetNativeBuffer()
        };
        VkDeviceSize offsets[] = { 0, 0 };
        cmd.BindVertexBuffers(0, 2, buffers, offsets);

        // 绑定 Quad 索引缓冲
        cmd.BindIndexBuffer(m_QuadIndexBuffer.GetNativeBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // 实例绘制：每粒子绘制一个 Quad（6 个 Index）
        cmd.DrawIndexed(6, m_InstanceCount, 0, 0, 0);
    }

} // namespace VansGraphics
