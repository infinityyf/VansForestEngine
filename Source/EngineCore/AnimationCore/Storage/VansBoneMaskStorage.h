#pragma once

#include "../VansBoneMask.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace VansGraphics
{
	class VansBoneMaskStorage
	{
	public:
		// The editor document and file loader use the same strict canonical codec.
		// This keeps Undo/Redo snapshots, preview compilation and disk persistence
		// on one schema path.
		static bool SerializeToJsonObject(const VansBoneMaskAsset& asset,
		                                  nlohmann::json& root,
		                                  std::string& error);
		static bool DeserializeFromJsonObject(const nlohmann::json& root,
		                                    VansBoneMaskAsset& asset,
		                                    std::string& error);
		static bool Load(const std::filesystem::path& path, VansBoneMaskAsset& asset,
		                 std::string& error);
		static bool SaveAtomic(const std::filesystem::path& path, const VansBoneMaskAsset& asset,
		                       std::string& error);
	};
}
