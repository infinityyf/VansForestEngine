#include "VansSceneNavigationComponentReader.h"
#include "VansSceneObjectBuildPlan.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/VansAssetGuid.h"
#include <algorithm>
#include <set>
#include <limits>

namespace Vans
{
namespace
{
using Value=VansSerializedValue;
bool Fields(const Value& data,std::initializer_list<const char*> required,std::string& error)
{
	if(data.kind!=Value::Kind::Object || data.objectFields.size()!=required.size()) { error="Navigation component requires exactly its current data fields"; return false; }
	std::set<std::string> seen;
	for(const auto& [name,value]:data.objectFields)
		if(!seen.insert(name).second || std::none_of(required.begin(),required.end(),[&](const char* key){return name==key;}))
		{ error="Navigation component has an unknown or duplicate field: "+name; return false; }
	return true;
}
bool Text(const Value& data,const char* key,std::string& value,bool allowEmpty=false)
{
	const auto* field=FindObjectField(data,key);
	if(!field || field->kind!=Value::Kind::String || field->stringValue.size()>256 || (!allowEmpty && field->stringValue.empty()) ||
		std::any_of(field->stringValue.begin(),field->stringValue.end(),[](unsigned char c){return c<32 || c==127;})) return false;
	value=field->stringValue; return true;
}
bool Number(const Value& data,const char* key,float& output)
{
	const auto* field=FindObjectField(data,key);
	if(!field || (field->kind!=Value::Kind::Float && field->kind!=Value::Kind::Int)) return false;
	const double value=field->kind==Value::Kind::Float?field->floatValue:static_cast<double>(field->intValue);
	if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)()) return false;
	output=static_cast<float>(value); return true;
}
bool Boolean(const Value& data,const char* key,bool& output)
{
    const auto* field=FindObjectField(data,key);
    if(!field || field->kind!=Value::Kind::Bool) return false;
    output=field->boolValue; return true;
}
bool Vector(const Value& data,const char* key,glm::vec3& value)
{
	const auto* field=FindObjectField(data,key);
	if(!field || field->kind!=Value::Kind::Array || field->arrayItems.size()!=3) return false;
	for(int i=0;i<3;++i)
	{
		const auto& item=field->arrayItems[i];
		if(item.kind!=Value::Kind::Float && item.kind!=Value::Kind::Int) return false;
		const double number=item.kind==Value::Kind::Float?item.floatValue:static_cast<double>(item.intValue);
		if(!std::isfinite(number) || std::abs(number)>(std::numeric_limits<float>::max)()) return false;
		value[i]=static_cast<float>(number);
	}
	return true;
}
bool Strings(const Value& data,const char* key,std::vector<std::string>& output,std::size_t capacity)
{
	const auto* field=FindObjectField(data,key);
	if(!field || field->kind!=Value::Kind::Array || field->arrayItems.size()>capacity) return false;
	std::set<std::string> seen;
	std::vector<std::string> candidate;
	for(const auto& item:field->arrayItems)
	{
		if(item.kind!=Value::Kind::String || item.stringValue.empty() || item.stringValue.size()>256 || !seen.insert(item.stringValue).second) return false;
		candidate.push_back(item.stringValue);
	}
	output=std::move(candidate); return true;
}
bool ComponentReference(const Value& item,const char* type,std::string& entityGuid,std::string& componentGuid,std::string& error)
{
    if(!Fields(item,{"domain","guid","entityGuid","componentGuid","componentType"},error)) return false;
    for(const auto* key:{"domain","guid","entityGuid","componentGuid","componentType"})
    { std::string text; if(!Text(item,key,text)) return false; }
    SerializedObjectReferenceValue reference; VansAssetGuid entity,component;
    if(!TryReadSerializedObjectReference(item,reference) || reference.domain!="SceneComponent" || reference.componentType!=type ||
        reference.guid!=reference.componentGuid || !VansAssetGuid::TryParse(reference.entityGuid,entity) ||
        !VansAssetGuid::TryParse(reference.componentGuid,component)) return false;
    entityGuid=reference.entityGuid; componentGuid=reference.componentGuid; return true;
}
bool Sources(const Value& data,std::vector<VansNavigationSourceReference>& output,std::string& error)
{
	const auto* field=FindObjectField(data,"sources");
	if(!field || field->kind!=Value::Kind::Array || field->arrayItems.size()>65536) return false;
	std::set<std::string> seen;
	for(const auto& item:field->arrayItems)
	{
        VansNavigationSourceReference reference;
        if(!ComponentReference(item,"NavigationSource",reference.entityGuid,reference.componentGuid,error) ||
            !seen.insert(reference.componentGuid).second) return false;
        output.push_back(std::move(reference));
	}
	return true;
}
bool Asset(const Value& data,const char* key,std::string& output)
{
	const auto* field=FindObjectField(data,key);
	if(!field || field->kind!=Value::Kind::Object || field->objectFields.size()!=1 || !Text(*field,"guid",output,true)) return false;
	VansAssetGuid guid; return output.empty() || VansAssetGuid::TryParse(output,guid);
}
template<class Config> bool Component(const Value& component,std::optional<Config>& output,std::string& error)
{
	Config candidate;
	const auto* data=FindObjectField(component,"data");
	if(!data || !VansSceneNavigationComponentReader::Read(*data,candidate.runtime,error)) return false;
	const auto* enabled=FindObjectField(component,"enabled");
	if(!enabled || enabled->kind!=Value::Kind::Bool) { error="Navigation component requires enabled boolean"; return false; }
	candidate.enabled=enabled->boolValue; output=std::move(candidate); return true;
}
bool Fail(std::string& error) { if(error.empty()) error="Navigation component contains invalid field types, references or values"; return false; }
}
bool VansSceneNavigationComponentReader::Read(const Value& data,VansRuntimeNavigationSurfaceComponent& output,std::string& error)
{
	VansRuntimeNavigationSurfaceComponent candidate; std::string collection,geometryMode;
	if(!Fields(data,{"mapId","profiles","bounds","collection","sources","collisionLayers","geometryMode","navigationMesh"},error) ||
		!Text(data,"mapId",candidate.mapId) || !Strings(data,"profiles",candidate.profiles,16) || candidate.profiles.empty() ||
		!Text(data,"collection",collection) || !Text(data,"geometryMode",geometryMode) || geometryMode!="Collision" ||
		!Sources(data,candidate.sources,error) || !Strings(data,"collisionLayers",candidate.collisionLayers,32) ||
		!Asset(data,"navigationMesh",candidate.navigationMeshGuid)) return Fail(error);
	const auto* bounds=FindObjectField(data,"bounds");
	if(!bounds || !Fields(*bounds,{"centerMeters","halfExtentsMeters"},error) || !Vector(*bounds,"centerMeters",candidate.boundsCenterMeters) ||
		!Vector(*bounds,"halfExtentsMeters",candidate.boundsHalfExtentsMeters) || glm::any(glm::lessThanEqual(candidate.boundsHalfExtentsMeters,glm::vec3(0)))) return Fail(error);
	if(collection=="All") candidate.collection=VansNavigationCollection::All;
	else if(collection=="Hierarchy") candidate.collection=VansNavigationCollection::Hierarchy;
	else if(collection=="ExplicitSources") candidate.collection=VansNavigationCollection::ExplicitSources;
	else return Fail(error);
	if(candidate.collection==VansNavigationCollection::ExplicitSources?candidate.sources.empty():!candidate.sources.empty()) return Fail(error);
	output=std::move(candidate); error.clear(); return true;
}
bool VansSceneNavigationComponentReader::Read(const Value& data,VansRuntimeNavigationSourceComponent& output,std::string& error)
{
	VansRuntimeNavigationSourceComponent candidate; std::string mode;
	if(!Fields(data,{"mode","area","cpuMesh"},error) || !Text(data,"mode",mode) || !Text(data,"area",candidate.area,true) || !Asset(data,"cpuMesh",candidate.cpuMeshGuid)) return Fail(error);
	if(mode=="Include") candidate.mode=VansNavigationSourceMode::Include;
	else if(mode=="Exclude") candidate.mode=VansNavigationSourceMode::Exclude;
	else if(mode=="Area") candidate.mode=VansNavigationSourceMode::Area;
	else return Fail(error);
	if((candidate.mode==VansNavigationSourceMode::Area && candidate.area.empty()) ||
		(candidate.mode!=VansNavigationSourceMode::Include && !candidate.cpuMeshGuid.empty())) return Fail(error);
	output=std::move(candidate); error.clear(); return true;
}
bool VansSceneNavigationComponentReader::Read(const Value& data,VansRuntimeNavigationModifierComponent& output,std::string& error)
{
	VansRuntimeNavigationModifierComponent candidate; std::string shape;
	if(!Fields(data,{"mapId","profiles","shape","centerMeters","halfExtentsMeters","convexVerticesMeters","minimumHeightMeters","maximumHeightMeters","area","exclude","priority"},error) ||
		!Text(data,"mapId",candidate.mapId) || !Strings(data,"profiles",candidate.profiles,16) || candidate.profiles.empty() || !Text(data,"shape",shape) ||
		!Vector(data,"centerMeters",candidate.centerMeters) || !Vector(data,"halfExtentsMeters",candidate.halfExtentsMeters) ||
		glm::any(glm::lessThanEqual(candidate.halfExtentsMeters,glm::vec3(0))) || !Number(data,"minimumHeightMeters",candidate.minimumHeightMeters) ||
		!Number(data,"maximumHeightMeters",candidate.maximumHeightMeters) || candidate.minimumHeightMeters>=candidate.maximumHeightMeters || !Text(data,"area",candidate.area,true)) return Fail(error);
	const auto* exclude=FindObjectField(data,"exclude"); const auto* priority=FindObjectField(data,"priority"); const auto* vertices=FindObjectField(data,"convexVerticesMeters");
	if(!exclude || exclude->kind!=Value::Kind::Bool || !priority || priority->kind!=Value::Kind::Int || (priority->intValue < -1000000 || priority->intValue > 1000000) ||
		!vertices || vertices->kind!=Value::Kind::Array || vertices->arrayItems.size()>64) return Fail(error);
	candidate.exclude=exclude->boolValue; candidate.priority=static_cast<int>(priority->intValue);
	if(!candidate.exclude && candidate.area.empty()) return Fail(error);
	for(const auto& vertex:vertices->arrayItems)
	{
		if(vertex.kind!=Value::Kind::Array || vertex.arrayItems.size()!=2) return Fail(error);
		glm::vec2 point;
		for(int i=0;i<2;++i)
		{
			const auto& item=vertex.arrayItems[i]; if(item.kind!=Value::Kind::Float && item.kind!=Value::Kind::Int) return Fail(error);
			const double value=item.kind==Value::Kind::Float?item.floatValue:static_cast<double>(item.intValue);
			if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)()) return Fail(error);
			point[i]=static_cast<float>(value);
		}
		candidate.convexVerticesMeters.push_back(point);
	}
	if(shape=="Box" && candidate.convexVerticesMeters.empty()) candidate.shape=VansNavigationModifierShape::Box;
	else if(shape=="Convex" && candidate.convexVerticesMeters.size()>=3) candidate.shape=VansNavigationModifierShape::Convex;
	else return Fail(error);
	output=std::move(candidate); error.clear(); return true;
}
bool VansSceneNavigationComponentReader::Read(const Value& data,VansRuntimeNavigationObstacleComponent& output,std::string& error)
{
    VansRuntimeNavigationObstacleComponent candidate; std::string shape,mode;
    if(!Text(data,"mapId",candidate.mapId) || !Strings(data,"profiles",candidate.profiles,16) || candidate.profiles.empty() ||
        !Text(data,"shape",shape) || !Text(data,"mode",mode) || !ParseNavigationObstacleMode(mode,candidate.mode)) return Fail(error);
    if(shape=="Box")
    {
        VansNavigationObstacleBox box;
        if(!Fields(data,{"mapId","profiles","mode","shape","centerMeters","halfExtentsMeters"},error) ||
            !Vector(data,"centerMeters",box.centerMeters) || !Vector(data,"halfExtentsMeters",box.halfExtentsMeters) ||
            glm::any(glm::lessThanEqual(box.halfExtentsMeters,glm::vec3(0)))) return Fail(error);
        candidate.shape=box;
    }
    else if(shape=="Cylinder")
    {
        VansNavigationObstacleCylinder cylinder;
        if(!Fields(data,{"mapId","profiles","mode","shape","centerMeters","radiusMeters","heightMeters"},error) ||
            !Vector(data,"centerMeters",cylinder.centerMeters) || !Number(data,"radiusMeters",cylinder.radiusMeters) || cylinder.radiusMeters<=0 ||
            !Number(data,"heightMeters",cylinder.heightMeters) || cylinder.heightMeters<=0) return Fail(error);
        candidate.shape=cylinder;
    }
    else if(shape=="Collider")
    {
        VansNavigationObstacleCollider collider;
        const auto* field=FindObjectField(data,"collider");
        if(!Fields(data,{"mapId","profiles","mode","shape","collider"},error) || !field ||
            !ComponentReference(*field,"Physics",collider.entityGuid,collider.componentGuid,error)) return Fail(error);
        candidate.shape=std::move(collider);
    }
    else return Fail(error);
    output=std::move(candidate); error.clear(); return true;
}
VansSerializedValue VansSceneNavigationComponentReader::CreateObstacleShapeData(const std::string& shape)
{
    const auto vector=[](const glm::vec3& v){return Value::Array({Value::Float(v.x),Value::Float(v.y),Value::Float(v.z)});};
    if(shape=="Box")
    {
        const VansNavigationObstacleBox box;
        return Value::Object({{"shape",Value::String(shape)},{"centerMeters",vector(box.centerMeters)},{"halfExtentsMeters",vector(box.halfExtentsMeters)}});
    }
    if(shape=="Cylinder")
    {
        const VansNavigationObstacleCylinder cylinder;
        return Value::Object({{"shape",Value::String(shape)},{"centerMeters",vector(cylinder.centerMeters)},
            {"radiusMeters",Value::Float(cylinder.radiusMeters)},{"heightMeters",Value::Float(cylinder.heightMeters)}});
    }
    if(shape=="Collider") return Value::Object({{"shape",Value::String(shape)},
        {"collider",MakeSerializedSceneComponentObjectReference({},{},"Physics")}});
    return {};
}
bool VansSceneNavigationComponentReader::Read(const Value& data,VansRuntimeNavigationLinkComponent& output,std::string& error)
{
    VansRuntimeNavigationLinkComponent candidate;
    if(!Fields(data,{"mapId","profiles","startMeters","endMeters","radiusMeters","area","bidirectional"},error) ||
        !Text(data,"mapId",candidate.mapId) || !Strings(data,"profiles",candidate.profiles,32) || candidate.profiles.empty() ||
        !Vector(data,"startMeters",candidate.startMeters) || !Vector(data,"endMeters",candidate.endMeters) || candidate.startMeters==candidate.endMeters ||
        !Number(data,"radiusMeters",candidate.radiusMeters) || candidate.radiusMeters<=0 || !Text(data,"area",candidate.area) ||
        !Boolean(data,"bidirectional",candidate.bidirectional)) return Fail(error);
    output=std::move(candidate); error.clear(); return true;
}
bool VansSceneNavigationComponentReader::Read(const Value& data,VansRuntimeNavigationAgentComponent& output,std::string& error)
{
	VansRuntimeNavigationAgentComponent candidate;
	if(!Fields(data,{"mapId","profileId","queryFilter","maxSpeed","acceleration","avoidance","repathInterval","targetMoveThreshold","forceRepathDistance"},error) ||
		!Text(data,"mapId",candidate.mapId) || !Text(data,"profileId",candidate.profileId) || !Text(data,"queryFilter",candidate.queryFilter) ||
		!Number(data,"maxSpeed",candidate.maxSpeed) || candidate.maxSpeed<=0 || !Number(data,"acceleration",candidate.acceleration) || candidate.acceleration<=0 ||
		!Number(data,"repathInterval",candidate.repathInterval) || !Number(data,"targetMoveThreshold",candidate.targetMoveThreshold) ||
		!Number(data,"forceRepathDistance",candidate.forceRepathDistance) || !Boolean(data,"avoidance",candidate.avoidance) || !ValidateNavigationAgentTiming(candidate,error)) return Fail(error);
	output=std::move(candidate); error.clear(); return true;
}
bool VansSceneNavigationComponentReader::ReadEntity(const Value& entity,VansSceneObjectBuildConfig& output,std::string& error)
{
	const auto* components=FindObjectField(entity,"components");
	if(!components || components->kind!=Value::Kind::Array) return Fail(error);
	for(const auto& component:components->arrayItems)
	{
		const auto type=ReadSerializedStringField(component,"type");
		if(type=="NavigationSurface") { if(output.navigationSurface || !Component(component,output.navigationSurface,error)) return Fail(error); }
		else if(type=="NavigationSource") { if(output.navigationSource || !Component(component,output.navigationSource,error)) return Fail(error); }
		else if(type=="NavigationModifier") { if(output.navigationModifier || !Component(component,output.navigationModifier,error)) return Fail(error); }
        else if(type=="NavigationObstacle") { if(output.navigationObstacle || !Component(component,output.navigationObstacle,error)) return Fail(error); }
        else if(type=="NavigationLink") { if(output.navigationLink || !Component(component,output.navigationLink,error)) return Fail(error); }
		else if(type=="NavigationAgent") { if(output.navigationAgent || !Component(component,output.navigationAgent,error)) return Fail(error); }
	}
	error.clear(); return true;
}
}
