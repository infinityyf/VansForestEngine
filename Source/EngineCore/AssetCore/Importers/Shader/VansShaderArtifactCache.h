#pragma once

#include "VansShaderCompiler.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
	struct VansShaderArtifactPrepareResult
	{
		VansShaderCompileResult compileResult;
		std::filesystem::path artifactRoot;
		std::string artifactKey;
		std::uint64_t binaryHash = 0;
		bool success = false;
		bool cacheHit = false;
		bool compiled = false;
		bool fallbackActive = false;
	};

	struct VansShaderArtifactCacheStats
	{
		std::uint64_t hits = 0;
		std::uint64_t misses = 0;
		std::uint64_t compiles = 0;
		std::uint64_t compileFailures = 0;
	};

	struct VansShaderCookProgram
	{
		std::string programId;
		std::filesystem::path artifactRoot;
	};

	// Portable source-to-SPIR-V artifact cache. Artifacts are immutable and
	// content addressed. The active pointer is only committed after the caller
	// has successfully created/applied the corresponding Vulkan shader modules.
	class VansShaderArtifactCache
	{
	public:
		static VansShaderArtifactCache& Get();

		VansShaderArtifactPrepareResult Prepare(
			const VansShaderCompileRequest& request,
			bool allowFallbackToActive = true);

		bool CommitActive(const VansShaderArtifactPrepareResult& prepared);
		bool ExportCookedArtifacts(
			const std::vector<VansShaderCookProgram>& programs,
			const std::filesystem::path& destinationRoot,
			std::string& error) const;

		VansShaderArtifactCacheStats GetStats() const;
		static bool IsCookedOnlyMode();
		static std::filesystem::path ResolveArtifactRoot(const VansShaderCompileRequest& request);

	private:
		VansShaderArtifactCache() = default;

		bool TryLoadArtifact(
			const VansShaderCompileRequest& request,
			const std::filesystem::path& root,
			const std::string& artifactKey,
			VansShaderArtifactPrepareResult& outResult) const;
		bool TryLoadActive(
			const VansShaderCompileRequest& request,
			const std::filesystem::path& root,
			VansShaderArtifactPrepareResult& outResult) const;
		bool PersistImmutableArtifact(
			const VansShaderCompileRequest& request,
			const std::filesystem::path& root,
			const std::string& artifactKey,
			const VansShaderCompileResult& compileResult) const;

		VansShaderCompiler m_Compiler;
		std::atomic<std::uint64_t> m_Hits{ 0 };
		std::atomic<std::uint64_t> m_Misses{ 0 };
		std::atomic<std::uint64_t> m_Compiles{ 0 };
		std::atomic<std::uint64_t> m_CompileFailures{ 0 };
	};
}
