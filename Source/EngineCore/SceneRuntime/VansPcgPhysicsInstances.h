#pragma once
#include "../PcgCore/VansPcgCollisionPlan.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "Transform/VansTransform.h"
#include <functional>
#include <memory>

namespace Vans
{
class VansRuntimeWorld;
using VansPcgCollisionMeshAccess=std::function<bool(const VansAssetGuid&,
    std::shared_ptr<const VansTriangleMeshData>&,std::string&)>;

// Shared by the live actor producer and offline collision export. No render calibration or GPU data.
bool ResolvePcgCollisionInput(const VansPcgCollisionInstance& instance,
    VansEngine::PhysicsNodeProperties& properties,VansTransform& pose,std::string& error);

// Scene owns this collection before RuntimeWorld teardown. GPU preview never publishes to it.
class VansPcgPhysicsInstances final
{
public:
    static bool Create(VansRuntimeWorld& world,const VansPcgCollisionPlan& plan,
        const VansPcgCollisionMeshAccess& meshes,std::unique_ptr<VansPcgPhysicsInstances>& output,std::string& error);
    ~VansPcgPhysicsInstances();
    void SynchronizeLifecycle();
    const std::vector<VansPcgCollisionExclusion>& NoCollisionPrototype() const { return m_Excluded; }
private:
    explicit VansPcgPhysicsInstances(VansRuntimeWorld& world);
    struct Entry;
    VansRuntimeWorld& m_World;
    std::vector<std::unique_ptr<Entry>> m_Entries;
    std::vector<VansPcgCollisionExclusion> m_Excluded;
};
}
