#pragma once
#include "glm/glm.hpp"
#include <cstdint>
#include <vector>

namespace VansGraphics
{
// 输入是一条有序曲线；出生顺序、锚定和生命周期由调用者决定。
struct VansPolylinePoint
{
    glm::vec3 position{0};
    float width = 0;
    glm::vec4 color{1};
    float u = 0;
};
struct VansPolylineVertex
{
    glm::vec3 position{0};
    float padding0 = 0;
    glm::vec4 color{1};
    glm::vec2 uv{0};
    glm::vec2 padding1{0};
};
static_assert(sizeof(VansPolylineVertex) == 48);
struct VansPolylineMesh
{
    std::vector<VansPolylineVertex> vertices;
    std::vector<std::uint32_t> indices;
};
class VansPolylineMeshBuilder final
{
public:
    // 面向相机的三角形带。尖角倒角、折返点断开，不依赖上一帧相机状态。
    static void Append(const std::vector<VansPolylinePoint>& points,
        const glm::vec3& cameraPosition, const glm::vec3& cameraRight,
        const glm::vec3& cameraUp, VansPolylineMesh& output);
};
}
