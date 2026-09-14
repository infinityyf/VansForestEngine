#include "EngineAPIImpl.h"
#include "VansMaterialLiveEditService.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../AssetCore/VansMaterialAuthoringAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderThreadTransaction.h"
#include "../../RenderCore/SceneBuild/VansSceneProjectResourceBuilder.h"
#include "../../RenderCore/VegetationCore/VansVegetationCollection.h"
#include "../../RenderCore/VegetationCore/VansVegetationSystem.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include <algorithm>
#include <functional>

namespace Vans::EditorAPI
{
namespace
{
class PcgPreviewTransaction final : public VansGraphics::IVansRenderThreadTransaction
{
public:
    explicit PcgPreviewTransaction(std::function<bool()> apply):m_Apply(std::move(apply)) {}
    bool Execute(VansGraphics::VansGraphicsDevice&) override {return m_Apply();}
private:
    std::function<bool()> m_Apply;
};
}
PcgEditorOperationResult EngineAPIImpl::CommitPcgPreview(std::shared_ptr<const VansPcgBatchUpdate> update)
{
    if (!m_Scene) return {true,{}};
    using namespace VansGraphics;
    auto& scene=*static_cast<VansScene*>(m_Scene);
    auto& manager=VansProjectManager::Get();
    auto* database=manager.GetAssetDatabase();
    if (!database) return {false,"The PCG resource index is unavailable."};
    // 只投影植被依赖，不重新加载场景、音频、游戏逻辑或其他渲染模块。
    const auto root=VansSerializedValue::Object({
        {"schemaVersion",VansSerializedValue::Int(2)},{"entities",VansSerializedValue::Array({})},
        {"settings",VansSerializedValue::Object({{"vegetation",VansSerializedValue::Object({
            {"asset",VansSerializedValue::Object({{"guid",VansSerializedValue::String(m_PcgSceneRecipeGuid)}})}
        })}})}});
    const auto path=m_PcgSceneDocument?m_PcgSceneDocument->SourcePath():database->AssetsRoot();
    auto dependencies=VansSceneAssetDependencyBuilder::BuildResourcePlan(*database,root,path,{},
        manager.GetAssetObjectRepository(),manager.GetBuiltInAssetDatabase());
    if (!dependencies.success) return {false,dependencies.errors.empty()?"PCG dependencies could not be resolved.":dependencies.errors.front()};
    auto resources=std::move(dependencies.resourcePlan);
    const auto context=VansSceneResourceLoadContext::ForEditor(manager.GetProjectRootPath(),{},
        manager.EnumerateAssetRecords());
    // 完整配置修改使用一次低频、GPU 空闲的结构事务。连续笔画仍使用帧局部载荷。
    std::string error;
    const auto apply=[&]() {
        auto* device=scene.GetRuntimeResourceDevice();
        if (!device || !scene.GetVegetationCollection()) {error="The scene vegetation renderer is unavailable.";return false;}
        VkDevice native=device->GetLogicDevice();
        std::vector<VansSceneMeshResourceRequest> meshes;
        for (auto request:resources.meshes) {
            auto* loaded=static_cast<VansMesh*>(scene.FindMeshAsset(request.name));
            const auto hasCpu=[](const VansMesh* mesh) {
                if (!mesh) return false;
                if (!mesh->m_IsMultiMesh) return !mesh->GetMeshRawPositionData().empty();
                return std::all_of(mesh->m_SubMeshes.begin(),mesh->m_SubMeshes.end(),[](const auto* part){
                    return part && !part->GetMeshRawPositionData().empty();
                });
            };
            if (loaded && (!request.needCpuData || hasCpu(loaded))) continue;
            if (loaded) {
                // 其他模块已持有这个模型；为草蒙皮保留单独的 CPU 几何副本，不替换已有指针。
                request.name="pcg-cpu/"+request.assetGuid;
                if (scene.FindMeshAsset(request.name)) continue;
            }
            request.supportRayTracing=false;
            meshes.push_back(std::move(request));
        }
        std::vector<VansSceneTextureResourceRequest> textures;
        for (const auto& request:resources.textures)
            if (!scene.FindTextureAssetByGuid(request.assetGuid)) textures.push_back(request);
        std::vector<VansSceneShaderResourceRequest> shaders;
        for (const auto& request:resources.shaders)
            if (!scene.FindShaderAsset(request.name)) shaders.push_back(request);
        for (const auto& guid:dependencies.requiredMaterials)
            if (!scene.FindMaterialAsset(guid)) {
                error="This material was added after the scene opened. Reopen the scene to initialize it: "+guid;
                return false;
            }
        if (!VansSceneProjectResourceBuilder::LoadMeshes(scene,meshes,context,native,device) ||
            (!textures.empty() && !VansSceneProjectResourceBuilder::LoadTextures(scene,textures,context,device,false))) {
            error="A selected PCG model or texture could not be loaded.";return false;
        }
        if (!shaders.empty()) VansSceneProjectResourceBuilder::RegisterShaders(scene,shaders,context,native,false);
        scene.FinalizeProjectResourceBatch();
        if (!textures.empty()) {
            VansMaterialLiveEditService live;
            for (const auto& text:dependencies.requiredMaterials) {
                VansAssetGuid guid;VansAssetGuid::TryParse(text,guid);
                const auto material=manager.GetAssetObjectRepository().ResolveLatest<VansMaterialAuthoringAsset>(guid);
                if (!material || material->textures.kind!=VansSerializedValue::Kind::Object) continue;
                for (const auto& entry:material->textures.objectFields) {
                    const auto textureGuid=ReadSerializedStringField(entry.second,"guid");
                    if (!textureGuid.empty()) live.ApplyMaterialTexture(&scene,text,entry.first,textureGuid);
                }
            }
        }
        if (!scene.GetVegetationCollection()->Apply(scene,native,*update,nullptr,error)) return false;
        scene.GetVegetationCollection()->ForEach([](VansVegetationSystem& system){
            for (const auto& part:system.GetRenderConfigsGPU()) {
                auto* material=static_cast<VansGrassMaterial*>(part.material);
                if (material && material->m_GrassOwnedLayout==VK_NULL_HANDLE) material->BuildGrassTextureDescriptors();
            }
        });
        return true;
    };
    try {
        if (!scene.ExecuteRenderThreadTransaction(std::make_unique<PcgPreviewTransaction>(apply)))
            return {false,error.empty()?"PCG preview could not run at this frame boundary. Apply again after the scene is ready.":error};
    } catch (const std::exception& exception) {return {false,exception.what()};}
    scene.DiscardPendingVegetationUpdates();
    return {true,{}};
}
}
