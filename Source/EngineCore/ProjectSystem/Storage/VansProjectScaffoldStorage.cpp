#include "VansProjectScaffoldStorage.h"

#include "../../AssetCore/Storage/VansFileStorage.h"

namespace Vans
{
namespace
{
constexpr const char* kDefaultPythonRequirementsTemplate =
	"# 在此处列出项目依赖的 Python 第三方库，每行一个\n"
	"# 示例:\n"
	"# matplotlib\n"
	"# numpy\n";
}

bool VansProjectScaffoldStorage::EnsureDefaultPythonRequirementsFile(
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

	if (!VansFileStorage::WriteAtomicBytes(path, kDefaultPythonRequirementsTemplate, error))
		return false;

	created = true;
	return true;
}
}
