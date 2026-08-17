#include "VansGAFProjectConfiguration.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>

namespace Vans
{
namespace
{
using Json = nlohmann::ordered_json;

constexpr const char* kConfigurationFiles[] = {
	"GAFSettings.json",
	"GAFSchemaRegistry.json",
	"GAFValidationRules.json",
	"GAFTemplates.json"
};

bool ReadRequired(const std::filesystem::path& path, Json& root, std::string& error)
{
	if (!VansJsonFileStorage::Read(path, root, error))
	{
		error = path.filename().string() + ": " + error;
		return false;
	}
	if (!root.is_object() || root.value("schemaVersion", 0u) != 1u)
	{
		error = path.filename().string() + ": invalid root or schemaVersion";
		return false;
	}
	return true;
}

bool IsTemplateFile(const std::filesystem::path& path)
{
	const std::string extension = path.extension().string();
	return extension == ".json" || extension == ".vaction" || extension == ".vactionset" ||
		extension == ".veffect" || extension == ".vcue" || extension == ".vattributeset" ||
		extension == ".vtargeting" || extension == ".vtagtree" ||
		extension == ".vpayloadschema" || extension == ".vactiongraph" ||
		extension == ".vcamerarig" || extension == ".vcamerashake" ||
		extension == ".gafeditorlayout";
}

void ReadStringSet(const Json& root, const char* name, std::unordered_set<std::string>& output)
{
	if (!root.contains(name) || !root[name].is_array()) return;
	for (const Json& item : root[name])
		if (item.is_string() && !item.get_ref<const std::string&>().empty())
			output.insert(item.get<std::string>());
}

Json SortedStrings(const std::unordered_set<std::string>& values)
{
	std::vector<std::string> sorted(values.begin(), values.end());
	std::sort(sorted.begin(), sorted.end());
	return sorted;
}

const char* NetworkModeName(VansGAFNetworkMode mode)
{
	switch (mode)
	{
	case VansGAFNetworkMode::Disabled: return "Disabled";
	case VansGAFNetworkMode::Loopback: return "Loopback";
	case VansGAFNetworkMode::ExternalTransport: return "ExternalTransport";
	}
	return "Disabled";
}

bool ReadNetworkMode(std::string_view name, VansGAFNetworkMode& mode)
{
	if (name == "Disabled") mode = VansGAFNetworkMode::Disabled;
	else if (name == "Loopback") mode = VansGAFNetworkMode::Loopback;
	else if (name == "ExternalTransport") mode = VansGAFNetworkMode::ExternalTransport;
	else return false;
	return true;
}

bool ReadDiagnosticSeverity(std::string_view name, VansGameplayDiagnosticSeverity& severity)
{
	if (name == "Info") severity = VansGameplayDiagnosticSeverity::Info;
	else if (name == "Warning") severity = VansGameplayDiagnosticSeverity::Warning;
	else if (name == "Error") severity = VansGameplayDiagnosticSeverity::Error;
	else if (name == "Fatal") severity = VansGameplayDiagnosticSeverity::Fatal;
	else return false;
	return true;
}

Json BuildSettingsJson(const VansGAFProjectConfiguration& configuration)
{
	const VansGAFSettings& settings = configuration.settings;
	return Json{
		{ "schemaVersion", settings.schemaVersion },
		{ "defaultTagRoots", settings.defaultTagRoots },
		{ "network", Json{
			{ "mode", NetworkModeName(settings.networkMode) },
			{ "predictionEnabled", settings.predictionEnabled },
			{ "requireRollbackPlan", settings.requireRollbackPlan },
			{ "failWithoutTransport", settings.failWithoutTransport },
			{ "maximumPayloadBytes", settings.performance.maximumPayloadBytes }
		} },
		{ "cook", Json{
			{ "deterministic", settings.deterministicCook },
			{ "stripEditorMetadata", settings.stripEditorMetadata },
			{ "treatWarningsAsErrors", settings.treatCookWarningsAsErrors }
		} },
		{ "performance", Json{
			{ "maximumActiveActionsPerHost", settings.performance.maximumActiveActionsPerHost },
			{ "maximumTasksPerAction", settings.performance.maximumTasksPerAction },
			{ "maximumGraphTransitionsPerTick", settings.performance.maximumGraphTransitionsPerTick },
			{ "maximumEffectsPerHost", settings.performance.maximumEffectsPerHost }
		} },
		{ "templateDirectory", settings.templateDirectory }
	};
}

Json BuildSchemaJson(const VansGAFProjectConfiguration& configuration)
{
	return Json{
		{ "schemaVersion", 1 },
		{ "allowedNodeTypes", SortedStrings(configuration.allowlist.nodeTypes) },
		{ "allowedServices", SortedStrings(configuration.allowlist.services) },
		{ "allowedHandlers", SortedStrings(configuration.allowlist.handlers) },
		{ "bridgeAllowlist", SortedStrings(configuration.allowlist.bridges) }
	};
}

Json BuildValidationJson(const VansGAFProjectConfiguration& configuration)
{
	Json overrides = Json::object();
	std::vector<std::pair<std::string, std::string>> sortedOverrides(
		configuration.validation.severityOverrides.begin(),
		configuration.validation.severityOverrides.end());
	std::sort(sortedOverrides.begin(), sortedOverrides.end());
	for (const auto& entry : sortedOverrides) overrides[entry.first] = entry.second;
	return Json{
		{ "schemaVersion", 1 },
		{ "severityOverrides", std::move(overrides) },
		{ "saveBlockingCodes", SortedStrings(configuration.validation.saveBlockingCodes) },
		{ "cookBlockingCodes", SortedStrings(configuration.validation.cookBlockingCodes) },
		{ "ciBlockingCodes", SortedStrings(configuration.validation.ciBlockingCodes) }
	};
}

Json BuildTemplatesJson(const VansGAFProjectConfiguration& configuration)
{
	Json templates = Json::object();
	std::vector<std::string> names;
	names.reserve(configuration.templates.size());
	for (const auto& entry : configuration.templates) names.push_back(entry.first);
	std::sort(names.begin(), names.end());
	for (const std::string& name : names)
		templates[name] = EncodeSerializedValueJson<Json>(configuration.templates.at(name));
	return Json{ { "schemaVersion", 1 }, { "templates", std::move(templates) } };
}

bool LoadTemplateDirectory(
	const std::filesystem::path& configurationRoot,
	const std::string& configuredDirectory,
	VansGAFProjectConfiguration& configuration,
	std::string& error)
{
	const std::filesystem::path relative(configuredDirectory);
	if (relative.is_absolute())
	{
		error = "GAF templateDirectory must be relative to the project or engine root";
		return false;
	}
	std::error_code pathError;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(configurationRoot, pathError);
	if (pathError)
	{
		error = "GAF configuration root cannot be resolved: " + pathError.message();
		return false;
	}
	const std::filesystem::path directory =
		std::filesystem::weakly_canonical(root / relative, pathError);
	if (pathError)
	{
		error = "GAF templateDirectory cannot be resolved: " + pathError.message();
		return false;
	}
	const std::filesystem::path contained = directory.lexically_relative(root);
	if (contained.empty() || contained.is_absolute() ||
		(!contained.empty() && *contained.begin() == ".."))
	{
		error = "GAF templateDirectory escapes the project or engine root";
		return false;
	}
	if (!std::filesystem::exists(directory)) return true;
	if (!std::filesystem::is_directory(directory))
	{
		error = "GAF templateDirectory is not a directory: " + directory.string();
		return false;
	}
	std::vector<std::filesystem::path> files;
	for (const std::filesystem::directory_entry& entry :
		std::filesystem::directory_iterator(directory))
		if (entry.is_regular_file() && IsTemplateFile(entry.path()))
			files.push_back(entry.path());
	std::sort(files.begin(), files.end());
	std::unordered_set<std::string> externalKinds;
	for (const std::filesystem::path& file : files)
	{
		Json document;
		if (!VansJsonFileStorage::Read(file, document, error))
		{
			error = "GAF template '" + file.filename().string() + "': " + error;
			return false;
		}
		if (!document.is_object() || !document.contains("assetKind") ||
			!document["assetKind"].is_string())
		{
			error = "GAF template is missing assetKind: " + file.filename().string();
			return false;
		}
		const std::string assetKind = document["assetKind"].get<std::string>();
		if (assetKind.empty() || !externalKinds.insert(assetKind).second)
		{
			error = "GAF templateDirectory contains a duplicate assetKind: " + assetKind;
			return false;
		}
		configuration.templates[assetKind] = DecodeSerializedValueJson(document);
	}
	return true;
}
}

bool VansGAFProjectConfiguration::Load(
	const std::filesystem::path& directory,
	VansGAFProjectConfiguration& configuration,
	std::string& error)
{
	configuration = {};
	Json settings;
	Json schemas;
	Json validation;
	Json templates;
	if (!ReadRequired(directory / "GAFSettings.json", settings, error) ||
		!ReadRequired(directory / "GAFSchemaRegistry.json", schemas, error) ||
		!ReadRequired(directory / "GAFValidationRules.json", validation, error) ||
		!ReadRequired(directory / "GAFTemplates.json", templates, error)) return false;
	configuration.settings.schemaVersion = settings.value("schemaVersion", 1u);
	if (settings.contains("defaultTagRoots") && settings["defaultTagRoots"].is_array())
		configuration.settings.defaultTagRoots = settings["defaultTagRoots"].get<std::vector<std::string>>();
	if (settings.contains("network") && settings["network"].is_object())
	{
		if (!ReadNetworkMode(settings["network"].value("mode", std::string("Disabled")),
			configuration.settings.networkMode))
		{
			error = "GAFSettings.json: invalid network mode";
			return false;
		}
		configuration.settings.predictionEnabled = settings["network"].value("predictionEnabled", false);
		configuration.settings.requireRollbackPlan = settings["network"].value("requireRollbackPlan", true);
		configuration.settings.failWithoutTransport =
			settings["network"].value("failWithoutTransport", true);
		configuration.settings.performance.maximumPayloadBytes =
			settings["network"].value("maximumPayloadBytes", 4096u);
	}
	if (settings.contains("cook") && settings["cook"].is_object())
	{
		configuration.settings.deterministicCook = settings["cook"].value("deterministic", true);
		configuration.settings.stripEditorMetadata = settings["cook"].value("stripEditorMetadata", true);
		configuration.settings.treatCookWarningsAsErrors =
			settings["cook"].value("treatWarningsAsErrors", false);
	}
	configuration.settings.templateDirectory =
		settings.value("templateDirectory", std::string("EngineAssets/GAF/Templates"));
	if (settings.contains("performance") && settings["performance"].is_object())
	{
		const Json& performance = settings["performance"];
		configuration.settings.performance.maximumActiveActionsPerHost =
			performance.value("maximumActiveActionsPerHost", 64u);
		configuration.settings.performance.maximumTasksPerAction =
			performance.value("maximumTasksPerAction", 64u);
		configuration.settings.performance.maximumGraphTransitionsPerTick =
			performance.value("maximumGraphTransitionsPerTick", 1024u);
		configuration.settings.performance.maximumEffectsPerHost =
			performance.value("maximumEffectsPerHost", 256u);
	}
	ReadStringSet(schemas, "allowedNodeTypes", configuration.allowlist.nodeTypes);
	ReadStringSet(schemas, "allowedServices", configuration.allowlist.services);
	ReadStringSet(schemas, "allowedHandlers", configuration.allowlist.handlers);
	ReadStringSet(schemas, "bridgeAllowlist", configuration.allowlist.bridges);
	if (validation.contains("severityOverrides") && validation["severityOverrides"].is_object())
		for (auto entry = validation["severityOverrides"].begin();
			entry != validation["severityOverrides"].end(); ++entry)
			if (entry.value().is_string())
				configuration.validation.severityOverrides.emplace(entry.key(), entry.value().get<std::string>());
	ReadStringSet(validation, "saveBlockingCodes", configuration.validation.saveBlockingCodes);
	ReadStringSet(validation, "cookBlockingCodes", configuration.validation.cookBlockingCodes);
	ReadStringSet(validation, "ciBlockingCodes", configuration.validation.ciBlockingCodes);
	if (templates.contains("templates") && templates["templates"].is_object())
		for (auto entry = templates["templates"].begin(); entry != templates["templates"].end(); ++entry)
			configuration.templates.emplace(entry.key(), DecodeSerializedValueJson(entry.value()));
	return configuration.Validate(error);
}

bool VansGAFProjectConfiguration::LoadForProject(
	const std::filesystem::path& projectRoot,
	const std::filesystem::path& engineRoot,
	VansGAFProjectConfiguration& configuration,
	std::string& error)
{
	const std::filesystem::path projectDirectory = projectRoot / "ProjectSettings";
	bool projectConfigurationComplete = true;
	for (const char* fileName : kConfigurationFiles)
		projectConfigurationComplete = projectConfigurationComplete &&
			std::filesystem::is_regular_file(projectDirectory / fileName);
	const std::filesystem::path directory = projectConfigurationComplete
		? projectDirectory
		: engineRoot / "EngineAssets/GAF/ProjectSettings";
	if (!Load(directory, configuration, error))
	{
		error = std::string(projectConfigurationComplete ? "project" : "built-in") +
			" GAF configuration: " + error;
		return false;
	}
	if (!LoadTemplateDirectory(projectConfigurationComplete ? projectRoot : engineRoot,
		configuration.settings.templateDirectory, configuration, error)) return false;
	return configuration.Validate(error);
}

bool VansGAFProjectConfiguration::EnsureProjectFiles(
	const std::filesystem::path& projectSettingsDirectory,
	const std::filesystem::path& builtInSettingsDirectory,
	std::string& error)
{
	VansGAFProjectConfiguration builtInConfiguration;
	if (!Load(builtInSettingsDirectory, builtInConfiguration, error))
	{
		error = "built-in GAF configuration is unavailable: " + error;
		return false;
	}
	std::error_code directoryError;
	std::filesystem::create_directories(projectSettingsDirectory, directoryError);
	if (directoryError)
	{
		error = "cannot create GAF project settings directory: " + directoryError.message();
		return false;
	}
	for (const char* fileName : kConfigurationFiles)
	{
		const std::filesystem::path target = projectSettingsDirectory / fileName;
		if (std::filesystem::is_regular_file(target)) continue;
		std::string bytes;
		if (!VansFileStorage::ReadAllBytes(builtInSettingsDirectory / fileName, bytes, error) ||
			!VansFileStorage::WriteAtomicBytes(target, bytes, error))
		{
			error = std::string("cannot initialize ") + fileName + ": " + error;
			return false;
		}
	}
	VansGAFProjectConfiguration projectConfiguration;
	return Load(projectSettingsDirectory, projectConfiguration, error);
}

bool VansGAFProjectConfiguration::Save(
	const std::filesystem::path& directory,
	const VansGAFProjectConfiguration& configuration,
	std::string& error)
{
	if (!configuration.Validate(error)) return false;
	std::error_code directoryError;
	std::filesystem::create_directories(directory, directoryError);
	if (directoryError)
	{
		error = "cannot create GAF project settings directory: " + directoryError.message();
		return false;
	}
	const std::array<Json, 4> documents{
		BuildSettingsJson(configuration), BuildSchemaJson(configuration),
		BuildValidationJson(configuration), BuildTemplatesJson(configuration)
	};
	std::array<std::string, 4> previousBytes;
	std::array<bool, 4> hadPrevious{};
	for (std::size_t index = 0; index < std::size(kConfigurationFiles); ++index)
	{
		const std::filesystem::path path = directory / kConfigurationFiles[index];
		if (!std::filesystem::is_regular_file(path)) continue;
		std::string readError;
		hadPrevious[index] = VansFileStorage::ReadAllBytes(path, previousBytes[index], readError);
		if (!hadPrevious[index])
		{
			error = std::string("cannot snapshot ") + kConfigurationFiles[index] + ": " + readError;
			return false;
		}
	}
	const auto rollbackFiles = [&](std::size_t count, std::string& rollbackError)
	{
		for (std::size_t rollback = 0; rollback < count; ++rollback)
		{
			const std::filesystem::path path = directory / kConfigurationFiles[rollback];
			if (hadPrevious[rollback])
			{
				std::string restoreError;
				if (!VansFileStorage::WriteAtomicBytes(path, previousBytes[rollback], restoreError))
				{
					rollbackError = std::string(kConfigurationFiles[rollback]) + ": " + restoreError;
					return false;
				}
			}
			else
			{
				std::error_code removeError;
				std::filesystem::remove(path, removeError);
				if (removeError)
				{
					rollbackError = std::string(kConfigurationFiles[rollback]) + ": " + removeError.message();
					return false;
				}
			}
		}
		return true;
	};
	for (std::size_t index = 0; index < documents.size(); ++index)
	{
		if (VansJsonFileStorage::WriteAtomic(directory / kConfigurationFiles[index], documents[index], error))
			continue;
		const std::string writeError = std::string(kConfigurationFiles[index]) + ": " + error;
		std::string rollbackError;
		if (!rollbackFiles(index, rollbackError))
			error = writeError + "; rollback failed for " + rollbackError;
		else
			error = writeError;
		return false;
	}
	VansGAFProjectConfiguration verified;
	if (!Load(directory, verified, error))
	{
		const std::string verificationError =
			"saved GAF project configuration failed verification: " + error;
		std::string rollbackError;
		error = rollbackFiles(documents.size(), rollbackError)
			? verificationError : verificationError + "; rollback failed for " + rollbackError;
		return false;
	}
	return true;
}

bool VansGAFProjectConfiguration::Validate(std::string& error) const
{
	if (settings.schemaVersion != 1 || settings.defaultTagRoots.empty() ||
		settings.templateDirectory.empty())
	{
		error = "GAFSettings requires schemaVersion, defaultTagRoots and templateDirectory";
		return false;
	}
	std::unordered_set<std::string> tagRoots;
	for (const std::string& root : settings.defaultTagRoots)
		if (root.empty() || !tagRoots.insert(root).second)
		{
			error = "GAFSettings defaultTagRoots contains an empty or duplicate root";
			return false;
		}
	if (settings.networkMode == VansGAFNetworkMode::Disabled && settings.predictionEnabled)
	{
		error = "GAF prediction requires Loopback or ExternalTransport network mode";
		return false;
	}
	const VansGAFPerformanceBudget& budget = settings.performance;
	if (budget.maximumActiveActionsPerHost == 0 || budget.maximumTasksPerAction == 0 ||
		budget.maximumGraphTransitionsPerTick == 0 || budget.maximumEffectsPerHost == 0 ||
		budget.maximumPayloadBytes == 0)
	{
		error = "GAF performance budgets must be positive";
		return false;
	}
	for (const auto& entry : validation.severityOverrides)
		if (entry.second != "Info" && entry.second != "Warning" && entry.second != "Error" &&
			entry.second != "Fatal")
		{
			error = "GAF validation severity override is invalid: " + entry.first;
			return false;
		}
	if (templates.empty())
	{
		error = "GAFTemplates contains no templates";
		return false;
	}
	for (const auto& entry : templates)
	{
		if (entry.first.empty() || entry.second.kind != VansSerializedValue::Kind::Object ||
			ReadSerializedStringField(entry.second, "assetKind") != entry.first ||
			ReadSerializedIntField(entry.second, "schemaVersion", 0) <= 0)
		{
			error = "GAF template identity is invalid: " + entry.first;
			return false;
		}
	}
	return true;
}

void VansGAFProjectConfiguration::ApplyValidationPolicy(
	VansGameplayDiagnostics& diagnostics) const
{
	for (VansGameplayDiagnostic& diagnostic : diagnostics)
	{
		const auto found = validation.severityOverrides.find(diagnostic.code);
		if (found == validation.severityOverrides.end()) continue;
		VansGameplayDiagnosticSeverity severity;
		if (ReadDiagnosticSeverity(found->second, severity)) diagnostic.severity = severity;
	}
}

bool VansGAFProjectConfiguration::IsBlockingDiagnostic(
	const VansGameplayDiagnostic& diagnostic,
	VansGAFValidationStage stage) const
{
	if (diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
		diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal) return true;
	const std::unordered_set<std::string>* blockingCodes = nullptr;
	switch (stage)
	{
	case VansGAFValidationStage::Save: blockingCodes = &validation.saveBlockingCodes; break;
	case VansGAFValidationStage::Cook: blockingCodes = &validation.cookBlockingCodes; break;
	case VansGAFValidationStage::CI: blockingCodes = &validation.ciBlockingCodes; break;
	}
	if (blockingCodes && blockingCodes->find(diagnostic.code) != blockingCodes->end()) return true;
	return stage == VansGAFValidationStage::Cook && settings.treatCookWarningsAsErrors &&
		diagnostic.severity == VansGameplayDiagnosticSeverity::Warning;
}

bool VansGAFProjectConfiguration::HasBlockingDiagnostics(
	const VansGameplayDiagnostics& diagnostics,
	VansGAFValidationStage stage) const
{
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& diagnostic)
	{
		return IsBlockingDiagnostic(diagnostic, stage);
	});
}
}
