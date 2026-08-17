#include "VansGameplayAssetStorage.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../RuntimeCore/VansStableIdentity.h"

#include <algorithm>
#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace Vans
{
namespace
{
constexpr std::array<char, 8> kCookedMagic{ 'V', 'G', 'A', 'F', 'C', 'B', '0', '1' };
constexpr std::uint32_t kCookedContainerVersion = 1;
constexpr std::size_t kCookedHeaderSize = 8 + 4 + 8 + 8;

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
	const auto hasReference = [](const VansSerializedValue* value)
	{
		if (!value) return false;
		if (value->kind == VansSerializedValue::Kind::String) return !value->stringValue.empty();
		if (value->kind != VansSerializedValue::Kind::Object) return false;
		for (const char* field : { "stableId", "id", "guid", "path", "assetGuid", "assetPath" })
			if (!ReadSerializedStringField(*value, field).empty()) return true;
		return false;
	};
	const auto validateBridge = [&](const std::string& bridge, const std::string& path)
	{
		if (!bridge.empty() && configuration.allowlist.bridges.find(bridge) ==
			configuration.allowlist.bridges.end())
			AddAllowlistDiagnostic(diagnostics, "GAF-PROJECT-BRIDGE-ALLOWLIST", bridge, path);
	};
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
				const std::string nodeType = ReadSerializedStringField(node, "type");
				const std::string nodeKind = ReadSerializedStringField(node, "kind", "Pure");
				const VansSerializedValue* properties =
					FindObjectField(node, "properties");
				const std::string service = properties
					? ReadSerializedStringField(*properties, "service") : std::string{};
				if (!service.empty() &&
					configuration.allowlist.services.find(service) ==
						configuration.allowlist.services.end())
					AddAllowlistDiagnostic(diagnostics, "GAF-PROJECT-SERVICE-ALLOWLIST",
						service, nodePath + "/properties/service");
				if (nodeType.compare(0, 7, "Camera.") == 0)
					validateBridge("Camera.Action", nodePath + "/type");
				const std::string bridge = properties
					? ReadSerializedStringField(*properties, "bridge") : std::string{};
				validateBridge(bridge, nodePath + "/properties/bridge");
				if (ReadSerializedStringField(node, "kind") == "Bridge" && bridge.empty())
					diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
						"GAF-GRAPH-BRIDGE-MISSING", "Bridge node must declare properties.bridge",
						{}, nodePath + "/properties/bridge" });
				const bool sideEffecting = nodeKind == "Command" || nodeKind == "Transaction" ||
					nodeKind == "Bridge";
				if (configuration.settings.predictionEnabled &&
					configuration.settings.requireRollbackPlan && sideEffecting &&
					ReadSerializedBoolField(node, "predictable", false) &&
					ReadSerializedStringField(node, "rollbackPlan", "None") == "None")
					diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
						"GAF-PROJECT-ROLLBACK-PLAN",
						"Predictable side-effecting node must declare Automatic or Compensate rollback",
						{}, nodePath + "/rollbackPlan" });
			}
	}
	else if (type == VansAssetType::ActionDefinition)
	{
		validateArrayField(FindSerializedPointer(source, "/dependencies/services"), nullptr,
			configuration.allowlist.services, "GAF-PROJECT-SERVICE-ALLOWLIST",
			"/dependencies/services");
		bool usesTimeline = hasReference(FindSerializedPointer(source, "/execution/timeline"));
		const VansSerializedValue* timelines = FindSerializedPointer(source, "/execution/timelines");
		if (timelines && timelines->kind == VansSerializedValue::Kind::Array)
			for (const VansSerializedValue& timeline : timelines->arrayItems)
				usesTimeline = usesTimeline || hasReference(&timeline);
		if (usesTimeline) validateBridge("Timeline.Action", "/execution/timeline");
		validateArrayField(FindSerializedPointer(source, "/extensions"), "bridge",
			configuration.allowlist.bridges, "GAF-PROJECT-BRIDGE-ALLOWLIST", "/extensions");
	}
	else if (type == VansAssetType::GameplayCue)
	{
		validateArrayField(FindSerializedPointer(source, "/adapters"), "service",
			configuration.allowlist.services, "GAF-PROJECT-SERVICE-ALLOWLIST", "/adapters");
	}
	else if (type == VansAssetType::TargetingPolicy)
	{
		validateArrayField(FindSerializedPointer(source, "/steps"), "handler",
			configuration.allowlist.handlers, "GAF-PROJECT-HANDLER-ALLOWLIST", "/steps");
	}
}

std::string CookPolicyFingerprint(const VansGAFProjectConfiguration& configuration)
{
	std::string result = "|gaf-cook-policy-v1|deterministic=" +
		std::to_string(configuration.settings.deterministicCook ? 1 : 0) +
		"|stripEditorMetadata=" +
		std::to_string(configuration.settings.stripEditorMetadata ? 1 : 0) +
		"|warningsAsErrors=" +
		std::to_string(configuration.settings.treatCookWarningsAsErrors ? 1 : 0) +
		"|prediction=" + std::to_string(configuration.settings.predictionEnabled ? 1 : 0) +
		"|requireRollback=" +
		std::to_string(configuration.settings.requireRollbackPlan ? 1 : 0);
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
	appendSet("services", configuration.allowlist.services);
	appendSet("handlers", configuration.allowlist.handlers);
	appendSet("bridges", configuration.allowlist.bridges);
	return result;
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
	const VansGAFProjectConfiguration* configuration)
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
	VansSerializedValue migratedSource = source;
	if (!VansGameplayAssetMigrationRegistry::BuiltIns().Migrate(
		type, descriptor->schemaVersion, migratedSource, result.migrations, result.error))
		return result;
	result.diagnostics = schemas.Validate(type, migratedSource);
	if (configuration)
		AppendProjectDiagnostics(type, migratedSource, *configuration, result.diagnostics);
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
	result.asset.schemaVersion = descriptor->schemaVersion;
	result.asset.dependencies = schemas.CollectDependencies(type, migratedSource);
	result.asset.runtimeDocument = std::move(migratedSource);
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
	const nlohmann::ordered_json canonical =
		EncodeSerializedValueJson<nlohmann::ordered_json>(result.asset.runtimeDocument);
	std::string hashInput = canonical.dump();
	if (configuration) hashInput += CookPolicyFingerprint(*configuration);
	result.asset.contentHash = VansStableHash64(hashInput);
	if (result.asset.contentHash == 0) result.asset.contentHash = 1;
	return result;
}

bool VansGameplayAssetStorage::SaveCookedAtomic(
	const std::filesystem::path& path,
	const VansGameplayCookedAsset& asset,
	std::string& error)
{
	if (!VansGameplayAssetSchemaRegistry::IsGameplayAssetType(asset.assetType) ||
		asset.assetType == VansAssetType::GAFEditorLayout || asset.schemaVersion == 0 ||
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
	root.push_back(asset.schemaVersion);
	root.push_back(asset.contentHash);
	root.push_back(std::move(dependencies));
	root.push_back(EncodeSerializedValueJson<nlohmann::ordered_json>(asset.runtimeDocument));
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
	AppendLittleEndian(bytes, kCookedContainerVersion);
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
	std::uint32_t containerVersion = 0;
	std::uint64_t payloadSize = 0;
	std::uint64_t payloadHash = 0;
	if (!ReadLittleEndian(bytes, offset, containerVersion) ||
		!ReadLittleEndian(bytes, offset, payloadSize) ||
		!ReadLittleEndian(bytes, offset, payloadHash) ||
		containerVersion != kCookedContainerVersion || payloadSize != bytes.size() - offset)
	{
		error = "cooked GAF asset container version or length is invalid";
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
		!root[1].is_number_unsigned() || !root[2].is_number_unsigned() ||
		!root[3].is_array())
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
	asset.schemaVersion = root[1].get<std::uint32_t>();
	asset.contentHash = root[2].get<std::uint64_t>();
	asset.dependencies = root[3].get<std::vector<std::string>>();
	asset.runtimeDocument = DecodeSerializedValueJson(root[4]);
	if (asset.schemaVersion != descriptor->schemaVersion || asset.contentHash == 0)
	{
		error = "cooked GAF asset version or ContentHash is invalid";
		return false;
	}
	VansGameplayCookResult verification = Cook(asset.assetType, asset.runtimeDocument);
	if (!verification || verification.asset.contentHash != asset.contentHash)
	{
		error = "cooked GAF asset ContentHash verification failed";
		return false;
	}
	return true;
}
}
