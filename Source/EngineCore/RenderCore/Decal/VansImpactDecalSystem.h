#pragma once
#include "../../GameplayTargeting/VansSurfaceImpact.h"
#include "../../SceneCore/VansSceneImpactDecalConfig.h"
#include "../../SceneRuntime/VansRuntimeHandle.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include <array>
#include <vector>

namespace Vans { class VansRuntimeWorld; }
namespace VansGraphics
{
class VansRenderNode;
class VansDecalRenderNode;
struct VansImpactDecalDebugEntry
{
    bool active = false;
    uint32_t receiver = 0;
    double remainingSeconds = 0;
    glm::vec3 position{0};
    glm::vec3 normal{0,1,0};
    glm::vec3 scale{1};
};
// 节点由 Scene 注册和销毁；本系统只持有有限池、代际附着和接收组。
class VansImpactDecalSystem
{
public:
    static constexpr uint32_t MaximumInstances = 256;
    static constexpr uint32_t TerrainReceiver = 1024;
    VansImpactDecalSystem(Vans::VansRuntimeWorld& world, VansRenderNode* terrain);
    bool AddPool(std::string source, const Vans::VansSceneImpactDecalConfig& config,
        const std::vector<VansDecalRenderNode*>& nodes, std::string& error);
    bool Spawn(const std::string& source, const Vans::VansSurfaceImpact& impact, std::string& error);
    void Tick(double deltaSeconds);
    std::vector<VansImpactDecalDebugEntry> CaptureDebug() const;
    uint64_t SpawnCount() const { return m_SpawnCount; }
    size_t Capacity() const { return m_Entries.size(); }
    // 可独立验证的坐标运算：尺寸始终是世界单位，不继承接收者非均匀缩放。
    static bool BuildPose(const glm::mat4& anchor, const glm::vec3& localPosition,
        const glm::vec3& localNormal, const glm::vec3& localTangent,
        const Vans::VansSceneImpactDecalConfig& config, Vans::VansTransform& pose);
private:
    struct Pool { std::string source; Vans::VansSceneImpactDecalConfig config; size_t start = 0, count = 0; };
    struct ReceiverGroup { std::vector<Vans::VansComponentHandle> components; uint32_t references = 0; };
    struct Entry
    {
        VansDecalRenderNode* node = nullptr;
        size_t pool = 0;
        uint32_t receiver = 0;
        Vans::VansComponentHandle anchorComponent;
        uint32_t transform = UINT32_MAX, transformGeneration = 0;
        glm::vec3 localPosition{0}, localNormal{0,1,0}, localTangent{1,0,0};
        Vans::VansTransform lastAnchor;
        double expires = 0;
        uint64_t sequence = 0;
    };
    std::vector<Vans::VansComponentHandle> FindReceivers(Vans::VansEntityHandle entity) const;
    bool SetReceiverGroup(const ReceiverGroup& group, uint32_t id);
    void Retire(Entry& entry);
    bool Refresh(Entry& entry, bool force);
    Vans::VansRuntimeWorld& m_World;
    VansRenderNode* m_Terrain;
    std::vector<Pool> m_Pools;
    std::vector<Entry> m_Entries;
    std::array<ReceiverGroup, MaximumInstances> m_Groups;
    double m_Time = 0;
    uint64_t m_SpawnCount = 0;
};
}
