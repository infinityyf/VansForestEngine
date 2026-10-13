#include "VansNavigationSettingsJsonCodec.h"

#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace Vans
{
namespace
{
template <typename T> struct FloatField { const char* name; float T::* member; };
constexpr FloatField<VansNavigationAgentProfile> ProfileFloatFields[] = {
    { "heightMeters", &VansNavigationAgentProfile::heightMeters },
    { "radiusMeters", &VansNavigationAgentProfile::radiusMeters },
    { "maxClimbMeters", &VansNavigationAgentProfile::maxClimbMeters },
    { "maxSlopeDegrees", &VansNavigationAgentProfile::maxSlopeDegrees }
};
constexpr FloatField<VansNavigationMapSettings> MapFloatFields[] = {
    { "cellSizeMeters", &VansNavigationMapSettings::cellSizeMeters },
    { "cellHeightMeters", &VansNavigationMapSettings::cellHeightMeters },
    { "minimumRegionSizeCells", &VansNavigationMapSettings::minimumRegionSizeCells },
    { "mergeRegionSizeCells", &VansNavigationMapSettings::mergeRegionSizeCells },
    { "maximumEdgeLengthMeters", &VansNavigationMapSettings::maximumEdgeLengthMeters },
    { "maximumEdgeErrorCells", &VansNavigationMapSettings::maximumEdgeErrorCells },
    { "detailSampleDistanceCells", &VansNavigationMapSettings::detailSampleDistanceCells },
    { "detailSampleErrorCells", &VansNavigationMapSettings::detailSampleErrorCells }
};
constexpr const char* MapIntegerFields[] = { "maximumVerticesPerPolygon", "tileSizeCells", "maximumTiles", "maximumPolygonsPerTile", "maximumBuildBytes" };
constexpr int VansNavigationMapSettings::* MapIntegerMembers[] = { &VansNavigationMapSettings::maximumVerticesPerPolygon, &VansNavigationMapSettings::tileSizeCells,
    &VansNavigationMapSettings::maximumTiles, &VansNavigationMapSettings::maximumPolygonsPerTile, &VansNavigationMapSettings::maximumBuildBytes };
template<typename T> struct IntField
{
	const char* name;
	int T::* member;
};
constexpr IntField<VansNavigationQuerySettings> QueryIntFields[] = {
	{ "maximumSearchNodes", &VansNavigationQuerySettings::maximumSearchNodes },
	{ "maximumCorridorPolygons", &VansNavigationQuerySettings::maximumCorridorPolygons },
	{ "maximumPathPoints", &VansNavigationQuerySettings::maximumPathPoints },
	{ "maximumActiveAgents", &VansNavigationQuerySettings::maximumActiveAgents },
	{ "maximumIterationsPerFrame", &VansNavigationQuerySettings::maximumIterationsPerFrame },
	{ "maximumIterationsPerSlice", &VansNavigationQuerySettings::maximumIterationsPerSlice },
	{ "maximumPathStartsPerFrame", &VansNavigationQuerySettings::maximumPathStartsPerFrame },
    { "maximumQueuedQueries", &VansNavigationQuerySettings::maximumQueuedQueries },
    { "maximumActiveQueries", &VansNavigationQuerySettings::maximumActiveQueries },
    { "maximumQueryHistory", &VansNavigationQuerySettings::maximumQueryHistory },
    { "maximumQueryFrames", &VansNavigationQuerySettings::maximumQueryFrames }
};

constexpr FloatField<VansNavigationAvoidanceSettings> AvoidanceFloatFields[] = {
    {"queryRangeMeters",&VansNavigationAvoidanceSettings::queryRangeMeters},
    {"horizonSeconds",&VansNavigationAvoidanceSettings::horizonSeconds},
    {"maximumYieldSeconds",&VansNavigationAvoidanceSettings::maximumYieldSeconds}
};
constexpr IntField<VansNavigationAvoidanceSettings> AvoidanceIntFields[] = {
    {"maximumNeighbors",&VansNavigationAvoidanceSettings::maximumNeighbors},
    {"maximumNeighborScans",&VansNavigationAvoidanceSettings::maximumNeighborScans},
    {"maximumCellsPerBody",&VansNavigationAvoidanceSettings::maximumCellsPerBody},
    {"maximumGridEntries",&VansNavigationAvoidanceSettings::maximumGridEntries},
    {"maximumBoundaryPolygons",&VansNavigationAvoidanceSettings::maximumBoundaryPolygons},
    {"maximumBoundarySegments",&VansNavigationAvoidanceSettings::maximumBoundarySegments},
    {"adaptiveDivisions",&VansNavigationAvoidanceSettings::adaptiveDivisions},
    {"adaptiveRings",&VansNavigationAvoidanceSettings::adaptiveRings},
    {"adaptiveDepth",&VansNavigationAvoidanceSettings::adaptiveDepth},
    {"maximumSamplesPerFrame",&VansNavigationAvoidanceSettings::maximumSamplesPerFrame}
};
const VansSerializedValue* Field(const VansSerializedValue& object, std::string_view name)
{
	for (const auto& entry : object.objectFields)
		if (entry.first == name) return &entry.second;
	return nullptr;
}

// 原生文档也可能含重复键；在读取前统一拒绝，不能借 JSON 转换覆盖作者值。
bool CheckFields(const VansSerializedValue& object,
	const std::vector<std::string_view>& names, const std::string& path, std::string& error)
{
	if (object.kind != VansSerializedValue::Kind::Object)
	{
		error = path + " must be an object";
		return false;
	}
	std::unordered_set<std::string_view> seen;
	for (const auto& entry : object.objectFields)
	{
		bool known = false;
		for (const auto name : names) known |= entry.first == name;
		if (!known || !seen.insert(entry.first).second)
		{
			error = path + "/" + entry.first + " is unknown or duplicated";
			return false;
		}
	}
	for (const auto name : names)
		if (!Field(object, name))
		{
			error = path + "/" + std::string(name) + " is required";
			return false;
		}
	return true;
}

bool ReadFloat(const VansSerializedValue& value, float& output,
	const std::string& path, std::string& error)
{
	const double number = value.kind == VansSerializedValue::Kind::Int
		? static_cast<double>(value.intValue) : value.floatValue;
	if ((value.kind != VansSerializedValue::Kind::Int &&
		value.kind != VansSerializedValue::Kind::Float) || !std::isfinite(number) ||
		std::abs(number) > std::numeric_limits<float>::max())
	{
		error = path + " must be a finite representable number";
		return false;
	}
	output = static_cast<float>(number);
	return true;
}

bool ReadInt(const VansSerializedValue& value, int& output,
	const std::string& path, std::string& error)
{
	if (value.kind != VansSerializedValue::Kind::Int ||
		value.intValue < std::numeric_limits<int>::min() ||
		value.intValue > std::numeric_limits<int>::max())
	{
		error = path + " must be a representable integer";
		return false;
	}
	output = static_cast<int>(value.intValue);
	return true;
}

bool ReadString(const VansSerializedValue& value, std::string& output,
	const std::string& path, std::string& error)
{
	if (value.kind != VansSerializedValue::Kind::String)
	{
		error = path + " must be a string";
		return false;
	}
	output = value.stringValue;
	return true;
}

bool ReadBool(const VansSerializedValue& value, bool& output,
	const std::string& path, std::string& error)
{
	if (value.kind != VansSerializedValue::Kind::Bool)
	{
		error = path + " must be a boolean";
		return false;
	}
	output = value.boolValue;
	return true;
}
}

bool VansNavigationSettingsJsonCodec::Decode(const VansSerializedValue& root,
	VansNavigationSettings& settings, std::string& error)
{
	error.clear();
    if (!CheckFields(root, { "profiles", "maps", "query", "avoidance", "areas", "queryFilters", "maximumResidentBytes", "maximumConcurrentBuildBytes", "maximumResidentTiles" }, "NavigationSettings", error)) return false;
    VansNavigationSettings candidate;
    if (!ReadInt(*Field(root, "maximumResidentBytes"), candidate.maximumResidentBytes, "NavigationSettings/maximumResidentBytes", error) ||
        !ReadInt(*Field(root, "maximumConcurrentBuildBytes"), candidate.maximumConcurrentBuildBytes, "NavigationSettings/maximumConcurrentBuildBytes", error) ||
        !ReadInt(*Field(root, "maximumResidentTiles"), candidate.maximumResidentTiles, "NavigationSettings/maximumResidentTiles", error)) return false;
    const auto& profiles = *Field(root, "profiles");
    const auto& maps = *Field(root, "maps");
    const auto& query = *Field(root, "query");
    const auto& areas = *Field(root, "areas");
    const auto& avoidance=*Field(root,"avoidance");
    std::vector<std::string_view> avoidanceNames;
    for(const auto& field:AvoidanceFloatFields) avoidanceNames.push_back(field.name);
    for(const auto& field:AvoidanceIntFields) avoidanceNames.push_back(field.name);
    if(!CheckFields(avoidance,avoidanceNames,"NavigationSettings/avoidance",error)) return false;
    for(const auto& field:AvoidanceFloatFields)
        if(!ReadFloat(*Field(avoidance,field.name),candidate.avoidance.*field.member,std::string("NavigationSettings/avoidance/")+field.name,error)) return false;
    for(const auto& field:AvoidanceIntFields)
        if(!ReadInt(*Field(avoidance,field.name),candidate.avoidance.*field.member,std::string("NavigationSettings/avoidance/")+field.name,error)) return false;
    if (profiles.kind != VansSerializedValue::Kind::Array || profiles.arrayItems.empty() || profiles.arrayItems.size() > kMaximumNavigationProfiles ||
        maps.kind != VansSerializedValue::Kind::Array || maps.arrayItems.empty() || maps.arrayItems.size() > kMaximumNavigationMaps)
    { error = "NavigationSettings requires bounded profiles/maps arrays"; return false; }
    candidate.profiles.clear(); candidate.maps.clear();
    std::vector<std::string_view> profileNames{ "id" }, mapNames{ "id", "updateMode", "profiles", "originMeters", "detailSamplingEnabled" }, queryNames;
    for (const auto& field : ProfileFloatFields) profileNames.push_back(field.name);
    for (const auto& field : MapFloatFields) mapNames.push_back(field.name);
    for (const auto* name : MapIntegerFields) mapNames.push_back(name);
    for (const auto& field : QueryIntFields) queryNames.push_back(field.name);
    if (!CheckFields(query, queryNames, "NavigationSettings/query", error) ||
        !CheckFields(areas, { "default", "definitions" }, "NavigationSettings/areas", error)) return false;
    for (std::size_t i = 0; i < profiles.arrayItems.size(); ++i)
    {
        const auto& item = profiles.arrayItems[i];
        const auto path = "NavigationSettings/profiles/" + std::to_string(i);
        VansNavigationAgentProfile profile;
        if (!CheckFields(item, profileNames, path, error) || !ReadString(*Field(item, "id"), profile.id, path + "/id", error)) return false;
        for (const auto& field : ProfileFloatFields)
            if (!ReadFloat(*Field(item, field.name), profile.*field.member, path + "/" + field.name, error)) return false;
        candidate.profiles.push_back(std::move(profile));
    }
    for (std::size_t i = 0; i < maps.arrayItems.size(); ++i)
    {
        const auto& item = maps.arrayItems[i];
        const auto path = "NavigationSettings/maps/" + std::to_string(i);
        VansNavigationMapSettings map;
        std::string mode;
        if (!CheckFields(item, mapNames, path, error) || !ReadString(*Field(item, "id"), map.id, path + "/id", error) ||
            !ReadString(*Field(item,"updateMode"),mode,path+"/updateMode",error) ||
            !ReadBool(*Field(item, "detailSamplingEnabled"), map.detailSamplingEnabled, path + "/detailSamplingEnabled", error)) return false;
        if(mode==NavigationMapUpdateModeName(VansNavigationMapUpdateMode::Baked)) map.updateMode=VansNavigationMapUpdateMode::Baked;
        else if(mode==NavigationMapUpdateModeName(VansNavigationMapUpdateMode::Runtime)) map.updateMode=VansNavigationMapUpdateMode::Runtime;
        else { error=path+"/updateMode must be Baked or Runtime"; return false; }
        for (const auto& field : MapFloatFields)
            if (!ReadFloat(*Field(item, field.name), map.*field.member, path + "/" + field.name, error)) return false;
        for (std::size_t field = 0; field < std::size(MapIntegerFields); ++field)
            if (!ReadInt(*Field(item, MapIntegerFields[field]), map.*MapIntegerMembers[field], path + "/" + MapIntegerFields[field], error)) return false;
        const auto& origin = *Field(item, "originMeters");
        const auto& references = *Field(item, "profiles");
        if (origin.kind != VansSerializedValue::Kind::Array || origin.arrayItems.size() != 3u ||
            references.kind != VansSerializedValue::Kind::Array || references.arrayItems.empty() || references.arrayItems.size() > kMaximumNavigationProfiles)
        { error = path + " requires originMeters[3] and bounded Profile references"; return false; }
        for (std::size_t axis = 0; axis < 3u; ++axis)
            if (!ReadFloat(origin.arrayItems[axis], map.originMeters[axis], path + "/originMeters/" + std::to_string(axis), error)) return false;
        map.profiles.clear();
        for (const auto& value : references.arrayItems)
        { std::string id; if (!ReadString(value, id, path + "/profiles", error)) return false; map.profiles.push_back(std::move(id)); }
        candidate.maps.push_back(std::move(map));
    }
	for (const auto& field : QueryIntFields)
		if (!ReadInt(*Field(query, field.name), candidate.query.*field.member,
			std::string("NavigationSettings/query/") + field.name, error)) return false;
	if (!ReadString(*Field(areas, "default"), candidate.areas.defaultArea,
		"NavigationSettings/areas/default", error)) return false;
	const auto& definitions = *Field(areas, "definitions");
	if (definitions.kind != VansSerializedValue::Kind::Array ||
		definitions.arrayItems.empty() || definitions.arrayItems.size() >
		static_cast<std::size_t>(kMaximumNavigationAreaId) + 1u)
	{
		error = "NavigationSettings/areas/definitions must contain 1 to 63 areas";
		return false;
	}
	candidate.areas.definitions.clear();
	for (std::size_t index = 0; index < definitions.arrayItems.size(); ++index)
	{
		const auto& item = definitions.arrayItems[index];
		const std::string path = "NavigationSettings/areas/definitions/" + std::to_string(index);
		if (!CheckFields(item, { "name", "id", "traversalCost", "traversable" }, path, error))
			return false;
		VansNavigationAreaDefinition area;
		int id = 0;
		if (!ReadString(*Field(item, "name"), area.name, path + "/name", error) ||
			!ReadInt(*Field(item, "id"), id, path + "/id", error) ||
			!ReadFloat(*Field(item, "traversalCost"), area.traversalCost,
				path + "/traversalCost", error) ||
			!ReadBool(*Field(item, "traversable"), area.traversable,
				path + "/traversable", error)) return false;
		if (id < 0 || id > kMaximumNavigationAreaId)
		{
			error = path + "/id must be in [0, 62]";
			return false;
		}
		area.id = static_cast<std::uint8_t>(id);
		candidate.areas.definitions.push_back(std::move(area));
	}
	const auto& filters = *Field(root, "queryFilters");
	if (filters.kind != VansSerializedValue::Kind::Array || filters.arrayItems.empty() || filters.arrayItems.size() > kMaximumNavigationQueryFilters)
	{ error = "NavigationSettings/queryFilters requires 1 to 64 filters"; return false; }
	candidate.queryFilters.clear();
	for (std::size_t index = 0; index < filters.arrayItems.size(); ++index)
	{
		const auto& item = filters.arrayItems[index];
		const auto path = "NavigationSettings/queryFilters/" + std::to_string(index);
		if (!CheckFields(item, { "id", "excludedAreas", "areaCosts" }, path, error)) return false;
		VansNavigationQueryFilterSettings filter;
		if (!ReadString(*Field(item, "id"), filter.id, path + "/id", error)) return false;
		const auto& exclusions = *Field(item, "excludedAreas");
		const auto& costs = *Field(item, "areaCosts");
		if (exclusions.kind != VansSerializedValue::Kind::Array || exclusions.arrayItems.size() > 63u ||
			costs.kind != VansSerializedValue::Kind::Object || costs.objectFields.size() > 63u)
		{ error = path + " requires bounded excludedAreas array and areaCosts object"; return false; }
		for (const auto& exclusion : exclusions.arrayItems)
		{
			std::string area;
			if (!ReadString(exclusion, area, path + "/excludedAreas", error)) return false;
			filter.excludedAreas.push_back(std::move(area));
		}
		for (const auto& [name, value] : costs.objectFields)
		{
			float cost = 0.0f;
			if (!ReadFloat(value, cost, path + "/areaCosts/" + name, error)) return false;
			filter.areaCosts.emplace_back(name, cost);
		}
		candidate.queryFilters.push_back(std::move(filter));
	}
	if (!ValidateNavigationSettings(candidate, error)) return false;
	settings = std::move(candidate);
	return true;
}

VansSerializedValue VansNavigationSettingsJsonCodec::Encode(const VansNavigationSettings& settings)
{
    std::vector<VansSerializedValue> profiles, maps;
    for (const auto& profile : settings.profiles)
    {
        std::vector<std::pair<std::string, VansSerializedValue>> fields{ { "id", VansSerializedValue::String(profile.id) } };
        for (const auto& field : ProfileFloatFields) fields.emplace_back(field.name, VansSerializedValue::Float(profile.*field.member));
        profiles.push_back(VansSerializedValue::Object(std::move(fields)));
    }
    for (const auto& map : settings.maps)
    {
        std::vector<VansSerializedValue> references;
        for (const auto& id : map.profiles) references.push_back(VansSerializedValue::String(id));
        std::vector<std::pair<std::string, VansSerializedValue>> fields{
            { "id", VansSerializedValue::String(map.id) }, { "profiles", VansSerializedValue::Array(std::move(references)) },
            { "updateMode", VansSerializedValue::String(NavigationMapUpdateModeName(map.updateMode)) },
            { "detailSamplingEnabled", VansSerializedValue::Bool(map.detailSamplingEnabled) },
            { "originMeters", VansSerializedValue::Array({ VansSerializedValue::Float(map.originMeters.x), VansSerializedValue::Float(map.originMeters.y), VansSerializedValue::Float(map.originMeters.z) }) } };
        for (const auto& field : MapFloatFields) fields.emplace_back(field.name, VansSerializedValue::Float(map.*field.member));
        for (std::size_t field = 0; field < std::size(MapIntegerFields); ++field)
            fields.emplace_back(MapIntegerFields[field], VansSerializedValue::Int(map.*MapIntegerMembers[field]));
        maps.push_back(VansSerializedValue::Object(std::move(fields)));
    }
	std::vector<std::pair<std::string, VansSerializedValue>> query;
	for (const auto& field : QueryIntFields)
		query.emplace_back(field.name, VansSerializedValue::Int(settings.query.*field.member));
	std::vector<std::pair<std::string,VansSerializedValue>> avoidance;
    for(const auto& field:AvoidanceFloatFields) avoidance.emplace_back(field.name,VansSerializedValue::Float(settings.avoidance.*field.member));
    for(const auto& field:AvoidanceIntFields) avoidance.emplace_back(field.name,VansSerializedValue::Int(settings.avoidance.*field.member));
	std::vector<VansSerializedValue> definitions;
	for (const auto& area : settings.areas.definitions)
		definitions.push_back(VansSerializedValue::Object({
			{ "name", VansSerializedValue::String(area.name) },
			{ "id", VansSerializedValue::Int(area.id) },
			{ "traversalCost", VansSerializedValue::Float(area.traversalCost) },
			{ "traversable", VansSerializedValue::Bool(area.traversable) }
		}));
	std::vector<VansSerializedValue> filters;
	for (const auto& filter : settings.queryFilters)
	{
		std::vector<VansSerializedValue> exclusions;
		for (const auto& area : filter.excludedAreas) exclusions.push_back(VansSerializedValue::String(area));
		std::vector<std::pair<std::string, VansSerializedValue>> costs;
		for (const auto& [name, cost] : filter.areaCosts) costs.emplace_back(name, VansSerializedValue::Float(cost));
		filters.push_back(VansSerializedValue::Object({
			{ "id", VansSerializedValue::String(filter.id) },
			{ "excludedAreas", VansSerializedValue::Array(std::move(exclusions)) },
			{ "areaCosts", VansSerializedValue::Object(std::move(costs)) } }));
	}
	return VansSerializedValue::Object({
		{ "profiles", VansSerializedValue::Array(std::move(profiles)) },
        { "maps", VansSerializedValue::Array(std::move(maps)) },
        { "maximumResidentBytes", VansSerializedValue::Int(settings.maximumResidentBytes) },
        { "maximumConcurrentBuildBytes", VansSerializedValue::Int(settings.maximumConcurrentBuildBytes) },
        { "maximumResidentTiles", VansSerializedValue::Int(settings.maximumResidentTiles) },
		{ "query", VansSerializedValue::Object(std::move(query)) },
		{ "avoidance", VansSerializedValue::Object(std::move(avoidance)) },
		{ "queryFilters", VansSerializedValue::Array(std::move(filters)) },
		{ "areas", VansSerializedValue::Object({
			{ "default", VansSerializedValue::String(settings.areas.defaultArea) },
			{ "definitions", VansSerializedValue::Array(std::move(definitions)) }
		}) }
	});
}
}
