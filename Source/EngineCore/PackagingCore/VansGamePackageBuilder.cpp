#include "VansGamePackageBuilder.h"
#include "VansPackageResourcePrewarmer.h"

#include "../AnimationCore/VansAnimationClip.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../ProjectSystem/VansProjectConfig.h"
#include "../RenderCore/VansShaderManager.h"
#include "../RenderCore/VulkanCore/VansPipelineDescriptor.h"
#include "../RuntimeCore/VansPackageManifest.h"
#include "../SceneCore/VansPackagedResourcePlan.h"
#include "../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../SceneCore/VansSceneResourceLoadContext.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
	std::string NormalizeForLog(fs::path path)
	{
		return path.lexically_normal().generic_string();
	}

	fs::path ExistingCanonicalOrAbsolute(const fs::path& path)
	{
		std::error_code ec;
		fs::path canonical = fs::weakly_canonical(path, ec);
		if (!ec)
			return canonical;
		return fs::absolute(path).lexically_normal();
	}

	fs::path GetCurrentExecutableDirectory()
	{
#ifdef _WIN32
		char buffer[MAX_PATH]{};
		const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			return fs::path(buffer).parent_path();
#endif
		return fs::current_path();
	}

	bool SamePathPrefix(const fs::path& childPath, const fs::path& parentPath)
	{
		const fs::path child = childPath.lexically_normal();
		const fs::path parent = parentPath.lexically_normal();

		auto childIt = child.begin();
		auto parentIt = parent.begin();
		for (; parentIt != parent.end(); ++parentIt, ++childIt)
		{
			if (childIt == child.end())
				return false;

			std::string childPart = childIt->string();
			std::string parentPart = parentIt->string();
			std::transform(childPart.begin(), childPart.end(), childPart.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(parentPart.begin(), parentPart.end(), parentPart.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (childPart != parentPart)
				return false;
		}
		return true;
	}

	std::string SanitizeName(std::string value)
	{
		if (value.empty())
			value = "Package";
		for (char& c : value)
		{
			const unsigned char uc = static_cast<unsigned char>(c);
			if (!std::isalnum(uc) && c != '_' && c != '-')
				c = '_';
		}
		while (!value.empty() && value.front() == '_') value.erase(value.begin());
		while (!value.empty() && value.back() == '_') value.pop_back();
		if (value.empty())
			value = "Package";
		if (value.size() > 96)
			value.resize(96);
		return value;
	}

	std::string UtcTimestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
		std::tm utcTime{};
#ifdef _WIN32
		gmtime_s(&utcTime, &nowTime);
#else
		gmtime_r(&nowTime, &utcTime);
#endif
		char buffer[32]{};
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
		return buffer;
	}

	fs::path MakeUniqueSiblingPath(const fs::path& parent, const std::string& prefix)
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		return parent / (prefix + "." + std::to_string(nonce));
	}

	struct PackageEntryMove
	{
		fs::path source;
		fs::path destination;
	};

	bool MovePackageDirectoryContents(
		const fs::path& sourceDirectory,
		const fs::path& destinationDirectory,
		std::vector<PackageEntryMove>& moves,
		std::vector<fs::path>& createdDirectories,
		std::string& error)
	{
		std::error_code ec;
		std::vector<fs::path> entries;
		for (fs::directory_iterator it(sourceDirectory, ec), end; !ec && it != end; it.increment(ec))
			entries.push_back(it->path());
		if (ec)
		{
			error = "Cannot enumerate package directory " + NormalizeForLog(sourceDirectory) + ": " + ec.message();
			return false;
		}

		for (const fs::path& source : entries)
		{
			const fs::path destination = destinationDirectory / source.filename();
			const bool destinationExists = fs::exists(destination, ec);
			if (ec)
			{
				error = "Cannot inspect package destination " + NormalizeForLog(destination) + ": " + ec.message();
				return false;
			}

			if (destinationExists)
			{
				if (!fs::is_directory(source, ec) || !fs::is_directory(destination, ec) || ec)
				{
					error = "Package destination already exists and cannot be merged: " + NormalizeForLog(destination);
					return false;
				}
				if (!MovePackageDirectoryContents(source, destination, moves, createdDirectories, error))
					return false;
				continue;
			}

			fs::rename(source, destination, ec);
			if (!ec)
			{
				moves.push_back({ source, destination });
				continue;
			}

			// Windows may deny renaming a directory whose root handle is open while
			// still allowing its children to move. Preserve the directory shell and
			// recursively transfer its contents without copying large package files.
			const std::string renameError = ec.message();
			ec.clear();
			if (!fs::is_directory(source, ec) || ec)
			{
				error = "Cannot move package entry " + NormalizeForLog(source) + ": " + renameError;
				return false;
			}
			fs::create_directory(destination, ec);
			if (ec)
			{
				error = "Cannot create package directory " + NormalizeForLog(destination) + ": " + ec.message();
				return false;
			}
			createdDirectories.push_back(destination);
			if (!MovePackageDirectoryContents(source, destination, moves, createdDirectories, error))
				return false;
		}
		return true;
	}

	void RollbackPackageEntryMoves(
		const std::vector<PackageEntryMove>& moves,
		const std::vector<fs::path>& createdDirectories,
		std::string& error,
		const char* failureLabel)
	{
		for (auto it = moves.rbegin(); it != moves.rend(); ++it)
		{
			std::error_code restoreError;
			fs::rename(it->destination, it->source, restoreError);
			if (restoreError)
				error += std::string("; ") + failureLabel + ": " + restoreError.message();
		}
		for (auto it = createdDirectories.rbegin(); it != createdDirectories.rend(); ++it)
		{
			std::error_code cleanupError;
			fs::remove(*it, cleanupError);
		}
	}

	bool RemovePackageDirectoryContents(const fs::path& directory, std::string& error)
	{
		std::error_code ec;
		std::vector<fs::path> entries;
		for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
			entries.push_back(it->path());
		if (ec)
		{
			error = "Cannot enumerate old package directory " + NormalizeForLog(directory) + ": " + ec.message();
			return false;
		}

		for (const fs::path& entry : entries)
		{
			fs::remove_all(entry, ec);
			if (!ec)
				continue;

			// A directory root can remain open on Windows even after all of its
			// children are deletable. Recursively clear it and retain only the empty
			// shell so the new package can be merged into the stable output path.
			ec.clear();
			if (!fs::is_directory(entry, ec) || ec || !RemovePackageDirectoryContents(entry, error))
			{
				if (error.empty())
					error = "Cannot delete old package entry " + NormalizeForLog(entry) + ": " + ec.message();
				return false;
			}

			ec.clear();
			fs::remove(entry, ec);
			if (ec)
			{
				ec.clear();
				if (!fs::is_empty(entry, ec) || ec)
				{
					error = "Cannot delete old package directory " + NormalizeForLog(entry) + ": " + ec.message();
					return false;
				}
			}
		}
		return true;
	}

	bool PublishPackageDirectoryContents(
		const fs::path& stagingDirectory,
		const fs::path& outputDirectory,
		std::string& error)
	{
		std::vector<PackageEntryMove> stagedMoves;
		std::vector<fs::path> stagedCreatedDirectories;
		if (!MovePackageDirectoryContents(
			stagingDirectory,
			outputDirectory,
			stagedMoves,
			stagedCreatedDirectories,
			error))
		{
			RollbackPackageEntryMoves(stagedMoves, stagedCreatedDirectories, error, "staged package rollback failed");
			return false;
		}

		std::error_code cleanupError;
		fs::remove_all(stagingDirectory, cleanupError);
		if (cleanupError)
			VANS_LOG("[Package] Warning: could not remove empty staging directory: " << cleanupError.message());
		return true;
	}

	bool PublishPackageDirectory(
		const fs::path& stagingDirectory,
		const fs::path& outputDirectory,
		std::string& error)
	{
		std::error_code ec;
		if (fs::exists(outputDirectory, ec))
		{
			if (!RemovePackageDirectoryContents(outputDirectory, error))
				return false;

			ec.clear();
			fs::remove(outputDirectory, ec);
			if (ec)
			{
				ec.clear();
				return PublishPackageDirectoryContents(stagingDirectory, outputDirectory, error);
			}
		}

		ec.clear();
		fs::rename(stagingDirectory, outputDirectory, ec);
		if (!ec)
			return true;

		error = "Cannot publish staged package: " + ec.message();
		return false;
	}

	bool CopyFileTo(const fs::path& source, const fs::path& destination, std::uint64_t& copiedFiles, std::string& error)
	{
		std::error_code ec;
		if (!fs::exists(source, ec) || !fs::is_regular_file(source, ec))
			return true;

		fs::create_directories(destination.parent_path(), ec);
		if (ec)
		{
			error = "Cannot create directory: " + NormalizeForLog(destination.parent_path());
			return false;
		}

		fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			error = "Cannot copy file: " + NormalizeForLog(source) + " -> " + NormalizeForLog(destination);
			return false;
		}

		++copiedFiles;
		return true;
	}

	bool CopyDirectoryTo(const fs::path& sourceDir, const fs::path& destinationDir, std::uint64_t& copiedFiles, std::string& error)
	{
		std::error_code ec;
		if (!fs::exists(sourceDir, ec))
			return true;
		if (!fs::is_directory(sourceDir, ec))
		{
			error = "Expected directory: " + NormalizeForLog(sourceDir);
			return false;
		}

		fs::create_directories(destinationDir, ec);
		if (ec)
		{
			error = "Cannot create directory: " + NormalizeForLog(destinationDir);
			return false;
		}

		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(sourceDir, ec))
		{
			if (ec)
			{
				error = "Cannot enumerate directory: " + NormalizeForLog(sourceDir);
				return false;
			}

			const fs::path relativePath = fs::relative(entry.path(), sourceDir, ec);
			if (ec)
			{
				error = "Cannot calculate relative path for: " + NormalizeForLog(entry.path());
				return false;
			}

			const fs::path destination = destinationDir / relativePath;
			if (entry.is_directory(ec))
			{
				fs::create_directories(destination, ec);
				if (ec)
				{
					error = "Cannot create directory: " + NormalizeForLog(destination);
					return false;
				}
			}
			else if (entry.is_regular_file(ec))
			{
				if (!CopyFileTo(entry.path(), destination, copiedFiles, error))
					return false;
			}
		}

		return true;
	}

	bool CopyDirectoryToFiltered(
		const fs::path& sourceDir,
		const fs::path& destinationDir,
		const std::function<bool(const fs::path&)>& shouldCopyRelativePath,
		std::uint64_t& copiedFiles,
		std::string& error)
	{
		std::error_code ec;
		if (!fs::exists(sourceDir, ec))
			return true;
		if (!fs::is_directory(sourceDir, ec))
		{
			error = "Expected directory: " + NormalizeForLog(sourceDir);
			return false;
		}

		fs::create_directories(destinationDir, ec);
		if (ec)
		{
			error = "Cannot create directory: " + NormalizeForLog(destinationDir);
			return false;
		}

		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(sourceDir, ec))
		{
			if (ec)
			{
				error = "Cannot enumerate directory: " + NormalizeForLog(sourceDir);
				return false;
			}

			const fs::path relativePath = fs::relative(entry.path(), sourceDir, ec);
			if (ec)
			{
				error = "Cannot calculate relative path for: " + NormalizeForLog(entry.path());
				return false;
			}

			const fs::path destination = destinationDir / relativePath;
			if (entry.is_directory(ec))
			{
				fs::create_directories(destination, ec);
				if (ec)
				{
					error = "Cannot create directory: " + NormalizeForLog(destination);
					return false;
				}
			}
			else if (entry.is_regular_file(ec))
			{
				if (shouldCopyRelativePath && !shouldCopyRelativePath(relativePath))
					continue;
				if (!CopyFileTo(entry.path(), destination, copiedFiles, error))
					return false;
			}
		}

		return true;
	}

	std::string ShaderStageName(const fs::path& path)
	{
		std::string extension = path.extension().string();
		if (!extension.empty() && extension.front() == '.')
			extension.erase(extension.begin());
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		static const std::unordered_set<std::string> stages = {
			"vert", "frag", "comp", "geom", "tesc", "tese",
			"rgen", "rmiss", "rchit", "rahit", "rint", "rcall"
		};
		return stages.find(extension) == stages.end() ? std::string{} : extension;
	}

	bool CookRegisteredEngineShaders(
		const fs::path& engineRoot,
		const fs::path& authoringArtifactRoot,
		const Vans::VansSceneResourceBuildPlan& resourcePlan,
		const fs::path& packageArtifactRoot,
		std::uint64_t& copiedFiles,
		std::string& error)
	{
		auto& manager = VansGraphics::VansShaderManager::Get();
		if (manager.FindShaderEntry("Unlit") == nullptr)
			RegisterEngineShaders();

	std::vector<Vans::VansShaderCookProgram> programs;
	std::unordered_set<std::string> programIds;
	bool success = true;
	manager.ForEachShader([&](const VansGraphics::VansShaderRecord& record)
	{
			if (!success || record.entry.relativePath.empty())
				return;
			const fs::path relativeFolder(record.entry.relativePath);
			std::string normalizedFolder = relativeFolder.lexically_normal().generic_string();
			std::transform(normalizedFolder.begin(), normalizedFolder.end(), normalizedFolder.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (normalizedFolder.rfind("engineassets/shaders/", 0) != 0)
				return;

			Vans::VansShaderCompileRequest request;
			request.programId = record.entry.name;
			request.sourceFolder = engineRoot / relativeFolder;
			request.includeRoots.push_back(engineRoot / "EngineAssets" / "Shaders");
			request.artifactRoot = authoringArtifactRoot;

			if (!record.entry.explicitStageFiles.empty())
			{
				for (const auto& [stage, file] : record.entry.explicitStageFiles)
				{
					(void)stage;
					fs::path sourcePath(file);
					if (sourcePath.is_relative())
						sourcePath = request.sourceFolder / sourcePath;
					const std::string stageName = ShaderStageName(sourcePath);
					if (!stageName.empty())
						request.stages.push_back({ stageName, sourcePath, "main" });
				}
			}
			else
			{
				std::error_code enumerateError;
				for (const fs::directory_entry& entry : fs::directory_iterator(request.sourceFolder, enumerateError))
				{
					if (enumerateError || !entry.is_regular_file())
						continue;
					const std::string stageName = ShaderStageName(entry.path());
					if (!stageName.empty())
						request.stages.push_back({ stageName, entry.path(), "main" });
				}
				if (enumerateError)
				{
					error = "Cannot enumerate engine shader program '" + record.entry.name
						+ "': " + enumerateError.message();
					success = false;
					return;
				}
			}

			if (request.stages.empty())
			{
				error = "Engine shader program has no compilable stages: " + record.entry.name;
				success = false;
				return;
			}
			auto prepared = Vans::VansShaderArtifactCache::Get().Prepare(request, false);
			if (!prepared.success || !Vans::VansShaderArtifactCache::Get().CommitActive(prepared))
			{
				error = "Failed to cook engine shader program: " + record.entry.name;
				if (!prepared.compileResult.diagnostics.empty())
					error += " (" + prepared.compileResult.diagnostics.front() + ")";
				success = false;
				return;
			}
			if (programIds.insert(request.programId).second)
				programs.push_back({ request.programId, authoringArtifactRoot });
		});
		if (!success)
			return false;
		for (const auto& shader : resourcePlan.shaders)
		{
			if (shader.name.empty())
				continue;
			if (programIds.insert(shader.name).second)
				programs.push_back({ shader.name, authoringArtifactRoot });
		}
		if (!Vans::VansShaderArtifactCache::Get().ExportCookedArtifacts(
			programs, packageArtifactRoot, error))
			return false;

		std::error_code countError;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(packageArtifactRoot, countError))
			if (!countError && entry.is_regular_file())
				++copiedFiles;
		if (countError)
		{
			error = "Cannot count packaged shader artifacts: " + countError.message();
			return false;
		}
		VANS_LOG("[PackageShaderCook] Published " << programs.size()
			<< " registered/project shader programs to " << NormalizeForLog(packageArtifactRoot));
		return true;
	}

	std::string NormalizeLookupPath(fs::path path)
	{
		std::string value = path.lexically_normal().generic_string();
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	const char* AssetTypeToString(Vans::VansAssetType type)
	{
		switch (type)
		{
		case Vans::VansAssetType::Model: return "model";
		case Vans::VansAssetType::Texture: return "texture";
		case Vans::VansAssetType::Material: return "material";
		case Vans::VansAssetType::Shader: return "shader";
		case Vans::VansAssetType::Audio: return "audio";
		case Vans::VansAssetType::Video: return "video";
		case Vans::VansAssetType::Scene: return "scene";
		case Vans::VansAssetType::Particle: return "particle";
		case Vans::VansAssetType::AnimationClip: return "animationClip";
		case Vans::VansAssetType::AnimatorController: return "animatorController";
		case Vans::VansAssetType::BoneMask: return "boneMask";
		case Vans::VansAssetType::Timeline: return "timeline";
		case Vans::VansAssetType::ClothProfile: return "clothProfile";
		case Vans::VansAssetType::PostProcessProfile: return "postProcessProfile";
		case Vans::VansAssetType::RagdollProfile: return "ragdollProfile";
		case Vans::VansAssetType::AudioReverbPreset: return "audioReverbPreset";
		case Vans::VansAssetType::AudioBusSnapshot: return "audioBusSnapshot";
		case Vans::VansAssetType::AudioDuckingRules: return "audioDuckingRules";
		default: return "unknown";
		}
	}

	const char* ArtifactFormatToString(Vans::VansAssetArtifactFormat format)
	{
		switch (format)
		{
		case Vans::VansAssetArtifactFormat::Imported: return "imported";
		case Vans::VansAssetArtifactFormat::Source: return "source";
		default: return "none";
		}
	}

	bool TextureNeedsPackagedSourceFallback(const std::string& sourcePath)
	{
		const std::string normalized = NormalizeLookupPath(sourcePath);
		return normalized.find("assets/pcg/") != std::string::npos ||
			normalized.find("assets/textures/terrain/") != std::string::npos;
	}

	struct MissingCookedResource
	{
		std::string type;
		std::string name;
		std::string sourcePath;
		std::string artifactPath;
	};

	struct CookedPackagePlanBuild
	{
		struct CacheCopy
		{
			fs::path sourcePath;
			fs::path packageRelativePath;
			bool directory = false;
		};

		bool enabled = false;
		bool prewarmEnabled = false;
		Vans::VansPackageResourcePrewarmResult prewarmResult;
		Vans::VansPackagedResourcePlan packagePlan;
		std::unordered_set<std::string> cookedSourceAssets;
		std::vector<fs::path> artifactPaths;
		std::vector<CacheCopy> cacheCopies;
		std::vector<MissingCookedResource> missingResources;
		fs::path planRelativePath = Vans::VansPackagedResourcePlanIO::DefaultRelativePath();
		fs::path reportRelativePath = "Library/Package/ResourcePlanReport.json";
	};

	fs::path RegisterSourceFormatCache(
		const std::string& guid,
		const fs::path& sourcePath,
		bool directory,
		CookedPackagePlanBuild& cookedPlan)
	{
		const fs::path relativeRoot = fs::path("Library") / "Artifacts" / "Resources" / guid;
		const fs::path relativePath = directory
			? relativeRoot
			: relativeRoot / sourcePath.filename();
		cookedPlan.cacheCopies.push_back({ sourcePath, relativePath, directory });
		return relativePath;
	}

	bool MakeProjectRelative(const fs::path& projectRoot, const fs::path& path, fs::path& outRelativePath)
	{
		std::error_code ec;
		const fs::path absolutePath = path.is_absolute()
			? path.lexically_normal()
			: (projectRoot / path).lexically_normal();
		if (!SamePathPrefix(absolutePath, projectRoot))
			return false;
		outRelativePath = fs::relative(absolutePath, projectRoot, ec);
		return !ec;
	}

	void RegisterCookedSourceAsset(
		const fs::path& projectRoot,
		const std::string& sourcePath,
		std::unordered_set<std::string>& cookedSourceAssets)
	{
		if (sourcePath.empty())
			return;

		fs::path relativePath;
		if (!MakeProjectRelative(projectRoot, fs::path(sourcePath), relativePath))
			return;

		const std::string normalized = NormalizeLookupPath(relativePath);
		cookedSourceAssets.insert(normalized);
		cookedSourceAssets.insert(normalized + ".meta");
	}

	bool CopyReferencedArtifact(
		const fs::path& projectRoot,
		const fs::path& contentRoot,
		const fs::path& artifactPath,
		std::unordered_set<std::string>& copiedArtifactSet,
		std::uint64_t& copiedFiles,
		std::string& error)
	{
		if (artifactPath.empty())
			return true;

		const fs::path source = ExistingCanonicalOrAbsolute(artifactPath);
		const std::string key = NormalizeLookupPath(source);
		if (!copiedArtifactSet.insert(key).second)
			return true;

		fs::path relativePath;
		if (!MakeProjectRelative(projectRoot, source, relativePath))
		{
			error = "Cooked artifact must be inside project root: " + NormalizeForLog(source);
			return false;
		}

		return CopyFileTo(source, contentRoot / relativePath, copiedFiles, error);
	}

	void WriteMissingCookedResourceLog(const std::vector<MissingCookedResource>& missingResources)
	{
		for (const MissingCookedResource& missing : missingResources)
		{
			VANS_LOG_WARN("[PackageResourcePlan] Missing cooked " << missing.type
				<< " artifact, keeping source fallback: " << missing.sourcePath
				<< " artifact=" << missing.artifactPath);
		}
	}

	bool WriteCookedResourcePlanReport(
		const fs::path& reportPath,
		const CookedPackagePlanBuild& cookedPlan,
		std::string& error)
	{
		try
		{
			nlohmann::ordered_json root;
			root["format"] = "ForestPackageResourcePlanReport";
			root["resourcePlan"] = cookedPlan.planRelativePath.generic_string();
			root["prewarm"] = {
				{ "enabled", cookedPlan.prewarmEnabled },
				{ "meshChecked", cookedPlan.prewarmResult.meshChecked },
				{ "meshCooked", cookedPlan.prewarmResult.meshCooked },
				{ "meshUpToDate", cookedPlan.prewarmResult.meshUpToDate },
				{ "meshNotEligible", cookedPlan.prewarmResult.meshNotEligible },
				{ "meshFailed", cookedPlan.prewarmResult.meshFailed },
				{ "textureChecked", cookedPlan.prewarmResult.textureChecked },
				{ "textureCooked", cookedPlan.prewarmResult.textureCooked },
				{ "textureUpToDate", cookedPlan.prewarmResult.textureUpToDate },
				{ "textureNotEligible", cookedPlan.prewarmResult.textureNotEligible },
				{ "textureFailed", cookedPlan.prewarmResult.textureFailed }
			};
			root["prewarmErrors"] = nlohmann::ordered_json::array();
			for (const std::string& prewarmError : cookedPlan.prewarmResult.errors)
				root["prewarmErrors"].push_back(prewarmError);
			root["cookedMeshCount"] = 0;
			root["cookedTextureCount"] = 0;
			for (const auto& mesh : cookedPlan.packagePlan.resourcePlan.meshes)
				if (mesh.cookedOnly)
					root["cookedMeshCount"] = root["cookedMeshCount"].get<int>() + 1;
			for (const auto& texture : cookedPlan.packagePlan.resourcePlan.textures)
				if (texture.cookedOnly)
					root["cookedTextureCount"] = root["cookedTextureCount"].get<int>() + 1;

			root["missingCookedArtifacts"] = nlohmann::ordered_json::array();
			for (const MissingCookedResource& missing : cookedPlan.missingResources)
			{
				nlohmann::ordered_json entry;
				entry["type"] = missing.type;
				entry["name"] = missing.name;
				entry["sourcePath"] = missing.sourcePath;
				entry["artifactPath"] = missing.artifactPath;
				root["missingCookedArtifacts"].push_back(std::move(entry));
			}

			std::error_code ec;
			fs::create_directories(reportPath.parent_path(), ec);
			if (ec)
			{
				error = "Cannot create package report directory: " + NormalizeForLog(reportPath.parent_path());
				return false;
			}

			std::ofstream file(reportPath, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				error = "Cannot write package resource report: " + NormalizeForLog(reportPath);
				return false;
			}
			file << root.dump(2) << '\n';
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool BuildCookedPackagePlan(
		const fs::path& projectRoot,
		const fs::path& engineRoot,
		const fs::path& scenePath,
		bool prewarmResourceCaches,
		CookedPackagePlanBuild& cookedPlan,
		std::string& error)
	{
		Vans::VansProjectConfig projectConfig;
		if (!projectConfig.LoadFromFile((projectRoot / "ForestProject.json").string()))
		{
			error = "Cannot load project config for cooked resource plan";
			return false;
		}

		Vans::VansAssetDatabase database(
			projectRoot / projectConfig.assetsRoot,
			projectRoot / projectConfig.importedArtifactRoot);
		Vans::VansAssetDatabase builtInDatabase(
			engineRoot / "EngineAssets",
			projectRoot / projectConfig.importedArtifactRoot / "Engine");
		const Vans::VansAssetScanResult scanResult = database.Scan(Vans::VansAssetOperationPolicy::Authoring());
		for (const std::string& scanError : scanResult.errors)
			VANS_LOG_ERROR("[PackageResourcePlan] " << scanError);
		if (!scanResult)
		{
			error = "Asset scan failed while building cooked resource plan";
			return false;
		}
		std::vector<std::string> builtInErrors;
		if (!Vans::VansBuiltInAssetCatalog::RegisterAssets(
			builtInDatabase,
			engineRoot,
			Vans::VansAssetOperationPolicy::ReadOnly(),
			builtInErrors))
		{
			for (const std::string& builtInError : builtInErrors)
				VANS_LOG_ERROR("[PackageResourcePlan] " << builtInError);
			error = "Built-in asset scan failed while building cooked resource plan";
			return false;
		}

		Vans::VansSceneAssetDependencyBuildResult dependencyResult =
			Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
				database,
				scenePath,
				projectConfig.runtimeAssetBindings,
				&builtInDatabase);
		if (!dependencyResult.success)
		{
			error = "Cannot build scene dependency plan for package";
			return false;
		}

		if (prewarmResourceCaches)
		{
			cookedPlan.prewarmEnabled = true;
			cookedPlan.prewarmResult = Vans::VansPackageResourcePrewarmer::Prewarm(
				projectRoot,
				database,
				builtInDatabase,
				dependencyResult.resourcePlan);

			if (cookedPlan.prewarmResult.ChangedArtifacts())
			{
				const Vans::VansAssetScanResult rescanResult = database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
				for (const std::string& scanError : rescanResult.errors)
					VANS_LOG_ERROR("[PackageResourcePlan] " << scanError);
				if (!rescanResult)
				{
					error = "Asset scan failed after prewarming resource caches";
					return false;
				}
				builtInErrors.clear();
				if (!Vans::VansBuiltInAssetCatalog::RegisterAssets(
					builtInDatabase,
					engineRoot,
					Vans::VansAssetOperationPolicy::ReadOnly(),
					builtInErrors))
				{
					error = "Built-in asset scan failed after prewarming resource caches";
					return false;
				}

				dependencyResult = Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
					database,
					scenePath,
					projectConfig.runtimeAssetBindings,
					&builtInDatabase);
				if (!dependencyResult.success)
				{
					error = "Cannot rebuild scene dependency plan after prewarming caches";
					return false;
				}
			}
		}

		cookedPlan.enabled = true;
		cookedPlan.packagePlan.resourcePlan = dependencyResult.resourcePlan;
		cookedPlan.packagePlan.runtimeAssetBindings.clear();
		for (const auto& [alias, guid] : projectConfig.runtimeAssetBindings)
			cookedPlan.packagePlan.runtimeAssetBindings.emplace(alias, guid);
		std::vector<Vans::VansAssetRecord> indexedAssets = database.All();
		const std::vector<Vans::VansAssetRecord> builtInAssets = builtInDatabase.All();
		indexedAssets.insert(indexedAssets.end(), builtInAssets.begin(), builtInAssets.end());
		const Vans::VansSceneResourceLoadContext packageBuildContext =
			Vans::VansSceneResourceLoadContext::ForEditor(projectRoot, engineRoot, indexedAssets);

		std::unordered_map<std::string, std::size_t> assetIndexByGuid;
		for (const Vans::VansAssetRecord& record : indexedAssets)
		{
			Vans::VansPackagedAssetIndexRecord indexRecord;
			indexRecord.guid = record.guid.ToString();
			indexRecord.type = AssetTypeToString(record.type);
			// 包运行时不保留源文件读取路径，所有读取必须落到下面的缓存索引。
			indexRecord.sourcePath.clear();
			indexRecord.authoringPath.clear();
			indexRecord.artifactPath = record.artifactPath.string();
			indexRecord.artifactFormat = ArtifactFormatToString(record.artifactFormat);
			indexRecord.sourceHash = record.sourceHash;
			indexRecord.metaHash = record.metaHash;
			indexRecord.missing = record.state == Vans::VansAssetState::Missing;
			assetIndexByGuid[indexRecord.guid] = cookedPlan.packagePlan.assetIndex.size();
			cookedPlan.packagePlan.assetIndex.push_back(std::move(indexRecord));
		}
		auto indexedRecord = [&](const std::string& guid) -> Vans::VansPackagedAssetIndexRecord*
		{
			const auto found = assetIndexByGuid.find(guid);
			return found == assetIndexByGuid.end()
				? nullptr
				: &cookedPlan.packagePlan.assetIndex[found->second];
		};

		std::error_code ec;
		for (const std::string& guidText : dependencyResult.requiredAssets)
		{
			Vans::VansAssetGuid guid;
			std::optional<Vans::VansAssetRecord> sourceRecord;
			if (Vans::VansAssetGuid::TryParse(guidText, guid))
			{
				sourceRecord = database.Find(guid);
				if (!sourceRecord)
					sourceRecord = builtInDatabase.Find(guid);
			}
			if (!sourceRecord || sourceRecord->state == Vans::VansAssetState::Missing)
			{
				cookedPlan.missingResources.push_back({ "asset dependency", guidText, {}, {} });
				continue;
			}
			if (sourceRecord->type != Vans::VansAssetType::AnimatorController
				&& sourceRecord->type != Vans::VansAssetType::AnimationClip
				&& sourceRecord->type != Vans::VansAssetType::BoneMask
				&& sourceRecord->type != Vans::VansAssetType::Timeline)
				continue;
			if (!fs::is_regular_file(sourceRecord->sourcePath, ec))
			{
				cookedPlan.missingResources.push_back({ "animation dependency", guidText,
					sourceRecord->sourcePath.string(), {} });
				ec.clear();
				continue;
			}
			if (sourceRecord->type == Vans::VansAssetType::AnimationClip)
			{
				VansGraphics::VansAnimationClipInfo clipInfo;
				if (!VansGraphics::VansAnimationClipIO::Peek(
					sourceRecord->sourcePath.string(), clipInfo))
				{
					error = "Animation Clip is not self-contained canonical form: '"
						+ NormalizeForLog(sourceRecord->sourcePath)
						+ "'. Run ForestAssetTool rewrite-animation-assets --write before packaging.";
					return false;
				}
			}
			if (auto* record = indexedRecord(guidText))
			{
				const fs::path cachedPath = RegisterSourceFormatCache(
					guidText, sourceRecord->sourcePath, false, cookedPlan).generic_string();
				record->artifactPath = cachedPath.generic_string();
				record->artifactFormat = "source";
				RegisterCookedSourceAsset(projectRoot, sourceRecord->sourcePath.string(),
					cookedPlan.cookedSourceAssets);
			}
			else
			{
				cookedPlan.missingResources.push_back({ "animation dependency index", guidText,
					sourceRecord->sourcePath.string(), {} });
			}
			ec.clear();
		}

		for (auto& mesh : cookedPlan.packagePlan.resourcePlan.meshes)
		{
			const Vans::VansResolvedSceneResourcePath resolved = packageBuildContext.ResolveMesh(mesh);
			const bool hasArtifact = resolved.artifactAvailable;
			mesh.cookedOnly = hasArtifact;
			if (hasArtifact)
			{
				cookedPlan.artifactPaths.push_back(resolved.artifactPath);
				if (auto* record = indexedRecord(mesh.assetGuid))
				{
					record->artifactPath = resolved.artifactPath.string();
					record->artifactFormat = "imported";
				}
				RegisterCookedSourceAsset(projectRoot, mesh.path, cookedPlan.cookedSourceAssets);
			}
			else
			{
				Vans::VansAssetGuid guid;
				std::optional<Vans::VansAssetRecord> sourceRecord;
				if (Vans::VansAssetGuid::TryParse(mesh.assetGuid, guid))
				{
					sourceRecord = database.Find(guid);
					if (!sourceRecord)
						sourceRecord = builtInDatabase.Find(guid);
				}
				if (!sourceRecord || !fs::is_regular_file(sourceRecord->sourcePath, ec))
				{
					cookedPlan.missingResources.push_back({ "mesh", mesh.name, mesh.path, mesh.artifactPath });
				}
				else if (auto* record = indexedRecord(mesh.assetGuid))
				{
					record->artifactPath = RegisterSourceFormatCache(
						mesh.assetGuid, sourceRecord->sourcePath, false, cookedPlan).generic_string();
					record->artifactFormat = "source";
				}
			}
			ec.clear();
		}

		for (auto& texture : cookedPlan.packagePlan.resourcePlan.textures)
		{
			const Vans::VansResolvedSceneResourcePath resolved = packageBuildContext.ResolveTexture(texture);
			const bool hasArtifact = resolved.artifactAvailable;
			const bool needsSourceFallback = TextureNeedsPackagedSourceFallback(texture.path);
			texture.cookedOnly = true;
			if (hasArtifact && !needsSourceFallback)
			{
				cookedPlan.artifactPaths.push_back(resolved.artifactPath);
				if (auto* record = indexedRecord(texture.assetGuid))
				{
					record->artifactPath = resolved.artifactPath.string();
					record->artifactFormat = "imported";
				}
				RegisterCookedSourceAsset(projectRoot, texture.path, cookedPlan.cookedSourceAssets);
			}
			else
			{
				Vans::VansAssetGuid guid;
				const auto sourceRecord = Vans::VansAssetGuid::TryParse(texture.assetGuid, guid)
					? database.Find(guid)
					: std::optional<Vans::VansAssetRecord>{};
				const fs::path sourceFormatPath = texture.textureType == 2
					? (fs::path(texture.path).is_absolute()
						? fs::path(texture.path)
						: projectRoot / texture.path)
					: (sourceRecord ? sourceRecord->sourcePath : fs::path{});
				const bool sourceIsDirectory = fs::is_directory(sourceFormatPath, ec);
				ec.clear();
				if (!sourceRecord || (!sourceIsDirectory && !fs::is_regular_file(sourceFormatPath, ec)))
				{
					cookedPlan.missingResources.push_back({ "texture", texture.name, texture.path, texture.artifactPath });
				}
				else if (auto* record = indexedRecord(texture.assetGuid))
				{
					record->artifactPath = RegisterSourceFormatCache(
						texture.assetGuid, sourceFormatPath, sourceIsDirectory, cookedPlan).generic_string();
					record->artifactFormat = "source";
					RegisterCookedSourceAsset(projectRoot, texture.path, cookedPlan.cookedSourceAssets);
				}
			}
			ec.clear();
		}

		for (const auto& audio : cookedPlan.packagePlan.resourcePlan.audios)
		{
			Vans::VansAssetGuid guid;
			const auto sourceRecord = Vans::VansAssetGuid::TryParse(audio.assetGuid, guid)
				? database.Find(guid)
				: std::optional<Vans::VansAssetRecord>{};
			if (!sourceRecord || !fs::is_regular_file(sourceRecord->sourcePath, ec))
				cookedPlan.missingResources.push_back({ "audio", audio.name, audio.path, {} });
			else if (auto* record = indexedRecord(audio.assetGuid))
			{
				record->artifactPath = RegisterSourceFormatCache(
					audio.assetGuid, sourceRecord->sourcePath, false, cookedPlan).generic_string();
				record->artifactFormat = "source";
			}
			ec.clear();
		}

		for (const auto& video : cookedPlan.packagePlan.resourcePlan.videos)
		{
			Vans::VansAssetGuid guid;
			const auto sourceRecord = Vans::VansAssetGuid::TryParse(video.assetGuid, guid)
				? database.Find(guid)
				: std::optional<Vans::VansAssetRecord>{};
			if (!sourceRecord || !fs::is_regular_file(sourceRecord->sourcePath, ec))
				cookedPlan.missingResources.push_back({ "video", video.name, video.path, {} });
			else if (auto* record = indexedRecord(video.assetGuid))
			{
				record->artifactPath = RegisterSourceFormatCache(
					video.assetGuid, sourceRecord->sourcePath, false, cookedPlan).generic_string();
				record->artifactFormat = "source";
			}
			ec.clear();
		}

		for (const Vans::VansAssetRecord& sourceRecord : database.All())
		{
			if (sourceRecord.type != Vans::VansAssetType::Material &&
				sourceRecord.type != Vans::VansAssetType::Shader &&
				sourceRecord.type != Vans::VansAssetType::AudioReverbPreset &&
				sourceRecord.type != Vans::VansAssetType::AudioBusSnapshot &&
				sourceRecord.type != Vans::VansAssetType::AudioDuckingRules)
				continue;

			const std::string guid = sourceRecord.guid.ToString();
			if (!fs::is_regular_file(sourceRecord.sourcePath, ec))
			{
				cookedPlan.missingResources.push_back({ "authoring", guid, {}, {} });
			}
			else if (auto* record = indexedRecord(guid))
			{
				const fs::path cachedPath = RegisterSourceFormatCache(
					guid, sourceRecord.sourcePath, false, cookedPlan);
				record->authoringPath = cachedPath.generic_string();
				if (sourceRecord.type == Vans::VansAssetType::Material)
				{
					record->artifactPath = cachedPath.generic_string();
					record->artifactFormat = "source";
				}
				if (sourceRecord.type == Vans::VansAssetType::AudioReverbPreset ||
					sourceRecord.type == Vans::VansAssetType::AudioBusSnapshot ||
					sourceRecord.type == Vans::VansAssetType::AudioDuckingRules)
				{
					record->artifactPath = cachedPath.generic_string();
					record->artifactFormat = "source";
				}
			}
			ec.clear();
		}

		for (const auto& shader : cookedPlan.packagePlan.resourcePlan.shaders)
		{
			const fs::path shaderSource = shader.source.empty()
				? fs::path{}
				: (fs::path(shader.source).is_absolute()
					? fs::path(shader.source)
					: projectRoot / shader.source);
			if (shaderSource.empty() || !fs::exists(shaderSource, ec))
				cookedPlan.missingResources.push_back({ "shader", shader.name, shader.source, {} });
			else if (auto* record = indexedRecord(shader.assetGuid))
			{
				const bool sourceIsDirectory = fs::is_directory(shaderSource, ec);
				record->artifactPath = RegisterSourceFormatCache(
					shader.assetGuid, shaderSource, sourceIsDirectory, cookedPlan).generic_string();
				if (record->authoringPath.empty())
					cookedPlan.missingResources.push_back({ "shader authoring", shader.name, {}, {} });
				record->artifactFormat = "source";
			}
			ec.clear();
		}

		WriteMissingCookedResourceLog(cookedPlan.missingResources);
		if (!cookedPlan.missingResources.empty())
		{
			error = "Package resource cache closure is incomplete";
			return false;
		}
		VANS_LOG("[PackageResourcePlan] Built plan: "
			<< cookedPlan.packagePlan.resourcePlan.meshes.size() << " meshes, "
			<< cookedPlan.packagePlan.resourcePlan.textures.size() << " textures, "
			<< cookedPlan.missingResources.size() << " missing cooked artifacts");
		return true;
	}

	bool CopyWindowsBinaries(
		const fs::path& sourceDir,
		const fs::path& binaryRoot,
		std::uint64_t& copiedFiles,
		std::string& error)
	{
		std::error_code ec;
		if (!fs::exists(sourceDir, ec) || !fs::is_directory(sourceDir, ec))
		{
			error = "Binary source directory does not exist: " + NormalizeForLog(sourceDir);
			return false;
		}

		const fs::path launcherPath = sourceDir / "ForestGameLauncher.exe";
		if (!fs::exists(launcherPath, ec))
		{
			error = "ForestGameLauncher.exe not found. Build the ForestGameLauncher target first: "
				+ NormalizeForLog(launcherPath);
			return false;
		}

		if (!CopyFileTo(launcherPath, binaryRoot / "ForestGameLauncher.exe", copiedFiles, error))
			return false;

		const fs::path runtimePath = sourceDir / "ForestRuntime.dll";
		if (!fs::exists(runtimePath, ec))
		{
			error = "ForestRuntime.dll not found. Build the ForestRuntime target first: "
				+ NormalizeForLog(runtimePath);
			return false;
		}

		if (!CopyFileTo(runtimePath, binaryRoot / "ForestRuntime.dll", copiedFiles, error))
		{
			return false;
		}

		const auto isRuntimeDependencyFile = [](const fs::path& sourcePath)
		{
			const fs::path extension = sourcePath.extension();
			std::string filename = sourcePath.filename().string();
			std::transform(filename.begin(), filename.end(), filename.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			const bool isDebugRuntimeArtifact =
				filename.size() > 6 && filename.compare(filename.size() - 6, 6, "_d.dll") == 0;
			if (filename.rfind("py", 0) == 0 ||
				filename.rfind("tcl", 0) == 0 ||
				filename.rfind("tk", 0) == 0 ||
				filename.rfind("libffi", 0) == 0 ||
				filename.rfind("sqlite3", 0) == 0 ||
				filename.rfind("_", 0) == 0 ||
				isDebugRuntimeArtifact)
			{
				return false;
			}

			return extension == ".dll";
		};

		for (const fs::directory_entry& entry : fs::directory_iterator(sourceDir, ec))
		{
			if (ec)
			{
				error = "Cannot enumerate binary source directory: " + NormalizeForLog(sourceDir);
				return false;
			}

			if (!entry.is_regular_file(ec))
				continue;

			const fs::path sourcePath = entry.path();
			if (!isRuntimeDependencyFile(sourcePath))
				continue;
			if (sourcePath.filename() == "ForestRuntime.dll")
				continue;

			if (!CopyFileTo(sourcePath, binaryRoot / sourcePath.filename(), copiedFiles, error))
				return false;
		}

		return true;
	}
}

namespace Vans
{
	const char* ToString(VansGamePackagePlatform platform)
	{
		switch (platform)
		{
		case VansGamePackagePlatform::Windows: return "Windows";
		default: return "Unknown";
		}
	}

	VansGamePackageResult VansGamePackageBuilder::Build(const VansGamePackageRequest& request)
	{
		VansGamePackageResult result;

		if (request.projectRootPath.empty())
		{
			result.message = "No project is loaded";
			return result;
		}
		if (request.scenePath.empty())
		{
			result.message = "No current scene is loaded";
			return result;
		}

		const fs::path projectRoot = ExistingCanonicalOrAbsolute(request.projectRootPath);
		const fs::path scenePath = ExistingCanonicalOrAbsolute(request.scenePath);
		const fs::path binarySourceDirectory = ExistingCanonicalOrAbsolute(
			request.binarySourceDirectory.empty()
				? GetCurrentExecutableDirectory()
				: fs::path(request.binarySourceDirectory));
		fs::path engineRoot = request.engineRootPath.empty()
			? fs::current_path()
			: ExistingCanonicalOrAbsolute(request.engineRootPath);

		std::error_code ec;
		if (!fs::exists(projectRoot, ec) || !fs::is_directory(projectRoot, ec))
		{
			result.message = "Project root does not exist: " + NormalizeForLog(projectRoot);
			return result;
		}
		if (!fs::exists(scenePath, ec) || !fs::is_regular_file(scenePath, ec))
		{
			result.message = "Current scene file does not exist: " + NormalizeForLog(scenePath);
			return result;
		}
		if (!SamePathPrefix(scenePath, projectRoot))
		{
			result.message = "Current scene must be inside the project root";
			return result;
		}

		const fs::path sceneRelativePath = fs::relative(scenePath, projectRoot, ec);
		if (ec)
		{
			result.message = "Cannot calculate current scene relative path";
			return result;
		}

		const std::string projectName = SanitizeName(projectRoot.filename().string());
		const std::string sceneName = SanitizeName(scenePath.stem().string());
		const fs::path platformOutputRoot = projectRoot / "Builds" / ToString(request.platform);
		const std::string packageDirectoryName = projectName + "_" + sceneName;
		const fs::path outputDir = platformOutputRoot / packageDirectoryName;
		// Keep the staging prefix shorter than the final package name so deep asset
		// paths do not cross the Windows path-length boundary only during assembly.
		const fs::path stagingDir = MakeUniqueSiblingPath(platformOutputRoot, ".stage");
		const fs::path binaryRoot = stagingDir / "Binaries" / ToString(request.platform);
		const fs::path contentRoot = stagingDir / "Content";

		if (!SamePathPrefix(outputDir, platformOutputRoot))
		{
			result.message = "Package output path failed safety validation";
			return result;
		}

		CookedPackagePlanBuild cookedPlan;
		if (request.useCookedResourcePlan && request.includeLibrary)
		{
			if (!BuildCookedPackagePlan(
				projectRoot,
				engineRoot,
				scenePath,
				request.prewarmResourceCaches,
				cookedPlan,
				result.message))
				return result;
			result.missingCookedArtifactCount = cookedPlan.missingResources.size();
		}

		if (fs::exists(outputDir, ec) && !request.overwriteExisting)
		{
			result.message = "Package output already exists: " + NormalizeForLog(outputDir);
			return result;
		}

		fs::create_directories(stagingDir, ec);
		if (ec)
		{
			result.message = "Cannot create package staging directory: " + NormalizeForLog(stagingDir);
			return result;
		}
		result.outputPath = NormalizeForLog(stagingDir);

		std::string error;
		std::uint64_t copiedFiles = 0;

		if (request.includeBinaries &&
			!CopyWindowsBinaries(binarySourceDirectory, binaryRoot, copiedFiles, error))
		{
			result.message = error;
			return result;
		}

		const auto shouldCopyProjectAsset = [&](const fs::path& relativePath)
		{
			if (!cookedPlan.enabled)
				return true;
			return cookedPlan.cookedSourceAssets.find(NormalizeLookupPath(fs::path("Assets") / relativePath)) ==
				cookedPlan.cookedSourceAssets.end();
		};

		if (!CopyFileTo(projectRoot / "ForestProject.json", contentRoot / "ForestProject.json", copiedFiles, error) ||
			!CopyDirectoryToFiltered(projectRoot / "Assets", contentRoot / "Assets", shouldCopyProjectAsset, copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "Scenes", contentRoot / "Scenes", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "ProjectSettings", contentRoot / "ProjectSettings", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "Scripts", contentRoot / "Scripts", copiedFiles, error))
		{
			result.message = error;
			return result;
		}

		if (request.includeLibrary && cookedPlan.enabled)
		{
			std::unordered_set<std::string> copiedArtifactSet;
			for (const fs::path& artifactPath : cookedPlan.artifactPaths)
			{
				if (!CopyReferencedArtifact(projectRoot, contentRoot, artifactPath, copiedArtifactSet, copiedFiles, error))
				{
					result.message = error;
					return result;
				}
			}
			for (const CookedPackagePlanBuild::CacheCopy& cacheCopy : cookedPlan.cacheCopies)
			{
				const fs::path destination = contentRoot / cacheCopy.packageRelativePath;
				const bool copied = cacheCopy.directory
					? CopyDirectoryTo(cacheCopy.sourcePath, destination, copiedFiles, error)
					: CopyFileTo(cacheCopy.sourcePath, destination, copiedFiles, error);
				if (!copied)
				{
					result.message = error;
					return result;
				}
			}

			if (!VansPackagedResourcePlanIO::Save(
				contentRoot / cookedPlan.planRelativePath,
				cookedPlan.packagePlan,
				projectRoot,
				error))
			{
				result.message = error;
				return result;
			}
			++copiedFiles;

			if (!WriteCookedResourcePlanReport(
				contentRoot / cookedPlan.reportRelativePath,
				cookedPlan,
				error))
			{
				result.message = error;
				return result;
			}
			++copiedFiles;
		}
		else if (request.includeLibrary &&
			!CopyDirectoryTo(projectRoot / "Library", contentRoot / "Library", copiedFiles, error))
		{
			result.message = error;
			return result;
		}

		const fs::path packagedShaderArtifacts = contentRoot / "Library" / "Artifacts" / "Shaders";
		if (!CookRegisteredEngineShaders(
			engineRoot,
			projectRoot / "Library" / "Artifacts" / "Shaders",
			cookedPlan.packagePlan.resourcePlan,
			packagedShaderArtifacts,
			copiedFiles,
			error))
		{
			result.message = error;
			return result;
		}

		if (request.includeEngineAssets)
		{
			if (!fs::exists(engineRoot / "EngineAssets", ec))
			{
				result.message = "EngineAssets not found under engine root: " + NormalizeForLog(engineRoot);
				return result;
			}
			const auto shouldCopyEngineAsset = [](const fs::path& relativePath)
			{
				const auto first = relativePath.begin();
				if (first == relativePath.end())
					return false;
				std::string rootName = first->string();
				std::transform(rootName.begin(), rootName.end(), rootName.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return rootName != "shaders" && rootName != "models";
			};
			if (!CopyDirectoryToFiltered(
				engineRoot / "EngineAssets",
				contentRoot / "EngineAssets",
				shouldCopyEngineAsset,
				copiedFiles,
				error))
			{
				result.message = error;
				return result;
			}
		}

		VansPackageManifest manifest;
		manifest.platform = ToString(request.platform);
		manifest.generatedAt = UtcTimestamp();
		manifest.binaryRoot = (fs::path("Binaries") / ToString(request.platform)).generic_string();
		manifest.scene = sceneRelativePath.generic_string();
		manifest.resourcePlan = cookedPlan.enabled ? cookedPlan.planRelativePath.generic_string() : std::string{};
		manifest.resourcePlanReport = cookedPlan.enabled ? cookedPlan.reportRelativePath.generic_string() : std::string{};
		manifest.shaderArtifacts = fs::relative(packagedShaderArtifacts, contentRoot).generic_string();
		manifest.copiedFileCount = copiedFiles + 1;
		if (!VansPackageManifestIO::Save(contentRoot / "ForestPackage.json", manifest, error))
		{
			result.message = error;
			return result;
		}
		++copiedFiles;

		if (!PublishPackageDirectory(
			stagingDir,
			outputDir,
			error))
		{
			result.message = error;
			return result;
		}

		result.success = true;
		result.outputPath = NormalizeForLog(outputDir);
		result.copiedFileCount = copiedFiles;
		result.missingCookedArtifactCount = cookedPlan.missingResources.size();
		result.message = "Package built successfully";
		VANS_LOG("[Package] Windows package built: " << result.outputPath
			<< " (" << copiedFiles << " files, missing cooked artifacts="
			<< result.missingCookedArtifactCount << ")");
		return result;
	}
}
