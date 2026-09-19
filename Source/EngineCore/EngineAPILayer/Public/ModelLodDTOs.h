#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace Vans::EditorAPI
{
struct ModelLodSourcePart { std::string model,material; int submesh=-1; bool alphaTest=false; };
struct ModelLodBuildRequest
{
    // 编辑器可只提供实体 GUID；运行时场景会从其 RenderNode 识别源模型、材质和子网格。
    std::string entityGuid;
    std::vector<ModelLodSourcePart> parts;
    std::array<float,2> ratios{.5f,.18f};
    float maximumError=.04f;
};
struct ModelLodPart { std::string model,material;int submesh=-1;uint32_t sourcePart=0,triangleCount=0;float error=0; };
struct ModelLodLevel { std::vector<ModelLodPart> parts; };
struct ModelLodBuildResult
{
    bool success=false;
    std::string message,buildKey;
    std::array<float,4> centerRadius{};
    std::vector<ModelLodLevel> levels;
};
// ModelLodPart::error is the meshoptimizer normalized error. Consumers that
// feed RenderCore selection must multiply it by centerRadius[3].
}
