#pragma once

#include "../../RenderCore/VulkanCore/VansVKDevice.h"

namespace VansGraphics
{
	class VansBaseWindowComponent
	{
	public:
		virtual void ShowWindow(VansVKDevice& device) = 0;
	};
}

