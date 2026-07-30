#pragma once

#include <string>
#include <vector>

namespace Vans
{
	enum class VansProjectConfigDiagnosticSeverity
	{
		Info,
		Warning,
		Error
	};

	struct VansProjectConfigDiagnostic
	{
		VansProjectConfigDiagnosticSeverity severity = VansProjectConfigDiagnosticSeverity::Info;
		std::string propertyPointer;
		std::string message;
	};

	using VansProjectConfigDiagnostics = std::vector<VansProjectConfigDiagnostic>;
}

