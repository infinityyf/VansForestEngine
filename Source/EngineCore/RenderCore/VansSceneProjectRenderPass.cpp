#include "VansScene.h"
#include "VansShaderManager.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../Util/VansProfiler.h"
#include <algorithm>
#include <unordered_set>

namespace VansGraphics
{
void VansScene::ExtractProjectRenderPassFrame(VansRenderSceneFrameSnapshot& snapshot, const glm::vec3& cameraPosition)
{
    VANS_ASSERT_MAIN_THREAD();
    snapshot.projectPasses = m_ProjectRenderPasses.CaptureFrame(cameraPosition);
    CollectProjectRenderPassDraws(snapshot.projectPasses);
}
void VansScene::CollectProjectRenderPassDraws(VansProjectRenderPassFrameInput& input) const
{
    VANS_ASSERT_MAIN_THREAD();
    const auto& configuration = input.configuration;
    if (!configuration || configuration->passes.empty()) return;
    VANS_PROFILE_SCOPE("ProjectPass::CollectSceneDraws", Vans::ProfileCategory::RenderPrepare);
    const std::array<std::pair<VansProjectSceneBucket,const std::vector<VansRenderNode*>*>,6> buckets{{
        {VansProjectSceneBucket::Opaque,&m_OpaqueRenderNodes}, {VansProjectSceneBucket::ForwardOpaque,&m_ForwardOpaquePreAtmosphereRenderNodes},
        {VansProjectSceneBucket::Transparent,&m_TransParentRenderNodes}, {VansProjectSceneBucket::Hair,&m_HairRenderNodes},
        {VansProjectSceneBucket::Decal,&m_DecalRenderNodes}, {VansProjectSceneBucket::PostProcess,&m_PostProcessRenderNodes}}};
    for (std::size_t pi = 0; pi < configuration->passes.size(); ++pi)
    {
        const auto& pass = configuration->passes[pi]; auto& frame = input.passes[pi];
        frame.filter = {};
        if (!frame.enabled || pass.definition.source != Vans::VansRenderPassSource::Scene) continue;
        std::vector<VansGraphicsShader*> shaders;
        for (const auto& name : pass.shaderNames) shaders.push_back(VansShaderManager::Get().FindGraphicsShader(name));
        std::unordered_set<const VansRenderNode*> visited;
        for (const auto& [bucket,nodes] : buckets) for (std::size_t ni = 0; ni < nodes->size(); ++ni)
        {
            const auto* node = (*nodes)[ni];
            if (!node || !visited.insert(node).second) continue;
            ++frame.filter.candidates;
            if (!node->IsEnabled() || !node->m_Material) { ++frame.filter.inactive; continue; }
            std::size_t selected = shaders.size();
            for (std::size_t si = 0; si < shaders.size(); ++si)
                if (shaders[si] && std::any_of(node->m_Material->m_PassShaders.begin(),node->m_Material->m_PassShaders.end(),
                    [&](const auto& binding) { return binding.second == shaders[si]; })) { selected = si; break; }
            if (selected == shaders.size()) { ++frame.filter.unmatchedShader; continue; }
            if (!pass.definition.layers.empty() && std::find(pass.definition.layers.begin(),pass.definition.layers.end(),node->m_RenderLayer) == pass.definition.layers.end())
            { ++frame.filter.excludedLayer; continue; }
            const auto* emissive = dynamic_cast<const VansEmissiveMaterial*>(node->m_Material);
            const auto proxy = FindMainRenderProxyHandle(node);
            if (!proxy.IsValid()) { ++frame.filter.missingProxy; continue; }
            if (node->m_HasSkeletonBone || node->m_AnimationEnabled ||
                node->m_Material->m_MaterialType == VAN_CLOTH || node->m_Material->m_MaterialType == VAN_HAIR ||
                (emissive && !emissive->m_VideoName.empty())) { ++frame.filter.unsupportedDynamic; continue; }
            if (frame.sceneDraws.size() == pass.definition.maxDraws) { ++frame.filter.capacityExceeded; continue; }
            frame.sceneDraws.push_back({proxy,bucket,static_cast<std::uint32_t>(ni),static_cast<std::uint32_t>(selected)});
        }
    }
}
}
