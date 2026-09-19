#include "EngineAPIImpl.h"
#include "VansEditorSceneQuery.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderSystem.h"

namespace Vans::EditorAPI
{
EditorScenePickResult EngineAPIImpl::PickEditorScene(const EditorScenePickRequest& request)
{
    auto* scene = static_cast<VansScene*>(m_Scene);
    auto* world = scene ? scene->GetRuntimeWorld() : nullptr;
    if (!world || !scene->IsSceneReady()) return {false, {}, "Scene is not ready for picking."};
    if (!m_EditorSceneQueryCache) m_EditorSceneQueryCache = std::make_shared<VansEditorSceneQuery>();
    return m_EditorSceneQueryCache->Pick(*world, request, m_RenderSystem);
}
EditorSceneBounds EngineAPIImpl::QueryEditorSceneBounds(const std::vector<std::string>& entityGuids)
{
    auto* scene = static_cast<VansScene*>(m_Scene);
    auto* world = scene ? scene->GetRuntimeWorld() : nullptr;
    if (!world || !scene->IsSceneReady()) return {};
    if (!m_EditorSceneQueryCache) m_EditorSceneQueryCache = std::make_shared<VansEditorSceneQuery>();
    return m_EditorSceneQueryCache->Bounds(*world, entityGuids, m_RenderSystem);
}
}
