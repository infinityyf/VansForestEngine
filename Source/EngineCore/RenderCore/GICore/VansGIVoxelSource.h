#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace VansGraphics
{
    class VansMesh;
    class VansPBRMaterial;
    // 来源适配器只提交模型、材质和实例；不拥有体素资源，也不依赖构建/查询实现。
    // 指针只在渲染线程提交来源时借用；捕获保留底层 GPU 资源，后台只解析完成的独立读回。
    struct GIVoxelSourcePart
    {
        VansMesh* mesh = nullptr;
        VansPBRMaterial* material = nullptr;
        int submesh = -1;
        // 与原生植被绘制的 Leaves 部件一致；同材质的树干不使用叶片裁剪。
        bool leaf = false;
        bool operator==(const GIVoxelSourcePart& other) const
        { return mesh==other.mesh && material==other.material && submesh==other.submesh && leaf==other.leaf; }
    };
    enum class GIVoxelSourceAvailability : uint8_t
    {
        Available,
        TemporarilyUnavailable
    };
    struct GIVoxelSource
    {
        // 来源拥有命名空间内的稳定区块键；同一不可变模型定义共享身份，不依赖 PCG 类型。
        std::string key;
        std::shared_ptr<const void> modelIdentity;
        std::vector<GIVoxelSourcePart> parts;
        std::vector<glm::mat4> instances;
        // 流送缺资源时保留已发布场；只有明确 Available 的删除更新才能移除几何。
        GIVoxelSourceAvailability availability=GIVoxelSourceAvailability::Available;
    };
    struct GIVoxelSourceChanges
    {
        std::vector<GIVoxelSource> updated;
        std::vector<std::string> removed;
        bool Empty() const { return updated.empty() && removed.empty(); }
    };
}
