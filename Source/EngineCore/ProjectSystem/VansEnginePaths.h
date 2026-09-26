#pragma once

#include <filesystem>
#include <string>

namespace Vans
{

class VansEnginePaths
{
  public:
	// Locate the nearest ancestor containing EngineAssets/.  Passing an
	// explicit start path keeps tests and tools independent of process layout.
	static bool FindEngineRoot(const std::filesystem::path& startPath,
						   std::string& engineRoot, std::string& error);

	// Resolve the running executable and then apply FindEngineRoot().
	static bool DiscoverEngineRoot(std::string& engineRoot, std::string& error);

	// Validate and normalize an explicitly supplied engine root.
	static bool NormalizeEngineRoot(const std::filesystem::path& candidate,
							std::string& engineRoot, std::string& error);
};

} // namespace Vans
