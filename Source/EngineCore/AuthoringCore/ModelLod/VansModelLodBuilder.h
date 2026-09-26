#pragma once
#include "../../AssetCore/VansModelLod.h"
#include <filesystem>

namespace Vans
{
class VansAssetDatabase;
enum class VansModelLodBuildMode
{
    Inspect,
    Publish
};
// 作者态通用静态模型构建服务。无 Editor、PCG、场景或 Vulkan 依赖。
class VansModelLodBuilder
{
public:
    static bool Build(VansAssetDatabase& database,
        const std::vector<VansModelLodSourcePart>& parts, const VansModelLodSettings& settings,
        VansModelLodBuildMode mode, VansModelLodAsset& result, std::string& error);
};
}
