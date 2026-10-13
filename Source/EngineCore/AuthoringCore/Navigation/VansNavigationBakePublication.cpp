#include "VansNavigationBakePublication.h"
#include "../../NavigationCore/Storage/VansNavigationMeshStorage.h"
#include "../../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cwctype>
#include <new>

namespace Vans
{
namespace
{
bool InsideAssets(const std::filesystem::path& path,const std::filesystem::path& root)
{
    auto part=path.begin();
    for(const auto& directory:root)
    {
        if(part==path.end()) return false;
        auto actual=part->wstring(),expected=directory.wstring();
        std::transform(actual.begin(),actual.end(),actual.begin(),std::towlower);
        std::transform(expected.begin(),expected.end(),expected.begin(),std::towlower);
        if(actual!=expected) return false;
        ++part;
    }
    return part!=path.end();
}
}

struct VansNavigationBakePublication::State
{
    State(VansAssetDatabase& db,VansAssetObjectRepository& objects):database(db),repository(objects) {}
    VansAssetDatabase& database;
    VansAssetObjectRepository& repository;
    VansStagedFileTransaction transaction;
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    std::filesystem::path path;
    std::optional<VansAssetRecord> previous;
    VansAssetRecord record;
    std::vector<std::filesystem::path> newDirectories;
    bool registrationAttempted=false,committed=false;

    ~State()
    {
        if(committed) return;
        transaction.Cleanup();
        if(registrationAttempted)
        {
            if(previous)
            {
                std::string error;
                if(!database.RegisterOrRefresh(path,VansAssetOperationPolicy::ReadOnly(),error))
                    VANS_LOG_ERROR("[NavigationBake] Asset index rollback failed: "<<error);
            }
            else database.RemovePath(path);
        }
        // 仅移除本次创建且仍为空的目录，不递归清理任何用户资产。
        for(const auto& directory:newDirectories)
        {
            std::error_code ignored;
            std::filesystem::remove(directory,ignored);
        }
    }
};

VansNavigationBakePublication::VansNavigationBakePublication(std::unique_ptr<State> state):m_State(std::move(state)) {}
VansNavigationBakePublication::~VansNavigationBakePublication()=default;
const VansAssetRecord& VansNavigationBakePublication::Record() const { return m_State->record; }

std::unique_ptr<VansNavigationBakePublication> VansNavigationBakePublication::Prepare(VansAssetDatabase& database,
    VansAssetObjectRepository& repository,std::shared_ptr<const VansNavigationMeshAsset> asset,
    const std::filesystem::path& requestedPath,std::string& error)
try
{
    if(!asset || !asset->IsReady()) { error="Navigation Bake requires a complete ready candidate"; return {}; }
    std::error_code ec;
    const auto root=std::filesystem::weakly_canonical(database.AssetsRoot(),ec);
    if(ec) { error="Cannot resolve Navigation Bake Assets root: "+ec.message(); return {}; }
    const auto path=std::filesystem::weakly_canonical(requestedPath,ec);
    if(ec || path.extension()!=".vnavmesh" || !InsideAssets(path,root))
    { error="Navigation Bake output must be a .vnavmesh file inside project Assets"; return {}; }
    auto state=std::make_unique<State>(database,repository);
    state->path=path; state->asset=std::move(asset); state->previous=database.Find(path);
    if(state->previous && state->previous->type!=VansAssetType::NavigationMesh)
    { error="Navigation Bake cannot replace a different asset type"; return {}; }
    VansScopedIOContext io(VansIODomain::Authoring,"Navigation.Bake",true);
    const auto metaPath=VansAssetMeta::MetaPathFor(path);
    const bool existingMeta=std::filesystem::exists(metaPath);
    VansAssetMeta meta;
    if(existingMeta)
    {
        if(!VansAssetMetaStorage::Load(metaPath,meta,error) || !meta.guid.IsValid() ||
            meta.importer!=VansAssetDatabase::ImporterFor(VansAssetType::NavigationMesh))
        { if(error.empty()) error="Navigation Bake metadata has an invalid GUID or importer"; return {}; }
    }
    else { meta.guid=VansAssetGuid::New(); meta.importer=VansAssetDatabase::ImporterFor(VansAssetType::NavigationMesh); }
    if(state->previous && state->previous->guid!=meta.guid)
    { error="Navigation Bake metadata identity changed after indexing"; return {}; }
    VansAssetObjectSnapshotInfo info;
    if(repository.FindInfo(meta.guid,info) && (info.assetType!=VansAssetType::NavigationMesh || !repository.ResolveLatest<VansNavigationMeshAsset>(meta.guid)))
    { error="Navigation Bake GUID is owned by a different decoded asset"; return {}; }
    for(auto directory=path.parent_path(); directory!=root && !std::filesystem::exists(directory); directory=directory.parent_path())
        state->newDirectories.push_back(directory);
    VansStagedFile stage;
    if(!VansNavigationMeshStorage::StageSave(path,*state->asset,stage,error)) return {};
    state->transaction.Add(std::move(stage));
    if(!existingMeta)
    {
        if(!VansAssetMetaStorage::StageSave(metaPath,meta,stage,error)) return {};
        stage.requireAbsent=true;
        state->transaction.Add(std::move(stage));
    }
    if(!state->transaction.PreparePublish(error)) return {};
    state->registrationAttempted=true;
    if(!database.RegisterOrRefresh(path,VansAssetOperationPolicy::ReadOnly(),error)) return {};
    const auto record=database.Find(path);
    if(!record || record->guid!=meta.guid) { error="Navigation Bake index did not publish the expected GUID"; return {}; }
    state->record=*record;
    error.clear();
    return std::unique_ptr<VansNavigationBakePublication>(new VansNavigationBakePublication(std::move(state)));
}
catch(const std::exception& exception) { error=exception.what(); return {}; }

bool VansNavigationBakePublication::Commit(std::string& error)
try
{
    if(m_State->committed) { error.clear(); return true; }
    if(!m_State->repository.Publish(m_State->record.guid,VansAssetType::NavigationMesh,
        AssetObjectContentHash(m_State->record),m_State->asset,{},error).IsValid()) return false;
    m_State->transaction.Commit(); m_State->committed=true;
    error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation Bake publication allocation failed"; return false; }
}
