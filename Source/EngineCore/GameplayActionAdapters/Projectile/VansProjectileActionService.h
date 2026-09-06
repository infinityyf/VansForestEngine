#pragma once
#include "../../GameplayActionCore/VansActionServices.h"
#include "../../RuntimeCore/VansGenerationPool.h"
#include "../../SceneCore/VansSceneParticleComponentConfig.h"
#include <glm/glm.hpp>
#include <functional>
#include <optional>

namespace Vans
{
class VansRuntimeWorld;
class VansGameplayRuntime;
struct VansProjectileSpawnRequest
{
    VansEntityHandle owner;
    std::string source;
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    float mass = 0.4f;
    float restitution = 0.25f;
    float friction = 0.6f;
    std::string restitutionCombine = "Average";
    std::string frictionCombine = "Average";
    std::string collisionLayer = "Default";
    std::optional<VansSceneParticleComponentConfig> particle;
};
struct VansProjectileSceneBackend
{
    std::function<VansEntityHandle(const VansProjectileSpawnRequest&, std::string&)> spawn;
    std::function<bool(VansEntityHandle)> destroy;
    std::function<bool(VansEntityHandle, const std::string&, const std::string&, const std::string&, std::string&)> bindSocket;
};

// 物理和渲染实体由 Scene 的正式结构变更入口构建；GAF 只管理命令和资源寿命。
class VansProjectileActionService final : public IVansActionService
{
public:
    VansProjectileActionService(VansRuntimeWorld& world, VansGameplayRuntime& gameplay, VansProjectileSceneBackend backend);
    const VansActionServiceCapability& Capability() const override;
    VansActionCommandResult Execute(const VansActionCommand& command) override;
    bool Release(VansGenerationHandle resource, std::string& error) override;
    void Tick(double deltaSeconds) override;
private:
    struct Projectile { VansEntityHandle entity; std::optional<double> remainingSeconds; bool justSpawned = true; };
    VansRuntimeWorld& m_World;
    VansGameplayRuntime& m_Gameplay;
    VansProjectileSceneBackend m_Backend;
    VansGenerationPool<Projectile> m_Projectiles;
};
const VansActionServiceCapability& VansAttachmentActionCapability();
std::shared_ptr<IVansActionService> VansCreateAttachmentActionService(VansProjectileSceneBackend backend);
}
