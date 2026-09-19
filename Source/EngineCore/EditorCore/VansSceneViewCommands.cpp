#include "VansSceneViewCommands.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include <algorithm>

namespace Vans
{
std::vector<EditorObjectHandle> VansSceneViewCommands::s_FrameSelection;
uint64_t VansSceneViewCommands::s_SelectionRevision = 0;
void VansSceneViewCommands::RequestFrameSelection()
{
    const auto& selection = VansEditorSelectionService::Get().Snapshot();
    s_FrameSelection = selection.objects;
    s_SelectionRevision = selection.revision;
}
void VansSceneViewCommands::Clear() { s_FrameSelection.clear(); }
bool VansSceneViewCommands::ConsumeFrameSelection(EditorAPI::IEngineEditorAPI& api, EditorAPI::EditorSceneBounds& bounds)
{
    auto objects = std::move(s_FrameSelection);
    s_FrameSelection.clear();
    bounds = {};
    if (objects.empty() || s_SelectionRevision != VansEditorSelectionService::Get().Snapshot().revision ||
        api.GetPlayState() != EditorAPI::EnginePlayState::Edit) return false;
    std::vector<std::string> entities;
    for (const auto& object : objects)
        if (object.domain == EditorObjectDomain::SceneEntity)
            entities.push_back(object.entityGuid.empty() ? object.guid : object.entityGuid);
    bounds = api.QueryEditorSceneBounds(entities);
    for (const auto& object : objects)
    {
        if (object.domain != EditorObjectDomain::SceneSubObject ||
            (object.subObjectKind != SceneSubObjectKind::Bone && object.subObjectKind != SceneSubObjectKind::Socket)) continue;
        EditorAPI::SceneSkeletonNodePoseRequest request;
        request.entityGuid = object.entityGuid; request.animationComponentGuid = object.componentGuid;
        request.anchorGuid = object.subObjectGuid;
        request.kind = object.subObjectKind == SceneSubObjectKind::Socket
            ? EditorAPI::SceneSkeletonNodeKind::Socket : EditorAPI::SceneSkeletonNodeKind::Bone;
        const auto pose = api.GetSceneSkeletonNodePose(request);
        if (!pose.available || !pose.worldTransform.available) continue;
        const auto p = pose.worldTransform.position;
        if (!bounds.available) { bounds.available = true; bounds.minimum = bounds.maximum = p; }
        else
        {
            bounds.minimum = {std::min(bounds.minimum.x, p.x), std::min(bounds.minimum.y, p.y), std::min(bounds.minimum.z, p.z)};
            bounds.maximum = {std::max(bounds.maximum.x, p.x), std::max(bounds.maximum.y, p.y), std::max(bounds.maximum.z, p.z)};
        }
    }
    return bounds.available;
}
}
