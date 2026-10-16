#pragma once
#include "VansGIQueryChanges.h"
#include "VansGISettings.h"
#include <cstdint>
#include <glm/glm.hpp>
namespace VansGraphics
{
// 接收者可见性的记录格式、工作上限与提交状态；不持有图形后端资源。
struct VansGIReceiverVisibility
{
    struct alignas(16) Record
    {
        glm::vec4 surface;
        glm::uvec4 metadata;
        glm::uvec4 probes[4];
        glm::vec4 anchor;
    };
    static_assert(sizeof(Record) == 112);
    static constexpr uint32_t RayBudget = 65536;
    static constexpr uint64_t WorkBytes = 16 + uint64_t(RayBudget) * 8;
    static constexpr uint32_t WorldCapacity = 65536;
    static constexpr uint64_t WorldBytes = 16 + uint64_t(WorldCapacity) * sizeof(Record);
    uint32_t frame = 0;

    // 硬件几何参与全部区域，GIWorld 只补充 worldOnly 区域。
    // 两个来源独立比较，不能用 XOR 合并后丢失同时发生的变化。
    VansGIReceiverInvalidation PrepareGeometry(uint64_t scene,
        const VansGIWorldQueryPublication& world, uint32_t worldRegions)
    {
        m_RecordedGeometry = {scene, world.generation, world.sequence};
        VansGIReceiverInvalidation result;
        if (scene != m_SubmittedGeometry.scene) result.regions.x = ((1u << kGIMaximumRegionCount) - 1u);
        else if (world.generation != m_SubmittedGeometry.worldGeneration ||
            (world.sequence != m_SubmittedGeometry.worldSequence &&
             world.sequence != m_SubmittedGeometry.worldSequence + 1u))
            result.regions.x = worldRegions;
        else if (world.sequence != m_SubmittedGeometry.worldSequence && !world.changes.Empty())
        {
            result.regions.y = worldRegions;
            if (world.changes.geometry)
            {
                result.geometryMin = glm::vec4(world.changes.geometry->minimum, 1);
                result.geometryMax = glm::vec4(world.changes.geometry->maximum, 0);
            }
            if (world.changes.residency)
            {
                result.residencyMin = glm::vec4(world.changes.residency->minimum, 1);
                result.residencyMax = glm::vec4(world.changes.residency->maximum, 0);
            }
        }
        return result;
    }

    void ResolveRecordedFrame(bool submitted)
    {
        if (submitted)
        {
            m_SubmittedGeometry = m_RecordedGeometry;
            ++frame;
        }
        else frame = 0; // 部分提交可能已经改写历史，下一帧必须重新建立。
    }

private:
    struct GeometryState { uint64_t scene = 0, worldGeneration = 0, worldSequence = 0; };
    GeometryState m_SubmittedGeometry, m_RecordedGeometry;
};
}
