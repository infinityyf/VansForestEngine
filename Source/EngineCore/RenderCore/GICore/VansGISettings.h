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
		glm::vec3 probeSpacingAxes = glm::vec3(0.5f);
		bool overrideGridDimensions = false;

		uint32_t raysPerProbe = 256;
		uint32_t spatialUpdateDivisor = 2;
		uint32_t directionUpdateSlices = 16;

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
		glm::vec3 probeSpacingAxes = glm::vec3(0.5f);

		uint32_t raysPerProbe = 256;
		uint32_t spatialUpdateDivisor = 2;
		uint32_t directionUpdateSlices = 16;

		float maxRayDistance = 100.0f;
		float normalBias = 0.25f;
		float volumeFadeDistance = 1.0f;
		float priority = 0.0f;

		uint64_t probeCount = 512000u;
	};

	struct VansGISettings
	{
		std::vector<GIProbeRegionDesc> regions;
		uint32_t selectedRegionIndex = 0;

		glm::uvec3 gridDimensions = glm::uvec3(80u);
		glm::vec3 probeSpacingAxes = glm::vec3(0.5f);
		glm::vec3 regionCenter = glm::vec3(0.0f, 6.0f, 0.0f);
		uint32_t raysPerProbe = 256;
		uint32_t spatialUpdateDivisor = 2;
		uint32_t directionUpdateSlices = 16;
		float maxRayDistance = 100.0f;
		float normalBias = 0.25f;
		float environmentIntensity = 5.0f;
		float maxIndirectRadiance = 2.0f;
		float maxSHL0 = 8.0f;
		float volumeFadeDistance = 1.0f;
		bool showProbeGizmos = false;
		bool showProbeVolume = false;
		uint32_t debugView = 0;
		float debugExposure = 1.0f;
		uint32_t gizmoStride = 8;
	};

	inline glm::uvec3 ResolveGIGridDimensions(const GIProbeRegionDesc& region)
	{
		if (region.overrideGridDimensions)
		{
			return glm::max(region.gridDimensions, glm::uvec3(1u));
		}

		const glm::vec3 spacing = glm::max(region.probeSpacingAxes, glm::vec3(0.001f));
		const glm::vec3 size = glm::max(region.size, spacing);
		return glm::uvec3(
			std::clamp(static_cast<uint32_t>(std::ceil(size.x / spacing.x)), 1u, 256u),
			std::clamp(static_cast<uint32_t>(std::ceil(size.y / spacing.y)), 1u, 256u),
			std::clamp(static_cast<uint32_t>(std::ceil(size.z / spacing.z)), 1u, 256u));
	}

	inline GIResolvedRegion ResolveGIRegion(const GIProbeRegionDesc& region)
	{
		GIResolvedRegion resolved;
		resolved.stableId = region.stableId;
		resolved.name = region.name;
		resolved.enabled = region.enabled;
		resolved.center = region.center;
		resolved.probeSpacingAxes = glm::max(region.probeSpacingAxes, glm::vec3(0.001f));
		resolved.gridDimensions = ResolveGIGridDimensions(region);
		resolved.volumeSize = glm::vec3(resolved.gridDimensions) * resolved.probeSpacingAxes;
		resolved.volumeMin = resolved.center - resolved.volumeSize * 0.5f;
		resolved.raysPerProbe = std::max(region.raysPerProbe, 1u);
		resolved.spatialUpdateDivisor = std::clamp(
			std::max(region.spatialUpdateDivisor, 1u),
			1u,
			std::max(1u, std::min({ resolved.gridDimensions.x, resolved.gridDimensions.y, resolved.gridDimensions.z })));
		resolved.directionUpdateSlices = std::clamp(
			std::max(region.directionUpdateSlices, 1u),
			1u,
			resolved.raysPerProbe);
		resolved.maxRayDistance = std::max(region.maxRayDistance, 0.001f);
		resolved.normalBias = std::max(region.normalBias, 0.0f);
		resolved.volumeFadeDistance = std::max(region.volumeFadeDistance, 0.0f);
		resolved.priority = region.priority;
		resolved.probeCount = static_cast<uint64_t>(resolved.gridDimensions.x) *
			resolved.gridDimensions.y * resolved.gridDimensions.z;
		return resolved;
	}

	inline const GIProbeRegionDesc& GetPrimaryGIRegionDesc(const VansGISettings& settings)
	{
		static const GIProbeRegionDesc fallback{};
		if (settings.regions.empty())
			return fallback;
		return settings.regions[std::min<uint32_t>(
			settings.selectedRegionIndex,
			static_cast<uint32_t>(settings.regions.size() - 1u))];
	}

	inline GIProbeRegionDesc BuildLegacyGIRegionDesc(const VansGISettings& settings)
	{
		GIProbeRegionDesc region;
		region.stableId = 1;
		region.name = "Default";
		region.enabled = true;
		region.center = settings.regionCenter;
		region.gridDimensions = glm::max(settings.gridDimensions, glm::uvec3(1u));
		region.probeSpacingAxes = glm::max(settings.probeSpacingAxes, glm::vec3(0.001f));
		region.size = glm::vec3(region.gridDimensions) * region.probeSpacingAxes;
		region.raysPerProbe = std::max(settings.raysPerProbe, 1u);
		region.spatialUpdateDivisor = std::max(settings.spatialUpdateDivisor, 1u);
		region.directionUpdateSlices = std::max(settings.directionUpdateSlices, 1u);
		region.maxRayDistance = std::max(settings.maxRayDistance, 0.001f);
		region.normalBias = std::max(settings.normalBias, 0.0f);
		region.volumeFadeDistance = std::max(settings.volumeFadeDistance, 0.0f);
		region.overrideGridDimensions = true;
		return region;
	}

	inline void NormalizeGISettings(VansGISettings& settings)
	{
		if (settings.regions.empty())
		{
			settings.regions.push_back(BuildLegacyGIRegionDesc(settings));
		}

		uint32_t nextStableId = 1;
		for (GIProbeRegionDesc& region : settings.regions)
		{
			if (region.stableId == 0)
				region.stableId = nextStableId;
			nextStableId = std::max(nextStableId, region.stableId + 1u);
			if (region.name.empty())
				region.name = "GI Region " + std::to_string(region.stableId);
			region.probeSpacingAxes = glm::max(region.probeSpacingAxes, glm::vec3(0.001f));
			region.gridDimensions = ResolveGIGridDimensions(region);
			region.size = glm::vec3(region.gridDimensions) * region.probeSpacingAxes;
			region.raysPerProbe = std::clamp(region.raysPerProbe, 1u, 4096u);
			region.spatialUpdateDivisor = std::clamp(
				std::max(region.spatialUpdateDivisor, 1u),
				1u,
				std::max(1u, std::min({ region.gridDimensions.x, region.gridDimensions.y, region.gridDimensions.z })));
			region.directionUpdateSlices = std::clamp(
				std::max(region.directionUpdateSlices, 1u),
				1u,
				region.raysPerProbe);
			region.maxRayDistance = std::max(region.maxRayDistance, 0.001f);
			region.normalBias = std::max(region.normalBias, 0.0f);
			region.volumeFadeDistance = std::max(region.volumeFadeDistance, 0.0f);
		}

		settings.selectedRegionIndex = std::min<uint32_t>(
			settings.selectedRegionIndex,
			static_cast<uint32_t>(settings.regions.size() - 1u));

		const GIProbeRegionDesc& primary = GetPrimaryGIRegionDesc(settings);
		settings.gridDimensions = primary.gridDimensions;
		settings.probeSpacingAxes = primary.probeSpacingAxes;
		settings.regionCenter = primary.center;
		settings.raysPerProbe = primary.raysPerProbe;
		settings.spatialUpdateDivisor = primary.spatialUpdateDivisor;
		settings.directionUpdateSlices = primary.directionUpdateSlices;
		settings.maxRayDistance = primary.maxRayDistance;
		settings.normalBias = primary.normalBias;
		settings.volumeFadeDistance = primary.volumeFadeDistance;
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
			leftResolved.probeSpacingAxes == rightResolved.probeSpacingAxes &&
			leftResolved.raysPerProbe == rightResolved.raysPerProbe &&
			leftResolved.maxRayDistance == rightResolved.maxRayDistance;
	}

	inline bool GISettingsResourceLayoutEquals(
		VansGISettings left,
		VansGISettings right)
	{
		NormalizeGISettings(left);
		NormalizeGISettings(right);
		if (left.regions.size() != right.regions.size())
			return false;
		for (size_t index = 0; index < left.regions.size(); ++index)
		{
			if (!GIRegionResourceLayoutEquals(left.regions[index], right.regions[index]))
				return false;
		}
		return true;
	}
}
