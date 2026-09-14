#pragma once
#include "PcgConfigurationDTOs.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace Vans::EditorAPI {
struct PcgLayerSnapshot {
 std::string recipeGuid, recipeName, regionId, regionName, layerId, name;
 std::string plantGuid, plantName, densityMaskGuid, exclusionMaskGuid, source;
 bool tree=false, enabled=false, locked=false;
 float density=0;
 uint32_t treeTargetCount=0;
 std::array<float,2> boundsMin{}, boundsMax{};
 size_t variantCount=0, fixedCount=0, addedCount=0;
};
struct PcgEditorSnapshot { std::vector<PcgLayerSnapshot> layers; std::vector<std::string> errors; std::string sceneRecipeGuid; };
struct PcgMaskPreviewSnapshot {
 bool available=false;
 uint32_t width=0,height=0,previewWidth=0,previewHeight=0;
 std::array<float,2> boundsMin{},boundsMax{};
 std::vector<uint8_t> pixels;
 std::string error;
};

struct PcgBrushTarget
{
 std::string recipeGuid, regionId, layerId, maskGuid;
 bool operator==(const PcgBrushTarget& other) const {
  return recipeGuid==other.recipeGuid && regionId==other.regionId &&
   layerId==other.layerId && maskGuid==other.maskGuid;
 }
};
enum class PcgBrushOperation { Add, Subtract, Set, Smooth, Erase };
struct PcgBrushSettings
{
 PcgBrushOperation operation=PcgBrushOperation::Add;
 float radius=0, strength=0, hardness=0, targetValue=0, spacingFraction=0;
};
struct PcgBrushSnapshot
{
 PcgBrushTarget target;
 PcgBrushSettings settings;
 bool available=false, enabled=false, editable=false, strokeActive=false;
 bool canvasEditable=false, canvasStrokeActive=false;
 bool dirty=false, canUndo=false, canRedo=false;
 std::string message;
};
struct PcgEditorOperationResult { bool success=false; std::string message; };
struct PcgLayerCreateRequest
{
 std::string recipeGuid, recipeName, regionId, regionName, name;
 bool tree=false;
 std::array<float,2> boundsMin{}, boundsMax{};
 float cellSize=0;
 uint32_t maskWidth=0, maskHeight=0;
 // 复制配置时保留外观和独立像素；不复制依赖原稳定点身份的实例覆盖。
 PcgBrushTarget copyFrom;
};
struct PcgLayerCreateResult : PcgEditorOperationResult { PcgBrushTarget target; };
enum class PcgBrushPhase { Hover, Begin, Update, Break, End, Cancel };
enum class PcgBrushSpace { Scene, MaskCanvas };
struct PcgBrushInput
{
 PcgBrushPhase phase=PcgBrushPhase::Hover;
 PcgBrushSpace space=PcgBrushSpace::Scene;
 std::array<float,2> maskUV{};
 PcgBrushTarget target;
 std::array<float,3> rayOrigin{}, rayDirection{};
 bool erase=false;
};
struct PcgBrushResult : PcgEditorOperationResult
{
 bool hit=false, strokeActive=false;
 std::array<float,3> position{}, normal{0,1,0};
 std::vector<std::array<float,3>> ring;
};
enum class PcgMaskDocumentAction { Undo, Redo, Revert, Save };
enum class PcgMaskDataAction { Fill, Import, Remap, Export };
struct PcgMaskDataRequest {
 PcgBrushTarget target;
 PcgMaskDataAction action=PcgMaskDataAction::Fill;
 float fill=0;
 std::string path;
 uint32_t channel=0,width=0,height=0;
 std::array<float,2> boundsMin{},boundsMax{};
 bool preserveWorld=true;
};
enum class PcgInstanceOrigin { Generated, Fixed, Added, Override };
struct PcgInstanceItem {
 std::string id,authoredId,variant;
 PcgInstanceOrigin origin=PcgInstanceOrigin::Generated;
 bool locked=false,removed=false,orphan=false;
 std::array<float,3> position{},scale{1,1,1};
 std::array<float,4> rotation{0,0,0,1};
};
struct PcgInstanceSnapshot : PcgEditorOperationResult {
 uint64_t documentState=0,total=0,offset=0;
 std::vector<PcgInstanceItem> items;
};
enum class PcgInstanceAction { Add, AddFixed, Transform, Remove, Lock, ResetOverride };
struct PcgInstanceEditRequest {
 PcgBrushTarget target;
 uint64_t documentState=0;
 PcgInstanceAction action=PcgInstanceAction::Transform;
 PcgInstanceItem instance;
};

}
