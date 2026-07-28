#pragma once

#include <string>

namespace Vans
{
	struct VansKeyEvent
	{
		int key = 0;
		int scancode = 0;
		int action = 0;
		int mods = 0;
	};

	struct VansMouseMoveEvent
	{
		double x = 0.0;
		double y = 0.0;
		double dx = 0.0;
		double dy = 0.0;
	};

	struct VansMouseButtonEvent
	{
		int button = 0;
		int action = 0;
		int mods = 0;
	};

	struct VansMouseScrollEvent
	{
		double xOffset = 0.0;
		double yOffset = 0.0;
	};

	struct VansInputActionEvent
	{
		std::string actionName;
		bool pressed = false;
		bool released = false;
		bool down = false;
	};
}
