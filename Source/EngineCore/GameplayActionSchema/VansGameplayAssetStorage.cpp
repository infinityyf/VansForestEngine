#include "VansGameplayAssetStorage.h"

#include "../GameplayActionCore/VansGAFExtensionRegistry.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../RuntimeCore/VansStableIdentity.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace Vans
{
namespace
{
constexpr std::array<char, 8> kCookedMagic{ 'V', 'G', 'A', 'F', 'C', 'O', 'O', 'K' };
constexpr std::size_t kCookedHeaderSize = 8 + 8 + 8;

bool HasErrors(const VansGameplayDiagnostics& diagnostics)
{
	for (const VansGameplayDiagnostic& diagnostic : diagnostics)
		if (diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
			diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal) return true;
	return false;
}

void AddAllowlistDiagnostic(
	VansGameplayDiagnostics& diagnostics,
	std::string code,
	std::string value,
	std::string fieldPath)
{
	diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, std::move(code),
		"Project GAF allowlist blocks '" + value + "'", {}, std::move(fieldPath) });
}

void ValidateProjectAllowlists(
	VansAssetType type,
	const VansSerializedValue& source,
	const VansGAFProjectConfiguration& configuration,
	VansGameplayDiagnostics& diagnostics)
{
	const auto validateArrayField = [&](const VansSerializedValue* values,
		const char* member,
		const std::unordered_set<std::string>& allowlist,
		const char* code,
		const char* path)
	{
		if (!values || values->kind != VansSerializedValue::Kind::Array) return;
		for (std::size_t index = 0; index < values->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = values->arrayItems[index];
			const std::string value = member
				? ReadSerializedStringField(item, member)
				: ReadSerializedString(item);
			if (!value.empty() && allowlist.find(value) == allowlist.end())
				AddAllowlistDiagnostic(diagnostics, code, value,
					std::string(path) + "/" + std::to_string(index) +
					(member ? std::string("/") + member : std::string{}));
		}
	};
	if (type == VansAssetType::ActionGraph)
	{
		const VansSerializedValue* nodes = FindSerializedPointer(source, "/nodes");
		validateArrayField(nodes, "type", configuration.allowlist.nodeTypes,
			"GAF-PROJECT-NODE-ALLOWLIST", "/nodes");
		if (nodes && nodes->kind == VansSerializedValue::Kind::Array)
			for (std::size_t index = 0; index < nodes->arrayItems.size(); ++index)
			{
				const VansSerializedValue& node = nodes->arrayItems[index];
				const std::string nodePath = "/nodes/" + std::to_string(index);
				const VansSerializedValue* properties =
					FindObjectField(node, "properties");
				const std::string capability = properties
					? ReadSerializedStringField(*properties, "capability") : std::string{};
				if (!capability.empty() &&
					configuration.allowlist.capabilities.find(capability) ==
						configuration.allowlist.capabilities.end())
					AddAllowlistDiagnostic(diagnostics, "GAF-PROJECT-CAPABILITY-ALLOWLIST",
						capability, nodePath + "/properties/capability");
			}
	}
	else if (type == VansAssetType::ActionDefinition)
	{
		validateArrayField(FindSerializedPointer(source, "/policies"), "type",
			configuration.allowlist.policies, "GAF-PROJECT-POLICY-ALLOWLIST", "/policies");
		for (const char* path : { "/phases/activate/guards", "/phases/commit/guards" })
			validateArrayField(FindSerializedPointer(source, path), "type",
				configuration.allowlist.guards, "GAF-PROJECT-GUARD-ALLOWLIST", path);
		for (const char* path : { "/phases/activate/operations", "/phases/commit/operations",
			"/phases/execute/operations", "/phases/finish/operations",
			"/phases/cancel/operations" })
			validateArrayField(FindSerializedPointer(source, path), "type",
				configuration.allowlist.operations, "GAF-PROJECT-OPERATION-ALLOWLIST", path);
		validateArrayField(FindSerializedPointer(source, "/phases/execute/drivers"), "type",
			configuration.allowlist.drivers, "GAF-PROJECT-DRIVER-ALLOWLIST",
			"/phases/execute/drivers");
		validateArrayField(FindSerializedPointer(source, "/transitions"), "type",
			configuration.allowlist.transitions, "GAF-PROJECT-TRANSITION-ALLOWLIST",
			"/transitions");
		validateArrayField(FindSerializedPointer(source, "/extensions"), "type",
			configuration.allowlist.extensions, "GAF-PROJECT-EXTENSION-ALLOWLIST",
			"/extensions");
		validateArrayField(FindSerializedPointer(source, "/context/schema"), "type",
			configuration.allowlist.valueTypes, "GAF-PROJECT-VALUE-TYPE-ALLOWLIST",
			"/context/schema");
		validateArrayField(FindSerializedPointer(source, "/dependencies/capabilities"), nullptr,
			configuration.allowlist.capabilities, "GAF-PROJECT-CAPABILITY-ALLOWLIST",
			"/dependencies/capabilities");
		validateArrayField(FindSerializedPointer(source, "/dependencies/modules"), nullptr,
			configuration.allowlist.modules, "GAF-PROJECT-MODULE-ALLOWLIST",
			"/dependencies/modules");
	}
	else if (type == VansAssetType::ActionSet)
	{
		validateArrayField(FindSerializedPointer(source, "/initializers"), "type",
			configuration.allowlist.extensions, "GAF-PROJECT-EXTENSION-ALLOWLIST",
			"/initializers");
		validateArrayField(FindSerializedPointer(source, "/policies"), "type",
			configuration.allowlist.extensions, "GAF-PROJECT-EXTENSION-ALLOWLIST",
			"/policies");
		if (const VansSerializedValue* grants = FindSerializedPointer(source, "/grants");
			grants && grants->kind == VansSerializedValue::Kind::Array)
			for (std::size_t index = 0; index < grants->arrayItems.size(); ++index)
			{
				const VansSerializedValue* extensions =
					FindObjectField(grants->arrayItems[index], "extensions");
				const std::string path = "/grants/" + std::to_string(index) + "/extensions";
				validateArrayField(extensions, "type", configuration.allowlist.extensions,
					"GAF-PROJECT-EXTENSION-ALLOWLIST", path.c_str());
			}
	}
	else if (type == VansAssetType::GameplayCue)
	{
		validateArrayField(FindSerializedPointer(source, "/bindings"), "type",
			configuration.allowlist.extensions, "GAF-PROJECT-EXTENSION-ALLOWLIST", "/bindings");
		if (const VansSerializedValue* bindings = FindSerializedPointer(source, "/bindings");
			bindings && bindings->kind == VansSerializedValue::Kind::Array)
			for (std::size_t index = 0; index < bindings->arrayItems.size(); ++index)
			{
				const VansSerializedValue* inputs = FindObjectField(bindings->arrayItems[index], "inputs");
				const std::string capability = inputs
					? ReadSerializedStringField(*inputs, "capability") : std::string{};
				if (!capability.empty() && configuration.allowlist.capabilities.find(capability) ==
					configuration.allowlist.capabilities.end())
					AddAllowlistDiagnostic(diagnostics, "GAF-PROJECT-CAPABILITY-ALLOWLIST",
						capability, "/bindings/" + std::to_string(index) + "/inputs/capability");
			}
	}
	else if (type == VansAssetType::TargetingPolicy)
	{
		validateArrayField(FindSerializedPointer(source, "/steps"), "type",
			configuration.allowlist.operations, "GAF-PROJECT-OPERATION-ALLOWLIST", "/steps");
	}
}

void AppendDiagnostics(
	VansGameplayDiagnostics& destination,
	VansGameplayDiagnostics source)
{
	destination.insert(destination.end(),
		std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
}

void ValidateTypedRecordArray(
	const VansSerializedValue& source,
	std::string_view path,
	VansGAFExtensionKind kind,
	const VansGAFSchemaRegistry& schemas,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue* records = FindSerializedPointer(source, std::string(path));
	if (!records || records->kind != VansSerializedValue::Kind::Array) return;
	for (std::size_t index = 0; index < records->arrayItems.size(); ++index)
	{
		const VansSerializedValue& record = records->arrayItems[index];
		const std::string recordPath = std::string(path) + "/" + std::to_string(index);
		const std::string typeId = ReadSerializedStringField(record, "type");
		const VansSerializedValue* inputs = FindObjectField(record, "inputs");
		AppendDiagnostics(diagnostics, schemas.Validate(kind, typeId,
			inputs ? *inputs : VansSerializedValue::Null(), recordPath));
	}
}

void ValidateValueDeclarations(
	const VansSerializedValue& source,
	std::string_view path,
	const VansGAFSchemaRegistry& schemas,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue* declarations = FindSerializedPointer(source, std::string(path));
	if (!declarations || declarations->kind != VansSerializedValue::Kind::Array) return;
	for (std::size_t index = 0; index < declarations->arrayItems.size(); ++index)
	{
		const std::string typeId = ReadSerializedStringField(
			declarations->arrayItems[index], "type");
		if (!schemas.ResolveValueType(typeId))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-VALUE-TYPE-UNREGISTERED",
				"GAF value declaration uses an unregistered TypeId", {},
				std::string(path) + "/" + std::to_string(index) + "/type" });
	}
}

bool BuildExtensionRegistries(
	const VansGAFProjectConfiguration* configuration,
	VansGAFTypeRegistry& types,
	VansGAFSchemaRegistry& schemas,
	std::string& error)
{
	if (!VansRegisterDefaultEngineGAFTypes(types, error) ||
		(configuration && !configuration->RegisterConfiguredTypes(types, error)) ||
		!types.Seal(error)) return false;
	schemas.BindTypes(types);
	return VansRegisterDefaultEngineGAFSchemas(schemas, error) &&
		(!configuration || configuration->RegisterConfiguredSchemas(schemas, error)) &&
		schemas.Seal(error);
}

std::string CookPolicyFingerprint(const VansGAFProjectConfiguration& configuration)
{
	std::string result = "|gaf-cook-policy|deterministic=" +
		std::to_string(configuration.settings.deterministicCook ? 1 : 0) +
		"|stripEditorMetadata=" +
		std::to_string(configuration.settings.stripEditorMetadata ? 1 : 0) +
		"|warningsAsErrors=" +
		std::to_string(configuration.settings.treatCookWarningsAsErrors ? 1 : 0);
	const auto appendSet = [&](const char* name, const std::unordered_set<std::string>& values)
	{
		std::vector<std::string> sorted(values.begin(), values.end());
		std::sort(sorted.begin(), sorted.end());
		result += "|";
		result += name;
		result += "=";
		for (const std::string& value : sorted) { result += value; result += ";"; }
	};
	appendSet("nodes", configuration.allowlist.nodeTypes);
	appendSet("modules", configuration.allowlist.modules);
	appendSet("capabilities", configuration.allowlist.capabilities);
	appendSet("policies", configuration.allowlist.policies);
	appendSet("guards", configuration.allowlist.guards);
	appendSet("operations", configuration.allowlist.operations);
	appendSet("drivers", configuration.allowlist.drivers);
	appendSet("extensions", configuration.allowlist.extensions);
	appendSet("transitions", configuration.allowlist.transitions);
	appendSet("signals", configuration.allowlist.signals);
	appendSet("valueTypes", configuration.allowlist.valueTypes);
	return result;
}

std::uint64_t CalculateCookedContentHash(
	const VansSerializedValue& runtimeDocument,
	const std::string& cookPolicyFingerprint)
{
	const nlohmann::ordered_json canonical =
		EncodeSerializedValueJson<nlohmann::ordered_json>(runtimeDocument);
	std::string hashInput = canonical.dump();
	hashInput += cookPolicyFingerprint;
	std::uint64_t contentHash = VansStableHash64(hashInput);
	return contentHash == 0 ? 1 : contentHash;
}

void Normalize(VansSerializedValue& value)
{
	if (value.kind == VansSerializedValue::Kind::Array)
	{
		for (VansSerializedValue& item : value.arrayItems) Normalize(item);
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	for (auto& field : value.objectFields) Normalize(field.second);
	std::stable_sort(value.objectFields.begin(), value.objectFields.end(), [](const auto& left, const auto& right)
	{
		return left.first < right.first;
	});
}

bool ErasePath(VansSerializedValue& root, const std::string& path)
{
	const std::vector<std::string> tokens = SplitSerializedPointer(path);
	if (tokens.empty()) return false;
	VansSerializedValue* parent = &root;
	for (std::size_t index = 0; index + 1 < tokens.size(); ++index)
	{
		parent = FindObjectField(*parent, tokens[index]);
		if (!parent) return false;
	}
	return EraseSerializedObjectField(*parent, tokens.back());
}

void StripNestedNonCooked(
	VansSerializedValue& value,
	const VansGameplayPropertySchema& schema)
{
	if (value.kind == VansSerializedValue::Kind::Array)
	{
		for (VansSerializedValue& item : value.arrayItems)
			StripNestedNonCooked(item, schema);
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	for (const VansGameplayPropertySchema& child : schema.children)
	{
		if (!child.cook)
		{
			EraseSerializedObjectField(value, child.path);
			continue;
		}
		if (VansSerializedValue* childValue = FindObjectField(value, child.path))
			StripNestedNonCooked(*childValue, child);
	}
}

void StripEditorMetadata(VansSerializedValue& value)
{
	if (value.kind == VansSerializedValue::Kind::Array)
	{
		for (VansSerializedValue& item : value.arrayItems) StripEditorMetadata(item);
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	EraseSerializedObjectField(value, "editor");
	EraseSerializedObjectField(value, "editorMetadata");
	EraseSerializedObjectField(value, "comments");
	for (auto& field : value.objectFields) StripEditorMetadata(field.second);
}

std::string AssetTypeName(VansAssetType type)
{
	if (const auto* descriptor = VansGameplayAssetSchemaRegistry::BuiltIns().Resolve(type))
		return descriptor->assetKind;
	return "Unknown";
}

template <typename T>
void AppendLittleEndian(std::string& bytes, T value)
{
	for (std::size_t index = 0; index < sizeof(T); ++index)
		bytes.push_back(static_cast<char>((value >> (index * 8)) & static_cast<T>(0xff)));
}

template <typename T>
bool ReadLittleEndian(const std::string& bytes, std::size_t& offset, T& value)
{
	if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return false;
	value = 0;
	for (std::size_t index = 0; index < sizeof(T); ++index)
		value |= static_cast<T>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8);
	offset += sizeof(T);
	return true;
}
}

bool VansGameplayAssetStorage::LoadSource(
	const std::filesystem::path& path,
	VansSerializedValue& root,
	std::string& error)
{
	VansJsonFileStorage::OrderedJson json;
	if (!VansJsonFileStorage::Read(path, json, error)) return false;
	root = DecodeSerializedValueJson(json);
	const VansAssetType type = VansAssetDatabase::Classify(path);
	if (!VansGameplayAssetSchemaRegistry::IsGameplayAssetType(type))
	{
		error = "file extension is not a GAF asset type";
		return false;
	}
	return true;
}

void VansGameplayAssetStorage::AppendProjectDiagnostics(
	VansAssetType type,
	const VansSerializedValue& source,
	const VansGAFProjectConfiguration& configuration,
	VansGameplayDiagnostics& diagnostics)
{
	ValidateProjectAllowlists(type, source, configuration, diagnostics);
}

void VansGameplayAssetStorage::AppendExtensionDiagnostics(
	VansAssetType type,
	const VansSerializedValue& source,
	const VansGAFSchemaRegistry& schemas,
	VansGameplayDiagnostics& diagnostics)
{
	if (type == VansAssetType::ActionSet)
	{
		ValidateTypedRecordArray(source, "/initializers", VansGAFExtensionKind::Extension,
			schemas, diagnostics);
		ValidateTypedRecordArray(source, "/policies", VansGAFExtensionKind::Extension,
			schemas, diagnostics);
		if (const VansSerializedValue* grants = FindSerializedPointer(source, "/grants");
			grants && grants->kind == VansSerializedValue::Kind::Array)
			for (std::size_t index = 0; index < grants->arrayItems.size(); ++index)
				ValidateTypedRecordArray(source,
					"/grants/" + std::to_string(index) + "/extensions",
					VansGAFExtensionKind::Extension, schemas, diagnostics);
		return;
	}
	if (type == VansAssetType::TargetingPolicy)
	{
		ValidateTypedRecordArray(source, "/steps", VansGAFExtensionKind::Operation,
			schemas, diagnostics);
		return;
	}
	if (type == VansAssetType::GameplayEffect)
	{
		ValidateTypedRecordArray(source, "/extensions", VansGAFExtensionKind::Extension,
			schemas, diagnostics);
		return;
	}
	if (type == VansAssetType::GameplayCue)
	{
		ValidateTypedRecordArray(source, "/bindings", VansGAFExtensionKind::Extension,
			schemas, diagnostics);
		return;
	}
	if (type != VansAssetType::ActionDefinition) return;
	ValidateValueDeclarations(source, "/context/schema", schemas, diagnostics);
	ValidateValueDeclarations(source, "/variables", schemas, diagnostics);
	ValidateTypedRecordArray(source, "/policies", VansGAFExtensionKind::Policy,
		schemas, diagnostics);
	for (const char* path : { "/phases/activate/guards", "/phases/commit/guards" })
		ValidateTypedRecordArray(source, path, VansGAFExtensionKind::Guard,
			schemas, diagnostics);
	for (const char* path : { "/phases/activate/operations", "/phases/commit/operations",
		"/phases/execute/operations", "/phases/finish/operations",
		"/phases/cancel/operations" })
		ValidateTypedRecordArray(source, path, VansGAFExtensionKind::Operation,
			schemas, diagnostics);
	ValidateTypedRecordArray(source, "/phases/execute/drivers",
		VansGAFExtensionKind::Driver, schemas, diagnostics);
	ValidateTypedRecordArray(source, "/transitions", VansGAFExtensionKind::Transition,
		schemas, diagnostics);
	ValidateTypedRecordArray(source, "/extensions", VansGAFExtensionKind::Extension,
		schemas, diagnostics);
}

bool VansGameplayAssetStorage::SaveSourceAtomic(
	const std::filesystem::path& path,
	const VansSerializedValue& root,
	std::string& error,
	const VansGAFProjectConfiguration* configuration)
{
	const VansAssetType type = VansAssetDatabase::Classify(path);
	const VansGameplayAssetSchemaRegistry& schemas = VansGameplayAssetSchemaRegistry::BuiltIns();
	if (!schemas.Resolve(type))
	{
		error = "file extension is not a registered GAF asset type";
		return false;
	}
	VansGameplayDiagnostics diagnostics = schemas.Validate(type, root);
	VansGAFTypeRegistry extensionTypes;
	VansGAFSchemaRegistry extensionSchemas;
	if (!BuildExtensionRegistries(configuration, extensionTypes, extensionSchemas, error))
	{
		error = "GAF extension registry construction failed: " + error;
		return false;
	}
	AppendExtensionDiagnostics(type, root, extensionSchemas, diagnostics);
	if (configuration)
	{
		AppendProjectDiagnostics(type, root, *configuration, diagnostics);
		configuration->ApplyValidationPolicy(diagnostics);
	}
	const bool blocked = configuration
		? configuration->HasBlockingDiagnostics(diagnostics, VansGAFValidationStage::Save)
		: HasErrors(diagnostics);
	if (blocked)
	{
		error = "GAF source asset failed validation";
		for (const VansGameplayDiagnostic& diagnostic : diagnostics)
			if (configuration
				? configuration->IsBlockingDiagnostic(diagnostic, VansGAFValidationStage::Save)
				: diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
					diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
			{
				error += ": " + diagnostic.code + " " + diagnostic.fieldPath + " " +
					diagnostic.message;
				break;
			}
		return false;
	}
	return VansJsonFileStorage::WriteAtomic(path,
		EncodeSerializedValueJson<VansJsonFileStorage::OrderedJson>(root), error);
}

VansGameplayCookResult VansGameplayAssetStorage::Cook(
	VansAssetType type,
	const VansSerializedValue& source,
	const VansGameplayAssetSchemaRegistry& schemas,
	const VansGAFProjectConfiguration* configuration,
	const VansGAFSchemaRegistry* extensionSchemas)
{
	VansGameplayCookResult result;
	result.asset.assetType = type;
	const VansGameplayAssetSchemaDescriptor* descriptor = schemas.Resolve(type);
	if (!descriptor)
	{
		result.error = "GAF Asset Schema is missing";
		return result;
	}
	if (descriptor->editorOnly)
	{
		result.error = "editor-only GAF asset cannot be cooked";
		return result;
	}
	VansSerializedValue currentSource = source;
	result.diagnostics = schemas.Validate(type, currentSource);
	VansGAFTypeRegistry localTypes;
	VansGAFSchemaRegistry localSchemas;
	if (!extensionSchemas)
	{
		if (!BuildExtensionRegistries(configuration, localTypes, localSchemas, result.error))
		{
			result.error = "GAF extension registry construction failed: " + result.error;
			return result;
		}
		extensionSchemas = &localSchemas;
	}
	AppendExtensionDiagnostics(type, currentSource, *extensionSchemas, result.diagnostics);
	if (configuration)
		AppendProjectDiagnostics(type, currentSource, *configuration, result.diagnostics);
	if (configuration) configuration->ApplyValidationPolicy(result.diagnostics);
	const bool blocked = configuration
		? configuration->HasBlockingDiagnostics(result.diagnostics, VansGAFValidationStage::Cook)
		: HasErrors(result.diagnostics);
	if (blocked)
	{
		result.error = "GAF asset validation failed before Cook";
		for (const VansGameplayDiagnostic& diagnostic : result.diagnostics)
			if (configuration
				? configuration->IsBlockingDiagnostic(diagnostic, VansGAFValidationStage::Cook)
				: diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
					diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
			{
				result.error += ": " + diagnostic.code + " " + diagnostic.fieldPath + " " +
					diagnostic.message;
				break;
			}
		return result;
	}
	result.asset.dependencies = schemas.CollectDependencies(type, currentSource);
	result.asset.runtimeDocument = std::move(currentSource);
	for (const VansGameplayPropertySchema& field : descriptor->fields)
	{
		if (!field.cook)
		{
			ErasePath(result.asset.runtimeDocument, field.path);
			continue;
		}
		if (VansSerializedValue* fieldValue = FindSerializedPointer(
			result.asset.runtimeDocument, field.path))
			StripNestedNonCooked(*fieldValue, field);
	}
	if (!configuration || configuration->settings.stripEditorMetadata)
		StripEditorMetadata(result.asset.runtimeDocument);
	if (!configuration || configuration->settings.deterministicCook)
		Normalize(result.asset.runtimeDocument);
	result.asset.cookPolicyFingerprint = configuration
		? CookPolicyFingerprint(*configuration) : std::string{};
	result.asset.contentHash = CalculateCookedContentHash(
		result.asset.runtimeDocument, result.asset.cookPolicyFingerprint);
	return result;
}

bool VansGameplayAssetStorage::SaveCookedAtomic(
	const std::filesystem::path& path,
	const VansGameplayCookedAsset& asset,
	std::string& error)
{
	if (!VansGameplayAssetSchemaRegistry::IsGameplayAssetType(asset.assetType) ||
		asset.assetType == VansAssetType::GAFEditorLayout ||
		asset.contentHash == 0)
	{
		error = "cooked GAF asset header is invalid";
		return false;
	}
	std::vector<std::string> dependencies = asset.dependencies;
	std::sort(dependencies.begin(), dependencies.end());
	dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
	nlohmann::ordered_json root = nlohmann::ordered_json::array();
	root.push_back(AssetTypeName(asset.assetType));
	root.push_back(asset.contentHash);
	root.push_back(std::move(dependencies));
	root.push_back(EncodeSerializedValueJson<nlohmann::ordered_json>(asset.runtimeDocument));
	root.push_back(asset.cookPolicyFingerprint);
	const std::vector<std::uint8_t> payloadBytes = nlohmann::ordered_json::to_cbor(root);
	if (payloadBytes.size() > static_cast<std::size_t>((std::numeric_limits<std::uint64_t>::max)()))
	{
		error = "cooked GAF asset payload is too large";
		return false;
	}
	const std::string payload(reinterpret_cast<const char*>(payloadBytes.data()), payloadBytes.size());
	std::string bytes;
	bytes.reserve(kCookedHeaderSize + payload.size());
	bytes.append(kCookedMagic.data(), kCookedMagic.size());
	AppendLittleEndian(bytes, static_cast<std::uint64_t>(payload.size()));
	AppendLittleEndian(bytes, VansStableHash64(payload));
	bytes.append(payload);
	return VansFileStorage::WriteAtomicBytes(path, bytes, error);
}

bool VansGameplayAssetStorage::LoadCooked(
	const std::filesystem::path& path,
	VansGameplayCookedAsset& asset,
	std::string& error)
{
	std::string bytes;
	if (!VansFileStorage::ReadAllBytes(path, bytes, error)) return false;
	if (bytes.size() < kCookedHeaderSize ||
		!std::equal(kCookedMagic.begin(), kCookedMagic.end(), bytes.begin()))
	{
		error = "cooked GAF asset header is invalid";
		return false;
	}
	std::size_t offset = kCookedMagic.size();
	std::uint64_t payloadSize = 0;
	std::uint64_t payloadHash = 0;
	if (!ReadLittleEndian(bytes, offset, payloadSize) ||
		!ReadLittleEndian(bytes, offset, payloadHash) ||
		payloadSize != bytes.size() - offset)
	{
		error = "cooked GAF asset container length is invalid";
		return false;
	}
	const std::string_view payload(bytes.data() + offset, static_cast<std::size_t>(payloadSize));
	if (payloadHash == 0 || VansStableHash64(payload) != payloadHash)
	{
		error = "cooked GAF asset payload hash is invalid";
		return false;
	}
	nlohmann::ordered_json root;
	try
	{
		root = nlohmann::ordered_json::from_cbor(payload.begin(), payload.end());
	}
	catch (const std::exception& exception)
	{
		error = std::string("cooked GAF asset CBOR is invalid: ") + exception.what();
		return false;
	}
	if (!root.is_array() || root.size() != 5 || !root[0].is_string() ||
		!root[1].is_number_unsigned() || !root[2].is_array() || !root[4].is_string())
	{
		error = "cooked GAF asset envelope is invalid";
		return false;
	}
	const auto* descriptor = VansGameplayAssetSchemaRegistry::BuiltIns().ResolveKind(
		root[0].get<std::string>());
	if (!descriptor || descriptor->editorOnly)
	{
		error = "cooked GAF assetKind is unknown or editor-only";
		return false;
	}
	asset = {};
	asset.assetType = descriptor->assetType;
	asset.contentHash = root[1].get<std::uint64_t>();
	asset.dependencies = root[2].get<std::vector<std::string>>();
	asset.runtimeDocument = DecodeSerializedValueJson(root[3]);
	asset.cookPolicyFingerprint = root[4].get<std::string>();
	if (asset.contentHash == 0)
	{
		error = "cooked GAF asset ContentHash is invalid";
		return false;
	}
	if (CalculateCookedContentHash(asset.runtimeDocument, asset.cookPolicyFingerprint) !=
		asset.contentHash)
	{
		error = "cooked GAF asset ContentHash verification failed";
		return false;
	}
	return true;
}
}
