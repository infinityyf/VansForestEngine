#include "VansProjectScaffoldStorage.h"

#include "../../AssetCore/Storage/VansFileStorage.h"

namespace Vans
{
namespace
{
constexpr const char* kDefaultLuaScriptTemplate =
	"local M = {}\n"
	"\n"
	"M.EmptyBehaviour = {\n"
	"    __fields = {},\n"
	"}\n"
	"\n"
	"function M.EmptyBehaviour:on_start()\n"
	"end\n"
	"\n"
	"function M.EmptyBehaviour:on_update(dt)\n"
	"end\n"
	"\n"
	"return M\n";
}

bool VansProjectScaffoldStorage::EnsureDefaultLuaScriptFile(
	const std::filesystem::path& path,
	bool& created,
	std::string& error)
{
	created = false;
	error.clear();

	std::error_code ec;
	if (std::filesystem::exists(path, ec))
		return true;
	if (ec)
	{
		error = ec.message();
		return false;
	}

	if (!VansFileStorage::WriteAtomicBytes(path, kDefaultLuaScriptTemplate, error))
		return false;

	created = true;
	return true;
}
}
