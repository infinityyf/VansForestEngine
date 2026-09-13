#include "VansSceneParticleComponentBuilder.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
VansScriptParticleComponent* VansSceneParticleComponentBuilder::BuildParticle(
    VansScene& scene, VansScriptObject& object,
    const Vans::VansSceneParticleComponentConfig& config, bool hasTransform,
    const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale)
{
    if (config.assetGuid.empty()) return nullptr;
    auto component = std::make_unique<VansScriptParticleComponent>();
    component->m_PlayOnAwake = config.playOnAwake;
    component->m_Manager = &scene.GetParticleManager();
    if (!component->LoadAssetGuid(config.assetGuid))
    { VANS_LOG_ERROR("[Particle] Memory asset is unavailable: " << config.assetGuid); return nullptr; }
    // 变换属于实体；纯粒子对象也不再依赖 RenderNode 来持有变换。
    if (!object.m_OwnsTransform && !object.GetComponent<VansScriptRenderComponent>())
    {
        object.m_TransformID = VansTransformStore::AllocateTransform();
        object.m_OwnsTransform = true;
        if (hasTransform)
        {
            auto& transform = VansTransformStore::GetTransform(object.m_TransformID);
            transform.m_Position = position; transform.m_Rotation = rotation; transform.m_Scale = scale;
        }
    }
    component->GetRuntime()->SetOwnerWorldTransform(VansTransformStore::GetTransform(object.m_TransformID).GetModelMatrix());
    if (component->m_PlayOnAwake) component->Play();
    auto* result = component.release(); object.AddComponent(result);
    return result;
}
}
