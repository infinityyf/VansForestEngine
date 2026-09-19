#pragma once

#include <string>
#include <vector>

namespace Vans
{
// LOD0 由挂载对象的 ModelRenderer 提供；每个后续级别保存与 RenderNode
// 子网格顺序对应的派生模型 GUID。该配置只保存资源引用与选择参数。
struct VansSceneLodGroupLevelConfig
{
    std::vector<std::string> modelGuids;
    std::vector<float> errors;
    float screenHeight = 0.0f;
};

struct VansSceneLodGroupComponentConfig
{
    std::string mode = "autoScreenError";
    float pixelErrorBudget = 1.0f;
    float qualityBias = 1.0f;
    float hysteresis = 0.1f;
    std::vector<VansSceneLodGroupLevelConfig> levels;
};
}
