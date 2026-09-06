#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansGameplaySchemaTypes.h"

#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
class VansGAFSchemaRegistry;
class VansGAFTypeRegistry;

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
	std::vector<std::string> defaultTagRoots;
	bool deterministicCook = true;
	bool stripEditorMetadata = true;
	bool treatCookWarningsAsErrors = false;
	std::string templateDirectory = "EngineAssets/GAF/Templates";
	VansGAFPerformanceBudget performance;
};

struct VansGAFSchemaAllowlist
{
	std::unordered_set<std::string> nodeTypes;
	std::unordered_set<std::string> modules;
	std::unordered_set<std::string> capabilities;
	std::unordered_set<std::string> policies;
	std::unordered_set<std::string> guards;
	std::unordered_set<std::string> operations;
	std::unordered_set<std::string> drivers;
	std::unordered_set<std::string> extensions;
	std::unordered_set<std::string> transitions;
	std::unordered_set<std::string> signals;
	std::unordered_set<std::string> valueTypes;
};

struct VansGAFValidationRules
{
	std::unordered_map<std::string, std::string> severityOverrides;
	std::unordered_set<std::string> saveBlockingCodes;
	std::unordered_set<std::string> cookBlockingCodes;
	std::unordered_set<std::string> ciBlockingCodes;
};

struct VansGAFConfiguredInputField
{
	std::string name;
	std::string valueType;
	bool required = false;
	VansSerializedValue defaultValue;
};

struct VansGAFConfiguredType
{
	std::string moduleId;
	std::string typeId;
	std::string displayName;
	std::string kind;
	std::vector<VansGAFConfiguredInputField> fields;
};

struct VansGAFProjectConfigurationDocuments
{
	VansSerializedValue settings = VansSerializedValue::Object({});
	VansSerializedValue schemaRegistry = VansSerializedValue::Object({});
	VansSerializedValue validationRules = VansSerializedValue::Object({});
	VansSerializedValue templates = VansSerializedValue::Object({});
};

struct VansGAFProjectConfiguration
{
	VansGAFSettings settings;
	VansGAFSchemaAllowlist allowlist;
	std::vector<VansGAFConfiguredType> configuredTypes;
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
	static std::array<std::string_view, 4> DocumentFileNames();
	static bool DecodeDocuments(
		const VansGAFProjectConfigurationDocuments& documents,
		VansGAFProjectConfiguration& configuration,
		std::string& error);
	static VansGAFProjectConfigurationDocuments EncodeDocuments(
		const VansGAFProjectConfiguration& configuration);
	bool RegisterConfiguredTypes(VansGAFTypeRegistry& registry, std::string& error) const;
	bool RegisterConfiguredSchemas(VansGAFSchemaRegistry& registry, std::string& error) const;
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
