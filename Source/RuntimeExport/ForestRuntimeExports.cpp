#include "ForestRuntimeCAPI.h"
#include "VansRuntimeHost.h"

struct ForestRuntimeHandle
{
	Vans::VansRuntimeHost m_Host;
};

FOREST_RUNTIME_API int ForestRuntime_GetAbiVersion()
{
	return 1;
}

FOREST_RUNTIME_API ForestRuntimeHandle* ForestRuntime_Create()
{
	return new ForestRuntimeHandle();
}

FOREST_RUNTIME_API void ForestRuntime_Destroy(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return;
	runtime->m_Host.Shutdown();
	delete runtime;
}

FOREST_RUNTIME_API int ForestRuntime_LoadPackage(ForestRuntimeHandle* runtime, const char* manifestPath)
{
	return runtime && runtime->m_Host.LoadPackage(manifestPath) ? 1 : 0;
}

FOREST_RUNTIME_API int ForestRuntime_CreateWindow(ForestRuntimeHandle* runtime, int width, int height,
												  const char* title)
{
	return runtime && runtime->m_Host.OpenWindow(width, height, title) ? 1 : 0;
}

FOREST_RUNTIME_API int ForestRuntime_LoadCurrentScene(ForestRuntimeHandle* runtime)
{
	return runtime && runtime->m_Host.LoadCurrentScene() ? 1 : 0;
}

FOREST_RUNTIME_API int ForestRuntime_Tick(ForestRuntimeHandle* runtime, float deltaTimeSeconds)
{
	return runtime && runtime->m_Host.Tick(deltaTimeSeconds) ? 1 : 0;
}

FOREST_RUNTIME_API int ForestRuntime_RenderFrame(ForestRuntimeHandle* runtime)
{
	return runtime && runtime->m_Host.RenderFrame() ? 1 : 0;
}

FOREST_RUNTIME_API int ForestRuntime_ShouldClose(ForestRuntimeHandle* runtime)
{
	return !runtime || runtime->m_Host.ShouldClose() ? 1 : 0;
}

FOREST_RUNTIME_API void ForestRuntime_Shutdown(ForestRuntimeHandle* runtime)
{
	if (runtime)
		runtime->m_Host.Shutdown();
}

FOREST_RUNTIME_API const char* ForestRuntime_GetLastError(ForestRuntimeHandle* runtime)
{
	return runtime ? runtime->m_Host.GetLastError().c_str() : "Runtime handle is null";
}

FOREST_RUNTIME_API const char* ForestRuntime_GetLoadedScene(ForestRuntimeHandle* runtime)
{
	return runtime ? runtime->m_Host.GetLoadedScene().c_str() : "";
}

FOREST_RUNTIME_API int ForestRuntime_IsProjectLoaded(ForestRuntimeHandle* runtime)
{
	return runtime && runtime->m_Host.IsProjectLoaded() ? 1 : 0;
}

FOREST_RUNTIME_API const char* ForestRuntime_GetProjectRoot(ForestRuntimeHandle* runtime)
{
	return runtime ? runtime->m_Host.GetProjectRoot().c_str() : "";
}
