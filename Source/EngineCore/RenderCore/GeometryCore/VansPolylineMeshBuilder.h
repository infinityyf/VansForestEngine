#pragma once
#include "glm/glm.hpp"
#include <cstddef>
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
struct VansPolylineBuildResult
{
    bool viewValid = true;
    std::size_t verticesAdded = 0;
    std::size_t indicesAdded = 0;
    std::size_t rejectedPointCount = 0;
    std::size_t splitRunCount = 0;
};
class VansPolylineMeshBuilder final
{
public:
    // 面向相机的三角形带。尖角倒角、折返点断开，不依赖上一帧相机状态。
    // 非法点只断开所在曲线段，其余有效段继续追加；返回值记录本次实际追加和拒绝数量。
    [[nodiscard]] static VansPolylineBuildResult Append(const std::vector<VansPolylinePoint>& points,
        const glm::vec3& cameraPosition, const glm::vec3& cameraRight,
        const glm::vec3& cameraUp, VansPolylineMesh& output);
};
}
