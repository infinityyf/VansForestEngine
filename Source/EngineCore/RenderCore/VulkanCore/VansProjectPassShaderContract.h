#pragma once
#include "../VansProjectRenderPassState.h"
#include "VansShader.h"
#include <map>

namespace VansGraphics
{
struct VansProjectPassShaderSignature
{
    struct VertexInput { std::uint32_t location=0; VkFormat format=VK_FORMAT_UNDEFINED; };
    std::vector<VertexInput> vertexInputs;
    std::vector<std::uint32_t> globalBindings;
    std::uint64_t binaryHash = 0;
};
bool ValidateProjectPassShader(const Vans::VansRenderPassAsset& definition,
    const VansGraphicsShader& shader, std::uint32_t parameterBytes,
    VansProjectPassShaderSignature& signature, std::string& error);
bool ValidateProjectPassShaderSpirv(const Vans::VansRenderPassAsset& definition,
    const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stages,
    std::uint32_t parameterBytes, VansProjectPassShaderSignature& signature, std::string& error);
}
