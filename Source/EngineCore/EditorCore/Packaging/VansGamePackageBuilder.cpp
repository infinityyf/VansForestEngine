#include "VansGamePackageBuilder.h"

#include "../../Util/VansLog.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

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

	std::string JsonEscape(const std::string& value)
	{
		std::ostringstream out;
		for (const char c : value)
		{
			switch (c)
			{
			case '\\': out << "\\\\"; break;
			case '"': out << "\\\""; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default: out << c; break;
			}
		}
		return out.str();
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

	bool WritePackageManifest(
		const fs::path& manifestPath,
		const Vans::VansGamePackageRequest& request,
		const fs::path& projectRoot,
		const fs::path& engineRoot,
		const fs::path& contentRoot,
		const fs::path& binaryRoot,
		const fs::path& sceneRelativePath,
		std::uint64_t copiedFileCount,
		std::string& error)
	{
		std::error_code ec;
		fs::create_directories(manifestPath.parent_path(), ec);
		if (ec)
		{
			error = "Cannot create manifest directory: " + NormalizeForLog(manifestPath.parent_path());
			return false;
		}

		std::ofstream file(manifestPath, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			error = "Cannot write manifest: " + NormalizeForLog(manifestPath);
			return false;
		}

		file
			<< "{\n"
			<< "  \"format\": \"ForestGamePackage\",\n"
			<< "  \"version\": 1,\n"
			<< "  \"platform\": \"" << JsonEscape(Vans::ToString(request.platform)) << "\",\n"
			<< "  \"startupMode\": \"play\",\n"
			<< "  \"generatedAt\": \"" << JsonEscape(UtcTimestamp()) << "\",\n"
			<< "  \"projectRoot\": \"" << JsonEscape(projectRoot.generic_string()) << "\",\n"
			<< "  \"engineRoot\": \"" << JsonEscape(engineRoot.generic_string()) << "\",\n"
			<< "  \"contentRoot\": \"" << JsonEscape(contentRoot.filename().generic_string()) << "\",\n"
			<< "  \"binaryRoot\": \"" << JsonEscape(binaryRoot.parent_path().filename().generic_string() + "/" + binaryRoot.filename().generic_string()) << "\",\n"
			<< "  \"scene\": \"" << JsonEscape(sceneRelativePath.generic_string()) << "\",\n"
			<< "  \"copiedFileCount\": " << copiedFileCount << "\n"
			<< "}\n";
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
		case VansGamePackagePlatform::Android: return "Android";
		default: return "Unknown";
		}
	}

	VansGamePackageResult VansGamePackageBuilder::Build(const VansGamePackageRequest& request)
	{
		VansGamePackageResult result;

		if (request.platform != VansGamePackagePlatform::Windows)
		{
			result.message = "Only Windows packaging is implemented right now";
			return result;
		}
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
		const fs::path outputDir = platformOutputRoot / (projectName + "_" + sceneName);
		const fs::path binaryRoot = outputDir / "Binaries" / ToString(request.platform);
		const fs::path contentRoot = outputDir / "Content";

		if (!SamePathPrefix(outputDir, platformOutputRoot))
		{
			result.message = "Package output path failed safety validation";
			return result;
		}

		if (fs::exists(outputDir, ec))
		{
			if (!request.overwriteExisting)
			{
				result.message = "Package output already exists: " + NormalizeForLog(outputDir);
				return result;
			}

			fs::remove_all(outputDir, ec);
			if (ec)
			{
				result.message = "Cannot clear package output: " + NormalizeForLog(outputDir);
				return result;
			}
		}

		fs::create_directories(outputDir, ec);
		if (ec)
		{
			result.message = "Cannot create package output: " + NormalizeForLog(outputDir);
			return result;
		}

		std::string error;
		std::uint64_t copiedFiles = 0;

		if (request.includeBinaries &&
			!CopyWindowsBinaries(binarySourceDirectory, binaryRoot, copiedFiles, error))
		{
			result.message = error;
			return result;
		}

		if (!CopyFileTo(projectRoot / "ForestProject.json", contentRoot / "ForestProject.json", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "Assets", contentRoot / "Assets", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "Scenes", contentRoot / "Scenes", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "ProjectSettings", contentRoot / "ProjectSettings", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "MotionMatchDataBase", contentRoot / "MotionMatchDataBase", copiedFiles, error) ||
			!CopyDirectoryTo(projectRoot / "Scripts", contentRoot / "Scripts", copiedFiles, error))
		{
			result.message = error;
			return result;
		}

		if (request.includeLibrary &&
			!CopyDirectoryTo(projectRoot / "Library", contentRoot / "Library", copiedFiles, error))
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
			if (!CopyDirectoryTo(engineRoot / "EngineAssets", contentRoot / "EngineAssets", copiedFiles, error))
			{
				result.message = error;
				return result;
			}
		}

		if (!WritePackageManifest(
			contentRoot / "ForestPackage.json",
			request,
			projectRoot,
			engineRoot,
			contentRoot,
			binaryRoot,
			sceneRelativePath,
			copiedFiles,
			error))
		{
			result.message = error;
			return result;
		}

		result.success = true;
		result.outputPath = NormalizeForLog(outputDir);
		result.copiedFileCount = copiedFiles;
		result.message = "Package built successfully";
		VANS_LOG("[Package] Windows package built: " << result.outputPath
			<< " (" << copiedFiles << " files)");
		return result;
	}
}
