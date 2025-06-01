#pragma once

#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansLightWindow : public VansBaseWindowComponent
	{
	private:
		void ShowWindow(VansVKDevice& device) override;
	};
}
