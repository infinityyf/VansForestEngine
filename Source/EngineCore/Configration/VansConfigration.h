#pragma once

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

public:

	static VansConfigration* GetInstance();

	int GetShadowMapWidth() { return ShadowMapWidth; }

	int GetShadowMapHeight() { return ShadowMapHeight; }

	int GetPunctualShadowMapWidth() { return PunctualShadowMapWidth; }

	int GetPunctualShadowMapHeight() { return PunctualShadowMapHeight; }

	bool GetSupportRayTracing() { return SupportRayTracing; }
};
