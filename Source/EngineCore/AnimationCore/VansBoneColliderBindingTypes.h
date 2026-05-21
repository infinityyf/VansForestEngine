#pragma once

#include "../PhysicsCore/VansPhysicsNode.h"
#include <../../GLM/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansAnimationNode;
}

namespace VansEngine
{
	struct BoneColliderBinding
	{
		// ── 骨骼端 ──────────────────────────────────────────────────────
		std::string boneName;
		int         boneIndex = -1;

		// ── 相对骨骼局部空间的偏移 ────────────────────────────────────
		glm::vec3 offsetPosition = glm::vec3(0.0f);
		glm::vec3 offsetRotation = glm::vec3(0.0f);
		glm::vec3 offsetScale    = glm::vec3(1.0f);

		// ── 附着点 Transform（由绑定系统持有并释放）──────────────────
		uint32_t attachmentTransformID = UINT32_MAX;
		bool     ownsAttachmentTransform = true;

		// ── 碰撞体端 ──────────────────────────────────────────────────
		std::string      physicsObjectName;
		VansPhysicsNode* physicsNode = nullptr;

		// ── 控制选项 ──────────────────────────────────────────────────
		bool        enabled      = true;
		bool        syncRotation = true;
		bool        syncScale    = false;
		std::string layerName    = "Default";
		bool        isTrigger    = false;

		// ── 自动创建预留字段（MVP 暂不创建节点）──────────────────────
		bool                autoCreateNode = false;
		PhysicsColliderType shapeType      = PhysicsColliderType::Capsule;
		glm::vec3           shapeExtents   = glm::vec3(0.1f, 0.25f, 0.1f);
	};

	struct BoneColliderBindingSet
	{
		VansGraphics::VansAnimationNode* animNode = nullptr;
		std::vector<BoneColliderBinding> bindings;
	};
}
