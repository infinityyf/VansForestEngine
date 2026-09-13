#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansCameraControlArbiter.h"
#include "VansSceneSurfaceQuery.h"
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
            VansCameraControlPose pose;
            // 输入读取上次实际输出的视图，不使用 BeginFrame 恢复的基础相机，
            // 不绕过 CameraCut / 相机贡献仲裁重新执行跟随脚本。
            if (!m_CameraControlArbiter || !m_CameraControlArbiter->GetLastResolvedPose(pose)) return false;
            origin = pose.position;
            const glm::vec3 angles = glm::radians(pose.rotationDegrees);
            direction = glm::normalize(glm::vec3(std::cos(angles.y) * std::cos(angles.x),
                std::sin(angles.x), std::sin(angles.y) * std::cos(angles.x)));
            return true;
        },
        [this](const glm::vec3& origin, const glm::vec3& direction, float range, uint32_t layers,
            Vans::VansEntityHandle owner, Vans::VansEntityHandle instigator, Vans::VansSurfaceImpact& impact, std::string& error) {
            if (!m_CombatSurfaces) { error = "Scene surface query is not prepared"; return false; }
            m_CombatSurfaces->Raycast(origin, direction, range, layers, owner, instigator, impact);
            return true;
        },
        [this](Vans::VansEntityHandle owner) { return m_CombatSurfaces && m_CombatSurfaces->HasPreciseCollider(owner); }
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
