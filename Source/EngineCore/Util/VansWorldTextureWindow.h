#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
namespace Vans
{
// 生产者和消费者共享的纯值世界 XZ 窗口，无 Terrain、Pass 或 GPU 依赖。
struct VansWorldTextureWindow
{
    glm::vec4 worldRect{-10.0f,-10.0f,20.0f,20.0f};
    glm::uvec2 resolution{1u};
    bool cameraCentered=false;
};
bool ResolveWorldTextureWindow(const VansWorldTextureWindow& window,const glm::vec3& camera,
    glm::vec4& rectangle,std::array<std::int64_t,2>& texelOrigin);
}
