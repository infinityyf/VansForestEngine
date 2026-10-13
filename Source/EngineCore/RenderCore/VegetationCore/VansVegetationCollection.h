#pragma once
#include "../../PcgCore/VansPcgBatchPlan.h"
#include "../VansRenderBounds.h"
#include "VansGrassWorkset.h"
#include <functional>
#include <glm/mat4x4.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vulkan/vulkan_core.h>
namespace VansGraphics
{
class VansScene;
class VansVKDevice;
class VansVegetationSystem;
struct GIVoxelSource;
class VansVegetationCollection
{
public:
    bool Apply(VansScene& scene,VansVKDevice& resourceDevice,const Vans::VansPcgBatchUpdate& update,
        std::string& error);
    // 树按树种全局驻留并分 LOD 绘制；草仍按区块距离驻留。
    bool UpdateResidency(VansScene& scene, VansVKDevice& resourceDevice,
        float cameraX, float cameraZ, const glm::mat4& worldToClip, uint32_t frameSlot, std::string& error);
    using TreeBatchKey=std::pair<std::shared_ptr<const Vans::VansPlantTypeAsset>,std::string>;
    using CellSources=std::map<Vans::VansPcgBatchKey,std::shared_ptr<const Vans::VansPcgBatchSource>>;
    using TreeSources=std::map<TreeBatchKey,std::shared_ptr<const Vans::VansPcgBatchSource>>;
    static TreeSources GroupTreeBatches(const CellSources& cells);
    static bool WithinDistance(const Vans::VansPcgBounds& roots, float cameraX, float cameraZ, float distance);
    void ForEach(const std::function<void(VansVegetationSystem&)>& visit) const;
    // 草按物种/变体派发和绘制；工作表只含相关 cell，待复制池也需录制。
    void ForEachViewRelevant(const std::function<void(VansVegetationSystem&)>& visit) const;
    // PCG 是通用体素来源之一；整株模型不受绘制裁剪影响，草只接收 GI。
    void CollectGIVoxelSources(VansScene& scene, std::vector<GIVoxelSource>& sources) const;
    // 资源流送完成后重试此前暂时不可用的树来源；不触发整场 GI 重建。
    void RetryUnavailableGIVoxelSources(VansScene& scene);
    std::size_t BatchCount() const { return m_GrassPools.size()+m_TreeBatches.size(); }
    std::string LastUpdateError() const { std::lock_guard<std::mutex> lock(m_StatusMutex);return m_LastUpdateError; }
private:
    mutable std::mutex m_StatusMutex;
    std::string m_LastUpdateError;
    struct Batch
    {
        std::shared_ptr<const Vans::VansPcgBatchSource> source;
        std::shared_ptr<VansVegetationSystem> system;
    };
    struct GrassCell
    {
        std::shared_ptr<const Vans::VansPcgBatchSource> source;
        Vans::VansPcgBounds roots;
        VansRenderAABB worldBounds;
        float cellSize = 0;
        bool resident = false;
        bool active = false;
        bool viewRelevant = false;
    };
    struct GrassPool
    {
        std::shared_ptr<VansVegetationSystem> system;
        CellSources cells;
        GrassPoolLayout<std::pair<Vans::VansPcgBatchKey,uint32_t>> layout;
        bool viewRelevant = false;
    };
    using GrassCells = std::map<Vans::VansPcgBatchKey, GrassCell>;
    using GrassPoolKey = std::pair<TreeBatchKey,uint32_t>;
    using GrassPageKey = std::pair<Vans::VansPcgBatchKey,uint32_t>;
    using GrassPools = std::map<GrassPoolKey, GrassPool>;
    struct GrassPoolBuild;
    GrassPoolBuild BuildGrassPools(VansScene& scene, VansVKDevice& resourceDevice, const GrassCells& cells) const;
    GrassCells m_GrassCells;
    GrassPools m_GrassPools;
    CellSources m_TreeCells;
    mutable std::set<Vans::VansPcgBatchKey> m_GIUnavailableTreeCells;
    std::set<std::string> m_GIPendingRemovedSources;
    std::map<TreeBatchKey,Batch> m_TreeBatches;
    bool m_HasView = false;
    float m_CameraX = 0, m_CameraZ = 0;
};
}
