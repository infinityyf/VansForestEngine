#pragma once
#include "../../AssetCore/Serialization/VansSerializedValue.h"
#include "EngineDTOs.h"
#include <string>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>

namespace Vans::EditorAPI
{
struct NavigationAuthoringChoice
{
    VansSerializedValue value;
    std::string label;
    VansSerializedValue componentFields;
};
struct NavigationAuthoringField
{
    std::string componentType;
    std::string field;
    std::string mapId;
    std::vector<NavigationAuthoringChoice> choices;
    VansSerializedValue arrayElement;
    std::vector<std::string> replacedFields;
};
struct NavigationAuthoringSchema
{
    // 项目目录和场景稳定身份；编辑器不持有领域对象或原生查询对象。
    std::vector<NavigationAuthoringField> fields;
    std::string runtimeDiagnostic;
    std::string obstacleCapabilities;
};
struct NavigationComponentValidation
{
    bool valid=false;
    std::string error;
};
struct NavigationSettingsArrayTemplate
{
    std::string path;
    VansSerializedValue element;
    std::size_t maximumItems=0;
};
struct NavigationSettingsChoiceField
{
    std::string path;
    std::vector<NavigationAuthoringChoice> choices;
};
struct NavigationSettingsSnapshot
{
    bool available=false;
    bool editable=false;
    std::string projectRoot;
    VansSerializedValue document;
    std::vector<NavigationSettingsArrayTemplate> arrays;
    std::vector<NavigationSettingsChoiceField> choiceFields;
    std::string runtimeDiagnostic;
};
struct NavigationPreviewProfile
{
    std::string id;
    std::size_t tileCount=0;
};
struct NavigationBuildPreviewSnapshot
{
    bool available=false,editable=false,busy=false,ready=false;
    std::uint64_t requestId=0;
    std::string state="Empty",mapId,diagnostic,suggestedOutputPath;
    std::size_t capturedSources=0,capturedSurfaces=0,payloadBytes=0,residentBytes=0;
    double elapsedMilliseconds=0;
    std::vector<NavigationPreviewProfile> profiles;
    std::vector<std::string> filters;
    struct Exclusion { std::string id,reason; std::uint64_t instanceCount=0; };
    std::vector<Exclusion> exclusions;
};
struct NavigationPreviewView
{
    std::uint64_t previewId=0;
    std::string profileId,filterId;
    std::array<Vec4,6> planes{};
    std::size_t maximumTiles=64,maximumTriangles=8192;
    std::size_t maximumLinks=128;
};
struct NavigationPreviewTriangle
{
    std::array<Vec3,3> positions;
    std::array<bool,3> tileEdges{};
    std::uint8_t area=0;
    bool traversable=false;
};
struct NavigationPreviewLink
{
    std::uint32_t identity=0;
    std::array<Vec3,2> positions;
    float radiusMeters=0;
    bool bidirectional=false,areaAllowed=false;
};
struct NavigationPreviewGeometry
{
    bool available=false,limited=false;
    std::string diagnostic;
    std::vector<NavigationPreviewTriangle> triangles;
    std::vector<NavigationPreviewLink> links;
    std::size_t tileCount=0;
};
enum class NavigationPreviewQueryKind : std::uint8_t { Path,PointProjection,Raycast,NearestBoundary,ReachablePoint };
struct NavigationPreviewQueryRequest
{
    std::uint64_t previewId=0;
    NavigationPreviewQueryKind kind=NavigationPreviewQueryKind::Path;
    std::string profileId,filterId;
    Vec3 start,goal,nearestExtents{1,2,1};
    float radius=4,goalProjectionToleranceMeters=.05f;
    std::uint32_t seed=0;
    int maximumAttempts=12;
    bool requireRequestedGoal=false;
};
struct NavigationPreviewQuerySnapshot
{
    bool available=false,busy=false,hasProjection=false,hasResult=false,hit=false;
    NavigationPreviewQueryKind kind=NavigationPreviewQueryKind::Path;
    std::uint64_t previewId=0,queryId=0;
    std::string state,profileId,filterId,result,failure,diagnostic;
    Vec3 requestedStart,requestedEnd,projectedStart,projectedEnd,reachedEnd,normal;
    float startProjectionDistance=0,endProjectionDistance=0,hitFraction=1,boundaryDistanceMeters=0;
    std::uint32_t iterations=0,attempts=0;
    std::size_t tileCount=0;
    std::vector<Vec3> points;
};
class INavigationEditorAPI
{
public:
    virtual ~INavigationEditorAPI()=default;
    virtual NavigationAuthoringSchema GetNavigationAuthoringSchema() const=0;
    // 整组数据通过领域 reader 和当前目录校验后才能进入作者事务。
    virtual NavigationComponentValidation ValidateNavigationComponent(
        const std::string& type,const VansSerializedValue& data) const=0;
    virtual NavigationSettingsSnapshot GetNavigationSettings() const=0;
    virtual NavigationComponentValidation ValidateNavigationSettingsDocument(const VansSerializedValue& document) const=0;
    virtual NavigationComponentValidation ApplyNavigationSettings(const VansSerializedValue& document,
        const VansSerializedValue& expectedDocument)=0;
    virtual NavigationComponentValidation BuildNavigationPreview(const std::string& mapId)=0;
    virtual NavigationBuildPreviewSnapshot GetNavigationBuildPreview()=0;
    virtual void CancelNavigationPreview()=0;
    virtual NavigationPreviewGeometry GetNavigationPreviewGeometry(const NavigationPreviewView& view)=0;
    virtual NavigationComponentValidation SubmitNavigationPreviewQuery(const NavigationPreviewQueryRequest& request)=0;
    virtual NavigationPreviewQuerySnapshot GetNavigationPreviewQuery()=0;
    virtual void CancelNavigationPreviewQuery()=0;
    virtual NavigationComponentValidation BakeNavigationPreview(const std::string& projectRelativeOutputPath)=0;
};
}
