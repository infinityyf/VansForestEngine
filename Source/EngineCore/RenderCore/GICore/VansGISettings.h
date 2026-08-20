#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
		float probeSpacing = 0.5f;

		uint32_t raysPerProbe = 256;
		uint32_t spatialUpdateDivisor = 2;
		uint32_t directionUpdateSlices = 16;

		float maxRayDistance = 100.0f;
		float normalBias = 0.25f;
		float volumeFadeDistance = 1.0f;
		float priority = 0.0f;

		uint64_t probeCount = 512000u;
	};

	// Probe GI 的调度必须与相机/SSGI 的时域索引解耦。这个 batch 是
	// RayTracing、Probe Shade、Probe Blend、调试统计共用的唯一真相来源。
	// 默认 256 rays、D=2、S=16 时，一个 probe 的完整方向周期严格为 128 帧。
	struct GIProbeUpdateBatch
	{
		uint32_t spatialPhaseCount = 1;
		uint32_t spatialPhase = 0;
		glm::uvec3 spatialOffset = glm::uvec3(0u);
		uint32_t directionSlice = 0;
		uint64_t cycleIndex = 0;

		glm::uvec3 activeGridDimensions = glm::uvec3(1u);
		uint32_t raysPerActiveProbe = 1;
		uint64_t activeProbeCount = 1;
		uint64_t activeRayCount = 1;
		uint64_t fullUpdateCycleFrameCount = 1;
	};

	struct VansGISettings
	{
		std::vector<GIProbeRegionDesc> regions = { GIProbeRegionDesc{} };
		uint32_t selectedRegionIndex = 0;
		// Multiplier for unfiltered environment radiance injected into DDGI.
		// One is the physical baseline; the historical value five only
		// compensated for the old non-normalized probe reconstruction.
		float environmentIntensity = 1.0f;
		float maxIndirectRadiance = 2.0f;
		float maxProbeRadiance = 8.0f;
		float irradianceHysteresis = 0.97f;
		float distanceHysteresis = 0.95f;
		float distanceSharpness = 12.0f;
		float brightnessChangeThreshold = 2.0f;
		float volumeFadeDistance = 1.0f;
		bool showProbeGizmos = false;
		bool showProbeVolume = false;
		uint32_t debugView = 0;
		float debugExposure = 1.0f;
		// Explicit diagnostic mode for inspecting per-pixel DDGI irradiance.
		// Keep the default on the normal final composite; GI Inspector or
		// FORESTENGINE_GI_PROBE_ONLY can enable this when debugging transport.
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
		resolved.raysPerProbe = std::max(region.raysPerProbe, 1u);
		resolved.spatialUpdateDivisor = std::clamp(
			std::max(region.spatialUpdateDivisor, 1u),
			1u,
			std::max({ 1u, resolved.gridDimensions.x, resolved.gridDimensions.y, resolved.gridDimensions.z }));
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

	inline GIProbeUpdateBatch BuildGIProbeUpdateBatch(
		const GIResolvedRegion& region,
		uint64_t updateFrameIndex)
	{
		GIProbeUpdateBatch batch;
		const uint32_t divisor = std::max(region.spatialUpdateDivisor, 1u);
		const uint32_t directionSlices = std::max(region.directionUpdateSlices, 1u);
		const uint32_t rayCount = std::max(region.raysPerProbe, 1u);

		const glm::uvec3 axisDivisors = glm::min(
			glm::uvec3(divisor),
			glm::max(region.gridDimensions, glm::uvec3(1u)));
		batch.spatialPhaseCount = axisDivisors.x * axisDivisors.y * axisDivisors.z;
		batch.spatialPhase = static_cast<uint32_t>(updateFrameIndex % batch.spatialPhaseCount);
		batch.spatialOffset = glm::uvec3(
			batch.spatialPhase % axisDivisors.x,
			(batch.spatialPhase / axisDivisors.x) % axisDivisors.y,
			batch.spatialPhase / (axisDivisors.x * axisDivisors.y));
		batch.directionSlice = static_cast<uint32_t>(
			(updateFrameIndex / batch.spatialPhaseCount) % directionSlices);
		batch.cycleIndex = updateFrameIndex /
			(static_cast<uint64_t>(batch.spatialPhaseCount) * directionSlices);
		const auto activeAxisCount = [](uint32_t dimension, uint32_t offset, uint32_t axisDivisor)
		{
			return dimension > offset ? ((dimension - 1u - offset) / axisDivisor + 1u) : 0u;
		};
		batch.activeGridDimensions = glm::uvec3(
			activeAxisCount(region.gridDimensions.x, batch.spatialOffset.x, axisDivisors.x),
			activeAxisCount(region.gridDimensions.y, batch.spatialOffset.y, axisDivisors.y),
			activeAxisCount(region.gridDimensions.z, batch.spatialOffset.z, axisDivisors.z));
		batch.raysPerActiveProbe = (rayCount + directionSlices - 1u) / directionSlices;
		batch.activeProbeCount = static_cast<uint64_t>(batch.activeGridDimensions.x) *
			batch.activeGridDimensions.y * batch.activeGridDimensions.z;
		batch.activeRayCount = batch.activeProbeCount * batch.raysPerActiveProbe;
		batch.fullUpdateCycleFrameCount = static_cast<uint64_t>(batch.spatialPhaseCount) * directionSlices;
		return batch;
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
			region.size = glm::vec3(region.gridDimensions) * region.probeSpacing;
			region.raysPerProbe = std::clamp(region.raysPerProbe, 1u, 4096u);
			region.spatialUpdateDivisor = std::clamp(
				std::max(region.spatialUpdateDivisor, 1u),
				1u,
				std::max({ 1u, region.gridDimensions.x, region.gridDimensions.y, region.gridDimensions.z }));
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
		settings.irradianceHysteresis = std::clamp(settings.irradianceHysteresis, 0.0f, 0.999f);
		settings.distanceHysteresis = std::clamp(settings.distanceHysteresis, 0.0f, 0.999f);
		settings.distanceSharpness = std::clamp(settings.distanceSharpness, 8.0f, 16.0f);
		settings.brightnessChangeThreshold = std::max(settings.brightnessChangeThreshold, 0.001f);
		settings.probeOnlyDeferredExposure = std::max(settings.probeOnlyDeferredExposure, 0.001f);

	}

	inline bool IsGIProbeOnlyDeferredOutputEnabled(const VansGISettings& settings)
	{
		if (const char* probeOnlyEnv = std::getenv("FORESTENGINE_GI_PROBE_ONLY"))
		{
			const std::string value = probeOnlyEnv;
			if (value == "0" || value == "false" || value == "False")
				return false;
			if (value == "1" || value == "true" || value == "True")
				return true;
		}
		return settings.probeOnlyDeferredOutput;
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
