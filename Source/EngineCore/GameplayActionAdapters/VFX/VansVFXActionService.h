#pragma once
#include "../../GameplayActionCore/VansActionServices.h"
#include "../../GameplayActionCore/VansGameplayServiceRuntime.h"
#include "../../RuntimeCore/VansGenerationPool.h"
#include "../../SceneCore/VansSceneParentReference.h"
#include "../../ParticleCore/VansParticleData.h"
#include "../../GameplayTargeting/VansSurfaceField.h"
#include <functional>
#include <optional>
#include <glm/glm.hpp>

namespace Vans
{
enum class VansVFXStopMode { Drain, DetachAndDrain, Immediate };
struct VansVFXSpawnRequest
{
    VansEntityHandle owner;
    VansAssetGuid effect;
    VansSceneParentReference source;
    VansGenerationHandle emissionGroup;
    std::optional<glm::vec3> worldPosition;
    // Detached root: local +X follows this direction with world +Y as up.
    // Missing direction retains the existing identity root orientation.
    std::optional<glm::vec3> worldDirection;
    std::optional<std::vector<VansGraphics::VansParticleEmissionPoint>> emissionPoints;
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
    std::function<bool(VansGenerationHandle,const std::vector<VansGraphics::VansParticleEmissionPoint>&)> setEmissionPoints;
    std::shared_ptr<VansSurfaceFieldRegistry> surfaceFields;
    std::function<bool(VansGenerationHandle,const std::vector<glm::vec3>&,std::vector<float>&)> sampleExtinction;
};
// GAF 账本拥有服务令牌；场景 ParticleManager 是模拟实例的唯一所有者。
class VansVFXActionService final : public IVansActionService
{
public:
    VansVFXActionService(IVansGameplayServiceRuntime& runtime,VansVFXSceneBackend backend);
    const VansActionServiceCapability& Capability() const override;
    VansActionCommandResult Execute(const VansActionCommand& command) override;
    bool Release(VansGenerationHandle resource,std::string& error) override;
    void Tick(double deltaSeconds) override;
private:
    struct SurfaceCell { VansGraphics::VansParticleEmissionPoint point; std::uint32_t parent=0,depth=0;
        float interval=.1f,nextSpawn=0; };
    struct SurfaceField
    {
        glm::vec3 origin{0}; std::uint32_t mask=0;
        float radius=0,spacing=1,probeHeight=1,maxDrop=2,minNormalY=0.25f;
        float duration=1,spreadDuration=1,spawnInterval=0.1f,age=0,nextSpawn=0;
        float spreadSpeed=1,childIntervalMultiplier=1,maxChildInterval=.5f;
        std::uint32_t maxDepth=4,randomState=1;
        bool connected=false;
        std::uint32_t maximum=16,candidate=0;
        bool stopped=false;
        std::string stopReason="Completed";
        std::vector<glm::vec3> candidates;
        std::vector<SurfaceCell> cells;
        std::vector<VansGraphics::VansParticleEmissionPoint> points;
    };
    struct ExtinguishingVolume {
        glm::vec3 origin{0},zeroDistanceDirection{0};
        std::vector<glm::vec3> sampleOffsets;
        VansAssetGuid extinguishEffect;
        float extinctionThreshold=0,fraction=0,period=.1f,age=0,nextSample=0;
        std::uint32_t minimumCells=0;
    };
    struct Effect { VansGenerationHandle instance; bool justSpawned = true; std::optional<SurfaceField> field; VansEntityHandle owner;
        VansActionHandle action; std::optional<ExtinguishingVolume> volume; bool completionNotified=false; };
    void PublishSurfaceField(VansGenerationHandle handle,const Effect& effect);
    void NotifySurfaceFieldStopped(VansGenerationHandle handle,Effect& effect);
    void SampleExtinguishingVolumes(const std::vector<VansGenerationHandle>& volumes);
    bool PlaceSurfaceCell(SurfaceField& field,glm::vec3 candidate,std::uint32_t& queryBudget,float bornTime,
        std::optional<std::uint32_t> requestedParent=std::nullopt);
    void ExtendSurfaceField(Effect& effect,float seconds,std::uint32_t& queryBudget);
    IVansGameplayServiceRuntime& m_Runtime;
    VansVFXSceneBackend m_Backend;
    VansGenerationPool<Effect> m_Effects;
    std::uint64_t m_FieldCursor=0;
};
}
