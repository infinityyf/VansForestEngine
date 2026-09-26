#include <algorithm>
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentRegistry.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentEditService.h"
#include "../EngineCore/EditorCore/VansEditorAssetSaveService.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/SceneCore/VansSceneDocument.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/SceneCore/Storage/VansSceneFileStorage.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/EditorCore/VansPrefabEditService.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/AssetCore/VansAssetDocument.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/AssetCore/VansAssetObjectRepository.h"
#include "../EngineCore/SceneCore/VansSceneAssetDependencyBuilder.h"
#include <filesystem>
#include "../EngineCore/SceneCore/Prefab/VansPrefabAsset.h"
#include "../EngineCore/SceneCore/VansSceneEntityFactory.h"
#include "../EngineCore/SceneCore/VansSceneParentReference.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace Vans;
using Json = nlohmann::ordered_json;
void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Json JsonOf(const VansSerializedValue& value) { return EncodeSerializedValueJson<Json>(value); }
}

void RunPrefabContractTests()
{
    using namespace Vans;
    const auto rootId = VansAssetGuid::New().ToString();
    const auto childId = VansAssetGuid::New().ToString();
    auto root = JsonOf(VansSceneEntityFactory::BuildEmptyEntity({}, rootId));
    auto child = JsonOf(VansSceneEntityFactory::BuildEmptyEntity({}, childId));
    root["name"] = "Crate";
    root["components"][0]["data"]["position"] = { 3.0, 4.0, 5.0 };
    child["parent"] = JsonOf(WriteEntityParentReference(rootId));
    const auto scriptId = VansAssetGuid::New().ToString();
    const auto timelineId = VansAssetGuid::New().ToString();
    const auto materialId = VansAssetGuid::New().ToString();
    const auto externalId = VansAssetGuid::New().ToString();
    root["components"].push_back({ { "id", scriptId }, { "type", "Script" }, { "version", 1 },
        { "enabled", false }, { "data", {
            { "internal", { { "domain", "SceneEntity" }, { "guid", childId }, { "entityGuid", childId } } },
            { "external", { { "domain", "SceneEntity" }, { "guid", externalId }, { "entityGuid", externalId } } },
            { "asset", { { "domain", "ProjectAsset" }, { "guid", materialId } } },
            { "ordinaryText", childId }, { "speed", 3.0 } } } });
    root["components"].push_back({ { "id", timelineId }, { "type", "Timeline" }, { "version", 1 },
        { "enabled", true }, { "data", { { "bindingOverrides", Json::array({ {
            { "bindingId", "self" }, { "targetEntity", childId },
            { "targetComponent", child["components"][0]["id"] } } }) } } } });
    Json scene{ { "schemaVersion", VansSceneSchemaVersion }, { "sceneGuid", VansAssetGuid::New().ToString() },
        { "entities", Json::array({ root, child }) } };
    const auto assetId = VansAssetGuid::New();
    VansPrefabExtraction extracted;
    std::string error;
    Check(VansPrefabResolver::Extract(DecodeSerializedValueJson(scene), rootId, assetId, extracted, error), error);
    Check(VansPrefabCodec::Validate(extracted.asset, error), error);
    auto asset = std::make_shared<VansPrefabAsset>(extracted.asset);
    VansPrefabLookup lookup = [asset, assetId](const std::string& guid)
        -> std::shared_ptr<const VansPrefabAsset> { return guid == assetId.ToString() ? asset : nullptr; };
    VansSerializedValue authoring, resolved;
    Check(VansPrefabResolver::CaptureScene(extracted.scene, lookup, authoring, error), error);
    Check(JsonOf(authoring)["entities"].empty(), "Inherited entities leaked into authoring scene");
    Check(VansPrefabResolver::ResolveScene(authoring, lookup, resolved, error), error);
    Check(JsonOf(resolved)["entities"] == scene["entities"], "Create/connect changed existing scene data or identities");
    Check(JsonOf(VansPrefabCodec::Encode(*asset))["entities"][0]["components"][1]["data"]["external"]["guid"] == "",
        "External scene reference leaked into prefab asset");
    auto instance = VansPrefabResolver::MakeInstance(assetId);
    VansSerializedValue first, second;
    Check(VansPrefabResolver::Instantiate(*asset, instance, first, error), error);
    Check(VansPrefabResolver::Instantiate(*asset, instance, second, error), error);
    Check(JsonOf(first) == JsonOf(second), "Instance identities are not stable");
    const auto a = JsonOf(first);
    const auto rootGuid = a[0]["id"];
    const auto childGuid = a[1]["id"];
    Check(a[0]["components"][1]["data"]["internal"]["guid"] == childGuid, "Internal reference not remapped");
    Check(a[0]["components"][1]["data"]["ordinaryText"] == childId, "Ordinary GUID-looking string was modified");
    Check(a[0]["components"][1]["data"]["asset"]["guid"] == materialId, "Shared asset identity changed");
    Check(a[0]["components"][2]["data"]["bindingOverrides"][0]["targetEntity"] == childGuid, "Timeline entity not remapped");
    Check(a[0]["components"][2]["data"]["bindingOverrides"][0]["targetComponent"] == a[1]["components"][0]["id"],
        "Timeline component not remapped");
    Check(a[1]["parent"]["entityGuid"] == rootGuid, "Parent identity not remapped");
    auto other = VansPrefabResolver::MakeInstance(assetId);
    Check(VansPrefabResolver::Instantiate(*asset, other, second, error), error);
    Check(JsonOf(second)[0]["id"] != rootGuid, "Different instances share object identities");
    auto edited = JsonOf(resolved);
    edited["entities"][0]["components"][1]["data"]["speed"] = 7.0;
    edited["entities"][1]["name"] = "Overridden child";
    Check(VansPrefabResolver::CaptureScene(DecodeSerializedValueJson(edited), lookup, authoring, error), error);
    auto changedAsset = JsonOf(VansPrefabCodec::Encode(*asset));
    changedAsset["entities"][0]["name"] = "Updated template";
    changedAsset["entities"][0]["components"][1]["data"]["speed"] = 9.0;
    Check(VansPrefabCodec::Decode(DecodeSerializedValueJson(changedAsset), *asset, error), error);
    Check(VansPrefabResolver::ResolveScene(authoring, lookup, resolved, error), error);
    const auto updated = JsonOf(resolved);
    Check(updated["entities"][0]["name"] == "Updated template", "Unmodified property did not inherit template change");
    Check(updated["entities"][0]["components"][1]["data"]["speed"] == 7.0, "Template erased instance override");
    Check(updated["entities"][1]["name"] == "Overridden child", "Entity override was lost");
    auto duplicate = JsonOf(authoring);
    duplicate["prefabInstances"].push_back(duplicate["prefabInstances"][0]);
    Check(!VansPrefabResolver::ResolveScene(DecodeSerializedValueJson(duplicate), lookup, resolved, error), "Duplicate instance identity accepted");
    VansPrefabLookup missing = [](const std::string&) -> std::shared_ptr<const VansPrefabAsset> { return {}; };
    Check(!VansPrefabResolver::ResolveScene(authoring, missing, resolved, error), "Missing prefab silently ignored");
    VansPrefabAsset roundTrip;
    Check(VansPrefabCodec::Decode(VansPrefabCodec::Encode(*asset), roundTrip, error), error);
    Check(JsonOf(VansPrefabCodec::Encode(roundTrip)) == JsonOf(VansPrefabCodec::Encode(*asset)), "Prefab codec lost data");
    auto invalidReference = JsonOf(asset->entities);
    invalidReference[0]["components"][2]["data"]["bindingOverrides"][0]["targetEntity"] = rootId;
    Check(!VansPrefabCodec::Validate({asset->rootEntity, DecodeSerializedValueJson(invalidReference)}, error),
        "Prefab accepted a component reference paired with the wrong owner");
    VansSceneData previewScene; previewScene.sceneGuid = VansAssetGuid::New();
    previewScene.settings = VansSceneSchema::MakeDefaultSettings();
    VansSceneContentBuildPlan previewPlan;
    const bool previewValid = VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
        DecodeSerializedValueJson(VansSceneSchema::SerializeSceneJson(previewScene)), {}, previewPlan, error);
    Check(previewValid, "Default empty scene is not runtime-loadable: " + error);
    auto healthScene = VansSceneSchema::SerializeSceneJson(previewScene);
    healthScene["entities"].push_back(JsonOf(
        VansSceneEntityFactory::BuildEmptyEntity({}, VansAssetGuid::New().ToString())));
    auto healthDocument = VansSceneDocument::CreateInMemory(
        DecodeSerializedValueJson(healthScene), {}, error);
    Check(healthDocument && healthDocument->IsHealthy(), "Healthy scene fixture failed: " + error);
    VansSceneEditService healthEdits(*healthDocument);
    const auto invalidTypeEdit = healthEdits.Set(
        MakeDocumentPropertyPath(
            DocumentPropertySpace::Scene, "/entities/0/components/0/type"),
        VansSerializedValue::String("UnknownRuntimeComponent"));
    Check(invalidTypeEdit.success && !healthDocument->IsHealthy(),
        "Scene diagnostics stayed healthy after an invalid component edit");
    Check(std::any_of(
        healthDocument->Diagnostics().begin(), healthDocument->Diagnostics().end(),
        [](const SceneDiagnostic& diagnostic)
        {
            return diagnostic.message.find("Unsupported runtime component") != std::string::npos;
        }), "Edited Scene diagnostics did not report the unsupported component");
    Check(healthEdits.Undo().success && healthDocument->IsHealthy(),
        "Undo did not rebuild healthy Scene diagnostics");
    Check(healthEdits.Redo().success && !healthDocument->IsHealthy(),
        "Redo did not rebuild invalid Scene diagnostics");
    Check(healthEdits.Undo().success && healthDocument->IsHealthy(),
        "Final health fixture restore failed");
    // 编辑命令始终保存精简作者数据；Undo/Redo 与磁盘重新打开恢复相同身份。
    auto doc = VansSceneDocument::CreateInMemory(authoring, lookup, error);
    Check(doc != nullptr, error);
    VansSceneEditService edits(*doc);
    const auto before = JsonOf(doc->AuthoringRootSnapshot());
    auto editResult = edits.Set(MakeDocumentPropertyPath(DocumentPropertySpace::Scene, "/entities/1/name"), VansSerializedValue::String("Edited in Inspector"));
    Check(editResult.success, editResult.message);
    Check(JsonOf(doc->AuthoringRootSnapshot())["entities"].empty(), "Inspector expanded prefab into scene file");
    Check(edits.Undo().success, "Undo failed");
    Check(JsonOf(doc->AuthoringRootSnapshot()) == before, "Undo did not restore canonical instance");
    Check(edits.Redo().success, "Redo failed");
    auto place = VansPrefabEditService::Place(*doc, edits, assetId.ToString(),
        DecodeSerializedValueJson(Json{{"parent", nullptr}, {"position", {1,2,3}}, {"rotation", {0,0,0,1}}}), {});
    Check(place.success, place.message);
    Check(JsonOf(doc->SerializedRootSnapshot())["entities"].size() == 4, "Second instance not expanded");
    Check(edits.Undo().success && JsonOf(doc->SerializedRootSnapshot())["entities"].size() == 2, "Placement undo failed");
    Check(edits.Redo().success, "Placement redo failed");
    const auto firstRoot = JsonOf(doc->SerializedRootSnapshot())["entities"][0]["id"].get<std::string>();
    Check(VansPrefabEditService::Unpack(*doc, edits, firstRoot).success, "Unpack failed");
    Check(JsonOf(doc->AuthoringRootSnapshot())["entities"].size() == 2, "Unpack lost entities");
    Check(edits.Undo().success && JsonOf(doc->AuthoringRootSnapshot())["entities"].empty(), "Unpack undo lost source association");
    // 新的普通子树不受同场景中其他 Prefab 影响。
    auto mixed = JsonOf(doc->SerializedRootSnapshot());
    const auto independentId = VansAssetGuid::New().ToString();
    mixed["entities"].push_back(JsonOf(VansSceneEntityFactory::BuildEmptyEntity({}, independentId)));
    VansPrefabExtraction independent;
    Check(VansPrefabResolver::Extract(DecodeSerializedValueJson(mixed), independentId, VansAssetGuid::New(), independent, error, lookup), error);
    Check(!VansPrefabResolver::Extract(doc->SerializedRootSnapshot(), firstRoot, VansAssetGuid::New(), independent, error, lookup),
        "Existing instance was silently flattened during prefab creation");

    VansSerializedValue duplicated; std::string duplicateRoot;
    Check(VansPrefabResolver::DuplicateSubtree(doc->SerializedRootSnapshot(), lookup, firstRoot, duplicated, duplicateRoot, error), error);
    Check(JsonOf(duplicated)["prefabInstances"].size() == 3, "Duplicate lost prefab source association");
    Check(duplicateRoot != firstRoot, "Duplicate retained the original root identity");
    VansSerializedValue duplicateAuthoring;
    Check(VansPrefabResolver::CaptureScene(duplicated, lookup, duplicateAuthoring, error), error);
    Check(JsonOf(duplicateAuthoring)["entities"].empty(), "Duplicate flattened inherited objects");
    const auto firstRecord = JsonOf(doc->AuthoringRootSnapshot())["prefabInstances"][0];
    VansPrefabAsset applied; VansSerializedValue appliedInstance;
    Check(VansPrefabResolver::Apply(*asset, DecodeSerializedValueJson(firstRecord), applied, appliedInstance, error), error);
    Check(JsonOf(applied.entities)[0]["components"][1]["data"]["speed"] == 7.0, "Apply did not update template property");
    Check(JsonOf(applied.entities)[0]["components"][1]["data"]["external"]["guid"] == "", "Apply leaked external scene binding into source");
    VansSerializedValue appliedObjects;
    Check(VansPrefabResolver::Instantiate(applied, appliedInstance, appliedObjects, error), error);
    Check(JsonOf(appliedObjects)[0]["components"][1]["data"]["external"]["guid"] == externalId, "Apply erased instance external binding");
    // 模板重排后，撤销仍恢复对应对象上的作者覆盖而非数组位置。
    auto rebasedAsset = JsonOf(asset->entities);
    auto added = JsonOf(VansSceneEntityFactory::BuildEmptyEntity({}, VansAssetGuid::New().ToString()));
    added["parent"] = JsonOf(WriteEntityParentReference(asset->rootEntity));
    rebasedAsset.insert(rebasedAsset.begin() + 1, added);
    const auto oldEntities = asset->entities;
    asset->entities = DecodeSerializedValueJson(rebasedAsset);
    Check(doc->RefreshPrefabView(error), error);
    Check(edits.Undo().success, "Prefab undo failed after template insertion");
    Check(edits.Redo().success, "Prefab redo failed after template insertion");
    asset->entities = oldEntities;
    Check(doc->RefreshPrefabView(error), error);

    struct TestDirectory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("ForestPrefabContract-" + VansAssetGuid::New().ToString());
        TestDirectory() { std::filesystem::create_directories(path / "Assets"); }
        ~TestDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } temporary;
    const auto scenePath = temporary.path / "Scene.vscene";
    Check(VansSceneFileStorage::WriteSceneDocument(
        scenePath, JsonOf(doc->AuthoringRootSnapshot()), error), error);
    const auto loaded = VansSceneDocumentLoader::Load(scenePath, lookup);
    Check(static_cast<bool>(loaded), "Saved prefab scene could not reopen");
    Check(JsonOf(loaded.document->SerializedRootSnapshot()) == JsonOf(doc->SerializedRootSnapshot()), "Save/open changed prefab identities or overrides");
    std::string bytes;
    Check(VansFileStorage::ReadAllBytes(scenePath, bytes, error), error);
    Check(Json::parse(bytes)["entities"].empty(), "Scene file contains expanded prefab entities");

    VansScopedIOContext write(VansIODomain::Authoring, "PrefabContract", true);
    const auto transactionTarget = temporary.path / "Atomic.txt";
    Check(VansFileStorage::WriteAtomicBytes(transactionTarget, "before", error), error);
    VansStagedFile file;
    Check(VansFileStorage::StageWriteBytes(transactionTarget, "after", file, error), error);
    {
        VansStagedFileTransaction transaction; transaction.Add(file);
        Check(transaction.PreparePublish(error), error);
        Check(transaction.PreparePublish(error), "Repeated prepare damaged the transaction");
        Check(VansFileStorage::ReadAllBytes(transactionTarget, bytes, error) && bytes == "after", "Prepared file invisible");
        // 不 Commit 模拟后续导入失败，析构必须恢复旧文件。
    }
    Check(VansFileStorage::ReadAllBytes(transactionTarget, bytes, error) && bytes == "before", "Failed save did not roll back disk");
    Check(VansFileStorage::StageWriteBytes(transactionTarget, "overwrite", file, error), error);
    file.requireAbsent = true;
    { VansStagedFileTransaction transaction; transaction.Add(file); Check(!transaction.Publish(error), "New asset overwrote existing file");
      Check(!transaction.PreparePublish(error), "Rolled-back transaction reported successful publication"); }
    Check(VansFileStorage::ReadAllBytes(transactionTarget, bytes, error) && bytes == "before", "Name collision changed existing asset");

    const auto retryPath = temporary.path / "Retry.json";
    Check(VansFileStorage::WriteAtomicBytes(retryPath, "{\"value\":1}", error), error);
    const auto retryOpenDocument = VansAssetDocumentRegistry::Get().GetOrOpen(retryPath);
    Check(retryOpenDocument && retryOpenDocument->sourceDocument.IsLoaded(),
        "Retry fixture document did not open");
    VansAssetDocument& retryDocument = retryOpenDocument->sourceDocument;
    Check(VansAssetDocumentEditService::ReplaceRoot(retryDocument, DecodeSerializedValueJson(Json{{"value",2}})).success,
        "Retry fixture edit failed");
    VansAssetDocumentSaveStage retryStage;
    Check(retryDocument.StageSave(retryStage, error), error);
    {
        VansStagedFileTransaction transaction;
        transaction.Add({retryStage.targetPath, retryStage.temporaryPath, retryStage.requireAbsent});
        Check(transaction.PreparePublish(error), error);
        Check(retryDocument.ObservePublishedSave(retryStage, error), error);
    }
    Check(retryDocument.StageSave(retryStage, error), "Rolled-back save poisoned document fingerprint: " + error);
    {
        VansStagedFileTransaction transaction;
        transaction.Add({retryStage.targetPath, retryStage.temporaryPath, retryStage.requireAbsent});
        Check(transaction.PreparePublish(error), error);
        Check(retryDocument.ObservePublishedSave(retryStage, error), error);
        transaction.Commit(); retryDocument.AdoptObservedSave(retryStage);
    }
    Check(!retryDocument.IsDirty(), "Successful save retry stayed dirty");

    const auto prefabPath = temporary.path / "Assets" / "Template.vprefab";
    Check(VansFileStorage::WriteAtomicBytes(prefabPath, JsonOf(VansPrefabCodec::Encode(*asset)).dump(), error), error);
    VansAssetMeta meta; meta.guid = assetId; meta.importer = "PrefabImporter";
    Check(VansAssetMetaStorage::SaveAtomic(VansAssetMeta::MetaPathFor(prefabPath), meta, error), error);
    VansAssetDatabase database(temporary.path / "Assets");
    Check(database.RegisterOrRefresh(prefabPath, VansAssetOperationPolicy::ReadOnly(), error), error);
    VansAssetObjectRepository repository;
    const auto bootstrap = VansAssetObjectBootstrapper::Publish(database.All(), repository);
    Check(static_cast<bool>(bootstrap), "Prefab asset bootstrap failed");
    Check(repository.ResolveLatest<VansPrefabAsset>(assetId) != nullptr, "Prefab asset was not published as typed memory object");
    Check(static_cast<bool>(VansSceneDocumentLoader::Load(scenePath, VansPrefabResolver::FromRepository(repository))), "Repository-backed scene load failed");
    // 复合 Apply 同时恢复源资产和实例，不能在两个互不协调的撤销栈中拆开。
    auto sourceDocument = VansAssetDocumentRegistry::Get().GetOrOpen(prefabPath);
    Check(sourceDocument && sourceDocument->sourceDocument.IsLoaded(), "Prefab document registry open failed");
    VansAssetDocumentRegistry::Get().SetWorkingCopyPublisher([asset](const VansOpenAssetDocument& source, std::string& why)
    { return VansPrefabCodec::Decode(source.sourceDocument.SerializedRootSnapshot(), *asset, why); });
    auto appliedScene = JsonOf(doc->AuthoringRootSnapshot());
    appliedScene["prefabInstances"][0] = JsonOf(appliedInstance);
    auto applyResult = edits.ApplyPrefab(sourceDocument, VansPrefabCodec::Encode(applied), DecodeSerializedValueJson(appliedScene));
    Check(applyResult.success, applyResult.message);
    Check(sourceDocument->sourceDocument.IsDirty(), "Apply did not mark template dirty");
    Check(edits.Undo().success && !sourceDocument->sourceDocument.IsDirty(), "Apply undo did not restore template saved state");
    Check(edits.Redo().success && sourceDocument->sourceDocument.IsDirty(), "Apply redo failed");
    Check(edits.Undo().success, "Apply final undo failed");
    VansAssetDocumentRegistry::Get().ClearWorkingCopyPublisher();
    VansAssetDocumentRegistry::Get().Clear();

    // 动态模板的默认模型必须参与依赖收集，即使场景里没有实例。
    VansPrefabAsset dependencyPrefab;
    const auto dependencyModel = VansAssetGuid::New();
    auto dependencyEntity = JsonOf(VansSceneEntityFactory::BuildEmptyEntity({}, VansAssetGuid::New().ToString()));
    auto renderer = VansSceneSchema::MakeModelRenderer(dependencyModel);
    dependencyEntity["components"].push_back({{"id", renderer.id.ToString()}, {"type", renderer.type}, {"version", renderer.version}, {"enabled", renderer.enabled}, {"data", JsonOf(renderer.data)}});
    dependencyPrefab.rootEntity = dependencyEntity["id"];
    dependencyPrefab.entities = DecodeSerializedValueJson(Json::array({dependencyEntity}));
    Check(repository.Publish<VansPrefabAsset>(assetId, VansAssetType::Prefab, 987,
        std::make_shared<const VansPrefabAsset>(dependencyPrefab), {}, error).IsValid(), error);
    Check(repository.PublishView<VansAssetMeta>(assetId, VansAssetType::Prefab, 987,
        std::make_shared<const VansAssetMeta>(meta), error).IsValid(), error);
    VansSceneData emptyScene; emptyScene.sceneGuid = VansAssetGuid::New();
    auto dependencyScene = VansSceneSchema::SerializeSceneJson(emptyScene);
    dependencyScene["prefabAssets"] = Json::array({assetId.ToString()});
    const auto dependencies = VansSceneAssetDependencyBuilder::BuildResourcePlan(database,
        DecodeSerializedValueJson(dependencyScene), scenePath, {}, repository);
    Check(dependencies.requiredModels.count(dependencyModel.ToString()) == 1, "Dynamic prefab default model missing from dependency closure");
    Check(!dependencies.success, "Missing prefab model silently accepted by resource collection");
    Check(std::any_of(dependencies.errors.begin(), dependencies.errors.end(), [&](const auto& text) { return text.find(dependencyModel.ToString()) != std::string::npos; }),
        "Dependency error did not identify the missing template model");
    dependencyScene["prefabAssets"] = Json::array({VansAssetGuid::New().ToString()});
    Check(!VansSceneAssetDependencyBuilder::BuildResourcePlan(database, DecodeSerializedValueJson(dependencyScene), scenePath, {}, repository).success,
        "Missing declared dynamic prefab silently omitted from package");
    auto unknown = dependencyPrefab.entities;
    unknown.arrayItems[0].objectFields.push_back({"opaque", VansSerializedValue::String("retained")});
    auto unknownJson = JsonOf(unknown); unknownJson[0]["components"][1]["type"] = "UnknownPrefabComponent";
    Check(!VansSceneSchema::ValidateEntityComponents(DecodeSerializedValueJson(unknownJson)).empty(),
        "Unsupported prefab component silently omitted from runtime");

    // 包内只保留缓存及索引路径，移除作者文件后依然能加载模板。
    const auto cache = temporary.path / "Cache"; std::filesystem::create_directories(cache);
    const auto cachedSource = cache / "Template.asset";
    const auto cachedMeta = cache / "Template.meta";
    std::filesystem::copy_file(prefabPath, cachedSource);
    std::filesystem::copy_file(VansAssetMeta::MetaPathFor(prefabPath), cachedMeta);
    auto packagedRecord = *database.Find(assetId);
    packagedRecord.sourcePath = cachedSource; packagedRecord.metaPath = cachedMeta; packagedRecord.authoringPath.clear();
    std::filesystem::remove(prefabPath); std::filesystem::remove(VansAssetMeta::MetaPathFor(prefabPath));
    VansAssetObjectRepository packagedRepository;
    Check(static_cast<bool>(VansAssetObjectBootstrapper::Publish({packagedRecord}, packagedRepository)), "Prefab cache requires an authoring source file");
    Check(static_cast<bool>(VansSceneDocumentLoader::Load(scenePath, VansPrefabResolver::FromRepository(packagedRepository))),
        "Packaged prefab scene failed after removing source asset files");

    // 使用真实编辑器 API 验证拖拽背后的资产创建、导入和复合保存入口。
    const auto projectPath = temporary.path / "EditorProject";
    std::filesystem::create_directories(projectPath / "Assets");
    VansProjectConfig configuration; configuration.SetDefaults("Prefab Editor Contract");
    Check(configuration.SaveToFile((projectPath / "ForestProject.json").string()), "Fixture project save failed");
    VansProjectOpenRequest openRequest; openRequest.m_ProjectRootPath = projectPath.string();
    openRequest.m_Options.m_UpdateRecentProjects = false; openRequest.m_Options.m_LoadProjectSettings = false;
    Check(VansProjectManager::Get().OpenProject(openRequest).m_Opened, "Fixture project open failed");
    struct ProjectScope
    {
        ~ProjectScope()
        {
            VansAssetDocumentRegistry::Get().ClearWorkingCopyPublisher();
            VansAssetDocumentRegistry::Get().Clear();
            VansAssetDocumentEditService::ClearAllHistories();
            VansProjectManager::Get().CloseProject();
        }
    } projectScope;
    EditorAPI::EngineAPIImpl api;
    VansAssetDocumentRegistry::Get().SetWorkingCopyPublisher([&](const VansOpenAssetDocument& document, std::string& why)
    {
        EditorAPI::AssetWorkingCopyPublishRequest request;
        request.sourcePath = document.sourcePath.string(); request.sourceLoaded = true; request.metaLoaded = true;
        request.sourceCanonicalJson = JsonOf(document.sourceDocument.SerializedRootSnapshot()).dump();
        request.metaCanonicalJson = JsonOf(document.metaDocument.SerializedRootSnapshot()).dump();
        const auto published = api.PublishAssetWorkingCopy(request); why = published.message; return published.success;
    });
    VansSceneData editorData; editorData.sceneGuid = VansAssetGuid::New(); editorData.settings = VansSceneSchema::MakeDefaultSettings();
    auto editorScene = VansSceneSchema::SerializeSceneJson(editorData);
    const auto editorRoot = VansAssetGuid::New().ToString();
    editorScene["entities"].push_back(JsonOf(VansSceneEntityFactory::BuildEmptyEntity({}, editorRoot)));
    editorScene["entities"][0]["name"] = "Editor Crate";
    auto editorDocument = VansSceneDocument::CreateInMemory(DecodeSerializedValueJson(editorScene), VansPrefabEditService::Lookup(api), error);
    Check(editorDocument != nullptr, error);
    const auto editorScenePath = projectPath / "MainScene.json";
    Check(VansSceneFileStorage::WriteSceneDocument(
        editorScenePath, JsonOf(editorDocument->AuthoringRootSnapshot()), error), error);
    auto loadedEditorDocument = VansSceneDocumentLoader::Load(
        editorScenePath, VansPrefabEditService::Lookup(api));
    Check(bool(loadedEditorDocument), "Editor fixture scene failed to reopen");
    editorDocument = std::move(loadedEditorDocument.document);
    VansSceneEditService editorEdits(*editorDocument);
    std::filesystem::path createdPath;
    const auto created = VansPrefabEditService::Create(api, *editorDocument, editorEdits, editorRoot, projectPath / "Assets", createdPath);
    Check(created.success, "Real editor prefab creation failed: " + created.message);
    Check(std::filesystem::is_regular_file(createdPath) && std::filesystem::is_regular_file(VansAssetMeta::MetaPathFor(createdPath)),
        "Create did not publish prefab and metadata together");
    Check(JsonOf(editorDocument->SerializedRootSnapshot())["entities"] == editorScene["entities"], "Create changed the original scene objects");
    Check(editorEdits.Undo().success && JsonOf(editorDocument->AuthoringRootSnapshot())["entities"].size() == 1,
        "Creation undo did not disconnect the scene subtree");
    Check(editorEdits.Redo().success, "Creation redo failed");
    Check(editorEdits.Set(MakeDocumentPropertyPath(DocumentPropertySpace::Scene, "/entities/0/name"), VansSerializedValue::String("Applied Crate")).success,
        "Editor instance edit failed");
    const auto apply = VansPrefabEditService::Apply(api, *editorDocument, editorEdits, editorRoot);
    Check(apply.success, "Real editor Apply failed: " + apply.message);
    std::string beforeAsset, beforeScene;
    Check(VansFileStorage::ReadAllBytes(createdPath, beforeAsset, error), error);
    Check(VansFileStorage::ReadAllBytes(editorScenePath, beforeScene, error), error);
    const auto beforeWriteTime = std::filesystem::last_write_time(editorScenePath);
    Check(VansFileStorage::WriteAtomicBytes(editorScenePath, beforeScene + "\n", error), error);
    Check(!VansEditorAssetSaveService::Get().SaveSceneAndOwnedAssets(api, editorDocument.get()), "Compound save ignored a scene conflict");
    Check(VansFileStorage::ReadAllBytes(createdPath, bytes, error) && bytes == beforeAsset, "Failed scene save partially saved its applied template");
    Check(VansFileStorage::WriteAtomicBytes(editorScenePath, beforeScene, error), error);
    std::filesystem::last_write_time(editorScenePath, beforeWriteTime);
    const auto editorSave = VansEditorAssetSaveService::Get().SaveSceneAndOwnedAssets(api, editorDocument.get());
    Check(static_cast<bool>(editorSave), "Compound save failed: " + editorSave.message);
    const auto reopened = VansSceneDocumentLoader::Load(editorScenePath, VansPrefabEditService::Lookup(api));
    Check(reopened && JsonOf(reopened.document->SerializedRootSnapshot())["entities"][0]["name"] == "Applied Crate",
        "Compound save/open lost the applied source value");
    Check(!editorDocument->IsDirty() && !VansAssetDocumentRegistry::Get().HasDirtyDocuments(), "Compound save did not adopt all saved states");

    const std::filesystem::path engineRoot =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream prefabSourceFile(
        engineRoot / "Source" / "EngineCore" / "SceneCore" / "Prefab" / "VansPrefabAsset.cpp",
        std::ios::binary);
    const std::string prefabSource{
        std::istreambuf_iterator<char>(prefabSourceFile),
        std::istreambuf_iterator<char>() };
    const std::size_t instantiateBegin = prefabSource.find(
        "bool VansPrefabResolver::Instantiate(");
    const std::size_t resolveBegin = prefabSource.find(
        "bool VansPrefabResolver::ResolveScene(", instantiateBegin);
    const std::size_t captureBegin = prefabSource.find(
        "bool VansPrefabResolver::CaptureScene(", resolveBegin);
    Check(instantiateBegin != std::string::npos && resolveBegin != std::string::npos &&
        captureBegin != std::string::npos,
        "Prefab runtime resolution source boundaries are unavailable");
    const std::string instantiateSource = prefabSource.substr(
        instantiateBegin, resolveBegin - instantiateBegin);
    const std::string resolveSource = prefabSource.substr(
        resolveBegin, captureBegin - resolveBegin);
    Check(instantiateSource.find("LocalSerializedEntities") != std::string::npos &&
        instantiateSource.find("CompleteSerializedIdentityMap") != std::string::npos &&
        instantiateSource.find("ToJson(") == std::string::npos &&
        instantiateSource.find("DecodeSerializedValueJson") == std::string::npos &&
        resolveSource.find("sceneEntities->arrayItems.insert") != std::string::npos &&
        resolveSource.find("ToJson(objects)") == std::string::npos &&
        resolveSource.find("DecodeSerializedValueJson(record)") == std::string::npos,
        "Prefab runtime resolution restored repeated JSON tree conversions");
    std::cout << "PREFAB_CONTRACT_PASS identity references timeline external-binding overrides roundtrip edit undo redo save reopen unpack transaction bootstrap apply duplicate dynamic-dependencies packaged-source-removal editor-create-import compound-save-conflict runtime-resolution=serialized-value\n";
}
