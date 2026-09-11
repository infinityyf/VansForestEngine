#pragma once
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKBuffer.h"
#include <memory>
#include "VansSkyLightingTypes.h"

namespace VansGraphics
{
    class VansVKDevice;
    // 世界天空照明源的唯一 GPU 资源所有者；不依赖编辑器或物理天空实现。
    class VansSkyLighting
    {
    public:
        void Initialize(VansVKDevice& device, VansVKCommandBuffer& commandBuffer, const std::string& sourceDirectory);
        void Destroy(VkDevice device);
        bool UploadFrame(const VansSkyLightingFrame& frame);
        std::string CaptureSourceKey(float authoredIntensity) const;
        VansTexture* Radiance() const { return m_Radiance.get(); }
        VansTexture* DiffuseIrradiance() const { return m_Diffuse.get(); }
        VansTexture* SpecularRadiance() const { return m_Specular.get(); }
        const VansVKBuffer& SH() const { return m_SH; }
        const VansVKBuffer& Parameters() const { return m_Parameters; }
    private:
        std::unique_ptr<VansTexture> m_Radiance, m_Diffuse, m_Specular;
        VansVKBuffer m_SH, m_Parameters;
        uint64_t m_SourceHash = 0;
    };
}
