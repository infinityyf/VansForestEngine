#pragma once

#include "../Retargeting/VansRetargetProfile.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
	class VansRetargetProfileStorage
	{
	public:
		static bool Load(const std::filesystem::path& path,
			VansRetargetProfileAsset& asset,
			std::string& error);
		static bool SaveAtomic(const std::filesystem::path& path,
			const VansRetargetProfileAsset& asset,
			std::string& error);
	};
}
