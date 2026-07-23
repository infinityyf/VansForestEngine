#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
	struct VansShaderStageCompileSource
	{
		std::string stage;
		std::filesystem::path sourcePath;
		std::string entryPoint = "main";
	};

	struct VansShaderCompileRequest
	{
		std::string programId;
		std::filesystem::path sourceFolder;
		std::vector<VansShaderStageCompileSource> stages;
		std::vector<std::filesystem::path> includeRoots;
		std::filesystem::path artifactRoot;
		std::uint64_t sourceRevision = 0;
	};

	struct VansCompiledShaderStage
	{
		std::string stage;
		std::string entryPoint = "main";
		std::vector<std::uint32_t> spirv;
	};

	struct VansShaderDependencyScanResult
	{
		std::vector<std::filesystem::path> resolvedDependencies;
		std::vector<std::filesystem::path> unresolvedDependencies;
		std::vector<std::string> diagnostics;
	};

	struct VansShaderCompileResult
	{
		std::string programId;
		std::uint64_t sourceRevision = 0;
		bool success = false;
		std::vector<VansCompiledShaderStage> stages;
		VansShaderDependencyScanResult dependencies;
		std::vector<std::string> diagnostics;
	};

	// Source-to-SPIR-V tooling service. It owns no Vulkan objects and never
	// watches the filesystem; Editor code decides when it is invoked.
	class VansShaderCompiler
	{
	public:
		VansShaderDependencyScanResult ScanDependencies(const VansShaderCompileRequest& request) const;
		VansShaderCompileResult Compile(const VansShaderCompileRequest& request) const;

		static bool IsShaderSourceExtension(const std::filesystem::path& path);
	};
}
