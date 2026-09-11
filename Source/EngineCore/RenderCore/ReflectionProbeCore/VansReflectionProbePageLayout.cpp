#include "VansReflectionProbePageLayout.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace VansGraphics
{
    bool VansReflectionProbePageLayout::Build(const std::vector<uint32_t>& resolutions,
        uint32_t maxImageArrayLayers, std::string& error)
    {
        error.clear();
        const uint32_t capacity = maxImageArrayLayers / 6u;
        if (!capacity) { error = "Device cannot store a complete reflection cubemap"; return false; }
        if (resolutions.size() > UINT32_MAX)
        { error = "Reflection logical probe count exceeds its address range"; return false; }
        VansReflectionProbePageLayout pending;
        pending.m_Addresses.resize(resolutions.size());
        std::array<uint32_t, 10> currentPages;
        currentPages.fill(UINT32_MAX);
        for (size_t index = 0; index < resolutions.size(); ++index)
        {
            const uint32_t resolution = resolutions[index];
            if (!resolution) continue;
            if (resolution > 512 || (resolution & (resolution - 1u)))
            { error = "Reflection texture resolution must be a power of two in 1..512"; return false; }
            uint32_t level = 0;
            for (uint32_t size = resolution; size > 1; size >>= 1u) ++level;
            uint32_t& pageIndex = currentPages[level];
            if (pageIndex == UINT32_MAX || pending.m_Pages[pageIndex].cubeCount == capacity)
            {
                if (pending.m_Pages.size() == ReflectionProbeMaxTexturePages)
                { error = "Reflection layout exceeds its texture-page descriptor capacity"; return false; }
                pageIndex = static_cast<uint32_t>(pending.m_Pages.size());
                pending.m_Pages.push_back({resolution, level + 1u, 0u});
            }
            pending.m_Addresses[index] = {pageIndex, pending.m_Pages[pageIndex].cubeCount++};
            pending.m_CaptureResolution = (std::max)(pending.m_CaptureResolution, resolution);
            pending.m_CaptureMipCount = (std::max)(pending.m_CaptureMipCount, level + 1u);
        }
        // 无局部探针时仅提供合法的最小描述符，不给天空分配局部捕获存储。
        if (pending.m_Pages.empty()) pending.m_Pages.push_back({1, 1, 1});
        for (const auto& page : pending.m_Pages)
            for (uint32_t size = page.resolution; size; size >>= 1u)
                pending.m_ResidentBytes += uint64_t(size) * size * 6u * 8u * page.cubeCount;
        *this = std::move(pending);
        return true;
    }
    VansReflectionProbeTextureAddress VansReflectionProbePageLayout::Address(uint32_t index) const
    {
        if (index >= m_Addresses.size()) throw std::out_of_range("Reflection probe texture address");
        return m_Addresses[index];
    }
}
