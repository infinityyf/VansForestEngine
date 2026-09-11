#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VulkanCore/VansRenderPassCatalog.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>

void InitAttachmentBlendStates(std::vector<VkPipelineColorBlendAttachmentState>&, bool, bool, bool, bool, bool, int, uint32_t);

namespace
{
bool Check(bool result, const char* message)
{
    if (!result) std::cerr << "[DecalContract] " << message << '\n';
    return result;
}
std::filesystem::path EngineRoot()
{
    auto root = std::filesystem::current_path();
    while (!root.empty())
    {
        if (std::filesystem::exists(root / "EngineAssets/Shaders/Decal/Decal.frag")) return root;
        if (std::filesystem::exists(root / "ForestEngine/EngineAssets/Shaders/Decal/Decal.frag")) return root / "ForestEngine";
        if (root == root.root_path()) break;
        root = root.parent_path();
    }
    throw std::runtime_error("Cannot locate engine shader sources");
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Missing contract source: " + path.string());
    return { std::istreambuf_iterator<char>(file), {} };
}
}

bool TestDecalRenderingContract()
{
    using namespace VansGraphics;
    std::vector<VkPipelineColorBlendAttachmentState> states;
    InitAttachmentBlendStates(states, false, false, true, false, false, -1, 0);
    if (!Check(states.size() == 3, "Modifier MRT count must be three")) return false;
    for (const auto& state : states)
        if (!Check(state.blendEnable && state.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA &&
            state.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
            state.srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE &&
            state.dstAlphaBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
            state.colorWriteMask == 15, "Modifier blend must retain accumulated coverage")) return false;

    // 重叠两层后 coverage=0.75，预乘结果与按层直接合成一致。
    const auto over = [](glm::vec4 dst, glm::vec4 src) {
        return glm::vec4(glm::vec3(src) * src.a + glm::vec3(dst) * (1-src.a), src.a + dst.a*(1-src.a));
    };
    const auto aggregate = over(over(glm::vec4(0), glm::vec4(1,0,0,0.5f)), glm::vec4(0,1,0,0.5f));
    if (!Check(aggregate == glm::vec4(0.25f,0.5f,0,0.75f), "Overlap accumulation lost a layer")) return false;

    VansMaterialManager manager;
    VansDecalMaterial decal;
    decal.m_MaterialIndex = 0;
    manager.m_GlobalCustomMaterialParamData.emplace_back();
    manager.m_GlobalCustomMaterialParamData[0].textureIndices = {11,12,13,14};
    if (!Check(manager.ApplyMaterialParameter(decal, "opacity", 0.4f) &&
        manager.ApplyMaterialParameter(decal, "colorWeight", 0.0f) &&
        manager.ApplyMaterialParameter(decal, "roughnessWeight", 0.0f) &&
        manager.ApplyMaterialParameter(decal, "sortPriority", -4.0f), "Decal live parameters rejected")) return false;
    if (!Check(!manager.ApplyMaterialParameter(decal, "metallic", 0.4f) &&
        !manager.ApplyMaterialParameter(decal, "ao", 0.4f), "Obsolete PBR payload path remains")) return false;
    auto frame = manager.CaptureRenderMaterialFrameData({&decal});
    VansCustomMaterialPayload captured;
    std::memcpy(&captured, frame.custom.bytes.data(), sizeof(captured));
    if (!Check(captured.values[0].a == 0.4f && captured.values[1].y == 0 && captured.values[1].w == 0 &&
        captured.values[2].x == -4 && captured.textureIndices == glm::ivec4(11,12,13,14),
        "Frame capture lost independent weights, priority or texture indices")) return false;
    manager.InitializeRuntimeMaterialPools(0, 1, nullptr);
    auto* instance = manager.AcquireRuntimeMaterialInstance("decal-contract", decal, VK_NULL_HANDLE);
    if (!Check(instance && instance->m_MaterialIndex == 1 &&
        manager.ApplyMaterialParameter(*instance, "opacity", 0.9f) &&
        manager.m_GlobalCustomMaterialParamData[0].values[0].a == 0.4f &&
        manager.m_GlobalCustomMaterialParamData[1].values[0].a == 0.9f,
        "Runtime decal instance must use the custom pool without altering its source")) return false;
    manager.ClearRuntimeMaterialInstances();

    VansRenderFeatureFrameFlags flags;
    for (bool enabled : {false,true})
    {
        flags.hasDecal = enabled;
        VansRenderFramePlan plan;
        VansRenderPassCatalog::BuildCompatibilityFramePlan(plan, flags, 1);
        const auto* pass = plan.FindPass(VansRenderPassNames::Decal);
        const auto* lighting = plan.FindPass(VansRenderPassNames::RawOpaqueLighting);
        if (!Check(lighting && (pass != nullptr) == enabled, "Decal skip condition lost")) return false;
        if (!enabled) continue;
        for (const auto& output : pass->writes)
            if (!Check(output.name == "DecalColor" || output.name == "DecalNormal" || output.name == "DecalRoughness",
                "Decal may not write any receiver GBuffer")) return false;
        if (!Check(pass->writes.size() == 3 && pass->reads.size() == 4, "Incomplete decal graph contract")) return false;
    }
    const auto root = EngineRoot();
    // 图形程序会扫描所属目录中的 shader stage，GPU 合约只能放在 Validation 下。
    for (const auto& source : std::filesystem::directory_iterator(root / "EngineAssets/Shaders/Decal"))
        if (!Check(source.path().extension() != ".comp", "Compute contract shader leaked into the decal graphics program")) return false;
    const auto renderPass = Read(root / "Source/EngineCore/RenderCore/VulkanCore/VansRenderPass.cpp");
    const auto begin = renderPass.find("void VansGraphics::VansRenderPassManager::SetupVansDecalRenderPass(");
    const auto end = renderPass.find("void VansGraphics::VansRenderPassManager::SetupVansScreenSpaceEffectsPass(", begin);
    const auto decalPass = renderPass.substr(begin, end-begin);
    if (!Check(decalPass.find("m_NormalImage") == std::string::npos && decalPass.find("m_GBufferImage") == std::string::npos &&
        decalPass.find("VK_ATTACHMENT_LOAD_OP_CLEAR") != std::string::npos &&
        decalPass.find("VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL") != std::string::npos,
        "Decal attachments must be independently cleared color images")) return false;
    const auto node = Read(root / "Source/EngineCore/RenderCore/VansRenderNode.cpp");
    if (!Check(node.find("frameBufferInputDescriptorSets[hasDecal ? 0 : 1]") != std::string::npos &&
        node.find("rp->GetEmptyDecal()") != std::string::npos, "Skipped pass must bind zero modifiers")) return false;
    std::cout << "[DecalContract] PASS material/frame/runtime instance, blend, graph, attachments and skip contracts\n";
    return true;
}

std::filesystem::path DecalContractShaderPath() { return EngineRoot() / "EngineAssets/Validation/Decal/DecalContractcomp.spv"; }
