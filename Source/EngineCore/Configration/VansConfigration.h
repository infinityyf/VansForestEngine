#pragma once
#include <string>

class VansConfigration
{
private:
	static VansConfigration* instance;

	VansConfigration();

private:

	int ShadowMapWidth;

	int ShadowMapHeight;

	int PunctualShadowMapWidth;

	int PunctualShadowMapHeight;

	bool SupportRayTracing;

	// Cascade Shadow Map
	int CascadeShadowMapSize;
	int CascadeCount;
	float CascadeSplits[4];

	std::string ProjectRootPath;

public:

	static VansConfigration* GetInstance();

	int GetShadowMapWidth() { return ShadowMapWidth; }

	int GetShadowMapHeight() { return ShadowMapHeight; }

	int GetPunctualShadowMapWidth() { return PunctualShadowMapWidth; }

	int GetPunctualShadowMapHeight() { return PunctualShadowMapHeight; }

	bool GetSupportRayTracing() { return SupportRayTracing; }

	const std::string& GetProjectRootPath() { return ProjectRootPath; }

	int GetCascadeShadowMapSize() { return CascadeShadowMapSize; }

	int GetCascadeCount() { return CascadeCount; }

	const float* GetCascadeSplits() { return CascadeSplits; }
};
