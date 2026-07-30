#pragma once

#include "VansProjectConfigDiagnostics.h"

#include <string>

namespace Vans
{
	struct VansProjectConfig;

	enum class VansProjectConfigPathField
	{
		DefaultScene,
		AssetsRoot,
		ImportedArtifactRoot,
		RenderSettings,
		PhysicsSettings,
		CollisionLayerSettings
	};

	class VansProjectConfigValidator
	{
	public:
		static VansProjectConfigDiagnostics Validate(const VansProjectConfig& config);
		static bool HasErrors(const VansProjectConfigDiagnostics& diagnostics);
		static bool ValidateForSave(
			const VansProjectConfig& config,
			VansProjectConfigDiagnostics& diagnostics,
			std::string& error);
		static bool IsSafeProjectRelativePath(const std::string& relativePath);
		static std::string NormalizeProjectRelativePath(const std::string& relativePath);
	};
}

