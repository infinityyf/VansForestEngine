#pragma once

#include <GLM/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
    class VansMesh;
    class VansVKDevice;

    struct VansMeshGeometryData
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texcoords;
        std::vector<uint32_t> indices;
    };

    class VansMeshGeometryReadback
    {
    public:
        // 调用方在现有 render-thread idle transaction / 场景准备阶段保证资源稳定。
        // 一次提交读取所有输入网格；不打开资产源文件，也不让网格常驻额外 CPU 副本。
        static bool Read(VansVKDevice& device, const std::vector<VansMesh*>& meshes,
            std::vector<VansMeshGeometryData>& output, std::string& error, bool includeTexcoords = false);
    };
}
