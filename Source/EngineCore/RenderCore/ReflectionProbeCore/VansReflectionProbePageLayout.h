#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
    // 与 ReflectionProbeData.glsl 的采样描述符数组上限一致。
    inline constexpr uint32_t ReflectionProbeMaxTexturePages = 64;
    struct VansReflectionProbeTextureAddress
    {
        uint32_t page = UINT32_MAX;
        uint32_t cube = UINT32_MAX;
    };
    struct VansReflectionProbeTexturePage
    {
        uint32_t resolution = 1;
        uint32_t mipCount = 1;
        uint32_t cubeCount = 0;
    };
    class VansReflectionProbePageLayout
    {
    public:
        // 0 表示天空等没有局部 cube 的逻辑项；其余尺寸必须为 1..512 的二次幂。
        bool Build(const std::vector<uint32_t>& resolutions, uint32_t maxImageArrayLayers, std::string& error);
        VansReflectionProbeTextureAddress Address(uint32_t index) const;
        const std::vector<VansReflectionProbeTexturePage>& Pages() const { return m_Pages; }
        uint32_t ProbeCount() const { return static_cast<uint32_t>(m_Addresses.size()); }
        uint32_t CaptureResolution() const { return m_CaptureResolution; }
        uint32_t CaptureMipCount() const { return m_CaptureMipCount; }
        uint64_t ResidentBytes() const { return m_ResidentBytes; }
    private:
        std::vector<VansReflectionProbeTextureAddress> m_Addresses;
        std::vector<VansReflectionProbeTexturePage> m_Pages;
        uint32_t m_CaptureResolution = 1;
        uint32_t m_CaptureMipCount = 1;
        uint64_t m_ResidentBytes = 0;
    };
}
