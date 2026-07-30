#include "VansProjectConfigValidator.h"

#include "VansProjectConfig.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace Vans
{
	namespace
	{
		void AddDiagnostic(
			VansProjectConfigDiagnostics& diagnostics,
			VansProjectConfigDiagnosticSeverity severity,
			std::string propertyPointer,
			std::string message)
		{
			VansProjectConfigDiagnostic diagnostic;
			diagnostic.severity = severity;
			diagnostic.propertyPointer = std::move(propertyPointer);
			diagnostic.message = std::move(message);
			diagnostics.push_back(std::move(diagnostic));
		}

		bool IsAllowedTopLevelDirectory(const std::string& normalized)
		{
			const std::string first = normalized.substr(0, normalized.find('/'));
			return first == "Assets" ||
				first == "Library" ||
				first == "ProjectSettings" ||
				first == "Scenes" ||
				first == "Scripts";
		}

		void ValidateRequiredPath(
			VansProjectConfigDiagnostics& diagnostics,
			const std::string& propertyPointer,
			const std::string& value)
		{
			if (!VansProjectConfigValidator::IsSafeProjectRelativePath(value))
			{
				AddDiagnostic(
					diagnostics,
					VansProjectConfigDiagnosticSeverity::Error,
					propertyPointer,
					"Path must be project-relative and must not contain parent-directory traversal.");
			}
		}
	}

	std::string VansProjectConfigValidator::NormalizeProjectRelativePath(const std::string& relativePath)
	{
		std::string normalized = relativePath;
		std::replace(normalized.begin(), normalized.end(), '\\', '/');
		while (!normalized.empty() && normalized.front() == '/')
			normalized.erase(normalized.begin());
		while (normalized.size() > 1 && normalized.back() == '/')
			normalized.pop_back();
		return normalized;
	}

	bool VansProjectConfigValidator::IsSafeProjectRelativePath(const std::string& relativePath)
	{
		const std::string normalized = NormalizeProjectRelativePath(relativePath);
		if (normalized.empty())
			return false;

		const fs::path path(normalized);
		if (path.is_absolute())
			return false;

		for (const fs::path& part : path)
		{
			const std::string value = part.string();
			if (value == "..")
				return false;
		}

		return IsAllowedTopLevelDirectory(normalized);
	}

	VansProjectConfigDiagnostics VansProjectConfigValidator::Validate(const VansProjectConfig& config)
	{
		VansProjectConfigDiagnostics diagnostics;

		if (config.projectName.empty())
		{
			AddDiagnostic(
				diagnostics,
				VansProjectConfigDiagnosticSeverity::Error,
				"/projectName",
				"Project name must not be empty.");
		}

		ValidateRequiredPath(diagnostics, "/defaultScene", config.defaultScene);
		ValidateRequiredPath(diagnostics, "/assetDatabase/assetsRoot", config.assetsRoot);
		ValidateRequiredPath(diagnostics, "/assetDatabase/importedArtifactRoot", config.importedArtifactRoot);
		ValidateRequiredPath(diagnostics, "/renderSettings", config.renderSettings);
		ValidateRequiredPath(diagnostics, "/physicsSettings", config.physicsSettings);
		ValidateRequiredPath(diagnostics, "/collisionLayerSettings", config.collisionLayerSettings);

		if (config.metaExtension.empty() || config.metaExtension.front() != '.')
		{
			AddDiagnostic(
				diagnostics,
				VansProjectConfigDiagnosticSeverity::Warning,
				"/assetDatabase/metaExtension",
				"Meta extension should start with '.'.");
		}

		std::unordered_set<std::string> assetDirectoryPaths;
		for (const auto& item : config.assetDirectories)
		{
			const std::string pointer = "/assetDirectories/" + item.first;
			if (item.first.empty())
			{
				AddDiagnostic(
					diagnostics,
					VansProjectConfigDiagnosticSeverity::Error,
					pointer,
					"Asset directory key must not be empty.");
			}
			ValidateRequiredPath(diagnostics, pointer, item.second);

			const std::string normalizedPath = NormalizeProjectRelativePath(item.second);
			if (!normalizedPath.empty() && !assetDirectoryPaths.insert(normalizedPath).second)
			{
				AddDiagnostic(
					diagnostics,
					VansProjectConfigDiagnosticSeverity::Warning,
					pointer,
					"Asset directory duplicates another logical directory path.");
			}
		}

		for (std::size_t index = 0; index < config.scriptSearchPaths.size(); ++index)
		{
			ValidateRequiredPath(
				diagnostics,
				"/scriptSearchPaths/" + std::to_string(index),
				config.scriptSearchPaths[index]);
		}

		if (config.scriptSearchPaths.empty())
		{
			AddDiagnostic(
				diagnostics,
				VansProjectConfigDiagnosticSeverity::Warning,
				"/scriptSearchPaths",
				"Script search paths are empty.");
		}

		return diagnostics;
	}

	bool VansProjectConfigValidator::HasErrors(const VansProjectConfigDiagnostics& diagnostics)
	{
		return std::any_of(
			diagnostics.begin(),
			diagnostics.end(),
			[](const VansProjectConfigDiagnostic& diagnostic)
			{
				return diagnostic.severity == VansProjectConfigDiagnosticSeverity::Error;
			});
	}

	bool VansProjectConfigValidator::ValidateForSave(
		const VansProjectConfig& config,
		VansProjectConfigDiagnostics& diagnostics,
		std::string& error)
	{
		diagnostics = Validate(config);
		if (!HasErrors(diagnostics))
			return true;

		for (const VansProjectConfigDiagnostic& diagnostic : diagnostics)
		{
			if (diagnostic.severity == VansProjectConfigDiagnosticSeverity::Error)
			{
				error = diagnostic.propertyPointer + ": " + diagnostic.message;
				break;
			}
		}
		if (error.empty())
			error = "Project config validation failed.";
		return false;
	}
}

