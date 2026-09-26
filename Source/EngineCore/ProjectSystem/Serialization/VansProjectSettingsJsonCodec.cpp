#include "VansProjectSettingsJsonCodec.h"
#include "../../RenderCore/UpscalingCore/VansUpscaleResolutionPolicy.h"

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
		if (!VansGraphics::VansUpscaleResolutionPolicy::ValidateConfig(
			settings.upscalerSettings, error))
			return false;

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
			if (!VansGraphics::VansUpscaleResolutionPolicy::ValidateOutputExtent(
				{ settings.renderOutputSettings.width, settings.renderOutputSettings.height },
				true, 0u, error))
				return false;
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
		nearMediaSettings.lightTransmittanceSamples =
			nearMedia.at("lightTransmittanceSamples").get<std::uint32_t>();
		nearMediaSettings.lightTransmittanceMaxDistanceMeters =
			nearMedia.at("lightTransmittanceMaxDistanceMeters").get<float>();
		if (nearMediaSettings.tileSize == 0 || nearMediaSettings.slices == 0 ||
			!std::isfinite(nearMediaSettings.nearDistanceMeters) || nearMediaSettings.nearDistanceMeters < 0.0f ||
			!std::isfinite(nearMediaSettings.farDistanceMeters) ||
			nearMediaSettings.farDistanceMeters <= nearMediaSettings.nearDistanceMeters ||
			!std::isfinite(nearMediaSettings.sliceDistributionPower) || nearMediaSettings.sliceDistributionPower <= 0.0f ||
			!std::isfinite(nearMediaSettings.historyWeight) || nearMediaSettings.historyWeight < 0.0f ||
			nearMediaSettings.historyWeight >= 1.0f ||
			nearMediaSettings.lightTransmittanceSamples == 0 ||
			nearMediaSettings.lightTransmittanceSamples > 32 ||
			!std::isfinite(nearMediaSettings.lightTransmittanceMaxDistanceMeters) ||
			nearMediaSettings.lightTransmittanceMaxDistanceMeters <= 0.0f)
		{
			error = "nearMediaQuality contains invalid dimensions, distance mapping, history weight, or light transmittance quality";
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

		if (!root.contains("cameraLensLimits") || !root["cameraLensLimits"].is_object())
		{
			error = "Missing required object: cameraLensLimits";
			return false;
		}
		const nlohmann::json& cameraLens = root.at("cameraLensLimits");
		settings.cameraLensLimits.minimumFieldOfView =
			cameraLens.at("minimumFieldOfView").get<float>();
		settings.cameraLensLimits.maximumFieldOfView =
			cameraLens.at("maximumFieldOfView").get<float>();
		settings.cameraLensLimits.minimumNearClip =
			cameraLens.at("minimumNearClip").get<float>();
		settings.cameraLensLimits.minimumClipSeparation =
			cameraLens.at("minimumClipSeparation").get<float>();
		if (!VansValidateCameraLensLimits(settings.cameraLensLimits, error))
		{
			error = "cameraLensLimits: " + error;
			return false;
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
		{ "historyWeight", settings.nearMediaQualitySettings.historyWeight },
		{ "lightTransmittanceSamples", settings.nearMediaQualitySettings.lightTransmittanceSamples },
		{ "lightTransmittanceMaxDistanceMeters",
			settings.nearMediaQualitySettings.lightTransmittanceMaxDistanceMeters }
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
	root["cameraLensLimits"] = {
		{ "minimumFieldOfView", settings.cameraLensLimits.minimumFieldOfView },
		{ "maximumFieldOfView", settings.cameraLensLimits.maximumFieldOfView },
		{ "minimumNearClip", settings.cameraLensLimits.minimumNearClip },
		{ "minimumClipSeparation", settings.cameraLensLimits.minimumClipSeparation }
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
		if (!root.is_object() || root.size() != 2u || !root.contains("simulation")
			|| !root.at("simulation").is_object() || root.at("simulation").size() != 4u
			|| !root.at("simulation").contains("fixedTimeStep")
			|| !root.at("simulation").at("fixedTimeStep").is_number()
			|| !root.at("simulation").contains("maximumSubsteps")
			|| !root.at("simulation").at("maximumSubsteps").is_number_integer()
			|| !root.at("simulation").contains("clothFrameTime")
			|| !root.at("simulation").at("clothFrameTime").is_number()
			|| !root.at("simulation").contains("clothSubsteps")
			|| !root.at("simulation").at("clothSubsteps").is_number_integer()
			|| !root.contains("queryProfiles") || !root.at("queryProfiles").is_object())
		{
			error = "Physics settings require simulation { fixedTimeStep, maximumSubsteps, clothFrameTime, clothSubsteps } and queryProfiles";
			return false;
		}
		const auto& simulation = root.at("simulation");
		for (const auto& item : simulation.items())
			if (item.key() != "fixedTimeStep" && item.key() != "maximumSubsteps" &&
				item.key() != "clothFrameTime" && item.key() != "clothSubsteps")
			{
				error = "Physics simulation settings contain unknown field '" + item.key() + "'";
				return false;
			}
		settings.timing.fixedTimeStep = simulation.at("fixedTimeStep").get<float>();
		const std::int64_t maximumSubsteps = simulation.at("maximumSubsteps").get<std::int64_t>();
		if (maximumSubsteps <= 0 || maximumSubsteps >
			static_cast<std::int64_t>(VansEngine::VansPhysicsTiming::kMaximumSupportedSubsteps))
		{
			error = "Physics maximumSubsteps must be in [1, "
				+ std::to_string(VansEngine::VansPhysicsTiming::kMaximumSupportedSubsteps) + "]";
			return false;
		}
		settings.timing.maximumSubsteps = static_cast<std::uint32_t>(maximumSubsteps);
		settings.timing.clothFrameTime = simulation.at("clothFrameTime").get<float>();
		const std::int64_t clothSubsteps = simulation.at("clothSubsteps").get<std::int64_t>();
		if (clothSubsteps <= 0 || clothSubsteps > static_cast<std::int64_t>(
			VansEngine::VansPhysicsTiming::kMaximumSupportedClothSubsteps))
		{
			error = "Physics clothSubsteps must be in [1, " + std::to_string(
				VansEngine::VansPhysicsTiming::kMaximumSupportedClothSubsteps) + "]";
			return false;
		}
		settings.timing.clothSubsteps = static_cast<std::uint32_t>(clothSubsteps);
		if (!settings.timing.IsValid())
		{
			error = "Physics fixedTimeStep and clothFrameTime must be finite and positive";
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
	root["simulation"] = {
		{ "fixedTimeStep", settings.timing.fixedTimeStep },
		{ "maximumSubsteps", settings.timing.maximumSubsteps },
		{ "clothFrameTime", settings.timing.clothFrameTime },
		{ "clothSubsteps", settings.timing.clothSubsteps }
	};
	root["queryProfiles"] = nlohmann::json::object();
	for (const auto& [name, layers] : settings.queryProfiles)
		root["queryProfiles"][name] = { { "collisionLayers", layers } };
	return root;
}

bool VansProjectSettingsJsonCodec::DecodeNavigationSettings(
	const nlohmann::json& root,
	VansNavigationSettings& settings,
	std::string& error)
{
	error.clear();
	try
	{
		if (!root.is_object() || root.size() != 4u ||
			root.value("schemaVersion", 0) != 2 ||
			!root.contains("bake") || !root.at("bake").is_object() ||
			!root.contains("query") || !root.at("query").is_object() ||
			!root.contains("areas") || !root.at("areas").is_object())
		{
			error = "Navigation settings require schemaVersion 2, bake, query, and areas objects";
			return false;
		}
		const nlohmann::json& bake = root.at("bake");
		const nlohmann::json& query = root.at("query");
		const nlohmann::json& areas = root.at("areas");
		static const std::unordered_set<std::string> bakeFields = {
			"cellSize", "cellHeight", "agentHeight", "agentRadius",
			"agentMaxClimb", "agentMaxSlopeDegrees", "regionMinSize",
			"regionMergeSize", "edgeMaxLength", "edgeMaxError",
			"maximumVerticesPerPolygon", "detailSamplingEnabled",
			"detailSampleDistance", "detailSampleMaxError"
		};
		static const std::unordered_set<std::string> queryFields = {
			"maximumSearchNodes", "maximumCorridorPolygons", "maximumPathPoints"
		};
		if (bake.size() != bakeFields.size())
		{
			error = "Navigation bake settings must define every supported field exactly once";
			return false;
		}
		for (const auto& item : bake.items())
			if (bakeFields.find(item.key()) == bakeFields.end())
			{
				error = "Navigation bake settings contain unknown field '" + item.key() + "'";
				return false;
			}
		if (query.size() != queryFields.size())
		{
			error = "Navigation query settings must define every supported field exactly once";
			return false;
		}
		for (const auto& item : query.items())
			if (queryFields.find(item.key()) == queryFields.end())
			{
				error = "Navigation query settings contain unknown field '" + item.key() + "'";
				return false;
			}
		if (areas.size() != 2u || !areas.contains("default") ||
			!areas.at("default").is_string() || !areas.contains("definitions") ||
			!areas.at("definitions").is_array())
		{
			error = "Navigation areas require default and definitions fields";
			return false;
		}
		for (const auto& item : areas.items())
			if (item.key() != "default" && item.key() != "definitions")
			{
				error = "Navigation areas contain unknown field '" + item.key() + "'";
				return false;
			}

		VansNavigationSettings decoded;
		decoded.bake.cellSize = bake.at("cellSize").get<float>();
		decoded.bake.cellHeight = bake.at("cellHeight").get<float>();
		decoded.bake.agentHeight = bake.at("agentHeight").get<float>();
		decoded.bake.agentRadius = bake.at("agentRadius").get<float>();
		decoded.bake.agentMaxClimb = bake.at("agentMaxClimb").get<float>();
		decoded.bake.agentMaxSlopeDegrees =
			bake.at("agentMaxSlopeDegrees").get<float>();
		decoded.bake.regionMinSize = bake.at("regionMinSize").get<float>();
		decoded.bake.regionMergeSize = bake.at("regionMergeSize").get<float>();
		decoded.bake.edgeMaxLength = bake.at("edgeMaxLength").get<float>();
		decoded.bake.edgeMaxError = bake.at("edgeMaxError").get<float>();
		decoded.bake.maximumVerticesPerPolygon =
			bake.at("maximumVerticesPerPolygon").get<int>();
		decoded.bake.detailSamplingEnabled =
			bake.at("detailSamplingEnabled").get<bool>();
		decoded.bake.detailSampleDistance =
			bake.at("detailSampleDistance").get<float>();
		decoded.bake.detailSampleMaxError =
			bake.at("detailSampleMaxError").get<float>();
		decoded.query.maximumSearchNodes =
			query.at("maximumSearchNodes").get<int>();
		decoded.query.maximumCorridorPolygons =
			query.at("maximumCorridorPolygons").get<int>();
		decoded.query.maximumPathPoints =
			query.at("maximumPathPoints").get<int>();
		decoded.areas.defaultArea = areas.at("default").get<std::string>();
		decoded.areas.definitions.clear();
		static const std::unordered_set<std::string> areaFields = {
			"name", "id", "traversalCost", "traversable"
		};
		for (const nlohmann::json& area : areas.at("definitions"))
		{
			if (!area.is_object() || area.size() != areaFields.size())
			{
				error = "Each navigation area definition must contain exactly four fields";
				return false;
			}
			for (const auto& item : area.items())
				if (areaFields.find(item.key()) == areaFields.end())
				{
					error = "Navigation area definition contains unknown field '" +
						item.key() + "'";
					return false;
				}
			const int areaId = area.at("id").get<int>();
			if (areaId < 0 || areaId > kMaximumNavigationAreaId)
			{
				error = "Navigation area id must be in [0, 62]";
				return false;
			}
			decoded.areas.definitions.push_back({
				area.at("name").get<std::string>(),
				static_cast<std::uint8_t>(areaId),
				area.at("traversalCost").get<float>(),
				area.at("traversable").get<bool>()
			});
		}
		if (!ValidateNavigationSettings(decoded, error))
			return false;
		settings = decoded;
		return true;
	}
	catch (const nlohmann::json::exception& exception)
	{
		error = std::string("Navigation settings JSON parse error: ") + exception.what();
		return false;
	}
}

nlohmann::json VansProjectSettingsJsonCodec::EncodeNavigationSettings(
	const VansNavigationSettings& settings)
{
	nlohmann::json definitions = nlohmann::json::array();
	for (const VansNavigationAreaDefinition& area : settings.areas.definitions)
		definitions.push_back({
			{ "name", area.name },
			{ "id", area.id },
			{ "traversalCost", area.traversalCost },
			{ "traversable", area.traversable }
		});
	return {
		{ "schemaVersion", 2 },
		{ "bake", {
			{ "cellSize", settings.bake.cellSize },
			{ "cellHeight", settings.bake.cellHeight },
			{ "agentHeight", settings.bake.agentHeight },
			{ "agentRadius", settings.bake.agentRadius },
			{ "agentMaxClimb", settings.bake.agentMaxClimb },
			{ "agentMaxSlopeDegrees", settings.bake.agentMaxSlopeDegrees },
			{ "regionMinSize", settings.bake.regionMinSize },
			{ "regionMergeSize", settings.bake.regionMergeSize },
			{ "edgeMaxLength", settings.bake.edgeMaxLength },
			{ "edgeMaxError", settings.bake.edgeMaxError },
			{ "maximumVerticesPerPolygon", settings.bake.maximumVerticesPerPolygon },
			{ "detailSamplingEnabled", settings.bake.detailSamplingEnabled },
			{ "detailSampleDistance", settings.bake.detailSampleDistance },
			{ "detailSampleMaxError", settings.bake.detailSampleMaxError }
		} },
		{ "query", {
			{ "maximumSearchNodes", settings.query.maximumSearchNodes },
			{ "maximumCorridorPolygons", settings.query.maximumCorridorPolygons },
			{ "maximumPathPoints", settings.query.maximumPathPoints }
		} },
		{ "areas", {
			{ "default", settings.areas.defaultArea },
			{ "definitions", std::move(definitions) }
		} }
	};
}
}
