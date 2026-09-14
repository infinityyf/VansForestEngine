#pragma once
#include <array>
#include <cstdint>
#include <string>
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
};
struct PcgGrassSettings
{
    uint32_t boneCount=0,subBladeCount=1;
    uint32_t scatterSeed=0;
    float restTipBendDegrees=0,restRootBendDegrees=0;
    float bladeHeight=0,leanDeviation=0,scatterRadiusMin=0,scatterRadiusMax=0;
    std::array<float,2> windDirection{};
    float windStrength=0,windFrequency=0,windSpeed=0,windBendMultiplier=0;
    float stiffness=0,damping=0,softness=0,simulationFullDistance=0,simulationFadeDistance=0;
    float subBladeLodMidDistance=0,subBladeLodFarDistance=0;
};
struct PcgRenderSettings
{
    bool cullingEnabled=false,hizEnabled=false,castShadows=false;
    float cullDistance=0,hizBias=0;
};
struct PcgPlantConfiguration
{
    bool available=false,tree=false,dirty=false,canUndo=false,canRedo=false;
    std::string guid,name,message;
    uint64_t documentState=0;
    std::vector<PcgPlantVariant> variants;
    PcgGrassSettings grass;
    PcgRenderSettings render;
};
struct PcgPlacementSettings
{
    float density=0,positionJitter=0,minimumSpacing=0;
    std::array<float,3> scaleMin{1,1,1},scaleMax{1,1,1};
    bool uniformScale=true;
    float yawMinDegrees=0,yawMaxDegrees=0,normalAlignment=0,maximumTiltDegrees=0,rootOffset=0;
    float maskThreshold=0,maskMultiplier=1;
    bool invertMask=false;
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
    PcgPlacementSettings placement;
    std::string regionName,terrainGuid;
    bool regionEnabled=false;
    std::array<float,2> boundsMin{},boundsMax{};
    PcgSurfaceKind surface=PcgSurfaceKind::Unassigned;
    float planeHeight=0,cellSize=0;
    uint32_t regionSeed=0;
};
enum class PcgConfigurationAction { Undo, Redo, Revert, Save };
}
