#pragma once
#include "../../GameplayActionCore/VansActionServices.h"
#include "../../GameplayActionCore/VansGameplayServiceRuntime.h"
#include "../../RuntimeCore/VansGenerationPool.h"
#include "../../SceneCore/VansSceneParticleComponentConfig.h"
#include "../../EventCore/VansEventConnection.h"
#include <glm/glm.hpp>
#include <functional>
#include <optional>
#include <unordered_set>

namespace Vans
{
class VansRuntimeWorld;
class VansGameplayAssetLibrary;
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
    VansProjectileActionService(VansRuntimeWorld& world, IVansGameplayServiceRuntime& runtime,
        const VansGameplayAssetLibrary& assets, VansProjectileSceneBackend backend);
    const VansActionServiceCapability& Capability() const override;
    VansActionCommandResult Execute(const VansActionCommand& command) override;
    bool Release(VansGenerationHandle resource, std::string& error) override;
    void Tick(double deltaSeconds) override;
private:
    struct Projectile
    {
        VansEntityHandle entity;
        std::optional<double> remainingSeconds;
        bool justSpawned = true;
        VansEntityHandle owner, instigator;
        VansActionId detonationAction;
        std::optional<double> fuseSeconds;
        std::string detonationMode = "Timer";
        VansActionId timeoutAction;
        std::uint32_t transformId = UINT32_MAX;
        float settleSpeed = 0.15f, impactNormalMinY = 0.7071f;
        double settleSeconds = 0.2, settledFor = 0;
        bool surfaceImpact = false;
        std::unordered_set<std::uint32_t> supports;
    };
    VansRuntimeWorld& m_World;
    IVansGameplayServiceRuntime& m_Runtime;
    const VansGameplayAssetLibrary& m_Assets;
    VansProjectileSceneBackend m_Backend;
    VansGenerationPool<Projectile> m_Projectiles;
    VansEventConnection m_Contacts;
};
const VansActionServiceCapability& VansAttachmentActionCapability();
std::shared_ptr<IVansActionService> VansCreateAttachmentActionService(VansProjectileSceneBackend backend);
}
