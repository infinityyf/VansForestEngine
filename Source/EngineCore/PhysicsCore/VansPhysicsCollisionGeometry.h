#pragma once
#include "VansPhysicsNode.h"
#include "../AssetCore/VansTriangleMeshData.h"
#include "../SceneRuntime/Transform/VansTransform.h"
#include <memory>
#include <cstdint>

namespace VansEngine
{
// 调用方持有缓存；签名只用于当前节点的运行缓存，不进入作者资产指纹。
struct VansPhysicsCollisionGeometrySnapshot
{
	std::uint64_t signature = 0;
	std::shared_ptr<const Vans::VansTriangleMeshData> geometry;
	glm::vec3 boundsMinimum{0}, boundsMaximum{0};
};
// 实际 Box 的世界姿态与半尺寸；没有物理对象、锁或 backend 类型。
struct VansPhysicsBoxShapeSnapshot
{
    glm::mat4 worldTransform{1};
    glm::vec3 halfExtents{0};
};
class VansPhysicsCollisionGeometry final
{
public:
    static bool BuildBoxShape(const PhysicsNodeProperties& properties,const Vans::VansTransform& worldTransform,
        const glm::vec3& creationScale,VansPhysicsBoxShapeSnapshot& output,std::string& error);
    static bool CaptureBoxShape(const VansPhysicsNode& node,VansPhysicsBoxShapeSnapshot& output,std::string& error);
	// 采用实际 PhysX cooking 和形状缩放约定；输出世界空间 CPU 三角形。
	// 曲面采用误差有界的保守三角化。调用者负责物理 cooking 生命周期。
	static bool Build(const PhysicsNodeProperties& properties, const Vans::VansTransform& worldTransform,
		const glm::vec3& creationScale, const Vans::VansTriangleMeshData* mesh, float maximumErrorMeters,
		std::uint64_t maximumBytes, Vans::VansTriangleMeshData& output, std::string& error);
	// 在物理 owner 的锁内冻结实际形状；未变化时复用不可变 CPU 数据。
	static bool Capture(const VansPhysicsNode& node, float maximumErrorMeters, std::uint64_t maximumBytes,
		VansPhysicsCollisionGeometrySnapshot& snapshot, std::string& error);
};
}
