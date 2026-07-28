#pragma once

#ifdef _WIN32
#ifdef FOREST_RUNTIME_EXPORTS
#define FOREST_RUNTIME_API extern "C" __declspec(dllexport)
#else
#define FOREST_RUNTIME_API extern "C" __declspec(dllimport)
#endif
#else
#define FOREST_RUNTIME_API extern "C" __attribute__((visibility("default")))
#endif

struct ForestRuntimeHandle;

FOREST_RUNTIME_API int ForestRuntime_GetAbiVersion();
FOREST_RUNTIME_API ForestRuntimeHandle* ForestRuntime_Create();
FOREST_RUNTIME_API void ForestRuntime_Destroy(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API int ForestRuntime_LoadPackage(ForestRuntimeHandle* runtime, const char* manifestPath);
FOREST_RUNTIME_API int ForestRuntime_CreateWindow(ForestRuntimeHandle* runtime, int width, int height, const char* title);
FOREST_RUNTIME_API int ForestRuntime_LoadCurrentScene(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API int ForestRuntime_Tick(ForestRuntimeHandle* runtime, float deltaTimeSeconds);
FOREST_RUNTIME_API int ForestRuntime_RenderFrame(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API int ForestRuntime_ShouldClose(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API void ForestRuntime_Shutdown(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API const char* ForestRuntime_GetLastError(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API const char* ForestRuntime_GetLoadedScene(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API int ForestRuntime_IsProjectLoaded(ForestRuntimeHandle* runtime);
FOREST_RUNTIME_API const char* ForestRuntime_GetProjectRoot(ForestRuntimeHandle* runtime);
