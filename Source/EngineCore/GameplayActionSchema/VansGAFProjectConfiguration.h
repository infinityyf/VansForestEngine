#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansGameplaySchemaTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
enum class VansGAFNetworkMode : std::uint8_t
{
	Disabled,
	Loopback,
	ExternalTransport
};

enum class VansGAFValidationStage : std::uint8_t
{
	Save,
	Cook,
	CI
};

struct VansGAFPerformanceBudget
{
	std::uint32_t maximumActiveActionsPerHost = 64;
	std::uint32_t maximumTasksPerAction = 64;
	std::uint32_t maximumGraphTransitionsPerTick = 1024;
	std::uint32_t maximumEffectsPerHost = 256;
	std::uint32_t maximumPayloadBytes = 4096;
};

struct VansGAFSettings
{
	std::uint32_t schemaVersion = 1;
	std::vector<std::string> defaultTagRoots;
	VansGAFNetworkMode networkMode = VansGAFNetworkMode::Disabled;
	bool predictionEnabled = false;
	bool requireRollbackPlan = true;
	bool failWithoutTransport = true;
	bool deterministicCook = true;
	bool stripEditorMetadata = true;
	bool treatCookWarningsAsErrors = false;
	std::string templateDirectory = "EngineAssets/GAF/Templates";
	VansGAFPerformanceBudget performance;
};

struct VansGAFSchemaAllowlist
{
	std::unordered_set<std::string> nodeTypes;
	std::unordered_set<std::string> services;
	std::unordered_set<std::string> handlers;
	std::unordered_set<std::string> bridges;
};

struct VansGAFValidationRules
{
	std::unordered_map<std::string, std::string> severityOverrides;
	std::unordered_set<std::string> saveBlockingCodes;
	std::unordered_set<std::string> cookBlockingCodes;
	std::unordered_set<std::string> ciBlockingCodes;
};

struct VansGAFProjectConfiguration
{
	VansGAFSettings settings;
	VansGAFSchemaAllowlist allowlist;
	VansGAFValidationRules validation;
	std::unordered_map<std::string, VansSerializedValue> templates;

	static bool Load(const std::filesystem::path& projectSettingsDirectory,
		VansGAFProjectConfiguration& configuration, std::string& error);
	static bool LoadForProject(const std::filesystem::path& projectRoot,
		const std::filesystem::path& engineRoot,
		VansGAFProjectConfiguration& configuration, std::string& error);
	static bool EnsureProjectFiles(const std::filesystem::path& projectSettingsDirectory,
		const std::filesystem::path& builtInSettingsDirectory, std::string& error);
	static bool Save(const std::filesystem::path& projectSettingsDirectory,
		const VansGAFProjectConfiguration& configuration, std::string& error);
	void ApplyValidationPolicy(VansGameplayDiagnostics& diagnostics) const;
	bool IsBlockingDiagnostic(
		const VansGameplayDiagnostic& diagnostic,
		VansGAFValidationStage stage) const;
	bool HasBlockingDiagnostics(
		const VansGameplayDiagnostics& diagnostics,
		VansGAFValidationStage stage) const;
	bool Validate(std::string& error) const;
};
}
