#pragma once

class VansConfigration
{
private:
	static VansConfigration* instance;

	VansConfigration();

private:

	int ShadowMapWidth;

	int ShadowMapHeight;

public:

	static VansConfigration* GetInstance();

	int GetShadowMapWidth() { return ShadowMapWidth; }

	int GetShadowMapHeight() { return ShadowMapHeight; }
};
