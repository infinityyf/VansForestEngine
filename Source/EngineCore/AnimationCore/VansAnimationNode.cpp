#include "VansAnimationNode.h"
#include "../RenderCore/VansRenderNode.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cstring>

using namespace VansGraphics;

// ════════════════════════════════════════════════════════════════
//  构造 & 析构
// ════════════════════════════════════════════════════════════════

VansAnimationNode::VansAnimationNode(const std::string& name)
	: m_Name(name)
{
	std::memset(&m_BoneMatricesSSBO, 0, sizeof(BoneMatricesSSBO));
	for (uint32_t i = 0; i < MAX_BONES; i++)
		m_BoneMatricesSSBO.boneMatrices[i] = glm::mat4(1.0f);
}

VansAnimationNode::~VansAnimationNode()
{
	DestroyGPUResources();
}

// ════════════════════════════════════════════════════════════════
//  关联 RenderNode
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetRenderNode(VansRenderNode* renderNode)
{
	m_RenderNodes = { renderNode };
}

void VansAnimationNode::SetRenderNodes(const std::vector<VansRenderNode*>& nodes)
{
	m_RenderNodes = nodes;
}

// ════════════════════════════════════════════════════════════════
//  骨骼
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetSkeleton(const Skeleton& skeleton)
{
	m_Skeleton = skeleton;
	VANS_LOG("[VansAnimationNode] " << m_Name << ": skeleton set with "
	         << m_Skeleton.bones.size() << " bones");
}

// ════════════════════════════════════════════════════════════════
//  Controller 绑定
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetController(VansAnimationController* controller)
{
	m_Controller = controller;

	if (m_Controller)
	{
		// 将 Node 侧的骨骼覆盖映射关联到 Controller，以便在 Update 管线中应用
		m_Controller->SetBoneOverrides(&m_BoneOverrides);

		VANS_LOG("[VansAnimationNode] " << m_Name << ": controller '" 
		         << m_Controller->GetName() << "' bound");
	}
}

// ════════════════════════════════════════════════════════════════
//  播放控制（委托给 Controller）
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::Play()
{
	if (m_Controller)
		m_Controller->Play();
}

void VansAnimationNode::Play(const std::string& stateName)
{
	if (m_Controller)
		m_Controller->Play(stateName);
}

void VansAnimationNode::Pause()
{
	if (m_Controller)
		m_Controller->Pause();
}

void VansAnimationNode::Resume()
{
	if (m_Controller)
		m_Controller->Resume();
}

void VansAnimationNode::Stop()
{
	if (m_Controller)
		m_Controller->Stop();
}

// ════════════════════════════════════════════════════════════════
//  状态查询（委托给 Controller）
// ════════════════════════════════════════════════════════════════

AnimationState VansAnimationNode::GetState() const
{
	if (m_Controller)
		return m_Controller->GetPlaybackState();
	return AnimationState::Stopped;
}

float VansAnimationNode::GetCurrentPlayTime() const
{
	if (m_Controller)
		return m_Controller->GetCurrentPlayTime();
	return 0.0f;
}

float VansAnimationNode::GetDuration() const
{
	if (m_Controller)
		return m_Controller->GetCurrentDuration();
	return 0.0f;
}

float VansAnimationNode::GetNormalizedTime() const
{
	if (m_Controller)
		return m_Controller->GetNormalizedTime();
	return 0.0f;
}

std::string VansAnimationNode::GetCurrentStateName() const
{
	if (m_Controller)
		return m_Controller->GetCurrentStateName();
	return "";
}

float VansAnimationNode::GetSpeed() const
{
	if (m_Controller)
		return m_Controller->GetSpeed();
	return 1.0f;
}

// ════════════════════════════════════════════════════════════════
//  Events
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::AddEvent(const std::string& clipName, AnimationEvent event)
{
	m_Events[clipName].push_back(std::move(event));

	auto& events = m_Events[clipName];
	std::sort(events.begin(), events.end(),
		[](const AnimationEvent& a, const AnimationEvent& b) {
			return a.triggerTime < b.triggerTime;
		});
}

// ════════════════════════════════════════════════════════════════
//  Root Motion
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::EnableRootMotion(bool enable)
{
	if (m_Controller)
		m_Controller->EnableRootMotion(enable);
}

bool VansAnimationNode::IsRootMotionEnabled() const
{
	if (m_Controller)
		return m_Controller->IsRootMotionEnabled();
	return false;
}

void VansAnimationNode::SetTransformID(uint32_t transformID)
{
	m_TransformID    = transformID;
	m_HasTransformID = true;
}

void VansAnimationNode::SetRootBone(const std::string& boneName)
{
	auto it = m_Skeleton.boneNameToIndex.find(boneName);
	if (it != m_Skeleton.boneNameToIndex.end())
	{
		if (m_Controller)
			m_Controller->SetRootBoneIndex(it->second);

		VANS_LOG("[VansAnimationNode] " << m_Name << ": root bone set to \"" << boneName
		         << "\" (index " << it->second << ")");
	}
	else
	{
		VANS_LOG_WARN("[VansAnimationNode] " << m_Name << ": root bone \"" << boneName << "\" not found in skeleton");
	}
}

glm::vec3 VansAnimationNode::GetRootMotionDelta() const
{
	if (m_Controller)
		return m_Controller->GetRootMotionDelta();
	return glm::vec3(0.0f);
}

glm::quat VansAnimationNode::GetRootRotationDelta() const
{
	if (m_Controller)
		return m_Controller->GetRootRotationDelta();
	return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

// ════════════════════════════════════════════════════════════════
//  Bone Overrides
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::SetBoneLocalTransform(const std::string& boneName, const glm::mat4& transform)
{
	m_BoneOverrides[boneName] = transform;
}

void VansAnimationNode::ClearBoneOverride(const std::string& boneName)
{
	m_BoneOverrides.erase(boneName);
}

// ════════════════════════════════════════════════════════════════
//  每帧更新
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::Update(float deltaTime)
{
	if (!m_Controller)
		return;

	// 1. 让 Controller 完成核心更新（状态机 + 关键帧插值 + 混合 + root motion + 矩阵输出）
	m_Controller->Update(deltaTime, m_Skeleton);

	// 2. 如果有 root motion，将 delta 应用到 Transform
	if (m_Controller->IsRootMotionEnabled() && m_HasTransformID)
	{
		glm::vec3 deltaPos = m_Controller->GetRootMotionDelta();
		glm::quat deltaRot = m_Controller->GetRootRotationDelta();
		ApplyRootMotionToTransform(deltaPos, deltaRot);
	}
	else
	{
		// 诊断: 仅输出一次
		static bool s_LoggedOnce = false;
		if (!s_LoggedOnce)
		{
			VANS_LOG("[RootMotion] Node '" << m_Name << "' skipped ApplyRootMotion: enabled="
			         << m_Controller->IsRootMotionEnabled() << " hasTransformID=" << m_HasTransformID);
			s_LoggedOnce = true;
		}
	}

	// 3. Fire events (Node 侧仍然管理事件)
	FireEvents();
}

// ════════════════════════════════════════════════════════════════
//  结果访问
// ════════════════════════════════════════════════════════════════

const BoneMatricesSSBO& VansAnimationNode::GetBoneSSBO() const
{
	if (m_Controller)
		return m_Controller->GetBoneMatricesSSBO();
	return m_BoneMatricesSSBO;
}

// ════════════════════════════════════════════════════════════════
//  GPU 资源管理
// ════════════════════════════════════════════════════════════════

bool VansAnimationNode::InitGPUResources(VkDevice device, uint32_t framesInFlight)
{
	m_Device         = device;
	m_FramesInFlight = framesInFlight;

	VkDeviceSize bufferSize = sizeof(BoneMatricesSSBO);
	m_BoneBuffers.resize(framesInFlight);

	for (uint32_t i = 0; i < framesInFlight; i++)
	{
		bool ok = m_BoneBuffers[i].CreatVulkanBuffer(
			device,
			bufferSize,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create bone buffer " << i);
			return false;
		}
	}

	VANS_LOG("[VansAnimationNode] " << m_Name << ": GPU resources initialized ("
	         << framesInFlight << " frames, " << bufferSize << " bytes each)");
	return true;
}

void VansAnimationNode::DestroyGPUResources()
{
	if (m_Device == VK_NULL_HANDLE)
		return;

	for (auto& buffer : m_BoneBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	for (auto& buffer : m_PerSubmeshBoneIDBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	for (auto& buffer : m_PerSubmeshBoneWeightBuffers)
		buffer.DestroyVulkanBuffer(m_Device);

	m_BoneBuffers.clear();
	m_PerSubmeshBoneIDBuffers.clear();
	m_PerSubmeshBoneWeightBuffers.clear();
	m_Device = VK_NULL_HANDLE;
}

void VansAnimationNode::UploadPerSubmeshBoneBuffers(const std::vector<std::vector<VertexBoneData>>& perSubmeshBoneData)
{
	if (perSubmeshBoneData.empty() || m_Device == VK_NULL_HANDLE)
		return;

	uint32_t submeshCount = static_cast<uint32_t>(perSubmeshBoneData.size());
	m_PerSubmeshBoneIDBuffers.resize(submeshCount);
	m_PerSubmeshBoneWeightBuffers.resize(submeshCount);

	for (uint32_t s = 0; s < submeshCount; s++)
	{
		const auto& boneData = perSubmeshBoneData[s];
		if (boneData.empty())
		{
			m_PerSubmeshBoneIDBuffers[s].CreatVulkanBuffer(
				m_Device, 64, VK_FORMAT_R32_SFLOAT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			m_PerSubmeshBoneWeightBuffers[s].CreatVulkanBuffer(
				m_Device, 64, VK_FORMAT_R32_SFLOAT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			continue;
		}

		uint32_t vertexCount = static_cast<uint32_t>(boneData.size());

		std::vector<VertexBoneID> boneIDs(vertexCount);
		std::vector<VertexBoneWeight> boneWeights(vertexCount);
		for (uint32_t v = 0; v < vertexCount; v++)
		{
			for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
			{
				boneIDs[v].boneIDs[i]     = boneData[v].boneIDs[i];
				boneWeights[v].weights[i]  = boneData[v].weights[i];
			}
		}

		VkDeviceSize idBufferSize = sizeof(VertexBoneID) * vertexCount;
		bool ok = m_PerSubmeshBoneIDBuffers[s].CreatVulkanBuffer(
			m_Device, idBufferSize, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create bone ID buffer for submesh " << s);
			continue;
		}
		m_PerSubmeshBoneIDBuffers[s].SetBufferData(
			boneIDs.data(), 0, static_cast<int>(idBufferSize));

		VkDeviceSize weightBufferSize = sizeof(VertexBoneWeight) * vertexCount;
		ok = m_PerSubmeshBoneWeightBuffers[s].CreatVulkanBuffer(
			m_Device, weightBufferSize, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (!ok)
		{
			VANS_LOG_ERROR("[VansAnimationNode] " << m_Name << ": failed to create bone weight buffer for submesh " << s);
			continue;
		}
		m_PerSubmeshBoneWeightBuffers[s].SetBufferData(
			boneWeights.data(), 0, static_cast<int>(weightBufferSize));

		VANS_LOG("[VansAnimationNode] " << m_Name << ": submesh " << s
			<< " bone buffers uploaded (" << vertexCount << " vertices)");
	}

	VANS_LOG("[VansAnimationNode] " << m_Name << ": uploaded per-submesh bone buffers for "
		<< submeshCount << " submesh(es)");
}

void VansAnimationNode::UploadBoneMatrices(uint32_t frameIndex)
{
	if (frameIndex >= m_BoneBuffers.size())
		return;

	const BoneMatricesSSBO& ssbo = GetBoneSSBO();
	m_BoneBuffers[frameIndex].SetBufferData(
		&ssbo,
		0,
		sizeof(BoneMatricesSSBO));
}

// ════════════════════════════════════════════════════════════════
//  内部方法: ApplyBoneOverrides
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms)
{
	for (const auto& [boneName, overrideTransform] : m_BoneOverrides)
	{
		auto it = m_Skeleton.boneNameToIndex.find(boneName);
		if (it != m_Skeleton.boneNameToIndex.end())
		{
			int idx = it->second;
			if (idx >= 0 && idx < static_cast<int>(localTransforms.size()))
				localTransforms[idx] = overrideTransform;
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  内部方法: ApplyRootMotionToTransform
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::ApplyRootMotionToTransform(const glm::vec3& deltaPos, const glm::quat& deltaRot)
{
	if (!m_HasTransformID)
		return;

	// 零值 delta 跳过
	if (glm::length(deltaPos) < 0.00001f && glm::abs(glm::dot(deltaRot, glm::quat(1, 0, 0, 0)) - 1.0f) < 0.00001f)
		return;

	VansTransform& transform = VansTransformStore::GetTransform(m_TransformID);

	// 将 local-space 的 delta 旋转到世界空间（基于实体当前 Y 旋转）
	float yawRad = glm::radians(transform.m_Rotation.y);
	glm::mat3 entityYawMat = glm::mat3(glm::rotate(glm::mat4(1.0f), yawRad, glm::vec3(0.0f, 1.0f, 0.0f)));
	glm::vec3 worldDelta = entityYawMat * (deltaPos * transform.m_Scale);

	transform.m_Position += worldDelta;

	// 从 deltaRot 提取 yaw 分量应用到实体
	glm::vec3 deltaEuler = glm::degrees(glm::eulerAngles(deltaRot));
	transform.m_Rotation.y += deltaEuler.y;

	VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
}

// ════════════════════════════════════════════════════════════════
//  内部方法: FireEvents
// ════════════════════════════════════════════════════════════════

void VansAnimationNode::FireEvents()
{
	if (!m_Controller)
		return;

	std::string currentClipName = m_Controller->GetCurrentStateName();
	float currentTime = m_Controller->GetCurrentPlayTime();

	auto it = m_Events.find(currentClipName);
	if (it == m_Events.end())
		return;

	for (const auto& event : it->second)
	{
		if (event.triggerTime > m_LastEventTime && event.triggerTime <= currentTime)
		{
			if (event.callback)
				event.callback();
		}
	}

	m_LastEventTime = currentTime;
}
