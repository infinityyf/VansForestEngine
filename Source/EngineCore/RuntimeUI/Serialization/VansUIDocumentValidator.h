#pragma once

#include "../Public/VansUIScreenConfig.h"
#include "../Public/VansUIComponentConfig.h"

#include <string>
#include <vector>

namespace VansRuntime
{
	class VansUIDocumentValidator
	{
	public:
		static bool ValidateScreenConfig(const VansUIScreenConfig& config, std::vector<std::string>& diagnostics);
		static bool ValidateComponentConfig(const VansUIComponentConfig& config, std::vector<std::string>& diagnostics);
	};
}
