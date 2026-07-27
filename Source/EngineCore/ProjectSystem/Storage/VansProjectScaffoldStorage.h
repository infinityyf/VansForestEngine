#pragma once

#include <filesystem>
#include <string>

namespace Vans
{
class VansProjectScaffoldStorage
{
public:
	static bool EnsureDefaultPythonRequirementsFile(
		const std::filesystem::path& path,
		bool& created,
		std::string& error);
};
}
