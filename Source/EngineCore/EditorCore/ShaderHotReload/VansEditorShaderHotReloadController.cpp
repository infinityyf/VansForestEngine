#include "VansEditorShaderHotReloadController.h"

#include "../../AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace Vans
{
	namespace
	{
		constexpr auto kShaderDebounce = std::chrono::milliseconds(250);

		std::filesystem::path NormalizePath(const std::filesystem::path& path)
		{
			std::error_code ec;
			std::filesystem::path result = std::filesystem::weakly_canonical(path, ec);
			if (ec)
			{
				ec.clear();
				result = std::filesystem::absolute(path, ec);
			}
			return result.lexically_normal();
		}

		std::wstring LocalPathKey(const std::filesystem::path& path)
		{
			std::wstring key = NormalizePath(path).wstring();
#if defined(_WIN32)
			std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
			return key;
		}

		template<typename T>
		void AppendUniquePaths(std::vector<std::filesystem::path>& destination, const T& source)
		{
			std::unordered_set<std::wstring> existing;
			for (const auto& path : destination)
				existing.emplace(LocalPathKey(path));
			for (const auto& path : source)
			{
				if (existing.emplace(LocalPathKey(path)).second)
					destination.push_back(path);
			}
		}
	}

	VansEditorShaderHotReloadController::~VansEditorShaderHotReloadController()
	{
		Shutdown();
	}

	void VansEditorShaderHotReloadController::Initialize(EditorAPI::IEngineEditorAPI& engineAPI)
	{
		if (m_Initialized)
			return;

		m_Initialized = true;
		RefreshProgramRegistry(engineAPI);
		m_FileWatcher.Start();
		VANS_LOG("[ShaderHotReload] Editor shader watcher initialized for " << m_Programs.size() << " programs");
	}

	void VansEditorShaderHotReloadController::Shutdown()
	{
		if (!m_Initialized)
			return;

		m_FileWatcher.Stop();
		m_FileWatcher.ClearWatches();
		m_Programs.clear();
		m_FileDependents.clear();
		m_PendingFiles.clear();
		m_RegistryFingerprint.clear();
		m_Initialized = false;
	}

	std::wstring VansEditorShaderHotReloadController::PathKey(const std::filesystem::path& path)
	{
		return LocalPathKey(path);
	}

	std::string VansEditorShaderHotReloadController::BuildRegistryFingerprint(
		const std::vector<EditorAPI::ShaderProgramSourceSnapshot>& snapshots)
	{
		std::ostringstream stream;
		for (const auto& program : snapshots)
		{
			stream << program.programId << '|' << program.sourceFolder << '|' << program.rayTracing;
			for (const auto& stage : program.stages)
				stream << '|' << stage.stage << ':' << stage.sourcePath << ':' << stage.entryPoint;
			stream << '\n';
		}
		return stream.str();
	}

	std::filesystem::path VansEditorShaderHotReloadController::FindShaderRoot(
		const std::filesystem::path& sourcePath)
	{
		std::filesystem::path current;
		for (const auto& component : NormalizePath(sourcePath))
		{
			current /= component;
			std::wstring name = component.wstring();
#if defined(_WIN32)
			std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
			if (name == L"shaders")
				return current;
		}
		return NormalizePath(sourcePath).parent_path();
	}

	void VansEditorShaderHotReloadController::RefreshProgramRegistry(EditorAPI::IEngineEditorAPI& engineAPI)
	{
		const auto snapshots = engineAPI.QueryShaderProgramSources();
		const std::string fingerprint = BuildRegistryFingerprint(snapshots);
		if (fingerprint == m_RegistryFingerprint)
			return;

		m_RegistryFingerprint = fingerprint;
		m_Programs.clear();
		m_FileDependents.clear();
		m_PendingFiles.clear();
		m_FileWatcher.ClearWatches();

		for (const auto& snapshot : snapshots)
		{
			if (snapshot.programId.empty() || snapshot.stages.empty())
				continue;

			ProgramState state;
			state.request.programId = snapshot.programId;
			state.request.sourceFolder = NormalizePath(snapshot.sourceFolder);
			const std::filesystem::path shaderRoot = FindShaderRoot(state.request.sourceFolder);
			state.request.includeRoots.push_back(shaderRoot);
			for (const auto& stage : snapshot.stages)
			{
				VansShaderStageCompileSource source;
				source.stage = stage.stage;
				source.sourcePath = NormalizePath(stage.sourcePath);
				source.entryPoint = stage.entryPoint;
				state.request.stages.emplace_back(std::move(source));
			}

			const VansShaderDependencyScanResult dependencies = m_Compiler.ScanDependencies(state.request);
			state.resolvedDependencies = dependencies.resolvedDependencies;
			state.unresolvedDependencies = dependencies.unresolvedDependencies;
			m_Programs.emplace(snapshot.programId, std::move(state));
		}

		RebuildDependencyIndex();
		for (const auto& [programId, program] : m_Programs)
		{
			(void)programId;
			AddDependencyWatches(program);
		}

		VANS_LOG("[ShaderHotReload] Program registry refreshed: " << m_Programs.size() << " programs");
	}

	void VansEditorShaderHotReloadController::RebuildDependencyIndex()
	{
		m_FileDependents.clear();
		for (const auto& [programId, program] : m_Programs)
		{
			for (const auto& stage : program.request.stages)
				m_FileDependents[PathKey(stage.sourcePath)].insert(programId);
			for (const auto& dependency : program.resolvedDependencies)
				m_FileDependents[PathKey(dependency)].insert(programId);
			for (const auto& dependency : program.unresolvedDependencies)
				m_FileDependents[PathKey(dependency)].insert(programId);
		}
	}

	void VansEditorShaderHotReloadController::AddDependencyWatches(const ProgramState& program)
	{
		m_FileWatcher.WatchTree(program.request.sourceFolder);
		for (const auto& root : program.request.includeRoots)
			m_FileWatcher.WatchTree(root);
		for (const auto& dependency : program.resolvedDependencies)
			m_FileWatcher.WatchTree(dependency.parent_path());
		for (const auto& dependency : program.unresolvedDependencies)
			m_FileWatcher.WatchTree(dependency.parent_path());
	}

	void VansEditorShaderHotReloadController::TickAndApply(EditorAPI::IEngineEditorAPI& engineAPI)
	{
		if (!m_Initialized)
			Initialize(engineAPI);

		RefreshProgramRegistry(engineAPI);
		for (const VansFileChange& change : m_FileWatcher.DrainChanges())
		{
			if (!VansShaderCompiler::IsShaderSourceExtension(change.path))
				continue;

			const std::wstring key = PathKey(change.path);
			PendingFile& pending = m_PendingFiles[key];
			pending.path = change.path;
			pending.lastObserved = change.observedAt;
		}

		const auto now = std::chrono::steady_clock::now();
		std::unordered_set<std::string> affectedPrograms;
		for (auto it = m_PendingFiles.begin(); it != m_PendingFiles.end();)
		{
			if (now - it->second.lastObserved < kShaderDebounce)
			{
				++it;
				continue;
			}

			const auto dependentIt = m_FileDependents.find(it->first);
			if (dependentIt != m_FileDependents.end())
				affectedPrograms.insert(dependentIt->second.begin(), dependentIt->second.end());
			it = m_PendingFiles.erase(it);
		}

		std::vector<std::string> orderedPrograms(affectedPrograms.begin(), affectedPrograms.end());
		std::sort(orderedPrograms.begin(), orderedPrograms.end());
		for (const std::string& programId : orderedPrograms)
			BuildProgram(programId, engineAPI);
	}

	void VansEditorShaderHotReloadController::BuildProgram(
		const std::string& programId,
		EditorAPI::IEngineEditorAPI& engineAPI)
	{
		auto programIt = m_Programs.find(programId);
		if (programIt == m_Programs.end())
			return;

		ProgramState& program = programIt->second;
		program.request.sourceRevision = ++program.sourceRevision;
		VANS_LOG("[ShaderHotReload] Compiling '" << programId << "' revision " << program.sourceRevision);

		VansShaderArtifactPrepareResult prepared =
			VansShaderArtifactCache::Get().Prepare(program.request, false);
		VansShaderCompileResult& compileResult = prepared.compileResult;
		if (!compileResult.success)
		{
			AppendUniquePaths(program.resolvedDependencies, compileResult.dependencies.resolvedDependencies);
			AppendUniquePaths(program.unresolvedDependencies, compileResult.dependencies.unresolvedDependencies);
			RebuildDependencyIndex();
			AddDependencyWatches(program);
			for (const std::string& diagnostic : compileResult.diagnostics)
				VANS_LOG_ERROR("[ShaderHotReload] " << programId << ": " << diagnostic);
			return;
		}

		EditorAPI::ShaderCandidatePackage package;
		package.programId = programId;
		package.sourceRevision = program.sourceRevision;
		for (auto& stage : compileResult.stages)
		{
			EditorAPI::ShaderCompiledStagePackage compiledStage;
			compiledStage.stage = std::move(stage.stage);
			compiledStage.entryPoint = std::move(stage.entryPoint);
			compiledStage.spirv = std::move(stage.spirv);
			package.stages.emplace_back(std::move(compiledStage));
		}

		const EditorAPI::ShaderCandidateApplyResult applyResult =
			engineAPI.ApplyShaderCandidateAtRenderSafePoint(package);
		if (!applyResult.applied)
		{
			VANS_LOG_ERROR("[ShaderHotReload] Failed to apply '" << programId
				<< "' revision " << program.sourceRevision << ": " << applyResult.error);
			return;
		}
		if (!VansShaderArtifactCache::Get().CommitActive(prepared))
		{
			VANS_LOG_WARN("[ShaderHotReload] Applied '" << programId
				<< "' but could not update its active artifact manifest");
		}

		program.resolvedDependencies = std::move(compileResult.dependencies.resolvedDependencies);
		program.unresolvedDependencies = std::move(compileResult.dependencies.unresolvedDependencies);
		RebuildDependencyIndex();
		AddDependencyWatches(program);
		VANS_LOG("[ShaderHotReload] Applied '" << programId << "' revision " << program.sourceRevision);
	}
}
