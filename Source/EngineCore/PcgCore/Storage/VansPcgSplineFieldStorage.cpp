#include "VansPcgSplineFieldStorage.h"
#include <algorithm>
#include "../Serialization/VansPcgSplineAssetCodec.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <cmath>

namespace Vans
{
namespace
{
constexpr std::size_t MaxBytes=512u*1024u*1024u;
std::uint64_t Digest(const char* bytes,std::size_t count)
{
    std::uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<count;++i){hash^=static_cast<unsigned char>(bytes[i]);hash*=1099511628211ull;}
    return hash;
}
struct Writer
{
    std::string bytes;
    template<class T> void Value(const T& value)
    {static_assert(std::is_trivially_copyable_v<T>);bytes.append(reinterpret_cast<const char*>(&value),sizeof(T));}
    void Text(const std::string& text){Value(std::uint32_t(text.size()));bytes.append(text);}
    template<class T> void Array(const std::vector<T>& values)
    {
        static_assert(std::is_trivially_copyable_v<T>);Value(std::uint32_t(values.size()));
        if(!values.empty())bytes.append(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(T));
    }
};
struct Reader
{
    const std::string& bytes;std::size_t cursor=0;
    void Require(std::size_t size){if(size>bytes.size()-cursor)throw std::runtime_error("Truncated spline cache.");}
    template<class T> T Value()
    {static_assert(std::is_trivially_copyable_v<T>);Require(sizeof(T));T value;std::memcpy(&value,bytes.data()+cursor,sizeof(T));cursor+=sizeof(T);return value;}
    std::uint32_t Count(std::size_t maximum){const auto count=Value<std::uint32_t>();if(count>maximum)throw std::runtime_error("Spline cache exceeds its data budget.");return count;}
    std::string Text(){const auto count=Count(4096);Require(count);auto result=bytes.substr(cursor,count);cursor+=count;return result;}
    template<class T> std::vector<T> Array(std::size_t maximum)
    {
        const auto count=Count(maximum);Require(std::size_t(count)*sizeof(T));std::vector<T> values(count);
        if(count)std::memcpy(values.data(),bytes.data()+cursor,std::size_t(count)*sizeof(T));cursor+=std::size_t(count)*sizeof(T);return values;
    }
};
}
const char* VansPcgSplineFieldStorage::CompilerFingerprint(){return VANS_PCG_SPLINE_COMPILER_FINGERPRINT;}
std::filesystem::path VansPcgSplineFieldStorage::CachePath(const std::filesystem::path& root,VansAssetGuid guid)
{return root/"Library"/"PCG"/"Splines"/(guid.ToString()+".pcgfields");}

bool VansPcgSplineFieldStorage::Save(const std::filesystem::path& path,const VansPcgSplineFieldSnapshot& field,std::string& error)
{
    if(!field.effectiveTerrain){error="Cannot bake an incomplete field.";return false;}
    Writer w;w.Text("ForestSplineFields");w.Text(CompilerFingerprint());w.Text(field.terrainGuid.ToString());
    w.Value(field.sourceFingerprint);w.Value(field.terrainFingerprint);w.Value(field.worldSize);w.Value(field.texelSize);w.Value(field.resolution);
    w.Array(field.effectiveTerrain->heights);
    w.Value(std::uint32_t(field.tiles.size()));
    for(const auto& [key,pointer]:field.tiles)
    {
        const auto& t=*pointer;w.Value(key);w.Value(t.fingerprint);w.Value(t.terrainShapeFingerprint);w.Array(t.heights);w.Array(t.velocities);w.Array(t.coverage);w.Array(t.vegetationExclusion);
        w.Value(t.minimumWaterHeight);w.Value(t.maximumWaterHeight);w.Value(t.minimumRiverWidth);w.Value(t.maximumHeightConflict);w.Value(std::uint8_t(t.hasRiver));
        w.Value(std::uint32_t(t.domains.size()));
        for(const auto& d:t.domains){w.Text(d.splineId);w.Value(d.cycleSeconds);w.Value(std::uint8_t(d.flowEnabled));w.Array(d.coordinates);w.Array(d.jacobians);}
    }
    w.Value(std::uint32_t(field.roads.size()));
    for(const auto& [id,road]:field.roads){w.Text(id);w.Text(road->material.ToString());w.Value(road->fingerprint);w.Array(road->vertices);w.Array(road->indices);}
    w.Value(std::uint32_t(field.warnings.size()));for(const auto& warning:field.warnings)w.Text(warning);
    w.Array(field.uncoveredBankPoints);
    w.Value(Digest(w.bytes.data(),w.bytes.size()));
    if(w.bytes.size()>MaxBytes){error="Spline bake exceeds the 512 MiB storage budget.";return false;}
    VansScopedIOContext scope(VansIODomain::Derived,"Pcg.BakeSplineFields");
    return VansFileStorage::WriteAtomicBytes(path,w.bytes,error);
}

std::shared_ptr<const VansPcgSplineFieldSnapshot> VansPcgSplineFieldStorage::Load(const std::filesystem::path& path,
    const VansPcgSplineAsset& source,std::shared_ptr<const VansTerrainAsset> base,std::string& error)
{
    error.clear();std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
    if(ec || size<8 || size>MaxBytes || !base){error="Spline cache is absent or exceeds its storage budget.";return {};}
    VansScopedIOContext scope(VansIODomain::Derived,"Pcg.LoadSplineFields");std::string bytes;
    if(!VansFileStorage::ReadAllBytes(path,bytes,error))return {};
    try
    {
        std::uint64_t digest;std::memcpy(&digest,bytes.data()+bytes.size()-sizeof(digest),sizeof(digest));
        if(Digest(bytes.data(),bytes.size()-sizeof(digest))!=digest)throw std::runtime_error("Spline cache checksum mismatch.");
        Reader r{bytes};
        if(r.Text()!="ForestSplineFields" || r.Text()!=CompilerFingerprint())throw std::runtime_error("Spline compiler content changed; rebake required.");
        auto field=std::make_shared<VansPcgSplineFieldSnapshot>();
        if(!VansAssetGuid::TryParse(r.Text(),field->terrainGuid)||field->terrainGuid!=source.terrain)throw std::runtime_error("Spline cache terrain mismatch.");
        field->sourceFingerprint=r.Value<std::uint64_t>();field->terrainFingerprint=r.Value<std::uint64_t>();
        if(field->sourceFingerprint!=VansPcgSplineAssetCodec::ContentHash(source)||field->terrainFingerprint!=HashTerrainAssetContent(*base))
            throw std::runtime_error("Spline source or base terrain changed; rebake required.");
        field->worldSize=r.Value<float>();field->texelSize=r.Value<float>();field->resolution=r.Value<std::uint32_t>();
        if(field->resolution<2||field->resolution>16384||field->worldSize!=base->settings.terrainSize||
            field->texelSize!=field->worldSize/field->resolution)throw std::runtime_error("Invalid spline field mapping.");
        auto terrain=std::make_shared<VansTerrainAsset>(*base);terrain->heights=r.Array<std::uint16_t>(base->heights.size());
        if(terrain->heights.size()!=base->heights.size())throw std::runtime_error("Invalid baked terrain size.");
        field->effectiveTerrain=terrain;
        field->hasVegetationExclusion=std::any_of(source.splines.begin(),source.splines.end(),[](const auto& s){return s.enabled && s.points.size()>=2 && s.excludeVegetation;});
        const auto pixels=std::size_t(VANS_SPLINE_TILE_EXTENT)*VANS_SPLINE_TILE_EXTENT;
        const auto tileCount=r.Count(VANS_SPLINE_MAX_ATLAS_PAGES);std::size_t totalDomains=0;
        const auto axisTiles=(field->resolution+VANS_SPLINE_TILE_SIZE-1)/VANS_SPLINE_TILE_SIZE;
        for(std::uint32_t i=0;i<tileCount;++i)
        {
            auto t=std::make_shared<VansPcgSplineFieldTile>();const auto key=r.Value<std::uint64_t>();
            t->x=std::uint32_t(key);t->z=std::uint32_t(key>>32);t->fingerprint=r.Value<std::uint64_t>();t->terrainShapeFingerprint=r.Value<std::uint64_t>();
            if(t->x>=axisTiles||t->z>=axisTiles||field->tiles.count(key))throw std::runtime_error("Invalid baked tile identity.");
            t->heights=r.Array<glm::vec2>(pixels);t->velocities=r.Array<glm::vec2>(pixels);t->coverage=r.Array<glm::vec4>(pixels);t->vegetationExclusion=r.Array<float>(pixels);
            if(t->heights.size()!=pixels||t->velocities.size()!=pixels||t->coverage.size()!=pixels||t->vegetationExclusion.size()!=pixels)throw std::runtime_error("Invalid baked tile extent.");
            t->minimumWaterHeight=r.Value<float>();t->maximumWaterHeight=r.Value<float>();t->minimumRiverWidth=r.Value<float>();t->maximumHeightConflict=r.Value<float>();t->hasRiver=r.Value<std::uint8_t>()!=0;
            const auto domains=r.Count(VANS_SPLINE_MAX_DOMAINS_PER_TILE);totalDomains+=domains;
            if(totalDomains>VANS_SPLINE_MAX_ATLAS_PAGES)throw std::runtime_error("Too many river coordinate pages.");
            for(std::uint32_t j=0;j<domains;++j)
            {
                VansPcgRiverCoordinateTile d;d.splineId=r.Text();d.cycleSeconds=r.Value<float>();d.flowEnabled=r.Value<std::uint8_t>()!=0;
                d.coordinates=r.Array<glm::vec4>(pixels);d.jacobians=r.Array<glm::vec4>(pixels);
                if(d.coordinates.size()!=pixels||d.jacobians.size()!=pixels||!std::isfinite(d.cycleSeconds)||d.cycleSeconds<=0)
                    throw std::runtime_error("Invalid river coordinate page.");
                t->domains.push_back(std::move(d));
            }
            field->tiles.emplace(key,std::move(t));field->changedTiles.push_back(key);
        }
        const auto roads=r.Count(16384);
        for(std::uint32_t i=0;i<roads;++i)
        {
            auto road=std::make_shared<VansPcgRoadMesh>();road->splineId=r.Text();
            if(!VansAssetGuid::TryParse(r.Text(),road->material))throw std::runtime_error("Invalid baked road material.");
            road->fingerprint=r.Value<std::uint64_t>();road->vertices=r.Array<VansPcgRoadVertex>(524288);road->indices=r.Array<std::uint32_t>(1572864);
            for(const auto index:road->indices)if(index>=road->vertices.size())throw std::runtime_error("Invalid baked road index.");
            if(!field->roads.emplace(road->splineId,road).second)throw std::runtime_error("Duplicate baked road.");
        }
        const auto warnings=r.Count(4096);for(std::uint32_t i=0;i<warnings;++i)field->warnings.push_back(r.Text());
        field->uncoveredBankPoints=r.Array<glm::vec3>(VANS_SPLINE_MAX_ATLAS_PAGES);
        if(r.cursor+sizeof(digest)!=bytes.size())throw std::runtime_error("Unexpected spline cache payload.");
        return field;
    }
    catch(const std::exception& exception){error=exception.what();return {};}
}
}
