#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace VansGraphics
{
	enum class GIProbeRegionMode : uint32_t
	{
		RegularGrid = 0,
	};

	struct GIProbeRegionDesc
	{
		uint32_t stableId = 1;
		std::string name = "Default";
		bool enabled = true;
		GIProbeRegionMode mode = GIProbeRegionMode::RegularGrid;

		glm::vec3 center = glm::vec3(0.0f, 6.0f, 0.0f);
		glm::vec3 size = glm::vec3(40.0f);
		glm::uvec3 gridDimensions = glm::uvec3(80u);
		float probeSpacing = 0.5f;
		bool overrideGridDimensions = false;

		uint32_t raysPerProbe = 256;

		float maxRayDistance = 100.0f;
		float normalBias = 0.25f;
		float volumeFadeDistance = 1.0f;
		float priority = 0.0f;
	};

	struct GIResolvedRegion
	{
		uint32_t stableId = 1;
		std::string name = "Default";
		bool enabled = true;

		glm::vec3 center = glm::vec3(0.0f, 6.0f, 0.0f);
		glm::vec3 volumeMin = glm::vec3(-20.0f, -14.0f, -20.0f);
		glm::vec3 volumeSize = glm::vec3(40.0f);
		glm::uvec3 gridDimensions = glm::uvec3(80u);
		float probeSpacing = 0.5f;

		uint32_t raysPerProbe = 256;

		float maxRayDistance = 100.0f;
		float normalBias = 0.25f;
		float volumeFadeDistance = 1.0f;
		float priority = 0.0f;

		uint64_t probeCount = 512000u;
	};

	struct GIProbePlacementSettings
	{
		bool enabled = false;
		// 可配置的细分终止间距；0.5 m 仅为默认值，不是算法硬下限。
		float minProbeSpacing = 0.5f;
		float maxProbeSpacing = 4.0f;
		// 父层真实采光的最大单元边长；小于等于该尺寸的覆盖节点保留独立数据。
		float parentProbeMaxSize = 5.0f;
		uint32_t maxProbeCount = 65536u;
		uint32_t maxProbeUpdatesPerFrame = 4096u;
		uint32_t maxRaysPerFrame = 65536u;
	};

	inline void NormalizeGIProbePlacementSettings(GIProbePlacementSettings& settings)
	{
		settings.minProbeSpacing = std::isfinite(settings.minProbeSpacing) && settings.minProbeSpacing > 0.0f
			? std::max(settings.minProbeSpacing, 0.001f) : 0.5f;
		settings.maxProbeSpacing = std::isfinite(settings.maxProbeSpacing) && settings.maxProbeSpacing > 0.0f
			? std::max(settings.maxProbeSpacing, settings.minProbeSpacing) : std::max(4.0f, settings.minProbeSpacing);
		settings.parentProbeMaxSize = std::isfinite(settings.parentProbeMaxSize) && settings.parentProbeMaxSize > 0.0f
			? std::max(settings.parentProbeMaxSize, settings.minProbeSpacing) : std::max(5.0f, settings.minProbeSpacing);
		settings.maxProbeCount = std::max(settings.maxProbeCount, 8u);
		settings.maxProbeUpdatesPerFrame = std::max(settings.maxProbeUpdatesPerFrame, 1u);
		settings.maxRaysPerFrame = std::max(settings.maxRaysPerFrame, 1u);
	}

	inline bool GIProbePlacementResourceLayoutEquals(
		const GIProbePlacementSettings& left, const GIProbePlacementSettings& right)
	{
		if (left.enabled != right.enabled) return false;
		if (left.maxProbeUpdatesPerFrame != right.maxProbeUpdatesPerFrame ||
            left.maxRaysPerFrame != right.maxRaysPerFrame) return false;
        if (!left.enabled) return true;
		return left.minProbeSpacing == right.minProbeSpacing && left.maxProbeSpacing == right.maxProbeSpacing &&
			left.parentProbeMaxSize == right.parentProbeMaxSize &&
			left.maxProbeCount == right.maxProbeCount && left.maxProbeUpdatesPerFrame == right.maxProbeUpdatesPerFrame &&
			left.maxRaysPerFrame == right.maxRaysPerFrame;
	}

	struct VansGISettings
	{
		GIProbePlacementSettings placement;
		std::vector<GIProbeRegionDesc> regions = { GIProbeRegionDesc{} };
		uint32_t selectedRegionIndex = 0;
		// 高亮及反馈保护上限；天光强度由统一 Sky Lighting 数据源控制。
		float maxIndirectRadiance = 2.0f;
		float maxProbeRadiance = 8.0f;
		// 每次完整球面更新保留的历史比例，与帧率和等待间隔无关。
		float irradianceHysteresis = 0.97f;
		float distanceHysteresis = 0.95f;
		float distanceSharpness = 12.0f;
		float volumeFadeDistance = 1.0f;
		bool showProbeGizmos = false;
		bool showProbeVolume = false;
		uint32_t debugView = 0;
		float debugExposure = 1.0f;
		// Explicit diagnostic mode for inspecting per-pixel DDGI irradiance.
		// 由 GI Inspector 显式切换，默认使用正常最终合成。
		bool probeOnlyDeferredOutput = false;
		float probeOnlyDeferredExposure = 1.0f;
		uint32_t gizmoStride = 8;
	};

	inline glm::uvec3 ResolveGIGridDimensions(const GIProbeRegionDesc& region)
	{
		if (region.overrideGridDimensions)
		{
			return glm::max(region.gridDimensions, glm::uvec3(1u));
		}

		const float spacing = std::max(region.probeSpacing, 0.001f);
		const glm::vec3 size = glm::max(region.size, glm::vec3(spacing));
		return glm::uvec3(
			std::clamp(static_cast<uint32_t>(std::ceil(size.x / spacing)), 1u, 256u),
			std::clamp(static_cast<uint32_t>(std::ceil(size.y / spacing)), 1u, 256u),
			std::clamp(static_cast<uint32_t>(std::ceil(size.z / spacing)), 1u, 256u));
	}

	inline GIResolvedRegion ResolveGIRegion(const GIProbeRegionDesc& region)
	{
		GIResolvedRegion resolved;
		resolved.stableId = region.stableId;
		resolved.name = region.name;
		resolved.enabled = region.enabled;
		resolved.center = region.center;
		resolved.probeSpacing = std::max(region.probeSpacing, 0.001f);
		resolved.gridDimensions = ResolveGIGridDimensions(region);
		resolved.volumeSize = glm::vec3(resolved.gridDimensions) * resolved.probeSpacing;
		resolved.volumeMin = resolved.center - resolved.volumeSize * 0.5f;
		resolved.raysPerProbe = std::clamp(region.raysPerProbe, 2u, 4096u);
		resolved.maxRayDistance = std::max(region.maxRayDistance, 0.001f);
		resolved.normalBias = std::max(region.normalBias, 0.0f);
		resolved.volumeFadeDistance = std::max(region.volumeFadeDistance, 0.0f);
		resolved.priority = region.priority;
		resolved.probeCount = static_cast<uint64_t>(resolved.gridDimensions.x) *
			resolved.gridDimensions.y * resolved.gridDimensions.z;
		return resolved;
	}

    // 固定几何射线独立于调度；小样本配置也必须保留动态光照方向。
    inline uint32_t GIProbeFixedRayCount(uint32_t rays) { return std::min(32u, rays / 2u); }
    inline uint64_t GIProbeRayCapacity(uint64_t probes, uint32_t rays, const GIProbePlacementSettings& budget)
    { return std::min(uint64_t(budget.maxRaysPerFrame), std::min(probes, uint64_t(budget.maxProbeUpdatesPerFrame)) * rays); }

	inline const GIProbeRegionDesc& GetPrimaryGIRegionDesc(const VansGISettings& settings)
	{
		static const GIProbeRegionDesc fallback{};
		if (settings.regions.empty())
			return fallback;
		return settings.regions[std::min<uint32_t>(
			settings.selectedRegionIndex,
			static_cast<uint32_t>(settings.regions.size() - 1u))];
	}

	inline std::vector<const GIProbeRegionDesc*> BuildActiveGIRegionOrder(const VansGISettings& settings)
	{
		std::vector<const GIProbeRegionDesc*> ordered;
		if (settings.regions.empty())
			return ordered;
		const uint32_t selected = std::min<uint32_t>(
			settings.selectedRegionIndex,
			static_cast<uint32_t>(settings.regions.size() - 1u));
		if (settings.regions[selected].enabled)
			ordered.push_back(&settings.regions[selected]);
		for (uint32_t index = 0; index < settings.regions.size(); ++index)
		{
			if (index != selected && settings.regions[index].enabled)
				ordered.push_back(&settings.regions[index]);
		}
		return ordered;
	}

	inline void NormalizeGISettings(VansGISettings& settings)
	{
		NormalizeGIProbePlacementSettings(settings.placement);
		if (settings.regions.empty())
		{
			settings.regions.push_back(GIProbeRegionDesc{});
		}

		uint32_t nextStableId = 1;
		for (GIProbeRegionDesc& region : settings.regions)
		{
			if (region.stableId == 0)
				region.stableId = nextStableId;
			nextStableId = std::max(nextStableId, region.stableId + 1u);
			if (region.name.empty())
				region.name = "GI Region " + std::to_string(region.stableId);
			region.probeSpacing = std::max(region.probeSpacing, 0.001f);
			region.gridDimensions = ResolveGIGridDimensions(region);
			// 作者输入尺寸与实际覆盖尺寸分开；反复 Apply 不得把 size 扩大后写回作者配置。
			region.size = region.overrideGridDimensions
				? glm::vec3(region.gridDimensions) * region.probeSpacing
				: glm::max(region.size, glm::vec3(region.probeSpacing));
			region.raysPerProbe = std::clamp(region.raysPerProbe, 2u, 4096u);
			region.maxRayDistance = std::max(region.maxRayDistance, 0.001f);
			region.normalBias = std::max(region.normalBias, 0.0f);
			region.volumeFadeDistance = std::max(region.volumeFadeDistance, 0.0f);
		}

		settings.selectedRegionIndex = std::min<uint32_t>(
			settings.selectedRegionIndex,
			static_cast<uint32_t>(settings.regions.size() - 1u));
		settings.irradianceHysteresis = std::clamp(settings.irradianceHysteresis, 0.0f, 0.999f);
		settings.distanceHysteresis = std::clamp(settings.distanceHysteresis, 0.0f, 0.999f);
		settings.distanceSharpness = std::clamp(settings.distanceSharpness, 8.0f, 16.0f);
		settings.probeOnlyDeferredExposure = std::max(settings.probeOnlyDeferredExposure, 0.001f);

	}

	inline bool GIRegionResourceLayoutEquals(
		const GIProbeRegionDesc& left,
		const GIProbeRegionDesc& right)
	{
		const GIResolvedRegion leftResolved = ResolveGIRegion(left);
		const GIResolvedRegion rightResolved = ResolveGIRegion(right);
		return leftResolved.enabled == rightResolved.enabled &&
			leftResolved.center == rightResolved.center &&
			leftResolved.volumeSize == rightResolved.volumeSize &&
			leftResolved.gridDimensions == rightResolved.gridDimensions &&
			leftResolved.probeSpacing == rightResolved.probeSpacing &&
			leftResolved.stableId == rightResolved.stableId &&
			leftResolved.raysPerProbe == rightResolved.raysPerProbe &&
			leftResolved.maxRayDistance == rightResolved.maxRayDistance;
	}

	inline bool GISettingsResourceLayoutEquals(
		VansGISettings left,
		VansGISettings right)
	{
		NormalizeGISettings(left);
		NormalizeGISettings(right);
		if (!GIProbePlacementResourceLayoutEquals(left.placement, right.placement))
			return false;
		if (left.regions.size() != right.regions.size())
			return false;
		if (left.selectedRegionIndex != right.selectedRegionIndex)
			return false;
		for (size_t index = 0; index < left.regions.size(); ++index)
		{
			if (!GIRegionResourceLayoutEquals(left.regions[index], right.regions[index]))
				return false;
			if (left.placement.enabled &&
				(left.regions[index].normalBias != right.regions[index].normalBias ||
				 left.regions[index].priority != right.regions[index].priority))
				return false;
		}
		return true;
	}
}
