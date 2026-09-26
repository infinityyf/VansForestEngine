#include "VansEnginePaths.h"

#include <Windows.h>

#include <array>
#include <system_error>

namespace Vans
{
namespace
{
bool IsEngineRoot(const std::filesystem::path& candidate)
{
	std::error_code error;
	return std::filesystem::is_directory(candidate / "EngineAssets", error) && !error;
}

std::string NormalizeDirectory(const std::filesystem::path& path)
{
	std::string normalized = path.lexically_normal().generic_string();
	if (!normalized.empty() && normalized.back() != '/')
		normalized.push_back('/');
	return normalized;
}
} // namespace

bool VansEnginePaths::FindEngineRoot(const std::filesystem::path& startPath,
							 std::string& engineRoot, std::string& error)
{
	engineRoot.clear();
	error.clear();
	if (startPath.empty())
	{
		error = "Engine-root discovery requires a non-empty start path.";
		return false;
	}

	std::error_code canonicalError;
	std::filesystem::path current = std::filesystem::weakly_canonical(startPath, canonicalError);
	if (canonicalError)
		current = std::filesystem::absolute(startPath, canonicalError).lexically_normal();
	if (canonicalError)
	{
		error = "Cannot normalize engine-root discovery path: " + startPath.string();
		return false;
	}
	if (std::filesystem::is_regular_file(current, canonicalError))
		current = current.parent_path();

	while (!current.empty())
	{
		if (IsEngineRoot(current))
		{
			engineRoot = NormalizeDirectory(current);
			return true;
		}
		const std::filesystem::path parent = current.parent_path();
		if (parent == current)
			break;
		current = parent;
	}

	error = "Cannot find an EngineAssets directory above: " + startPath.string();
	return false;
}

bool VansEnginePaths::DiscoverEngineRoot(std::string& engineRoot, std::string& error)
{
	std::array<wchar_t, 32768> executablePath{};
	const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
										  static_cast<DWORD>(executablePath.size()));
	if (length == 0 || length >= executablePath.size())
	{
		error = "Cannot resolve the running executable path.";
		engineRoot.clear();
		return false;
	}
	return FindEngineRoot(std::filesystem::path(executablePath.data()), engineRoot, error);
}

bool VansEnginePaths::NormalizeEngineRoot(const std::filesystem::path& candidate,
								  std::string& engineRoot, std::string& error)
{
	engineRoot.clear();
	error.clear();
	std::error_code canonicalError;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, canonicalError);
	if (canonicalError || !IsEngineRoot(normalized))
	{
		error = "Engine root must contain an EngineAssets directory: " + candidate.string();
		return false;
	}
	engineRoot = NormalizeDirectory(normalized);
	return true;
}

} // namespace Vans
