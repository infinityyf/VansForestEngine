#include "VansProjectSettingsJsonCodec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace Vans
{
namespace
{
const char* ToString(VansProjectFSRMode mode)
{
	switch (mode)
	{
	case VansProjectFSRMode::NativeAA: return "NativeAA";
	case VansProjectFSRMode::Quality: return "Quality";
	case VansProjectFSRMode::Performance: return "Performance";
	case VansProjectFSRMode::MatchViewport:
	default: return "MatchViewport";
	}
}

VansProjectFSRMode ParseFSRMode(const std::string& value, std::vector<std::string>& warnings)
{
	if (value == "NativeAA") return VansProjectFSRMode::NativeAA;
	if (value == "Quality") return VansProjectFSRMode::Quality;
	if (value == "Performance") return VansProjectFSRMode::Performance;
	if (value != "MatchViewport")
		warnings.push_back("Unknown FSR mode '" + value + "', fallback to MatchViewport");
	return VansProjectFSRMode::MatchViewport;
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
		const std::uint32_t schemaVersion = root.value("schemaVersion", 1u);
		if (schemaVersion != 1u)
		{
			warnings.push_back(
				"Unsupported render settings schemaVersion=" +
				std::to_string(schemaVersion) +
				", reading known fields only");
		}

		if (root.contains("fsr") && root["fsr"].is_object())
		{
			const nlohmann::json& fsr = root["fsr"];
			settings.fsrSettings.mode = ParseFSRMode(
				fsr.value("mode", std::string("MatchViewport")),
				warnings);
			settings.fsrSettings.sharpness = std::clamp(
				fsr.value("sharpness", 0.35f),
				0.0f,
				1.0f);
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
	root["schemaVersion"] = 1;
	root["fsr"] = {
		{ "mode", ToString(settings.fsrSettings.mode) },
		{ "sharpness", settings.fsrSettings.sharpness }
	};
	root["commandRecording"] = {
		{ "parallelEnabled", settings.commandRecordingSettings.parallelEnabled },
		{ "frameContextRingEnabled", settings.commandRecordingSettings.frameContextRingEnabled },
		{ "framesInFlight", settings.commandRecordingSettings.framesInFlight }
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
