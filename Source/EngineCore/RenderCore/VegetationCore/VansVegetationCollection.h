#pragma once
#include "../../PcgCore/VansPcgBatchPlan.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vulkan/vulkan_core.h>
namespace VansGraphics
{
class VansScene;
class VansVKDevice;
class VansVegetationSystem;
class VansVegetationCollection
{
public:
    bool Apply(VansScene& scene,VkDevice device,const Vans::VansPcgBatchUpdate& update,
        VansVKDevice* retirementDevice,std::string& error);
    // 只在渲染后端消费相机快照；远处保留稳定 CPU 实例，GPU 资源按距离驻留。
    bool UpdateResidency(VansScene& scene, VkDevice device, VansVKDevice& retirementDevice,
        float cameraX, float cameraZ, std::string& error);
    static bool WithinDistance(const Vans::VansPcgBounds& roots, float cameraX, float cameraZ, float distance);
    void ForEach(const std::function<void(VansVegetationSystem&)>& visit) const;
    std::size_t BatchCount() const { return m_Batches.size(); }
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
    bool m_HasView = false;
    float m_CameraX = 0, m_CameraZ = 0;
};
}
