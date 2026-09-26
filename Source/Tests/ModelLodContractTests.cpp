#include "../EngineCore/AuthoringCore/ModelLod/VansModelLodBuilder.h"
#include "../EngineCore/AuthoringCore/Pcg/VansPlantLodOrchestrator.h"
#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AssetCore/VansDerivedArtifactLayout.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/PcgCore/Serialization/VansPlantTypeAssetCodec.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationCollection.h"
#include "../EngineCore/RenderCore/Lod/VansLodSelection.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

bool RunModelLodContractTests()
{
    using namespace Vans;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("ForestModelLOD-"+VansAssetGuid::New().ToString());
    struct Cleanup { fs::path root; ~Cleanup(){std::error_code error;fs::remove_all(root,error);} } cleanup{root};
    const auto check=[](bool valid,const std::string& message){if(!valid)throw std::runtime_error(message);};
    try {
        VansScopedIOContext io(VansIODomain::Authoring,"ModelLod.Contract",true);
        fs::create_directories(root/"Assets");
        const auto model=VansAssetGuid::New(),material=VansAssetGuid::New();
        std::string error;
        const auto source=root/"Assets/Grid.obj";
        // UV 与位置具有可检查的关系；非单位导入缩放只能应用一次。
        std::ostringstream mesh;
        const int width=17;
        for(int y=0;y<width;++y)for(int x=0;x<width;++x)
            mesh<<"v "<<x<<' '<<y<<" 0\nvt "<<x/16.f<<' '<<y/16.f<<'\n';
        mesh<<"vn 0 0 1\n";
        for(int y=0;y<width-1;++y)for(int x=0;x<width-1;++x){
            const int a=y*width+x+1,b=a+1,c=a+width,d=c+1;
            mesh<<"f "<<a<<'/'<<a<<"/1 "<<b<<'/'<<b<<"/1 "<<c<<'/'<<c<<"/1\n";
            mesh<<"f "<<b<<'/'<<b<<"/1 "<<d<<'/'<<d<<"/1 "<<c<<'/'<<c<<"/1\n";
        }
        check(VansFileStorage::WriteAtomicBytes(source,mesh.str(),error),error);
        VansAssetMeta meta;meta.guid=model;meta.importer="ModelImporter";
        meta.SetSerializedSettings(VansSerializedValue::Object({{"scaleFactor",VansSerializedValue::Float(.5)},
            {"loadMultiMesh",VansSerializedValue::Bool(true)}}));
        check(VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(source),meta,error),error);
        const auto materialPath=root/"Assets/Surface.mat";
        check(VansFileStorage::WriteAtomicBytes(materialPath,"{}",error),error);
        meta=VansAssetMeta{};meta.guid=material;meta.importer="MaterialImporter";
        check(VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(materialPath),meta,error),error);
        VansAssetDatabase database(root/"Assets");
        check(bool(database.Scan(VansAssetOperationPolicy::ReadOnly())),"Asset scan failed");
        const std::vector<VansModelLodSourcePart> sources{{model,material,0,false}};
        VansModelLodSettings settings;VansModelLodAsset built;
        VansPlantTypeAsset inspectSource;inspectSource.name="Inspect Tree";inspectSource.category=VansPlantCategory::Tree;
        VansPlantVariant inspectVariant;inspectVariant.id="inspect-grid";inspectVariant.name="Inspect Grid";
        inspectVariant.geometry=VansPlantGeometry::Mesh;inspectVariant.weight=1;
        inspectVariant.parts={{"surface",VansPlantPartKind::Surface,model,0,material}};
        inspectSource.variants={inspectVariant};
        VansPlantTypeAsset inspected;
        std::vector<VansPlantLodVariantSummary> inspectSummaries;
        VansIOAudit::Reset();
        check(VansPlantLodOrchestrator::Build(database,inspectSource,VansModelLodBuildMode::Inspect,
            inspected,inspectSummaries,error),error);
        check(inspected.variants[0].lod.levels.size()==2&&inspectSummaries.size()==1&&
            inspectSummaries[0].triangleCounts.size()==2,"Plant LOD inspection lost orchestration results");
        const auto modelLodLocation=VansDerivedArtifactLayout::ModelLodAuthoringAssetSet(
            root/"Assets",inspected.variants[0].lod.buildKey);
        check(modelLodLocation&&modelLodLocation.artifactClass==VansDerivedArtifactClass::AuthoringAssetSet&&
            modelLodLocation.path.parent_path().filename()=="ModelLOD",
            "Model LOD artifact layout lost its authoring-asset classification");
        check(!VansDerivedArtifactLayout::ModelLodAuthoringAssetSet(root/"Assets","../escape"),
            "Model LOD artifact layout accepted a non-leaf build key");
        for(const auto& event:VansIOAudit::Snapshot())
            check(event.operation!=VansIOOperation::StageWrite,"Plant LOD dry-run wrote a derived artifact");
        check(!fs::exists(root/"Assets/Generated/ModelLOD"),"Plant LOD dry-run created the artifact root");
        check(VansModelLodBuilder::Build(database,sources,settings,
            VansModelLodBuildMode::Publish,built,error),error);
        check(built.levels.size()==2 && built.centerRadius[3]>0,"LOD levels/bounds missing");
        Assimp::Importer referenceImporter;
        const auto* reference=referenceImporter.ReadFile(source.string(),aiProcess_Triangulate|aiProcess_FlipUVs|aiProcess_CalcTangentSpace|aiProcess_GenSmoothNormals);
        check(reference && reference->mMeshes[0]->HasTangentsAndBitangents(),"Reference import lost tangent frame");
        const auto referenceTangent=reference->mMeshes[0]->mTangents[0];
        const auto referenceBitangent=reference->mMeshes[0]->mBitangents[0];
        uint32_t previous=512;
        fs::path firstPath;
        for(const auto& level:built.levels){
            check(level.parts.size()==1,"Material part lost");const auto& part=level.parts[0];
            check(part.triangleCount<previous && part.triangleCount>0,"LOD did not reduce triangle count");
            check(part.material==material && part.sourcePart==0,"Material/source binding changed");
            previous=part.triangleCount;
            const auto record=database.Find(part.model);check(bool(record),"Generated model unregistered");
            if(firstPath.empty())firstPath=record->sourcePath;
            Assimp::Importer importer;const auto* scene=importer.ReadFile(record->sourcePath.string(),aiProcess_Triangulate|aiProcess_FlipUVs|aiProcess_CalcTangentSpace);
            check(scene && scene->mNumMeshes==1,"Generated GLB cannot be reimported");
            const auto* geometry=scene->mMeshes[0];check(geometry->mNumFaces==part.triangleCount,"GLB triangle count mismatch");
            for(unsigned i=0;i<geometry->mNumVertices;++i){
                const auto p=geometry->mVertices[i],uv=geometry->mTextureCoords[0][i];
                check(std::abs(uv.x-p.x/8.f)<.0001f && std::abs(uv.y-(1-p.y/8.f))<.0001f,"GLB UV or import scale changed");
                check(p.x>=0 && p.x<=8 && p.y>=0 && p.y<=8,"Generated bounds changed");
                check(geometry->HasTangentsAndBitangents() && (geometry->mTangents[i]-referenceTangent).SquareLength()<.0001f &&
                    (geometry->mBitangents[i]-referenceBitangent).SquareLength()<.0001f,
                    "GLB tangent handedness changed");
            }
        }
        const auto timestamp=fs::last_write_time(firstPath);
        VansIOAudit::Reset();VansModelLodAsset again;
        check(VansModelLodBuilder::Build(database,sources,settings,
            VansModelLodBuildMode::Publish,again,error),error);
        check(again.buildKey==built.buildKey && again.levels[0].parts[0].model==built.levels[0].parts[0].model &&
            fs::last_write_time(firstPath)==timestamp,"Cache did not reuse model identity and bytes");
        for(const auto& event:VansIOAudit::Snapshot())check(event.operation!=VansIOOperation::StageWrite,"Cache hit wrote an asset");
        check(VansFileStorage::WriteAtomicBytes(firstPath,"corrupt",error),error);
        check(VansModelLodBuilder::Build(database,sources,settings,
            VansModelLodBuildMode::Publish,again,error),error);
        check(fs::file_size(firstPath)>7 && again.buildKey==built.buildKey,"Corrupt derived asset was not rebuilt");
        check(VansFileStorage::WriteAtomicBytes(source,mesh.str()+"# source revision\n",error),error);
        check(VansModelLodBuilder::Build(database,sources,settings,
            VansModelLodBuildMode::Publish,again,error),error);
        check(again.buildKey!=built.buildKey,"Source edit did not invalidate derived models");
        auto oneLevel=settings;oneLevel.ratios={.4f};
        VansModelLodAsset oneLevelResult;
        check(VansModelLodBuilder::Build(database,sources,oneLevel,
            VansModelLodBuildMode::Inspect,oneLevelResult,error) && oneLevelResult.levels.size()==1,
            "A valid one-level LOD configuration was not built");
        auto invalid=settings;invalid.ratios={.2f,.5f};
        check(!VansModelLodBuilder::Build(database,sources,invalid,
            VansModelLodBuildMode::Publish,again,error),"Invalid LOD ordering accepted");
        invalid.ratios.clear();
        check(!VansModelLodBuilder::Build(database,sources,invalid,
            VansModelLodBuildMode::Publish,again,error),"An empty LOD configuration was accepted");
        invalid.ratios={.6f,.3f,.1f};
        check(!VansModelLodBuilder::Build(database,sources,invalid,
            VansModelLodBuildMode::Publish,again,error),"LOD levels exceeded the runtime capacity");
        VansPlantTypeAsset plant;plant.name="Tree";plant.category=VansPlantCategory::Tree;
        VansPlantVariant variant;variant.id="grid";variant.geometry=VansPlantGeometry::Mesh;variant.weight=1;
        variant.parts={{"surface",VansPlantPartKind::Surface,model,0,material}};variant.lod=built;
        plant.variants={variant};plant.render.lodDistances={50,200};
        VansSerializedValue encoded;VansPlantTypeAsset decoded;
        check(VansPlantTypeAssetCodec::Encode(plant,encoded,error) && VansPlantTypeAssetCodec::Decode(encoded,decoded,error),error);
        check(decoded.render.lodDistances==plant.render.lodDistances && decoded.Dependencies().size()==4,"LOD references or shared distance policy lost");
        // 256 个生成 cell、两个树种，只能产生两个树种渲染组；实例数保持不变。
        using Collection=VansGraphics::VansVegetationCollection;
        Collection::CellSources cells;auto shared=std::make_shared<VansPlantTypeAsset>(plant);
        for(int cell=0;cell<256;++cell)for(const auto* species:{"pine","oak"}){
            auto data=std::make_shared<VansPcgBatchSource>();data->plant=shared;
            data->key={"region","layer",species,cell,0};data->points.resize(10);
            cells.emplace(data->key,std::move(data));
        }
        const auto groups=Collection::GroupTreeBatches(cells);
        check(groups.size()==2,"Tree draw groups still split by PCG cells");
        for(const auto& group:groups)check(group.second->points.size()==2560,"Cross-cell batching dropped instances");
        cells.erase(cells.begin());const auto updated=Collection::GroupTreeBatches(cells);
        size_t total=0;for(const auto& group:updated)total+=group.second->points.size();
        check(updated.size()==2 && total==5110,"Partial cell replacement lost untouched tree instances");
        // Runtime selection is pure view math: it must choose a level with
        // hysteresis and must not depend on asset generation or RT state.
        const glm::mat4 view = glm::mat4(1.0f);
        const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        const auto bounds = VansGraphics::MakeRenderBoundsFromLocalAABB(
            glm::vec3(-0.5f), glm::vec3(0.5f),
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f)));
        VansGraphics::VansLodSelectionInput selection;
        selection.bounds = bounds; selection.view = view; selection.projection = projection;
        selection.viewportSize = glm::vec2(1920.0f, 1080.0f); selection.pixelErrorBudget = 10.0f;
        selection.levels = {{0.02f, true}, {0.08f, true}, {0.20f, true}};
        const auto medium = VansGraphics::SelectLod(selection);
        check(medium.valid && medium.level == 1 && std::isfinite(medium.projectedErrorPixels),
            "Screen-error LOD selection chose an unexpected level " + std::to_string(medium.level) +
            " error=" + std::to_string(medium.projectedErrorPixels) +
            " height=" + std::to_string(medium.screenHeight));
        selection.mode = VansGraphics::VansLodSelectionMode::ScreenRelativeHeight;
        // The projected height at z=-10 is about 0.09 of the viewport, so
        // the final level must use a threshold above that value to be selected.
        selection.levels = {{0.5f, true}, {0.2f, true}, {0.1f, true}};
        const auto far = VansGraphics::SelectLod(selection);
        check(far.valid && far.level == 2, "Screen-height LOD selection chose an unexpected level " +
            std::to_string(far.level) + " height=" + std::to_string(far.screenHeight));
        selection.previousLevel = 2; selection.hysteresis = 0.1f;
        const auto stable = VansGraphics::SelectLod(selection);
        check(stable.level == 2, "LOD hysteresis did not preserve the previous level");
        selection.previousLevel = 1;
        const auto held = VansGraphics::SelectLod(selection);
        check(held.level == 1, "LOD hysteresis did not hold the finer level near a transition");
        VansSerializedValue lodEntity = VansSerializedValue::Object({
            {"id", VansSerializedValue::String(VansAssetGuid::New().ToString())},
            {"name", VansSerializedValue::String("LODEntity")},
            {"components", VansSerializedValue::Array({
                VansSerializedValue::Object({
                    {"id", VansSerializedValue::String(VansAssetGuid::New().ToString())},
                    {"type", VansSerializedValue::String("LODGroup")},
                    {"data", VansSerializedValue::Object({
                        {"mode", VansSerializedValue::String("screenRelativeHeight")},
                        {"levels", VansSerializedValue::Array({VansSerializedValue::Object({
                            {"meshes", VansSerializedValue::Array({VansSerializedValue::String(VansAssetGuid::New().ToString())})},
                            {"errors", VansSerializedValue::Array({VansSerializedValue::Float(0.25)})},
                            {"screenHeight", VansSerializedValue::Float(0.2)}
                        })})}
                    })}
                })
            })}
        });
        VansSceneContentBuildPlan lodPlan;
        std::string lodPlanError;
        check(VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
            VansSerializedValue::Array({lodEntity}), root.string(), lodPlan, lodPlanError),
            "LODGroup scene projection failed: " + lodPlanError);
        check(lodPlan.objects.objects.size() == 1 && lodPlan.objects.objects[0].lodGroup.has_value() &&
            lodPlan.objects.objects[0].lodGroup->levels.size() == 1,
            "LODGroup component was not persisted into the runtime build plan");
        std::cout<<"[ModelLOD] PASS: reduction, GLB UV/scale, cache reuse/repair/invalidation, references, shared distances, selection+hysteresis, 512 cells -> 2 tree groups\n";
        return true;
    } catch(const std::exception& exception){std::cerr<<"[ModelLOD] "<<exception.what()<<'\n';return false;}
}
