#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansCameraControlArbiter.h"
#include "../GeometryCore/VansSceneSurfaceQuery.h"
#include "../../GameplayActionAdapters/Combat/VansCombatActionService.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../Util/VansLog.h"
#include <cmath>

namespace VansGraphics
{
Vans::VansCombatSceneBackend VansScene::MakeCombatSceneBackend()
{
    return {
        [this](glm::vec3& origin, glm::vec3& direction) {
            if (!m_Camera || m_LoadMode != VansSceneLoadMode::Runtime) return false;
            Vans::VansCameraViewSnapshot view;
            // 输入读取上次实际输出的视图，不使用 BeginFrame 恢复的基础相机，
            // 不绕过 CameraCut / 相机贡献仲裁重新执行跟随脚本。
            if (!m_CameraControlArbiter || !m_CameraControlArbiter->GetLastResolvedView(view)) return false;
            origin = view.pose.position;
            const glm::vec3 angles = glm::radians(view.pose.rotationDegrees);
            direction = glm::normalize(glm::vec3(std::cos(angles.y) * std::cos(angles.x),
                std::sin(angles.x), std::sin(angles.y) * std::cos(angles.x)));
            return true;
        },
        [this](const Vans::VansSurfaceQueryRequest& request,
            Vans::VansSurfaceImpact& impact, std::string& error) {
            if (!m_CombatSurfaces) { error = "Scene surface query is not prepared"; return false; }
            return m_CombatSurfaces->Query(request, impact, error);
        }
    };
}

bool VansScene::PrepareCombatSurfaces(VansVKDevice& device)
{
    if (m_LoadMode != VansSceneLoadMode::Runtime || !m_RuntimeWorld || !m_GameplayRuntime ||
        !m_GameplayRuntime->Services().Resolve(Vans::VansCombatActionCapability().service)) return true;
    auto surfaces = std::make_unique<VansSceneSurfaceQuery>(*m_RuntimeWorld);
    std::string error;
    if (!surfaces->Prepare(device, error)) { VANS_LOG_ERROR("[Combat] " << error); return false; }
    m_CombatSurfaces = std::move(surfaces);
    return true;
}
}
