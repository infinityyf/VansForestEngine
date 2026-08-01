#pragma once

// Public request/result contract shared by the Editor and command-line packager.

#include <cstdint>
#include <string>

namespace Vans
{
	enum class VansGamePackagePlatform
	{
		Windows
	};

	const char* ToString(VansGamePackagePlatform platform);

	struct VansGamePackageRequest
	{
		VansGamePackagePlatform platform = VansGamePackagePlatform::Windows;
		std::string projectRootPath;
		std::string engineRootPath;
		std::string scenePath;
		std::string binarySourceDirectory;
		bool includeEngineAssets = true;
		bool includeLibrary = true;
		bool includeBinaries = true;
		bool overwriteExisting = true;
		bool useCookedResourcePlan = true;
		bool prewarmResourceCaches = true;
	};

	struct VansGamePackageResult
	{
		bool success = false;
		std::string message;
		std::string outputPath;
		std::uint64_t copiedFileCount = 0;
		std::uint64_t missingCookedArtifactCount = 0;

		explicit operator bool() const { return success; }
	};

	class VansGamePackageBuilder
	{
	public:
		static VansGamePackageResult Build(const VansGamePackageRequest& request);
	};
}
