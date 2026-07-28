#include "../EngineCore/EditorCore/Packaging/VansGamePackageBuilder.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
#ifndef FOREST_ENGINE_SOURCE_ROOT
#define FOREST_ENGINE_SOURCE_ROOT ""
#endif

	struct PackageToolOptions
	{
		std::string projectRootPath;
		std::string scenePath;
		std::string engineRootPath = FOREST_ENGINE_SOURCE_ROOT;
		std::string binarySourceDirectory;
		Vans::VansGamePackagePlatform platform = Vans::VansGamePackagePlatform::Windows;
		bool includeEngineAssets = true;
		bool includeLibrary = true;
		bool includeBinaries = true;
		bool overwriteExisting = true;
		bool showHelp = false;
	};

	fs::path GetExecutableDirectory(char* argv0)
	{
#ifdef _WIN32
		char buffer[MAX_PATH]{};
		const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			return fs::path(buffer).parent_path();
#endif
		return fs::absolute(argv0).parent_path();
	}

	std::string ReadTextFile(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return {};

		std::ostringstream stream;
		stream << file.rdbuf();
		return stream.str();
	}

	std::string ReadJsonStringField(const std::string& json, const std::string& fieldName)
	{
		const std::string key = "\"" + fieldName + "\"";
		const std::size_t keyPos = json.find(key);
		if (keyPos == std::string::npos)
			return {};

		const std::size_t colonPos = json.find(':', keyPos + key.size());
		if (colonPos == std::string::npos)
			return {};

		const std::size_t quoteStart = json.find('"', colonPos + 1);
		if (quoteStart == std::string::npos)
			return {};

		std::string value;
		bool escaping = false;
		for (std::size_t i = quoteStart + 1; i < json.size(); ++i)
		{
			const char c = json[i];
			if (escaping)
			{
				switch (c)
				{
				case 'n': value.push_back('\n'); break;
				case 'r': value.push_back('\r'); break;
				case 't': value.push_back('\t'); break;
				default: value.push_back(c); break;
				}
				escaping = false;
				continue;
			}
			if (c == '\\')
			{
				escaping = true;
				continue;
			}
			if (c == '"')
				break;
			value.push_back(c);
		}
		return value;
	}

	void PrintUsage()
	{
		std::cout
			<< "ForestPackageTool\n"
			<< "Usage:\n"
			<< "  ForestPackageTool --project <projectRoot> [--scene <scenePath>] [options]\n\n"
			<< "Options:\n"
			<< "  --platform Windows       Target platform. Android is reserved but not implemented.\n"
			<< "  --engine-root <path>     Engine source root containing EngineAssets.\n"
			<< "  --binary-dir <path>      Directory containing ForestGameLauncher.exe and ForestRuntime.dll.\n"
			<< "  --no-engine-assets       Do not copy EngineAssets.\n"
			<< "  --no-library             Do not copy project Library.\n"
			<< "  --no-binaries            Do not copy launcher/runtime binaries.\n"
			<< "  --no-overwrite           Fail if the package output already exists.\n"
			<< "  --help                   Show this help.\n";
	}

	bool ParsePlatform(const std::string& value, Vans::VansGamePackagePlatform& platform)
	{
		if (value == "Windows" || value == "windows" || value == "Win64" || value == "win64")
		{
			platform = Vans::VansGamePackagePlatform::Windows;
			return true;
		}
		if (value == "Android" || value == "android")
		{
			platform = Vans::VansGamePackagePlatform::Android;
			return true;
		}
		return false;
	}

	bool ParseOptions(int argc, char** argv, PackageToolOptions& options, std::string& error)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::string arg = argv[i];
			const auto requireValue = [&](const char* optionName) -> const char*
			{
				if (i + 1 >= argc)
				{
					error = std::string("Missing value for ") + optionName;
					return nullptr;
				}
				return argv[++i];
			};

			if (arg == "--help" || arg == "-h")
			{
				options.showHelp = true;
			}
			else if (arg == "--project")
			{
				if (const char* value = requireValue("--project"))
					options.projectRootPath = value;
			}
			else if (arg == "--scene")
			{
				if (const char* value = requireValue("--scene"))
					options.scenePath = value;
			}
			else if (arg == "--engine-root")
			{
				if (const char* value = requireValue("--engine-root"))
					options.engineRootPath = value;
			}
			else if (arg == "--binary-dir")
			{
				if (const char* value = requireValue("--binary-dir"))
					options.binarySourceDirectory = value;
			}
			else if (arg == "--platform")
			{
				if (const char* value = requireValue("--platform"))
				{
					if (!ParsePlatform(value, options.platform))
					{
						error = std::string("Unknown platform: ") + value;
						return false;
					}
				}
			}
			else if (arg == "--no-engine-assets")
			{
				options.includeEngineAssets = false;
			}
			else if (arg == "--no-library")
			{
				options.includeLibrary = false;
			}
			else if (arg == "--no-binaries")
			{
				options.includeBinaries = false;
			}
			else if (arg == "--no-overwrite")
			{
				options.overwriteExisting = false;
			}
			else
			{
				error = "Unknown option: " + arg;
				return false;
			}

			if (!error.empty())
				return false;
		}
		return true;
	}

	fs::path ResolveProjectRelativePath(const fs::path& projectRoot, const std::string& value)
	{
		const fs::path path(value);
		if (path.is_absolute())
			return path;
		return projectRoot / path;
	}

	bool ResolveDefaultScene(PackageToolOptions& options, std::string& error)
	{
		if (!options.scenePath.empty())
			return true;

		const fs::path projectRoot = fs::absolute(options.projectRootPath).lexically_normal();
		const fs::path projectConfigPath = projectRoot / "ForestProject.json";
		const std::string configText = ReadTextFile(projectConfigPath);
		if (configText.empty())
		{
			error = "Cannot read project config: " + projectConfigPath.string();
			return false;
		}

		options.scenePath = ReadJsonStringField(configText, "defaultScene");
		if (options.scenePath.empty())
		{
			error = "ForestProject.json does not specify defaultScene";
			return false;
		}
		return true;
	}
}

int main(int argc, char** argv)
{
	PackageToolOptions options;
	options.binarySourceDirectory = GetExecutableDirectory(argc > 0 ? argv[0] : "").string();

	std::string error;
	if (!ParseOptions(argc, argv, options, error))
	{
		std::cerr << "[ForestPackageTool] " << error << std::endl;
		PrintUsage();
		return 2;
	}

	if (options.showHelp)
	{
		PrintUsage();
		return 0;
	}

	if (options.projectRootPath.empty())
	{
		std::cerr << "[ForestPackageTool] --project is required" << std::endl;
		PrintUsage();
		return 2;
	}

	if (!ResolveDefaultScene(options, error))
	{
		std::cerr << "[ForestPackageTool] " << error << std::endl;
		return 3;
	}

	const fs::path projectRoot = fs::absolute(options.projectRootPath).lexically_normal();
	const fs::path scenePath = ResolveProjectRelativePath(projectRoot, options.scenePath).lexically_normal();

	Vans::VansGamePackageRequest request;
	request.platform = options.platform;
	request.projectRootPath = projectRoot.string();
	request.engineRootPath = options.engineRootPath;
	request.scenePath = scenePath.string();
	request.binarySourceDirectory = options.binarySourceDirectory;
	request.includeEngineAssets = options.includeEngineAssets;
	request.includeLibrary = options.includeLibrary;
	request.includeBinaries = options.includeBinaries;
	request.overwriteExisting = options.overwriteExisting;

	const Vans::VansGamePackageResult result = Vans::VansGamePackageBuilder::Build(request);
	if (!result)
	{
		std::cerr << "[ForestPackageTool] Package failed: " << result.message << std::endl;
		return 4;
	}

	std::cout << "Package built successfully" << std::endl;
	std::cout << "Output: " << result.outputPath << std::endl;
	std::cout << "Copied files: " << result.copiedFileCount << std::endl;
	return 0;
}
