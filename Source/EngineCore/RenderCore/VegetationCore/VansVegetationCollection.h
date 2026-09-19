#pragma once
#include "../../PcgCore/VansPcgBatchPlan.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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
    bool Apply(VansScene& scene,VkDevice device,const Vans::VansPcgBatchUpdate& update,
        VansVKDevice* retirementDevice,std::string& error);
    // 树按树种全局驻留并分 LOD 绘制；草仍按区块距离驻留。
    bool UpdateResidency(VansScene& scene, VkDevice device, VansVKDevice& retirementDevice,
        float cameraX, float cameraZ, std::string& error);
    using TreeBatchKey=std::pair<std::shared_ptr<const Vans::VansPlantTypeAsset>,std::string>;
    using CellSources=std::map<Vans::VansPcgBatchKey,std::shared_ptr<const Vans::VansPcgBatchSource>>;
    using TreeSources=std::map<TreeBatchKey,std::shared_ptr<const Vans::VansPcgBatchSource>>;
    static TreeSources GroupTreeBatches(const CellSources& cells);
    static bool WithinDistance(const Vans::VansPcgBounds& roots, float cameraX, float cameraZ, float distance);
    void ForEach(const std::function<void(VansVegetationSystem&)>& visit) const;
    // PCG 是通用体素来源之一；整株模型不受绘制裁剪影响，草只接收 GI。
    void CollectGIVoxelSources(VansScene& scene, std::vector<GIVoxelSource>& sources) const;
    // 资源流送完成后重试此前暂时不可用的树来源；不触发整场 GI 重建。
    void RetryUnavailableGIVoxelSources(VansScene& scene);
    std::size_t BatchCount() const { return m_Batches.size()+m_TreeBatches.size(); }
    std::string LastUpdateError() const { std::lock_guard<std::mutex> lock(m_StatusMutex);return m_LastUpdateError; }
private:
    mutable std::mutex m_StatusMutex;
    std::string m_LastUpdateError;
    struct Batch
    {
        std::shared_ptr<const Vans::VansPcgBatchSource> source;
        std::shared_ptr<VansVegetationSystem> system;
        Vans::VansPcgBounds roots;
        float cellSize = 0;
        bool active = false;
    };
    std::map<Vans::VansPcgBatchKey,Batch> m_Batches;
    CellSources m_TreeCells;
    mutable std::set<Vans::VansPcgBatchKey> m_GIUnavailableTreeCells;
    std::set<std::string> m_GIPendingRemovedSources;
    std::map<TreeBatchKey,Batch> m_TreeBatches;
    bool m_HasView = false;
    float m_CameraX = 0, m_CameraZ = 0;
};
}
