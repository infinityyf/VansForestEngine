#pragma once

#include "../../RenderCore/VulkanCore/VansVKDevice.h"

namespace VansGraphics
{
	class VansBaseWindowComponent
	{
	public:
		virtual ~VansBaseWindowComponent() = default;
		virtual void ShowWindow(VansVKDevice& device) = 0;
	};
}

