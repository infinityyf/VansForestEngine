#pragma once
#include "VansPhysicsNode.h"
#include <algorithm>

namespace VansEngine
{
enum class VansPhysicsShapeScalePhase { Creation, Update };
struct VansPhysicsShapeDimensions
{
	glm::vec3 boxExtents{0.0f};
	float sphereRadius=0.0f, capsuleRadius=0.0f, capsuleHalfHeight=0.0f;
};
// 保留物理 owner 的初次创建与后续缩放语义；离线碰撞导出共用同一计算。
inline VansPhysicsShapeDimensions ResolvePhysicsShapeDimensions(const PhysicsNodeProperties& properties,
	const glm::vec3& scale,VansPhysicsShapeScalePhase phase)
{
	VansPhysicsShapeDimensions result;
	if(phase==VansPhysicsShapeScalePhase::Creation)
	{
		result.boxExtents=properties.boxExtents*scale;
		result.sphereRadius=properties.sphereRadius*((scale.x+scale.y+scale.z)/3.f);
		result.capsuleRadius=properties.capsuleRadius*((scale.x+scale.z)/2.f);
		result.capsuleHalfHeight=properties.capsuleHalfHeight*scale.y;
	}
	else
	{
		const auto absolute=glm::abs(scale);
		result.boxExtents=glm::max(glm::abs(properties.boxExtents*scale),glm::vec3(1.e-4f));
		const float average=(std::max)((absolute.x+absolute.y+absolute.z)/3.f,1.e-4f);
		result.sphereRadius=(std::max)(properties.sphereRadius*average,1.e-4f);
		const float radiusScale=(std::max)((absolute.x+absolute.z)/2.f,1.e-4f);
		const float heightScale=(std::max)(absolute.y,1.e-4f);
		result.capsuleRadius=(std::max)(properties.capsuleRadius*radiusScale,1.e-4f);
		result.capsuleHalfHeight=(std::max)(properties.capsuleHalfHeight*heightScale,1.e-4f);
	}
	return result;
}
}
