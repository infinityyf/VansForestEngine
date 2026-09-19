#pragma once
#include "VansEditorSelectionService.h"

namespace Vans::EditorAPI { class IEngineEditorAPI; }
namespace Vans
{
// 窗口之间只传递取景请求，不互相持有相机或窗口指针。
class VansSceneViewCommands
{
public:
    static void RequestFrameSelection();
    static bool ConsumeFrameSelection(EditorAPI::IEngineEditorAPI& api, EditorAPI::EditorSceneBounds& bounds);
    static void Clear();
private:
    static std::vector<EditorObjectHandle> s_FrameSelection;
    static uint64_t s_SelectionRevision;
};
}
