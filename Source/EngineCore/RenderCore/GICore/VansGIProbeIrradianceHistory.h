#pragma once

#include <array>
#include <glm/glm.hpp>

namespace VansGraphics
{
    // 与 GIProbeIrradianceHistory.glsl 的 std430 布局一致，仅世界区域分配。
    // 两份 6×6 历史均为按接收法线积分后的 E，不是沿射线方向的入射辐亮度。
    // 对外图集仍为带边框的总 E，消费者无需知道主光照/散射分量历史。
    struct alignas(16) VansGIProbeIrradianceHistory
    {
        glm::uvec2 integration{0u}; // 主光照次数、散射次数；光源变化请求仅由 CPU 调度器持有。
        alignas(16) std::array<glm::vec4, 36> primary{};
        std::array<glm::vec4, 36> scattering{};
    };
    static_assert(sizeof(VansGIProbeIrradianceHistory) == 1168);
}
