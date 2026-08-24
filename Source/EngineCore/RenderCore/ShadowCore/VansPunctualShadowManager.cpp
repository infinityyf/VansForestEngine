#include "VansPunctualShadowManager.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace
{
	constexpr float kEpsilon = 1e-5f;
	constexpr uint32_t kMinimumResidencyFrames = 30;
	constexpr uint32_t kUpgradeConfirmationFrames = 8;
	constexpr uint32_t kDowngradeConfirmationFrames = 30;
	constexpr uint16_t kMaximumShadowResolution = 512;
	constexpr uint32_t kMaxStaleBoostFrames = 16;
	constexpr float kAtlasFadeInStep = 1.0f / 6.0f;
	constexpr float kAtlasFadeOutStep = 1.0f / 8.0f;
	constexpr uint32_t kShadowOwnerSignature = 0xA5000000u;
	constexpr uint32_t kShadowOwnerAtlasShift = 16u;

	uint32_t BuildShadowOwnerKey(
		VansGraphics::VansPunctualShadowLightType type,
		uint32_t gpuLightIndex,
		uint32_t atlasIndex)
	{
		return kShadowOwnerSignature |
			((atlasIndex & 0x3u) << kShadowOwnerAtlasShift) |
			((static_cast<uint32_t>(type) & 0x3u) << 8u) |
			(gpuLightIndex & 0xffu);
	}

	bool NearlyEqual(float a, float b, float epsilon = kEpsilon)
	{
		return std::abs(a - b) <= epsilon * (std::max)(1.0f, (std::max)(std::abs(a), std::abs(b)));
	}

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b)
	{
		return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z);
	}

	glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
	{
		if (glm::dot(value, value) > 1e-8f)
			return glm::normalize(value);
		return fallback;
	}

	glm::vec3 StableUp(const glm::vec3& forward)
	{
		const glm::vec3 axes[] = {
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(1.0f, 0.0f, 0.0f)
		};
		glm::vec3 result = axes[0];
		float best = std::abs(glm::dot(forward, axes[0]));
		for (uint32_t index = 1; index < 3; ++index)
		{
			const float score = std::abs(glm::dot(forward, axes[index]));
			if (score < best)
			{
				best = score;
				result = axes[index];
			}
		}
		return result;
	}

	uint32_t CountBits(uint8_t mask)
	{
		uint32_t count = 0;
		while (mask != 0)
		{
			count += mask & 1u;
			mask >>= 1u;
		}
		return count;
	}
}

namespace VansGraphics
{
	VansPunctualShadowManager::VansPunctualShadowManager(uint32_t atlasSize, uint32_t basePageSize, uint32_t gutter)
		: m_AtlasAllocators{
			VansShadowAtlasAllocator(atlasSize, basePageSize, gutter),
			VansShadowAtlasAllocator(atlasSize, basePageSize, gutter) }
	{
		m_Budget.atlasPageBudget = GetTotalAtlasPages();
	}

	void VansPunctualShadowManager::Reset()
	{
		m_Runtimes.clear();
		m_LightToMetaIndex.clear();
		m_GPUShadowData.clear();
		m_GPUShadowViews.clear();
		m_RenderJobs.clear();
		m_Statistics = {};
		m_NextAtomicGroupId = 1;
		for (VansShadowAtlasAllocator& allocator : m_AtlasAllocators)
		{
			allocator.Reset(
				allocator.GetAtlasSize(),
				allocator.GetBasePageSize(),
				allocator.GetGutter());
		}
	}

	void VansPunctualShadowManager::PrepareFrame(
		const VansPunctualShadowCameraData& camera,
		const std::vector<VansPunctualShadowLightInput>& lights,
		uint64_t frameIndex)
	{
		m_Camera = camera;
		m_FrameIndex = frameIndex;
		m_RenderJobs.clear();
		m_GPUShadowData.clear();
		m_GPUShadowViews.clear();
		m_LightToMetaIndex.clear();
		m_Statistics = {};

		for (auto& pair : m_Runtimes)
		{
			pair.second.seenThisFrame = false;
			pair.second.selected = false;
			// 上一帧若没有收到提交成功通知，这些 job 必须重新排队。
			pair.second.queuedActiveFaceMask = 0;
			pair.second.queuedPendingFaceMask = 0;
		}

		std::vector<Candidate> candidates;
		candidates.reserve(lights.size());

		for (const VansPunctualShadowLightInput& input : lights)
		{
			Runtime& runtime = m_Runtimes[input.stableLightId];
			const bool typeChanged = runtime.hasPreviousInput && runtime.input.type != input.type;
			const bool projectionChanged = runtime.hasPreviousInput && ProjectionChanged(runtime, input);
			const bool keepSecondaryPointCache = !typeChanged && IsSecondaryPointResident(runtime);
			if (typeChanged)
				ReleaseRuntime(runtime);
			runtime.input = input;
			runtime.requiredFaceMask = RequiredFaceMask(input.type);
			runtime.seenThisFrame = true;
			runtime.hasPreviousInput = true;

			if (!IsEligible(input, camera))
			{
				// Disabled/invalid lights must not retain or publish a stale handle.
				// Release is atomic for point-light six-face groups.
				ReleaseRuntime(runtime);
				runtime.state = VansShadowRuntimeState::Disabled;
				continue;
			}

			runtime.coverage = ComputeCoverage(input, camera);
			runtime.cameraDistance = glm::length(input.position - camera.position);
			runtime.distancePriority = ComputeDistancePriority(input, camera);
			runtime.importance = ComputeImportance(
				input,
				camera,
				runtime.activeResolution != 0,
				runtime.coverage);

			if (projectionChanged && !keepSecondaryPointCache)
			{
				runtime.dirtyFaceMask = runtime.requiredFaceMask;
				runtime.dirtyReasons |= VansShadowDirty_LightTransform | VansShadowDirty_Projection;
				runtime.projectionValid = false;
			}

			const bool updatesEveryFrame =
				(input.type == VansPunctualShadowLightType::Point &&
					runtime.activeResolution != 0 &&
					runtime.activeBlocks[0].atlasIndex == VANS_PUNCTUAL_SHADOW_PRIMARY_ATLAS_INDEX) ||
				(input.type != VansPunctualShadowLightType::Point &&
					input.settings.updateMode == VansShadowUpdateMode::EveryFrame);
			if (updatesEveryFrame && runtime.activeResolution != 0)
			{
				runtime.dirtyFaceMask = runtime.requiredFaceMask;
				runtime.dirtyReasons |= VansShadowDirty_DynamicCaster;
			}

			const uint16_t desired = ComputeDesiredResolution(input, camera, runtime.coverage);
			Candidate candidate;
			candidate.runtime = &runtime;
			candidate.resolution = keepSecondaryPointCache
				? runtime.activeResolution
				: ResolveHystereticResolution(runtime, desired);
			candidate.pageCost = PageCost(
				candidate.resolution,
				ViewCount(input.type),
				m_AtlasAllocators[0].GetBasePageSize());
			candidates.push_back(candidate);
		}

		std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
		{
			const bool aHero = a.runtime->input.settings.policy == VansShadowPolicy::Hero;
			const bool bHero = b.runtime->input.settings.policy == VansShadowPolicy::Hero;
			if (aHero != bHero)
				return aHero;
			if (!NearlyEqual(a.runtime->importance, b.runtime->importance))
				return a.runtime->importance > b.runtime->importance;
			const float aUtility = a.runtime->importance / static_cast<float>((std::max)(a.pageCost, 1u));
			const float bUtility = b.runtime->importance / static_cast<float>((std::max)(b.pageCost, 1u));
			if (!NearlyEqual(aUtility, bUtility))
				return aUtility > bUtility;
			return a.runtime->input.stableLightId < b.runtime->input.stableLightId;
		});

		uint32_t virtualPagesRemaining = (std::min)(m_Budget.atlasPageBudget, GetTotalAtlasPages());
		std::vector<Runtime*> orderedRuntimes;
		orderedRuntimes.reserve(candidates.size());

		// 最小驻留期是硬保护层，必须先计入 residency budget。旧实现先把
		// 全部预算分给 challenger，随后再追加 protected resident，导致 CPU
		// 选择结果超出 Atlas 容量并在真实分配阶段反复失败。
		for (Candidate& candidate : candidates)
		{
			Runtime& runtime = *candidate.runtime;
			if (runtime.activeResolution == 0 || runtime.residencyFrames >= kMinimumResidencyFrames)
				continue;

			runtime.selected = true;
			runtime.targetResolution = runtime.activeResolution;
			const uint32_t residentCost = PageCost(
				runtime.activeResolution,
				ViewCount(runtime.input.type),
				m_AtlasAllocators[0].GetBasePageSize());
			virtualPagesRemaining = residentCost < virtualPagesRemaining
				? virtualPagesRemaining - residentCost
				: 0u;
			orderedRuntimes.push_back(&runtime);
		}

		for (Candidate& candidate : candidates)
		{
			if (candidate.runtime->selected)
				continue;

			uint16_t resolution = candidate.resolution;
			uint32_t cost = candidate.pageCost;
			while (cost > virtualPagesRemaining && resolution > m_AtlasAllocators[0].GetBasePageSize())
			{
				resolution = DownshiftResolution(resolution);
				cost = PageCost(resolution, ViewCount(candidate.runtime->input.type), m_AtlasAllocators[0].GetBasePageSize());
			}

			if (cost > virtualPagesRemaining)
				continue;

			candidate.runtime->selected = true;
			candidate.runtime->targetResolution = resolution;
			virtualPagesRemaining -= cost;
			orderedRuntimes.push_back(candidate.runtime);
		}

		for (auto it = m_Runtimes.begin(); it != m_Runtimes.end();)
		{
			Runtime& runtime = it->second;
			if (!runtime.seenThisFrame)
			{
				ReleaseRuntime(runtime);
				it = m_Runtimes.erase(it);
				continue;
			}

			if (!runtime.selected)
			{
				// Pending allocation 不属于可采样缓存。灯光失去 residency 后必须
				// 立即回收，否则相机移动会持续留下 pending-only Atlas 泄漏。
				ReleasePending(runtime);
				if (runtime.activeResolution != 0)
				{
					runtime.state = VansShadowRuntimeState::Evicting;
					runtime.atlasWeight = (std::max)(0.0f, runtime.atlasWeight - kAtlasFadeOutStep);
					if (runtime.atlasWeight <= 0.0f)
					{
						ReleaseRuntime(runtime);
						++m_Statistics.evictions;
					}
				}
				else
				{
					runtime.state = runtime.input.settings.fallback == VansShadowFallback::ScreenSpace
						? VansShadowRuntimeState::FallbackScreenSpace
						: VansShadowRuntimeState::FallbackNone;
				}
			}

			++it;
		}

		std::sort(orderedRuntimes.begin(), orderedRuntimes.end(), [](const Runtime* a, const Runtime* b)
		{
			const bool aHero = a->input.settings.policy == VansShadowPolicy::Hero;
			const bool bHero = b->input.settings.policy == VansShadowPolicy::Hero;
			if (aHero != bHero)
				return aHero;
			return a->importance > b->importance;
		});

		// A light can become important again while its cached Atlas allocation is
		// fading out. Cancel the eviction immediately so distance-driven ownership
		// changes remain reversible and do not strand a partially weighted cache.
		for (Runtime* runtime : orderedRuntimes)
		{
			if (runtime->state == VansShadowRuntimeState::Evicting && runtime->activeResolution != 0)
			{
				runtime->state = runtime->dirtyFaceMask != 0
					? VansShadowRuntimeState::ResidentDirty
					: VansShadowRuntimeState::ResidentClean;
			}
		}

		for (Runtime* runtime : orderedRuntimes)
		{
			if (runtime->activeResolution == 0 || runtime->activeResolution != runtime->targetResolution)
			{
				uint16_t allocationResolution = runtime->targetResolution;
				for (;;)
				{
					// 降级已经回到当前 resident 分辨率时直接取消迁移，避免为
					// 同规格缓存再申请一套 Pending block。
					if (runtime->activeResolution != 0 && allocationResolution == runtime->activeResolution)
					{
						ReleasePending(*runtime);
						runtime->targetResolution = runtime->activeResolution;
						break;
					}
					if (EnsurePendingAllocation(*runtime, allocationResolution))
					{
						runtime->targetResolution = runtime->pendingResolution;
						break;
					}
					if (allocationResolution <= m_AtlasAllocators[0].GetBasePageSize())
						break;
					allocationResolution = DownshiftResolution(allocationResolution);
				}
			}
		}

		BuildRenderJobs(orderedRuntimes);
		BuildGPUData(lights);
		m_Statistics.usedAtlasPages = 0;
		for (const VansShadowAtlasAllocator& allocator : m_AtlasAllocators)
			m_Statistics.usedAtlasPages += allocator.GetUsedPages();
	}

	void VansPunctualShadowManager::NotifyRenderJobsSubmitted()
	{
		for (auto& [stableLightId, runtime] : m_Runtimes)
		{
			(void)stableLightId;
			if (runtime.pendingResolution != 0 &&
				runtime.queuedPendingFaceMask == runtime.requiredFaceMask)
			{
				runtime.activeWorldToShadow = runtime.queuedWorldToShadow;
				runtime.lastRenderedFrame = m_FrameIndex;
				runtime.staleFrames = 0;
				runtime.dirtyReasons = VansShadowDirty_None;
				PromotePending(runtime);
			}
			else if (runtime.queuedActiveFaceMask != 0)
			{
				for (uint32_t face = 0; face < ViewCount(runtime.input.type); ++face)
				{
					const uint8_t faceBit = static_cast<uint8_t>(1u << face);
					if ((runtime.queuedActiveFaceMask & faceBit) != 0)
						runtime.activeWorldToShadow[face] = runtime.queuedWorldToShadow[face];
				}
				runtime.dirtyFaceMask &= static_cast<uint8_t>(~runtime.queuedActiveFaceMask);
				runtime.lastRenderedFrame = m_FrameIndex;
				if (runtime.dirtyFaceMask == 0)
				{
					runtime.projectionValid = true;
					runtime.validFaceMask = runtime.requiredFaceMask;
					runtime.dirtyReasons = VansShadowDirty_None;
					runtime.staleFrames = 0;
					runtime.state = VansShadowRuntimeState::ResidentClean;
				}
			}

			runtime.queuedActiveFaceMask = 0;
			runtime.queuedPendingFaceMask = 0;
		}
	}

	void VansPunctualShadowManager::InvalidateAllCasters(uint32_t dirtyReason)
	{
		for (auto& pair : m_Runtimes)
		{
			Runtime& runtime = pair.second;
			if (runtime.activeResolution == 0 || IsSecondaryPointResident(runtime))
				continue;
			runtime.dirtyFaceMask |= runtime.requiredFaceMask;
			runtime.dirtyReasons |= dirtyReason;
		}
	}

	void VansPunctualShadowManager::InvalidateCastersInBounds(
		const VansShadowAABB& oldBounds,
		const VansShadowAABB& newBounds,
		uint32_t dirtyReason)
	{
		for (auto& pair : m_Runtimes)
		{
			Runtime& runtime = pair.second;
			if (runtime.activeResolution == 0 || IsSecondaryPointResident(runtime))
				continue;
			if (IntersectsSphere(oldBounds, runtime.input.position, runtime.input.radius) ||
				IntersectsSphere(newBounds, runtime.input.position, runtime.input.radius))
			{
				runtime.dirtyFaceMask |= runtime.requiredFaceMask;
				runtime.dirtyReasons |= dirtyReason;
			}
		}
	}

	uint32_t VansPunctualShadowManager::GetShadowMetaIndex(uint32_t stableLightId) const
	{
		const auto found = m_LightToMetaIndex.find(stableLightId);
		return found != m_LightToMetaIndex.end() ? found->second : VANS_INVALID_SHADOW_INDEX;
	}

	bool VansPunctualShadowManager::HasRenderJobs(uint32_t atlasIndex) const
	{
		return std::any_of(m_RenderJobs.begin(), m_RenderJobs.end(), [atlasIndex](const auto& job)
		{
			return job.atlasIndex == atlasIndex;
		});
	}

	uint32_t VansPunctualShadowManager::GetTotalAtlasPages() const
	{
		uint32_t totalPages = 0;
		for (const VansShadowAtlasAllocator& allocator : m_AtlasAllocators)
			totalPages += allocator.GetTotalPages();
		return totalPages;
	}

	VansPunctualShadowDebugSnapshot VansPunctualShadowManager::CaptureDebugSnapshot() const
	{
		VansPunctualShadowDebugSnapshot snapshot;
		snapshot.frameIndex = m_FrameIndex;
		snapshot.atlasSize = m_AtlasAllocators[0].GetAtlasSize();
		snapshot.atlasCount = VANS_PUNCTUAL_SHADOW_ATLAS_COUNT;
		snapshot.basePageSize = m_AtlasAllocators[0].GetBasePageSize();
		snapshot.gutter = m_AtlasAllocators[0].GetGutter();
		snapshot.budget = m_Budget;
		snapshot.statistics = m_Statistics;
		snapshot.lights.reserve(m_Runtimes.size());

		for (const auto& [stableLightId, runtime] : m_Runtimes)
		{
			VansPunctualShadowRuntimeDebug debug;
			debug.stableLightId = stableLightId;
			debug.gpuLightIndex = runtime.input.gpuLightIndex;
			debug.lightType = runtime.input.type;
			debug.runtimeState = runtime.state;
			debug.policy = runtime.input.settings.policy;
			debug.fallback = runtime.input.settings.fallback;
			debug.priority = runtime.input.settings.priority;
			debug.dirtyFaceMask = runtime.dirtyFaceMask;
			debug.validFaceMask = runtime.validFaceMask;
			debug.activeResolution = runtime.activeResolution;
			debug.targetResolution = runtime.targetResolution;
			debug.importance = runtime.importance;
			debug.coverage = runtime.coverage;
			debug.cameraDistance = runtime.cameraDistance;
			debug.distancePriority = runtime.distancePriority;
			debug.atlasWeight = runtime.atlasWeight;
			debug.residencyFrames = runtime.residencyFrames;
			debug.staleFrames = runtime.staleFrames;
			debug.lastRenderedFrame = runtime.lastRenderedFrame;
			debug.castShadows = runtime.input.settings.castShadows;
			debug.affectsVolumetricFog = runtime.input.settings.affectsVolumetricFog;
			debug.affectsGI = runtime.input.settings.affectsGI;
			debug.activeBlocks = runtime.activeBlocks;
			snapshot.lights.push_back(debug);
		}

		std::sort(snapshot.lights.begin(), snapshot.lights.end(), [](const auto& lhs, const auto& rhs)
		{
			if (lhs.lightType != rhs.lightType)
				return lhs.lightType < rhs.lightType;
			if (lhs.gpuLightIndex != rhs.gpuLightIndex)
				return lhs.gpuLightIndex < rhs.gpuLightIndex;
			return lhs.stableLightId < rhs.stableLightId;
		});
		return snapshot;
	}

	uint8_t VansPunctualShadowManager::RequiredFaceMask(VansPunctualShadowLightType type)
	{
		return type == VansPunctualShadowLightType::Point ? 0x3Fu : 0x01u;
	}

	uint32_t VansPunctualShadowManager::ViewCount(VansPunctualShadowLightType type)
	{
		return type == VansPunctualShadowLightType::Point ? 6u : 1u;
	}

	uint32_t VansPunctualShadowManager::PageCost(
		uint32_t resolution,
		uint32_t viewCount,
		uint32_t basePageSize)
	{
		const uint32_t pagesPerAxis = resolution / basePageSize;
		return pagesPerAxis * pagesPerAxis * viewCount;
	}

	uint16_t VansPunctualShadowManager::DownshiftResolution(uint16_t resolution)
	{
		return static_cast<uint16_t>((std::max)(128u, static_cast<uint32_t>(resolution) / 2u));
	}

	bool VansPunctualShadowManager::IntersectsSphere(
		const VansShadowAABB& bounds,
		const glm::vec3& center,
		float radius)
	{
		if (!bounds.IsValid())
			return false;
		const glm::vec3 closest = glm::clamp(center, bounds.min, bounds.max);
		return glm::dot(center - closest, center - closest) <= radius * radius;
	}

	bool VansPunctualShadowManager::IsSecondaryPointResident(const Runtime& runtime)
	{
		return runtime.input.type == VansPunctualShadowLightType::Point &&
			runtime.activeResolution != 0 &&
			runtime.activeBlocks[0].IsValid() &&
			runtime.activeBlocks[0].atlasIndex == VANS_PUNCTUAL_SHADOW_SECONDARY_ATLAS_INDEX;
	}

	float VansPunctualShadowManager::ComputeCoverage(
		const VansPunctualShadowLightInput& input,
		const VansPunctualShadowCameraData& camera) const
	{
		const float distance = glm::length(input.position - camera.position);
		if (distance <= input.radius)
			return 1.0f;
		const float tanHalfFov = (std::max)(std::tan(camera.verticalFovRadians * 0.5f), 0.01f);
		const float projectedRadius = input.radius / (std::max)(distance, 0.01f) / tanHalfFov;
		return glm::clamp(0.78539816339f * projectedRadius * projectedRadius, 0.0f, 1.0f);
	}

	float VansPunctualShadowManager::ComputeImportance(
		const VansPunctualShadowLightInput& input,
		const VansPunctualShadowCameraData& camera,
		bool resident,
		float coverage) const
	{
		const float priority = static_cast<float>(input.settings.priority) / 255.0f;
		const float luminance = glm::dot(input.color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
		const float energy = glm::clamp(std::log2(1.0f + (std::max)(luminance * input.intensity, 0.0f)) / 10.0f, 0.0f, 1.0f);
		const float distanceToInfluence = (std::max)(glm::length(input.position - camera.position) - input.radius, 0.0f);
		const float distanceWeight = glm::clamp(
			1.0f - distanceToInfluence / (std::max)(input.settings.maxShadowDistance, 0.01f),
			0.0f,
			1.0f);
		float consumerBonus = input.settings.affectsVolumetricFog ? 0.50f : 0.0f;
		consumerBonus += input.settings.affectsGI ? 0.35f : 0.0f;
		const float policyBonus = input.settings.policy == VansShadowPolicy::Hero ? 1000.0f : 0.0f;
		if (input.settings.policy == VansShadowPolicy::DistanceDynamic)
		{
			// Distance is deliberately the dominant term in this policy. Artist
			// priority remains a tie-break/bias, while coverage and energy avoid
			// spending a cache slot on a near but visually irrelevant light.
			return 8.0f * ComputeDistancePriority(input, camera) +
				1.5f * priority +
				1.0f * std::sqrt(coverage) +
				0.5f * energy +
				consumerBonus +
				(resident ? 0.75f : 0.0f);
		}
		return policyBonus +
			4.0f * priority +
			3.0f * std::sqrt(coverage) +
			1.5f * energy +
			distanceWeight +
			consumerBonus +
			(resident ? 0.75f : 0.0f);
	}

	float VansPunctualShadowManager::ComputeDistancePriority(
		const VansPunctualShadowLightInput& input,
		const VansPunctualShadowCameraData& camera) const
	{
		const float distance = glm::length(input.position - camera.position);
		const float range = (std::max)(input.settings.maxShadowDistance, 0.01f);
		const float t = glm::clamp(distance / range, 0.0f, 1.0f);
		// Smoothstep falloff has zero slope at both ends: close lights remain
		// stable, and lights approaching the cutoff lose priority gradually.
		const float smoothT = t * t * (3.0f - 2.0f * t);
		return 1.0f - smoothT;
	}

	uint16_t VansPunctualShadowManager::ComputeDesiredResolution(
		const VansPunctualShadowLightInput& input,
		const VansPunctualShadowCameraData& camera,
		float coverage) const
	{
		if (input.settings.resolution != VansShadowResolution::Auto)
			return (std::min)(static_cast<uint16_t>(input.settings.resolution), kMaximumShadowResolution);

		const float viewportPixels = static_cast<float>((std::max)(camera.viewportWidth, 1u)) *
			static_cast<float>((std::max)(camera.viewportHeight, 1u));
		const float raw = std::sqrt(coverage * viewportPixels) * 0.5f;
		uint16_t resolution = 128;
		while (resolution < kMaximumShadowResolution && static_cast<float>(resolution) < raw)
			resolution = static_cast<uint16_t>(resolution * 2u);
		if (input.settings.policy == VansShadowPolicy::Hero)
			resolution = (std::max)(resolution, static_cast<uint16_t>(512));
		return resolution;
	}

	uint16_t VansPunctualShadowManager::ResolveHystereticResolution(Runtime& runtime, uint16_t desiredResolution) const
	{
		if (runtime.activeResolution == 0)
			return desiredResolution;

		if (desiredResolution >= runtime.activeResolution * 3u / 2u)
		{
			++runtime.upgradeFrames;
			runtime.downgradeFrames = 0;
			if (runtime.upgradeFrames >= kUpgradeConfirmationFrames)
			{
				runtime.upgradeFrames = 0;
				return desiredResolution;
			}
		}
		else if (desiredResolution * 2u <= runtime.activeResolution)
		{
			++runtime.downgradeFrames;
			runtime.upgradeFrames = 0;
			if (runtime.downgradeFrames >= kDowngradeConfirmationFrames)
			{
				runtime.downgradeFrames = 0;
				return desiredResolution;
			}
		}
		else
		{
			runtime.upgradeFrames = 0;
			runtime.downgradeFrames = 0;
		}

		return runtime.activeResolution;
	}

	bool VansPunctualShadowManager::IsEligible(
		const VansPunctualShadowLightInput& input,
		const VansPunctualShadowCameraData& camera) const
	{
		if (!input.settings.castShadows || input.settings.policy == VansShadowPolicy::Disabled)
			return false;
		if (!std::isfinite(input.intensity) || !std::isfinite(input.radius) || input.intensity <= 0.0f || input.radius <= 0.01f)
			return false;
		const float distanceToInfluence = (std::max)(glm::length(input.position - camera.position) - input.radius, 0.0f);
		return input.settings.policy == VansShadowPolicy::Hero ||
			distanceToInfluence <= input.settings.maxShadowDistance ||
			input.settings.affectsVolumetricFog ||
			input.settings.affectsGI;
	}

	bool VansPunctualShadowManager::ProjectionChanged(
		const Runtime& runtime,
		const VansPunctualShadowLightInput& input) const
	{
		const VansPunctualShadowLightInput& previous = runtime.input;
		if (previous.type != input.type || !NearlyEqual(previous.position, input.position) || !NearlyEqual(previous.radius, input.radius))
			return true;
		if (input.type != VansPunctualShadowLightType::Point && !NearlyEqual(previous.direction, input.direction))
			return true;
		if (input.type == VansPunctualShadowLightType::Spot && !NearlyEqual(previous.outerConeRadians, input.outerConeRadians))
			return true;
		if (input.type == VansPunctualShadowLightType::Rect &&
			(!NearlyEqual(previous.halfWidth, input.halfWidth) || !NearlyEqual(previous.halfHeight, input.halfHeight)))
			return true;
		if (previous.settings.shadowCasterMask != input.settings.shadowCasterMask)
			return true;
		return !NearlyEqual(previous.settings.nearPlaneOverride, input.settings.nearPlaneOverride);
	}

	bool VansPunctualShadowManager::EnsurePendingAllocation(Runtime& runtime, uint16_t resolution)
	{
		if (runtime.pendingResolution == resolution && runtime.pendingFaceMask != 0)
			return true;

		if (runtime.pendingResolution != 0)
		{
			ReleaseBlocks(runtime.pendingBlocks, ViewCount(runtime.input.type));
			runtime.pendingResolution = 0;
			runtime.pendingFaceMask = 0;
		}

		if (!AllocateGroup(resolution, ViewCount(runtime.input.type), runtime.pendingBlocks))
		{
			++m_Statistics.allocationFailures;
			runtime.state = runtime.input.settings.fallback == VansShadowFallback::ScreenSpace
				? VansShadowRuntimeState::FallbackScreenSpace
				: VansShadowRuntimeState::FallbackNone;
			return false;
		}

		runtime.pendingResolution = resolution;
		runtime.pendingFaceMask = runtime.requiredFaceMask;
		runtime.dirtyReasons |= runtime.activeResolution == 0
			? VansShadowDirty_NewAllocation
			: VansShadowDirty_Resolution;
		runtime.state = VansShadowRuntimeState::PendingRender;
		return true;
	}

	bool VansPunctualShadowManager::AllocateGroup(
		uint16_t resolution,
		uint32_t viewCount,
		std::array<VansShadowAtlasBlock, 6>& outBlocks)
	{
		std::array<uint32_t, VANS_PUNCTUAL_SHADOW_ATLAS_COUNT> order{};
		for (uint32_t atlasIndex = 0; atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT; ++atlasIndex)
			order[atlasIndex] = atlasIndex;
		std::stable_sort(order.begin(), order.end(), [&](uint32_t lhs, uint32_t rhs)
		{
			return m_AtlasAllocators[lhs].GetUsedPages() < m_AtlasAllocators[rhs].GetUsedPages();
		});

		for (uint32_t atlasIndex : order)
		{
			if (!m_AtlasAllocators[atlasIndex].AllocateGroup(resolution, viewCount, outBlocks))
				continue;
			for (uint32_t view = 0; view < viewCount; ++view)
				outBlocks[view].atlasIndex = static_cast<uint16_t>(atlasIndex);
			return true;
		}
		outBlocks = {};
		return false;
	}

	bool VansPunctualShadowManager::ValidateBlock(const VansShadowAtlasBlock& block) const
	{
		return block.atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT &&
			m_AtlasAllocators[block.atlasIndex].Validate(block);
	}

	void VansPunctualShadowManager::ReleaseBlocks(
		std::array<VansShadowAtlasBlock, 6>& blocks,
		uint32_t viewCount)
	{
		for (uint32_t view = 0; view < viewCount; ++view)
		{
			if (blocks[view].IsValid() && blocks[view].atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT)
				m_AtlasAllocators[blocks[view].atlasIndex].Free(blocks[view]);
			blocks[view] = {};
		}
	}

	void VansPunctualShadowManager::ReleasePending(Runtime& runtime)
	{
		if (runtime.pendingResolution == 0)
			return;
		ReleaseBlocks(runtime.pendingBlocks, ViewCount(runtime.input.type));
		runtime.pendingResolution = 0;
		runtime.pendingFaceMask = 0;
		runtime.queuedPendingFaceMask = 0;
	}

	void VansPunctualShadowManager::ReleaseRuntime(Runtime& runtime)
	{
		ReleaseBlocks(runtime.activeBlocks, ViewCount(runtime.input.type));
		ReleaseBlocks(runtime.pendingBlocks, ViewCount(runtime.input.type));
		runtime.activeResolution = 0;
		runtime.pendingResolution = 0;
		runtime.targetResolution = 0;
		runtime.validFaceMask = 0;
		runtime.dirtyFaceMask = 0;
		runtime.pendingFaceMask = 0;
		runtime.queuedActiveFaceMask = 0;
		runtime.queuedPendingFaceMask = 0;
		runtime.activeWorldToShadow = {};
		runtime.queuedWorldToShadow = {};
		runtime.projectionValid = false;
		runtime.atlasWeight = 0.0f;
		runtime.residencyFrames = 0;
		runtime.state = runtime.input.settings.fallback == VansShadowFallback::ScreenSpace
			? VansShadowRuntimeState::FallbackScreenSpace
			: VansShadowRuntimeState::FallbackNone;
	}

	void VansPunctualShadowManager::PromotePending(Runtime& runtime)
	{
		ReleaseBlocks(runtime.activeBlocks, ViewCount(runtime.input.type));
		runtime.activeBlocks = runtime.pendingBlocks;
		runtime.pendingBlocks = {};
		runtime.activeResolution = runtime.pendingResolution;
		runtime.pendingResolution = 0;
		runtime.pendingFaceMask = 0;
		runtime.validFaceMask = runtime.requiredFaceMask;
		runtime.dirtyFaceMask = 0;
		runtime.projectionValid = true;
		runtime.residencyFrames = 0;
		runtime.allocationGeneration = static_cast<uint16_t>(runtime.allocationGeneration + 1u);
		if (runtime.allocationGeneration == 0)
			runtime.allocationGeneration = 1;
		runtime.state = VansShadowRuntimeState::ResidentClean;
	}

	void VansPunctualShadowManager::BuildRenderJobs(std::vector<Runtime*>& orderedRuntimes)
	{
		// Residency 排序与更新排序是两个问题。更新阶段优先保证新 allocation
		// 和失效投影的原子正确性，再用 stale boost 防止普通 dirty light 饥饿。
		std::stable_sort(orderedRuntimes.begin(), orderedRuntimes.end(), [](const Runtime* a, const Runtime* b)
		{
			const auto tier = [](const Runtime* runtime)
			{
				const bool hero = runtime->input.settings.policy == VansShadowPolicy::Hero;
				const bool pendingOnly = runtime->pendingResolution != 0 && runtime->activeResolution == 0;
				const bool projectionInvalid = runtime->activeResolution != 0 &&
					runtime->dirtyFaceMask != 0 && !runtime->projectionValid;
				const bool pendingMigration = runtime->pendingResolution != 0 && runtime->activeResolution != 0;
				if (hero && (pendingOnly || projectionInvalid)) return 0u;
				if (pendingOnly || projectionInvalid) return 1u;
				if (hero && runtime->dirtyFaceMask != 0) return 2u;
				if (runtime->dirtyFaceMask != 0) return 3u;
				if (pendingMigration) return 4u;
				return 5u;
			};

			const uint32_t aTier = tier(a);
			const uint32_t bTier = tier(b);
			if (aTier != bTier)
				return aTier < bTier;

			const float aStaleFactor = 1.0f + 0.25f * static_cast<float>((std::min)(a->staleFrames, kMaxStaleBoostFrames));
			const float bStaleFactor = 1.0f + 0.25f * static_cast<float>((std::min)(b->staleFrames, kMaxStaleBoostFrames));
			const uint32_t aResolution = a->pendingResolution != 0 ? a->pendingResolution : a->activeResolution;
			const uint32_t bResolution = b->pendingResolution != 0 ? b->pendingResolution : b->activeResolution;
			const float aCost = static_cast<float>((std::max)(aResolution * aResolution * ViewCount(a->input.type), 1u));
			const float bCost = static_cast<float>((std::max)(bResolution * bResolution * ViewCount(b->input.type), 1u));
			const float aScore = a->importance * aStaleFactor / aCost;
			const float bScore = b->importance * bStaleFactor / bCost;
			if (!NearlyEqual(aScore, bScore))
				return aScore > bScore;
			return a->input.stableLightId < b->input.stableLightId;
		});

		uint64_t remainingTexels = m_Budget.maxDirtyTexelsPerFrame;
		for (Runtime* runtime : orderedRuntimes)
		{
			const uint32_t viewCount = ViewCount(runtime->input.type);
			// 点光入驻任一 Atlas 时必须原子写满六面。入驻后只有主 Atlas
			// 点光绕过逐帧预算；次 Atlas 保留提交成功时的静态缓存。
			const bool pointPendingMustRender = runtime->input.type == VansPunctualShadowLightType::Point;
			const bool primaryPointMustRender = pointPendingMustRender &&
				runtime->activeResolution != 0 &&
				runtime->activeBlocks[0].atlasIndex == VANS_PUNCTUAL_SHADOW_PRIMARY_ATLAS_INDEX;
			if (runtime->pendingResolution != 0 && runtime->pendingFaceMask != 0)
			{
				const uint64_t groupTexels = static_cast<uint64_t>(runtime->pendingResolution) * runtime->pendingResolution * viewCount;
				if (pointPendingMustRender || groupTexels <= remainingTexels)
				{
					const uint32_t atomicGroup = m_NextAtomicGroupId++;
					for (uint32_t face = 0; face < viewCount; ++face)
					{
						const VansShadowAtlasBlock& block = runtime->pendingBlocks[face];
						VansPunctualShadowRenderJob job;
						job.stableLightId = runtime->input.stableLightId;
						job.gpuLightIndex = runtime->input.gpuLightIndex;
						job.atomicGroupId = atomicGroup;
						job.dirtyReasons = runtime->dirtyReasons;
						job.lightType = runtime->input.type;
						job.faceIndex = static_cast<uint8_t>(face);
						job.resolution = runtime->pendingResolution;
						job.rendersPendingAllocation = true;
						job.shadowCasterMask = runtime->input.settings.shadowCasterMask;
						job.atlasIndex = block.atlasIndex;
						job.atlasRect = { block.x, block.y, block.resolution, block.resolution };
						job.worldToShadow = BuildShadowMatrix(*runtime, face, block);
						m_RenderJobs.push_back(std::move(job));
					}
					remainingTexels = groupTexels < remainingTexels
						? remainingTexels - groupTexels
						: 0ull;
					m_Statistics.dirtyTexels += groupTexels;
					m_Statistics.renderedViews += viewCount;
					// 保持 PendingRender；只有 GPU 提交成功通知才能原子发布。
					runtime->state = VansShadowRuntimeState::PendingRender;
					continue;
				}

				++runtime->staleFrames;
				runtime->state = VansShadowRuntimeState::PendingRender;
				if (runtime->activeResolution == 0)
					continue;
			}

			if (runtime->activeResolution == 0 || runtime->dirtyFaceMask == 0)
				continue;

			uint8_t scheduledMask = runtime->dirtyFaceMask;
			const bool requiresAtomicUpdate = runtime->input.type == VansPunctualShadowLightType::Point ||
				!runtime->projectionValid;
			const uint64_t faceTexels = static_cast<uint64_t>(runtime->activeResolution) * runtime->activeResolution;
			const uint64_t requestedTexels = faceTexels * CountBits(scheduledMask);
			if (!primaryPointMustRender && requiresAtomicUpdate && requestedTexels > remainingTexels)
			{
				++runtime->staleFrames;
				runtime->state = VansShadowRuntimeState::ResidentDirty;
				continue;
			}

			const uint32_t atomicGroup = requiresAtomicUpdate ? m_NextAtomicGroupId++ : 0;
			uint8_t queuedMask = 0;
			for (uint32_t face = 0; face < viewCount; ++face)
			{
				const uint8_t bit = static_cast<uint8_t>(1u << face);
				if ((scheduledMask & bit) == 0)
					continue;
				if (!requiresAtomicUpdate && faceTexels > remainingTexels)
					break;

				const VansShadowAtlasBlock& block = runtime->activeBlocks[face];
				VansPunctualShadowRenderJob job;
				job.stableLightId = runtime->input.stableLightId;
				job.gpuLightIndex = runtime->input.gpuLightIndex;
				job.atomicGroupId = atomicGroup;
				job.dirtyReasons = runtime->dirtyReasons;
				job.lightType = runtime->input.type;
				job.faceIndex = static_cast<uint8_t>(face);
				job.resolution = runtime->activeResolution;
				job.shadowCasterMask = runtime->input.settings.shadowCasterMask;
				job.atlasIndex = block.atlasIndex;
				job.atlasRect = { block.x, block.y, block.resolution, block.resolution };
				job.worldToShadow = BuildShadowMatrix(*runtime, face, block);
				m_RenderJobs.push_back(std::move(job));
				remainingTexels = faceTexels < remainingTexels
					? remainingTexels - faceTexels
					: 0ull;
				m_Statistics.dirtyTexels += faceTexels;
				++m_Statistics.renderedViews;
				queuedMask |= bit;
			}

			const uint8_t unscheduledMask = runtime->dirtyFaceMask & static_cast<uint8_t>(~queuedMask);
			if (unscheduledMask == 0)
			{
				// 提交成功前仍保持 dirty，避免录制/提交失败后丢失更新。
				runtime->state = VansShadowRuntimeState::ResidentDirty;
			}
			else
			{
				++runtime->staleFrames;
				runtime->state = VansShadowRuntimeState::ResidentDirty;
			}
		}
	}

	void VansPunctualShadowManager::BuildGPUData(const std::vector<VansPunctualShadowLightInput>& lights)
	{
		m_GPUShadowData.reserve(lights.size());
		m_GPUShadowViews.reserve(VANS_MAX_PUNCTUAL_SHADOW_VIEWS);
		std::unordered_map<uint32_t, uint32_t> stableIdUseCounts;
		stableIdUseCounts.reserve(lights.size());
		for (const VansPunctualShadowLightInput& input : lights)
			++stableIdUseCounts[input.stableLightId];

		for (const VansPunctualShadowLightInput& input : lights)
		{
			Runtime& runtime = m_Runtimes[input.stableLightId];
			const uint32_t metaIndex = static_cast<uint32_t>(m_GPUShadowData.size());
			const bool hasUniqueStableId = input.stableLightId != 0 &&
				stableIdUseCounts[input.stableLightId] == 1u;
			if (hasUniqueStableId)
				m_LightToMetaIndex[input.stableLightId] = metaIndex;

			VansPunctualShadowGPU gpu;
			gpu.sourceRadius = input.settings.sourceRadius;
			gpu.maxShadowDistance = input.settings.maxShadowDistance;
			gpu.importance = runtime.importance;
			const bool shadowEnabled = input.settings.castShadows &&
				input.settings.policy != VansShadowPolicy::Disabled;
			if (shadowEnabled && input.settings.fallback == VansShadowFallback::ScreenSpace)
				gpu.flags |= VansShadowGPU_FallbackEligible;
			if (shadowEnabled && input.settings.policy == VansShadowPolicy::Hero)
				gpu.flags |= VansShadowGPU_Hero;
			if (input.settings.affectsVolumetricFog)
				gpu.flags |= VansShadowGPU_AffectsFog;
			if (input.settings.affectsGI)
				gpu.flags |= VansShadowGPU_AffectsGI;

			bool allocationValid = hasUniqueStableId && shadowEnabled && runtime.activeResolution != 0 &&
				runtime.projectionValid && runtime.validFaceMask == runtime.requiredFaceMask;
			if (allocationValid)
			{
				for (uint32_t face = 0; face < ViewCount(input.type); ++face)
					allocationValid = allocationValid && ValidateBlock(runtime.activeBlocks[face]) &&
						runtime.activeBlocks[face].atlasIndex == runtime.activeBlocks[0].atlasIndex;
			}

			if (allocationValid)
			{
				gpu.flags |= VansShadowGPU_HasAtlas;
				gpu.firstView = static_cast<uint32_t>(m_GPUShadowViews.size());
				gpu.viewCount = ViewCount(input.type);
				gpu.ownerKey = BuildShadowOwnerKey(input.type, input.gpuLightIndex, runtime.activeBlocks[0].atlasIndex);
				if (runtime.state != VansShadowRuntimeState::Evicting)
					runtime.atlasWeight = (std::min)(1.0f, runtime.atlasWeight + kAtlasFadeInStep);
				gpu.atlasWeight = runtime.atlasWeight;
				for (uint32_t face = 0; face < gpu.viewCount; ++face)
					m_GPUShadowViews.push_back(BuildGPUView(
						runtime,
						runtime.activeBlocks[face],
						runtime.activeWorldToShadow[face]));
				++m_Statistics.residentLights;
				m_Statistics.residentViews += gpu.viewCount;
				++runtime.residencyFrames;
			}
			else
			{
				gpu.atlasWeight = 0.0f;
				if ((gpu.flags & VansShadowGPU_FallbackEligible) != 0)
					++m_Statistics.fallbackLights;
			}

			m_GPUShadowData.push_back(gpu);
		}

		for (VansPunctualShadowRenderJob& job : m_RenderJobs)
		{
			const uint32_t metaIndex = GetShadowMetaIndex(job.stableLightId);
			job.shadowMetaIndex = metaIndex;
			const auto runtimeIt = m_Runtimes.find(job.stableLightId);
			if (runtimeIt == m_Runtimes.end())
				continue;
			Runtime& runtime = runtimeIt->second;
			const uint8_t faceBit = static_cast<uint8_t>(1u << job.faceIndex);

			if (job.rendersPendingAllocation)
			{
				if (job.faceIndex >= ViewCount(runtime.input.type) ||
					m_GPUShadowViews.size() >= VANS_MAX_PUNCTUAL_SHADOW_VIEWS)
					continue;
				const VansShadowAtlasBlock& block = runtime.pendingBlocks[job.faceIndex];
				if (!ValidateBlock(block) || block.atlasIndex != job.atlasIndex)
					continue;
				job.shadowViewIndex = static_cast<uint32_t>(m_GPUShadowViews.size());
				m_GPUShadowViews.push_back(BuildGPUView(runtime, block, job.worldToShadow));
				runtime.queuedWorldToShadow[job.faceIndex] = job.worldToShadow;
				runtime.queuedPendingFaceMask |= faceBit;
			}
			else if (metaIndex < m_GPUShadowData.size() &&
				m_GPUShadowData[metaIndex].firstView != VANS_INVALID_SHADOW_INDEX)
			{
				const uint32_t viewIndex = m_GPUShadowData[metaIndex].firstView + job.faceIndex;
				if (viewIndex < m_GPUShadowViews.size())
				{
					job.shadowViewIndex = viewIndex;
					runtime.queuedWorldToShadow[job.faceIndex] = job.worldToShadow;
					runtime.queuedActiveFaceMask |= faceBit;
				}
			}
			else if (job.faceIndex < ViewCount(runtime.input.type) &&
				m_GPUShadowViews.size() < VANS_MAX_PUNCTUAL_SHADOW_VIEWS)
			{
				// 投影变化时旧 handle 不可采样，但写入 pass 仍需要本次更新矩阵。
				const VansShadowAtlasBlock& block = runtime.activeBlocks[job.faceIndex];
				if (ValidateBlock(block) && block.atlasIndex == job.atlasIndex)
				{
					job.shadowViewIndex = static_cast<uint32_t>(m_GPUShadowViews.size());
					m_GPUShadowViews.push_back(BuildGPUView(runtime, block, job.worldToShadow));
					runtime.queuedWorldToShadow[job.faceIndex] = job.worldToShadow;
					runtime.queuedActiveFaceMask |= faceBit;
				}
			}
		}
	}

	glm::mat4 VansPunctualShadowManager::BuildShadowMatrix(
		const Runtime& runtime,
		uint32_t faceIndex,
		const VansShadowAtlasBlock& block) const
	{
		const float resolution = static_cast<float>((std::max)(block.resolution, static_cast<uint16_t>(1)));
		const float usable = (std::max)(resolution - static_cast<float>(block.gutter * 2u), 1.0f);
		const float nearPlane = runtime.input.settings.nearPlaneOverride > 0.0f
			? runtime.input.settings.nearPlaneOverride
			: glm::clamp(runtime.input.radius * 0.005f, 0.02f, 0.20f);
		const float farPlane = (std::max)(runtime.input.radius, nearPlane + 0.01f);

		glm::vec3 forward;
		glm::vec3 up;
		float fov = glm::radians(90.0f);
		if (runtime.input.type == VansPunctualShadowLightType::Point)
		{
			static const glm::vec3 directions[6] = {
				glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
				glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
				glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)
			};
			static const glm::vec3 upVectors[6] = {
				glm::vec3(0, -1, 0), glm::vec3(0, -1, 0),
				glm::vec3(0, 0, 1), glm::vec3(0, 0, -1),
				glm::vec3(0, -1, 0), glm::vec3(0, -1, 0)
			};
			forward = directions[(std::min)(faceIndex, 5u)];
			up = upVectors[(std::min)(faceIndex, 5u)];
			fov = 2.0f * std::atan(resolution / usable);
		}
		else if (runtime.input.type == VansPunctualShadowLightType::Spot)
		{
			forward = SafeNormalize(-runtime.input.direction, glm::vec3(0.0f, 0.0f, 1.0f));
			up = StableUp(forward);
			const float guardedTan = std::tan(glm::clamp(runtime.input.outerConeRadians, 0.001f, glm::radians(89.5f))) * resolution / usable;
			fov = glm::clamp(2.0f * std::atan(guardedTan), glm::radians(1.0f), glm::radians(179.0f));
		}
		else
		{
			forward = SafeNormalize(runtime.input.direction, glm::vec3(0.0f, 0.0f, 1.0f));
			up = StableUp(forward);
			const float halfDiagonal = std::sqrt(runtime.input.halfWidth * runtime.input.halfWidth + runtime.input.halfHeight * runtime.input.halfHeight);
			const float baseFov = glm::clamp(2.0f * std::atan2(halfDiagonal + runtime.input.radius * 0.05f, nearPlane), glm::radians(1.0f), glm::radians(160.0f));
			fov = glm::clamp(2.0f * std::atan(std::tan(baseFov * 0.5f) * resolution / usable), glm::radians(1.0f), glm::radians(179.0f));
		}

		const glm::mat4 projection = glm::perspective(fov, 1.0f, nearPlane, farPlane);
		const glm::mat4 view = glm::lookAt(runtime.input.position, runtime.input.position + forward, up);
		return projection * view;
	}

	VansPunctualShadowViewGPU VansPunctualShadowManager::BuildGPUView(
		const Runtime& runtime,
		const VansShadowAtlasBlock& block,
		const glm::mat4& worldToShadow) const
	{
		VansPunctualShadowViewGPU view;
		view.worldToShadow = worldToShadow;
		const float atlasSize = static_cast<float>(m_AtlasAllocators[0].GetAtlasSize());
		const float resolution = static_cast<float>(block.resolution);
		view.atlasScaleBias = glm::vec4(
			resolution / atlasSize,
			resolution / atlasSize,
			static_cast<float>(block.x) / atlasSize,
			static_cast<float>(block.y) / atlasSize);
		const float clampInset = 1.5f;
		view.atlasClamp = glm::vec4(
			(static_cast<float>(block.x) + clampInset) / atlasSize,
			(static_cast<float>(block.y) + clampInset) / atlasSize,
			(static_cast<float>(block.x) + resolution - clampInset) / atlasSize,
			(static_cast<float>(block.y) + resolution - clampInset) / atlasSize);
		view.texelBiasParams = glm::vec4(
			1.0f / (std::max)(resolution, 1.0f),
			runtime.input.settings.depthBiasTexels,
			runtime.input.settings.normalBiasTexels,
			(std::max)(resolution - static_cast<float>(block.gutter * 2u), 1.0f));
		return view;
	}
}
