#pragma once
#include "../../AssetCore/VansModelLod.h"
#include <filesystem>

namespace Vans
{
class VansAssetDatabase;
// 编辑器专用通用静态模型构建服务。无 PCG、场景或 Vulkan 依赖。
class VansModelLodBuilder
{
public:
    static bool Build(VansAssetDatabase& database,
        const std::vector<VansModelLodSourcePart>& parts, const VansModelLodSettings& settings,
        VansModelLodAsset& result, std::string& error);
};
}
