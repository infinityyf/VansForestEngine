#include "VansProjectSettingsJsonCodec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace Vans
{
namespace
{
bool ParseUpscalerBackend(
	const std::string& value,
	VansGraphics::VansUpscalerBackend& backend)
{
	if (value == "Off") backend = VansGraphics::VansUpscalerBackend::Off;
	else if (value == "FSR") backend = VansGraphics::VansUpscalerBackend::FSR;
	else if (value == "DLSS") backend = VansGraphics::VansUpscalerBackend::DLSS;
	else return false;
	return true;
}

bool ParseUpscaleQuality(
	const std::string& value,
	VansGraphics::VansUpscaleQualityMode& quality)
{
	if (value == "NativeAA") quality = VansGraphics::VansUpscaleQualityMode::NativeAA;
	else if (value == "Quality") quality = VansGraphics::VansUpscaleQualityMode::Quality;
	else if (value == "Balanced") quality = VansGraphics::VansUpscaleQualityMode::Balanced;
	else if (value == "Performance") quality = VansGraphics::VansUpscaleQualityMode::Performance;
	else if (value == "UltraPerformance")
		quality = VansGraphics::VansUpscaleQualityMode::UltraPerformance;
	else return false;
	return true;
}
}

bool VansProjectSettingsJsonCodec::DecodeRenderSettings(
	const nlohmann::json& root,
	VansProjectRenderSettingsData& settings,
	std::vector<std::string>& warnings,
	std::string& error)
{
	warnings.clear();
	error.clear();
	try
	{
		if (!root.is_object())
		{
			error = "Render settings root must be an object";
			return false;
		}
		const std::uint32_t schemaVersion = root.at("schemaVersion").get<std::uint32_t>();
		if (schemaVersion != 2u)
		{
			error = "Unsupported render settings schemaVersion=" +
				std::to_string(schemaVersion) + "; expected 2";
			return false;
		}
		if (!root.contains("upscaler") || !root["upscaler"].is_object())
		{
			error = "Missing required object: upscaler";
			return false;
		}
		const nlohmann::json& upscaler = root["upscaler"];
		const std::string backendValue = upscaler.at("backend").get<std::string>();
		const std::string qualityValue = upscaler.at("quality").get<std::string>();
		if (!ParseUpscalerBackend(backendValue, settings.upscalerSettings.backend))
		{
			error = "Invalid upscaler.backend '" + backendValue +
				"'; expected Off, FSR, or DLSS";
			return false;
		}
		if (!ParseUpscaleQuality(qualityValue, settings.upscalerSettings.quality))
		{
			error = "Invalid upscaler.quality '" + qualityValue +
				"'; expected NativeAA, Quality, Balanced, Performance, or UltraPerformance";
			return false;
		}
		settings.upscalerSettings.fsrSharpness =
			upscaler.at("fsrSharpness").get<float>();
		settings.upscalerSettings.fsrDebugView =
			upscaler.at("fsrDebugView").get<bool>();
		if (!std::isfinite(settings.upscalerSettings.fsrSharpness) ||
			settings.upscalerSettings.fsrSharpness < 0.0f ||
			settings.upscalerSettings.fsrSharpness > 1.0f)
		{
			error = "upscaler.fsrSharpness must be in [0, 1]";
			return false;
		}
		if (settings.upscalerSettings.backend == VansGraphics::VansUpscalerBackend::Off &&
			settings.upscalerSettings.quality !=
				VansGraphics::VansUpscaleQualityMode::NativeAA)
		{
			error = "Off upscaler backend requires NativeAA quality";
			return false;
		}

		if (root.contains("outputResolution"))
		{
			if (!root["outputResolution"].is_object())
			{
				error = "outputResolution must be an object";
				return false;
			}
			const nlohmann::json& outputResolution = root["outputResolution"];
			settings.renderOutputSettings.width =
				outputResolution.value("width", 0u);
			settings.renderOutputSettings.height =
				outputResolution.value("height", 0u);
			constexpr std::uint32_t kMinimumOutputWidth = 320u;
			constexpr std::uint32_t kMinimumOutputHeight = 180u;
			constexpr std::uint32_t kMaximumOutputDimension = 16384u;
			if (!settings.renderOutputSettings.UsesWindowExtent() &&
				(!settings.renderOutputSettings.HasExplicitExtent() ||
				 settings.renderOutputSettings.width < kMinimumOutputWidth ||
				 settings.renderOutputSettings.height < kMinimumOutputHeight ||
				 settings.renderOutputSettings.width > kMaximumOutputDimension ||
				 settings.renderOutputSettings.height > kMaximumOutputDimension))
			{
				error = "outputResolution must be 0x0 (follow window) or an explicit "
					"resolution between 320x180 and 16384x16384";
				return false;
			}
		}

		if (root.contains("commandRecording") && root["commandRecording"].is_object())
		{
			const nlohmann::json& commandRecording = root["commandRecording"];
			settings.commandRecordingSettings.parallelEnabled =
				commandRecording.value("parallelEnabled", true);
			settings.commandRecordingSettings.frameContextRingEnabled =
				commandRecording.value("frameContextRingEnabled", false);
			settings.commandRecordingSettings.framesInFlight =
				std::clamp<std::uint32_t>(
					commandRecording.value("framesInFlight", 2u),
					1u,
					2u);
			settings.commandRecordingSettings.asyncComputeEnabled =
				commandRecording.value("asyncComputeEnabled", false);
		}

		if (!root.contains("atmosphereQuality") || !root["atmosphereQuality"].is_object() ||
			!root.contains("nearMediaQuality") || !root["nearMediaQuality"].is_object() ||
			!root.contains("cloudShadowQuality") || !root["cloudShadowQuality"].is_object())
		{
			error = "Render settings require atmosphereQuality, nearMediaQuality, and cloudShadowQuality objects";
			return false;
		}
		const nlohmann::json& atmosphere = root.at("atmosphereQuality");
		auto& atmosphereSettings = settings.atmosphereQualitySettings;
		atmosphereSettings.transmittanceWidth = atmosphere.at("transmittanceWidth").get<std::uint32_t>();
		atmosphereSettings.transmittanceHeight = atmosphere.at("transmittanceHeight").get<std::uint32_t>();
		atmosphereSettings.multiScatteringWidth = atmosphere.at("multiScatteringWidth").get<std::uint32_t>();
		atmosphereSettings.multiScatteringHeight = atmosphere.at("multiScatteringHeight").get<std::uint32_t>();
		atmosphereSettings.skyViewWidth = atmosphere.at("skyViewWidth").get<std::uint32_t>();
		atmosphereSettings.skyViewHeight = atmosphere.at("skyViewHeight").get<std::uint32_t>();
		atmosphereSettings.farAerialTileSize = atmosphere.at("farAerialTileSize").get<std::uint32_t>();
		atmosphereSettings.farAerialSlices = atmosphere.at("farAerialSlices").get<std::uint32_t>();
		atmosphereSettings.farAerialMaxDistanceMeters = atmosphere.at("farAerialMaxDistanceMeters").get<float>();
		atmosphereSettings.transmittanceSamples = atmosphere.at("transmittanceSamples").get<std::uint32_t>();
		atmosphereSettings.multiScatteringSamples = atmosphere.at("multiScatteringSamples").get<std::uint32_t>();
		atmosphereSettings.skyViewSamples = atmosphere.at("skyViewSamples").get<std::uint32_t>();
		atmosphereSettings.farAerialSamplesPerSlice = atmosphere.at("farAerialSamplesPerSlice").get<std::uint32_t>();
		if (atmosphereSettings.transmittanceWidth == 0 || atmosphereSettings.transmittanceHeight == 0 ||
			atmosphereSettings.multiScatteringWidth == 0 || atmosphereSettings.multiScatteringHeight == 0 ||
			atmosphereSettings.skyViewWidth == 0 || atmosphereSettings.skyViewHeight == 0 ||
			atmosphereSettings.farAerialTileSize == 0 || atmosphereSettings.farAerialSlices == 0 ||
			!std::isfinite(atmosphereSettings.farAerialMaxDistanceMeters) ||
			atmosphereSettings.farAerialMaxDistanceMeters <= 0.0f ||
			atmosphereSettings.transmittanceSamples == 0 || atmosphereSettings.multiScatteringSamples == 0 ||
			atmosphereSettings.skyViewSamples == 0 || atmosphereSettings.farAerialSamplesPerSlice == 0)
		{
			error = "atmosphereQuality dimensions, distances, and sample counts must be positive";
			return false;
		}

		const nlohmann::json& nearMedia = root.at("nearMediaQuality");
		auto& nearMediaSettings = settings.nearMediaQualitySettings;
		nearMediaSettings.tileSize = nearMedia.at("tileSize").get<std::uint32_t>();
		nearMediaSettings.slices = nearMedia.at("slices").get<std::uint32_t>();
		nearMediaSettings.nearDistanceMeters = nearMedia.at("nearDistanceMeters").get<float>();
		nearMediaSettings.farDistanceMeters = nearMedia.at("farDistanceMeters").get<float>();
		nearMediaSettings.sliceDistributionPower = nearMedia.at("sliceDistributionPower").get<float>();
		nearMediaSettings.temporalReprojection = nearMedia.at("temporalReprojection").get<bool>();
		nearMediaSettings.historyWeight = nearMedia.at("historyWeight").get<float>();
		if (nearMediaSettings.tileSize == 0 || nearMediaSettings.slices == 0 ||
			!std::isfinite(nearMediaSettings.nearDistanceMeters) || nearMediaSettings.nearDistanceMeters < 0.0f ||
			!std::isfinite(nearMediaSettings.farDistanceMeters) ||
			nearMediaSettings.farDistanceMeters <= nearMediaSettings.nearDistanceMeters ||
			!std::isfinite(nearMediaSettings.sliceDistributionPower) || nearMediaSettings.sliceDistributionPower <= 0.0f ||
			!std::isfinite(nearMediaSettings.historyWeight) || nearMediaSettings.historyWeight < 0.0f ||
			nearMediaSettings.historyWeight >= 1.0f)
		{
			error = "nearMediaQuality contains invalid dimensions, distance mapping, or history weight";
			return false;
		}

		const nlohmann::json& cloudShadow = root.at("cloudShadowQuality");
		auto& cloudShadowSettings = settings.cloudShadowQualitySettings;
		cloudShadowSettings.clipmapCount = cloudShadow.at("clipmapCount").get<std::uint32_t>();
		cloudShadowSettings.resolution = cloudShadow.at("resolution").get<std::uint32_t>();
		cloudShadowSettings.nearCoverageMeters = cloudShadow.at("nearCoverageMeters").get<float>();
		cloudShadowSettings.farCoverageMeters = cloudShadow.at("farCoverageMeters").get<float>();
		cloudShadowSettings.rayMarchSamples = cloudShadow.at("rayMarchSamples").get<std::uint32_t>();
		cloudShadowSettings.clipmapCrossFadeFraction = cloudShadow.at("clipmapCrossFadeFraction").get<float>();
		if (cloudShadowSettings.clipmapCount == 0 || cloudShadowSettings.resolution == 0 ||
			!std::isfinite(cloudShadowSettings.nearCoverageMeters) || cloudShadowSettings.nearCoverageMeters <= 0.0f ||
			!std::isfinite(cloudShadowSettings.farCoverageMeters) ||
			cloudShadowSettings.farCoverageMeters <= cloudShadowSettings.nearCoverageMeters ||
			cloudShadowSettings.rayMarchSamples == 0 ||
			!std::isfinite(cloudShadowSettings.clipmapCrossFadeFraction) ||
			cloudShadowSettings.clipmapCrossFadeFraction < 0.0f ||
			cloudShadowSettings.clipmapCrossFadeFraction > 0.5f)
		{
			error = "cloudShadowQuality contains invalid clipmap dimensions or coverage";
			return false;
		}

		if (root.contains("mainCameraHiZCulling") && root["mainCameraHiZCulling"].is_object())
		{
			const nlohmann::json& hiz = root["mainCameraHiZCulling"];
			settings.mainCameraHiZCullSettings.enabled = hiz.value("enabled", true);
			settings.mainCameraHiZCullSettings.enableOpaque = hiz.value("enableOpaque", true);
			settings.mainCameraHiZCullSettings.enableHair = hiz.value("enableHair", true);
			settings.mainCameraHiZCullSettings.enableTransparent = hiz.value("enableTransparent", false);
			settings.mainCameraHiZCullSettings.enableDecal = hiz.value("enableDecal", true);
			settings.mainCameraHiZCullSettings.enableForwardOpaquePreAtmosphere =
				hiz.value("enableForwardOpaquePreAtmosphere", true);
			settings.mainCameraHiZCullSettings.depthBiasMeters =
				std::max(hiz.value("depthBiasMeters", 0.35f), 0.0f);
			settings.mainCameraHiZCullSettings.cameraMotionDisableDistance =
				std::max(hiz.value("cameraMotionDisableDistance", 1.0f), 0.0f);
			settings.mainCameraHiZCullSettings.cameraMotionDisableAngleRadians =
				std::max(hiz.value("cameraMotionDisableAngleRadians", 0.13962634f), 0.0f);
			settings.mainCameraHiZCullSettings.forceVisibleFramesAfterChange =
				std::max(hiz.value("forceVisibleFramesAfterChange", 1u), 1u);
			settings.mainCameraHiZCullSettings.refreshCulledEveryNFrames =
				std::max(hiz.value("refreshCulledEveryNFrames", 30u), 1u);
			settings.mainCameraHiZCullSettings.maxScreenCoverageForCull =
				std::clamp(hiz.value("maxScreenCoverageForCull", 0.65f), 0.05f, 1.0f);
		}
	}
	catch (const nlohmann::json::exception& exception)
	{
		error = std::string("Render settings JSON parse error: ") + exception.what();
		return false;
	}
	return true;
}

nlohmann::json VansProjectSettingsJsonCodec::EncodeRenderSettings(
	const VansProjectRenderSettingsData& settings)
{
	nlohmann::json root;
	root["schemaVersion"] = 2;
	root["upscaler"] = {
		{ "backend", VansGraphics::ToString(settings.upscalerSettings.backend) },
		{ "quality", VansGraphics::ToString(settings.upscalerSettings.quality) },
		{ "fsrSharpness", settings.upscalerSettings.fsrSharpness },
		{ "fsrDebugView", settings.upscalerSettings.fsrDebugView }
	};
	root["commandRecording"] = {
		{ "parallelEnabled", settings.commandRecordingSettings.parallelEnabled },
		{ "frameContextRingEnabled", settings.commandRecordingSettings.frameContextRingEnabled },
		{ "framesInFlight", settings.commandRecordingSettings.framesInFlight },
		{ "asyncComputeEnabled", settings.commandRecordingSettings.asyncComputeEnabled }
	};
	root["outputResolution"] = {
		{ "width", settings.renderOutputSettings.width },
		{ "height", settings.renderOutputSettings.height }
	};
	root["atmosphereQuality"] = {
		{ "transmittanceWidth", settings.atmosphereQualitySettings.transmittanceWidth },
		{ "transmittanceHeight", settings.atmosphereQualitySettings.transmittanceHeight },
		{ "multiScatteringWidth", settings.atmosphereQualitySettings.multiScatteringWidth },
		{ "multiScatteringHeight", settings.atmosphereQualitySettings.multiScatteringHeight },
		{ "skyViewWidth", settings.atmosphereQualitySettings.skyViewWidth },
		{ "skyViewHeight", settings.atmosphereQualitySettings.skyViewHeight },
		{ "farAerialTileSize", settings.atmosphereQualitySettings.farAerialTileSize },
		{ "farAerialSlices", settings.atmosphereQualitySettings.farAerialSlices },
		{ "farAerialMaxDistanceMeters", settings.atmosphereQualitySettings.farAerialMaxDistanceMeters },
		{ "transmittanceSamples", settings.atmosphereQualitySettings.transmittanceSamples },
		{ "multiScatteringSamples", settings.atmosphereQualitySettings.multiScatteringSamples },
		{ "skyViewSamples", settings.atmosphereQualitySettings.skyViewSamples },
		{ "farAerialSamplesPerSlice", settings.atmosphereQualitySettings.farAerialSamplesPerSlice }
	};
	root["nearMediaQuality"] = {
		{ "tileSize", settings.nearMediaQualitySettings.tileSize },
		{ "slices", settings.nearMediaQualitySettings.slices },
		{ "nearDistanceMeters", settings.nearMediaQualitySettings.nearDistanceMeters },
		{ "farDistanceMeters", settings.nearMediaQualitySettings.farDistanceMeters },
		{ "sliceDistributionPower", settings.nearMediaQualitySettings.sliceDistributionPower },
		{ "temporalReprojection", settings.nearMediaQualitySettings.temporalReprojection },
		{ "historyWeight", settings.nearMediaQualitySettings.historyWeight }
	};
	root["cloudShadowQuality"] = {
		{ "clipmapCount", settings.cloudShadowQualitySettings.clipmapCount },
		{ "resolution", settings.cloudShadowQualitySettings.resolution },
		{ "nearCoverageMeters", settings.cloudShadowQualitySettings.nearCoverageMeters },
		{ "farCoverageMeters", settings.cloudShadowQualitySettings.farCoverageMeters },
		{ "rayMarchSamples", settings.cloudShadowQualitySettings.rayMarchSamples },
		{ "clipmapCrossFadeFraction", settings.cloudShadowQualitySettings.clipmapCrossFadeFraction }
	};
	root["mainCameraHiZCulling"] = {
		{ "enabled", settings.mainCameraHiZCullSettings.enabled },
		{ "enableOpaque", settings.mainCameraHiZCullSettings.enableOpaque },
		{ "enableHair", settings.mainCameraHiZCullSettings.enableHair },
		{ "enableTransparent", settings.mainCameraHiZCullSettings.enableTransparent },
		{ "enableDecal", settings.mainCameraHiZCullSettings.enableDecal },
		{ "enableForwardOpaquePreAtmosphere", settings.mainCameraHiZCullSettings.enableForwardOpaquePreAtmosphere },
		{ "depthBiasMeters", settings.mainCameraHiZCullSettings.depthBiasMeters },
		{ "cameraMotionDisableDistance", settings.mainCameraHiZCullSettings.cameraMotionDisableDistance },
		{ "cameraMotionDisableAngleRadians", settings.mainCameraHiZCullSettings.cameraMotionDisableAngleRadians },
		{ "forceVisibleFramesAfterChange", settings.mainCameraHiZCullSettings.forceVisibleFramesAfterChange },
		{ "refreshCulledEveryNFrames", settings.mainCameraHiZCullSettings.refreshCulledEveryNFrames },
		{ "maxScreenCoverageForCull", settings.mainCameraHiZCullSettings.maxScreenCoverageForCull }
	};
	return root;
}

bool VansProjectSettingsJsonCodec::DecodePhysicsSettings(
	const nlohmann::json& root,
	VansProjectPhysicsSettingsData& settings,
	std::string& error)
{
	error.clear();
	try
	{
		if (!root.is_object() || !root.contains("fixedTimeStep")
			|| !root["fixedTimeStep"].is_number() || !root.contains("queryProfiles")
			|| !root["queryProfiles"].is_object())
		{
			error = "Physics settings require numeric fixedTimeStep and object queryProfiles";
			return false;
		}
		for (const auto& item : root.items())
			if (item.key() != "fixedTimeStep" && item.key() != "queryProfiles")
			{
				error = "Physics settings contain unknown field '" + item.key() + "'";
				return false;
			}
		settings.fixedTimeStep = root.at("fixedTimeStep").get<float>();
		if (!std::isfinite(settings.fixedTimeStep) || settings.fixedTimeStep <= 0.0f)
		{
			error = "Physics fixedTimeStep must be finite and positive";
			return false;
		}
		settings.queryProfiles.clear();
		for (const auto& profile : root.at("queryProfiles").items())
		{
			if (profile.key().empty() || !profile.value().is_object()
				|| profile.value().size() != 1 || !profile.value().contains("collisionLayers")
				|| !profile.value().at("collisionLayers").is_array())
			{
				error = "Physics query profile '" + profile.key()
					+ "' requires only a collisionLayers array";
				return false;
			}
			std::vector<std::string> layers;
			std::unordered_set<std::string> uniqueLayers;
			for (const nlohmann::json& layer : profile.value().at("collisionLayers"))
			{
				if (!layer.is_string() || layer.get<std::string>().empty()
					|| !uniqueLayers.insert(layer.get<std::string>()).second)
				{
					error = "Physics query profile '" + profile.key()
						+ "' contains an invalid or duplicate collision layer";
					return false;
				}
				layers.push_back(layer.get<std::string>());
			}
			if (layers.empty())
			{
				error = "Physics query profile '" + profile.key() + "' cannot be empty";
				return false;
			}
			settings.queryProfiles.emplace(profile.key(), std::move(layers));
		}
	}
	catch (const nlohmann::json::exception& exception)
	{
		error = std::string("Physics settings JSON parse error: ") + exception.what();
		return false;
	}
	return true;
}

nlohmann::json VansProjectSettingsJsonCodec::EncodePhysicsSettings(
	const VansProjectPhysicsSettingsData& settings)
{
	nlohmann::json root;
	root["fixedTimeStep"] = settings.fixedTimeStep;
	root["queryProfiles"] = nlohmann::json::object();
	for (const auto& [name, layers] : settings.queryProfiles)
		root["queryProfiles"][name] = { { "collisionLayers", layers } };
	return root;
}
}
