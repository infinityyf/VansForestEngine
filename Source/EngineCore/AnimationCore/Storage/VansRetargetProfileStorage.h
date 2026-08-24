#pragma once

#include "../Retargeting/VansRetargetProfile.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace VansGraphics
{
	class VansRetargetProfileStorage
	{
	public:
		static bool DeserializeFromJsonObject(const nlohmann::json& root,
			VansRetargetProfileAsset& asset,
			std::string& error);
		static bool SerializeToJsonObject(const VansRetargetProfileAsset& asset,
			nlohmann::json& root,
			std::string& error);
		static bool Load(const std::filesystem::path& path,
			VansRetargetProfileAsset& asset,
			std::string& error);
		static bool SaveAtomic(const std::filesystem::path& path,
			const VansRetargetProfileAsset& asset,
			std::string& error);
	};
}
