#pragma once

namespace VansGraphics
{
	class INativeWindowProvider
	{
	public:
		virtual ~INativeWindowProvider() = default;
		virtual void* GetNativeWindowHandle() const = 0;
	};
}
