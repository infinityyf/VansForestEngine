#pragma once

#include "../Procedural/VansAnimationRig.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace VansGraphics
{
	class VansAnimationRigStorage
	{
	public:
		static bool DeserializeFromJsonObject(const nlohmann::json& root,
		                                    VansAnimationRigAsset& asset,
		                                    std::string& error);
		static bool SerializeToJsonObject(const VansAnimationRigAsset& asset,
		                                  nlohmann::json& root,
		                                  std::string& error);
		static bool Load(const std::filesystem::path& path,
		                 VansAnimationRigAsset& asset,
		                 std::string& error);
		static bool SaveAtomic(const std::filesystem::path& path,
		                       const VansAnimationRigAsset& asset,
		                       std::string& error);
	};
}
