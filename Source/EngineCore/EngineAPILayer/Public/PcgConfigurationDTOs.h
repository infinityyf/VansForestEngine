#pragma once
#include "ModelLodDTOs.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Vans::EditorAPI
{
enum class PcgGeometry { Unassigned, Mesh, ProceduralBlade };
enum class PcgPartKind { Surface, Trunk, Leaves };
struct PcgPlantPart
{
    std::string id, mesh, material;
    PcgPartKind kind=PcgPartKind::Surface;
    int submesh=-1;
};
struct PcgPlantVariant
{
    std::string id,name;
    PcgGeometry geometry=PcgGeometry::Unassigned;
    float weight=0,footprintRadius=0,cullingRadius=0,bladeWidth=0;
    std::array<float,3> offset{},scale{1,1,1};
    std::array<float,4> rotation{0,0,0,1};
    std::vector<PcgPlantPart> parts;
    std::vector<float> lodRatios{.5f,.18f};
    float lodMaximumError=.04f;
    std::string lodBuildKey;
    std::vector<ModelLodLevel> lodLevels;
};
enum class PcgConfigurationFieldKind { Float, Unsigned, Boolean, Float2, Float3, FloatList };
enum class PcgConfigurationFieldVisibility { Always, DensitySourceOnly };
struct PcgConfigurationField
{
    std::string name,label;
    PcgConfigurationFieldKind kind=PcgConfigurationFieldKind::Float;
    PcgConfigurationFieldVisibility visibility=PcgConfigurationFieldVisibility::Always;
    std::vector<float> values;
    uint32_t unsignedValue=0;
    bool boolValue=false;
    float editorSpeed=.01f,minimum=0,maximum=0;
    bool hasMinimum=false,hasMaximum=false,editorConstrained=false;
    uint32_t minimumCount=0,maximumCount=0,editorOrder=0;
};
inline PcgConfigurationField* FindPcgConfigurationField(
    std::vector<PcgConfigurationField>& fields,std::string_view name)
{
    for(auto& field:fields) if(field.name==name) return &field;
    return nullptr;
}
inline const PcgConfigurationField* FindPcgConfigurationField(
    const std::vector<PcgConfigurationField>& fields,std::string_view name)
{
    for(const auto& field:fields) if(field.name==name) return &field;
    return nullptr;
}
struct PcgPlantConfiguration
{
    bool available=false,tree=false,dirty=false,canUndo=false,canRedo=false;
    std::string guid,name,message;
    uint64_t documentState=0;
    std::vector<PcgPlantVariant> variants;
    std::vector<PcgConfigurationField> grassFields,renderFields;
};
enum class PcgSourceMode { Density, Count, Fixed };
enum class PcgSurfaceKind { Unassigned, Plane, Terrain };
struct PcgLayerConfiguration
{
    bool available=false,dirty=false,canUndo=false,canRedo=false;
    uint64_t documentState=0;
    std::string message,name,plantGuid;
    bool enabled=false,locked=false;
    PcgSourceMode source=PcgSourceMode::Density;
    uint32_t seed=0,treeTargetCount=0;
    uint64_t maxCandidates=0,maxInstances=0;
    std::vector<PcgConfigurationField> placementFields;
    std::string regionName,terrainGuid;
    bool regionEnabled=false;
    std::array<float,2> boundsMin{},boundsMax{};
    PcgSurfaceKind surface=PcgSurfaceKind::Unassigned;
    float planeHeight=0,cellSize=0;
    uint32_t regionSeed=0;
};
enum class PcgConfigurationAction { Undo, Redo, Revert, Save };
}
