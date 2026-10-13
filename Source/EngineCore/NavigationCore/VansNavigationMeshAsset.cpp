#include "VansNavigationMeshAsset.h"
#include "VansNavigationBuilder.h"
#include <algorithm>
#include <new>

namespace Vans
{
namespace
{
std::uint64_t MetadataBytes(const VansNavigationProfileMetadata& profile)
{
    std::uint64_t bytes=profile.profileId.capacity()+1+
        profile.contributions.capacity()*sizeof(VansNavigationContribution);
    for(const auto& contribution:profile.contributions) bytes+=contribution.id.capacity()+1;
    return bytes;
}
}
std::uint64_t VansNavigationProfileBuildData::EstimateBytes() const
{
    std::uint64_t bytes=sizeof(*this)+MetadataBytes(*this)+tiles.capacity()*sizeof(VansNavigationTilePayload)+
        layerData.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& tile:tiles) bytes+=tile.bytes.capacity();
    for(const auto& row:layerData) bytes+=row.EstimateBytes()-sizeof(row);
    return bytes;
}
std::uint64_t VansNavigationLayerRead::EstimateBytes() const
{
    std::uint64_t bytes=sizeof(*this)+profileId.capacity()+1+cells.capacity()*sizeof(VansNavigationTileCell)+
        current.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& row:current) bytes+=row.EstimateBytes()-sizeof(row);
    return bytes;
}
bool VansNavigationLayerRead::Read(std::uint64_t baseInputHash,const VansNavigationBuildControl* control,
    std::vector<VansNavigationTileLayerData>& output,std::string& error) const
try
{
    const auto* profile=asset?asset->FindProfile(profileId):nullptr;
    if(!profile || !baseInputHash || cells.size()>kMaximumNavigationTileUpdateBatch ||
        !std::is_sorted(cells.begin(),cells.end()) || std::adjacent_find(cells.begin(),cells.end())!=cells.end())
    { error="Navigation layer read has invalid owned source/cells"; return false; }
    if(control && control->IsCancelled()) { error="Navigation layer read cancelled"; return false; }
    std::uint64_t bytes=EstimateBytes()+output.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& row:output) bytes+=row.EstimateBytes()-sizeof(row);
    const auto metadata=cells.size()*sizeof(VansNavigationTileLayerData);
    std::vector<VansNavigationTileLayerData> rows;
    if(bytes>maximumBytes || metadata>maximumBytes-bytes)
    { output=std::move(rows); error.clear(); return true; }
    bytes+=metadata; rows.reserve(cells.size());
    for(const auto cell:cells)
    {
        if(control && control->IsCancelled()) { error="Navigation layer read cancelled"; return false; }
        const auto present=std::lower_bound(current.begin(),current.end(),cell,
            [](const auto& row,const auto& key){return VansNavigationTileCell{row.x,row.z}<key;});
        if(present!=current.end() && VansNavigationTileCell{present->x,present->z}==cell)
        {
            const auto cost=present->EstimateBytes()-sizeof(*present);
            if(!present->layers.empty() && cost<=maximumBytes-bytes)
            { rows.push_back(*present); bytes+=cost; }
            continue;
        }
        // 来源改变后必须按当前冻结几何重建，旧基础层不能作为有效 seed。
        if(profile->baseInputHash!=baseInputHash) continue;
        const auto found=std::lower_bound(profile->layerData.begin(),profile->layerData.end(),cell,
            [](const auto& row,const auto& key){return VansNavigationTileCell{row.x,row.z}<key;});
        if(found==profile->layerData.end() || !(VansNavigationTileCell{found->x,found->z}==cell)) continue;
        const auto cost=found->EstimateBytes()-sizeof(*found);
        if(cost>maximumBytes-bytes) continue;
        rows.push_back(*found); bytes+=cost;
    }
    if(control && control->IsCancelled()) { error="Navigation layer read cancelled"; return false; }
    output=std::move(rows); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation layer read allocation failed"; return false; }
std::size_t VansNavigationMeshAsset::GetPayloadBytes() const
{
    std::size_t bytes=0;
    for(const auto& profile:m_Profiles)
    {
        for(const auto& tile:profile.tiles) bytes+=tile.bytes.size();
        for(const auto& row:profile.layerData)
            for(const auto& layer:row.layers) bytes+=layer.bytes.size();
    }
    return bytes;
}
std::uint64_t VansNavigationMeshAsset::EstimateResidentBytes() const
{
    std::uint64_t bytes=sizeof(*this)+m_MapId.capacity()+1+m_Source.scene.capacity()+1+
        m_Profiles.capacity()*sizeof(VansNavigationProfileBuildData)+m_BakedAreas.defaultArea.capacity()+1+
        m_BakedAreas.definitions.capacity()*sizeof(VansNavigationAreaDefinition);
    for(const auto& area:m_BakedAreas.definitions) bytes+=area.name.capacity()+1;
    for(const auto& profile:m_Profiles) bytes+=profile.EstimateBytes()-sizeof(profile);
    return bytes;
}
std::uint64_t VansNavigationMeshAsset::EstimateProfileReadBytes(const std::string& profileId) const
{
    const auto* profile=FindProfile(profileId);
    return profile?profile->EstimateBytes():0;
}
bool VansNavigationMeshAsset::ReadProfile(const std::string& profileId,VansNavigationProfileBuildData& output,std::string& error) const
try
{
    const auto* profile=FindProfile(profileId);
    if(!profile) { error="Navigation Profile data is unavailable"; return false; }
    // 先形成独立 CPU 快照；失败时保留调用方此前的完整结果。
    VansNavigationProfileBuildData candidate=*profile;
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation Profile read allocation failed"; return false; }
bool VansNavigationMeshAsset::PublishProfiles(std::vector<VansNavigationProfileBuildData> profiles,
    std::uint64_t maximumBytes,std::string& error)
{
    std::uint64_t working=profiles.capacity()*sizeof(VansNavigationProfileBuildData);
    for(const auto& profile:profiles) working+=profile.EstimateBytes()-sizeof(profile);
    const auto retained=EstimateResidentBytes();
    if(retained>maximumBytes || working>maximumBytes-retained)
    { error="Navigation Profile publication exceeds peak memory capacity"; return false; }
    m_Profiles=std::move(profiles); error.clear(); return true;
}
}
