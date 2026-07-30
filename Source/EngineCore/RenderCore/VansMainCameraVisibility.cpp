#include "VansMainCameraVisibility.h"

#include "VansCamera.h"
#include "VansRenderNode.h"
#include "VansScene.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
	bool NearlyEqual(float a, float b, float epsilon)
	{
		return std::abs(a - b) <= epsilon;
	}

	glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
	{
		const float len2 = glm::dot(v, v);
		return len2 > 1.0e-8f ? glm::normalize(v) : fallback;
	}
}

uint64_t VansGraphics::MakeMainCameraCullNodeId(const VansRenderNode* node)
{
	return reinterpret_cast<uint64_t>(node);
}

bool VansGraphics::TryGetStaticNodeWorldBounds(VansRenderNode* node, VansRenderBounds& bounds)
{
	if (node == nullptr || node->m_Mesh == nullptr || node->m_HasSkeletonBone ||
		!node->m_Mesh->HasLocalOBB())
	{
		return false;
	}

	if (!node->HasWorldBounds())
		node->UpdateWorldBoundsFromTransform();
	if (!node->HasWorldBounds())
		return false;
	bounds = node->GetWorldBounds();
	return bounds.IsValid();
}

bool VansGraphics::IsNodeVisibleInFrustum(VansRenderNode* node, const glm::mat4& worldToClip)
{
	VansRenderBounds bounds;
	return !TryGetStaticNodeWorldBounds(node, bounds) ||
		RenderBoundsIntersectsClipFrustum(bounds, worldToClip);
}

void VansGraphics::VansScene::SetMainCameraHiZCullSettings(const VansMainCameraHiZCullSettings& settings)
{
	m_MainCameraHiZCullSettings = settings;
	if (!settings.enabled)
	{
		ResetMainCameraHiZVisibility();
	}
}

void VansGraphics::VansScene::ResetMainCameraHiZVisibility()
{
	m_MainCameraCullCandidates.clear();
	m_MainCameraCullObjectsGPU.clear();
	m_MainCameraCullIndexByNodeId.clear();
	m_MainCameraDrawVisibilityByNodeId.clear();
	m_MainCameraHiZCulledDebugNodes.clear();
	m_MainCameraPreviousCullBounds.clear();
	m_MainCameraForceVisibleFramesByNodeId.clear();
	m_MainCameraHiZHistory = VansMainCameraHiZHistoryState{};
	m_MainCameraVisibilityStats = VansMainCameraVisibilityStats{};
	m_MainCameraHasPendingVisibilityReadback = false;
}

void VansGraphics::VansScene::ReleaseMainCameraHiZGpuResources(VkDevice device)
{
	if (m_MainCameraCullObjectBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
		m_MainCameraCullObjectBuffer.DestroyVulkanBuffer(device);
	if (m_MainCameraVisibilityBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
		m_MainCameraVisibilityBuffer.DestroyVulkanBuffer(device);
	m_MainCameraCullBufferCapacity = 0;
	m_MainCameraHasPendingVisibilityReadback = false;
}

bool VansGraphics::VansScene::EnsureMainCameraHiZGpuResources(VansVKDevice& device, uint32_t candidateCount)
{
	if (candidateCount == 0)
		return true;

	if (candidateCount <= m_MainCameraCullBufferCapacity &&
		m_MainCameraCullObjectBuffer.GetNativeBuffer() != VK_NULL_HANDLE &&
		m_MainCameraVisibilityBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		return true;
	}

	const uint32_t newCapacity = std::max(candidateCount, std::max(128u, m_MainCameraCullBufferCapacity * 2u));
	VkDevice nativeDevice = device.GetLogicDevice();
	ReleaseMainCameraHiZGpuResources(nativeDevice);

	const VkDeviceSize objectBytes = sizeof(VansMainCameraCullObjectGPU) * static_cast<VkDeviceSize>(newCapacity);
	const VkDeviceSize visibilityBytes = sizeof(uint32_t) * static_cast<VkDeviceSize>(newCapacity);
	const bool objectOk = m_MainCameraCullObjectBuffer.CreatVulkanBuffer(
		nativeDevice,
		objectBytes,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	const bool visibilityOk = m_MainCameraVisibilityBuffer.CreatVulkanBuffer(
		nativeDevice,
		visibilityBytes,
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	if (!objectOk || !visibilityOk)
	{
		ReleaseMainCameraHiZGpuResources(nativeDevice);
		VANS_LOG_ERROR("[MainCameraHiZCull] Failed to create cull buffers.");
		return false;
	}

	m_MainCameraCullBufferCapacity = newCapacity;
	return true;
}

void VansGraphics::VansScene::ConsumeMainCameraHiZReadback()
{
	m_MainCameraDrawVisibilityByNodeId.clear();
	m_MainCameraHiZCulledDebugNodes.clear();
	m_MainCameraVisibilityStats.hizCulledCount = 0;
	if (!m_MainCameraHasPendingVisibilityReadback ||
		m_MainCameraVisibilityBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
		!m_MainCameraVisibilityBuffer.IsMapped())
	{
		m_MainCameraHasPendingVisibilityReadback = false;
		return;
	}

	const uint32_t count = std::min(
		static_cast<uint32_t>(m_MainCameraLastDispatchedNodeIds.size()),
		m_MainCameraCullBufferCapacity);
	if (count == 0)
	{
		m_MainCameraHasPendingVisibilityReadback = false;
		return;
	}

	m_MainCameraVisibilityBuffer.InvalidateMappedRange(0, sizeof(uint32_t) * count);
	const auto* visibility = static_cast<const uint32_t*>(m_MainCameraVisibilityBuffer.GetMappedPtr());
	uint32_t culledCount = 0;
	m_MainCameraHiZCulledDebugNodes.reserve(count);
	for (uint32_t index = 0; index < count; ++index)
	{
		const bool visible = visibility[index] != 0;
		const uint64_t nodeId = m_MainCameraLastDispatchedNodeIds[index];
		m_MainCameraDrawVisibilityByNodeId[nodeId] = visible;
		if (!visible)
		{
			++culledCount;
			if (index < m_MainCameraCullCandidates.size())
			{
				const VansMainCameraCullCandidate& candidate = m_MainCameraCullCandidates[index];
				VansMainCameraHiZCulledNodeDebug debugNode;
				debugNode.nodeId = nodeId;
				debugNode.nodeName = candidate.node ? candidate.node->m_NodeName : std::string{};
				debugNode.cullClass = candidate.cullClass;
				debugNode.bounds = candidate.bounds;
				m_MainCameraHiZCulledDebugNodes.push_back(std::move(debugNode));
			}
		}
	}
	m_MainCameraVisibilityStats.hizCulledCount = culledCount;
	m_MainCameraHasPendingVisibilityReadback = false;
}

void VansGraphics::VansScene::UpdateMainCameraHiZHistory(VkExtent2D extent)
{
	if (!m_Camera)
	{
		m_MainCameraHiZHistory.invalidFrames = 2;
		m_MainCameraHiZHistory.hasValidHistory = false;
		return;
	}

	const glm::mat4 view = m_Camera->GetViewMatrix();
	const glm::mat4 viewProjection = m_Camera->GetProjectiveMatrix() * view;
	const glm::vec3 cameraPosition = glm::vec3(m_Camera->GetPosition());
	const glm::vec3 cameraForward = SafeNormalize(glm::vec3(m_Camera->GetForward()), glm::vec3(0.0f, 0.0f, -1.0f));

	bool invalidate = !m_MainCameraHiZHistory.hasValidHistory;
	if (m_MainCameraHiZHistory.hasValidHistory)
	{
		const VansMainCameraHiZCullSettings& settings = m_MainCameraHiZCullSettings;
		const float moved = glm::length(cameraPosition - m_MainCameraHiZHistory.previousCameraPosition);
		const float dotForward = std::clamp(glm::dot(cameraForward, m_MainCameraHiZHistory.previousCameraForward), -1.0f, 1.0f);
		const float angle = std::acos(dotForward);
		invalidate |= moved > settings.cameraMotionDisableDistance;
		invalidate |= angle > settings.cameraMotionDisableAngleRadians;
		invalidate |= extent.width != m_MainCameraHiZHistory.previousExtent.width ||
			extent.height != m_MainCameraHiZHistory.previousExtent.height;
		invalidate |= !NearlyEqual(m_Camera->GetFov(), m_MainCameraHiZHistory.previousFov, 0.5f);
		invalidate |= !NearlyEqual(m_Camera->GetNearClip(), m_MainCameraHiZHistory.previousNearClip, 0.001f);
		invalidate |= !NearlyEqual(m_Camera->GetFarClip(), m_MainCameraHiZHistory.previousFarClip, 5.0f);
	}

	if (invalidate)
	{
		m_MainCameraHiZHistory.invalidFrames = std::max(m_MainCameraHiZHistory.invalidFrames, 2u);
		m_MainCameraDrawVisibilityByNodeId.clear();
	}
	else if (m_MainCameraHiZHistory.invalidFrames > 0)
	{
		--m_MainCameraHiZHistory.invalidFrames;
	}

	m_MainCameraHiZHistory.previousViewProjection = viewProjection;
	m_MainCameraHiZHistory.previousCameraPosition = cameraPosition;
	m_MainCameraHiZHistory.previousCameraForward = cameraForward;
	m_MainCameraHiZHistory.previousExtent = extent;
	m_MainCameraHiZHistory.previousFov = m_Camera->GetFov();
	m_MainCameraHiZHistory.previousNearClip = m_Camera->GetNearClip();
	m_MainCameraHiZHistory.previousFarClip = m_Camera->GetFarClip();
	m_MainCameraHiZHistory.hasValidHistory = true;
	m_MainCameraVisibilityStats.historyValid = m_MainCameraHiZHistory.invalidFrames == 0;
}

bool VansGraphics::VansScene::ShouldMainCameraCullClassRunHiZ(VansMainCameraCullClass cullClass) const
{
	const VansMainCameraHiZCullSettings& s = m_MainCameraHiZCullSettings;
	if (!s.enabled)
		return false;
	switch (cullClass)
	{
	case VansMainCameraCullClass::Opaque:
		return s.enableOpaque;
	case VansMainCameraCullClass::Hair:
		return s.enableHair;
	case VansMainCameraCullClass::Transparent:
		return s.enableTransparent;
	case VansMainCameraCullClass::ForwardOpaqueAfterDeferred:
		return s.enableForwardOpaqueAfterDeferred;
	case VansMainCameraCullClass::Decal:
		return s.enableDecal;
	default:
		return false;
	}
}

void VansGraphics::VansScene::AppendMainCameraCullCandidate(
	VansRenderNode* node,
	VansMainCameraCullClass cullClass,
	const glm::mat4& viewProjection)
{
	if (node == nullptr || !node->IsEnabled())
		return;

	VansRenderBounds bounds;
	if (!TryGetStaticNodeWorldBounds(node, bounds))
	{
		++m_MainCameraVisibilityStats.forcedVisibleCount;
		return;
	}
	if (!RenderBoundsIntersectsClipFrustum(bounds, viewProjection))
		return;

	++m_MainCameraVisibilityStats.frustumVisibleCount;
	if (!ShouldMainCameraCullClassRunHiZ(cullClass))
		return;

	const uint64_t nodeId = MakeMainCameraCullNodeId(node);
	uint32_t flags = 0;
	if (cullClass == VansMainCameraCullClass::Transparent)
		flags |= VANS_MAIN_CAMERA_CULL_TRANSPARENT;

	auto previousBounds = m_MainCameraPreviousCullBounds.find(nodeId);
	if (previousBounds == m_MainCameraPreviousCullBounds.end() ||
		RenderBoundsChanged(bounds, previousBounds->second, 1.0e-3f))
	{
		m_MainCameraForceVisibleFramesByNodeId[nodeId] =
			std::max(m_MainCameraHiZCullSettings.forceVisibleFramesAfterChange, 1u);
		m_MainCameraPreviousCullBounds[nodeId] = bounds;
	}

	auto forceIt = m_MainCameraForceVisibleFramesByNodeId.find(nodeId);
	if (forceIt != m_MainCameraForceVisibleFramesByNodeId.end() && forceIt->second > 0)
	{
		flags |= VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE;
		--forceIt->second;
		++m_MainCameraVisibilityStats.forcedVisibleCount;
	}

	if (m_MainCameraHiZHistory.invalidFrames > 0)
		flags |= VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE;

	const uint32_t refreshEvery = std::max(m_MainCameraHiZCullSettings.refreshCulledEveryNFrames, 1u);
	if ((m_MainCameraFrameIndex % refreshEvery) == 0)
		flags |= VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE;

	VansMainCameraCullCandidate candidate;
	candidate.nodeId = nodeId;
	candidate.node = node;
	candidate.bounds = bounds;
	candidate.cullClass = cullClass;
	candidate.flags = flags;
	candidate.visibilityIndex = static_cast<uint32_t>(m_MainCameraCullCandidates.size());

	VansMainCameraCullObjectGPU gpuObject;
	gpuObject.center = glm::vec4(bounds.obb.center, 1.0f);
	gpuObject.axisXHalf = glm::vec4(bounds.obb.axisX * bounds.obb.halfExtent.x, 0.0f);
	gpuObject.axisYHalf = glm::vec4(bounds.obb.axisY * bounds.obb.halfExtent.y, 0.0f);
	gpuObject.axisZHalf = glm::vec4(bounds.obb.axisZ * bounds.obb.halfExtent.z, 0.0f);
	gpuObject.nodeIndex = candidate.visibilityIndex;
	gpuObject.flags = flags;

	m_MainCameraCullIndexByNodeId[nodeId] = candidate.visibilityIndex;
	m_MainCameraCullCandidates.push_back(candidate);
	m_MainCameraCullObjectsGPU.push_back(gpuObject);
}

void VansGraphics::VansScene::BuildMainCameraCullCandidates(VkExtent2D extent)
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderPrep);

	ConsumeMainCameraHiZReadback();
	m_MainCameraCullCandidates.clear();
	m_MainCameraCullObjectsGPU.clear();
	m_MainCameraCullIndexByNodeId.clear();
	m_MainCameraLastDispatchedNodeIds.clear();
	m_MainCameraVisibilityStats.candidateCount = 0;
	m_MainCameraVisibilityStats.frustumVisibleCount = 0;
	m_MainCameraVisibilityStats.forcedVisibleCount = 0;
	m_MainCameraVisibilityStats.enabled = m_MainCameraHiZCullSettings.enabled;

	if (!m_MainCameraHiZCullSettings.enabled || m_Camera == nullptr)
	{
		m_MainCameraDrawVisibilityByNodeId.clear();
		m_MainCameraHiZCulledDebugNodes.clear();
		return;
	}

	UpdateMainCameraHiZHistory(extent);

	const glm::mat4 viewProjection = m_Camera->GetProjectiveMatrix() * m_Camera->GetViewMatrix();
	for (auto* node : m_OpaqueRenderNodes)
		AppendMainCameraCullCandidate(node, VansMainCameraCullClass::Opaque, viewProjection);
	for (auto* node : m_HairRenderNodes)
		AppendMainCameraCullCandidate(node, VansMainCameraCullClass::Hair, viewProjection);
	for (auto* node : m_ForwardOpaqueAfterDeferredRenderNodes)
		AppendMainCameraCullCandidate(node, VansMainCameraCullClass::ForwardOpaqueAfterDeferred, viewProjection);
	for (auto* node : m_TransParentRenderNodes)
		AppendMainCameraCullCandidate(node, VansMainCameraCullClass::Transparent, viewProjection);
	for (auto* node : m_DecalRenderNodes)
		AppendMainCameraCullCandidate(node, VansMainCameraCullClass::Decal, viewProjection);

	m_MainCameraVisibilityStats.candidateCount = static_cast<uint32_t>(m_MainCameraCullCandidates.size());
	++m_MainCameraFrameIndex;
}

bool VansGraphics::VansScene::UploadMainCameraCullCandidates(VansVKDevice& device)
{
	const uint32_t count = static_cast<uint32_t>(m_MainCameraCullObjectsGPU.size());
	if (count == 0)
		return true;
	if (!EnsureMainCameraHiZGpuResources(device, count))
		return false;
	const VkDeviceSize objectBytes = sizeof(VansMainCameraCullObjectGPU) * static_cast<VkDeviceSize>(count);
	const VkDeviceSize visibilityBytes = sizeof(uint32_t) * static_cast<VkDeviceSize>(count);
	if (!m_MainCameraCullObjectBuffer.SetBufferData(m_MainCameraCullObjectsGPU.data(), 0, objectBytes))
		return false;
	std::vector<uint32_t> visible(count, 1u);
	if (!m_MainCameraVisibilityBuffer.SetBufferData(visible.data(), 0, visibilityBytes))
		return false;
	return true;
}

bool VansGraphics::VansScene::IsMainCameraNodeVisible(VansRenderNode* node) const
{
	if (!m_MainCameraHiZCullSettings.enabled || node == nullptr)
		return true;
	const auto it = m_MainCameraDrawVisibilityByNodeId.find(MakeMainCameraCullNodeId(node));
	return it == m_MainCameraDrawVisibilityByNodeId.end() ? true : it->second;
}

void VansGraphics::VansScene::MarkMainCameraHiZCullDispatched()
{
	m_MainCameraLastDispatchedNodeIds.clear();
	m_MainCameraLastDispatchedNodeIds.reserve(m_MainCameraCullCandidates.size());
	for (const VansMainCameraCullCandidate& candidate : m_MainCameraCullCandidates)
		m_MainCameraLastDispatchedNodeIds.push_back(candidate.nodeId);
	m_MainCameraHasPendingVisibilityReadback = !m_MainCameraLastDispatchedNodeIds.empty();
}
