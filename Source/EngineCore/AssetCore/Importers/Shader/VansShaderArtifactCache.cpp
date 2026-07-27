#include "VansShaderArtifactCache.h"

#include "../../Storage/VansFileStorage.h"
#include "../../Storage/VansJsonFileStorage.h"
#include "../../../Util/VansLog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Vans
{
	namespace
	{
		using json = nlohmann::json;
		constexpr std::uint32_t kArtifactSchemaVersion = 1;
		constexpr const char* kCompilerArguments = "-V --target-env vulkan1.2";
		constexpr std::uint64_t kMaxStageBytes = 64ull * 1024ull * 1024ull;
		std::mutex g_ArtifactWriteMutex;

		std::filesystem::path NormalizePath(const std::filesystem::path& path)
		{
			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
			if (ec)
			{
				ec.clear();
				normalized = std::filesystem::absolute(path, ec);
			}
			return normalized.lexically_normal();
		}

		std::string Lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		std::string SanitizeName(std::string value)
		{
			for (char& c : value)
			{
				const unsigned char byte = static_cast<unsigned char>(c);
				if (!std::isalnum(byte) && c != '_' && c != '-')
					c = '_';
			}
			return value.empty() ? "Shader" : value;
		}

		std::uint64_t HashBytes(const void* data, std::size_t size, std::uint64_t seed = 14695981039346656037ull)
		{
			const auto* bytes = static_cast<const std::uint8_t*>(data);
			std::uint64_t hash = seed;
			for (std::size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash == 0 ? 1 : hash;
		}

		void HashAppend(std::uint64_t& hash, const std::string& text)
		{
			hash = HashBytes(text.data(), text.size(), hash);
			const char separator = '\0';
			hash = HashBytes(&separator, 1, hash);
		}

		std::string HashToHex(std::uint64_t hash)
		{
			std::ostringstream stream;
			stream << std::hex << std::setw(16) << std::setfill('0') << hash;
			return stream.str();
		}

		bool ReadBinary(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
		{
			bytes.clear();
			std::string content;
			std::string error;
			if (!VansFileStorage::ReadAllBytes(path, content, error))
				return false;
			if (content.empty() || static_cast<std::uint64_t>(content.size()) > kMaxStageBytes)
				return false;
			bytes.assign(content.begin(), content.end());
			return true;
		}

		bool ReadSpirv(const std::filesystem::path& path, std::vector<std::uint32_t>& words)
		{
			std::vector<std::uint8_t> bytes;
			if (!ReadBinary(path, bytes) || bytes.size() % sizeof(std::uint32_t) != 0)
				return false;
			words.resize(bytes.size() / sizeof(std::uint32_t));
			std::memcpy(words.data(), bytes.data(), bytes.size());
			return !words.empty() && words.front() == 0x07230203u;
		}

		bool WriteBinary(const std::filesystem::path& path, const void* data, std::size_t size)
		{
			std::string error;
			return VansFileStorage::WriteAtomicBytes(
				path,
				std::string(reinterpret_cast<const char*>(data), size),
				error);
		}

		bool ReadJsonFile(const std::filesystem::path& path, json& root)
		{
			std::string error;
			return VansJsonFileStorage::Read(path, root, error);
		}

		bool WriteJsonFile(const std::filesystem::path& path, const json& root)
		{
			std::string error;
			return VansJsonFileStorage::WriteAtomic(path, root, error);
		}

		std::string ProgramDirectoryName(const std::string& programId)
		{
			return SanitizeName(programId) + "-" + HashToHex(HashBytes(programId.data(), programId.size()));
		}

		std::filesystem::path ProgramRoot(const std::filesystem::path& root, const std::string& programId)
		{
			return root / "Programs" / ProgramDirectoryName(programId);
		}

		std::string CompilerFingerprint()
		{
			static const std::string fingerprint = []()
			{
				std::ostringstream stream;
				stream << "glslangValidator|" << kCompilerArguments << "|artifact-schema=" << kArtifactSchemaVersion;
				if (const char* overrideValue = std::getenv("FORESTENGINE_SHADER_COMPILER_FINGERPRINT"))
				{
					stream << "|override=" << overrideValue;
					return stream.str();
				}
#if defined(_WIN32)
				std::array<wchar_t, 32768> executable{};
				const DWORD length = SearchPathW(nullptr, L"glslangValidator.exe", nullptr,
					static_cast<DWORD>(executable.size()), executable.data(), nullptr);
				if (length > 0 && length < executable.size())
				{
					std::error_code ec;
					const std::filesystem::path path(executable.data());
					stream << "|path=" << NormalizePath(path).generic_u8string();
					stream << "|size=" << std::filesystem::file_size(path, ec);
					ec.clear();
					stream << "|time=" << std::filesystem::last_write_time(path, ec).time_since_epoch().count();
				}
#endif
				return stream.str();
			}();
			return fingerprint;
		}

		bool BuildSourceFingerprint(
			const VansShaderCompileRequest& request,
			const VansShaderDependencyScanResult& dependencies,
			std::string& outFingerprint)
		{
			if (request.stages.empty() || !dependencies.unresolvedDependencies.empty())
				return false;

			std::uint64_t hash = 14695981039346656037ull;
			HashAppend(hash, CompilerFingerprint());
			for (const auto& stage : request.stages)
			{
				std::vector<std::uint8_t> bytes;
				if (!ReadBinary(stage.sourcePath, bytes))
					return false;
				HashAppend(hash, stage.stage);
				HashAppend(hash, stage.entryPoint);
				HashAppend(hash, NormalizePath(stage.sourcePath).generic_u8string());
				hash = HashBytes(bytes.data(), bytes.size(), hash);
			}

			std::vector<std::filesystem::path> sortedDependencies = dependencies.resolvedDependencies;
			std::sort(sortedDependencies.begin(), sortedDependencies.end(), [](const auto& left, const auto& right)
			{
				return NormalizePath(left).generic_u8string() < NormalizePath(right).generic_u8string();
			});
			for (const auto& dependency : sortedDependencies)
			{
				std::vector<std::uint8_t> bytes;
				if (!ReadBinary(dependency, bytes))
					return false;
				HashAppend(hash, NormalizePath(dependency).generic_u8string());
				hash = HashBytes(bytes.data(), bytes.size(), hash);
			}
			outFingerprint = HashToHex(hash);
			return true;
		}

		std::uint64_t ComputeBinaryHash(const std::vector<VansCompiledShaderStage>& stages)
		{
			std::uint64_t hash = 14695981039346656037ull;
			for (const auto& stage : stages)
			{
				HashAppend(hash, stage.stage);
				HashAppend(hash, stage.entryPoint);
				if (!stage.spirv.empty())
					hash = HashBytes(stage.spirv.data(), stage.spirv.size() * sizeof(std::uint32_t), hash);
			}
			return hash == 0 ? 1 : hash;
		}

#if defined(_WIN32)
		class ScopedFileLock
		{
		public:
			explicit ScopedFileLock(const std::filesystem::path& path)
			{
				m_Handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
					OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			}
			~ScopedFileLock()
			{
				if (m_Handle != INVALID_HANDLE_VALUE)
					CloseHandle(m_Handle);
			}
			bool IsLocked() const { return m_Handle != INVALID_HANDLE_VALUE; }
		private:
			HANDLE m_Handle = INVALID_HANDLE_VALUE;
		};
#endif
	}

	VansShaderArtifactCache& VansShaderArtifactCache::Get()
	{
		static VansShaderArtifactCache cache;
		return cache;
	}

	VansShaderArtifactPrepareResult VansShaderArtifactCache::Prepare(
		const VansShaderCompileRequest& request,
		bool allowFallbackToActive)
	{
		VansShaderArtifactPrepareResult prepared;
		prepared.artifactRoot = ResolveArtifactRoot(request);
		prepared.compileResult.programId = request.programId;
		prepared.compileResult.sourceRevision = request.sourceRevision;

		const VansShaderDependencyScanResult dependencies = m_Compiler.ScanDependencies(request);
		std::string sourceFingerprint;
		const bool sourceAvailable = BuildSourceFingerprint(request, dependencies, sourceFingerprint);
		const bool forceRecompile = std::getenv("FORESTENGINE_SHADER_FORCE_RECOMPILE") != nullptr;

		if (sourceAvailable && !forceRecompile &&
			TryLoadArtifact(request, prepared.artifactRoot, sourceFingerprint, prepared))
		{
			prepared.cacheHit = true;
			prepared.success = true;
			m_Hits.fetch_add(1, std::memory_order_relaxed);
			VANS_LOG("[ShaderArtifact] Hit '" << request.programId << "' " << sourceFingerprint);
			return prepared;
		}

		m_Misses.fetch_add(1, std::memory_order_relaxed);
		if (!IsCookedOnlyMode() && sourceAvailable)
		{
			prepared.compileResult = m_Compiler.Compile(request);
			if (prepared.compileResult.success)
			{
				prepared.artifactKey = sourceFingerprint;
				prepared.binaryHash = ComputeBinaryHash(prepared.compileResult.stages);
				prepared.success = true;
				prepared.compiled = true;
				m_Compiles.fetch_add(1, std::memory_order_relaxed);
				if (!PersistImmutableArtifact(request, prepared.artifactRoot, prepared.artifactKey, prepared.compileResult))
				{
					prepared.compileResult.diagnostics.emplace_back("Compiled successfully, but failed to persist shader artifact");
					prepared.artifactKey.clear();
				}
				VANS_LOG("[ShaderArtifact] Compiled '" << request.programId << "' " << sourceFingerprint);
				return prepared;
			}
			m_CompileFailures.fetch_add(1, std::memory_order_relaxed);
		}
		else if (IsCookedOnlyMode())
		{
			prepared.compileResult.diagnostics.emplace_back(
				"Cooked-only shader mode forbids source compiler fallback for '" + request.programId + "'");
		}
		else
		{
			prepared.compileResult.diagnostics.emplace_back(
				"Shader source/dependency set is incomplete for '" + request.programId + "'");
		}

		if (allowFallbackToActive)
		{
			const auto failureDiagnostics = prepared.compileResult.diagnostics;
			VansShaderArtifactPrepareResult fallback;
			if (TryLoadActive(request, prepared.artifactRoot, fallback))
			{
				fallback.fallbackActive = true;
				fallback.success = true;
				fallback.compileResult.diagnostics.insert(
					fallback.compileResult.diagnostics.end(), failureDiagnostics.begin(), failureDiagnostics.end());
				m_Hits.fetch_add(1, std::memory_order_relaxed);
				VANS_LOG_WARN("[ShaderArtifact] Using last-known-good artifact for '" << request.programId << "'");
				return fallback;
			}
		}

		return prepared;
	}

	bool VansShaderArtifactCache::TryLoadArtifact(
		const VansShaderCompileRequest& request,
		const std::filesystem::path& root,
		const std::string& artifactKey,
		VansShaderArtifactPrepareResult& outResult) const
	{
		if (artifactKey.empty())
			return false;
		const std::filesystem::path objectRoot = ProgramRoot(root, request.programId) / "Objects" / artifactKey;
		json manifest;
		if (!ReadJsonFile(objectRoot / "manifest.json", manifest))
			return false;
		if (manifest.value("schema", 0u) != kArtifactSchemaVersion ||
			manifest.value("programId", std::string{}) != request.programId ||
			manifest.value("artifactKey", std::string{}) != artifactKey ||
			!manifest.contains("stages") || !manifest["stages"].is_array())
			return false;

		VansShaderCompileResult loaded;
		loaded.programId = request.programId;
		loaded.sourceRevision = request.sourceRevision;
		for (const auto& stageJson : manifest["stages"])
		{
			VansCompiledShaderStage stage;
			stage.stage = stageJson.value("stage", std::string{});
			stage.entryPoint = stageJson.value("entryPoint", std::string("main"));
			const std::string file = stageJson.value("file", std::string{});
			const std::uint64_t expectedHash = stageJson.value("hash", 0ull);
			if (stage.stage.empty() || file.empty() || !ReadSpirv(objectRoot / file, stage.spirv))
				return false;
			const std::uint64_t actualHash = HashBytes(stage.spirv.data(), stage.spirv.size() * sizeof(std::uint32_t));
			if (actualHash != expectedHash)
				return false;
			loaded.stages.emplace_back(std::move(stage));
		}

		if (loaded.stages.empty())
			return false;
		if (manifest.contains("dependencies") && manifest["dependencies"].is_array())
		{
			for (const auto& dependency : manifest["dependencies"])
				if (dependency.is_string())
					loaded.dependencies.resolvedDependencies.emplace_back(dependency.get<std::string>());
		}
		loaded.success = true;
		outResult.compileResult = std::move(loaded);
		outResult.artifactRoot = root;
		outResult.artifactKey = artifactKey;
		outResult.binaryHash = ComputeBinaryHash(outResult.compileResult.stages);
		outResult.success = true;
		return true;
	}

	bool VansShaderArtifactCache::TryLoadActive(
		const VansShaderCompileRequest& request,
		const std::filesystem::path& root,
		VansShaderArtifactPrepareResult& outResult) const
	{
		json active;
		if (!ReadJsonFile(ProgramRoot(root, request.programId) / "active.json", active))
			return false;
		if (active.value("schema", 0u) != kArtifactSchemaVersion ||
			active.value("programId", std::string{}) != request.programId)
			return false;
		return TryLoadArtifact(request, root, active.value("artifactKey", std::string{}), outResult);
	}

	bool VansShaderArtifactCache::PersistImmutableArtifact(
		const VansShaderCompileRequest& request,
		const std::filesystem::path& root,
		const std::string& artifactKey,
		const VansShaderCompileResult& compileResult) const
	{
		if (!compileResult.success || compileResult.stages.empty() || artifactKey.empty())
			return false;
		std::lock_guard<std::mutex> processLock(g_ArtifactWriteMutex);
		const std::filesystem::path programRoot = ProgramRoot(root, request.programId);
		const std::filesystem::path objectRoot = programRoot / "Objects" / artifactKey;
		std::error_code ec;
		std::filesystem::create_directories(objectRoot, ec);
		if (ec)
			return false;
#if defined(_WIN32)
		ScopedFileLock fileLock(programRoot / ".write.lock");
		if (!fileLock.IsLocked())
			return false;
#endif

		json manifest;
		manifest["schema"] = kArtifactSchemaVersion;
		manifest["programId"] = request.programId;
		manifest["artifactKey"] = artifactKey;
		manifest["compiler"] = CompilerFingerprint();
		manifest["stages"] = json::array();
		for (std::size_t index = 0; index < compileResult.stages.size(); ++index)
		{
			const auto& stage = compileResult.stages[index];
			const std::string file = std::to_string(index) + "-" + SanitizeName(stage.stage) + ".spv";
			const std::size_t byteSize = stage.spirv.size() * sizeof(std::uint32_t);
			if (stage.spirv.empty() || !WriteBinary(objectRoot / file, stage.spirv.data(), byteSize))
				return false;
			manifest["stages"].push_back({
				{ "stage", stage.stage },
				{ "entryPoint", stage.entryPoint },
				{ "file", file },
				{ "wordCount", stage.spirv.size() },
				{ "hash", HashBytes(stage.spirv.data(), byteSize) }
			});
		}
		manifest["dependencies"] = json::array();
		for (const auto& dependency : compileResult.dependencies.resolvedDependencies)
			manifest["dependencies"].push_back(NormalizePath(dependency).generic_u8string());
		return WriteJsonFile(objectRoot / "manifest.json", manifest);
	}

	bool VansShaderArtifactCache::CommitActive(const VansShaderArtifactPrepareResult& prepared)
	{
		if (!prepared.success || prepared.artifactKey.empty() || prepared.compileResult.programId.empty())
			return false;
		std::lock_guard<std::mutex> processLock(g_ArtifactWriteMutex);
		const std::filesystem::path programRoot = ProgramRoot(prepared.artifactRoot, prepared.compileResult.programId);
		std::error_code ec;
		std::filesystem::create_directories(programRoot, ec);
		if (ec)
			return false;
#if defined(_WIN32)
		ScopedFileLock fileLock(programRoot / ".write.lock");
		if (!fileLock.IsLocked())
			return false;
#endif
		json active = {
			{ "schema", kArtifactSchemaVersion },
			{ "programId", prepared.compileResult.programId },
			{ "artifactKey", prepared.artifactKey },
			{ "binaryHash", prepared.binaryHash }
		};
		return WriteJsonFile(programRoot / "active.json", active);
	}

	bool VansShaderArtifactCache::ExportCookedArtifacts(
		const std::vector<VansShaderCookProgram>& programs,
		const std::filesystem::path& destinationRoot,
		std::string& error) const
	{
		error.clear();
		if (programs.empty() || destinationRoot.empty())
		{
			error = "No loaded shader programs or cooked destination was provided";
			return false;
		}

		std::lock_guard<std::mutex> processLock(g_ArtifactWriteMutex);
		json cookedManifest;
		cookedManifest["schema"] = kArtifactSchemaVersion;
		cookedManifest["programs"] = json::array();
		std::error_code ec;
		std::filesystem::create_directories(destinationRoot, ec);
		if (ec)
		{
			error = "Cannot create cooked shader destination: " + ec.message();
			return false;
		}

		for (const VansShaderCookProgram& program : programs)
		{
			const std::filesystem::path sourceProgramRoot = ProgramRoot(program.artifactRoot, program.programId);
			json active;
			if (!ReadJsonFile(sourceProgramRoot / "active.json", active))
			{
				error = "Missing or invalid active shader artifact for '" + program.programId + "'";
				return false;
			}
			const std::string artifactKey = active.value("artifactKey", std::string{});
			if (active.value("schema", 0u) != kArtifactSchemaVersion ||
				active.value("programId", std::string{}) != program.programId || artifactKey.empty() ||
				artifactKey.find("..") != std::string::npos || artifactKey.find_first_of("/\\") != std::string::npos)
			{
				error = "Active shader artifact identity is invalid for '" + program.programId + "'";
				return false;
			}

			const std::filesystem::path sourceObjectRoot = sourceProgramRoot / "Objects" / artifactKey;
			json objectManifest;
			if (!ReadJsonFile(sourceObjectRoot / "manifest.json", objectManifest))
			{
				error = "Missing shader object manifest for '" + program.programId + "'";
				return false;
			}
			if (objectManifest.value("programId", std::string{}) != program.programId ||
				objectManifest.value("artifactKey", std::string{}) != artifactKey)
			{
				error = "Shader object manifest identity mismatch for '" + program.programId + "'";
				return false;
			}

			const std::filesystem::path destinationProgramRoot = ProgramRoot(destinationRoot, program.programId);
			const std::filesystem::path destinationObjectRoot = destinationProgramRoot / "Objects" / artifactKey;
			std::filesystem::create_directories(destinationObjectRoot, ec);
			if (ec)
			{
				error = "Cannot create cooked object directory for '" + program.programId + "'";
				return false;
			}
			for (const auto& entry : std::filesystem::directory_iterator(sourceObjectRoot, ec))
			{
				if (ec || !entry.is_regular_file())
					continue;
				std::filesystem::copy_file(entry.path(), destinationObjectRoot / entry.path().filename(),
					std::filesystem::copy_options::overwrite_existing, ec);
				if (ec)
				{
					error = "Failed copying cooked shader object for '" + program.programId + "': " + ec.message();
					return false;
				}
			}
			if (!WriteJsonFile(destinationProgramRoot / "active.json", active))
			{
				error = "Failed publishing cooked active manifest for '" + program.programId + "'";
				return false;
			}
			cookedManifest["programs"].push_back({
				{ "programId", program.programId }, { "artifactKey", artifactKey }
			});
		}

		if (!WriteJsonFile(destinationRoot / "cooked-shader-manifest.json", cookedManifest))
		{
			error = "Failed publishing cooked shader root manifest";
			return false;
		}
		return true;
	}

	VansShaderArtifactCacheStats VansShaderArtifactCache::GetStats() const
	{
		return {
			m_Hits.load(std::memory_order_relaxed),
			m_Misses.load(std::memory_order_relaxed),
			m_Compiles.load(std::memory_order_relaxed),
			m_CompileFailures.load(std::memory_order_relaxed)
		};
	}

	bool VansShaderArtifactCache::IsCookedOnlyMode()
	{
		const char* mode = std::getenv("FORESTENGINE_SHADER_MODE");
		if (!mode)
			return false;
		const std::string normalized = Lower(mode);
		return normalized == "cooked" || normalized == "cookedonly" || normalized == "cooked-only";
	}

	std::filesystem::path VansShaderArtifactCache::ResolveArtifactRoot(const VansShaderCompileRequest& request)
	{
		if (!request.artifactRoot.empty())
			return NormalizePath(request.artifactRoot);
		if (IsCookedOnlyMode())
			if (const char* cookedRoot = std::getenv("FORESTENGINE_COOKED_SHADER_DIR"))
				return NormalizePath(cookedRoot);
		if (const char* overrideRoot = std::getenv("FORESTENGINE_SHADER_ARTIFACT_DIR"))
			return NormalizePath(overrideRoot);

		std::filesystem::path current = NormalizePath(request.sourceFolder);
		while (!current.empty() && current != current.root_path())
		{
			const std::string name = Lower(current.filename().string());
			if (name == "engineassets" || name == "assets")
				return current.parent_path() / "Library" / "Artifacts" / "Shaders";
			current = current.parent_path();
		}
		return std::filesystem::current_path() / "Library" / "Artifacts" / "Shaders";
	}
}
