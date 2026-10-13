#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../AuthoringCore/IVansSceneAuthoringHost.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderSystem.h"
#include "../../RenderCore/SceneBuild/VansProjectRenderPassProjector.h"
#include "../../RenderCore/SceneBuild/VansSceneResourceArtifactPrewarmer.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include <unordered_set>

namespace Vans::EditorAPI
{
ProjectRenderPassSettingsSnapshot EngineAPIImpl::GetProjectRenderPassSettings() const
{
    VANS_ASSERT_MAIN_THREAD();
    ProjectRenderPassSettingsSnapshot result;
    const auto& project=VansProjectManager::Get();
    for(const auto& guid:project.GetProjectSettings().GetProjectRenderPasses())result.assetGuids.push_back(guid.ToString());
    result.available=project.IsProjectLoaded()&&m_Scene&&m_RenderSystem;
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    if(scene&&scene->ProjectRenderPasses().Configuration())result.configuredRevision=scene->ProjectRenderPasses().Configuration()->revision;
    if(auto* device=dynamic_cast<VansGraphics::VansVKDevice*>(static_cast<VansGraphics::VansGraphicsDevice*>(m_Device)))
    {
        const auto runtime=device->ProjectRenderPassRuntime().CaptureSnapshot();
        result.generation=runtime.generation;result.allocatedBytes=runtime.allocatedBytes;
        result.passCount=runtime.passCount;result.draws=runtime.draws;result.runtimeError=runtime.error;
        for(const auto& pass:runtime.passes)
        {
            ProjectRenderPassFrameDiagnostic diagnostic;
            diagnostic.passId=pass.passId;diagnostic.shaders=pass.shaders;
            diagnostic.enabled=pass.enabled;diagnostic.submitted=pass.submitted;
            diagnostic.commandSlot=pass.commandSlot;diagnostic.targetSlot=pass.targetSlot;
            diagnostic.batches=pass.batches;diagnostic.instances=pass.instances;diagnostic.imageBarriers=pass.imageBarriers;
            diagnostic.candidates=pass.filter.candidates;diagnostic.inactive=pass.filter.inactive;
            diagnostic.unmatchedShader=pass.filter.unmatchedShader;diagnostic.excludedLayer=pass.filter.excludedLayer;
            diagnostic.missingProxy=pass.filter.missingProxy;diagnostic.unsupportedDynamic=pass.filter.unsupportedDynamic;
            diagnostic.capacityExceeded=pass.filter.capacityExceeded;diagnostic.cameraCulled=pass.filter.cameraCulled;
            diagnostic.uploadedBytes=pass.uploadedBytes;diagnostic.payloadBytes=pass.payloadBytes;
            result.passes.push_back(std::move(diagnostic));
        }
    }
    return result;
}
ApplyProjectRenderPassSettingsResult EngineAPIImpl::ApplyProjectRenderPassSettings(const std::vector<std::string>& ids)
{
    VANS_ASSERT_MAIN_THREAD();
    ApplyProjectRenderPassSettingsResult result;
    auto& project=VansProjectManager::Get();
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    auto* document=m_SceneAuthoringHost?m_SceneAuthoringHost->SceneDocument():nullptr;
    if(!project.IsProjectLoaded()||!scene||!m_RenderSystem||!document||!project.GetAssetDatabase())
    {result.error="A loaded project, scene document and render safe point are required";return result;}
    if(ids.size()>64){result.error="Project Pass count exceeds 64";return result;}
    std::vector<VansAssetGuid> roots;
    for(const auto& id:ids)
    {
        VansAssetGuid guid;
        if(!VansAssetGuid::TryParse(id,guid)||!guid.IsValid()){result.error="Invalid project Pass GUID: "+id;return result;}
        roots.push_back(guid);
    }
    auto settings=project.GetProjectSettings();
    if(!settings.SetProjectRenderPasses(roots,result.error))return result;
    const auto snapshot=document->CreateSnapshot();
    const auto dependencies=VansSceneAssetDependencyBuilder::BuildResourcePlan(*project.GetAssetDatabase(),snapshot.Root(),
        snapshot.sourcePath,project.GetConfig().runtimeAssetBindings,project.GetAssetObjectRepository(),project.GetBuiltInAssetDatabase(),roots);
    if(!dependencies.success)
    {
        result.error="Project Pass dependency planning failed";
        for(const auto& error:dependencies.errors)result.error+="\n"+error;
        return result;
    }
    VansSceneResourceBuildPlan artifacts;
    artifacts.includeDefaultTextureSet=false;artifacts.loadRegisteredShaders=false;
    std::unordered_set<std::string> meshGuids,shaderGuids;
    for(const auto& pass:dependencies.resourcePlan.projectRenderPasses)
    {
        if(pass.definition.instanceMesh.IsValid())meshGuids.insert(pass.definition.instanceMesh.ToString());
        for(const auto& guid:pass.definition.shaders)shaderGuids.insert(guid.ToString());
    }
    for(const auto& mesh:dependencies.resourcePlan.meshes)if(meshGuids.count(mesh.assetGuid))artifacts.meshes.push_back(mesh);
    for(const auto& shader:dependencies.resourcePlan.shaders)if(shaderGuids.count(shader.assetGuid))artifacts.shaders.push_back(shader);
    auto* builtIn=project.GetBuiltInAssetDatabase();
    if(!builtIn){result.error="Built-in asset index is unavailable";return result;}
    const auto prewarm=VansGraphics::VansSceneResourceArtifactPrewarmer::Prewarm(project.GetProjectRootPath(),
        *project.GetAssetDatabase(),*builtIn,artifacts);
    if(!prewarm.Succeeded())
    {result.error="Project Pass runtime artifact preparation failed";for(const auto& error:prewarm.errors)result.error+="\n"+error;return result;}
    const auto context=VansSceneResourceLoadContext::ForEditor(project.GetProjectRootPath(),project.GetPathResolver().GetEngineRoot(),project.EnumerateAssetRecords());
    if(!VansGraphics::VansProjectRenderPassProjector::Apply(*scene,*m_RenderSystem,dependencies.resourcePlan,context,
        m_RenderSystem->GetRenderWidth(),m_RenderSystem->GetRenderHeight(),result.error))return result;
    // GUID/容量已在前置验证通过；同步事务返回后 Main 立即发布项目文档根，不写盘。
    result.accepted=project.SetProjectRenderPasses(roots,result.error);
    return result;
}
}
