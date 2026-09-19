#pragma once
#include "VansAssetGuid.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
// 通用静态模型派生产物契约。运行时只消费资源引用，不依赖减面库或编辑器。
struct VansModelLodSettings
{
    std::array<float, 2> ratios{0.5f, 0.18f};
    float maximumError = 0.04f;
};
struct VansModelLodSourcePart
{
    VansAssetGuid model, material;
    int submesh = -1;
    bool alphaTest = false;
};
struct VansModelLodPart
{
    VansAssetGuid model, material;
    int submesh = -1;
    uint32_t sourcePart = 0, triangleCount = 0;
    float error = 0;
};
struct VansModelLodLevel
{
    std::vector<VansModelLodPart> parts;
};
struct VansModelLodAsset
{
    std::string buildKey;
    std::array<float, 4> centerRadius{};
    // 原始模型仍由调用者持有；这里只存两个简化级。
    std::vector<VansModelLodLevel> levels;
};
}
