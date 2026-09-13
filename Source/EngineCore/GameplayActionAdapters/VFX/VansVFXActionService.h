#pragma once
#include "../../GameplayActionCore/VansActionServices.h"
#include "../../RuntimeCore/VansGenerationPool.h"
#include "../../SceneCore/VansSceneParentReference.h"
#include <functional>

namespace Vans
{
class VansGameplayRuntime;
enum class VansVFXStopMode { Drain, DetachAndDrain, Immediate };
struct VansVFXSpawnRequest
{
    VansEntityHandle owner;
    VansAssetGuid effect;
    VansSceneParentReference source;
    std::uint32_t maxConcurrentPerSource = 16;
};
struct VansVFXSceneBackend
{
    std::function<VansGenerationHandle(const VansVFXSpawnRequest&,std::string&)> spawn;
    // 有限时长效果由场景自动清理；重复触发不产生新的动作账本令牌。
    std::function<bool(const VansVFXSpawnRequest&,std::string&)> pulse;
    std::function<bool(VansGenerationHandle,VansVFXStopMode)> stop;
    std::function<bool(VansGenerationHandle)> finished;
    std::function<bool(VansGenerationHandle)> destroy;
};
// GAF 账本拥有服务令牌；场景 ParticleManager 是模拟实例的唯一所有者。
class VansVFXActionService final : public IVansActionService
{
public:
    VansVFXActionService(VansGameplayRuntime& gameplay,VansVFXSceneBackend backend);
    const VansActionServiceCapability& Capability() const override;
    VansActionCommandResult Execute(const VansActionCommand& command) override;
    bool Release(VansGenerationHandle resource,std::string& error) override;
    void Tick(double deltaSeconds) override;
private:
    struct Effect { VansGenerationHandle instance; bool justSpawned = true; };
    VansGameplayRuntime& m_Gameplay;
    VansVFXSceneBackend m_Backend;
    VansGenerationPool<Effect> m_Effects;
};
}
