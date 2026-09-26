#pragma once

#include "../ModelLod/VansModelLodBuilder.h"
#include "../../PcgCore/VansPlantTypeAsset.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
class VansAssetDatabase;

struct VansPlantLodVariantSummary
{
    std::string variantId;
    std::string buildKey;
    std::vector<std::uint64_t> triangleCounts;
};

// Tree Plant 的 LOD 编排唯一入口。只计算候选资产和摘要，不保存 Plant 文档。
class VansPlantLodOrchestrator
{
public:
    static bool Build(VansAssetDatabase& database,
        const VansPlantTypeAsset& source,
        VansModelLodBuildMode mode,
        VansPlantTypeAsset& result,
        std::vector<VansPlantLodVariantSummary>& summaries,
        std::string& error);
};
}
