#pragma once
#include "PcgEditorDTOs.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
enum class PcgSplineKind { Road, River };
enum class PcgSplineTangentMode { Auto, Aligned, Mirrored, Broken };
enum class PcgSplineSegmentMode { Curve, Line };
struct PcgSplinePoint
{
    std::string id;
    std::array<float,3> position{},arrive{},leave{};
    PcgSplineTangentMode tangentMode=PcgSplineTangentMode::Auto;
    PcgSplineSegmentMode outgoing=PcgSplineSegmentMode::Curve;
    float leftWidth=3,rightWidth=3,bankAngleDegrees=0,depth=2,speed=2;
    bool linkedWidth=true;
    float effectiveFlowSpeed=0;
    float bankSteepness=0;
};
struct PcgSplineItem
{
    std::string id,name,materialGuid;
    PcgSplineKind kind=PcgSplineKind::Road;
    bool enabled=true,locked=false,excludeVegetation=false;
    float vegetationFade=2;
    float waterSurfaceDrop=.15f;
    int priority=0;
    float shoulder=3,blendWidth=1,surfaceOffset=.025f,textureRepeat=4;
    float flowSign=1,fadeInDistance=5,fadeOutDistance=5,flowCycleSeconds=2;
    bool normalFlowEnabled=true;
    std::vector<PcgSplinePoint> points;
};
struct PcgSplineGuide
{
    std::string id;
    std::vector<std::array<float,3>> center,left,right;
    std::vector<std::array<float,3>> flowOrigins,flowVectors;
};
struct PcgSplineSnapshot
{
    bool available=false,editable=false,dirty=false,canUndo=false,canRedo=false,building=false,toolEnabled=false;
    std::uint64_t documentState=0;
    std::string assetGuid,terrainGuid,selectedSpline,selectedPoint,message;
    float fieldTexelSize=.5f,sampleSpacing=.5f,curveTolerance=.025f,heightConflictThreshold=1;
    std::vector<PcgSplineItem> splines;
    std::vector<PcgSplineGuide> guides;
    std::vector<std::string> warnings;
    std::vector<std::array<float,3>> uncoveredBankPoints;
    std::size_t activeTiles=0,domainTiles=0,rebuiltTiles=0;
};
enum class PcgSplineEditPhase { Apply, Begin, Update, Commit, Cancel };
struct PcgSplineEditRequest
{
    std::uint64_t documentState=0;
    PcgSplineEditPhase phase=PcgSplineEditPhase::Apply;
    PcgSplineItem spline;
};
enum class PcgSplineCommand { AddRoad,AddRiver,Duplicate,Remove,Reverse,InsertPoint,RemovePoint,Undo,Redo,Save,Bake,Focus,ConfigureFields };
struct PcgSplineCommandRequest
{
    PcgSplineCommand command=PcgSplineCommand::AddRoad;
    std::string splineId,pointId,materialGuid;
    std::uint32_t segment=0;
    float parameter=.5f;
    std::array<float,3> position{};
    std::array<float,4> fieldSettings{.5f,.5f,.025f,1.f};
};
}
