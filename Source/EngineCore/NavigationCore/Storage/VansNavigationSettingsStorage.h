#pragma once

#include "../VansNavigationTypes.h"

#include <filesystem>

namespace Vans
{
class VansNavigationSettingsStorage
{
public:
	static bool Load(const std::filesystem::path& path,
		VansNavigationSettings& settings, std::string& error);
	static bool Save(const std::filesystem::path& path,
		const VansNavigationSettings& settings, std::string& error);
};
}
