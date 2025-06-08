#include "VansConfigration.h"

VansConfigration*  VansConfigration::instance = nullptr;

VansConfigration::VansConfigration()
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
