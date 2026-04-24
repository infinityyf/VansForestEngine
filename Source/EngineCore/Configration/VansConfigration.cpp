#include "VansConfigration.h"
#include <Windows.h>
#include <filesystem>

VansConfigration*  VansConfigration::instance = nullptr;

VansConfigration::VansConfigration()
{
	ShadowMapHeight = 2048;
	ShadowMapWidth = 2048;
	PunctualShadowMapWidth = 4096;
	PunctualShadowMapHeight = 4096;
	SupportRayTracing = true;

	// Cascade Shadow Map defaults
	CascadeShadowMapSize = 512;
	CascadeCount = 4;
	CascadeSplits[0] = 5.0f;
	CascadeSplits[1] = 20.0f;
	CascadeSplits[2] = 80.0f;
	CascadeSplits[3] = 320.0f;

	// Get executable path and compute project root
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::filesystem::path executablePath(exePath);
	// Go up from executable to project root (typically executable is in x64/Debug or x64/Release)
	ProjectRootPath = executablePath.parent_path().parent_path().parent_path().string();
	// Normalize path separators to forward slashes for consistency
	for (auto& c : ProjectRootPath) {
		if (c == '\\') c = '/';
	}
	if (!ProjectRootPath.empty() && ProjectRootPath.back() != '/') {
		ProjectRootPath += "/ForestEngine/";
	}
}

VansConfigration* VansConfigration::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansConfigration();
	}
	return instance;
}
