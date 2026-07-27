#include "VansProjectSettingsLegacyJsonCodec.h"

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

bool VansProjectSettingsLegacyJsonCodec::DecodeRenderSettings(
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
	}
	catch (const nlohmann::json::exception& exception)
	{
		error = std::string("Render settings JSON parse error: ") + exception.what();
		return false;
	}
	return true;
}

nlohmann::json VansProjectSettingsLegacyJsonCodec::EncodeRenderSettings(
	const VansProjectRenderSettingsData& settings)
{
	nlohmann::json root;
	root["schemaVersion"] = 1;
	root["fsr"] = {
		{ "mode", ToString(settings.fsrSettings.mode) },
		{ "sharpness", settings.fsrSettings.sharpness }
	};
	return root;
}

bool VansProjectSettingsLegacyJsonCodec::DecodePhysicsSettings(
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

nlohmann::json VansProjectSettingsLegacyJsonCodec::EncodePhysicsSettings(
	const VansProjectPhysicsSettingsData& settings)
{
	nlohmann::json root;
	root["fixedTimeStep"] = settings.fixedTimeStep;
	return root;
}
}
