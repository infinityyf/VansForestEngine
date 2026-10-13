#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Vans
{
// 冻结后的碰撞采样；每格采用 (a,c,b)、(b,c,d) 对角线，列沿 X、行沿 Z。
struct VansHeightfieldData
{
    std::uint32_t columns = 0, rows = 0;
    glm::vec3 origin{0.0f};
    float spacingX = 0, spacingZ = 0, heightScale = 0;
    std::vector<std::int16_t> samples;
};
}
