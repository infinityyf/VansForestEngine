#include "VansProjectSettingsJsonCodec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

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

		if (root.contains("mainCameraHiZCulling") && root["mainCameraHiZCulling"].is_object())
		{
			const nlohmann::json& hiz = root["mainCameraHiZCulling"];
			settings.mainCameraHiZCullSettings.enabled = hiz.value("enabled", true);
			settings.mainCameraHiZCullSettings.enableOpaque = hiz.value("enableOpaque", true);
			settings.mainCameraHiZCullSettings.enableHair = hiz.value("enableHair", true);
			settings.mainCameraHiZCullSettings.enableTransparent = hiz.value("enableTransparent", false);
			settings.mainCameraHiZCullSettings.enableDecal = hiz.value("enableDecal", true);
			settings.mainCameraHiZCullSettings.enableForwardOpaqueAfterDeferred =
				hiz.value("enableForwardOpaqueAfterDeferred", true);
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
	root["mainCameraHiZCulling"] = {
		{ "enabled", settings.mainCameraHiZCullSettings.enabled },
		{ "enableOpaque", settings.mainCameraHiZCullSettings.enableOpaque },
		{ "enableHair", settings.mainCameraHiZCullSettings.enableHair },
		{ "enableTransparent", settings.mainCameraHiZCullSettings.enableTransparent },
		{ "enableDecal", settings.mainCameraHiZCullSettings.enableDecal },
		{ "enableForwardOpaqueAfterDeferred", settings.mainCameraHiZCullSettings.enableForwardOpaqueAfterDeferred },
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
		settings.fixedTimeStep = root.value("fixedTimeStep",
			root.value("physicsDeltaTime", 1.0f / 60.0f));
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
	return root;
}
}
