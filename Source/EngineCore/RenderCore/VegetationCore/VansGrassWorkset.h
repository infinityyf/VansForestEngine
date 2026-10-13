#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace VansGraphics
{
inline constexpr uint32_t GrassWorkgroupWidth = 64u;
// Vulkan 保证支持的 X 方向工作组数；大工作表分段提交，避免缩小植被容量。
inline constexpr uint32_t GrassDispatchGroupLimit = 65535u;

struct GrassInstanceRange
{
    uint32_t baseInstance = 0;
    uint32_t instanceCount = 0;
    uint32_t capacity = 0;
};

// 一个实例段的骨状态按骨层排列，矩阵和绘制索引仍使用全池实例地址。
struct GrassWorkgroupGPU
{
    uint32_t baseInstance = 0;
    uint32_t instanceCount = 0;
    uint32_t stateStride = 0;
    uint32_t localBase = 0;
};
static_assert(sizeof(GrassWorkgroupGPU) == 16);

// A fitting layout keeps its buffers. Reused page addresses are written by GPU
// transfers whose physical resource dependencies wait for earlier frame readers.
// Capacity growth owns new buffers and retires the previous owner after its fence.
template <typename Key>
struct GrassPoolLayout
{
    uint32_t capacity = 0;
    std::map<Key, GrassInstanceRange> pages;
};

template <typename Key>
inline bool CanUpdateGrassPoolInPlace(const GrassPoolLayout<Key>& previous,
    const GrassPoolLayout<Key>& next, const std::map<Key,uint32_t>& allRequestedPages)
{
    if (previous.capacity != next.capacity) return false;
    // A surviving page moved to another pool still needs the old arena as an
    // immutable migration source. A truly removed page can be reused safely.
    for (const auto& page : previous.pages)
        if (allRequestedPages.count(page.first) && !next.pages.count(page.first)) return false;
    return true;
}

inline uint32_t GrassPoolInstanceLimit(uint64_t maxStorageBufferRange, uint32_t boneCount, uint32_t subBladeCount)
{
    if (boneCount < 2u || boneCount > 64u || !subBladeCount)
        throw std::invalid_argument("Grass pool requires valid bone and sub-blade counts.");
    const uint64_t maxBytes = std::min<uint64_t>(maxStorageBufferRange, std::numeric_limits<int>::max());
    const uint64_t capacity = std::min<uint64_t>(maxBytes / (boneCount * 2u * 64u),
        std::numeric_limits<uint32_t>::max() / subBladeCount);
    const auto aligned = capacity / GrassWorkgroupWidth * GrassWorkgroupWidth;
    if (!aligned) throw std::invalid_argument("Device storage buffer range cannot hold a grass page.");
    return static_cast<uint32_t>(aligned);
}

template <typename Key>
inline std::vector<std::map<Key, uint32_t>> PartitionGrassPages(
    const std::map<Key, uint32_t>& counts, uint32_t capacity)
{
    std::vector<std::map<Key, uint32_t>> result;
    std::vector<uint64_t> used;
    for (const auto& item : counts)
    {
        const uint64_t reserved = (uint64_t(item.second) + GrassWorkgroupWidth - 1u) /
            GrassWorkgroupWidth * GrassWorkgroupWidth;
        if (!item.second || reserved > capacity)
            throw std::invalid_argument("Grass page exceeds a pool's device capacity.");
        size_t pool = 0;
        while (pool < used.size() && used[pool] + reserved > capacity) ++pool;
        if (pool == used.size()) { result.emplace_back(); used.push_back(0); }
        result[pool].emplace(item.first, item.second);
        used[pool] += reserved;
    }
    return result;
}

template <typename Key>
inline GrassPoolLayout<Key> BuildGrassPoolLayout(const GrassPoolLayout<Key>& old,
    const std::map<Key, uint32_t>& counts)
{
    GrassPoolLayout<Key> result;
    result.capacity = old.capacity;
    for (const auto& item : counts)
    {
        if (!item.second) throw std::invalid_argument("Grass pool pages must contain instances.");
        const auto previous = old.pages.find(item.first);
        if (previous != old.pages.end() && item.second <= previous->second.capacity)
        {
            auto range = previous->second;
            range.instanceCount = item.second;
            result.pages.emplace(item.first, range);
        }
    }
    for (const auto& item : counts)
    {
        if (result.pages.count(item.first)) continue;
        const uint64_t capacity = (uint64_t(item.second) + GrassWorkgroupWidth - 1u) /
            GrassWorkgroupWidth * GrassWorkgroupWidth;
        std::vector<GrassInstanceRange> occupied;
        for (const auto& page : result.pages) occupied.push_back(page.second);
        std::sort(occupied.begin(), occupied.end(), [](const auto& a, const auto& b)
            { return a.baseInstance < b.baseInstance; });
        uint64_t base = 0;
        for (const auto& range : occupied)
        {
            if (base + capacity <= range.baseInstance) break;
            base = uint64_t(range.baseInstance) + range.capacity;
        }
        if (base + capacity > std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("Grass pool layout exceeds its address capacity.");
        result.capacity = std::max(result.capacity, static_cast<uint32_t>(base + capacity));
        result.pages.emplace(item.first, GrassInstanceRange{ static_cast<uint32_t>(base),
            item.second, static_cast<uint32_t>(capacity) });
    }
    return result;
}

inline std::vector<GrassWorkgroupGPU> BuildGrassWorkgroups(
    uint32_t poolCapacity, const std::vector<GrassInstanceRange>& ranges)
{
    auto ordered = ranges;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b)
        { return a.baseInstance < b.baseInstance; });
    uint64_t previousEnd = 0, groupCount = 0;
    for (const auto& range : ordered)
    {
        const uint64_t end = uint64_t(range.baseInstance) + range.capacity;
        if (range.instanceCount > range.capacity || end > poolCapacity ||
            range.baseInstance < previousEnd)
            throw std::invalid_argument("Grass work ranges overlap or exceed the pool capacity.");
        previousEnd = end;
        groupCount += (uint64_t(range.instanceCount) + GrassWorkgroupWidth - 1u) / GrassWorkgroupWidth;
    }
    if (groupCount > std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("Grass workgroup table exceeds the dispatch address range.");
    std::vector<GrassWorkgroupGPU> groups;
    groups.reserve(static_cast<size_t>(groupCount));
    for (const auto& range : ranges)
        for (uint64_t localBase = 0; localBase < range.instanceCount; localBase += GrassWorkgroupWidth)
            groups.push_back({ range.baseInstance, range.instanceCount, range.capacity,
                static_cast<uint32_t>(localBase) });
    return groups;
}

enum class GrassStateBuffer { Bones, Matrices, Lod, Instances };
struct GrassStateCopyRegion
{
    GrassStateBuffer buffer;
    uint64_t sourceElement;
    uint64_t destinationElement;
    uint64_t elementCount;
};

inline std::vector<GrassStateCopyRegion> BuildGrassStateCopyRegions(uint32_t sourceCapacity,
    uint32_t destinationCapacity, uint32_t boneCount,
    const GrassInstanceRange& source, const GrassInstanceRange& destination)
{
    if (boneCount < 2u || boneCount > 64u || source.instanceCount != destination.instanceCount ||
        source.instanceCount > source.capacity || destination.instanceCount > destination.capacity)
        throw std::invalid_argument("Grass state copy changed the page's bone layout.");
    BuildGrassWorkgroups(sourceCapacity, { source });
    BuildGrassWorkgroups(destinationCapacity, { destination });
    const uint64_t sourceBone = uint64_t(source.baseInstance) * boneCount;
    const uint64_t destinationBone = uint64_t(destination.baseInstance) * boneCount;
    const uint64_t matrices = uint64_t(source.instanceCount) * boneCount;
    std::vector<GrassStateCopyRegion> result;
    result.reserve(boneCount + 3u);
    for (uint32_t bone = 0; bone < boneCount; ++bone)
        result.push_back({ GrassStateBuffer::Bones, sourceBone + uint64_t(bone) * source.capacity,
            destinationBone + uint64_t(bone) * destination.capacity, source.instanceCount });
    result.push_back({ GrassStateBuffer::Matrices, sourceBone, destinationBone, matrices });
    result.push_back({ GrassStateBuffer::Matrices, sourceBone + uint64_t(sourceCapacity) * boneCount,
        destinationBone + uint64_t(destinationCapacity) * boneCount, matrices });
    result.push_back({ GrassStateBuffer::Lod, source.baseInstance, destination.baseInstance, source.instanceCount });
    return result;
}
}
