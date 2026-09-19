#include "VansMeshGeometryReadback.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include <GLM/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace VansGraphics
{
    namespace
    {
        uint32_t AttributeBytes(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_R16G16B16_SFLOAT: return 6;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
            case VK_FORMAT_R32G32B32_SFLOAT: return 12;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
            default: return 0;
            }
        }
        glm::vec3 DecodeAttribute(const uint8_t* data, VkFormat format)
        {
            glm::vec3 value;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (format == VK_FORMAT_R16G16B16_SFLOAT || format == VK_FORMAT_R16G16B16A16_SFLOAT)
                {
                    uint16_t half; std::memcpy(&half, data + axis * sizeof(half), sizeof(half));
                    value[axis] = glm::unpackHalf1x16(half);
                }
                else std::memcpy(&value[axis], data + axis * sizeof(float), sizeof(float));
            }
            return value;
        }
        struct ReadbackScope
        {
            VkDevice& device;
            VansVKBuffer buffer;
            ~ReadbackScope() { buffer.DestroyVulkanBuffer(device); }
        };
        struct MeshCopy
        {
            VkDeviceSize vertexOffset = 0, vertexBytes = 0, indexOffset = 0, indexBytes = 0;
            uint32_t stride = 0, vertexCount = 0, indexCount = 0;
            VkIndexType indexType = VK_INDEX_TYPE_UINT32;
            VkVertexInputAttributeDescription position{}, normal{}, uv{};
        };
        bool Finite(const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    bool VansMeshGeometryReadback::Read(VansVKDevice& device, const std::vector<VansMesh*>& meshes,
        std::vector<VansMeshGeometryData>& output, std::string& error, bool includeTexcoords)
    {
        output.clear(); error.clear();
        if (meshes.empty()) return true;
        std::vector<MeshCopy> copies(meshes.size());
        VkDeviceSize bytes = 0;
        auto fail = [&](const std::string& message) { output.clear(); error = message; return false; };
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            auto* mesh = meshes[i];
            if (!mesh) return fail("Geometry readback received a null mesh");
            auto& copy = copies[i];
            copy.stride = mesh->GetMeshVertexStride(); copy.vertexCount = mesh->GetMeshVertexCount();
            copy.indexCount = mesh->GetIndexCount(); copy.indexType = mesh->GetIndexBufferParameter().IndexType;
            for (const auto& attribute : mesh->m_VertexInputAttributeDescriptions)
            {
                if (attribute.location == 0) copy.position = attribute;
                if (attribute.location == 2) copy.normal = attribute;
                if (includeTexcoords && attribute.location == 1) copy.uv = attribute;
            }
            if (copy.stride == 0 || copy.vertexCount == 0 || !AttributeBytes(copy.position.format)
                || copy.position.binding != 0 || copy.position.offset + AttributeBytes(copy.position.format) > copy.stride)
                return fail("Geometry readback requires a supported position attribute at location 0, binding 0");
            if (copy.normal.format != VK_FORMAT_UNDEFINED && (!AttributeBytes(copy.normal.format)
                || copy.normal.binding != 0 || copy.normal.offset + AttributeBytes(copy.normal.format) > copy.stride))
                return fail("Geometry readback found an unsupported normal attribute");
            if (copy.indexCount && copy.indexType != VK_INDEX_TYPE_UINT16 && copy.indexType != VK_INDEX_TYPE_UINT32)
                return fail("Geometry readback found an unsupported index format");
            if ((copy.indexCount ? copy.indexCount : copy.vertexCount) % 3u != 0)
                return fail("Geometry readback requires triangle-list topology");
            copy.vertexBytes = VkDeviceSize(copy.stride) * copy.vertexCount;
            if (includeTexcoords && (copy.uv.binding != 0 ||
                (copy.uv.format != VK_FORMAT_R16G16_SFLOAT && copy.uv.format != VK_FORMAT_R32G32_SFLOAT) ||
                copy.uv.offset + (copy.uv.format == VK_FORMAT_R16G16_SFLOAT ? 4u : 8u) > copy.stride))
                return fail("GI voxelization requires a valid UV attribute");
            copy.indexBytes = VkDeviceSize(copy.indexCount) * (copy.indexType == VK_INDEX_TYPE_UINT16 ? 2u : 4u);
            if (!mesh->GetBLASVertexBuffer().GetNativeBuffer() || copy.vertexBytes > mesh->GetBLASVertexBuffer().GetBufferSize()
                || (copy.indexBytes && (!mesh->GetIndexBuffer().GetNativeBuffer() || copy.indexBytes > mesh->GetIndexBuffer().GetBufferSize())))
                return fail("Geometry readback found missing or undersized GPU buffers");
            copy.vertexOffset = bytes; bytes = (bytes + copy.vertexBytes + 3u) & ~VkDeviceSize(3u);
            copy.indexOffset = bytes; bytes = (bytes + copy.indexBytes + 3u) & ~VkDeviceSize(3u);
            // 显式失败并保留调用方旧布局，不截断几何并把遗漏物体当成空地。
            if (bytes > (VkDeviceSize(2) << 30)) return fail("Geometry readback exceeds the 2 GiB staging budget");
        }

        ReadbackScope readback{ device.GetLogicDevice() };
        if (!readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(), bytes, VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            || !readback.buffer.PersistentMap()) return fail("Geometry readback allocation failed");
        auto& commandBuffer = device.GetImmediateGraphicsCommandBuffer();
        if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
            return fail("Geometry readback command recording failed");
        VkMemoryBarrier toCopy{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        toCopy.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, { toCopy }, {}, {});
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            const auto& copy = copies[i];
            commandBuffer.CopyBuffer(meshes[i]->GetBLASVertexBuffer().GetNativeBuffer(), readback.buffer.GetNativeBuffer(),
                0, copy.vertexOffset, copy.vertexBytes);
            if (copy.indexBytes) commandBuffer.CopyBuffer(meshes[i]->GetIndexBuffer().GetNativeBuffer(), readback.buffer.GetNativeBuffer(),
                0, copy.indexOffset, copy.indexBytes);
        }
        VkMemoryBarrier toHost{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, { toHost }, {}, {});
        const bool ended = commandBuffer.EndCommandBufferRecord();
        const bool submitted = ended && VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
            { commandBuffer.GetVKCommandBuffer() }, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
        commandBuffer.ResetCommandBuffer(false);
        if (!submitted) return fail("Geometry readback GPU submission failed");
        readback.buffer.InvalidateMappedRange(0, bytes);
        const auto* data = static_cast<const uint8_t*>(readback.buffer.GetMappedPtr());
        try
        {
            output.resize(meshes.size());
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                const auto& copy = copies[i]; auto& geometry = output[i];
                geometry.positions.resize(copy.vertexCount);
                if (includeTexcoords) geometry.texcoords.resize(copy.vertexCount);
                if (copy.normal.format != VK_FORMAT_UNDEFINED) geometry.normals.resize(copy.vertexCount);
                for (uint32_t vertex = 0; vertex < copy.vertexCount; ++vertex)
                {
                    const auto* source = data + copy.vertexOffset + VkDeviceSize(vertex) * copy.stride;
                    geometry.positions[vertex] = DecodeAttribute(source + copy.position.offset, copy.position.format);
                    if (includeTexcoords)
                    {
                        if (copy.uv.format == VK_FORMAT_R16G16_SFLOAT)
                        { uint32_t packed; std::memcpy(&packed,source+copy.uv.offset,4);geometry.texcoords[vertex]=glm::unpackHalf2x16(packed); }
                        else std::memcpy(&geometry.texcoords[vertex],source+copy.uv.offset,8);
                    }
                    if (!Finite(geometry.positions[vertex])) return fail("Geometry readback found a non-finite position");
                    if (!geometry.normals.empty())
                    {
                        geometry.normals[vertex] = DecodeAttribute(source + copy.normal.offset, copy.normal.format);
                        if (!Finite(geometry.normals[vertex])) return fail("Geometry readback found a non-finite normal");
                    }
                }
                geometry.indices.resize(copy.indexCount ? copy.indexCount : copy.vertexCount);
                for (uint32_t index = 0; index < geometry.indices.size(); ++index)
                {
                    uint32_t value = index;
                    if (copy.indexCount)
                    {
                        const auto* source = data + copy.indexOffset;
                        if (copy.indexType == VK_INDEX_TYPE_UINT16)
                        { uint16_t shortIndex; std::memcpy(&shortIndex, source + size_t(index) * 2, 2); value = shortIndex; }
                        else std::memcpy(&value, source + size_t(index) * 4, 4);
                    }
                    if (value >= copy.vertexCount) return fail("Geometry readback found an out-of-range triangle index");
                    geometry.indices[index] = value;
                }
            }
        }
        catch (const std::exception& exception) { return fail(exception.what()); }
        return true;
    }
}
