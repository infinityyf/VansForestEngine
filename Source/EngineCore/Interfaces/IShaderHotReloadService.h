#pragma once

#include <string>

namespace VansGraphics
{
	class IShaderHotReloadService
	{
	public:
		virtual ~IShaderHotReloadService() = default;
		virtual void WatchFolder(const std::string& folder) = 0;
		virtual bool ConsumeUpdatedShaderFolder(const std::string& folder) = 0;
	};
}
