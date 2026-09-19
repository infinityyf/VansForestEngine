#pragma once
#include "../Public/EngineDTOs.h"
#include <memory>

namespace Vans { class VansRuntimeWorld; }
namespace VansGraphics { class IVansRenderThreadTransactionExecutor; }
namespace Vans::EditorAPI
{
// 生命周期由 Editor API 持有；游戏运行库不链接此查询与缓存。
class VansEditorSceneQuery
{
public:
    VansEditorSceneQuery();
    ~VansEditorSceneQuery();
    EditorScenePickResult Pick(VansRuntimeWorld& world, const EditorScenePickRequest& request,
        VansGraphics::IVansRenderThreadTransactionExecutor* renderer);
    EditorSceneBounds Bounds(VansRuntimeWorld& world, const std::vector<std::string>& entityGuids,
        VansGraphics::IVansRenderThreadTransactionExecutor* renderer);
private:
    struct Cache;
    std::unique_ptr<Cache> m_Cache;
};
}
