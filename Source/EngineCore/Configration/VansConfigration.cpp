#include "VansConfigration.h"

VansConfigration*  VansConfigration::instance = nullptr;

VansConfigration::VansConfigration()
{
	ShadowMapHeight = 2048;
	ShadowMapWidth = 2048;
}

VansConfigration* VansConfigration::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansConfigration();
	}
	return instance;
}
