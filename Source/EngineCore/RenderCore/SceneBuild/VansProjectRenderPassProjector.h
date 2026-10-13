#pragma once
#include "../../SceneCore/VansSceneResourcePlan.h"
#include "../../SceneCore/VansSceneResourceLoadContext.h"

namespace VansGraphics
{
class VansScene;
class IVansRenderThreadTransactionExecutor;
// Main 配置验证与 RenderThread 候选准备共用一个发布入口，不重载或重建场景。
class VansProjectRenderPassProjector final
{
public:
    static bool Apply(VansScene& scene, IVansRenderThreadTransactionExecutor& executor,
        const Vans::VansSceneResourceBuildPlan& resources, Vans::VansSceneResourceLoadContext loadContext,
        std::uint32_t renderWidth, std::uint32_t renderHeight, std::string& error);
private:
    class Transaction;
};
}
