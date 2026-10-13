#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansCameraControlArbiter.h"
#include "../GeometryCore/VansSceneSurfaceQuery.h"
#include "../../GameplayActionAdapters/Combat/VansCombatActionService.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
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
        },
        {},
        [this](Vans::VansEntityHandle owner,const std::string& boneName,glm::vec3& point) {
            if (!m_RuntimeWorld) return false;
            const auto* transforms=m_RuntimeWorld->FindStorage<Vans::VansRuntimeTransformComponent>(Vans::VansRuntimeComponentType_Transform);
            const auto* transform=transforms ? transforms->Get(transforms->FindFirstOwnedBy(owner)) : nullptr;
            if (!transform) return false;
            // A view attached below this host is its eye. Otherwise use the
            // authored bone from the current gameplay pose, including crouch.
            if (m_Camera) {
                auto id=m_Camera->GetTransformID();
                for (std::size_t depth=0;id!=UINT32_MAX && depth<256;++depth) {
                    if (id==transform->transformStoreId) {
                        Vans::VansCameraViewSnapshot view;
                        if (!m_CameraControlArbiter || !m_CameraControlArbiter->GetLastResolvedView(view)) return false;
                        point=view.pose.position;return true;
                    }
                    id=GetParentTransformID(id);
                }
            }
            const auto* animations=m_RuntimeWorld->FindStorage<Vans::VansRuntimeAnimationComponent>(Vans::VansRuntimeComponentType_Animation);
            const auto* component=animations ? animations->Get(animations->FindFirstOwnedBy(owner)) : nullptr;
            auto* animation=component ? component->animationNode : nullptr;
            const auto pose=animation ? animation->GetFinalPoseView() : VansSkeletonPoseView{};
            if (!pose.IsValid() || !Vans::VansTransformStore::IsAllocated(animation->GetTransformID())) return false;
            const int bone=pose.skeleton->FindBoneIndex(boneName);
            if (bone<0) return false;
            point=glm::vec3(Vans::VansTransformStore::Read(animation->GetTransformID()).GetModelMatrix()*(*pose.modelTransforms)[bone][3]);
            return std::isfinite(glm::length(point));
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
