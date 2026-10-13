#include "VansNavigationMeshCodec.h"
#include "../Detail/VansNavigationTileData.h"
#include "../Detail/VansNavigationTileCacheLayerCodec.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <algorithm>
#include <unordered_set>
#include "../../Util/VansFileFingerprint.h"

namespace Vans
{
namespace
{
constexpr std::uint64_t kNavigationMagic = 0x4853454D56414E56ull; // VNAVMESH: one current tiled/layer layout
constexpr std::uint32_t kMaximumStringBytes = 4096u;
struct Writer
{
    std::string bytes;
    void U8(std::uint8_t value) { bytes.push_back(static_cast<char>(value)); }
    void U32(std::uint32_t value) { for (int shift = 0; shift < 32; shift += 8) U8(static_cast<std::uint8_t>(value >> shift)); }
    void U64(std::uint64_t value) { for (int shift = 0; shift < 64; shift += 8) U8(static_cast<std::uint8_t>(value >> shift)); }
    void Float(float value) { std::uint32_t bits; static_assert(sizeof(value) == sizeof(bits)); std::memcpy(&bits, &value, sizeof(bits)); U32(bits); }
    void String(const std::string& value) { U32(static_cast<std::uint32_t>(value.size())); bytes.append(value); }
};
struct Reader
{
    std::string_view bytes;
    std::size_t cursor = 0;
    bool U8(std::uint8_t& value) { if (cursor == bytes.size()) return false; value = static_cast<std::uint8_t>(bytes[cursor++]); return true; }
    bool U32(std::uint32_t& value)
    {
        if (bytes.size() - cursor < 4u) return false;
        value = 0; for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[cursor++])) << shift;
        return true;
    }
    bool U32(int& value) { std::uint32_t integer; if (!U32(integer) || integer > static_cast<std::uint32_t>((std::numeric_limits<int>::max)())) return false; value = static_cast<int>(integer); return true; }
    bool I32(int& value) { std::uint32_t bits; static_assert(sizeof(value) == sizeof(bits)); if (!U32(bits)) return false; std::memcpy(&value, &bits, sizeof(bits)); return true; }
    bool U64(std::uint64_t& value)
    {
        if (bytes.size() - cursor < 8u) return false;
        value = 0; for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[cursor++])) << shift;
        return true;
    }
    bool Bool(bool& value) { std::uint8_t integer; if (!U8(integer) || integer > 1u) return false; value = integer != 0; return true; }
    bool Float(float& value) { std::uint32_t bits; if (!U32(bits)) return false; std::memcpy(&value, &bits, sizeof(bits)); return std::isfinite(value); }
    bool String(std::string& value) { std::uint32_t size; if (!U32(size) || !size || size > kMaximumStringBytes || size > bytes.size() - cursor) return false; value.assign(bytes.substr(cursor, size)); cursor += size; return true; }
};
bool ValidString(const std::string& value) { return !value.empty() && value.size() <= kMaximumStringBytes; }
void WriteBake(Writer& writer, const VansNavigationBakeSettings& bake)
{
    writer.Float(bake.cellSize);
    writer.Float(bake.cellHeight);
    writer.Float(bake.agentHeight);
    writer.Float(bake.agentRadius);
    writer.Float(bake.agentMaxClimb);
    writer.Float(bake.agentMaxSlopeDegrees);
    writer.Float(bake.regionMinSize);
    writer.Float(bake.regionMergeSize);
    writer.Float(bake.edgeMaxLength);
    writer.Float(bake.edgeMaxError);
    writer.U32(bake.maximumVerticesPerPolygon);
    writer.U8(bake.detailSamplingEnabled);
    writer.Float(bake.detailSampleDistance);
    writer.Float(bake.detailSampleMaxError);
    writer.U32(bake.tileSizeCells);
    writer.Float(bake.tileOrigin.x); writer.Float(bake.tileOrigin.y); writer.Float(bake.tileOrigin.z);
    writer.U32(bake.maximumTiles); writer.U32(bake.maximumPolygonsPerTile); writer.U32(bake.maximumBuildBytes);
    writer.U8(bake.buildTileCacheLayers?1u:0u);
}
bool ReadBake(Reader& reader, VansNavigationBakeSettings& bake)
{
    return !(!reader.Float(bake.cellSize) ||
        !reader.Float(bake.cellHeight) ||
        !reader.Float(bake.agentHeight) ||
        !reader.Float(bake.agentRadius) ||
        !reader.Float(bake.agentMaxClimb) ||
        !reader.Float(bake.agentMaxSlopeDegrees) ||
        !reader.Float(bake.regionMinSize) ||
        !reader.Float(bake.regionMergeSize) ||
        !reader.Float(bake.edgeMaxLength) ||
        !reader.Float(bake.edgeMaxError) ||
        !reader.U32(bake.maximumVerticesPerPolygon) ||
        !reader.Bool(bake.detailSamplingEnabled) ||
        !reader.Float(bake.detailSampleDistance) ||
        !reader.Float(bake.detailSampleMaxError) ||
        !reader.U32(bake.tileSizeCells) || !reader.Float(bake.tileOrigin.x) ||
        !reader.Float(bake.tileOrigin.y) || !reader.Float(bake.tileOrigin.z) ||
        !reader.U32(bake.maximumTiles) || !reader.U32(bake.maximumPolygonsPerTile) ||
        !reader.U32(bake.maximumBuildBytes) || !reader.Bool(bake.buildTileCacheLayers));
}
bool ValidId(const std::string& value)
{
    return !value.empty() && value.size() <= 64u && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c <= 32u || c == 127u; });
}
}

bool VansNavigationMeshCodec::Encode(const VansNavigationMeshAsset& asset, std::string& bytes, std::string& error)
try
{
    std::vector<VansNavigationProfileBuildData> profiles; profiles.reserve(asset.GetProfiles().size());
    std::uint64_t readBytes=asset.EstimateResidentBytes();
    for(const auto& index:asset.GetProfiles())
    {
        const auto estimate=asset.EstimateProfileReadBytes(index.profileId);
        if(readBytes>maximumAssetBytes || estimate>maximumAssetBytes-readBytes)
        { error="Navigation encoding Profile read exceeds peak memory capacity"; return false; }
        VansNavigationProfileBuildData profile;
        if(!asset.ReadProfile(index.profileId,profile,error)) return false;
        readBytes+=profile.EstimateBytes(); profiles.push_back(std::move(profile));
    }
    if (!asset.IsReady() || !ValidId(asset.m_MapId) || !ValidateNavigationSourceIdentity(asset.m_Source.scene, error) ||
        profiles.empty() || profiles.size() > 16u || !ValidateNavigationAreaSettings(asset.m_BakedAreas, error))
    { if (error.empty()) error = "Navigation asset metadata is invalid"; return false; }
    std::unordered_set<std::string> ids;
    for (const auto& profile : profiles)
        if (!ValidId(profile.profileId) || !ids.insert(profile.profileId).second ||
            profile.bakeSettingsHash != HashNavigationBakeSettings(profile.bake) ||
            !ValidateNavigationBakeSettings(profile.bake, error) ||
            !ValidateNavigationTileSet(profile.tiles, profile.bake, asset.m_BakedAreas, error) || !profile.baseInputHash ||
            (!profile.bake.buildTileCacheLayers && !profile.layerData.empty()) ||
            !ValidateNavigationTileLayerData(profile.layerData,profile.baseInputHash,profile.bake,asset.m_BakedAreas,maximumAssetBytes,error))
        { if (error.empty()) error = "Navigation asset Profile metadata is invalid"; return false; }
    std::uint64_t inputHash=VANS_FNV1A64_OFFSET_BASIS;
    for(const auto& profile:profiles)
    {
        if(!profile.inputHash || profile.contributions.size()>262144)
        { error="Navigation Profile source fingerprint exceeds capacity or is empty"; return false; }
        std::unordered_set<std::string> contributions;
        for(const auto& item:profile.contributions)
            if(!ValidString(item.id) || item.id.size()>512 || !item.contentHash || !contributions.insert(item.id).second)
            { error="Navigation contribution identity/fingerprint is invalid"; return false; }
        inputHash=ContinueMemoryFnv1a64(inputHash,&profile.inputHash,sizeof(profile.inputHash));
    }
    if(inputHash!=asset.m_Source.inputHash) { error="Navigation source aggregate fingerprint is invalid"; return false; }
    for (const auto& area : asset.m_BakedAreas.definitions)
        if (!ValidString(area.name)) { error = "Navigation area name exceeds capacity"; return false; }
    const auto stringBytes=[](const std::string& value){return 4u+value.size();};
    std::uint64_t encodedBytes=8u+stringBytes(asset.m_MapId)+stringBytes(asset.m_Source.scene)+8u+
        stringBytes(asset.m_BakedAreas.defaultArea)+8u+asset.GetPayloadBytes();
    for(const auto& area:asset.m_BakedAreas.definitions) encodedBytes+=stringBytes(area.name)+6u;
    for(const auto& profile:profiles)
    {
        encodedBytes+=stringBytes(profile.profileId)+82u+24u+12u+profile.tiles.size()*20u;
        for(const auto& item:profile.contributions) encodedBytes+=stringBytes(item.id)+8u;
        for(const auto& row:profile.layerData) encodedBytes+=36u+row.layers.size()*12u;
    }
    if(encodedBytes>maximumAssetBytes || readBytes>maximumAssetBytes-encodedBytes) { error="Navigation asset index/payload exceeds byte capacity"; return false; }
    Writer writer; writer.bytes.reserve(static_cast<std::size_t>(encodedBytes));
    writer.U64(kNavigationMagic);
    writer.String(asset.m_MapId);
    writer.String(asset.m_Source.scene); writer.U64(asset.m_Source.inputHash);
    writer.String(asset.m_BakedAreas.defaultArea);
    writer.U32(static_cast<std::uint32_t>(asset.m_BakedAreas.definitions.size()));
    for (const auto& area : asset.m_BakedAreas.definitions)
    { writer.String(area.name); writer.U8(area.id); writer.Float(area.traversalCost); writer.U8(area.traversable ? 1u : 0u); }
    writer.U32(static_cast<std::uint32_t>(profiles.size()));
    for (const auto& profile : profiles)
    {
        writer.String(profile.profileId); WriteBake(writer, profile.bake); writer.U64(profile.bakeSettingsHash);
        writer.U64(profile.inputHash); writer.U64(profile.baseInputHash);
        writer.U32(static_cast<std::uint32_t>(profile.contributions.size()));
        for(const auto& item:profile.contributions) { writer.String(item.id); writer.U64(item.contentHash); }
        writer.U32(static_cast<std::uint32_t>(profile.layerData.size()));
        for(const auto& row:profile.layerData)
        {
            writer.U32(static_cast<std::uint32_t>(row.x)); writer.U32(static_cast<std::uint32_t>(row.z));
            for(int axis=0;axis<3;++axis) writer.Float(row.boundsMinimum[axis]);
            for(int axis=0;axis<3;++axis) writer.Float(row.boundsMaximum[axis]);
            writer.U32(static_cast<std::uint32_t>(row.layers.size()));
            for(const auto& layer:row.layers) { writer.U32(static_cast<std::uint32_t>(layer.layer)); writer.U64(layer.bytes.size()); }
        }
        writer.U32(static_cast<std::uint32_t>(profile.tiles.size()));
        for (const auto& tile : profile.tiles)
        { writer.U32(static_cast<std::uint32_t>(tile.x)); writer.U32(static_cast<std::uint32_t>(tile.z));
          writer.U32(static_cast<std::uint32_t>(tile.layer)); writer.U64(tile.bytes.size()); }
    }
    // 所有索引先行，cooked payload 连续；解码完成总预算验证后才复制 payload。
    for (const auto& profile : profiles) for (const auto& tile : profile.tiles)
        writer.bytes.append(reinterpret_cast<const char*>(tile.bytes.data()), tile.bytes.size());
    for(const auto& profile:profiles) for(const auto& row:profile.layerData) for(const auto& layer:row.layers)
        writer.bytes.append(reinterpret_cast<const char*>(layer.bytes.data()),layer.bytes.size());
    if(writer.bytes.size()!=encodedBytes) { error="Navigation asset size invariant failed"; return false; }
    if (writer.bytes.size() > maximumAssetBytes) { error = "Navigation asset exceeds byte capacity"; return false; }
    bytes = std::move(writer.bytes); error.clear(); return true;
}
catch (const std::bad_alloc&) { error = "Navigation codec allocation failed"; return false; }

bool VansNavigationMeshCodec::Decode(std::string_view bytes, std::shared_ptr<const VansNavigationMeshAsset>& asset, std::string& error)
try
{
    if (bytes.empty() || bytes.size() > maximumAssetBytes)
    { error = "Navigation asset exceeds byte capacity or is empty"; return false; }
    Reader reader{ bytes };
    std::uint64_t magic = 0;
    auto candidate = std::shared_ptr<VansNavigationMeshAsset>(new VansNavigationMeshAsset());
    std::uint32_t count = 0;
    if (!reader.U64(magic) || magic != kNavigationMagic || !reader.String(candidate->m_MapId) || !ValidId(candidate->m_MapId) ||
        !reader.String(candidate->m_Source.scene) || !reader.U64(candidate->m_Source.inputHash) ||
        !reader.String(candidate->m_BakedAreas.defaultArea) || !reader.U32(count) || count == 0 || count > 63u)
    { error = "Navigation asset Map/source/Area header is invalid"; return false; }
    if (!ValidateNavigationSourceIdentity(candidate->m_Source.scene, error)) return false;
    candidate->m_BakedAreas.definitions.clear();
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VansNavigationAreaDefinition area;
        if (!reader.String(area.name) || !reader.U8(area.id) || !reader.Float(area.traversalCost) || !reader.Bool(area.traversable))
        { error = "Navigation asset Area definition is invalid"; return false; }
        candidate->m_BakedAreas.definitions.push_back(std::move(area));
    }
    if (!ValidateNavigationAreaSettings(candidate->m_BakedAreas, error) || !reader.U32(count) || count == 0 || count > 16u)
    { if (error.empty()) error = "Navigation Profile count is invalid"; return false; }
    struct ProfileSizes
    {
        std::vector<std::uint64_t> tiles;
        std::vector<std::vector<std::uint64_t>> layers;
    };
    std::vector<ProfileSizes> sizes; sizes.reserve(count);
    std::vector<VansNavigationProfileBuildData> profiles; profiles.reserve(count);
    std::unordered_set<std::string> ids;
    std::uint64_t totalBytes = 0;
    // 文件索引会展开为拥有型向量；在每次 reserve/复制前计入本轮峰值，
    // 避免合法文件长度下的恶意索引先分配超限，再在末尾拒绝。
    std::uint64_t decodedPeakBytes=bytes.size()+candidate->EstimateResidentBytes()+
        sizes.capacity()*sizeof(ProfileSizes)+profiles.capacity()*sizeof(VansNavigationProfileBuildData)+65536u;
    const auto reserveBytes=[&](std::uint64_t additional)
    {
        if(decodedPeakBytes>maximumAssetBytes || additional>maximumAssetBytes-decodedPeakBytes)
        { error="Navigation decoded owned payload/index exceeds byte capacity before allocation"; return false; }
        decodedPeakBytes+=additional; return true;
    };
    std::uint64_t tileValidationBytes=0;
    std::uint64_t inputHash=VANS_FNV1A64_OFFSET_BASIS;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VansNavigationProfileBuildData profile;
        std::uint32_t tiles = 0, contributions = 0;
        if (!reader.String(profile.profileId) || !ValidId(profile.profileId) || !ids.insert(profile.profileId).second ||
            !ReadBake(reader, profile.bake) || !ValidateNavigationBakeSettings(profile.bake, error) ||
            !reader.U64(profile.bakeSettingsHash) || profile.bakeSettingsHash != HashNavigationBakeSettings(profile.bake) ||
            !reader.U64(profile.inputHash) || !profile.inputHash || !reader.U64(profile.baseInputHash) || !profile.baseInputHash || !reader.U32(contributions) || contributions>262144 ||
            contributions>(bytes.size()-reader.cursor)/13u)
        { if (error.empty()) error = "Navigation Profile fingerprint or contribution count is invalid"; return false; }
        const auto contributionScratch=static_cast<std::uint64_t>(contributions)*128u;
        if(!reserveBytes(profile.profileId.capacity()+1u+static_cast<std::uint64_t>(contributions)*
            sizeof(VansNavigationContribution)+contributionScratch)) return false;
        profile.contributions.reserve(contributions);
        // 已预留的 vector 保持 string 地址稳定，唯一性集合不复制来源名称。
        std::unordered_set<std::string_view> contributionIds;
        contributionIds.reserve(contributions);
        for(std::uint32_t j=0;j<contributions;++j)
        {
            VansNavigationContribution item;
            if(!reader.String(item.id) || item.id.size()>512 || !reader.U64(item.contentHash) || !item.contentHash)
            { error="Navigation contribution identity or fingerprint is invalid"; return false; }
            if(!reserveBytes(item.id.capacity()+1u)) return false;
            profile.contributions.push_back(std::move(item));
            if(!contributionIds.insert(profile.contributions.back().id).second)
            { error="Navigation contribution identity or fingerprint is invalid"; return false; }
        }
        inputHash=ContinueMemoryFnv1a64(inputHash,&profile.inputHash,sizeof(profile.inputHash));
        ProfileSizes profileSizes;
        std::uint32_t rows=0;
        if(!reader.U32(rows) || rows>static_cast<unsigned>(profile.bake.maximumTiles) ||
            rows>(bytes.size()-reader.cursor)/48u || (!profile.bake.buildTileCacheLayers && rows))
        { error="Navigation base layer row index is invalid"; return false; }
        if(!reserveBytes(static_cast<std::uint64_t>(rows)*(sizeof(VansNavigationTileLayerData)+
            sizeof(std::vector<std::uint64_t>)))) return false;
        profile.layerData.reserve(rows); profileSizes.layers.reserve(rows);
        for(std::uint32_t rowIndex=0;rowIndex<rows;++rowIndex)
        {
            VansNavigationTileLayerData row; row.baseInputHash=profile.baseInputHash;
            std::uint32_t layers=0;
            if(!reader.I32(row.x) || !reader.I32(row.z)) { error="Navigation layer row key is invalid"; return false; }
            for(int axis=0;axis<3;++axis)
                if(!reader.Float(row.boundsMinimum[axis])) { error="Navigation layer raster minimum is invalid"; return false; }
            for(int axis=0;axis<3;++axis)
                if(!reader.Float(row.boundsMaximum[axis]) || row.boundsMinimum[axis]>row.boundsMaximum[axis])
                { error="Navigation layer raster maximum is invalid"; return false; }
            const VansNavigationTileCell cell{row.x,row.z};
            if(row.x<-(1<<28) || row.x>(1<<28) || row.z<-(1<<28) || row.z>(1<<28) ||
                (rowIndex && !(VansNavigationTileCell{profile.layerData.back().x,profile.layerData.back().z}<cell)) ||
                !reader.U32(layers) || !layers || layers>8u || layers>(bytes.size()-reader.cursor)/12u)
            { error="Navigation base layer cell order or count is invalid"; return false; }
            if(!reserveBytes(static_cast<std::uint64_t>(layers)*(sizeof(VansNavigationTileCacheLayerPayload)+
                sizeof(std::uint64_t)))) return false;
            std::vector<std::uint64_t> layerSizes; layerSizes.reserve(layers); row.layers.reserve(layers);
            for(std::uint32_t layerIndex=0;layerIndex<layers;++layerIndex)
            {
                int layer=0; std::uint64_t size=0;
                if(!reader.U32(layer) || layer!=static_cast<int>(layerIndex) || !reader.U64(size) ||
                    !size || size>8u*1024u*1024u || size>maximumAssetBytes-totalBytes)
                { error="Navigation compressed layer index or byte size is invalid"; return false; }
                if(!reserveBytes(size)) return false;
                totalBytes+=size; layerSizes.push_back(size); row.layers.push_back({row.x,row.z,layer,{}});
            }
            profile.layerData.push_back(std::move(row)); profileSizes.layers.push_back(std::move(layerSizes));
        }
        if(!reader.U32(tiles) || tiles==0 || tiles>static_cast<unsigned>(profile.bake.maximumTiles) || tiles>(bytes.size()-reader.cursor)/20u)
        { error="Navigation Profile Tile index is invalid"; return false; }
        const auto validationBytes=static_cast<std::uint64_t>(tiles)*96u;
        if(!reserveBytes(static_cast<std::uint64_t>(tiles)*(sizeof(VansNavigationTilePayload)+sizeof(std::uint64_t))+
            (validationBytes>tileValidationBytes?validationBytes-tileValidationBytes:0u))) return false;
        tileValidationBytes=(std::max)(tileValidationBytes,validationBytes);
        profileSizes.tiles.reserve(tiles); profile.tiles.reserve(tiles);
        for (std::uint32_t tileIndex = 0; tileIndex < tiles; ++tileIndex)
        {
            VansNavigationTilePayload tile;
            std::uint64_t size = 0;
            if (!reader.I32(tile.x) || !reader.I32(tile.z) || !reader.U32(tile.layer) || !reader.U64(size) ||
                size == 0 || size > 8u * 1024u * 1024u || size > maximumAssetBytes - totalBytes)
            { error = "Navigation Tile index key or byte size is invalid"; return false; }
            if(!reserveBytes(size)) return false;
            totalBytes += size; profileSizes.tiles.push_back(size); profile.tiles.push_back(std::move(tile));
        }
        sizes.push_back(std::move(profileSizes)); profiles.push_back(std::move(profile));
        decodedPeakBytes-=contributionScratch;
    }
    if (inputHash!=candidate->m_Source.inputHash || totalBytes != bytes.size() - reader.cursor)
    { error = "Navigation Tile payload is truncated or has trailing bytes"; return false; }
    std::uint64_t indexBytes=sizes.capacity()*sizeof(ProfileSizes);
    for(const auto& index:sizes)
    {
        indexBytes+=index.tiles.capacity()*sizeof(std::uint64_t)+index.layers.capacity()*sizeof(std::vector<std::uint64_t>);
        for(const auto& row:index.layers) indexBytes+=row.capacity()*sizeof(std::uint64_t);
    }
    const auto workingBytes=[&](){std::uint64_t bytes=profiles.capacity()*sizeof(VansNavigationProfileBuildData); for(const auto& profile:profiles) bytes+=profile.EstimateBytes()-sizeof(profile); return bytes;};
    if(bytes.size()+candidate->EstimateResidentBytes()+workingBytes()+totalBytes+indexBytes>maximumAssetBytes)
    { error="Navigation decoded owned payload/index exceeds byte capacity"; return false; }
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        auto& profile = profiles[i];
        for (std::size_t tile = 0; tile < sizes[i].tiles.size(); ++tile)
        {
            const auto size = static_cast<std::size_t>(sizes[i].tiles[tile]);
            profile.tiles[tile].bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(reader.cursor),
                bytes.begin() + static_cast<std::ptrdiff_t>(reader.cursor + size));
            reader.cursor += size;
        }
        if (!ValidateNavigationTileSet(profile.tiles, profile.bake, candidate->m_BakedAreas, error)) return false;
    }
    for(std::size_t i=0;i<sizes.size();++i)
    {
        auto& profile=profiles[i];
        for(std::size_t row=0;row<sizes[i].layers.size();++row)
            for(std::size_t layer=0;layer<sizes[i].layers[row].size();++layer)
            {
                const auto size=static_cast<std::size_t>(sizes[i].layers[row][layer]);
                profile.layerData[row].layers[layer].bytes.assign(bytes.begin()+static_cast<std::ptrdiff_t>(reader.cursor),
                    bytes.begin()+static_cast<std::ptrdiff_t>(reader.cursor+size)); reader.cursor+=size;
            }
    }
    const auto retained=bytes.size()+candidate->EstimateResidentBytes()+workingBytes()+indexBytes;
    if(retained>maximumAssetBytes) { error="Navigation decoded asset exceeds byte capacity"; return false; }
    for(const auto& profile:profiles)
    {
        std::uint64_t rows=profile.layerData.capacity()*sizeof(VansNavigationTileLayerData);
        for(const auto& row:profile.layerData) rows+=row.EstimateBytes()-sizeof(row);
        if(!ValidateNavigationTileLayerData(profile.layerData,profile.baseInputHash,profile.bake,candidate->m_BakedAreas,
            maximumAssetBytes-(retained-rows),error)) return false;
    }
    if(bytes.size()+indexBytes>maximumAssetBytes ||
        !candidate->PublishProfiles(std::move(profiles),maximumAssetBytes-indexBytes-bytes.size(),error)) return false;
    asset = std::move(candidate); error.clear(); return true;
}
catch (const std::bad_alloc&) { error = "Navigation codec allocation failed"; return false; }
}
