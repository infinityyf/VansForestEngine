#include "VansNavigationBuilder.h"
#include "Detail/VansNavigationTileData.h"
#include "Detail/VansNavigationHeightfieldInput.h"
#include "Detail/VansNavigationBuildInputMemory.h"
#include "Detail/VansNavigationTileCacheBuild.h"
#include "Detail/VansNavigationTileCacheLayerCodec.h"
#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <array>
#include <set>
#include <string_view>
#include "../Util/VansFileFingerprint.h"

namespace Vans
{
namespace
{
constexpr unsigned short kWalkableFlag = 0x1u;
bool CheckBuildCancellation(const VansNavigationBuildControl* control, std::string& error)
{
    if (!control || !control->IsCancelled()) return true;
    error = "Navigation build was cancelled";
    return false;
}
bool IsFiniteGeometry(const VansNavigationGeometry& geometry)
{
	if (geometry.vertices.size() % 3u != 0u || geometry.indices.size() % 3u != 0u)
		return false;
	if (geometry.areas.size() != geometry.TriangleCount()) return false;
	if (geometry.VertexCount() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
		geometry.TriangleCount() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
	for (float value : geometry.vertices)
		if (!std::isfinite(value)) return false;
	const int vertexCount = static_cast<int>(geometry.VertexCount());
	for (int index : geometry.indices)
		if (index < 0 || index >= vertexCount) return false;
	return true;
}


struct TriangleBounds
{
    float minimumX, maximumX, minimumZ, maximumZ;
};
bool BuildTile(const VansNavigationGeometry& geometry, const VansNavigationGeometry& heightfieldGeometry, const VansNavigationBakeSettings& bakeSettings, const VansNavigationAreaSettings& areas,
    const std::vector<std::size_t>& triangleIds, const std::vector<VansNavigationModifierRecord>& modifiers,
    const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,const rcConfig& config,
    int x, int z,std::uint64_t availableBytes,std::vector<VansNavigationTilePayload>& output,std::string& error,
    const VansNavigationBuildControl* control,VansNavigationBuildContext* buildContext,VansNavigationTileLayerData* layerData)
{
    if (!CheckBuildCancellation(control,error)) return false;
    rcContext context(true);
	std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>
		heightfield(rcAllocHeightfield(), &rcFreeHeightField);
	std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>
		compact(nullptr, &rcFreeCompactHeightfield);
	std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>
		contours(nullptr, &rcFreeContourSet);
	std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>
		polyMesh(nullptr, &rcFreePolyMesh);
	std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>
		detailMesh(nullptr, &rcFreePolyMeshDetail);
	if (!heightfield || !rcCreateHeightfield(&context, *heightfield,
		config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
	{
		error = "Recast could not create the heightfield";
		return false;
	}

	const int triangleCount = static_cast<int>(triangleIds.size());
	std::vector<int> triangleIndices;
	triangleIndices.reserve(triangleIds.size() * 3u);
	std::vector<unsigned char> triangleAreas;
	triangleAreas.reserve(triangleIds.size());
	for (std::size_t triangle : triangleIds)
	{
		triangleAreas.push_back(static_cast<unsigned char>(geometry.areas[triangle] + 1u));
		for (std::size_t corner = 0; corner < 3u; ++corner)
			triangleIndices.push_back(geometry.indices[triangle * 3u + corner]);
	}
    if(triangleCount>0)
    {
	rcClearUnwalkableTriangles(&context, config.walkableSlopeAngle,
		geometry.vertices.data(), static_cast<int>(geometry.VertexCount()),
		triangleIndices.data(), triangleCount, triangleAreas.data());
	if (!rcRasterizeTriangles(&context, geometry.vertices.data(),
		static_cast<int>(geometry.VertexCount()), triangleIndices.data(),
		triangleAreas.data(), triangleCount, *heightfield, config.walkableClimb))
	{
		error = "Recast could not rasterize navigation geometry";
		return false;
	}
    }
    if(!heightfieldGeometry.Empty())
    {
        std::vector<unsigned char> heightfieldAreas;
        heightfieldAreas.reserve(heightfieldGeometry.areas.size());
        for(const auto area:heightfieldGeometry.areas) heightfieldAreas.push_back(static_cast<unsigned char>(area+1));
        rcClearUnwalkableTriangles(&context,config.walkableSlopeAngle,heightfieldGeometry.vertices.data(),
            static_cast<int>(heightfieldGeometry.VertexCount()),heightfieldGeometry.indices.data(),
            static_cast<int>(heightfieldGeometry.TriangleCount()),heightfieldAreas.data());
        if(!rcRasterizeTriangles(&context,heightfieldGeometry.vertices.data(),static_cast<int>(heightfieldGeometry.VertexCount()),
            heightfieldGeometry.indices.data(),heightfieldAreas.data(),static_cast<int>(heightfieldGeometry.TriangleCount()),
            *heightfield,config.walkableClimb))
        { error="Recast could not rasterize frozen heightfield samples"; return false; }
    }
	if (!CheckBuildCancellation(control,error)) return false;
	rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
	rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
	rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

	if (!CheckBuildCancellation(control,error)) return false;
	compact.reset(rcAllocCompactHeightfield());
	if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight,
		config.walkableClimb, *heightfield, *compact))
	{
		error = "Recast could not build the compact heightfield";
		return false;
	}
	heightfield.reset();
	if (compact->spanCount == 0) return true;
    const std::vector<unsigned char> baseAreas(compact->areas,compact->areas+compact->spanCount);
    // 空间规则在侵蚀和区域划分之前覆盖 Area；同优先级按稳定身份确定顺序。
    for(const auto& record:modifiers)
    {
        const auto& modifier=record.modifier;
        const auto inverse=glm::inverse(record.worldTransform);
        const auto* area=modifier.exclude?nullptr:FindNavigationAreaByName(areas,modifier.area);
        for(int row=0;row<compact->height;++row) for(int column=0;column<compact->width;++column)
        {
            const auto& cell=compact->cells[column+row*compact->width];
            for(unsigned int index=cell.index;index<cell.index+cell.count;++index)
            {
                if(baseAreas[index]==RC_NULL_AREA) continue;
                const auto local=glm::vec3(inverse*glm::vec4(compact->bmin[0]+(column+.5f)*compact->cs,
                    compact->bmin[1]+compact->spans[index].y*compact->ch,compact->bmin[2]+(row+.5f)*compact->cs,1));
                bool inside=false;
                if(modifier.shape==VansNavigationModifierShape::Box)
                    inside=glm::all(glm::lessThanEqual(glm::abs(local-modifier.centerMeters),modifier.halfExtentsMeters));
                else if(local.y>=modifier.minimumHeightMeters && local.y<=modifier.maximumHeightMeters)
                {
                    inside=true; float sign=0;
                    const auto point=glm::vec2(local.x,local.z);
                    for(std::size_t vertex=0;vertex<modifier.convexVerticesMeters.size();++vertex)
                    {
                        const auto a=modifier.convexVerticesMeters[vertex],b=modifier.convexVerticesMeters[(vertex+1)%modifier.convexVerticesMeters.size()];
                        const float cross=(b.x-a.x)*(point.y-a.y)-(b.y-a.y)*(point.x-a.x);
                        if(std::abs(cross)<1.e-5f) continue;
                        if(sign!=0 && cross*sign<0) { inside=false; break; }
                        sign=cross;
                    }
                }
                if(inside) compact->areas[index]=modifier.exclude?RC_NULL_AREA:static_cast<unsigned char>(area->id+1u);
            }
        }
    }
	if (!CheckBuildCancellation(control,error)) return false;
	if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) ||
		!rcBuildDistanceField(&context, *compact) ||
		!rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
	{
		error = "Recast could not build navigable regions";
		return false;
	}
	if(!obstacles.empty())
	{
		// Layer builder must not reintroduce regions removed by the common base build.
		for(int span=0;span<compact->spanCount;++span)
			if(compact->spans[span].reg==0) compact->areas[span]=RC_NULL_AREA;
		const auto result=BuildNavigationCarvedTileCache(context,*compact,config.borderSize,config.walkableHeight,
			bakeSettings,areas,x,z,obstacles,links,availableBytes,control,output,error,buildContext,layerData);
		if(result==VansNavigationTileCacheBuildResult::Built) return true;
		if(result==VansNavigationTileCacheBuildResult::Failed) return false;
		MarkNavigationCarveAreas(*compact,bakeSettings,obstacles);
		if(!CheckBuildCancellation(control,error) || !rcBuildDistanceField(&context,*compact) ||
			!rcBuildRegions(&context,*compact,config.borderSize,config.minRegionArea,config.mergeRegionArea))
		{ if(error.empty()) error="Recast could not rebuild Carve regions"; return false; }
	}

	if (!CheckBuildCancellation(control,error)) return false;
	contours.reset(rcAllocContourSet());
	if (!contours || !rcBuildContours(&context, *compact,
		config.maxSimplificationError, config.maxEdgeLen, *contours))
	{
		error = "Recast could not build navigation contours";
		return false;
	}
	if (contours->nconts == 0) return true;
	if (!CheckBuildCancellation(control,error)) return false;
	polyMesh.reset(rcAllocPolyMesh());
	if (!polyMesh || !rcBuildPolyMesh(&context, *contours,
		config.maxVertsPerPoly, *polyMesh))
	{
		error = "Recast produced no navigation polygons";
		return false;
	}
	if (polyMesh->npolys == 0) return true;
	if (polyMesh->npolys > bakeSettings.maximumPolygonsPerTile)
	{ error = "Navigation Tile polygon count exceeds native reference capacity"; return false; }
	if (!CheckBuildCancellation(control,error)) return false;
	detailMesh.reset(rcAllocPolyMeshDetail());
	if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact,
		config.detailSampleDist, config.detailSampleMaxError, *detailMesh))
	{
		error = "Recast could not build navigation detail mesh";
		return false;
	}
	if(!layerData || !bakeSettings.buildTileCacheLayers || !obstacles.empty()) compact.reset();
	contours.reset();

	for (int polygon = 0; polygon < polyMesh->npolys; ++polygon)
	{
		if (polyMesh->areas[polygon] == RC_NULL_AREA)
		{
			error = "Recast produced a polygon without a navigation area";
			return false;
		}
		const std::uint8_t areaId =
			static_cast<std::uint8_t>(polyMesh->areas[polygon] - 1u);
		if (!FindNavigationAreaById(areas, areaId))
		{
			error = "Recast produced an undefined navigation area";
			return false;
		}
		polyMesh->areas[polygon] = areaId;
		polyMesh->flags[polygon] = kWalkableFlag;
	}

	dtNavMeshCreateParams params{};
	params.tileX = x;
	params.tileY = z;
	params.tileLayer = 0;
	params.verts = polyMesh->verts;
	params.vertCount = polyMesh->nverts;
	params.polys = polyMesh->polys;
	params.polyAreas = polyMesh->areas;
	params.polyFlags = polyMesh->flags;
	params.polyCount = polyMesh->npolys;
	params.nvp = polyMesh->nvp;
	params.detailMeshes = detailMesh->meshes;
	params.detailVerts = detailMesh->verts;
	params.detailVertsCount = detailMesh->nverts;
	params.detailTris = detailMesh->tris;
	params.detailTriCount = detailMesh->ntris;
	params.walkableHeight = bakeSettings.agentHeight;
	params.walkableRadius = bakeSettings.agentRadius;
	params.walkableClimb = bakeSettings.agentMaxClimb;
	rcVcopy(params.bmin, polyMesh->bmin);
	rcVcopy(params.bmax, polyMesh->bmax);
	params.cs = config.cs;
	params.ch = config.ch;
	params.buildBvTree = true;
    VansNavigationNativeLinks nativeLinks;
    if(!nativeLinks.Compile(links,areas,bakeSettings,x,z,error)) return false;
    nativeLinks.Apply(params);

	unsigned char* navData = nullptr;
	int navDataSize = 0;
	if (!dtCreateNavMeshData(&params, &navData, &navDataSize) || !navData || navDataSize <= 0)
	{
		error = "Detour could not create navigation mesh data";
		return false;
	}
	std::unique_ptr<unsigned char, decltype(&dtFree)> navDataOwner(navData, &dtFree);
	if (navDataSize > 8 * 1024 * 1024)
	{ error = "Navigation Tile cooked payload exceeds byte capacity"; return false; }
	VansNavigationTilePayload tile{ x, z, 0, std::vector<unsigned char>(navData, navData + navDataSize) };
	if (!ValidateNavigationTileData(tile.bytes, areas, error)) return false;
	output.push_back(std::move(tile));
    if(layerData && obstacles.empty() && bakeSettings.buildTileCacheLayers)
    {
        // Preserve the exact Recast detail payload. Layer capture is an owned
        // companion of this build, never a replacement query mesh.
        polyMesh.reset(); detailMesh.reset();
        for(int span=0;span<compact->spanCount;++span) if(compact->spans[span].reg==0) compact->areas[span]=RC_NULL_AREA;
        const auto bytes=static_cast<std::uint64_t>(navDataSize)*2u+output.capacity()*sizeof(VansNavigationTilePayload);
        if(bytes>=availableBytes) { error="Navigation Tile/base layer companion exceeds build budget"; return false; }
        const auto result=BuildNavigationTileCacheLayers(context,*compact,config.borderSize,config.walkableHeight,
            bakeSettings,areas,x,z,availableBytes-bytes,control,*layerData,error,buildContext);
        if(result==VansNavigationTileCacheBuildResult::Failed) return false;
    }
    compact.reset();
	return CheckBuildCancellation(control,error);

}
}

namespace
{
bool ValidateProfileInput(const VansNavigationProfileBuildInput& profile,const VansNavigationSettings& settings,
    const VansNavigationMapSettings& map,bool allowEmpty,std::uint64_t& fingerprintScratch,std::string& error)
{
    if(std::find(map.profiles.begin(),map.profiles.end(),profile.profileId)==map.profiles.end() ||
        !profile.geometry || (!allowEmpty && profile.geometry->Empty() && profile.heightfields.empty()) ||
        !IsFiniteGeometry(*profile.geometry) || profile.contributions.size()>262144 || profile.modifiers.size()>4096 || profile.footprints.size()>267264 ||
        profile.carveObstacles.size()>kMaximumNavigationObstacles || profile.links.size()>kMaximumNavigationLinkCapabilities)
    { error="Navigation Profile input has invalid identity, geometry or dependency capacity"; return false; }
    for(const auto area:profile.geometry->areas) if(!FindNavigationAreaById(settings.areas,area))
    { error="Navigation geometry references an undefined Area"; return false; }
    std::set<std::string> linkIds; std::set<std::uint32_t> linkKeys;
    for(const auto& link:profile.links)
        if(!ValidateNavigationLinkGeometry(link,settings.areas,error) || !linkIds.insert(link.id).second || !linkKeys.insert(link.key.value).second)
        { if(error.empty()) error="Navigation Link input has duplicate identities"; return false; }
    fingerprintScratch=(std::max)(fingerprintScratch,static_cast<std::uint64_t>(profile.geometry->TriangleCount())*48u);
    if(!profile.links.empty()) fingerprintScratch=(std::max)(fingerprintScratch,static_cast<std::uint64_t>(profile.links.size())*128u+4096u);
    if(profile.heightfields.size()>65536) { error="Navigation heightfield source count exceeds capacity"; return false; }
    std::set<std::string> fieldIds;
    for(const auto& field:profile.heightfields)
    {
        if(!fieldIds.insert(field.sourceId).second || !FindNavigationAreaById(settings.areas,field.area) ||
            !ValidateNavigationHeightfieldInput(field,error)) return false;
    }
    std::string_view previousFootprint;
    for(const auto& footprint:profile.footprints)
    {
        if(footprint.id.empty() || footprint.id.size()>512 || !footprint.contentHash || footprint.id<=previousFootprint ||
            !std::isfinite(footprint.boundsMinimum.x) || !std::isfinite(footprint.boundsMinimum.y) || !std::isfinite(footprint.boundsMinimum.z) ||
            !std::isfinite(footprint.boundsMaximum.x) || !std::isfinite(footprint.boundsMaximum.y) || !std::isfinite(footprint.boundsMaximum.z) ||
            glm::any(glm::greaterThan(footprint.boundsMinimum,footprint.boundsMaximum)))
        { error="Navigation runtime contribution coverage is invalid"; return false; }
        previousFootprint=footprint.id;
    }
    std::set<std::string> modifierIds;
    for(const auto& modifier:profile.modifiers)
    {
        if(!modifierIds.insert(modifier.id).second || !ValidateNavigationModifierRecord(modifier,settings,map.id,error) ||
            std::find(modifier.modifier.profiles.begin(),modifier.modifier.profiles.end(),profile.profileId)==modifier.modifier.profiles.end())
        { if(error.empty()) error="Navigation Modifier Profile or identity is incompatible with its input"; return false; }
    }
    if(!profile.carveObstacles.empty() && map.updateMode!=VansNavigationMapUpdateMode::Runtime)
    { error="Navigation Carve requires a Runtime Map"; return false; }
    std::string_view previousObstacle;
    for(const auto& shape:profile.carveObstacles)
    {
        if(!ValidateNavigationObstacleGeometry(shape,error)) return false;
        if(shape.id<=previousObstacle) { error="Navigation Carve identities must be unique and sorted"; return false; }
        previousObstacle=shape.id;
    }
    return true;
}
bool BuildProfileTiles(const VansNavigationGeometry& geometry, const std::vector<VansNavigationHeightfieldInput>& heightfields, const VansNavigationBakeSettings& bake,
    const VansNavigationAreaSettings& areas, const std::vector<VansNavigationModifierRecord>& modifiers,
    const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,std::uint64_t availableBytes,
    VansNavigationProfileBuildData& output, std::string& error, const VansNavigationBuildControl* control,
    const std::vector<VansNavigationTileCell>* selectedCells=nullptr,float rasterOriginMeters=0,
    VansNavigationBuildContext* buildContext=nullptr,std::uint64_t contextBaseBytes=0,
    std::vector<VansNavigationTileLayerData>* layerRows=nullptr,std::uint64_t baseHash=0)
{
    VansNavigationBakeQuantization quantization;
    if (!QuantizeNavigationBakeSettings(bake, quantization, error)) return false;
    if(selectedCells && geometry.Empty() && heightfields.empty()) { error.clear(); return true; }
    float boundsMinimum[3]{(std::numeric_limits<float>::max)(),(std::numeric_limits<float>::max)(),(std::numeric_limits<float>::max)()};
    float boundsMaximum[3]{-boundsMinimum[0],-boundsMinimum[1],-boundsMinimum[2]};
    if(!geometry.Empty()) rcCalcBounds(geometry.vertices.data(),static_cast<int>(geometry.VertexCount()),boundsMinimum,boundsMaximum);
    for(const auto& field:heightfields) for(int axis=0;axis<3;++axis)
    {
        boundsMinimum[axis]=(std::min)(boundsMinimum[axis],field.boundsMinimum[axis]);
        boundsMaximum[axis]=(std::max)(boundsMaximum[axis],field.boundsMaximum[axis]);
    }
    if(selectedCells)
    {
        const auto lowerSteps=(std::max)(0.0,std::ceil((static_cast<double>(rasterOriginMeters)-boundsMinimum[1])/bake.cellHeight));
        boundsMinimum[1]=static_cast<float>(static_cast<double>(rasterOriginMeters)-lowerSteps*bake.cellHeight);
    }
    const double verticalCells = (static_cast<double>(boundsMaximum[1]) - boundsMinimum[1]) / bake.cellHeight;
    const double tileWidth = static_cast<double>(bake.cellSize) * bake.tileSizeCells;
    if (!std::isfinite(verticalCells) || verticalCells > RC_SPAN_MAX_HEIGHT ||
        !std::isfinite(tileWidth) || tileWidth <= 0)
    { error = "Navigation Tile raster dimensions exceed native integer capacity"; return false; }
    const auto coordinate = [&](double position, int axis, int& value)
    {
        const double cell = std::floor((position - bake.tileOrigin[axis]) / tileWidth);
        if (!std::isfinite(cell) || cell < (std::numeric_limits<int>::min)() + 2.0 ||
            cell > (std::numeric_limits<int>::max)() - 2.0)
        { error = "Navigation Tile coordinate exceeds native integer capacity"; return false; }
        value = static_cast<int>(cell);
        return true;
    };
    int minimumX, maximumX, minimumZ, maximumZ;
    if (!coordinate(boundsMinimum[0], 0, minimumX) || !coordinate(boundsMaximum[0], 0, maximumX) ||
        !coordinate(boundsMinimum[2], 2, minimumZ) || !coordinate(boundsMaximum[2], 2, maximumZ)) return false;
    const auto width = static_cast<std::uint64_t>(static_cast<std::int64_t>(maximumX) - minimumX + 1);
    const auto height = static_cast<std::uint64_t>(static_cast<std::int64_t>(maximumZ) - minimumZ + 1);
    if (!selectedCells && (width > static_cast<unsigned>(bake.maximumTiles) || height > static_cast<unsigned>(bake.maximumTiles) ||
        width * height > static_cast<unsigned>(bake.maximumTiles)))
    { error = "Navigation Tile grid exceeds the configured map capacity"; return false; }
    const auto slots = selectedCells?selectedCells->size():static_cast<std::size_t>(width * height);
    const auto budget = static_cast<std::uint64_t>(availableBytes);
    // 指纹规范化、空间索引及源数据也计入预算，避免只报告单个 native heightfield。
    const std::uint64_t inputBytes = geometry.vertices.size() * sizeof(float) + geometry.indices.size() * sizeof(int) +
        geometry.areas.size() + geometry.TriangleCount() * (sizeof(TriangleBounds) + 48u) +
        slots * sizeof(std::vector<std::size_t>);
    if (inputBytes >= budget)
    { error = "Navigation source/index memory exceeds the configured build byte budget"; return false; }
    const int border = quantization.radiusCells + 3;
    const double borderMeters = static_cast<double>(border) * bake.cellSize;
    std::vector<TriangleBounds> triangleBounds;
    triangleBounds.reserve(geometry.TriangleCount());
    std::vector<std::vector<std::size_t>> bins(slots);
    std::uint64_t references = 0;
    for (std::size_t triangle = 0; triangle < geometry.TriangleCount(); ++triangle)
    {
        if ((triangle & 1023u)==0 && !CheckBuildCancellation(control,error)) return false;
        TriangleBounds box{ (std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)() };
        for (std::size_t corner = 0; corner < 3u; ++corner)
        {
            const auto vertex = static_cast<std::size_t>(geometry.indices[triangle * 3u + corner]) * 3u;
            box.minimumX = (std::min)(box.minimumX, geometry.vertices[vertex]);
            box.maximumX = (std::max)(box.maximumX, geometry.vertices[vertex]);
            box.minimumZ = (std::min)(box.minimumZ, geometry.vertices[vertex + 2u]);
            box.maximumZ = (std::max)(box.maximumZ, geometry.vertices[vertex + 2u]);
        }
        triangleBounds.push_back(box);
        int firstX, lastX, firstZ, lastZ;
        if (!coordinate(box.minimumX - borderMeters, 0, firstX) || !coordinate(box.maximumX + borderMeters, 0, lastX) ||
            !coordinate(box.minimumZ - borderMeters, 2, firstZ) || !coordinate(box.maximumZ + borderMeters, 2, lastZ)) return false;
        if(selectedCells)
        {
            // A small bounded selection never allocates the full world grid or
            // iterates the potentially enormous rectangle of a source triangle.
            for(std::size_t slot=0;slot<selectedCells->size();++slot)
            {
                const auto cell=(*selectedCells)[slot];
                if(cell.x<firstX || cell.x>lastX || cell.z<firstZ || cell.z>lastZ) continue;
                if(references>=(budget-inputBytes)/(sizeof(std::size_t)*2u))
                { error="Navigation Tile source index exceeds the configured build byte budget"; return false; }
                ++references; bins[slot].push_back(triangle);
            }
            continue;
        }
        firstX = (std::max)(firstX, minimumX); lastX = (std::min)(lastX, maximumX);
        firstZ = (std::max)(firstZ, minimumZ); lastZ = (std::min)(lastZ, maximumZ);
        const auto additions = static_cast<std::uint64_t>(lastX - firstX + 1) * static_cast<std::uint64_t>(lastZ - firstZ + 1);
        if (additions > (budget - inputBytes) / (sizeof(std::size_t) * 2u) - references)
        { error = "Navigation Tile source index exceeds the configured build byte budget"; return false; }
        references += additions;
        for (int z = firstZ; z <= lastZ; ++z)
            for (int x = firstX; x <= lastX; ++x)
                bins[static_cast<std::size_t>(z - minimumZ) * width + static_cast<std::size_t>(x - minimumX)].push_back(triangle);
    }
    std::uint64_t payloadBytes = 0;
    for(const auto& tile:output.tiles) payloadBytes+=tile.bytes.size();
    if(layerRows) for(const auto& row:*layerRows) payloadBytes+=row.EstimateBytes();
    for (std::size_t slot=0;slot<slots;++slot)
        {
            const int x=selectedCells?(*selectedCells)[slot].x:minimumX+static_cast<int>(slot%width);
            const int z=selectedCells?(*selectedCells)[slot].z:minimumZ+static_cast<int>(slot/width);
            if (!CheckBuildCancellation(control,error)) return false;
            const auto& triangles = bins[slot];
            if (triangles.empty() && heightfields.empty()) continue;
            rcConfig config{};
            config.cs = bake.cellSize; config.ch = bake.cellHeight;
            config.walkableSlopeAngle = bake.agentMaxSlopeDegrees;
            config.walkableHeight = quantization.heightCells; config.walkableClimb = quantization.climbCells;
            config.walkableRadius = quantization.radiusCells; config.borderSize = border;
            config.tileSize = bake.tileSizeCells;
            config.width = config.height = bake.tileSizeCells + border * 2;
            config.maxEdgeLen = quantization.maximumEdgeCells; config.maxSimplificationError = bake.edgeMaxError;
            config.minRegionArea = quantization.minimumRegionCellArea; config.mergeRegionArea = quantization.mergeRegionCellArea;
            config.maxVertsPerPoly = bake.maximumVerticesPerPolygon;
            config.detailSampleDist = quantization.detailSampleDistanceMeters; config.detailSampleMaxError = quantization.detailSampleErrorMeters;
            config.bmin[0] = static_cast<float>(bake.tileOrigin.x + x * tileWidth - borderMeters);
            config.bmax[0] = static_cast<float>(bake.tileOrigin.x + (static_cast<double>(x) + 1) * tileWidth + borderMeters);
            config.bmin[2] = static_cast<float>(bake.tileOrigin.z + z * tileWidth - borderMeters);
            config.bmax[2] = static_cast<float>(bake.tileOrigin.z + (static_cast<double>(z) + 1) * tileWidth + borderMeters);
            config.bmin[1] = boundsMinimum[1]; config.bmax[1] = boundsMaximum[1];
            if (!std::isfinite(config.bmin[0]) || !std::isfinite(config.bmax[0]) ||
                !std::isfinite(config.bmin[2]) || !std::isfinite(config.bmax[2]) ||
                config.bmax[0] <= config.bmin[0] || config.bmax[2] <= config.bmin[2])
            { error = "Navigation Tile world coordinates are not representable"; return false; }
            const auto contextBytes=buildContext?VansNavigationBuildContextAccess::WorkingBytes(*buildContext):0;
            const auto contextGrowth=contextBytes>contextBaseBytes?contextBytes-contextBaseBytes:0;
            const std::uint64_t baseBytes = contextGrowth + inputBytes + references * sizeof(std::size_t) * 2u + payloadBytes +
                static_cast<std::uint64_t>(config.width) * config.height * 256u + triangles.size() * 16u+links.size()*80u+4096u;
            if (baseBytes >= budget)
            { error = "Navigation Tile working memory exceeds the configured build byte budget"; return false; }
            VansNavigationGeometry heightfieldGeometry;
            if(!CollectNavigationHeightfieldTile(heightfields,glm::vec3(config.bmin[0],config.bmin[1],config.bmin[2]),
                glm::vec3(config.bmax[0],config.bmax[1],config.bmax[2]),(budget-baseBytes)/2,heightfieldGeometry,error)) return false;
            if(triangles.empty() && heightfieldGeometry.Empty()) continue;
            const std::uint64_t terrainBytes=heightfieldGeometry.vertices.size()*sizeof(float)+heightfieldGeometry.indices.size()*sizeof(int)+
                heightfieldGeometry.areas.size()*2u;
            if(terrainBytes>=budget-baseBytes) { error="Navigation Tile heightfield working memory exceeds build budget"; return false; }
            std::uint64_t spanBound = 0;
            for(std::size_t triangle=0;triangle<heightfieldGeometry.TriangleCount();++triangle)
            {
                glm::vec3 low((std::numeric_limits<float>::max)()),high=-low;
                for(std::size_t corner=0;corner<3;++corner)
                {
                    const auto index=static_cast<std::size_t>(heightfieldGeometry.indices[triangle*3+corner])*3;
                    const glm::vec3 point(heightfieldGeometry.vertices[index],heightfieldGeometry.vertices[index+1],heightfieldGeometry.vertices[index+2]);
                    low=glm::min(low,point); high=glm::max(high,point);
                }
                const auto spans=static_cast<std::uint64_t>(std::ceil((high.x-low.x)/config.cs)+2)*
                    static_cast<std::uint64_t>(std::ceil((high.z-low.z)/config.cs)+2);
                if(spans>(budget-baseBytes-terrainBytes)/384u-spanBound)
                { error="Navigation Tile heightfield raster estimate exceeds build budget"; return false; }
                spanBound+=spans;
            }
            for (const auto triangle : triangles)
            {
                const auto& box = triangleBounds[triangle];
                const double cellsX = (std::max)(0.0, ((std::min)(static_cast<double>(box.maximumX), static_cast<double>(config.bmax[0])) -
                    (std::max)(static_cast<double>(box.minimumX), static_cast<double>(config.bmin[0]))) / config.cs) + 2;
                const double cellsZ = (std::max)(0.0, ((std::min)(static_cast<double>(box.maximumZ), static_cast<double>(config.bmax[2])) -
                    (std::max)(static_cast<double>(box.minimumZ), static_cast<double>(config.bmin[2]))) / config.cs) + 2;
                const auto spans = static_cast<std::uint64_t>(std::ceil(cellsX)) * static_cast<std::uint64_t>(std::ceil(cellsZ));
                if (spans > (budget - baseBytes - terrainBytes) / 384u - spanBound)
                { error = "Navigation Tile raster/contour memory estimate exceeds the configured build byte budget"; return false; }
                spanBound += spans;
            }
            std::vector<VansNavigationTilePayload> tiles;
            VansNavigationTileLayerData layerData;
            if (!BuildTile(geometry, heightfieldGeometry, bake, areas, triangles, modifiers,obstacles,links,config,x,z,
                budget-baseBytes-terrainBytes-spanBound*384u,tiles,error,control,buildContext,layerRows?&layerData:nullptr))
            { error = "Tile (" + std::to_string(x) + "," + std::to_string(z) + "): " + error; return false; }
            if(layerRows && !layerData.layers.empty())
            { layerData.baseInputHash=baseHash; payloadBytes+=layerData.EstimateBytes(); layerRows->push_back(std::move(layerData)); }
            if(buildContext && obstacles.empty()) VansNavigationBuildContextAccess::StoreBase(*buildContext,x,z,tiles);
            for(auto& tile:tiles)
            {
                if(output.tiles.size()>=static_cast<std::size_t>(bake.maximumTiles) ||
                    tile.bytes.size() > 255u * 1024u * 1024u - payloadBytes || tile.bytes.size()>budget-baseBytes-terrainBytes)
                { error = "Navigation cooked payload exceeds Tile/byte capacity"; return false; }
                payloadBytes += tile.bytes.size();
                output.tiles.push_back(std::move(tile));
            }
        }
    if ((!selectedCells || !output.tiles.empty()) && !ValidateNavigationTileSet(output.tiles, bake, areas, error)) return false;
    error.clear();
    return true;
}
}

bool VansNavigationBuilder::Build(const VansNavigationBuildInput& input,
    const VansNavigationSettings& settings,const std::string& sourceScene, const VansNavigationBuildControl* control,
    std::shared_ptr<const VansNavigationMeshAsset>& output,std::string& error)
try
{
    if(!ValidateNavigationSourceIdentity(sourceScene,error) || !ValidateNavigationSettings(settings,error)) return false;
    if (!CheckBuildCancellation(control,error)) return false;
    const auto* map=FindNavigationMap(settings,input.mapId);
    if(!map || input.profiles.size()!=map->profiles.size()) { error="Navigation input must cover exactly the Map's Profiles"; return false; }
    const auto budget=static_cast<std::uint64_t>((std::min)(map->maximumBuildBytes,settings.maximumConcurrentBuildBytes));
    VansNavigationBuildInputMemory memory;
    for(const auto& profile:input.profiles) memory.Add(profile);
    std::uint64_t inputBytes=0;
    std::set<std::string> profileIds;
    std::uint64_t fingerprintScratch=0;
    for(const auto& profile:input.profiles)
    {
        if(!profileIds.insert(profile.profileId).second)
        { error="Navigation input repeats a Profile"; return false; }
        if(!ValidateProfileInput(profile,settings,*map,false,fingerprintScratch,error)) return false;
        if(memory.bytes+fingerprintScratch>=budget) { error="Navigation Profile snapshot/fingerprint memory exceeds build budget"; return false; }
    }
    inputBytes=memory.bytes;
    auto candidate=std::shared_ptr<VansNavigationMeshAsset>(new VansNavigationMeshAsset());
    candidate->m_MapId=input.mapId; candidate->m_BakedAreas=settings.areas; candidate->m_Source.scene=sourceScene;
    candidate->m_Source.inputHash=VANS_FNV1A64_OFFSET_BASIS;
    std::uint64_t retainedBytes=0;
    std::vector<VansNavigationProfileBuildData> profiles; profiles.reserve(input.profiles.size());
    for(const auto& id:map->profiles)
    {
        if (!CheckBuildCancellation(control,error)) return false;
        const auto& source=*std::find_if(input.profiles.begin(),input.profiles.end(),[&](const auto& p){return p.profileId==id;});
        const auto& geometry=*source.geometry;
        VansNavigationProfileBuildData profile; profile.profileId=id;
        if(!ResolveNavigationBakeSettings(settings,input.mapId,id,profile.bake,error) || !ComputeNavigationBuildInputHash(source,profile.inputHash,error) || !ComputeNavigationBuildBaseHash(source,profile.baseInputHash,error)) return false;
        profile.bakeSettingsHash=HashNavigationBakeSettings(profile.bake); profile.contributions=source.contributions;
        auto modifiers=source.modifiers;
        std::sort(modifiers.begin(),modifiers.end(),[](const auto& a,const auto& b){return a.modifier.priority==b.modifier.priority?a.id<b.id:a.modifier.priority<b.modifier.priority;});
        const auto rawBytes=geometry.vertices.size()*sizeof(float)+geometry.indices.size()*sizeof(int)+geometry.areas.size();
        const auto otherInputBytes=inputBytes-rawBytes;
        if(retainedBytes+otherInputBytes>=budget || !BuildProfileTiles(geometry,source.heightfields,profile.bake,settings.areas,modifiers,source.carveObstacles,source.links,budget-retainedBytes-otherInputBytes,profile,error,control,nullptr,0,nullptr,0,
                profile.bake.buildTileCacheLayers?&profile.layerData:nullptr,profile.baseInputHash))
        { if(error.empty()) error="Navigation multi-Profile build exceeds aggregate memory budget"; return false; }
        std::uint64_t linkValidationBytes=retainedBytes+inputBytes;
        for(const auto& tile:profile.tiles) linkValidationBytes+=sizeof(tile)+tile.bytes.capacity();
        for(const auto& row:profile.layerData) linkValidationBytes+=row.EstimateBytes();
        if(!source.links.empty() && (linkValidationBytes>=budget || !ValidateNavigationConnectedLinks(profile.tiles,profile.bake,source.links,budget-linkValidationBytes,error)))
        { if(error.empty()) error="Navigation Link validation exceeds aggregate build budget"; return false; }
        std::sort(profile.layerData.begin(),profile.layerData.end(),[](const auto& a,const auto& b)
            {return VansNavigationTileCell{a.x,a.z}<VansNavigationTileCell{b.x,b.z};});
        if(!ValidateNavigationTileLayerData(profile.layerData,profile.baseInputHash,profile.bake,settings.areas,budget-otherInputBytes,error)) return false;
        for(const auto& tile:profile.tiles) retainedBytes+=tile.bytes.capacity()+sizeof(tile);
        retainedBytes+=profile.layerData.capacity()*sizeof(VansNavigationTileLayerData);
        for(const auto& row:profile.layerData) retainedBytes+=row.EstimateBytes()-sizeof(row);
        candidate->m_Source.inputHash=ContinueMemoryFnv1a64(candidate->m_Source.inputHash,&profile.inputHash,sizeof(profile.inputHash));
        profiles.push_back(std::move(profile));
    }
    if(!candidate->PublishProfiles(std::move(profiles),budget-inputBytes,error)) return false;
    if(candidate->GetTileCount()>static_cast<std::size_t>(settings.maximumResidentTiles) ||
        candidate->EstimateResidentBytes()+EstimateNavigationQueryBytes(settings.query)>static_cast<std::uint64_t>(settings.maximumResidentBytes) || retainedBytes>255u*1024u*1024u)
    { error="Navigation multi-Profile output exceeds aggregate memory/Tile capacity"; return false; }
    if (!CheckBuildCancellation(control,error)) return false;
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation build allocation failed"; return false; }

bool VansNavigationBuilder::BuildSelectedTiles(const VansNavigationProfileBuildInput& input,
    const VansNavigationSettings& settings,const std::string& mapId,
    const std::vector<VansNavigationTileCell>& cells,float rasterOriginMeters,
    std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
    VansNavigationTileBuildOutput& output,std::string& error,VansNavigationBuildContext* buildContext,
    const std::vector<VansNavigationTileLayerData>* seedLayers)
try
{
    if(!ValidateNavigationSettings(settings,error) || !CheckBuildCancellation(control,error)) return false;
    const auto* map=FindNavigationMap(settings,mapId);
    if(!map || cells.empty() || cells.size()>kMaximumNavigationTileUpdateBatch || !std::isfinite(rasterOriginMeters))
    { error="Navigation selected Tile build has invalid Map/cell capacity/raster origin"; return false; }
    auto selection=cells;
    std::sort(selection.begin(),selection.end());
    if(std::adjacent_find(selection.begin(),selection.end())!=selection.end())
    { error="Navigation selected Tile build repeats a cell"; return false; }
    const auto budget=(std::min)(maximumBytes,static_cast<std::uint64_t>((std::min)(map->maximumBuildBytes,settings.maximumConcurrentBuildBytes)));
    std::uint64_t scratchBytes=0;
    if(!ValidateProfileInput(input,settings,*map,true,scratchBytes,error)) return false;
    VansNavigationBuildInputMemory memory; memory.Add(input);
    std::uint64_t inputBytes=memory.bytes+selection.capacity()*sizeof(VansNavigationTileCell)+
        kMaximumNavigationTileUpdateBatch*sizeof(VansNavigationTilePayload)*2u+output.EstimateBytes();
    if(seedLayers)
    { inputBytes+=seedLayers->capacity()*sizeof(VansNavigationTileLayerData); for(const auto& row:*seedLayers) inputBytes+=row.EstimateBytes()-sizeof(row); }
    if(inputBytes+scratchBytes>=budget)
    { error="Navigation selected Tile snapshot/fingerprint memory exceeds build budget"; return false; }
    std::uint64_t baseHash=0;
    if(!ComputeNavigationBuildBaseHash(input,baseHash,error)) return false;
    VansNavigationProfileBuildData candidate;
    if(!ResolveNavigationBakeSettings(settings,mapId,input.profileId,candidate.bake,error)) return false;
    struct ContextLease
    {
        VansNavigationBuildContext* context=nullptr;
        ~ContextLease() { if(context) VansNavigationBuildContextAccess::End(*context); }
    } lease;
    if(buildContext)
    {
        if(!VansNavigationBuildContextAccess::Begin(*buildContext,baseHash,HashNavigationBakeSettings(candidate.bake),rasterOriginMeters,error)) return false;
        lease.context=buildContext;
    }
    const auto contextBaseBytes=buildContext?VansNavigationBuildContextAccess::WorkingBytes(*buildContext):0;
    VansNavigationTileBuildOutput result; result.baseInputHash=baseHash;
    if(buildContext && seedLayers && !VansNavigationBuildContextAccess::Seed(*buildContext,*seedLayers,candidate.bake,
        settings.areas,budget-inputBytes, error)) return false;
    std::vector<VansNavigationTileCell> missing;
    std::uint64_t exported=0;
    for(const auto& cell:selection)
    {
        if(!buildContext) { missing.push_back(cell); continue; }
        const auto live=VansNavigationBuildContextAccess::WorkingBytes(*buildContext);
        const auto growth=live>contextBaseBytes?live-contextBaseBytes:0;
        if(inputBytes+growth+exported>=budget)
        { error="Navigation retained context/candidate exceeds build budget"; return false; }
        std::vector<VansNavigationTilePayload> cached; VansNavigationTileLayerData cachedLayers;
        const auto cachedResult=VansNavigationBuildContextAccess::BuildCached(*buildContext,cell.x,cell.z,candidate.bake,
            settings.areas,input.carveObstacles,input.links,budget-inputBytes-growth-exported,control,cached,error,&cachedLayers);
        if(cachedResult==VansNavigationCachedBuildResult::Failed) return false;
        if(cachedResult==VansNavigationCachedBuildResult::Missing) { missing.push_back(cell); continue; }
        if(!cachedLayers.layers.empty()) { exported+=cachedLayers.EstimateBytes(); result.layerData.push_back(std::move(cachedLayers)); }
        for(auto& tile:cached) { exported+=tile.bytes.size(); candidate.tiles.push_back(std::move(tile)); }
    }
    if(!missing.empty())
    {
        auto modifiers=input.modifiers;
        std::sort(modifiers.begin(),modifiers.end(),[](const auto& a,const auto& b){return a.modifier.priority==b.modifier.priority?a.id<b.id:a.modifier.priority<b.modifier.priority;});
        const auto& geometry=*input.geometry;
        const auto rawBytes=geometry.vertices.size()*sizeof(float)+geometry.indices.size()*sizeof(int)+geometry.areas.size();
        if(!BuildProfileTiles(geometry,input.heightfields,candidate.bake,settings.areas,modifiers,input.carveObstacles,input.links,
            budget-(inputBytes-rawBytes),candidate,error,control,&missing,rasterOriginMeters,buildContext,contextBaseBytes,&result.layerData,baseHash)) return false;
    }
    std::sort(candidate.tiles.begin(),candidate.tiles.end(),[](const auto& a,const auto& b)
        {return a.x!=b.x?a.x<b.x:a.z!=b.z?a.z<b.z:a.layer<b.layer;});
    const auto live=buildContext?VansNavigationBuildContextAccess::WorkingBytes(*buildContext):0;
    const auto growth=live>contextBaseBytes?live-contextBaseBytes:0;
    std::sort(result.layerData.begin(),result.layerData.end(),[](const auto& a,const auto& b){return VansNavigationTileCell{a.x,a.z}<VansNavigationTileCell{b.x,b.z};});
    std::uint64_t bytes=inputBytes+growth+candidate.tiles.capacity()*sizeof(VansNavigationTilePayload)+result.EstimateBytes();
    for(const auto& tile:candidate.tiles) bytes+=tile.bytes.capacity();
    if(bytes>=budget || candidate.tiles.size()>static_cast<std::size_t>(candidate.bake.maximumTiles))
    { error="Navigation selected candidate/context exceeds Tile/byte capacity"; return false; }
    if((!candidate.tiles.empty() && !ValidateNavigationTileSet(candidate.tiles,candidate.bake,settings.areas,error)) ||
        !CheckBuildCancellation(control,error)) return false;
    if(!ValidateNavigationTileLayerData(result.layerData,baseHash,candidate.bake,settings.areas,budget-inputBytes,error)) return false;
    result.tiles=std::move(candidate.tiles); output=std::move(result); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation selected Tile allocation failed"; return false; }
}
