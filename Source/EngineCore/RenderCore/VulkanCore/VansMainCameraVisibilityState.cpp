#include "VansMainCameraVisibilityState.h"

#include "../VansRenderFrame.h"
#include "VansVKDevice.h"
#include "../../RuntimeCore/VansFramePhase.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cmath>
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

VansGraphics::VansMainCameraVisibilityState::FrameSlot&
VansGraphics::VansMainCameraVisibilityState::ActiveSlot()
{
	return m_FrameSlots[m_ActiveFrameSlotIndex];
}

const VansGraphics::VansMainCameraVisibilityState::FrameSlot&
VansGraphics::VansMainCameraVisibilityState::ActiveSlot() const
{
	return m_FrameSlots[m_ActiveFrameSlotIndex];
}

void VansGraphics::VansMainCameraVisibilityState::ReleaseSlotGpuResources(
	FrameSlot& slot,
	VkDevice device)
{
	if (slot.cullObjectBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
		slot.cullObjectBuffer.DestroyVulkanBuffer(device);
	if (slot.visibilityBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
		slot.visibilityBuffer.DestroyVulkanBuffer(device);
	slot.bufferCapacity = 0;
	slot.pendingVisibilityReadback = false;
}

bool VansGraphics::VansMainCameraVisibilityState::EnsureActiveGpuResources(
	VansVKDevice& device,
	uint32_t candidateCount)
{
	if (candidateCount == 0)
		return true;

	FrameSlot& slot = ActiveSlot();
	if (candidateCount <= slot.bufferCapacity &&
		slot.cullObjectBuffer.GetNativeBuffer() != VK_NULL_HANDLE &&
		slot.visibilityBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		return true;
	}

	const uint32_t newCapacity = std::max(candidateCount, std::max(128u, slot.bufferCapacity * 2u));
	VkDevice nativeDevice = device.GetLogicDevice();
	ReleaseSlotGpuResources(slot, nativeDevice);

	const VkDeviceSize objectBytes = sizeof(VansMainCameraCullObjectGPU) * static_cast<VkDeviceSize>(newCapacity);
	const VkDeviceSize visibilityBytes = sizeof(uint32_t) * static_cast<VkDeviceSize>(newCapacity);
	const bool objectOk = slot.cullObjectBuffer.CreatVulkanBuffer(
		nativeDevice,
		objectBytes,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	const bool visibilityOk = slot.visibilityBuffer.CreatVulkanBuffer(
		nativeDevice,
		visibilityBytes,
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	if (!objectOk || !visibilityOk)
	{
		ReleaseSlotGpuResources(slot, nativeDevice);
		VANS_LOG_ERROR("[MainCameraHiZCull] Failed to create cull buffers.");
		return false;
	}

	slot.bufferCapacity = newCapacity;
	return true;
}

void VansGraphics::VansMainCameraVisibilityState::ConsumeActiveReadback()
{
	FrameSlot& slot = ActiveSlot();
	if (!slot.pendingVisibilityReadback)
		return;
	if (slot.visibilityBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
		!slot.visibilityBuffer.IsMapped())
	{
		slot.pendingVisibilityReadback = false;
		return;
	}

	const uint32_t count = std::min(
		static_cast<uint32_t>(slot.dispatchedNodeIds.size()),
		slot.bufferCapacity);
	if (count == 0)
	{
		slot.pendingVisibilityReadback = false;
		return;
	}
	if (slot.dispatchedFrameSerial <= m_LastConsumedFrameSerial)
	{
		slot.pendingVisibilityReadback = false;
		return;
	}

	slot.visibilityBuffer.InvalidateMappedRange(0, sizeof(uint32_t) * count);
	const auto* visibility = static_cast<const uint32_t*>(slot.visibilityBuffer.GetMappedPtr());
	m_DrawVisibilityByNodeId.clear();
	m_CulledDebugNodes.clear();
	uint32_t culledCount = 0;
	m_CulledDebugNodes.reserve(count);
	for (uint32_t index = 0; index < count; ++index)
	{
		const bool visible = visibility[index] != 0;
		const uint64_t nodeId = slot.dispatchedNodeIds[index];
		m_DrawVisibilityByNodeId[nodeId] = visible;
		if (!visible)
		{
			++culledCount;
			if (index < slot.candidates.size())
			{
				const VansMainCameraCullCandidate& candidate = slot.candidates[index];
				VansMainCameraHiZCulledNodeDebug debugNode;
				debugNode.nodeId = nodeId;
				debugNode.nodeName = candidate.nodeName;
				debugNode.cullClass = candidate.cullClass;
				debugNode.bounds = candidate.bounds;
				m_CulledDebugNodes.push_back(std::move(debugNode));
			}
		}
	}
	m_Stats.hizCulledCount = culledCount;
	m_LastConsumedFrameSerial = slot.dispatchedFrameSerial;
	slot.pendingVisibilityReadback = false;
}

void VansGraphics::VansMainCameraVisibilityState::UpdateHistory(
	const VansRenderViewSnapshot& viewSnapshot)
{
	const glm::uvec2 extent{ viewSnapshot.viewportWidth, viewSnapshot.viewportHeight };
	const glm::mat4 viewProjection = viewSnapshot.projection * viewSnapshot.view;
	const glm::vec3 cameraPosition = viewSnapshot.position;
	const glm::vec3 cameraForward = SafeNormalize(
		viewSnapshot.forward,
		glm::vec3(0.0f, 0.0f, -1.0f));

	bool invalidate = !m_History.hasValidHistory;
	if (m_History.hasValidHistory)
	{
		const VansMainCameraHiZCullSettings& settings = m_Settings;
		const float moved = glm::length(cameraPosition - m_History.previousCameraPosition);
		const float dotForward = std::clamp(glm::dot(cameraForward, m_History.previousCameraForward), -1.0f, 1.0f);
		const float angle = std::acos(dotForward);
		invalidate |= moved > settings.cameraMotionDisableDistance;
		invalidate |= angle > settings.cameraMotionDisableAngleRadians;
		invalidate |= extent.x != m_History.previousExtent.x ||
			extent.y != m_History.previousExtent.y;
		invalidate |= !NearlyEqual(
			viewSnapshot.fieldOfViewRadians,
			m_History.previousFov,
			glm::radians(0.5f));
		invalidate |= !NearlyEqual(
			viewSnapshot.nearClip,
			m_History.previousNearClip,
			0.001f);
		invalidate |= !NearlyEqual(
			viewSnapshot.farClip,
			m_History.previousFarClip,
			5.0f);
	}

	if (invalidate)
	{
		m_History.invalidFrames = std::max(m_History.invalidFrames, 2u);
		m_DrawVisibilityByNodeId.clear();
	}
	else if (m_History.invalidFrames > 0)
	{
		--m_History.invalidFrames;
	}

	m_History.previousViewProjection = viewProjection;
	m_History.previousCameraPosition = cameraPosition;
	m_History.previousCameraForward = cameraForward;
	m_History.previousExtent = extent;
	m_History.previousFov = viewSnapshot.fieldOfViewRadians;
	m_History.previousNearClip = viewSnapshot.nearClip;
	m_History.previousFarClip = viewSnapshot.farClip;
	m_History.hasValidHistory = true;
	m_Stats.historyValid = m_History.invalidFrames == 0;
}

bool VansGraphics::VansMainCameraVisibilityState::ShouldCullClassRunHiZ(
	VansMainCameraCullClass cullClass) const
{
	const VansMainCameraHiZCullSettings& s = m_Settings;
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
	case VansMainCameraCullClass::ForwardOpaquePreAtmosphere:
		return s.enableForwardOpaquePreAtmosphere;
	case VansMainCameraCullClass::Decal:
		return s.enableDecal;
	default:
		return false;
	}
}

void VansGraphics::VansMainCameraVisibilityState::AppendCandidate(
	FrameSlot& slot,
	const VansRenderMainCameraCullInput& input,
	const glm::mat4& viewProjection)
{
	if (!input.proxy.IsValid() || !input.hasBounds || !input.bounds.IsValid())
	{
		if (input.proxy.IsValid())
			m_CurrentFrustumVisibilityByNodeId[MakeRenderProxyStableId(input.proxy)] = true;
		++m_Stats.forcedVisibleCount;
		return;
	}
	const VansRenderBounds& bounds = input.bounds;
	const uint64_t nodeId = MakeRenderProxyStableId(input.proxy);
	const bool frustumVisible = RenderBoundsIntersectsClipFrustum(bounds, viewProjection);
	m_CurrentFrustumVisibilityByNodeId[nodeId] = frustumVisible;
	if (!frustumVisible)
		return;

	++m_Stats.frustumVisibleCount;
	if (!ShouldCullClassRunHiZ(input.cullClass))
		return;

	uint32_t flags = 0;
	if (input.cullClass == VansMainCameraCullClass::Transparent)
		flags |= VANS_MAIN_CAMERA_CULL_TRANSPARENT;

	auto previousBounds = m_PreviousCullBounds.find(nodeId);
	if (previousBounds == m_PreviousCullBounds.end() ||
		RenderBoundsChanged(bounds, previousBounds->second, 1.0e-3f))
	{
		m_ForceVisibleFramesByNodeId[nodeId] =
			std::max(m_Settings.forceVisibleFramesAfterChange, 1u);
		m_PreviousCullBounds[nodeId] = bounds;
	}

	auto forceIt = m_ForceVisibleFramesByNodeId.find(nodeId);
	if (forceIt != m_ForceVisibleFramesByNodeId.end() && forceIt->second > 0)
	{
		flags |= VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE;
		--forceIt->second;
		++m_Stats.forcedVisibleCount;
	}

	if (m_History.invalidFrames > 0)
		flags |= VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE;

	const uint32_t refreshEvery = std::max(m_Settings.refreshCulledEveryNFrames, 1u);
	if ((m_FrameIndex % refreshEvery) == 0)
		flags |= VANS_MAIN_CAMERA_CULL_FORCE_VISIBLE;

	VansMainCameraCullCandidate candidate;
	candidate.nodeId = nodeId;
	candidate.nodeName = input.nodeName;
	candidate.bounds = bounds;
	candidate.cullClass = input.cullClass;
	candidate.flags = flags;
	candidate.visibilityIndex = static_cast<uint32_t>(slot.candidates.size());

	VansMainCameraCullObjectGPU gpuObject;
	gpuObject.center = glm::vec4(bounds.obb.center, 1.0f);
	gpuObject.axisXHalf = glm::vec4(bounds.obb.axisX * bounds.obb.halfExtent.x, 0.0f);
	gpuObject.axisYHalf = glm::vec4(bounds.obb.axisY * bounds.obb.halfExtent.y, 0.0f);
	gpuObject.axisZHalf = glm::vec4(bounds.obb.axisZ * bounds.obb.halfExtent.z, 0.0f);
	gpuObject.nodeIndex = candidate.visibilityIndex;
	gpuObject.flags = flags;

	m_CurrentCullIndexByNodeId[nodeId] = candidate.visibilityIndex;
	slot.candidates.push_back(std::move(candidate));
	slot.gpuObjects.push_back(gpuObject);
}

void VansGraphics::VansMainCameraVisibilityState::PrepareFrame(
	const VansRenderViewSnapshot& view,
	const VansRenderSceneFrameSnapshot& sceneSnapshot,
	uint32_t frameSlotIndex,
	uint64_t frameSerial)
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderThreadConsume);
	m_ActiveFrameSlotIndex = frameSlotIndex % kFrameSlotCount;
	m_CurrentFrameSerial = frameSerial;
	FrameSlot& slot = ActiveSlot();

	ConsumeActiveReadback();
	slot.candidates.clear();
	slot.gpuObjects.clear();
	slot.dispatchedNodeIds.clear();
	m_CurrentCullIndexByNodeId.clear();
	m_CurrentFrustumVisibilityByNodeId.clear();
	m_Stats.candidateCount = 0;
	m_Stats.frustumVisibleCount = 0;
	m_Stats.forcedVisibleCount = 0;
	m_Settings = sceneSnapshot.mainCameraHiZCullSettings;
	m_Stats.enabled = sceneSnapshot.sceneReady && m_Settings.enabled;
	m_DrawCallStatsFrameSerial.store(frameSerial, std::memory_order_release);
	m_DrawCallStatsPacked.store(0, std::memory_order_relaxed);

	if (!m_Stats.enabled)
	{
		m_DrawVisibilityByNodeId.clear();
		m_CulledDebugNodes.clear();
		m_PreviousCullBounds.clear();
		m_ForceVisibleFramesByNodeId.clear();
		m_History = VansMainCameraHiZHistoryState{};
		m_Stats.historyValid = false;
		m_Stats.hizCulledCount = 0;
		for (FrameSlot& frameSlot : m_FrameSlots)
			frameSlot.pendingVisibilityReadback = false;
		PublishDebugSnapshot();
		return;
	}

	UpdateHistory(view);

	const glm::mat4 viewProjection = view.projection * view.view;
	for (const VansRenderMainCameraCullInput& input : sceneSnapshot.mainCameraCullInputs)
		AppendCandidate(slot, input, viewProjection);

	m_Stats.candidateCount = static_cast<uint32_t>(slot.candidates.size());
	++m_FrameIndex;
	PublishDebugSnapshot();
}

bool VansGraphics::VansMainCameraVisibilityState::UploadActiveCandidates(VansVKDevice& device)
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);
	FrameSlot& slot = ActiveSlot();
	const uint32_t count = static_cast<uint32_t>(slot.gpuObjects.size());
	if (count == 0)
		return true;
	if (!EnsureActiveGpuResources(device, count))
		return false;
	const VkDeviceSize objectBytes = sizeof(VansMainCameraCullObjectGPU) * static_cast<VkDeviceSize>(count);
	const VkDeviceSize visibilityBytes = sizeof(uint32_t) * static_cast<VkDeviceSize>(count);
	if (!slot.cullObjectBuffer.SetBufferData(slot.gpuObjects.data(), 0, objectBytes))
		return false;
	std::vector<uint32_t> visible(count, 1u);
	if (!slot.visibilityBuffer.SetBufferData(visible.data(), 0, visibilityBytes))
		return false;
	return true;
}

bool VansGraphics::VansMainCameraVisibilityState::HasActiveCandidates() const
{
	return !ActiveSlot().candidates.empty();
}

uint32_t VansGraphics::VansMainCameraVisibilityState::GetActiveCandidateCount() const
{
	return static_cast<uint32_t>(ActiveSlot().candidates.size());
}

VansGraphics::VansVKBuffer&
VansGraphics::VansMainCameraVisibilityState::GetActiveCullObjectBuffer()
{
	return ActiveSlot().cullObjectBuffer;
}

VansGraphics::VansVKBuffer&
VansGraphics::VansMainCameraVisibilityState::GetActiveVisibilityBuffer()
{
	return ActiveSlot().visibilityBuffer;
}

bool VansGraphics::VansMainCameraVisibilityState::ShouldDraw(VansRenderProxyHandle proxy)
{
	if (!m_Settings.enabled || !proxy.IsValid())
		return true;
	const uint64_t nodeId = MakeRenderProxyStableId(proxy);
	const auto frustumIt = m_CurrentFrustumVisibilityByNodeId.find(nodeId);
	if (frustumIt != m_CurrentFrustumVisibilityByNodeId.end() && !frustumIt->second)
		return false;
	const auto visibilityIt = m_DrawVisibilityByNodeId.find(nodeId);
	const bool visible = visibilityIt == m_DrawVisibilityByNodeId.end()
		? true
		: visibilityIt->second;
	if (m_CurrentCullIndexByNodeId.find(nodeId) == m_CurrentCullIndexByNodeId.end())
		return visible;

	const uint64_t preCullIncrement = 1u;
	const uint64_t culledIncrement = visible ? 0u : (uint64_t{ 1 } << 32u);
	m_DrawCallStatsPacked.fetch_add(
		preCullIncrement | culledIncrement,
		std::memory_order_relaxed);
	return visible;
}

void VansGraphics::VansMainCameraVisibilityState::MarkDispatched()
{
	FrameSlot& slot = ActiveSlot();
	slot.dispatchedNodeIds.clear();
	slot.dispatchedNodeIds.reserve(slot.candidates.size());
	for (const VansMainCameraCullCandidate& candidate : slot.candidates)
		slot.dispatchedNodeIds.push_back(candidate.nodeId);
	slot.dispatchedFrameSerial = m_CurrentFrameSerial;
	slot.pendingVisibilityReadback = !slot.dispatchedNodeIds.empty();
}

void VansGraphics::VansMainCameraVisibilityState::PublishDebugSnapshot()
{
	std::lock_guard<std::mutex> lock(m_DebugMutex);
	m_PublishedDebugSnapshot.stats = m_Stats;
	m_PublishedDebugSnapshot.culledNodes = m_CulledDebugNodes;
	m_PublishedDebugFrameSerial = m_CurrentFrameSerial;
}

VansGraphics::VansMainCameraVisibilityDebugSnapshot
VansGraphics::VansMainCameraVisibilityState::GetDebugSnapshot() const
{
	VansMainCameraVisibilityDebugSnapshot snapshot;
	uint64_t publishedFrameSerial = 0;
	{
		std::lock_guard<std::mutex> lock(m_DebugMutex);
		snapshot = m_PublishedDebugSnapshot;
		publishedFrameSerial = m_PublishedDebugFrameSerial;
	}
	const uint64_t drawCallFrameSerial =
		m_DrawCallStatsFrameSerial.load(std::memory_order_acquire);
	const uint64_t packed = drawCallFrameSerial == publishedFrameSerial
		? m_DrawCallStatsPacked.load(std::memory_order_relaxed)
		: 0u;
	snapshot.stats.preCullDrawCallCount = static_cast<uint32_t>(packed);
	snapshot.stats.culledDrawCallCount = static_cast<uint32_t>(packed >> 32u);
	snapshot.stats.drawnDrawCallCount =
		snapshot.stats.preCullDrawCallCount - snapshot.stats.culledDrawCallCount;
	return snapshot;
}

void VansGraphics::VansMainCameraVisibilityState::Reset()
{
	for (FrameSlot& slot : m_FrameSlots)
	{
		slot.candidates.clear();
		slot.gpuObjects.clear();
		slot.dispatchedNodeIds.clear();
		slot.dispatchedFrameSerial = 0;
		slot.pendingVisibilityReadback = false;
	}
	m_Settings = VansMainCameraHiZCullSettings{};
	m_History = VansMainCameraHiZHistoryState{};
	m_Stats = VansMainCameraVisibilityStats{};
	m_CulledDebugNodes.clear();
	m_CurrentCullIndexByNodeId.clear();
	m_CurrentFrustumVisibilityByNodeId.clear();
	m_DrawVisibilityByNodeId.clear();
	m_PreviousCullBounds.clear();
	m_ForceVisibleFramesByNodeId.clear();
	m_DrawCallStatsFrameSerial.store(0, std::memory_order_release);
	m_DrawCallStatsPacked.store(0, std::memory_order_relaxed);
	m_LastConsumedFrameSerial = 0;
	m_CurrentFrameSerial = 0;
	m_FrameIndex = 0;
	m_ActiveFrameSlotIndex = 0;
	PublishDebugSnapshot();
}

void VansGraphics::VansMainCameraVisibilityState::ReleaseGpuResources(VkDevice device)
{
	for (FrameSlot& slot : m_FrameSlots)
		ReleaseSlotGpuResources(slot, device);
	Reset();
}
