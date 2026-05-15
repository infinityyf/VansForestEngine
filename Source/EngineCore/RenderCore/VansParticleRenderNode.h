#pragma once
#include "VansRenderNode.h"
#include "../ParticleCore/VansParticleInstanceData.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include <vector>
#include <string>

namespace VansGraphics
{
    // ============================================================
    // VansParticleRenderNode — 粒子透明 Pass 渲染节点
    //
    // 不依赖 VansMaterial 系统，自持粒子专用 Billboard Pipeline。
    // 每帧由主线程调用 UpdateInstanceBuffer() 将 CPU 侧双缓冲
    // 的前台数据上传至 GPU VkBuffer，然后在 Transparent Pass
    // 中通过 DrawIndexed（instanced）绘制粒子 Quad。
    // ============================================================
    class VansParticleRenderNode : public VansRenderNode
    {
    public:
        VansParticleRenderNode(VkDevice& device);
        ~VansParticleRenderNode() override;

        // ── 渲染资源 ─────────────────────────────────────────────
        // Quad 顶点缓冲（4 顶点 [-0.5, 0.5]）
        VansVKBuffer m_QuadVertexBuffer;
        // Quad 索引缓冲（6 个 uint16，两三角形）
        VansVKBuffer m_QuadIndexBuffer;

        // GPU 端实例数据缓冲（VK_BUFFER_USAGE_VERTEX_BUFFER_BIT）
        // 每帧通过 UpdateInstanceBuffer() 上传
        VansVKBuffer m_InstanceBuffer;
        uint32_t     m_InstanceCount      = 0;
        uint32_t     m_InstanceCapacity   = 0;   // 当前分配容量（粒子数）

        // 粒子纹理（非拥有指针）
        VansTexture* m_ParticleTexture  = nullptr;

        // 渲染所用的 Shader（粒子 Billboard vert/frag）
        VansGraphicsShader* m_Shader    = nullptr;

        // Descriptor Set（持有 Camera UBO + 粒子纹理绑定）
        VkDescriptorSet            m_DescriptorSet       = VK_NULL_HANDLE;
        VkDescriptorSetLayout      m_DescriptorSetLayout = VK_NULL_HANDLE;

        // ── 每帧接口 ─────────────────────────────────────────────

        // 初始化静态 Quad 顶点/索引缓冲（场景加载时调用一次）
        bool InitQuadBuffers(VkDevice& device);

        // 将 CPU 侧实例数据上传到 GPU 实例缓冲（主线程调用）
        void UpdateInstanceBuffer(VkDevice& device,
                                   const std::vector<VansParticleInstanceData>& data);

        // Draw：绑定 Pipeline + 描述符集 + 顶点/实例缓冲 → DrawIndexed
        void Draw(VansVKCommandBuffer& cmd,
                  GlobalStateData& globalStateData) override;

    private:
        VkDevice m_Device = VK_NULL_HANDLE;  // 缓存设备引用，用于资源操作
    };

} // namespace VansGraphics
