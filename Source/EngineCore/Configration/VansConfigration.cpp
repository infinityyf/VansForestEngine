#include "VansConfigration.h"

VansConfigration*  VansConfigration::instance = nullptr;

VansConfigration::VansConfigration()
{
	ShadowMapHeight = 2048;
	ShadowMapWidth = 2048;
	PunctualShadowMapWidth = 2048;
	PunctualShadowMapHeight = 2048;
	SupportRayTracing = true;
}

VansConfigration* VansConfigration::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansConfigration();
	}
	return instance;
}
