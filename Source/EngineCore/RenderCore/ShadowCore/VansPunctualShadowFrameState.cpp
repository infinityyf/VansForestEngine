#include "VansPunctualShadowFrameState.h"

#include "../VansRenderBounds.h"

#include <algorithm>
#include <unordered_set>

namespace
{
	VansGraphics::VansShadowAABB ToShadowAABB(
		const VansGraphics::VansRenderAABB& bounds)
	{
		VansGraphics::VansShadowAABB result;
		result.min = bounds.min;
		result.max = bounds.max;
		return result;
	}
}

VansGraphics::VansPunctualShadowFrameState::VansPunctualShadowFrameState()
{
	PublishDebugSnapshot();
}

bool VansGraphics::VansPunctualShadowFrameState::ValidateFrameInput(
	const VansRenderSceneFrameSnapshot& snapshot) const
{
	if (!snapshot.sceneReady)
		return true;
	if (!snapshot.light.IsComplete() || !snapshot.punctualShadow.IsComplete())
		return false;

	std::unordered_set<std::uint32_t> stableLightIds;
	stableLightIds.reserve(snapshot.punctualShadow.lights.size());
	for (const VansPunctualShadowLightInput& input : snapshot.punctualShadow.lights)
	{
		if (input.stableLightId == 0 || !stableLightIds.insert(input.stableLightId).second)
			return false;
		switch (input.type)
		{
		case VansPunctualShadowLightType::Point:
			if (input.gpuLightIndex >= snapshot.light.pointLights.size()) return false;
			break;
		case VansPunctualShadowLightType::Spot:
			if (input.gpuLightIndex >= snapshot.light.spotLights.size()) return false;
			break;
		case VansPunctualShadowLightType::Rect:
			if (input.gpuLightIndex >= snapshot.light.rectLights.size()) return false;
			break;
		default:
			return false;
		}
	}

	std::unordered_set<VansRenderProxyHandle, VansRenderProxyHandleHash> casterHandles;
	casterHandles.reserve(snapshot.punctualShadow.casters.size());
	for (const VansRenderPunctualShadowCasterInput& caster : snapshot.punctualShadow.casters)
	{
		if (!caster.proxy.IsValid() || !casterHandles.insert(caster.proxy).second)
			return false;
	}
	return true;
}

bool VansGraphics::VansPunctualShadowFrameState::PrepareFrame(
	VansRenderSceneFrameSnapshot& snapshot,
	std::uint64_t frameIndex)
{
	if (!ValidateFrameInput(snapshot))
		return false;

	if (!m_HasSceneEpoch || m_SceneEpoch != snapshot.sceneEpoch)
	{
		m_Manager.Reset();
		m_Casters.clear();
		m_SceneEpoch = snapshot.sceneEpoch;
		m_HasSceneEpoch = true;
	}

	if (!snapshot.sceneReady)
	{
		snapshot.punctualShadowJobs.clear();
		snapshot.features.hasPunctualShadowJobs = false;
		PublishDebugSnapshot();
		return true;
	}

	UpdateCasterState(snapshot.punctualShadow.casters);
	m_Manager.PrepareFrame(
		snapshot.punctualShadow.camera,
		snapshot.punctualShadow.lights,
		frameIndex);
	ResolveLightMetaIndices(snapshot.punctualShadow.lights, snapshot.light);
	snapshot.punctualShadowJobs = m_Manager.GetRenderJobs();
	BuildCasterLists(snapshot.punctualShadowJobs);
	snapshot.features.hasPunctualShadowJobs = !snapshot.punctualShadowJobs.empty();
	PublishDebugSnapshot();
	return true;
}

void VansGraphics::VansPunctualShadowFrameState::UpdateCasterState(
	const std::vector<VansRenderPunctualShadowCasterInput>& casters)
{
	std::unordered_map<
		VansRenderProxyHandle,
		VansRenderPunctualShadowCasterInput,
		VansRenderProxyHandleHash> current;
	current.reserve(casters.size());
	bool requiresGlobalInvalidation = false;

	for (const VansRenderPunctualShadowCasterInput& caster : casters)
	{
		const auto previous = m_Casters.find(caster.proxy);
		if (!caster.hasBounds)
		{
			requiresGlobalInvalidation = true;
		}
		else if (previous == m_Casters.end())
		{
			m_Manager.InvalidateCastersInBounds(
				{}, ToShadowAABB(caster.bounds.aabb), VansShadowDirty_CasterGeometry);
		}
		else if (caster.dynamic || RenderBoundsChanged(previous->second.bounds, caster.bounds))
		{
			m_Manager.InvalidateCastersInBounds(
				ToShadowAABB(previous->second.bounds.aabb),
				ToShadowAABB(caster.bounds.aabb),
				caster.dynamic ? VansShadowDirty_DynamicCaster : VansShadowDirty_CasterTransform);
		}
		else if (previous->second.shadowCasterMask != caster.shadowCasterMask)
		{
			const VansShadowAABB bounds = ToShadowAABB(caster.bounds.aabb);
			m_Manager.InvalidateCastersInBounds(
				bounds, bounds, VansShadowDirty_CasterMaterial);
		}
		current.emplace(caster.proxy, caster);
	}

	for (const auto& previous : m_Casters)
	{
		if (current.find(previous.first) != current.end())
			continue;
		if (previous.second.hasBounds)
		{
			m_Manager.InvalidateCastersInBounds(
				ToShadowAABB(previous.second.bounds.aabb), {}, VansShadowDirty_CasterGeometry);
		}
		else
		{
			requiresGlobalInvalidation = true;
		}
	}
	if (requiresGlobalInvalidation)
		m_Manager.InvalidateAllCasters(VansShadowDirty_CasterGeometry);
	m_Casters.swap(current);
}

void VansGraphics::VansPunctualShadowFrameState::BuildCasterLists(
	std::vector<VansPunctualShadowRenderJob>& jobs) const
{
	for (VansPunctualShadowRenderJob& job : jobs)
	{
		job.casterHandles.clear();
		job.casterHandles.reserve(m_Casters.size());
		for (const auto& caster : m_Casters)
		{
			if ((caster.second.shadowCasterMask & job.shadowCasterMask) == 0u)
				continue;
			if (!caster.second.hasBounds ||
				RenderBoundsIntersectsClipFrustum(caster.second.bounds, job.worldToShadow))
			{
				job.casterHandles.push_back(caster.first);
			}
		}
	}
}

void VansGraphics::VansPunctualShadowFrameState::ResolveLightMetaIndices(
	const std::vector<VansPunctualShadowLightInput>& inputs,
	VansRenderLightFrameData& lightFrame) const
{
	for (VansPointLight& light : lightFrame.pointLights)
		light.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	for (VansSpotLight& light : lightFrame.spotLights)
		light.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;
	for (VansRectLight& light : lightFrame.rectLights)
		light.m_ShadowMetaIndex = VANS_INVALID_SHADOW_INDEX;

	for (const VansPunctualShadowLightInput& input : inputs)
	{
		const std::uint32_t metaIndex = m_Manager.GetShadowMetaIndex(input.stableLightId);
		switch (input.type)
		{
		case VansPunctualShadowLightType::Point:
			lightFrame.pointLights[input.gpuLightIndex].m_ShadowMetaIndex = metaIndex;
			break;
		case VansPunctualShadowLightType::Spot:
			lightFrame.spotLights[input.gpuLightIndex].m_ShadowMetaIndex = metaIndex;
			break;
		case VansPunctualShadowLightType::Rect:
			lightFrame.rectLights[input.gpuLightIndex].m_ShadowMetaIndex = metaIndex;
			break;
		}
	}
}

void VansGraphics::VansPunctualShadowFrameState::NotifyRenderJobsSubmitted()
{
	m_Manager.NotifyRenderJobsSubmitted();
	PublishDebugSnapshot();
}

void VansGraphics::VansPunctualShadowFrameState::RequestDebugPreview()
{
	m_DebugPreviewRequested.store(true, std::memory_order_release);
}

bool VansGraphics::VansPunctualShadowFrameState::ConsumeDebugPreviewRefreshRequest()
{
	if (m_DebugPreviewRequested.exchange(false, std::memory_order_acq_rel))
	{
		if (m_DebugPreviewHeartbeat == 0)
			m_DebugPreviewForceRefresh = true;
		m_DebugPreviewHeartbeat = 3;
	}
	if (m_DebugPreviewHeartbeat == 0)
		return false;

	const bool shouldRefresh =
		m_DebugPreviewForceRefresh || m_Manager.GetStatistics().renderedViews > 0;
	m_DebugPreviewForceRefresh = false;
	--m_DebugPreviewHeartbeat;
	return shouldRefresh;
}

void VansGraphics::VansPunctualShadowFrameState::PublishDebugSnapshot()
{
	VansPunctualShadowDebugSnapshot snapshot = m_Manager.CaptureDebugSnapshot();
	const std::uint32_t totalPages = m_Manager.GetTotalAtlasPages();
	std::lock_guard<std::mutex> lock(m_DebugSnapshotMutex);
	m_PublishedDebugSnapshot = std::move(snapshot);
	m_PublishedTotalAtlasPages = totalPages;
}

VansGraphics::VansPunctualShadowDebugSnapshot
VansGraphics::VansPunctualShadowFrameState::CaptureDebugSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_DebugSnapshotMutex);
	return m_PublishedDebugSnapshot;
}

std::uint32_t VansGraphics::VansPunctualShadowFrameState::GetTotalAtlasPages() const
{
	std::lock_guard<std::mutex> lock(m_DebugSnapshotMutex);
	return m_PublishedTotalAtlasPages;
}
