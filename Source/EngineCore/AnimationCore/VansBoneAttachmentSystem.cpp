#include "VansBoneAttachmentSystem.h"

#include "VansAnimationController.h"
#include "VansAnimationNode.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <algorithm>

using namespace VansEngine;
using namespace VansGraphics;

VansBoneAttachmentSystem& VansBoneAttachmentSystem::GetInstance()
{
	static VansBoneAttachmentSystem instance;
	return instance;
}

void VansBoneAttachmentSystem::Initialize()
{
	// 当前系统无外部资源，保留接口便于后续扩展调试绘制资源。
}

void VansBoneAttachmentSystem::Shutdown()
{
	for (auto& set : m_BindingSets)
	{
		for (auto& binding : set.bindings)
			FreeBindingTransform(binding);
	}
	m_BindingSets.clear();
}

void VansBoneAttachmentSystem::RegisterBindingSet(BoneColliderBindingSet&& set)
{
	if (set.animNode == nullptr)
		return;

	UnregisterBindingSet(set.animNode);

	for (auto& binding : set.bindings)
	{
		if (binding.attachmentTransformID == UINT32_MAX)
		{
			binding.attachmentTransformID = VansTransformStore::AllocateTransform();
			binding.ownsAttachmentTransform = true;
		}
	}

	m_BindingSets.push_back(std::move(set));
}

void VansBoneAttachmentSystem::UnregisterBindingSet(VansAnimationNode* animNode)
{
	if (animNode == nullptr)
		return;

	auto it = std::remove_if(m_BindingSets.begin(), m_BindingSets.end(),
		[&](BoneColliderBindingSet& set)
		{
			if (set.animNode != animNode)
				return false;

			for (auto& binding : set.bindings)
				FreeBindingTransform(binding);
			return true;
		});

	m_BindingSets.erase(it, m_BindingSets.end());
}

BoneColliderBindingSet* VansBoneAttachmentSystem::FindBindingSet(VansAnimationNode* animNode)
{
	if (animNode == nullptr)
		return nullptr;

	for (auto& set : m_BindingSets)
	{
		if (set.animNode == animNode)
			return &set;
	}
	return nullptr;
}

void VansBoneAttachmentSystem::Update()
{
	for (auto& set : m_BindingSets)
	{
		if (set.animNode == nullptr)
			continue;

		for (auto& binding : set.bindings)
			SyncBinding(binding, set.animNode);
	}
}

void VansBoneAttachmentSystem::DrawDebugGizmos(bool enabledOnly)
{
	// MVP 暂不绘制 Gizmo；保留接口供编辑器后续接入。
	(void)enabledOnly;
}

nlohmann::json VansBoneAttachmentSystem::SerializeBindingSet(const BoneColliderBindingSet& set) const
{
	nlohmann::json root = nlohmann::json::array();

	for (const auto& binding : set.bindings)
	{
		nlohmann::json item;
		item["bone_name"]       = binding.boneName;
		item["physics_object"]  = binding.physicsObjectName;
		item["offset_position"] = { binding.offsetPosition.x, binding.offsetPosition.y, binding.offsetPosition.z };
		item["offset_rotation"] = { binding.offsetRotation.x, binding.offsetRotation.y, binding.offsetRotation.z };
		item["offset_scale"]    = { binding.offsetScale.x, binding.offsetScale.y, binding.offsetScale.z };
		item["sync_rotation"]   = binding.syncRotation;
		item["sync_scale"]      = binding.syncScale;
		item["layer"]           = binding.layerName;
		item["is_trigger"]      = binding.isTrigger;
		item["enabled"]         = binding.enabled;
		item["auto_create_node"] = binding.autoCreateNode;
		root.push_back(item);
	}

	return root;
}

void VansBoneAttachmentSystem::SyncBinding(BoneColliderBinding& binding,
                                            VansAnimationNode* animNode)
{
	if (!binding.enabled || animNode == nullptr || binding.attachmentTransformID == UINT32_MAX)
		return;

	VansAnimationController* controller = animNode->GetController();
	if (controller == nullptr)
		return;

	const auto& globalTransforms = controller->GetCachedGlobalTransforms();
	if (binding.boneIndex < 0 || binding.boneIndex >= static_cast<int>(globalTransforms.size()))
		return;

	uint32_t rootTransformID = animNode->GetTransformID();
	if (rootTransformID >= VansTransformStore::GlobalTransforms.size())
		return;

	glm::mat4 rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();
	glm::mat4 boneWorld = rootWorld * globalTransforms[binding.boneIndex];
	glm::mat4 attachWorld = boneWorld * MakeTRS(binding.offsetPosition,
	                                           binding.offsetRotation,
	                                           binding.offsetScale);

	glm::vec3 pos;
	glm::vec3 rotDeg;
	glm::vec3 scale;
	DecomposeWorldMatrix(attachWorld, pos, rotDeg, scale);

	VansTransform& attachment = VansTransformStore::GetTransform(binding.attachmentTransformID);
	attachment.m_Position = pos;
	if (binding.syncRotation)
		attachment.m_Rotation = rotDeg;
	if (binding.syncScale)
		attachment.m_Scale = scale;
	else
		attachment.m_Scale = glm::vec3(1.0f);

	VansTransformStore::TransformIDToTransformDirty[binding.attachmentTransformID] = true;

	if (binding.physicsNode != nullptr && binding.physicsNode->GetTransformID() == binding.attachmentTransformID)
		return;

	// 绑定到已有 PhysicsNode 时，该节点必须已经使用 attachmentTransformID 初始化。
	// JSON 加载阶段会完成 TransformID 重绑；此处仅保留指针有效性。
}

void VansBoneAttachmentSystem::FreeBindingTransform(BoneColliderBinding& binding)
{
	if (binding.attachmentTransformID == UINT32_MAX)
		return;

	VansTransformStore::TransformIDToTransformDirty.erase(binding.attachmentTransformID);
	if (binding.ownsAttachmentTransform)
		VansTransformStore::FreeTransform(binding.attachmentTransformID);
	binding.attachmentTransformID = UINT32_MAX;
	binding.ownsAttachmentTransform = false;
	binding.physicsNode = nullptr;
}

void VansBoneAttachmentSystem::DecomposeWorldMatrix(const glm::mat4& m,
                                                     glm::vec3& pos,
                                                     glm::vec3& rotDeg,
                                                     glm::vec3& scale)
{
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::quat rotation;
	glm::decompose(m, scale, rotation, pos, skew, perspective);
	rotDeg = glm::degrees(glm::eulerAngles(rotation));
}

glm::mat4 VansBoneAttachmentSystem::MakeTRS(const glm::vec3& pos,
                                             const glm::vec3& rotDeg,
                                             const glm::vec3& scale)
{
	glm::mat4 result(1.0f);
	result = glm::translate(result, pos);
	result = glm::rotate(result, glm::radians(rotDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
	result = glm::rotate(result, glm::radians(rotDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
	result = glm::rotate(result, glm::radians(rotDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
	result = glm::scale(result, scale);
	return result;
}
