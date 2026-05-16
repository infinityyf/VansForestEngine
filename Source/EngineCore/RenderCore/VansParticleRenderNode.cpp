#include "VansParticleRenderNode.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VulkanCore/VansVKCommandBuffer.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "VulkanCore/VansVKDescriptorManager.h"
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

    // ── SetupDescriptors ─────────────────────────────────────────────────
    // 在 CreateGlobalDescriptorSet() 之后调用，完成 GPU 端描述符绑定。
    // Set 0：引擎全局集（Camera UBO 等），直接复用场景已分配的资源。
    // Set 1：粒子纹理，创建独立 layout 并分配 descriptor set，写入 defaultTex。

    void VansParticleRenderNode::SetupDescriptors(VkDescriptorSetLayout globalLayout,
                                                   VkDescriptorSet       globalSet,
                                                   VansTexture*          defaultTex)
    {
        // Set 0：全局集（Camera、Lights、Bindless textures…）
        m_UsedDescSetLayouts.push_back(globalLayout);
        m_UsedDescSets.push_back(globalSet);

        if (defaultTex == nullptr)
        {
            VANS_LOG_WARN("[ParticleRenderNode] SetupDescriptors：未提供粒子纹理，Set 1 将跳过绑定");
            // 仍需为 Set 1 提供占位 layout，使 pipeline layout 与 shader 一致
            std::vector<VkDescriptorSet> emptySets;
            VansDescriptorSetLayoutFactory::CreateAndAllocate_Empty(
                m_DescriptorSetLayout, emptySets);
            m_DescriptorSet = emptySets[0];
        }
        else
        {
            // 为 Set 1 创建：binding 0 = sampler2D particleTex
            const std::vector<VkDescriptorSetLayoutBinding> texBindings = {
                { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                  VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
            };
            std::vector<VkDescriptorSet> texSets;
            VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
                texBindings, m_DescriptorSetLayout);
            std::vector<VkDescriptorSetLayout> layouts(1, m_DescriptorSetLayout);
            VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(layouts, texSets);
            m_DescriptorSet = texSets[0];

            // 写入纹理图像描述符
            VansVKDescriptorManager::GetInstance()->ResetState();
            VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
                {
                    m_DescriptorSet,
                    0,   // binding 0
                    0,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    {
                        {
                            defaultTex->GetImage().GetSampler(),
                            defaultTex->GetImage().GetImageView(),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        }
                    }
                }
            );
            VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
            m_ParticleTexture = defaultTex;
        }

        // Set 1 layout/set 追加到公共数组，用于 EnsureGraphicsShader 管线布局创建
        m_UsedDescSetLayouts.push_back(m_DescriptorSetLayout);
        m_UsedDescSets.push_back(m_DescriptorSet);
    }

    // ── Draw ──────────────────────────────────────────────────────────────

    // 粒子 Quad 的顶点输入布局（静态，全局共享）
    // Binding 0：per-vertex（stride = 16 bytes：  vec2 pos + vec2 uv）
    // Binding 1：per-instance（stride = 48 bytes：VansParticleInstanceData）
    static const std::array<VkVertexInputBindingDescription, 2> PARTICLE_BINDING_DESCS = { {
        { 0, sizeof(float) * 4, VK_VERTEX_INPUT_RATE_VERTEX   },   // QuadVertex  (x,y,u,v)
        { 1, sizeof(VansGraphics::VansParticleInstanceData),
             VK_VERTEX_INPUT_RATE_INSTANCE }                         // InstanceData
    } };

    static const std::array<VkVertexInputAttributeDescription, 8> PARTICLE_ATTR_DESCS = { {
        // Binding 0 — per-vertex
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0  },   // location 0 inLocalPos  (offset 0)
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8  },   // location 1 inUV        (offset 8)
        // Binding 1 — per-instance (对应 VansParticleInstanceData 各字段偏移)
        { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VansGraphics::VansParticleInstanceData, m_WorldPosition) },  // location 2 instWorldPos
        { 3, 1, VK_FORMAT_R32_SFLOAT,       offsetof(VansGraphics::VansParticleInstanceData, m_Size)          },  // location 3 instSize
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VansGraphics::VansParticleInstanceData, m_Color)      },  // location 4 instColor
        { 5, 1, VK_FORMAT_R32_SFLOAT,       offsetof(VansGraphics::VansParticleInstanceData, m_Rotation)      },  // location 5 instRotation
        { 6, 1, VK_FORMAT_R32_SFLOAT,       offsetof(VansGraphics::VansParticleInstanceData, m_FrameIndex)    },  // location 6 instFrameIndex
        { 7, 1, VK_FORMAT_R32G32_SFLOAT,    offsetof(VansGraphics::VansParticleInstanceData, m_Padding)       },  // location 7 instPadding
    } };

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

        // ── 1. 注入粒子专用顶点输入布局，供 EnsureGraphicsShader 构建 Pipeline ──
        auto* savedBindings   = globalStateData.vertexInputBindingDescriptions;
        auto* savedAttributes = globalStateData.vertexInputAttributeDescriptions;

        static std::vector<VkVertexInputBindingDescription>   s_Bindings(
            PARTICLE_BINDING_DESCS.begin(), PARTICLE_BINDING_DESCS.end());
        static std::vector<VkVertexInputAttributeDescription> s_Attributes(
            PARTICLE_ATTR_DESCS.begin(), PARTICLE_ATTR_DESCS.end());

        globalStateData.vertexInputBindingDescriptions   = &s_Bindings;
        globalStateData.vertexInputAttributeDescriptions = &s_Attributes;

        // ── 2. 确保 Pipeline 已构建（延迟创建，首帧触发） ─────────────────────
        cmd.EnsureGraphicsShader(*m_Shader, globalStateData, m_UsedDescSetLayouts);

        // 恢复全局顶点输入状态，避免污染后续其他节点的绘制
        globalStateData.vertexInputBindingDescriptions   = savedBindings;
        globalStateData.vertexInputAttributeDescriptions = savedAttributes;

        // ── 3. 绑定 Pipeline ──────────────────────────────────────────────
        VansVKGraphicsPipeline* pipeline = m_Shader->GetGraphicsPipeline();
        if (!pipeline) return;
        cmd.BindGraphicsPipeline(*pipeline);

        // ── 4. 绑定描述符集（Set 0 全局 + Set 1 粒子纹理） ───────────────────
        if (!m_UsedDescSets.empty())
        {
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   *m_Shader, 0, m_UsedDescSets, {});
        }

        // ── 5. 推送 Push Constants（精灵动画参数：单帧时传 (1,1,0,0)） ──────
        if (m_Shader->GetPushConstantSize() > 0)
        {
            // spriteSheetParams: (cols, rows, unused, unused)
            // 默认单帧：cols=1, rows=1
            float spriteParams[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
            cmd.UpdatePushConstants(*pipeline,
                                    VK_SHADER_STAGE_VERTEX_BIT,
                                    0, m_Shader->GetPushConstantSize(),
                                    spriteParams);
        }

        // ── 6. 绑定顶点缓冲（Quad binding 0 + 实例 binding 1） ─────────────
        VkBuffer     buffers[] = {
            m_QuadVertexBuffer.GetNativeBuffer(),
            m_InstanceBuffer.GetNativeBuffer()
        };
        VkDeviceSize offsets[] = { 0, 0 };
        cmd.BindVertexBuffers(0, 2, buffers, offsets);

        // ── 7. 绑定索引缓冲 ───────────────────────────────────────────────
        cmd.BindIndexBuffer(m_QuadIndexBuffer.GetNativeBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // ── 8. 实例化绘制：每粒子一个 Quad（6 个索引） ──────────────────────
        cmd.DrawIndexed(6, m_InstanceCount, 0, 0, 0);
    }

} // namespace VansGraphics
