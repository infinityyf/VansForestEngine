#include "VansConfigration.h"

VansConfigration*  VansConfigration::instance = nullptr;

VansConfigration::VansConfigration():
	m_EnableDeferredRendering(true)
{

}

VansConfigration* VansConfigration::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansConfigration();
	}
	return instance;
}
