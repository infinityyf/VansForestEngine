#include "PcgAssetContractTests.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentRegistry.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentEditService.h"
#include "../EngineCore/AuthoringCore/VansAuthoringAssetCreationService.h"
#include "../EngineCore/AuthoringCore/Pcg/VansPcgSplineAuthoringSession.h"
#include "../EngineCore/AuthoringCore/Pcg/VansPcgTerrainPreviewScheduler.h"
#include "../EngineCore/PcgCore/Serialization/VansPlantTypeAssetCodec.h"
#include "../EngineCore/PcgCore/Serialization/VansPcgSplineAssetCodec.h"
#include "../EngineCore/PcgCore/VansPcgMaskAsset.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/EditorCore/VansEditorAssetSaveService.h"
#include "../EngineCore/SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
class ContractAuthoringSaveHost final : public Vans::IVansAuthoringSaveHost
{
public:
    explicit ContractAuthoringSaveHost(Vans::EditorAPI::EngineAPIImpl& api) : m_Api(api) {}

    Vans::VansAuthoringSaveResult SaveAssetDocument(
        const std::shared_ptr<Vans::VansOpenAssetDocument>& document) override
    {
        const Vans::VansAssetSaveResult saved =
            Vans::VansEditorAssetSaveService::Get().SaveAsset(m_Api, document);
        return { static_cast<bool>(saved), saved.message };
    }

private:
    Vans::EditorAPI::EngineAPIImpl& m_Api;
};

class ContractSceneAuthoringHost final : public Vans::IVansSceneAuthoringHost
{
public:
    ContractSceneAuthoringHost(
        Vans::VansSceneDocument& document,
        Vans::VansSceneEditService& edits)
        : m_Document(document), m_Edits(edits) {}

    Vans::VansSceneDocument* SceneDocument() const override { return &m_Document; }

    Vans::VansSceneAuthoringResult SetSceneValue(
        const Vans::DocumentPropertyPath& path,
        Vans::VansSerializedValue value) override
    {
        const Vans::SceneEditResult edited = m_Edits.Set(path, std::move(value));
        return { edited.success, edited.message };
    }

private:
    Vans::VansSceneDocument& m_Document;
    Vans::VansSceneEditService& m_Edits;
};
}

bool RunPcgEditorConfigurationContractTests()
{
    using namespace Vans;
    using namespace Vans::EditorAPI;
    namespace fs=std::filesystem;
    const auto check=[](bool passed,const std::string& error) {
        if (!passed) std::cerr<<"[PcgConfiguration] "<<error<<'\n';
        return passed;
    };
	fs::path testSource=fs::path(__FILE__);
	if(testSource.is_relative())testSource=fs::absolute(testSource);
	const auto engineRoot=testSource.parent_path().parent_path().parent_path();
	const auto readSource=[](const fs::path& path) {
		std::ifstream input(path,std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
	};
	const auto apiPreview=readSource(engineRoot/"Source/EngineCore/EngineAPILayer/Private/EngineAPIImpl.PcgPreview.cpp");
	const auto renderProjector=readSource(engineRoot/"Source/EngineCore/RenderCore/PcgCore/VansPcgPreviewProjector.cpp");
	const auto apiSplines=readSource(engineRoot/"Source/EngineCore/EngineAPILayer/Private/EngineAPIImpl.PcgSplines.cpp");
	const auto authoringSplineSession=readSource(engineRoot/"Source/EngineCore/AuthoringCore/Pcg/VansPcgSplineAuthoringSession.cpp");
	const auto runtimeSplineSession=readSource(engineRoot/"Source/EngineCore/RenderCore/PcgCore/VansPcgSplinePreviewSession.cpp");
	const auto engineApi=readSource(engineRoot/"Source/EngineCore/EngineAPILayer/Private/EngineAPIImpl.cpp");
	const auto terrainPreviewScheduler=readSource(engineRoot/"Source/EngineCore/AuthoringCore/Pcg/VansPcgTerrainPreviewScheduler.h");
	const auto plantAssetSource=readSource(engineRoot/"Source/EngineCore/PcgCore/VansPlantTypeAsset.cpp");
	const auto runtimePolicy=readSource(engineRoot/"Source/EngineCore/PcgCore/VansPcgRuntimePolicy.h");
	const auto vegetationCollection=readSource(engineRoot/"Source/EngineCore/RenderCore/VegetationCore/VansVegetationCollection.cpp");
	const auto vegetationSystem=readSource(engineRoot/"Source/EngineCore/RenderCore/VegetationCore/VansVegetationSystem.h");
	const auto environmentBuilder=readSource(engineRoot/"Source/EngineCore/RenderCore/SceneBuild/VansSceneEnvironmentNodeBuilder.cpp");
	const auto pcgConfigurationWindow=readSource(engineRoot/"Source/EngineCore/EditorCore/Windows/VansPcgWindow.Configuration.cpp");
	const auto plantCodec=readSource(engineRoot/"Source/EngineCore/PcgCore/Serialization/VansPlantTypeAssetCodec.cpp");
	const auto recipeCodec=readSource(engineRoot/"Source/EngineCore/PcgCore/Serialization/VansPcgRecipeCodec.cpp");
	const auto configurationApi=readSource(engineRoot/"Source/EngineCore/EngineAPILayer/Private/EngineAPIImpl.PcgConfiguration.cpp");
	const std::array<const char*,7> forbiddenApiGpuTokens={
		"VulkanCore/","VkDevice","VansMesh","VansGrassMaterial","VK_NULL_HANDLE",
		"VansSceneProjectResourceBuilder","IVansRenderThreadTransaction"};
	if(!check(apiPreview.find("VansPcgPreviewProjector::Project")!=std::string::npos &&
		std::none_of(forbiddenApiGpuTokens.begin(),forbiddenApiGpuTokens.end(),
			[&](const char* token){return apiPreview.find(token)!=std::string::npos;}) &&
		renderProjector.find("ExecuteRenderThreadTransaction")!=std::string::npos &&
		renderProjector.find("VansSceneProjectResourceBuilder")!=std::string::npos &&
		renderProjector.find("VansGrassMaterial")!=std::string::npos,
		"PCG preview GPU projection escaped its RenderCore owner"))return false;
	const std::array<const char*,6> forbiddenApiSplineState={
		"PcgSplineAuthoringState","std::future","VansJobSystem","SplineBuildResult",
		"PublishSplineField","QueueVegetationUpdate"};
	if(!check(apiSplines.find("VansPcgSplineAuthoringSession")!=std::string::npos &&
		apiSplines.find("VansPcgSplinePreviewSession")!=std::string::npos &&
		std::none_of(forbiddenApiSplineState.begin(),forbiddenApiSplineState.end(),
			[&](const char* token){return apiSplines.find(token)!=std::string::npos;}) &&
		authoringSplineSession.find("VansAssetDocumentEditService")!=std::string::npos &&
		authoringSplineSession.find("VansJobSystem")==std::string::npos &&
		authoringSplineSession.find("RenderCore")==std::string::npos &&
		runtimeSplineSession.find("VansJobSystem")!=std::string::npos &&
		runtimeSplineSession.find("PublishSplineField")!=std::string::npos &&
		runtimeSplineSession.find("VansAssetDocumentRegistry")==std::string::npos,
		"PCG spline authoring or async preview state escaped its owning session"))return false;
	{
		VansPcgTerrainPreviewScheduler scheduler;
		const auto start=VansPcgTerrainPreviewScheduler::Clock::time_point{};
		std::uint32_t consumed=0;
		for(std::uint32_t notification=0;notification<60;++notification)
		{
			const auto now=start+std::chrono::milliseconds(notification*16);
			scheduler.NotifyHeightChanged(now);
			if(scheduler.IsRequestDue(false,now))consumed+=scheduler.MarkRequestSubmitted(now);
		}
		const auto finalTick=start+std::chrono::seconds(2);
		if(scheduler.IsRequestDue(false,finalTick))consumed+=scheduler.MarkRequestSubmitted(finalTick);
		scheduler.NotifyHeightChanged(finalTick+std::chrono::milliseconds(1));
		VansPcgTerrainPreviewScheduler busyScheduler;
		for(std::uint32_t notification=0;notification<60;++notification)
		{
			const auto now=start+std::chrono::milliseconds(notification*16);
			busyScheduler.NotifyHeightChanged(now);
			if(!check(!busyScheduler.IsRequestDue(true,now),
				"An in-flight terrain preview allowed another rebuild request"))return false;
		}
		const auto busyConsumed=busyScheduler.IsRequestDue(false,finalTick)
			?busyScheduler.MarkRequestSubmitted(finalTick):0;
		if(!check(consumed==60 && scheduler.PendingHeightChanges()==1 &&
			scheduler.SubmittedRequests()<=11 &&
			!scheduler.IsRequestDue(true,finalTick+std::chrono::seconds(1)) &&
			busyConsumed==60 && busyScheduler.SubmittedRequests()==1 &&
			engineApi.find("RequestPcgSplinePreview();")==std::string::npos &&
			engineApi.find("NotifyTerrainHeightChanged")!=std::string::npos &&
			terrainPreviewScheduler.find("MinimumRequestInterval")!=std::string::npos &&
			apiSplines.find("RequestPendingTerrainPreview")!=std::string::npos,
			"Terrain height changes are no longer losslessly coalesced behind the PCG preview throttle"))return false;
		std::cout<<"[PcgTerrainPreview] 60 notifications -> "<<scheduler.SubmittedRequests()
			<<" zero-build requests; 60 in-flight notifications -> "
			<<busyScheduler.SubmittedRequests()<<" follow-up request\n";
	}
	if(!check(plantAssetSource.find("ResolvePlantTreeRuntimeBounds")!=std::string::npos &&
		plantAssetSource.find("requires baked LOD resources")!=std::string::npos &&
		vegetationCollection.find("ResolvePlantTreeRuntimeBounds")!=std::string::npos &&
		vegetationCollection.find("variant->lod.levels.empty()?")==std::string::npos &&
		vegetationCollection.find("?variant->cullingRadius:")==std::string::npos,
		"Runtime-ready trees regained the cullingRadius fallback instead of requiring baked LOD bounds"))return false;
	if(!check(plantAssetSource.find("MinimumModelLodLevelCount")!=std::string::npos &&
		plantAssetSource.find("MaximumModelLodLevelCount")!=std::string::npos &&
		pcgConfigurationWindow.find("Simplified LOD levels")!=std::string::npos &&
		pcgConfigurationWindow.find("DragFloat2(\"Triangle ratios\"")==std::string::npos &&
		pcgConfigurationWindow.find("DragFloat2(\"Tree LOD distances")==std::string::npos,
		"Tree LOD configuration returned to a fixed two-element editor path"))return false;
	if(!check(runtimePolicy.find("MaximumExactPcgCellCoordinate")!=std::string::npos &&
		runtimePolicy.find("PcgGrassResidencyHysteresisCells")!=std::string::npos &&
		vegetationCollection.find("ResolvePcgGrassResidencyExitDistance")!=std::string::npos &&
		vegetationCollection.find("render.cullDistance+batch.cellSize")==std::string::npos &&
		vegetationSystem.find("std::optional<TreeLodRuntimeSettings>")!=std::string::npos &&
		vegetationSystem.find("m_TreeLodMidDistance=60.f")==std::string::npos &&
		environmentBuilder.find("effectiveTerrain->settings.terrainSize * 0.5f")!=std::string::npos &&
		environmentBuilder.find("const float terrainHalfSize = 512.0f")==std::string::npos,
		"PCG runtime policy returned to numeric literals, duplicate Tree defaults, or fixed water coverage"))return false;
	if(!check(plantCodec.find("ReadPcgConfigurationFields")!=std::string::npos &&
		plantCodec.find("WritePcgConfigurationFields")!=std::string::npos &&
		recipeCodec.find("ReadPcgConfigurationFields")!=std::string::npos &&
		recipeCodec.find("PlacementFloats")==std::string::npos &&
		configurationApi.find("ToPublicFields")!=std::string::npos &&
		configurationApi.find("FromPublicFields")!=std::string::npos &&
		pcgConfigurationWindow.find("ConfigurationFields(p.grassFields")!=std::string::npos &&
		pcgConfigurationWindow.find("p.grass.bladeHeight")==std::string::npos,
		"PCG configuration fields returned to hand-copied Codec, API, DTO, or ImGui paths"))return false;
    const auto directory=fs::temp_directory_path()/("ForestPcgEditor-"+VansAssetGuid::New().ToString());
    struct Cleanup {
        fs::path directory;
        ~Cleanup() {
            auto& registry=VansAssetDocumentRegistry::Get();registry.ClearWorkingCopyPublisher();registry.Clear();
            VansAssetDocumentEditService::ClearAllHistories();VansProjectManager::Get().CloseProject();
            std::error_code error;fs::remove_all(directory,error);
        }
    } cleanup{directory};
    fs::create_directories(directory/"Assets/Vegetation");fs::create_directories(directory/"Scenes");
    VansProjectConfig project;project.SetDefaults("PCG Configuration Contract");
    std::string error;
    const auto plantGuid=VansAssetGuid::New(),recipeGuid=VansAssetGuid::New(),splineGuid=VansAssetGuid::New();
    const auto plantPath=directory/"Assets/UserPlant.vplant",recipePath=directory/"Assets/Vegetation/UserVegetation.json";
    const auto splinePath=directory/"Assets/UserSpline.vpcgspline";
    const auto write=[&](const fs::path& path,VansAssetGuid guid,VansAssetType type,const VansSerializedValue& root) {
        VansAssetMeta meta;meta.guid=guid;meta.importer=VansAssetDatabase::ImporterFor(type);
        return VansJsonFileStorage::WriteAtomic(path,EncodeSerializedValueJson<nlohmann::ordered_json>(root),error) &&
            VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(path),meta,error);
    };
    VansPlantTypeAsset plant;plant.name="User tree";plant.category=VansPlantCategory::Tree;
    VansPlantVariant variant;variant.id="oak";variant.name="Oak";variant.geometry=VansPlantGeometry::Mesh;variant.weight=1;
    variant.parts={{"bark",VansPlantPartKind::Trunk,VansAssetGuid::New(),1,VansAssetGuid::New()},
        {"leaves",VansPlantPartKind::Leaves,VansAssetGuid::New(),0,VansAssetGuid::New()}};
    plant.variants={variant};
    VansPcgRecipeAsset recipe;recipe.name="User recipe";
    VansPcgRegion region;region.id="region";region.name="User region";region.bounds={{-8,-8},{8,8}};region.cellSize=4;
    VansPcgLayer layer;layer.id="first";layer.name="First";layer.category=VansPlantCategory::Tree;
    layer.plant=plantGuid;layer.densityMask=VansAssetGuid::New();layer.source=VansPcgSourceMode::Fixed;
    VansPcgAuthoredInstance fixed;fixed.id="authored";fixed.variant="oak";fixed.position={2,3,4};layer.fixedInstances={fixed};
    region.layers={layer};layer.id="second";layer.name="Second";layer.densityMask=VansAssetGuid::New();region.layers.push_back(layer);
    recipe.regions={region};
    VansPcgSplineAsset spline;spline.name="User spline";spline.terrain=VansAssetGuid::New();
    VansSerializedValue plantRoot,recipeRoot,splineRoot;
    {
        VansScopedIOContext scope(VansIODomain::Authoring,"PcgConfiguration.Fixture",true);
        const auto modelPath=directory/"Assets/Fixture.obj";
        const std::string geometry="o bark\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no leaves\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf 4 5 6\n";
        {std::ofstream output(modelPath);output<<geometry;}
        VansAssetMeta modelMeta;modelMeta.guid=plant.variants[0].parts[0].mesh;modelMeta.importer="ModelImporter";
        modelMeta.SetSerializedSettings(VansSerializedValue::Object({{"loadMultiMesh",VansSerializedValue::Bool(true)},{"scaleFactor",VansSerializedValue::Float(1)}}));
        plant.variants[0].parts[1].mesh=modelMeta.guid;
        if(!check(VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(modelPath),modelMeta,error),error))return false;
        for(size_t i=0;i<2;++i){
            const auto materialPath=directory/"Assets"/("Fixture"+std::to_string(i)+".mat");
            if(!check(write(materialPath,plant.variants[0].parts[i].material,VansAssetType::Material,VansSerializedValue::Object({})),error))return false;
        }
        if (!check(project.SaveToFile((directory/"ForestProject.json").string()) &&
            VansPlantTypeAssetCodec::Encode(plant,plantRoot,error) && VansVegetationConfigCodec::Encode(recipe,recipeRoot,error) &&
            VansPcgSplineAssetCodec::Encode(spline,splineRoot,error) &&
            write(plantPath,plantGuid,VansAssetType::PlantType,plantRoot) &&
            write(recipePath,recipeGuid,VansAssetType::VegetationConfig,recipeRoot) &&
            write(splinePath,splineGuid,VansAssetType::PcgSpline,splineRoot),error)) return false;
    }
    auto& manager=VansProjectManager::Get();manager.CloseProject();
    VansProjectOpenRequest openRequest;openRequest.m_ProjectRootPath=directory.string();
    openRequest.m_Options.m_UpdateRecentProjects=false;openRequest.m_Options.m_LoadProjectSettings=false;
    if (!check(manager.OpenProject(openRequest).m_Opened,"Cannot open the isolated editor fixture")) return false;
	{
		VansPcgSplineAuthoringSession session;
		session.ResetContext(directory.string(),17);
		if(!check(session.MatchesContext(directory.string(),17) &&
			session.BindAsset(splineGuid.ToString(),manager.GetAssetDatabase(),manager.GetAssetObjectRepository(),{}) &&
			session.SynchronizeDocument(manager.GetAssetObjectRepository(),true) &&
			session.Document() && session.WorkingAsset().name=="User spline",
			"Spline authoring session could not bind and decode its document"))return false;
		std::string sessionError;
		if(!check(session.BeginEdit(sessionError),sessionError))return false;
		auto draftSpline=session.WorkingAsset();draftSpline.name="Draft spline";
		if(!check(session.ReplaceDraft(draftSpline,sessionError) && session.Dragging() &&
			session.WorkingAsset().name=="Draft spline" && !session.Document()->IsDirty(),
			"Spline drag draft wrote the document or lost its working copy"))return false;
		session.CancelEdit();
		if(!check(!session.RequestPreview(manager.GetAssetObjectRepository(),{},sessionError) &&
			session.WorkingAsset().name=="User spline" && sessionError=="Spline terrain is unavailable.",
			"Spline cancel did not restore the document snapshot before preview validation"))return false;
		if(!check(session.BeginEdit(sessionError),sessionError))return false;
		draftSpline=session.WorkingAsset();draftSpline.name="Committed spline";
		if(!check(session.CommitAsset(draftSpline,sessionError) && !session.Dragging() &&
			session.Document()->IsDirty() && !fs::exists(splinePath.string()+".tmp"),
			"Spline commit did not remain an in-memory authoring transaction"))return false;
		const auto undone=VansAssetDocumentEditService::Undo(session.Document()->sourceDocument);
		if(!check(undone && session.SynchronizeDocument(manager.GetAssetObjectRepository(),true) &&
			session.WorkingAsset().name=="User spline",
			"Spline session did not follow document undo"))return false;
	}
	{
		const auto rollbackDirectory=directory/"Assets/CreationRollback";
		const auto rollbackPath=rollbackDirectory/"Plant.vplant";
		const auto rollbackGuid=VansAssetGuid::New();
		VansAuthoringAssetCreateItem item;
		item.sourcePath=rollbackPath;
		item.guid=rollbackGuid;
		item.type=VansAssetType::PlantType;
		item.serializedRoot=plantRoot;
		const auto rolledBack=VansAuthoringAssetCreationService::CreateBundle(
			*manager.GetAssetDatabase(),manager.GetAssetObjectRepository(),rollbackDirectory,
			{std::move(item)},"Pcg.ContractCreationRollback",
			[](std::string& message){message="forced finalize failure";return false;});
		const bool sourceRemoved=!fs::exists(rollbackPath);
		const bool metaRemoved=!fs::exists(VansAssetMeta::MetaPathFor(rollbackPath));
		const bool directoryRemoved=!fs::exists(rollbackDirectory);
		const auto tombstone=manager.GetAssetDatabase()->Find(rollbackGuid);
		const bool indexDetached=!manager.GetAssetDatabase()->Find(rollbackPath)
			&& tombstone && tombstone->state==VansAssetState::Missing;
		const bool memoryRemoved=!manager.GetAssetObjectRepository().ResolveLatest<VansPlantTypeAsset>(rollbackGuid);
		if (!check(!rolledBack && sourceRemoved && metaRemoved && directoryRemoved
			&& indexDetached && memoryRemoved,
			"Authoring bundle rollback state source="+std::to_string(sourceRemoved)
			+" meta="+std::to_string(metaRemoved)
			+" directory="+std::to_string(directoryRemoved)
			+" index="+std::to_string(indexDetached)
			+" memory="+std::to_string(memoryRemoved))) return false;
	}
    EngineAPIImpl api;
    ContractAuthoringSaveHost saveHost(api);
    api.BindAuthoringSaveHost(&saveHost);
    auto& registry=VansAssetDocumentRegistry::Get();registry.Clear();
    registry.SetWorkingCopyPublisher([&](const VansOpenAssetDocument& document,std::string& message) {
        AssetWorkingCopyPublishRequest request;request.sourcePath=document.sourcePath.string();request.sourceLoaded=true;request.metaLoaded=true;
        request.sourceCanonicalJson=EncodeSerializedValueJson<nlohmann::ordered_json>(document.sourceDocument.SerializedRootSnapshot()).dump();
        request.metaCanonicalJson=EncodeSerializedValueJson<nlohmann::ordered_json>(document.metaDocument.SerializedRootSnapshot()).dump();
        const auto result=api.PublishAssetWorkingCopy(request);message=result.message;return result.success;
    });
    const PcgBrushTarget target{recipeGuid.ToString(),"region","first",{}};
    auto draft=api.GetPcgPlantConfiguration(plantGuid.ToString());
    auto distribution=api.GetPcgLayerConfiguration(target);
    if (!check(draft.available && distribution.available && draft.variants.size()==1 && draft.variants.front().parts.size()==2,
        "Typed editor snapshot lost models or material parts")) return false;
    const auto* lodDistances=FindPcgConfigurationField(draft.renderFields,"lodDistances");
    const auto* shadowDistance=FindPcgConfigurationField(draft.renderFields,"cullDistance");
    if (!check(draft.grassFields.size()==VansPlantGrassConfigurationFields.size() &&
        draft.renderFields.size()==VansPlantRenderConfigurationFields.size() &&
        distribution.placementFields.size()==VansPcgPlacementConfigurationFields.size() &&
        lodDistances && lodDistances->values==std::vector<float>({60.f,180.f}) &&
        shadowDistance && shadowDistance->label=="Shadow distance",
        "Engine configuration descriptors did not produce the complete typed editor field snapshot")) return false;
    auto malformedDistribution=distribution;malformedDistribution.placementFields.erase(malformedDistribution.placementFields.begin());
    if (!check(!api.ApplyPcgLayerConfiguration(target,malformedDistribution).success,
        "A missing descriptor-driven configuration field was silently accepted")) return false;
    const auto stale=draft;
    draft.name="Edited tree";draft.variants[0].weight=2;draft.variants[0].scale={1.1f,1.2f,1.3f};
    VansIOAudit::Reset();
    const auto applied=api.ApplyPcgPlantConfiguration(draft);
    if (!check(applied.success,applied.message)) return false;
    const auto published=manager.GetAssetObjectRepository().ResolveLatest<VansPlantTypeAsset>(plantGuid);
    if (!check(published && published->name==draft.name && published->variants[0].weight==2 &&
        published->variants[0].parts[0].material==plant.variants[0].parts[0].material &&
        published->variants[0].parts[1].mesh==plant.variants[0].parts[1].mesh,
        "Plant application lost material/model bindings or did not publish memory")) return false;
    if (!check(!api.ApplyPcgPlantConfiguration(stale).success,"A stale draft overwrote newer edits")) return false;
    for (const auto& event : VansIOAudit::Snapshot())
        if (!check(event.operation!=VansIOOperation::StageWrite,"Apply wrote an authoring file")) return false;
    draft=api.GetPcgPlantConfiguration(plantGuid.ToString());draft.variants[0].weight=-1;
    if (!check(!api.ApplyPcgPlantConfiguration(draft).success &&
        api.GetPcgPlantConfiguration(plantGuid.ToString()).variants[0].weight==2,"Invalid input partially changed a plant")) return false;
    if (!check(api.EditPcgConfiguration(plantGuid.ToString(),PcgConfigurationAction::Undo).success &&
        api.GetPcgPlantConfiguration(plantGuid.ToString()).name==plant.name &&
        api.EditPcgConfiguration(plantGuid.ToString(),PcgConfigurationAction::Redo).success &&
        api.GetPcgPlantConfiguration(plantGuid.ToString()).name=="Edited tree","Plant undo/redo failed")) return false;
    auto* distributionDensity=FindPcgConfigurationField(distribution.placementFields,"density");
    if (!check(distributionDensity && distributionDensity->values.size()==1,"Distribution density field is missing")) return false;
    distributionDensity->values[0]=2.5f;distribution.seed=71;distribution.name="Edited distribution";
    const auto changed=api.ApplyPcgLayerConfiguration(target,distribution);
    if (!check(changed.success,changed.message)) return false;
    const auto updated=manager.GetAssetObjectRepository().ResolveLatest<VansVegetationConfigAsset>(recipeGuid);
    if (!check(updated && updated->config.regions[0].layers[0].placement.density==2.5f &&
        updated->config.regions[0].layers[0].fixedInstances[0].position==fixed.position &&
        updated->config.regions[0].layers[1].name=="Second" &&
        updated->config.regions[0].layers[1].densityMask==region.layers[1].densityMask,
        "Distribution edit changed fixed transforms or another layer's configuration/Mask")) return false;
    const auto saved=api.EditPcgConfiguration(plantGuid.ToString(),PcgConfigurationAction::Save);
    if (!check(saved.success,saved.message)) return false;
    registry.Clear();
    const auto reopened=api.GetPcgPlantConfiguration(plantGuid.ToString());
    if (!check(reopened.available && !reopened.dirty && reopened.name=="Edited tree" &&
        reopened.variants[0].scale==std::array<float,3>{1.1f,1.2f,1.3f},"Explicit save/reopen lost plant settings")) return false;
    PcgLayerCreateRequest create;
    create.name="User grass A";create.recipeName="User-created recipe";create.regionName="User region";
    create.boundsMin={-8,-8};create.boundsMax={8,8};create.cellSize=4;create.maskWidth=32;create.maskHeight=32;
    const auto created=api.CreatePcgLayer(create);
    if (!check(created.success,created.message)) return false;
    const auto first=api.GetPcgLayerConfiguration(created.target);
    auto countGrass=first;countGrass.source=PcgSourceMode::Count;
    if (!check(!api.ApplyPcgLayerConfiguration(created.target,countGrass).success,
        "Grass editor accepted the removed Count mode")) return false;
    countGrass=first;countGrass.treeTargetCount=1;
    if (!check(!api.ApplyPcgLayerConfiguration(created.target,countGrass).success,
        "Grass editor accepted a tree target count")) return false;
    const auto blank=api.GetPcgPlantConfiguration(first.plantGuid);
    const auto* firstDensity=FindPcgConfigurationField(first.placementFields,"density");
    if (!check(first.available && !first.enabled && firstDensity && firstDensity->values==std::vector<float>{0} && blank.variants.empty(),
        "New plant creation introduced a preset or enabled density")) return false;
    VansAssetGuid maskId;VansAssetGuid::TryParse(created.target.maskGuid,maskId);
    const auto black=manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(maskId);
    if (!check(black && black->mask.pixels==std::vector<std::uint16_t>(1024,0),"New Mask is not independently black")) return false;
    const auto pixels=manager.GetAssetDatabase()->Find(black->pixelAsset);
    if (!check(pixels && pixels->textureImport.linear && !pixels->textureImport.compressed &&
        !pixels->textureImport.mipmapped && pixels->textureImport.channelCount==1 && pixels->textureImport.precision=="mid16",
        "New Mask metadata violates the linear 16-bit contract")) return false;
    const auto recordsBefore=manager.GetAssetDatabase()->All().size();
    create.maskWidth=0;
    if (!check(!api.CreatePcgLayer(create).success && recordsBefore==manager.GetAssetDatabase()->All().size(),
        "Invalid creation left indexed assets behind")) return false;
    create.recipeGuid=created.target.recipeGuid;create.regionId=created.target.regionId;create.name="User grass B";
    create.copyFrom=created.target;
    const auto copied=api.CreatePcgLayer(create);
    if (!check(copied.success && copied.target.maskGuid!=created.target.maskGuid,copied.message)) return false;
    VansAssetGuid copyMask;VansAssetGuid::TryParse(copied.target.maskGuid,copyMask);
    const auto independent=manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(copyMask);
    if (!check(independent && independent->pixelAsset!=black->pixelAsset && independent->mask.pixels==black->mask.pixels &&
        api.GetPcgLayerConfiguration(copied.target).plantGuid!=first.plantGuid,"Copied layers share writable assets")) return false;
    const auto scenePath=directory/"Scenes/PcgAuthoring.json";
    {
        VansScopedIOContext scope(VansIODomain::Authoring,"PcgConfiguration.SceneFixture",true);
        nlohmann::ordered_json root={{"schemaVersion",2},{"sceneGuid",VansAssetGuid::New().ToString()},
            {"name","PCG authoring"},{"entities",nlohmann::ordered_json::array()},{"settings",nlohmann::ordered_json::object()}};
        if (!check(VansJsonFileStorage::WriteAtomic(scenePath,root,error),error)) return false;
    }
    auto scene=VansSceneDocumentLoader::Load(scenePath);
    if (!check(static_cast<bool>(scene),"Cannot open the scene authoring fixture")) return false;
    VansSceneEditService edits(*scene.document);
    ContractSceneAuthoringHost sceneHost(*scene.document,edits);
    api.BindSceneAuthoring(&sceneHost);
    if (!check(api.BindPcgRecipeToScene(created.target.recipeGuid).success && scene.document->IsDirty(),
        "Binding the new recipe did not edit the scene document")) return false;
    auto surface=api.GetPcgLayerConfiguration(created.target);surface.surface=PcgSurfaceKind::Plane;
    if (!check(api.ApplyPcgLayerConfiguration(created.target,surface).success,"Cannot choose the painting surface")) return false;
    if (!check(api.SelectPcgBrushTarget(created.target,true).success,"Cannot enable the new scene brush")) return false;
    PcgBrushSettings brush;brush.radius=2;brush.strength=1;brush.hardness=.5f;brush.spacingFraction=.15f;
    if (!check(api.ConfigurePcgBrush(brush).success,"Cannot configure the new brush")) return false;
	if (!check(!api.GetPcgBrushSnapshot().dirty,
		"Changing a PCG tool brush dirtied the Mask asset before any pixel edit")) return false;
    PcgBrushInput input;input.target=created.target;input.rayOrigin={0,10,0};input.rayDirection={0,-1,0};input.phase=PcgBrushPhase::Begin;
    const auto painted=api.ApplyPcgBrushInput(input);
    input.phase=PcgBrushPhase::End;
    if (!check(painted.success && painted.hit && painted.ring.size()==65 && api.ApplyPcgBrushInput(input).success,
        "Scene brush did not raycast and paint the owned Mask")) return false;
    const auto paintedMask=manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(maskId);
    if (!check(paintedMask->mask.pixels!=black->mask.pixels &&
        manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(copyMask)->mask.pixels==black->mask.pixels,
        "Painting one layer changed another layer's pixels")) return false;
    if (!check(api.EditPcgMaskDocument(PcgMaskDocumentAction::Undo).success &&
        manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(maskId)->mask.pixels==black->mask.pixels &&
        api.EditPcgMaskDocument(PcgMaskDocumentAction::Redo).success,"Scene brush undo/redo failed")) return false;
    if (!check(api.EditPcgMaskDocument(PcgMaskDocumentAction::Save).success &&
        api.EditPcgConfiguration(created.target.recipeGuid,PcgConfigurationAction::Save).success,"New assets could not be saved")) return false;
    if (!check(api.RemovePcgLayer(copied.target).success && !api.GetPcgLayerConfiguration(copied.target).available &&
        api.EditPcgConfiguration(created.target.recipeGuid,PcgConfigurationAction::Undo).success &&
        api.GetPcgLayerConfiguration(copied.target).available,"Layer removal undo lost the independent layer")) return false;
    api.BindSceneAuthoring(nullptr);
    if (!check(api.SelectPcgBrushTarget(created.target,false).success,"Cannot select the canvas Mask")) return false;
    input.target=created.target;input.space=PcgBrushSpace::MaskCanvas;input.maskUV={.25f,.25f};input.phase=PcgBrushPhase::Begin;
    if (!check(api.ApplyPcgBrushInput(input).success && api.GetPcgBrushSnapshot().canvasStrokeActive,
        "Canvas painting requires the Scene tool or does not own its stroke")) return false;
    auto wrongSpace=input;wrongSpace.space=PcgBrushSpace::Scene;wrongSpace.phase=PcgBrushPhase::End;
    if (!check(!api.ApplyPcgBrushInput(wrongSpace).success && api.GetPcgBrushSnapshot().canvasStrokeActive,
        "Scene input stole a canvas stroke")) return false;
    input.phase=PcgBrushPhase::Cancel;
    if (!check(api.ApplyPcgBrushInput(input).success && !api.GetPcgBrushSnapshot().strokeActive,"Canvas cancel failed")) return false;
    PcgMaskDataRequest data;data.target=created.target;data.action=PcgMaskDataAction::Fill;data.fill=.25f;
    const auto pixelFingerprint=VansAssetDocument::Fingerprint(pixels->sourcePath,error);
    if (!check(api.EditPcgMaskData(data).success,"Fill failed")) return false;
    data.action=PcgMaskDataAction::Remap;data.width=64;data.height=16;data.boundsMin={-16,-8};data.boundsMax={16,8};data.preserveWorld=false;
    if (!check(api.EditPcgMaskData(data).success && api.GetPcgMaskPreview(data.target.maskGuid).width==64 &&
        api.EditPcgMaskDocument(PcgMaskDocumentAction::Undo).success && api.GetPcgMaskPreview(data.target.maskGuid).width==32 &&
        api.EditPcgMaskDocument(PcgMaskDocumentAction::Redo).success && api.GetPcgMaskPreview(data.target.maskGuid).width==64 &&
        VansAssetDocument::Fingerprint(pixels->sourcePath,error)==pixelFingerprint,"Mapping history changed disk or lost dimensions")) return false;
    data.action=PcgMaskDataAction::Export;data.path=(directory/"Export.png").string();
    if (!check(api.EditPcgMaskData(data).success,"16-bit export failed")) return false;
    data.action=PcgMaskDataAction::Fill;data.fill=0;
    if (!check(api.EditPcgMaskData(data).success,"Clear failed")) return false;
    data.action=PcgMaskDataAction::Import;data.channel=3;
    if (!check(!api.EditPcgMaskData(data).success,"Import accepted a missing channel")) return false;
    data.channel=0;
    if (!check(api.EditPcgMaskData(data).success && api.EditPcgMaskDocument(PcgMaskDocumentAction::Save).success &&
        manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(maskId)->mask.pixels==std::vector<uint16_t>(1024,16384) &&
        manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(copyMask)->mask.pixels==black->mask.pixels,
        "Import/save lost precision or touched another layer")) return false;
    if (!check(api.CreatePcgExclusionMask(created.target).success && !api.CreatePcgExclusionMask(created.target).success,
        "Exclusion creation failed or replaced an existing Mask")) return false;
    const auto exclusionTarget=api.GetPcgBrushSnapshot().target;
    VansAssetGuid exclusionGuid;VansAssetGuid::TryParse(exclusionTarget.maskGuid,exclusionGuid);
    const auto exclusion=manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(exclusionGuid);
    if (!check(exclusion && exclusion->pixelAsset!=black->pixelAsset && exclusion->mask.pixels==std::vector<uint16_t>(1024,0),
        "Exclusion pixels were shared or not initially empty")) return false;
    auto instances=api.GetPcgInstances(target,0);
    if (!check(instances.success && instances.items.size()==1 && instances.items[0].position==fixed.position,
        "Fixed instance editor lost author transforms")) return false;
    PcgInstanceEditRequest instanceEdit;instanceEdit.target=target;instanceEdit.documentState=instances.documentState;
    instanceEdit.instance=instances.items[0];instanceEdit.instance.position={5,6,7};
    if (!check(api.EditPcgInstance(instanceEdit).success && api.GetPcgInstances(target,0).items[0].position==instanceEdit.instance.position &&
        !api.EditPcgInstance(instanceEdit).success,"Instance edit failed or accepted a stale recipe state")) return false;
    if (!check(api.EditPcgConfiguration(target.recipeGuid,PcgConfigurationAction::Undo).success &&
        api.GetPcgInstances(target,0).items[0].position==fixed.position,"Instance undo failed")) return false;
    instances=api.GetPcgInstances(target,0);instanceEdit.documentState=instances.documentState;instanceEdit.action=PcgInstanceAction::Add;
    if (!check(api.EditPcgInstance(instanceEdit).success && api.GetPcgInstances(target,0).total==2,"Manual instance addition failed")) return false;
    instances=api.GetPcgInstances(target,0);instanceEdit.documentState=instances.documentState;instanceEdit.instance=instances.items.back();
    instanceEdit.action=PcgInstanceAction::Remove;
    if (!check(api.EditPcgInstance(instanceEdit).success && api.GetPcgInstances(target,0).total==1,"Manual instance removal failed")) return false;
    std::cout<<"[PcgConfiguration] Typed edits, blank creation, independent copy, scene paint, undo/redo, save and removal passed\n";
    return true;
}
