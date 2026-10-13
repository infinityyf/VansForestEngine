#pragma once
#include "../NavigationCore/VansNavigationSourceSnapshot.h"
#include "../PhysicsCore/VansPhysicsCollisionGeometry.h"
#include <functional>
#include <map>

namespace Vans
{
class VansRuntimeWorld;
using VansNavigationCpuMeshAccess = std::function<bool(const std::string&,
	std::shared_ptr<const VansTriangleMeshData>&,std::string&)>;

// Scene 组合层使用的现场适配器；发布的数据不带节点、actor 或资源解析回调。
class VansRuntimeNavigationSources final
{
public:
	bool Capture(const VansRuntimeWorld& runtime,const VansNavigationSettings& settings,
		const VansNavigationCpuMeshAccess& meshes,const std::vector<VansNavigationSourceRecord>& environment,
		VansNavigationSourceSnapshot& output,std::uint64_t& observation,std::string& error);
private:
	std::map<std::uint64_t,VansEngine::VansPhysicsCollisionGeometrySnapshot> m_Collisions;
};
}
