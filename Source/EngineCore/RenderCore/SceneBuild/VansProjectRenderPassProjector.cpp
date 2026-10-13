#include "VansProjectRenderPassProjector.h"
#include "VansSceneProjectResourceBuilder.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VansRenderThreadTransaction.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include <algorithm>
#include <memory>
#include <functional>
#include <unordered_set>

namespace VansGraphics
{
namespace
{
struct Candidate
{
    explicit Candidate(Vans::VansSceneResourceLoadContext context):loadContext(std::move(context)) {}
    VansScene* scene=nullptr;
    VansProjectRenderPassState state;
    VansProjectRenderPassFrameInput frame;
    Vans::VansSceneResourceBuildPlan resources;
    Vans::VansSceneResourceLoadContext loadContext;
    std::uint32_t width=0,height=0;
    std::uint64_t epoch=0;
    std::string error;
};
}
class VansProjectRenderPassProjector::Transaction final:public IVansRenderThreadTransaction
{
public:
    explicit Transaction(std::shared_ptr<Candidate> candidate):m_Candidate(std::move(candidate)) {}
    bool Execute(VansGraphicsDevice& backend) override
    {
        VANS_ASSERT_RENDER_THREAD();
        auto* device=dynamic_cast<VansVKDevice*>(&backend);
        auto& candidate=*m_Candidate;
        if(!device){candidate.error="Project Pass requires a Vulkan device";return false;}
        std::vector<std::unique_ptr<VansAsset>> newMeshes;
        std::vector<std::unique_ptr<VansAsset>> newTextures;
        std::vector<std::string> newShaders;
        bool committed=false;
        struct Rollback
        {
            std::function<void()> action;bool& committed;
            ~Rollback(){if(!committed)action();}
        } rollback{[&] {
            device->WaitForDevice();
            for(auto& mesh:newMeshes)candidate.scene->m_AssetRegistry.RemoveMesh(mesh.get());
            for(auto& texture:newTextures)candidate.scene->m_AssetRegistry.RemoveTexture(texture.get());
            for(const auto& name:newShaders)VansShaderManager::Get().RemoveUnpublishedShaderAtIdle(name);
            candidate.scene->SyncShaderAssetsFromShaderManager();
        },committed};
        std::unordered_set<std::string> meshGuids,shaderGuids,textureGuids;
        for(const auto& pass:candidate.frame.configuration->passes)
        {
            if(pass.definition.instanceMesh.IsValid())meshGuids.insert(pass.definition.instanceMesh.ToString());
            for(const auto& guid:pass.definition.shaders)shaderGuids.insert(guid.ToString());
            for(const auto& texture:pass.definition.textures)textureGuids.insert(texture.asset.ToString());
        }
        auto native=device->GetLogicDevice();
        for (const auto& texture : candidate.resources.textures)
        {
            if (!textureGuids.count(texture.assetGuid)) continue;
            if (auto* existing = candidate.scene->GetTextureAsset(texture.name))
            {
                if (candidate.scene->m_AssetRegistry.FindTextureByGuid(texture.assetGuid) != existing)
                { candidate.error = "Project Texture runtime name belongs to another asset: " + texture.name; return false; }
                continue;
            }
            if (!VansSceneProjectResourceBuilder::LoadTextures(*candidate.scene, {texture}, candidate.loadContext, device, false))
            { candidate.error = "Project Pass candidate Texture loading failed: " + texture.assetGuid; return false; }
            newTextures.emplace_back(candidate.scene->GetTextureAsset(texture.name));
        }
        for(const auto& mesh:candidate.resources.meshes)
        {
            if(!meshGuids.count(mesh.assetGuid))continue;
            if(!candidate.scene->GetMeshAsset(mesh.name))
            {
                const auto resolved=candidate.loadContext.ResolveMesh(mesh);
                if(!resolved.artifactAvailable){candidate.error="Project mesh requires its prepared runtime artifact: "+mesh.assetGuid;return false;}
                if(!VansSceneProjectResourceBuilder::LoadMeshes(*candidate.scene,{mesh},candidate.loadContext,native,device))
                {candidate.error="Project Pass candidate mesh loading failed: "+mesh.assetGuid;return false;}
                newMeshes.emplace_back(candidate.scene->GetMeshAsset(mesh.name));
            }
        }
        std::vector<Vans::VansSceneShaderResourceRequest> shaders;
        for(const auto& shader:candidate.resources.shaders)
        {
            if(!shaderGuids.count(shader.assetGuid))continue;
            if(VansShaderManager::Get().FindGraphicsShader(shader.name))
            {
                const auto* entry=VansShaderManager::Get().FindShaderEntry(shader.name);
                if(!entry||entry->assetGuid!=shader.assetGuid)
                {candidate.error="Project Shader runtime name belongs to another asset: "+shader.name;return false;}
                continue;
            }
            // 注册名已被其他种类/未就绪 Shader 使用时，不覆盖已有记录。
            if(VansShaderManager::Get().FindShaderEntry(shader.name))
            {candidate.error="Project Shader runtime name is already registered but unavailable: "+shader.name;return false;}
            shaders.push_back(shader);newShaders.push_back(shader.name);
        }
        if(!shaders.empty()&&!VansSceneProjectResourceBuilder::RegisterShaders(*candidate.scene,shaders,candidate.loadContext,native,true))
        {candidate.error="Project Pass candidate Shader loading failed";return false;}
        if(!device->ProjectRenderPassRuntime().Prepare(*device,*candidate.scene,candidate.frame,device->GetEffectiveFramesInFlight(),
            {candidate.width,candidate.height},candidate.epoch,candidate.error))return false;
        for(auto& mesh:newMeshes)mesh.release();
        for(auto& texture:newTextures)texture.release();
        candidate.scene->SyncShaderAssetsFromShaderManager();
        committed=true;
        return true;
    }
private:
    std::shared_ptr<Candidate> m_Candidate;
};
bool VansProjectRenderPassProjector::Apply(VansScene& scene,IVansRenderThreadTransactionExecutor& executor,
    const Vans::VansSceneResourceBuildPlan& resources,Vans::VansSceneResourceLoadContext loadContext,
    std::uint32_t width,std::uint32_t height,std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    auto candidate=std::make_shared<Candidate>(std::move(loadContext));
    candidate->scene=&scene;candidate->state=scene.ProjectRenderPasses();
    if(!candidate->state.Configure(resources,error))return false;
    candidate->frame=candidate->state.CaptureFrame();candidate->resources=resources;
    scene.CollectProjectRenderPassDraws(candidate->frame);
    candidate->width=width;candidate->height=height;
    candidate->epoch=scene.GetRenderSceneEpoch();
    if(!executor.ExecuteRenderThreadTransaction(std::make_unique<Transaction>(candidate)))
    {error=candidate->error.empty()?"Project Pass preparation failed or render safe point is unavailable":candidate->error;return false;}
    scene.ProjectRenderPasses()=std::move(candidate->state);
    error.clear();return true;
}
}
