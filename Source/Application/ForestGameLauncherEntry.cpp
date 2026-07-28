#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <cstdlib>

#include "../RuntimeExport/ForestRuntimeCAPI.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
	fs::path GetExecutablePath(char* argv0)
	{
#ifdef _WIN32
		char buffer[MAX_PATH]{};
		const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			return fs::path(buffer);
#endif
		return fs::absolute(argv0);
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

#ifdef _WIN32
	struct LauncherOptions
	{
		bool noWindow = false;
		int width = 1280;
		int height = 720;
		int maxFrames = 0;
	};

	bool ParseIntegerOption(const char* value, int& output)
	{
		if (!value)
			return false;
		char* end = nullptr;
		const long parsed = std::strtol(value, &end, 10);
		if (!end || *end != '\0')
			return false;
		output = static_cast<int>(parsed);
		return true;
	}

	LauncherOptions ParseLauncherOptions(int argc, char** argv)
	{
		LauncherOptions options;
		for (int i = 1; i < argc; ++i)
		{
			const std::string arg = argv[i];
			if (arg == "--no-window")
			{
				options.noWindow = true;
			}
			else if (arg == "--width" && i + 1 < argc)
			{
				ParseIntegerOption(argv[++i], options.width);
			}
			else if (arg == "--height" && i + 1 < argc)
			{
				ParseIntegerOption(argv[++i], options.height);
			}
			else if (arg == "--max-frames" && i + 1 < argc)
			{
				ParseIntegerOption(argv[++i], options.maxFrames);
			}
		}
		if (options.width <= 0) options.width = 1280;
		if (options.height <= 0) options.height = 720;
		if (options.maxFrames < 0) options.maxFrames = 0;
		return options;
	}

	template <typename T>
	bool LoadRuntimeFunction(HMODULE module, const char* name, T& outFunction)
	{
		outFunction = reinterpret_cast<T>(GetProcAddress(module, name));
		if (!outFunction)
		{
			std::cerr << "[ForestGameLauncher] Runtime function not found: " << name << std::endl;
			return false;
		}
		return true;
	}

	struct ForestRuntimeApi
	{
		using GetAbiVersionFn = int (*)();
		using CreateFn = ForestRuntimeHandle* (*)();
		using DestroyFn = void (*)(ForestRuntimeHandle*);
		using LoadPackageFn = int (*)(ForestRuntimeHandle*, const char*);
		using CreateWindowFn = int (*)(ForestRuntimeHandle*, int, int, const char*);
		using LoadCurrentSceneFn = int (*)(ForestRuntimeHandle*);
		using TickFn = int (*)(ForestRuntimeHandle*, float);
		using RenderFrameFn = int (*)(ForestRuntimeHandle*);
		using ShouldCloseFn = int (*)(ForestRuntimeHandle*);
		using ShutdownFn = void (*)(ForestRuntimeHandle*);
		using GetLastErrorFn = const char* (*)(ForestRuntimeHandle*);
		using GetLoadedSceneFn = const char* (*)(ForestRuntimeHandle*);
		using IsProjectLoadedFn = int (*)(ForestRuntimeHandle*);
		using GetProjectRootFn = const char* (*)(ForestRuntimeHandle*);

		HMODULE module = nullptr;
		GetAbiVersionFn GetAbiVersion = nullptr;
		CreateFn Create = nullptr;
		DestroyFn Destroy = nullptr;
		LoadPackageFn LoadPackage = nullptr;
		CreateWindowFn CreateRuntimeWindow = nullptr;
		LoadCurrentSceneFn LoadCurrentScene = nullptr;
		TickFn Tick = nullptr;
		RenderFrameFn RenderFrame = nullptr;
		ShouldCloseFn ShouldClose = nullptr;
		ShutdownFn Shutdown = nullptr;
		GetLastErrorFn GetLastError = nullptr;
		GetLoadedSceneFn GetLoadedScene = nullptr;
		IsProjectLoadedFn IsProjectLoaded = nullptr;
		GetProjectRootFn GetProjectRoot = nullptr;

		~ForestRuntimeApi()
		{
			if (module)
				FreeLibrary(module);
		}

		bool Load(const fs::path& runtimeDllPath)
		{
			SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
			AddDllDirectory(runtimeDllPath.parent_path().wstring().c_str());
			module = LoadLibraryA(runtimeDllPath.string().c_str());
			if (!module)
			{
				std::cerr << "[ForestGameLauncher] Cannot load runtime DLL: "
					<< runtimeDllPath.string() << " (GetLastError=" << ::GetLastError() << ")" << std::endl;
				return false;
			}

			return
				LoadRuntimeFunction(module, "ForestRuntime_GetAbiVersion", GetAbiVersion) &&
				LoadRuntimeFunction(module, "ForestRuntime_Create", Create) &&
				LoadRuntimeFunction(module, "ForestRuntime_Destroy", Destroy) &&
				LoadRuntimeFunction(module, "ForestRuntime_LoadPackage", LoadPackage) &&
				LoadRuntimeFunction(module, "ForestRuntime_CreateWindow", CreateRuntimeWindow) &&
				LoadRuntimeFunction(module, "ForestRuntime_LoadCurrentScene", LoadCurrentScene) &&
				LoadRuntimeFunction(module, "ForestRuntime_Tick", Tick) &&
				LoadRuntimeFunction(module, "ForestRuntime_RenderFrame", RenderFrame) &&
				LoadRuntimeFunction(module, "ForestRuntime_ShouldClose", ShouldClose) &&
				LoadRuntimeFunction(module, "ForestRuntime_Shutdown", Shutdown) &&
				LoadRuntimeFunction(module, "ForestRuntime_GetLastError", GetLastError) &&
				LoadRuntimeFunction(module, "ForestRuntime_GetLoadedScene", GetLoadedScene) &&
				LoadRuntimeFunction(module, "ForestRuntime_IsProjectLoaded", IsProjectLoaded) &&
				LoadRuntimeFunction(module, "ForestRuntime_GetProjectRoot", GetProjectRoot);
		}
	};
#endif
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	const LauncherOptions options = ParseLauncherOptions(argc, argv);
#else
	const bool noWindow = false;
#endif

	const fs::path executablePath = GetExecutablePath(argc > 0 ? argv[0] : "");
	const fs::path packageRoot = executablePath.parent_path().parent_path().parent_path();
	const fs::path binaryRoot = executablePath.parent_path();
	const fs::path contentRoot = packageRoot / "Content";
	const fs::path manifestPath = contentRoot / "ForestPackage.json";
	const fs::path runtimeDllPath = binaryRoot / "ForestRuntime.dll";

	const std::string manifestText = ReadTextFile(manifestPath);
	if (manifestText.empty())
	{
		std::cerr << "[ForestGameLauncher] Package manifest not found: "
			<< manifestPath.string() << std::endl;
		return 2;
	}

	const std::string platform = ReadJsonStringField(manifestText, "platform");
	const std::string scene = ReadJsonStringField(manifestText, "scene");
	const std::string startupMode = ReadJsonStringField(manifestText, "startupMode");

	std::cout << "ForestGameLauncher" << std::endl;
	std::cout << "Package root: " << packageRoot.string() << std::endl;
	std::cout << "Content root: " << contentRoot.string() << std::endl;
	std::cout << "Platform: " << platform << std::endl;
	std::cout << "Startup mode: " << (startupMode.empty() ? "play" : startupMode) << std::endl;
	std::cout << "Scene: " << scene << std::endl;

#ifdef _WIN32
	ForestRuntimeApi runtimeApi;
	if (!runtimeApi.Load(runtimeDllPath))
		return 3;

	const int abiVersion = runtimeApi.GetAbiVersion();
	if (abiVersion != 1)
	{
		std::cerr << "[ForestGameLauncher] Unsupported ForestRuntime ABI version: "
			<< abiVersion << std::endl;
		return 4;
	}

	ForestRuntimeHandle* runtime = runtimeApi.Create();
	if (!runtime)
	{
		std::cerr << "[ForestGameLauncher] Cannot create runtime handle" << std::endl;
		return 5;
	}

	int exitCode = 0;
	if (!runtimeApi.LoadPackage(runtime, manifestPath.string().c_str()))
	{
		std::cerr << "[ForestGameLauncher] Runtime package load failed: "
			<< runtimeApi.GetLastError(runtime) << std::endl;
		exitCode = 6;
	}
	else if (!runtimeApi.Tick(runtime, 0.0f))
	{
		std::cerr << "[ForestGameLauncher] Runtime tick failed: "
			<< runtimeApi.GetLastError(runtime) << std::endl;
		exitCode = 7;
	}
	else
	{
		std::cout << "Runtime ABI: " << abiVersion << std::endl;
		std::cout << "Runtime loaded scene: " << runtimeApi.GetLoadedScene(runtime) << std::endl;
		std::cout << "Runtime project loaded: " << (runtimeApi.IsProjectLoaded(runtime) ? "true" : "false") << std::endl;
		std::cout << "Runtime project root: " << runtimeApi.GetProjectRoot(runtime) << std::endl;
		if (options.noWindow)
		{
			std::cout << "Runtime handoff reached packaged-project stage." << std::endl;
		}
		else if (!runtimeApi.CreateRuntimeWindow(runtime, options.width, options.height, "ForestGame"))
		{
			std::cerr << "[ForestGameLauncher] Runtime window creation failed: "
				<< runtimeApi.GetLastError(runtime) << std::endl;
			exitCode = 8;
		}
		else if (!runtimeApi.LoadCurrentScene(runtime))
		{
			std::cerr << "[ForestGameLauncher] Runtime scene load failed: "
				<< runtimeApi.GetLastError(runtime) << std::endl;
			exitCode = 9;
		}
		else
		{
			std::cout << "Runtime handoff reached render-loop stage." << std::endl;
			int renderedFrames = 0;
			auto lastFrameTime = std::chrono::steady_clock::now();
			while (!runtimeApi.ShouldClose(runtime))
			{
				const auto now = std::chrono::steady_clock::now();
				const std::chrono::duration<float> delta = now - lastFrameTime;
				lastFrameTime = now;
				if (!runtimeApi.Tick(runtime, delta.count()) || !runtimeApi.RenderFrame(runtime))
				{
					std::cerr << "[ForestGameLauncher] Runtime frame failed: "
						<< runtimeApi.GetLastError(runtime) << std::endl;
					exitCode = 10;
					break;
				}
				++renderedFrames;
				if (options.maxFrames > 0 && renderedFrames >= options.maxFrames)
					break;
			}
		}
	}

	runtimeApi.Shutdown(runtime);
	runtimeApi.Destroy(runtime);
	return exitCode;
#else
	std::cerr << "[ForestGameLauncher] Runtime DLL loading is implemented for Windows only." << std::endl;
	return 8;
#endif
}
