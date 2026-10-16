#pragma once

#include <GLM/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace VansGraphics
{
    class VansMesh;
    class VansVKDevice;
    class VansVKCommandBuffer;
    class VansVKBuffer;

    struct VansMeshGeometryData
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texcoords;
        std::vector<uint32_t> indices;
    };

    struct VansMeshGeometryReadbackAttributes
    {
        bool normals=true;
        bool texcoords=false;
    };

    class VansMeshGeometryReadback
    {
    public:
        VansMeshGeometryReadback();
        ~VansMeshGeometryReadback();
        VansMeshGeometryReadback(VansMeshGeometryReadback&&) noexcept;
        VansMeshGeometryReadback& operator=(VansMeshGeometryReadback&&) noexcept;
        VansMeshGeometryReadback(const VansMeshGeometryReadback&)=delete;
        VansMeshGeometryReadback& operator=(const VansMeshGeometryReadback&)=delete;

        // Snapshot layout and retain native buffers while input wrappers are valid.
        // Record does not submit or wait; Decode reads only completed host bytes.
        bool Prepare(const std::vector<VansMesh*>& meshes,std::string& error,VansMeshGeometryReadbackAttributes attributes={});
        uint64_t ByteSize() const;
        void Record(VansVKCommandBuffer& command,VansVKBuffer& destination) const;
        bool Decode(const void* bytes,std::vector<VansMeshGeometryData>& output,std::string& error) const;
        void ReleaseSourcesAfterGpuCompletion();
        // 调用方在现有 render-thread idle transaction / 场景准备阶段保证资源稳定。
        // 一次提交读取所有输入网格；不打开资产源文件，也不让网格常驻额外 CPU 副本。
        static bool Read(VansVKDevice& device, const std::vector<VansMesh*>& meshes,
            std::vector<VansMeshGeometryData>& output, std::string& error, VansMeshGeometryReadbackAttributes attributes={});
    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
