#pragma once

#include "../AssetsSystem/VansAssetsFileWatcher.h"
#include "../../AssetCore/Importers/Shader/VansShaderCompiler.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
	// Editor policy owner for automatic shader rebuilds. RenderCore is reached
	// only through IEngineEditorAPI at the frame-safe call site.
	class VansEditorShaderHotReloadController
	{
	public:
		VansEditorShaderHotReloadController() = default;
		~VansEditorShaderHotReloadController();

		void Initialize(EditorAPI::IEngineEditorAPI& engineAPI);
		void Shutdown();
		void TickAndApply(EditorAPI::IEngineEditorAPI& engineAPI);

	private:
		struct ProgramState
		{
			VansShaderCompileRequest request;
			std::vector<std::filesystem::path> resolvedDependencies;
			std::vector<std::filesystem::path> unresolvedDependencies;
			std::uint64_t sourceRevision = 0;
		};

		struct PendingFile
		{
			std::filesystem::path path;
			std::chrono::steady_clock::time_point lastObserved;
		};

		static std::wstring PathKey(const std::filesystem::path& path);
		static std::string BuildRegistryFingerprint(
			const std::vector<EditorAPI::ShaderProgramSourceSnapshot>& snapshots);
		static std::filesystem::path FindShaderRoot(const std::filesystem::path& sourcePath);

		void RefreshProgramRegistry(EditorAPI::IEngineEditorAPI& engineAPI);
		void RebuildDependencyIndex();
		void AddDependencyWatches(const ProgramState& program);
		void BuildProgram(const std::string& programId, EditorAPI::IEngineEditorAPI& engineAPI);

		VansAssetsFileWatcher m_FileWatcher;
		VansShaderCompiler m_Compiler;
		std::unordered_map<std::string, ProgramState> m_Programs;
		std::unordered_map<std::wstring, std::unordered_set<std::string>> m_FileDependents;
		std::unordered_map<std::wstring, PendingFile> m_PendingFiles;
		std::string m_RegistryFingerprint;
		bool m_Initialized = false;
	};
}
