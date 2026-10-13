#include "VansNavigationSourceCollector.h"
#include "VansNavigationSource.h"
#include "Detail/VansNavigationHeightfieldInput.h"
#include "../Util/VansFileFingerprint.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cmath>
#include <limits>
#include <new>

namespace Vans
{
namespace
{
template<class T,class Less>
std::vector<const T*> SortedReferences(const std::vector<T>& values,Less less)
{
	std::vector<const T*> references; references.reserve(values.size());
	for(const auto& value:values) references.push_back(&value);
	std::sort(references.begin(),references.end(),[&](const auto* a,const auto* b){return less(*a,*b);});
	return references;
}
class Hash
{
public:
    explicit Hash(std::uint64_t seed=VANS_FNV1A64_OFFSET_BASIS):value_(seed) {}
	template<class T> void Add(const T& value) { value_ = ContinueMemoryFnv1a64(value_, &value, sizeof(value)); }
	void Add(const std::string& value) { Add(static_cast<std::uint64_t>(value.size())); value_ = ContinueMemoryFnv1a64(value_, value.data(), value.size()); }
	void Add(float value) { const float canonical = value == 0 ? 0 : value; value_ = ContinueMemoryFnv1a64(value_, &canonical, sizeof(canonical)); }
	void Add(const glm::vec3& value) { Add(value.x); Add(value.y); Add(value.z); }
	void Add(const glm::mat4& value) { for (int c=0;c<4;++c) for (int r=0;r<4;++r) Add(value[c][r]); }
	void AddSorted(std::vector<std::string> values) { std::sort(values.begin(),values.end()); Add(static_cast<std::uint64_t>(values.size())); for(const auto& value:values) Add(value); }
	std::uint64_t Value() const { return value_; }
private:
	std::uint64_t value_;
};
VansNavigationLinkKey LinkIdentity(const std::string& id)
{
    Hash identity; identity.Add(id); const auto hash=identity.Value();
    return {static_cast<std::uint32_t>(hash)^static_cast<std::uint32_t>(hash>>32)};
}
bool ValidLinkIdentity(const std::string& id)
{
    return !id.empty() && id.size()<=256 && LinkIdentity(id).value &&
        std::none_of(id.begin(),id.end(),[](unsigned char c){return c<32 || c==127;});
}
std::uint64_t LinkHash(const VansNavigationLinkGeometry& link)
{
    Hash hash; hash.Add(link.id); hash.Add(link.key.value); hash.Add(link.start); hash.Add(link.end);
    hash.Add(link.radiusMeters); hash.Add(link.area); hash.Add(link.bidirectional); return hash.Value();
}
// 逐面规范化保留绕序；常量空间聚合不受 native 顶点编号、三角形次序影响。
class TriangleFingerprint
{
public:
    void Add(const std::array<glm::vec3,3>& points)
    {
        std::array<std::array<float,3>,3> canonical{};
        for(int corner=0;corner<3;++corner) for(int axis=0;axis<3;++axis)
            canonical[corner][axis]=points[corner][axis]==0?0:points[corner][axis];
        const auto original=canonical;
        for(int rotation=1;rotation<3;++rotation)
        {
            const std::array<std::array<float,3>,3> candidate{original[rotation],original[(rotation+1)%3],original[(rotation+2)%3]};
            canonical=(std::min)(canonical,candidate);
        }
        Hash triangle;
        for(const auto& point:canonical) for(const auto axis:point) triangle.Add(axis);
        const auto value=triangle.Value();
        ++m_Count; m_Sum+=value; m_Xor^=(value<<23)|(value>>41);
    }
    void AppendTo(Hash& hash) const { hash.Add(m_Count); hash.Add(m_Sum); hash.Add(m_Xor); }
private:
    std::uint64_t m_Count=0,m_Sum=0,m_Xor=0;
};
bool Finite(const glm::vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool ValidTransform(const glm::mat4& matrix)
{
	for(int c=0;c<4;++c) for(int r=0;r<4;++r) if(!std::isfinite(matrix[c][r])) return false;
	const float determinant=glm::determinant(matrix);
	return std::isfinite(determinant) && std::abs(determinant)>1.e-8f &&
		std::abs(matrix[0][3])+std::abs(matrix[1][3])+std::abs(matrix[2][3])<1.e-6f && std::abs(matrix[3][3]-1)<1.e-6f;
}
bool Has(const std::vector<std::string>& values,const std::string& value) { return std::find(values.begin(),values.end(),value)!=values.end(); }
bool ValidNames(const std::vector<std::string>& values,std::size_t limit)
{
	std::set<std::string> seen;
	if(values.size()>limit) return false;
	for(const auto& value:values) if(value.empty() || value.size()>256 || !seen.insert(value).second ||
		std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32 || c==127;})) return false;
	return true;
}
struct Volume { glm::mat4 inverse; glm::vec3 minimum,maximum; };
using Polygon=std::vector<glm::vec3>;
float Distance(const Volume& volume,const glm::vec3& point,int plane)
{
	const auto local=glm::vec3(volume.inverse*glm::vec4(point,1));
	const int axis=plane/2;
	return plane%2 ? volume.maximum[axis]-local[axis] : local[axis]-volume.minimum[axis];
}
// Link 的直线连接也必须完整位于该 Profile 的有效 Surface 并集内。
bool LinkWithinSurfaces(const VansNavigationLinkGeometry& link,const std::string& profileId,
    const std::vector<const VansNavigationSurfaceRecord*>& surfaces)
{
    std::vector<std::pair<float,float>> intervals;
    intervals.reserve(surfaces.size());
    for(const auto* record:surfaces)
    {
        const auto& surface=record->surface;
        if(!record->enabled || !Has(surface.profiles,profileId)) continue;
        const auto inverse=glm::inverse(record->worldTransform);
        const auto start=glm::vec3(inverse*glm::vec4(link.start,1));
        const auto delta=glm::vec3(inverse*glm::vec4(link.end,1))-start;
        const auto minimum=surface.boundsCenterMeters-surface.boundsHalfExtentsMeters;
        const auto maximum=surface.boundsCenterMeters+surface.boundsHalfExtentsMeters;
        float begin=0,end=1;
        for(int axis=0;axis<3 && begin<=end;++axis)
        {
            if(delta[axis]==0)
            {
                if(start[axis]<minimum[axis] || start[axis]>maximum[axis]) end=-1;
                continue;
            }
            const auto a=(minimum[axis]-start[axis])/delta[axis];
            const auto b=(maximum[axis]-start[axis])/delta[axis];
            begin=(std::max)(begin,(std::min)(a,b));
            end=(std::min)(end,(std::max)(a,b));
        }
        if(begin<=end) intervals.emplace_back(begin,end);
    }
    std::sort(intervals.begin(),intervals.end());
    float covered=0;
    for(const auto& interval:intervals)
    {
        if(interval.first>covered) return false;
        covered=(std::max)(covered,interval.second);
        if(covered>=1) return true;
    }
    return false;
}
Polygon Clip(const Polygon& input,const Volume& volume,int plane,bool inside)
{
	Polygon result;
	if(input.empty()) return result;
	auto previous=input.back();
	float before=Distance(volume,previous,plane)*(inside?1.f:-1.f);
	for(const auto& point:input)
	{
		const float after=Distance(volume,point,plane)*(inside?1.f:-1.f);
		if((before>=0)!=(after>=0)) result.push_back(previous+(point-previous)*(before/(before-after)));
		if(after>=0) result.push_back(point);
		previous=point; before=after;
	}
	return result;
}
Polygon Intersect(Polygon polygon,const Volume& volume)
{
	for(int plane=0;plane<6 && !polygon.empty();++plane) polygon=Clip(polygon,volume,plane,true);
	return polygon;
}
std::vector<Polygon> Subtract(const Polygon& polygon,const Volume& volume)
{
	std::vector<Polygon> result;
	auto remainder=polygon;
	for(int plane=0;plane<6 && !remainder.empty();++plane)
	{
		auto outside=Clip(remainder,volume,plane,false);
		if(outside.size()>=3) result.push_back(std::move(outside));
		remainder=Clip(remainder,volume,plane,true);
	}
	return result;
}
std::uint64_t SurfaceHash(const VansNavigationSurfaceRecord& record)
{
	Hash hash; const auto& s=record.surface;
	hash.Add(record.enabled); hash.Add(record.worldTransform); hash.Add(s.mapId); hash.AddSorted(s.profiles);
	hash.Add(s.boundsCenterMeters); hash.Add(s.boundsHalfExtentsMeters); hash.Add(s.collection);
	const auto references=SortedReferences(s.sources,[](const auto& a,const auto& b){return a.componentGuid<b.componentGuid;});
	hash.Add(static_cast<std::uint64_t>(references.size()));
	for(const auto* reference:references) { hash.Add(reference->entityGuid); hash.Add(reference->componentGuid); }
	hash.AddSorted(s.collisionLayers);
	return hash.Value();
}
std::uint64_t ModifierHash(const VansNavigationModifierRecord& record)
{
	Hash hash; const auto& m=record.modifier;
	hash.Add(record.worldTransform); hash.Add(m.mapId); hash.AddSorted(m.profiles); hash.Add(m.shape);
	hash.Add(m.centerMeters); hash.Add(m.halfExtentsMeters); hash.Add(m.minimumHeightMeters); hash.Add(m.maximumHeightMeters);
	hash.Add(m.area); hash.Add(m.exclude); hash.Add(m.priority); hash.Add(static_cast<std::uint64_t>(m.convexVerticesMeters.size()));
	for(const auto& p:m.convexVerticesMeters) { hash.Add(p.x); hash.Add(p.y); }
	return hash.Value();
}
std::uint64_t CarveHash(const VansNavigationObstacleGeometry& shape)
{
    Hash hash; hash.Add(shape.kind); hash.Add(shape.center); hash.Add(shape.halfExtents);
    hash.Add(shape.radius); hash.Add(shape.height); hash.Add(shape.yawRadians);
    return hash.Value();
}
bool ValidModifier(const VansNavigationModifierRecord& record,const VansNavigationSettings& settings,
	const VansNavigationMapSettings& map,std::string& error)
{
	const auto& m=record.modifier;
	if(record.id.empty() || !ValidTransform(record.worldTransform) || m.profiles.empty() || !ValidNames(m.profiles,16) ||
		!Finite(m.centerMeters) || !Finite(m.halfExtentsMeters) || glm::any(glm::lessThanEqual(m.halfExtentsMeters,glm::vec3(0))) ||
		!std::isfinite(m.minimumHeightMeters) || !std::isfinite(m.maximumHeightMeters) || m.minimumHeightMeters>=m.maximumHeightMeters ||
		(!m.exclude && !FindNavigationAreaByName(settings.areas,m.area)))
	{ error="Navigation Modifier has invalid bounds, Area or identity: "+record.id; return false; }
	for(const auto& profile:m.profiles) if(!Has(map.profiles,profile)) { error="Navigation Modifier references an unavailable Profile: "+profile; return false; }
	if(m.shape==VansNavigationModifierShape::Convex)
	{
		if(m.convexVerticesMeters.size()<3 || m.convexVerticesMeters.size()>64) { error="Navigation Modifier convex footprint requires 3..64 vertices"; return false; }
		float sign=0;
		for(std::size_t i=0;i<m.convexVerticesMeters.size();++i)
		{
			const auto a=m.convexVerticesMeters[i],b=m.convexVerticesMeters[(i+1)%m.convexVerticesMeters.size()],c=m.convexVerticesMeters[(i+2)%m.convexVerticesMeters.size()];
			const float turn=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
			if(!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(turn) || std::abs(turn)<1.e-6f || (sign!=0 && turn*sign<=0))
			{ error="Navigation Modifier footprint must be finite, convex and nondegenerate"; return false; }
            for(const auto& point:m.convexVerticesMeters)
            {
                const float side=(b.x-a.x)*(point.y-a.y)-(b.y-a.y)*(point.x-a.x);
                if(!std::isfinite(side) || side*turn < -1.e-6f)
                { error="Navigation Modifier footprint must be a simple convex polygon"; return false; }
            }
			sign=turn;
		}
	}
	else if(m.shape!=VansNavigationModifierShape::Box || !m.convexVerticesMeters.empty())
	{ error="Navigation Modifier shape inputs are mutually exclusive"; return false; }
	return true;
}
}

bool ValidateNavigationModifierRecord(const VansNavigationModifierRecord& record,
	const VansNavigationSettings& settings,const std::string& mapId,std::string& error)
{
	const auto* map=FindNavigationMap(settings,mapId);
	if(!map || record.modifier.mapId!=mapId || !record.enabled)
	{ error="Navigation Modifier does not belong to the enabled Map input"; return false; }
	return ValidModifier(record,settings,*map,error);
}

bool ComputeNavigationSurfaceContributionHash(const VansNavigationSurfaceRecord& record,std::uint64_t& output,std::string& error)
{
	if(!ValidTransform(record.worldTransform) || !Finite(record.surface.boundsCenterMeters) || !Finite(record.surface.boundsHalfExtentsMeters) ||
		glm::any(glm::lessThanEqual(record.surface.boundsHalfExtentsMeters,glm::vec3(0))))
	{ error="Navigation Surface world bounds are invalid"; return false; }
	output=SurfaceHash(record); error.clear(); return true;
}

bool ValidateNavigationHeightfieldInput(const VansNavigationHeightfieldInput& input,std::string& error)
{
    if(!input.data || input.sourceId.empty() || input.sourceId.size()>256 || !ValidTransform(input.worldTransform) ||
        !Finite(input.boundsMinimum) || !Finite(input.boundsMaximum) || glm::any(glm::greaterThan(input.boundsMinimum,input.boundsMaximum)) ||
        input.surfaces.empty() || input.surfaces.size()>1024)
    { error="Navigation heightfield input has invalid identity, transform or Surface coverage"; return false; }
    const auto& grid=*input.data;
    if(grid.columns<2 || grid.rows<2 || grid.columns>4097 || grid.rows>4097 ||
        static_cast<std::uint64_t>(grid.columns)*grid.rows!=grid.samples.size() || !Finite(grid.origin) ||
        !std::isfinite(grid.spacingX) || grid.spacingX<=0 || !std::isfinite(grid.spacingZ) || grid.spacingZ<=0 ||
        !std::isfinite(grid.heightScale) || grid.heightScale<=0)
    { error="Navigation heightfield has invalid dimensions or physical sampling"; return false; }
    std::set<std::string> identities;
    for(const auto& surface:input.surfaces)
    {
        std::uint64_t hash=0;
        if(!surface.enabled || surface.id.empty() || !identities.insert(surface.id).second ||
            !ComputeNavigationSurfaceContributionHash(surface,hash,error)) return false;
    }
    error.clear(); return true;
}

bool CollectNavigationHeightfieldTile(const std::vector<VansNavigationHeightfieldInput>& inputs,
    const glm::vec3& minimum,const glm::vec3& maximum,std::uint64_t maximumBytes,
    VansNavigationGeometry& output,std::string& error)
try
{
    VansNavigationGeometry candidate;
    const Volume tile{glm::mat4(1),minimum,maximum};
    for(const auto& input:inputs)
    {
        if(glm::any(glm::lessThan(input.boundsMaximum,minimum)) || glm::any(glm::greaterThan(input.boundsMinimum,maximum))) continue;
        const auto& grid=*input.data;
        const auto inverse=glm::inverse(input.worldTransform);
        glm::vec3 localMinimum((std::numeric_limits<float>::max)()),localMaximum=-localMinimum;
        for(int corner=0;corner<8;++corner)
        {
            const glm::vec3 point(corner&1?maximum.x:minimum.x,corner&2?maximum.y:minimum.y,corner&4?maximum.z:minimum.z);
            const auto local=glm::vec3(inverse*glm::vec4(point,1));
            localMinimum=glm::min(localMinimum,local); localMaximum=glm::max(localMaximum,local);
        }
        const auto coordinate=[](float point,float origin,float step,std::uint32_t count,bool end)
        {
            const double value=(static_cast<double>(point)-origin)/step;
            return static_cast<std::uint32_t>((std::max)(0.0,(std::min)(static_cast<double>(count-1),end?std::ceil(value):std::floor(value))));
        };
        const auto firstX=coordinate(localMinimum.x,grid.origin.x,grid.spacingX,grid.columns,false);
        const auto lastX=coordinate(localMaximum.x,grid.origin.x,grid.spacingX,grid.columns,true);
        const auto firstZ=coordinate(localMinimum.z,grid.origin.z,grid.spacingZ,grid.rows,false);
        const auto lastZ=coordinate(localMaximum.z,grid.origin.z,grid.spacingZ,grid.rows,true);
        if(static_cast<std::uint64_t>(lastX-firstX)*(lastZ-firstZ)>maximumBytes/1024u)
        { error="Navigation Tile heightfield sampling exceeds build memory budget"; return false; }
        std::vector<Volume> volumes;
        for(const auto& record:input.surfaces)
            volumes.push_back({glm::inverse(record.worldTransform),record.surface.boundsCenterMeters-record.surface.boundsHalfExtentsMeters,
                record.surface.boundsCenterMeters+record.surface.boundsHalfExtentsMeters});
        const auto vertex=[&](std::uint32_t x,std::uint32_t z)
        {
            const auto sample=grid.samples[static_cast<std::size_t>(z)*grid.columns+x];
            return glm::vec3(input.worldTransform*glm::vec4(grid.origin+glm::vec3(x*grid.spacingX,sample*grid.heightScale,z*grid.spacingZ),1));
        };
        for(auto z=firstZ;z<lastZ;++z) for(auto x=firstX;x<lastX;++x)
        {
            const auto a=vertex(x,z),b=vertex(x+1,z),c=vertex(x,z+1),d=vertex(x+1,z+1);
            for(const auto& triangle:std::array<std::array<glm::vec3,3>,2>{{{a,c,b},{b,c,d}}})
            {
                Polygon original(triangle.begin(),triangle.end());
                if(glm::determinant(input.worldTransform)<0) std::swap(original[1],original[2]);
                original=Intersect(std::move(original),tile);
                if(original.size()<3) continue;
                for(std::size_t surface=0;surface<volumes.size();++surface)
                {
                    auto polygon=Intersect(original,volumes[surface]);
                    if(polygon.size()<3) continue;
                    std::vector<Polygon> pieces{std::move(polygon)};
                    for(std::size_t prior=0;prior<surface && !pieces.empty();++prior)
                    {
                        std::vector<Polygon> remaining;
                        for(const auto& piece:pieces) for(auto& part:Subtract(piece,volumes[prior])) remaining.push_back(std::move(part));
                        pieces=std::move(remaining);
                        if(pieces.size()>4096) { error="Navigation heightfield Surface union exceeds capacity"; return false; }
                    }
                    for(const auto& piece:pieces) for(std::size_t corner=1;corner+1<piece.size();++corner)
                    {
                        const std::array<glm::vec3,3> points{piece[0],piece[corner],piece[corner+1]};
                        if(glm::length(glm::cross(points[1]-points[0],points[2]-points[0]))<1.e-8f) continue;
                        if(candidate.TriangleCount()>=maximumBytes/512u)
                        { error="Navigation Tile heightfield clipping exceeds memory budget"; return false; }
                        for(const auto& point:points)
                        {
                            if(!Finite(point)) { error="Navigation heightfield has a non-finite world vertex"; return false; }
                            candidate.indices.push_back(static_cast<int>(candidate.VertexCount()));
                            candidate.vertices.insert(candidate.vertices.end(),{point.x,point.y,point.z});
                        }
                        candidate.areas.push_back(input.area);
                    }
                }
            }
        }
    }
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation Tile heightfield allocation failed"; return false; }

VansNavigationLinkKey DeriveNavigationLinkKey(const std::string& stableIdentity)
{
    return ValidLinkIdentity(stableIdentity)?LinkIdentity(stableIdentity):VansNavigationLinkKey{};
}
bool ValidateNavigationLinkGeometry(const VansNavigationLinkGeometry& link,const VansNavigationAreaSettings& areas,std::string& error)
{
    const auto key=LinkIdentity(link.id).value;
    if(!ValidLinkIdentity(link.id) || link.key.value!=key || !Finite(link.start) || !Finite(link.end) || link.start==link.end ||
        !std::isfinite(link.radiusMeters) || link.radiusMeters<=0 || !FindNavigationAreaById(areas,link.area))
    { error="Navigation Link has invalid identity, endpoints, radius or Area: "+link.id; return false; }
    error.clear(); return true;
}
bool ValidateNavigationLinkRecord(const VansNavigationLinkRecord& record,const VansNavigationSettings& settings,std::string& error)
{
    const auto& link=record.link; const auto* map=FindNavigationMap(settings,link.mapId);
    if(!ValidLinkIdentity(record.id) || !map || !ValidNames(link.profiles,32) || link.profiles.empty() || !FindNavigationAreaByName(settings.areas,link.area) ||
        !ValidTransform(record.worldTransform) || !Finite(link.startMeters) || !Finite(link.endMeters) || link.startMeters==link.endMeters ||
        !std::isfinite(link.radiusMeters) || link.radiusMeters<=0)
    { error="Navigation Link has invalid Map, Profile, transform, endpoints, radius or Area: "+record.id; return false; }
    for(const auto& profile:link.profiles)
        if(!FindNavigationProfile(settings,profile) || !Has(map->profiles,profile))
        { error="Navigation Link Profile is unavailable in its Map: "+profile; return false; }
    error.clear(); return true;
}
bool FreezeNavigationLinkGeometry(const VansNavigationLinkRecord& record,const VansNavigationSettings& settings,
    VansNavigationLinkGeometry& output,std::string& error)
{
    if(!ValidateNavigationLinkRecord(record,settings,error)) return false;
    VansNavigationLinkGeometry candidate; candidate.id=record.id;
    candidate.key=LinkIdentity(record.id);
    candidate.start=glm::vec3(record.worldTransform*glm::vec4(record.link.startMeters,1));
    candidate.end=glm::vec3(record.worldTransform*glm::vec4(record.link.endMeters,1));
    candidate.radiusMeters=record.link.radiusMeters; candidate.bidirectional=record.link.bidirectional;
    candidate.area=FindNavigationAreaByName(settings.areas,record.link.area)->id;
    if(!ValidateNavigationLinkGeometry(candidate,settings.areas,error)) return false;
    output=std::move(candidate); error.clear(); return true;
}
bool ComputeNavigationBuildBaseHash(const VansNavigationProfileBuildInput& input,std::uint64_t& output,std::string& error)
try
{
	std::uint64_t geometryHash=0;
	if(!input.geometry || !ComputeNavigationGeometryHash(*input.geometry,geometryHash,error)) return false;
    const auto heightfields=SortedReferences(input.heightfields,[](const auto& a,const auto& b){return a.sourceId<b.sourceId;});
    Hash gridHash; gridHash.Add(static_cast<std::uint64_t>(heightfields.size()));
    std::string previousGrid;
    for(const auto* fieldPointer:heightfields)
    {
        const auto& field=*fieldPointer;
        if(field.sourceId==previousGrid || !ValidateNavigationHeightfieldInput(field,error)) return false;
        const auto& grid=*field.data;
        gridHash.Add(field.sourceId); gridHash.Add(field.worldTransform); gridHash.Add(field.area);
        gridHash.Add(grid.columns); gridHash.Add(grid.rows); gridHash.Add(grid.origin);
        gridHash.Add(grid.spacingX); gridHash.Add(grid.spacingZ); gridHash.Add(grid.heightScale);
        gridHash.Add(ComputeMemoryFnv1a64(grid.samples.data(),grid.samples.size()*sizeof(std::int16_t)));
        const auto surfaces=SortedReferences(field.surfaces,[](const auto& a,const auto& b){return a.id<b.id;});
        for(const auto* surface:surfaces) { gridHash.Add(surface->id); gridHash.Add(SurfaceHash(*surface)); }
        previousGrid=field.sourceId;
    }
	const auto contributions=SortedReferences(input.contributions,[](const auto& a,const auto& b){return a.id<b.id;});
	if(contributions.size()>262144) { error="Navigation contribution count exceeds capacity"; return false; }
	Hash hash; hash.Add(geometryHash); hash.Add(gridHash.Value()); hash.Add(input.profileId); hash.Add(static_cast<std::uint64_t>(contributions.size()));
	std::string previous;
	for(const auto* itemPointer:contributions)
	{
		const auto& item=*itemPointer;
		if(item.id.empty() || item.id.size()>512 || item.id==previous || item.contentHash==0)
		{ error="Navigation contribution has invalid or duplicate identity"; return false; }
		hash.Add(item.id); hash.Add(item.contentHash); previous=item.id;
	}
	const auto modifiers=SortedReferences(input.modifiers,[](const auto& a,const auto& b){return a.id<b.id;});
	hash.Add(static_cast<std::uint64_t>(modifiers.size()));
	for(const auto* modifier:modifiers) { hash.Add(modifier->id); hash.Add(ModifierHash(*modifier)); }
    if(!input.links.empty())
    {
        if(input.links.size()>kMaximumNavigationLinkCapabilities) { error="Navigation Link count exceeds capacity"; return false; }
        const auto links=SortedReferences(input.links,[](const auto& a,const auto& b){return a.id<b.id;});
        hash.Add(static_cast<std::uint64_t>(links.size()));
        std::string previousLink; std::set<std::uint32_t> keys;
        for(const auto* link:links)
        {
            if(link->id==previousLink || !keys.insert(link->key.value).second || !link->key.value ||
                !Finite(link->start) || !Finite(link->end) || link->start==link->end || !std::isfinite(link->radiusMeters) || link->radiusMeters<=0)
            { error="Navigation Link fingerprint contains invalid or colliding connections"; return false; }
            hash.Add(link->id); hash.Add(LinkHash(*link)); previousLink=link->id;
        }
    }
    output=hash.Value(); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation base fingerprint allocation failed"; return false; }

bool ComputeNavigationBuildInputHash(const VansNavigationProfileBuildInput& input,std::uint64_t& output,std::string& error)
try
{
    std::uint64_t base=0;
    if(!ComputeNavigationBuildBaseHash(input,base,error)) return false;
    Hash hash(base);
	if(!input.carveObstacles.empty())
	{
		if(input.carveObstacles.size()>kMaximumNavigationObstacles)
		{ error="Navigation Carve obstacle count exceeds capacity"; return false; }
		const auto shapes=SortedReferences(input.carveObstacles,[](const auto& a,const auto& b){return a.id<b.id;});
		std::string previousShape;
		for(const auto* shape:shapes)
		{
			if(!ValidateNavigationObstacleGeometry(*shape,error)) return false;
			if(shape->id==previousShape) { error="Navigation Carve identity is duplicated"; return false; }
			hash.Add(shape->id); hash.Add(CarveHash(*shape)); previousShape=shape->id;
		}
	}
	output=hash.Value(); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation input fingerprint allocation failed"; return false; }

bool VansNavigationSourceCollector::Collect(const VansNavigationSourceSnapshot& snapshot,const VansNavigationSettings& settings,
	const std::string& mapId,VansNavigationBuildInput& output,std::string& error)
try
{
	if(!ValidateNavigationSettings(settings,error)) return false;
	const auto* map=FindNavigationMap(settings,mapId);
	if(!map) { error="Navigation collection references an unavailable Map: "+mapId; return false; }
	if(snapshot.sources.size()>65536 || snapshot.exclusions.size()>65536 || snapshot.surfaces.size()>1024 || snapshot.modifiers.size()>4096 ||
        snapshot.obstacles.size()>kMaximumNavigationObstacles || snapshot.links.size()>kMaximumNavigationLinkCapabilities || !ValidNames(snapshot.collisionLayers,32))
	{ error="Navigation snapshot exceeds source/Surface/Modifier/layer capacity"; return false; }
    const auto memoryBudget=static_cast<std::uint64_t>((std::min)(map->maximumBuildBytes,settings.maximumConcurrentBuildBytes));
    std::uint64_t retainedBytes=sizeof(snapshot)+snapshot.sources.size()*sizeof(VansNavigationSourceRecord)+
        snapshot.surfaces.size()*sizeof(VansNavigationSurfaceRecord)+snapshot.modifiers.size()*sizeof(VansNavigationModifierRecord)+
        snapshot.exclusions.size()*sizeof(VansNavigationSourceSnapshot::Exclusion)+snapshot.obstacles.size()*sizeof(VansNavigationObstacleRecord)+
        snapshot.links.size()*sizeof(VansNavigationLinkRecord);
    std::set<std::string> obstacleIds;
    for(const auto& obstacle:snapshot.obstacles)
    {
        if(!ValidateNavigationObstacleRecord(obstacle,settings,error)) return false;
        if(!obstacleIds.insert(obstacle.shape.id).second)
        { error="Navigation Carve has duplicate identity"; return false; }
        retainedBytes+=obstacle.shape.id.capacity()+obstacle.mapId.capacity();
        for(const auto& profile:obstacle.profiles) retainedBytes+=sizeof(profile)+profile.capacity();
    }
    std::set<std::string> linkIds;
    std::map<std::string,std::set<std::uint32_t>> linkKeys;
    for(const auto& record:snapshot.links)
    {
        VansNavigationLinkGeometry link;
        if(!FreezeNavigationLinkGeometry(record,settings,link,error)) return false;
        if(!linkIds.insert(record.id).second || !linkKeys[record.link.mapId].insert(link.key.value).second)
        { error="Navigation Link has duplicate or colliding stable identity: "+record.id; return false; }
        retainedBytes+=record.id.capacity()+record.link.mapId.capacity()+record.link.area.capacity();
        for(const auto& profile:record.link.profiles) retainedBytes+=sizeof(profile)+profile.capacity();
    }
    std::set<std::string> exclusionIds;
    for(const auto& excluded:snapshot.exclusions)
    {
        if(excluded.id.empty() || excluded.id.size()>512 || excluded.reason.empty() || excluded.reason.size()>256 ||
            excluded.kind>VansNavigationGeometryKind::CustomMesh ||
            !excluded.instanceCount || !exclusionIds.insert(excluded.id).second)
        { error="Navigation producer exclusion has an invalid identity/reason/count"; return false; }
        retainedBytes+=excluded.id.size()+excluded.reason.size();
    }
    std::set<const VansTriangleMeshData*> meshes;
    std::set<const VansHeightfieldData*> heightfields;
    for(const auto& source:snapshot.sources)
    {
        retainedBytes+=source.id.size()+source.ownerId.size()+source.collisionLayer.size()+source.area.size()+source.geometryError.size();
        for(const auto& ancestor:source.ancestors) retainedBytes+=sizeof(ancestor)+ancestor.size();
        if(source.geometry && meshes.insert(source.geometry.get()).second)
            retainedBytes+=source.geometry->positions.size()*sizeof(float)+source.geometry->indices.size()*sizeof(std::uint32_t);
        if(source.heightfield && heightfields.insert(source.heightfield.get()).second)
            retainedBytes+=source.heightfield->samples.size()*sizeof(std::int16_t)+sizeof(VansHeightfieldData);
        if(source.hasExplicitSource && source.sourceEnabled &&
            (source.source.mode!=VansNavigationSourceMode::Include && source.source.mode!=VansNavigationSourceMode::Exclude && source.source.mode!=VansNavigationSourceMode::Area))
        { error="NavigationSource has an unknown mode"; return false; }
        if(source.hasExplicitSource && source.sourceEnabled && source.source.mode==VansNavigationSourceMode::Area && source.source.area.empty())
        { error="NavigationSource Area mode requires a named Area"; return false; }
    }
    if(retainedBytes>=memoryBudget) { error="Navigation frozen source snapshot exceeds collection memory budget"; return false; }
	std::set<std::string> sourceIds,surfaceIds,modifierIds;
	std::map<std::string,std::string> explicitOwners;
	for(const auto& source:snapshot.sources)
	{
		if(source.id.empty() || source.id.size()>256 || !sourceIds.insert(source.id).second)
		{ error="Navigation snapshot source identity is empty or duplicated"; return false; }
		if(source.hasExplicitSource) explicitOwners.emplace(source.id,source.ownerId);
	}
    std::vector<const VansNavigationSourceRecord*> sources;
    for(const auto& source:snapshot.sources) sources.push_back(&source);
    std::sort(sources.begin(),sources.end(),[](const auto* a,const auto* b){return a->id<b->id;});
	std::vector<const VansNavigationSurfaceRecord*> surfaces;
	for(const auto& surface:snapshot.surfaces)
	{
		if(surface.id.empty() || surface.id.size()>256 || !surfaceIds.insert(surface.id).second)
		{ error="Navigation Surface identity is empty or duplicated"; return false; }
		if(surface.surface.mapId!=mapId) continue;
		const auto& s=surface.surface;
		if(!ValidTransform(surface.worldTransform) || !Finite(s.boundsCenterMeters) || !Finite(s.boundsHalfExtentsMeters) ||
			glm::any(glm::lessThanEqual(s.boundsHalfExtentsMeters,glm::vec3(0))) || s.profiles.empty() || !ValidNames(s.profiles,16) ||
			s.sources.size()>65536 || !ValidNames(s.collisionLayers,32) ||
			(s.collection!=VansNavigationCollection::All && s.collection!=VansNavigationCollection::Hierarchy && s.collection!=VansNavigationCollection::ExplicitSources) ||
            (s.collection==VansNavigationCollection::ExplicitSources ? s.sources.empty() : !s.sources.empty()))
		{ error="Navigation Surface has invalid bounds, profiles or collection: "+surface.id; return false; }
		for(const auto& profile:s.profiles) if(!Has(map->profiles,profile)) { error="Navigation Surface references an unavailable Profile: "+profile; return false; }
		for(const auto& layer:s.collisionLayers) if(!Has(snapshot.collisionLayers,layer)) { error="Navigation Surface references an unknown collision layer: "+layer; return false; }
		std::set<std::string> references;
		for(const auto& reference:s.sources)
		{
			const auto owner=explicitOwners.find(reference.componentGuid);
			if(owner==explicitOwners.end() || owner->second!=reference.entityGuid || !references.insert(reference.componentGuid).second)
			{ error="Navigation Surface references an unavailable, duplicated or incorrectly owned NavigationSource: "+reference.componentGuid; return false; }
		}
		surfaces.push_back(&surface);
	}
	if(surfaces.empty()) { error="Navigation Map has no authored Surface: "+mapId; return false; }
	std::sort(surfaces.begin(),surfaces.end(),[](const auto* a,const auto* b){return a->id<b->id;});
	VansNavigationBuildInput candidate; candidate.mapId=mapId;
	for(const auto& profileId:map->profiles)
	{
		VansNavigationProfileBuildInput profile; profile.profileId=profileId;
		auto geometry=std::make_shared<VansNavigationGeometry>();
		using Point=std::array<float,3>;
		std::map<Point,int> vertices;
		std::set<std::array<float,10>> triangles;
		std::map<std::string,std::vector<Volume>> covered;
		const auto budget=static_cast<std::size_t>(memoryBudget-retainedBytes);
		for(const auto* record:surfaces)
		{
			const auto& s=record->surface;
			if(!Has(s.profiles,profileId)) continue;
			profile.contributions.push_back({"surface/"+record->id,SurfaceHash(*record)});
			if(!record->enabled) continue;
			const Volume volume{glm::inverse(record->worldTransform),s.boundsCenterMeters-s.boundsHalfExtentsMeters,s.boundsCenterMeters+s.boundsHalfExtentsMeters};
			for(const auto* sourcePointer:sources)
			{
                const auto& source=*sourcePointer;
				if(!source.enabled || source.isTrigger ||
					(source.hasExplicitSource && source.sourceEnabled && source.source.mode==VansNavigationSourceMode::Exclude) ||
					(!s.collisionLayers.empty() && !Has(s.collisionLayers,source.collisionLayer)) ||
					(s.collection==VansNavigationCollection::Hierarchy && source.ownerId!=record->ownerId && !Has(source.ancestors,record->ownerId)) ||
					(s.collection==VansNavigationCollection::ExplicitSources && !ReferencesNavigationSource(s,source.ownerId,source.id))) continue;
				if(s.collection==VansNavigationCollection::ExplicitSources && !source.sourceEnabled) continue;
				if(!source.isStatic)
				{
					if(source.hasExplicitSource && source.sourceEnabled && map->updateMode!=VansNavigationMapUpdateMode::Runtime)
					{ error="Explicit dynamic NavigationSource requires a Runtime geometry strategy: "+source.id; return false; }
                    if(!source.hasExplicitSource || !source.sourceEnabled) continue;
				}
                const std::string areaName=source.hasExplicitSource && source.sourceEnabled && !source.source.area.empty()?source.source.area:(source.area.empty()?settings.areas.defaultArea:source.area);
                const auto* area=FindNavigationAreaByName(settings.areas,areaName);
                if(!area) { error="Navigation source references unknown area '"+areaName+"': "+source.id; return false; }
                if(!source.geometryError.empty()) { error="Navigation source '"+source.id+"': "+source.geometryError; return false; }
                if(source.heightfield)
                {
                    if(source.geometry) { error="Navigation source cannot contain both a mesh and heightfield"; return false; }
                    glm::vec3 surfaceMinimum((std::numeric_limits<float>::max)()),surfaceMaximum=-surfaceMinimum;
                    for(int corner=0;corner<8;++corner)
                    {
                        const auto local=s.boundsCenterMeters+s.boundsHalfExtentsMeters*glm::vec3(corner&1?1.f:-1.f,corner&2?1.f:-1.f,corner&4?1.f:-1.f);
                        const auto p=glm::vec3(record->worldTransform*glm::vec4(local,1));
                        surfaceMinimum=glm::min(surfaceMinimum,p); surfaceMaximum=glm::max(surfaceMaximum,p);
                    }
                    if(glm::any(glm::lessThan(source.boundsMaximum,surfaceMinimum)) || glm::any(glm::greaterThan(source.boundsMinimum,surfaceMaximum))) continue;
                    auto existing=std::find_if(profile.heightfields.begin(),profile.heightfields.end(),[&](const auto& field){return field.sourceId==source.id;});
                    if(existing==profile.heightfields.end())
                    {
                        VansNavigationHeightfieldInput field;
                        field.sourceId=source.id; field.data=source.heightfield; field.worldTransform=source.worldTransform; field.area=area->id;
                        field.boundsMinimum=glm::max(source.boundsMinimum,surfaceMinimum); field.boundsMaximum=glm::min(source.boundsMaximum,surfaceMaximum);
                        field.surfaces.push_back(*record);
                        if(!ValidateNavigationHeightfieldInput(field,error)) return false;
                        profile.heightfields.push_back(std::move(field));
                    }
                    else
                    {
                        existing->boundsMinimum=glm::min(existing->boundsMinimum,glm::max(source.boundsMinimum,surfaceMinimum));
                        existing->boundsMaximum=glm::max(existing->boundsMaximum,glm::min(source.boundsMaximum,surfaceMaximum));
                        existing->surfaces.push_back(*record);
                    }
                    Hash hash; hash.Add(source.id); hash.Add(area->id); hash.Add(source.kind);
                    const auto& grid=*source.heightfield;
                    hash.Add(source.worldTransform); hash.Add(grid.columns); hash.Add(grid.rows); hash.Add(grid.origin);
                    hash.Add(grid.spacingX); hash.Add(grid.spacingZ); hash.Add(grid.heightScale);
                    hash.Add(ComputeMemoryFnv1a64(grid.samples.data(),grid.samples.size()*sizeof(std::int16_t)));
                    profile.contributions.push_back({"source/"+record->id+"/"+source.id,hash.Value()});
                    if(map->updateMode==VansNavigationMapUpdateMode::Runtime)
                    {
                        Hash coverageHash; coverageHash.Add(hash.Value()); coverageHash.Add(SurfaceHash(*record));
                        profile.footprints.push_back({profile.contributions.back().id,coverageHash.Value(),
                            glm::max(source.boundsMinimum,surfaceMinimum),glm::min(source.boundsMaximum,surfaceMaximum)});
                    }
                    continue;
                }
				if(!source.geometry || source.geometry->Empty() || !ValidTransform(source.worldTransform))
				{ error="Included Navigation source has no valid CPU geometry: "+source.id; return false; }
				const auto& mesh=*source.geometry;
				if(mesh.positions.size()%3 || mesh.indices.size()%3 || !Finite(source.boundsMinimum) || !Finite(source.boundsMaximum))
				{ error="Navigation source has invalid CPU triangle data: "+source.id; return false; }
				bool contributed=false;
                glm::vec3 contributionMinimum((std::numeric_limits<float>::max)()),contributionMaximum=-contributionMinimum;
                TriangleFingerprint sourceTriangles;
				for(std::size_t triangle=0;triangle<mesh.TriangleCount();++triangle)
				{
					Polygon polygon;
					for(std::size_t corner=0;corner<3;++corner)
					{
						const auto index=mesh.indices[triangle*3+corner];
						if(index>=mesh.VertexCount()) { error="Navigation source has an invalid vertex index: "+source.id; return false; }
						const auto p=glm::vec3(source.worldTransform*glm::vec4(mesh.positions[index*3],mesh.positions[index*3+1],mesh.positions[index*3+2],1));
						if(!Finite(p)) { error="Navigation source has a non-finite world vertex: "+source.id; return false; }
						polygon.push_back(p);
					}
					if(glm::determinant(source.worldTransform)<0) std::swap(polygon[1],polygon[2]);
					if(glm::dot(glm::cross(polygon[1]-polygon[0],polygon[2]-polygon[0]),glm::cross(polygon[1]-polygon[0],polygon[2]-polygon[0]))==0)
					{ ++profile.skippedZeroAreaTriangles; continue; }
					polygon=Intersect(std::move(polygon),volume);
					if(polygon.size()<3) continue;
					contributed=true;
                    for(std::size_t corner=1;corner+1<polygon.size();++corner)
                    {
                        const std::array<glm::vec3,3> points{polygon[0],polygon[corner],polygon[corner+1]};
                        if(glm::length(glm::cross(points[1]-points[0],points[2]-points[0]))>=1.e-8f)
                        {
                            sourceTriangles.Add(points);
                            if(map->updateMode==VansNavigationMapUpdateMode::Runtime)
                                for(const auto& point:points)
                                { contributionMinimum=glm::min(contributionMinimum,point); contributionMaximum=glm::max(contributionMaximum,point); }
                        }
                    }
					std::vector<Polygon> pieces{std::move(polygon)};
					for(const auto& prior:covered[source.id])
					{
						std::vector<Polygon> remaining;
						for(const auto& piece:pieces) { auto outside=Subtract(piece,prior); for(auto& part:outside) remaining.push_back(std::move(part)); }
						pieces=std::move(remaining);
						if(pieces.size()>4096) { error="Navigation overlapping Surface clipping exceeds capacity"; return false; }
					}
					for(const auto& piece:pieces) for(std::size_t corner=1;corner+1<piece.size();++corner)
					{
						const std::array<glm::vec3,3> points{piece[0],piece[corner],piece[corner+1]};
						if(glm::length(glm::cross(points[1]-points[0],points[2]-points[0]))<1.e-8f) continue;
						std::array<float,10> key{};
                        int first=0;
                        for(int k=1;k<3;++k)
                            if(std::array<float,3>{points[k].x,points[k].y,points[k].z}<std::array<float,3>{points[first].x,points[first].y,points[first].z}) first=k;
                        for(int k=0;k<3;++k) for(int a=0;a<3;++a) key[k*3+a]=points[(first+k)%3][a]==0?0:points[(first+k)%3][a];
						key[9]=area->id;
						if(!triangles.insert(key).second) continue;
						if(triangles.size()>budget/512) { error="Navigation collected geometry exceeds build memory budget"; return false; }
						for(const auto& p:points)
						{
							const Point point{p.x==0?0:p.x,p.y==0?0:p.y,p.z==0?0:p.z};
							const auto [found,inserted]=vertices.emplace(point,static_cast<int>(geometry->VertexCount()));
							if(inserted) geometry->vertices.insert(geometry->vertices.end(),point.begin(),point.end());
							geometry->indices.push_back(found->second);
						}
						geometry->areas.push_back(area->id);
					}
				}
				if(contributed)
				{
					covered[source.id].push_back(volume);
					Hash hash; hash.Add(source.id); hash.Add(area->id); hash.Add(source.kind);
                    sourceTriangles.AppendTo(hash);
					profile.contributions.push_back({"source/"+record->id+"/"+source.id,hash.Value()});
                    if(map->updateMode==VansNavigationMapUpdateMode::Runtime &&
                        glm::all(glm::lessThanEqual(contributionMinimum,contributionMaximum)))
                        profile.footprints.push_back({profile.contributions.back().id,hash.Value(),contributionMinimum,contributionMaximum});
				}
			}
		}
		if(geometry->Empty() && profile.heightfields.empty() && map->updateMode==VansNavigationMapUpdateMode::Baked)
        { error="Navigation Profile has no enabled Surface geometry: "+profileId; return false; }
		profile.geometry=std::move(geometry);
        glm::vec3 effectiveMinimum((std::numeric_limits<float>::max)()),effectiveMaximum=-effectiveMinimum;
        for(const auto& footprint:profile.footprints)
        { effectiveMinimum=glm::min(effectiveMinimum,footprint.boundsMinimum); effectiveMaximum=glm::max(effectiveMaximum,footprint.boundsMaximum); }
		for(const auto& modifier:snapshot.modifiers)
		{
			if(!modifier.enabled || modifier.modifier.mapId!=mapId || !Has(modifier.modifier.profiles,profileId)) continue;
			if(!modifierIds.insert(profileId+"/"+modifier.id).second || !ValidModifier(modifier,settings,*map,error))
			{ if(error.empty()) error="Navigation Modifier identity is duplicated"; return false; }
			profile.modifiers.push_back(modifier);
            if(map->updateMode==VansNavigationMapUpdateMode::Runtime)
            {
                const auto& m=modifier.modifier;
                glm::vec3 minimum((std::numeric_limits<float>::max)()),maximum=-minimum;
                const auto addPoint=[&](const glm::vec3& local)
                {
                    const auto point=glm::vec3(modifier.worldTransform*glm::vec4(local,1));
                    minimum=glm::min(minimum,point); maximum=glm::max(maximum,point);
                };
                if(m.shape==VansNavigationModifierShape::Box)
                    for(int corner=0;corner<8;++corner)
                        addPoint(m.centerMeters+m.halfExtentsMeters*glm::vec3(corner&1?1.f:-1.f,corner&2?1.f:-1.f,corner&4?1.f:-1.f));
                else for(const auto& vertex:m.convexVerticesMeters)
                {
                    addPoint({vertex.x,m.minimumHeightMeters,vertex.y});
                    addPoint({vertex.x,m.maximumHeightMeters,vertex.y});
                }
                minimum=glm::max(minimum,effectiveMinimum); maximum=glm::min(maximum,effectiveMaximum);
                if(glm::all(glm::lessThanEqual(minimum,maximum)))
                    profile.footprints.push_back({"modifier/"+modifier.id,ModifierHash(modifier),minimum,maximum});
            }
		}
        for(const auto& obstacle:snapshot.obstacles)
        {
            if(!obstacle.enabled || !NavigationObstacleCarves(obstacle.mode) || obstacle.mapId!=mapId || !Has(obstacle.profiles,profileId)) continue;
            profile.carveObstacles.push_back(obstacle.shape);
            glm::vec3 minimum,maximum; NavigationObstacleGeometryBounds(obstacle.shape,minimum,maximum);
            const auto* agent=FindNavigationProfile(settings,profileId);
            minimum-=glm::vec3(agent->radiusMeters,agent->heightMeters,agent->radiusMeters);
            maximum+=glm::vec3(agent->radiusMeters,0,agent->radiusMeters);
            minimum=glm::max(minimum,effectiveMinimum); maximum=glm::min(maximum,effectiveMaximum);
            if(glm::all(glm::lessThanEqual(minimum,maximum)))
                profile.footprints.push_back({"obstacle/"+obstacle.shape.id,CarveHash(obstacle.shape),minimum,maximum});
        }
        for(const auto& record:snapshot.links)
        {
            if(!record.enabled || record.link.mapId!=mapId || !Has(record.link.profiles,profileId)) continue;
            VansNavigationLinkGeometry link;
            if(!FreezeNavigationLinkGeometry(record,settings,link,error)) return false;
            if(!LinkWithinSurfaces(link,profileId,surfaces))
            { error="Navigation Link leaves the enabled Surface boundary: "+link.id; return false; }
            const auto hash=LinkHash(link); profile.contributions.push_back({"link/"+link.id,hash});
            if(map->updateMode==VansNavigationMapUpdateMode::Runtime)
            {
                const auto* agent=FindNavigationProfile(settings,profileId);
                const glm::vec3 margin(link.radiusMeters,agent->heightMeters+agent->maxClimbMeters,link.radiusMeters);
                profile.footprints.push_back({"link/"+link.id+"/entry",hash,link.start-margin,link.start+margin});
                profile.footprints.push_back({"link/"+link.id+"/exit",hash,link.end-margin,link.end+margin});
            }
            profile.links.push_back(std::move(link));
        }
        std::sort(profile.links.begin(),profile.links.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        std::sort(profile.carveObstacles.begin(),profile.carveObstacles.end(),[](const auto& a,const auto& b){return a.id<b.id;});
		std::sort(profile.modifiers.begin(),profile.modifiers.end(),[](const auto& a,const auto& b){return a.modifier.priority==b.modifier.priority?a.id<b.id:a.modifier.priority<b.modifier.priority;});
        std::sort(profile.footprints.begin(),profile.footprints.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        retainedBytes+=profile.geometry->vertices.size()*sizeof(float)+profile.geometry->indices.size()*sizeof(int)+profile.geometry->areas.size();
        for(const auto& contribution:profile.contributions) retainedBytes+=sizeof(contribution)+contribution.id.size();
        for(const auto& footprint:profile.footprints) retainedBytes+=sizeof(footprint)+footprint.id.capacity()+1;
        for(const auto& shape:profile.carveObstacles) retainedBytes+=sizeof(shape)+shape.id.capacity()+1;
        for(const auto& link:profile.links) retainedBytes+=sizeof(link)+link.id.capacity()+1;
        for(const auto& field:profile.heightfields) retainedBytes+=sizeof(field)+field.sourceId.size()+field.surfaces.size()*sizeof(VansNavigationSurfaceRecord);
        if(retainedBytes>=memoryBudget) { error="Navigation multi-Profile collection exceeds aggregate memory budget"; return false; }
		candidate.profiles.push_back(std::move(profile));
	}
	output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation source collection allocation failed"; return false; }
}
