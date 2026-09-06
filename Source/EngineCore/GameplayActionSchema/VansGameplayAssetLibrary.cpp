#include "VansGameplayAssetLibrary.h"

#include "VansGameplayAssetStorage.h"
#include "../GameplayActionCore/VansGAFExtensionRegistry.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/VansAssetObjectRepository.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
namespace
{
std::string NormalizePath(std::filesystem::path path)
{
	std::string value = path.lexically_normal().generic_string();
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return value;
}

std::string AssetReference(const VansSerializedValue* value)
{
	if (!value) return {};
	if (value->kind == VansSerializedValue::Kind::String) return value->stringValue;
	if (value->kind != VansSerializedValue::Kind::Object) return {};
	for (const char* field : { "stableId", "id", "guid", "path", "assetGuid", "assetPath" })
	{
		const std::string reference = ReadSerializedStringField(*value, field);
		if (!reference.empty()) return reference;
	}
	return {};
}

}

void VansGameplayAssetLibrary::Clear()
{
	m_Entries.clear();
	m_ByGuid.clear();
	m_ByPath.clear();
	m_ActionSets.clear();
	m_Cues.clear();
	m_CueIds.clear();
	m_Actions = {};
	m_Tags = {};
	m_Attributes = {};
	m_Effects = {};
	m_TargetingPolicies = {};
	m_PayloadSchemas = {};
	m_ContentManifestHash = 0;
	m_Loaded = false;
}

bool VansGameplayAssetLibrary::Load(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	std::string& error)

{
	return Load(records, assetObjects, {}, error);
}

bool VansGameplayAssetLibrary::Load(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
	std::string& error)
{
	VansGAFTypeRegistry types;
	VansGAFSchemaRegistry schemas;
	if (!VansRegisterDefaultEngineGAFTypes(types, error) || !types.Seal(error)) return false;
	schemas.BindTypes(types);
	if (!VansRegisterDefaultEngineGAFSchemas(schemas, error) ||
		!schemas.Seal(error)) return false;
	VansGameplayAssetCompilerRegistry compilers;
	if (!VansRegisterDefaultGameplayAssetCompilers(compilers, error) ||
		!compilers.Seal(error)) return false;
	return Load(records, assetObjects, sourceOverrides, schemas, compilers, error);
}

bool VansGameplayAssetLibrary::Load(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
	const VansGAFSchemaRegistry& extensionSchemas,
	std::string& error)
{
	VansGameplayAssetCompilerRegistry compilers;
	if (!VansRegisterDefaultGameplayAssetCompilers(compilers, error) ||
		!compilers.Seal(error)) return false;
	return Load(records, assetObjects, sourceOverrides, extensionSchemas, compilers, error);
}

bool VansGameplayAssetLibrary::Load(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
	const VansGAFSchemaRegistry& extensionSchemas,
	const VansGameplayAssetCompilerRegistry& compilers,
	std::string& error)
{
	return Load(records, assetObjects, sourceOverrides,
		VansGameplayAssetSchemaRegistry::BuiltIns(), extensionSchemas, compilers, error);
}

bool VansGameplayAssetLibrary::Load(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
	const VansGameplayAssetSchemaRegistry& assetSchemas,
	const VansGAFSchemaRegistry& extensionSchemas,
	const VansGameplayAssetCompilerRegistry& compilers,
	std::string& error)
{
	Clear();
	std::unordered_map<std::string, const VansSerializedValue*> overrides;
	for (const VansGameplayAssetSourceOverride& sourceOverride : sourceOverrides)
	{
		if (sourceOverride.sourcePath.empty() || !overrides.emplace(
			NormalizePath(sourceOverride.sourcePath), &sourceOverride.document).second)
		{
			error = "Gameplay asset source overrides contain an empty or duplicate path";
			return false;
		}
	}
	std::vector<VansAssetRecord> ordered;
	for (const VansAssetRecord& record : records)
		if (record.state != VansAssetState::Missing &&
			assetSchemas.Resolve(record.type) &&
			!assetSchemas.Resolve(record.type)->editorOnly)
			ordered.push_back(record);
	std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
	{
		return left.guid.ToString() < right.guid.ToString();
	});
	for (const VansAssetRecord& record : ordered)
	{
		VansGameplayCookedAsset cooked;
		const auto findOverride = [&](const std::filesystem::path& path)
			-> const VansSerializedValue*
		{
			if (path.empty()) return nullptr;
			const auto found = overrides.find(NormalizePath(path));
			return found == overrides.end() ? nullptr : found->second;
		};
		const VansSerializedValue* sourceOverride = findOverride(record.authoringPath);
		if (!sourceOverride) sourceOverride = findOverride(record.sourcePath);
		if (sourceOverride)
		{
			VansGameplayCookResult cookResult =
				VansGameplayAssetStorage::Cook(record.type, *sourceOverride,
					assetSchemas, nullptr, &extensionSchemas);
			if (!cookResult)
			{
				error = record.sourcePath.string() + ": " + cookResult.error;
				Clear();
				return false;
			}
			cooked = std::move(cookResult.asset);
		}
		else
		{
			const std::shared_ptr<const VansGameplayAssetMemoryObject> memoryAsset =
				assetObjects.ResolveLatest<VansGameplayAssetMemoryObject>(record.guid);
			if (!memoryAsset)
			{
				error = record.guid.ToString() + ": GAF memory object is unavailable";
				Clear();
				return false;
			}
			if (memoryAsset->hasCookedAsset)
			{
				cooked = memoryAsset->cookedAsset;
			}
			else
			{
				VansGameplayCookResult cookResult = VansGameplayAssetStorage::Cook(
					record.type, memoryAsset->sourceDocument,
					assetSchemas, nullptr, &extensionSchemas);
				if (!cookResult)
				{
					error = memoryAsset->sourcePath.string() + ": " + cookResult.error;
					Clear();
					return false;
				}
				cooked = std::move(cookResult.asset);
			}
		}
		if (cooked.assetType != record.type)
		{
			error = "GAF asset index type does not match cooked data: " + record.guid.ToString();
			Clear();
			return false;
		}
		VansGameplayDiagnostics extensionDiagnostics;
		VansGameplayAssetStorage::AppendExtensionDiagnostics(
			record.type, cooked.runtimeDocument, extensionSchemas, extensionDiagnostics);
		for (const VansGameplayDiagnostic& diagnostic : extensionDiagnostics)
			if (diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
				diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
			{
				error = record.guid.ToString() + ": " + diagnostic.code + " " +
					diagnostic.fieldPath + " " + diagnostic.message;
				Clear();
				return false;
			}
		VansGameplayCompileResult compileResult =
			VansGameplayAssetCompiler::Compile(cooked, compilers);
		if (!compileResult)
		{
			error = record.guid.ToString() + ": " + compileResult.error;
			Clear();
			return false;
		}
		Entry entry;
		entry.guid = record.guid.ToString();
		entry.sourcePath = record.sourcePath;
		entry.asset = std::move(compileResult.asset);
		const std::size_t index = m_Entries.size();
		if (!m_ByGuid.emplace(entry.guid, index).second)
		{
			error = "duplicate GAF asset GUID: " + entry.guid;
			Clear();
			return false;
		}
		if (!entry.sourcePath.empty())
			m_ByPath.emplace(NormalizePath(entry.sourcePath), index);
		m_Entries.push_back(std::move(entry));
	}
	std::uint64_t manifest = 1469598103934665603ull;
	for (const Entry& entry : m_Entries)
	{
		for (unsigned char character : entry.guid) { manifest ^= character; manifest *= 1099511628211ull; }
		for (std::size_t byte = 0; byte < sizeof(entry.asset.contentHash); ++byte)
		{
			manifest ^= static_cast<std::uint8_t>(entry.asset.contentHash >> (byte * 8u));
			manifest *= 1099511628211ull;
		}
	}
	m_ContentManifestHash = manifest;
	if (!BuildRegistries(error) || !LinkReferences(error))
	{
		Clear();
		return false;
	}
	m_Loaded = true;
	return true;
}

bool VansGameplayAssetLibrary::BuildRegistries(std::string& error)
{
	bool hasTargetingPolicies = false;
	std::unordered_map<VansGameplayTagId, std::string> tagNames;
	for (const Entry& entry : m_Entries)
		if (const auto* tree = std::get_if<VansCompiledGameplayTagTreeDefinition>(&entry.asset.data))
			for (const VansGameplayTagDefinition& definition : tree->tags)
				tagNames.emplace(definition.id, definition.name);
	for (const Entry& entry : m_Entries)
	{
		if (const auto* tree = std::get_if<VansCompiledGameplayTagTreeDefinition>(&entry.asset.data))
		{
			for (const VansGameplayTagDefinition& definition : tree->tags)
			{
				const auto replacement = tagNames.find(definition.replacement);
				if (!m_Tags.Register(definition.name, definition.description, definition.deprecated,
					replacement == tagNames.end() ? std::string{} : replacement->second, error)) return false;
			}
		}
		else if (const auto* set = std::get_if<VansCompiledAttributeSetDefinition>(&entry.asset.data))
		{
			for (const VansAttributeDefinition& definition : set->attributes)
				if (!m_Attributes.Register(definition, error)) return false;
		}
		else if (const auto* schema = std::get_if<VansPayloadSchema>(&entry.asset.data))
		{
			if (!m_PayloadSchemas.Register(*schema, error)) return false;
		}
		else if (const auto* cue = std::get_if<VansCompiledGameplayCueDefinition>(&entry.asset.data))
		{
			const std::size_t index = m_Cues.size();
			if (!m_CueIds.emplace(cue->id, index).second)
			{
				error = "duplicate Gameplay Cue id: " + cue->name;
				return false;
			}
			m_Cues.push_back(*cue);
		}
		else if (const auto* policy = std::get_if<VansTargetingPolicy>(&entry.asset.data))
		{
			hasTargetingPolicies = true;
			if (!m_TargetingPolicies.Register(*policy, error)) return false;
		}
	}
	return m_Tags.Seal(error) && m_Attributes.Seal(error) && m_PayloadSchemas.Seal(error) &&
		(!hasTargetingPolicies || m_TargetingPolicies.Seal(error));
}

bool VansGameplayAssetLibrary::LinkReferences(std::string& error)
{
	const auto resolveCue = [&](std::string_view reference, VansCueId fallback,
		const std::string& owner, VansCueId& output)
	{
		if (!reference.empty())
		{
			if (const Entry* target = ResolveEntry(reference))
			{
				const auto* definition = std::get_if<VansCompiledGameplayCueDefinition>(
					&target->asset.data);
				if (!definition)
				{
					error = owner + " references an asset that is not a Gameplay Cue: " +
						std::string(reference);
					return false;
				}
				output = definition->id;
			}
			else output = VansMakeStableId<VansCueIdTag>(reference);
		}
		else output = fallback;
		if (m_CueIds.find(output) != m_CueIds.end()) return true;
		error = owner + " references an unknown Gameplay Cue";
		return false;
	};
	const auto resolveCueList = [&](const std::vector<std::string>& references,
		std::vector<VansCueId>& cues, const std::string& owner)
	{
		for (std::size_t index = 0; index < cues.size(); ++index)
		{
			const std::string_view reference = index < references.size()
				? std::string_view(references[index]) : std::string_view{};
			if (!resolveCue(reference, cues[index], owner, cues[index])) return false;
		}
		return true;
	};
	const auto resolveAction = [&](std::string_view reference, VansActionId fallback,
		const std::string& owner, VansActionId& output)
	{
		if (!reference.empty())
		{
			if (const Entry* target = ResolveEntry(reference))
			{
				const auto* definition = std::get_if<
					std::shared_ptr<const VansCompiledActionDefinition>>(&target->asset.data);
				if (!definition || !*definition)
				{
					error = owner + " references an asset that is not an Action: " +
						std::string(reference);
					return false;
				}
				output = (*definition)->id;
				return true;
			}
			fallback = VansMakeStableId<VansActionIdTag>(reference);
		}
		if (!fallback) return true;
		for (const Entry& candidate : m_Entries)
		{
			const auto* definition = std::get_if<
				std::shared_ptr<const VansCompiledActionDefinition>>(&candidate.asset.data);
			if (definition && *definition && (*definition)->id == fallback)
			{
				output = fallback;
				return true;
			}
		}
		error = owner + " references an unknown Action";
		return false;
	};
	bool hasEffects = false;
	for (Entry& entry : m_Entries)
	{
		if (auto* effect = std::get_if<std::shared_ptr<const VansEffectDefinition>>(
			&entry.asset.data); effect && *effect)
		{
			hasEffects = true;
			auto definition = std::make_shared<VansEffectDefinition>(**effect);
			const std::string owner = "Gameplay Effect " + definition->name;
			if (!resolveCueList(definition->executeCueReferences,
				definition->executeCues, owner) ||
				!resolveCueList(definition->persistentCueReferences,
					definition->persistentCues, owner) ||
				!resolveCueList(definition->periodicCueReferences,
					definition->periodicCues, owner) ||
				!resolveCueList(definition->removeCueReferences,
					definition->removeCues, owner)) return false;
			for (const VansEffectModifier& modifier : definition->modifiers)
				if (!m_Attributes.Resolve(modifier.attribute))
				{
					error = "Gameplay Effect references an unknown Attribute: " + definition->name;
					return false;
				}
			*effect = definition;
			if (!m_Effects.Register(std::move(definition), error)) return false;
		}
	}
	if (hasEffects && !m_Effects.Seal(error)) return false;

	for (Entry& entry : m_Entries)
	{
		auto* actionPointer = std::get_if<std::shared_ptr<const VansCompiledActionDefinition>>(
			&entry.asset.data);
		if (!actionPointer || !*actionPointer) continue;
		auto action = std::make_shared<VansCompiledActionDefinition>(**actionPointer);
		for (VansCompiledActionRecord& operation : action->program.activate.operations)
		{
			if (operation.type != "Gameplay.Targeting.Resolve") continue;
			VansSerializedValue* asset = FindObjectField(operation.inputs, "asset");
			const std::string reference = AssetReference(asset);
			if (const Entry* target = ResolveEntry(reference))
			{
				const auto* policy = std::get_if<VansTargetingPolicy>(&target->asset.data);
				if (!policy)
				{
					error = "Action TargetingPolicy reference has the wrong asset type: " +
						action->name;
					return false;
				}
				*asset = VansSerializedValue::String(policy->name);
			}
			else if (!m_TargetingPolicies.IsSealed() || !m_TargetingPolicies.Resolve(
				VansMakeStableId<VansTargetingPolicyIdTag>(reference)))
			{
				error = "Action TargetingPolicy reference is unresolved: " + action->name;
				return false;
			}
		}
		for (VansCompiledActionRecord& operation : action->program.commit.operations)
		{
			if (operation.type != "Gameplay.Effects.Apply") continue;
			VansSerializedValue* asset = FindObjectField(operation.inputs, "asset");
			const std::string reference = AssetReference(asset);
			if (const Entry* target = ResolveEntry(reference))
			{
				const auto* definition = std::get_if<std::shared_ptr<const VansEffectDefinition>>(
					&target->asset.data);
				if (!definition || !*definition)
				{
					error = "Action Effect reference has the wrong asset type: " + reference;
					return false;
				}
				*asset = VansSerializedValue::String((*definition)->name);
			}
			else if (!m_Effects.Resolve(VansMakeStableId<VansEffectIdTag>(reference)))
			{
				error = "Action Effect reference is unresolved: " + reference;
				return false;
			}
		}
		for (VansCompiledActionRecord& operation : action->program.execute.operations)
		{
			if (operation.type != "Gameplay.Cue.Emit") continue;
			VansSerializedValue* assets = FindObjectField(operation.inputs, "assets");
			if (!assets || assets->kind != VansSerializedValue::Kind::Array) continue;
			for (VansSerializedValue& asset : assets->arrayItems)
			{
				const std::string reference = AssetReference(&asset);
				if (const Entry* target = ResolveEntry(reference))
				{
					const auto* cue = std::get_if<VansCompiledGameplayCueDefinition>(
						&target->asset.data);
					if (!cue)
					{
						error = "Action Cue reference has the wrong asset type: " + reference;
						return false;
					}
					asset = VansSerializedValue::String(cue->name);
				}
				else if (m_CueIds.find(VansMakeStableId<VansCueIdTag>(reference)) == m_CueIds.end())
				{
					error = "Action Cue reference is unresolved: " + reference;
					return false;
				}
			}
		}
		const auto resolveActionList = [&](const std::vector<std::string>& references,
			std::vector<VansActionId>& actions, std::string_view field)
		{
			for (std::size_t index = 0; index < actions.size(); ++index)
			{
				const std::string_view reference = index < references.size()
					? std::string_view(references[index]) : std::string_view{};
				if (!resolveAction(reference, actions[index],
					"Action " + action->name + " " + std::string(field), actions[index]))
					return false;
			}
			return true;
		};
		if (!resolveActionList(action->blockedActionReferences,
			action->blockedActions, "blockedActions") ||
			!resolveActionList(action->cancelActionReferences,
				action->cancelActions, "cancelActions")) return false;
		const auto resolveActionRecordReference = [&](VansCompiledActionRecord& record,
			const char* field, const std::string& owner)
		{
			VansSerializedValue* value = FindObjectField(record.inputs, field);
			const std::string reference = AssetReference(value);
			if (reference.empty()) return true;
			VansActionId resolved;
			if (!resolveAction(reference, {}, owner, resolved)) return false;
			for (const Entry& candidate : m_Entries)
			{
				const auto* definition = std::get_if<
					std::shared_ptr<const VansCompiledActionDefinition>>(&candidate.asset.data);
				if (definition && *definition && (*definition)->id == resolved)
				{
					*value = VansSerializedValue::String((*definition)->name);
					return true;
				}
			}
			error = owner + " could not canonicalize its Action reference";
			return false;
		};
		for (VansCompiledActionRecord& transition : action->program.transitions)
			if (!resolveActionRecordReference(transition, "target",
				"Action " + action->name + " transition")) return false;
		for (VansCompiledActionRecord& policy : action->program.policies)
			if (policy.type == "Core.Policy.Failure" &&
				!resolveActionRecordReference(policy, "action",
					"Action " + action->name + " failure fallback")) return false;
		if (!action->executionGraphAsset.empty())
		{
			const Entry* target = ResolveEntry(action->executionGraphAsset);
			const auto* graph = target ? std::get_if<std::shared_ptr<const VansCompiledActionGraph>>(
				&target->asset.data) : nullptr;
			if (!graph || !*graph)
			{
				error = "Action Graph reference is unresolved or has the wrong asset type: " +
					action->executionGraphAsset;
				return false;
			}
			action->executionGraph = *graph;
		}
		if (action->executor == VansMakeStableId<VansActionExecutorIdTag>(ActionExecutorNames::Graph) &&
			!action->executionGraph)
		{
			error = "Graph Action Executor requires an Action Graph asset: " + action->name;
			return false;
		}
		*actionPointer = action;
		if (!m_Actions.Register(std::move(action), error)) return false;
	}

	for (std::size_t entryIndex = 0; entryIndex < m_Entries.size(); ++entryIndex)
	{
		Entry& entry = m_Entries[entryIndex];
		auto* set = std::get_if<VansActionSetDefinition>(&entry.asset.data);
		if (!set) continue;
		for (VansActionGrantDesc& grant : set->grants)
		{
			if (const Entry* target = ResolveEntry(grant.actionReference))
			{
				const auto* definition = std::get_if<std::shared_ptr<const VansCompiledActionDefinition>>(
					&target->asset.data);
				if (!definition || !*definition)
				{
					error = "ActionSet grant reference has the wrong asset type: " + grant.actionReference;
					return false;
				}
				grant.action = (*definition)->id;
			}
			if (!m_Actions.Resolve(grant.action))
			{
				error = "ActionSet grant is unresolved: " + grant.actionReference;
				return false;
			}
			for (const VansCompiledActionRecord& extension : grant.extensions)
				if (extension.type == "Gameplay.Tags.Dynamic")
				{
					const VansSerializedValue* tags = FindObjectField(extension.inputs, "tags");
					if (!tags || tags->kind != VansSerializedValue::Kind::Array)
					{
						error = "ActionSet dynamic Tag extension is invalid: " + set->name;
						return false;
					}
					for (const VansSerializedValue& tag : tags->arrayItems)
						if (tag.kind != VansSerializedValue::Kind::String ||
							!m_Tags.Find(tag.stringValue))
						{
							error = "ActionSet dynamic Tag is unresolved: " +
								ReadSerializedString(tag);
							return false;
						}
				}
		}
		std::unordered_set<VansAttributeId> initializedAttributes;
		for (VansCompiledActionRecord& initializer : set->initializers)
		{
			if (initializer.type == "Gameplay.Effects.Initialize")
			{
				VansSerializedValue* asset = FindObjectField(initializer.inputs, "asset");
				const std::string reference = AssetReference(asset);
				if (const Entry* target = ResolveEntry(reference))
				{
					const auto* definition = std::get_if<std::shared_ptr<const VansEffectDefinition>>(
						&target->asset.data);
					if (!definition || !*definition)
					{
						error = "ActionSet Effect reference has the wrong asset type: " + reference;
						return false;
					}
					*asset = VansSerializedValue::String((*definition)->name);
				}
				const std::string stableName = AssetReference(asset);
				if (!m_Effects.Resolve(VansMakeStableId<VansEffectIdTag>(stableName)))
				{
					error = "ActionSet Effect reference is unresolved: " + reference;
					return false;
				}
			}
			else if (initializer.type == "Gameplay.Attributes.Initialize")
			{
				const std::string attributeName =
					ReadSerializedStringField(initializer.inputs, "attribute");
				const VansAttributeId attribute =
					VansMakeStableId<VansAttributeIdTag>(attributeName);
				const VansSerializedValue* value = FindObjectField(initializer.inputs, "value");
				if (attributeName.empty() || !value ||
					!std::isfinite(ReadSerializedNumber(*value)) ||
					!m_Attributes.Resolve(attribute) ||
					!initializedAttributes.insert(attribute).second)
				{
					error = "ActionSet Attribute initializer is invalid: " + set->name;
					return false;
				}
			}
		}
		if (!m_ActionSets.emplace(set->id, entryIndex).second)
		{
			error = "duplicate ActionSet id: " + set->name;
			return false;
		}
	}
	return true;
}

const VansGameplayAssetLibrary::Entry* VansGameplayAssetLibrary::ResolveEntry(
	std::string_view reference) const
{
	if (reference.empty()) return nullptr;
	if (const auto found = m_ByGuid.find(std::string(reference)); found != m_ByGuid.end())
		return &m_Entries[found->second];
	const auto path = m_ByPath.find(NormalizePath(std::filesystem::path(reference)));
	return path == m_ByPath.end() ? nullptr : &m_Entries[path->second];
}

const VansCompiledGameplayAsset* VansGameplayAssetLibrary::ResolveAsset(
	std::string_view reference) const
{
	const Entry* entry = ResolveEntry(reference);
	return entry ? &entry->asset : nullptr;
}

std::shared_ptr<const VansCompiledActionDefinition> VansGameplayAssetLibrary::ResolveAction(
	std::string_view reference) const
{
	if (const Entry* entry = ResolveEntry(reference))
		if (const auto* action = std::get_if<std::shared_ptr<const VansCompiledActionDefinition>>(
			&entry->asset.data)) return *action;
	return m_Actions.Resolve(VansMakeStableId<VansActionIdTag>(reference));
}

const VansActionSetDefinition* VansGameplayAssetLibrary::ResolveActionSet(
	std::string_view reference) const
{
	if (const Entry* entry = ResolveEntry(reference))
		return std::get_if<VansActionSetDefinition>(&entry->asset.data);
	const auto found = m_ActionSets.find(VansMakeStableId<VansActionSetIdTag>(reference));
	return found == m_ActionSets.end()
		? nullptr : std::get_if<VansActionSetDefinition>(&m_Entries[found->second].asset.data);
}

const VansCompiledGameplayExtensionAsset* VansGameplayAssetLibrary::ResolveExtensionAsset(
	std::string_view reference,
	std::string_view typeId) const
{
	if (const Entry* entry = ResolveEntry(reference))
		if (const auto* extension = std::get_if<VansCompiledGameplayExtensionAsset>(
			&entry->asset.data); extension && extension->typeId == typeId) return extension;
	for (const Entry& entry : m_Entries)
		if (const auto* extension = std::get_if<VansCompiledGameplayExtensionAsset>(
			&entry.asset.data); extension && extension->typeId == typeId &&
			extension->stableName == reference) return extension;
	return nullptr;
}

std::vector<const VansCompiledGameplayExtensionAsset*> VansGameplayAssetLibrary::ExtensionAssets(
	std::string_view typeId) const
{
	std::vector<const VansCompiledGameplayExtensionAsset*> result;
	for (const Entry& entry : m_Entries)
		if (const auto* extension = std::get_if<VansCompiledGameplayExtensionAsset>(
			&entry.asset.data); extension && extension->typeId == typeId)
			result.push_back(extension);
	return result;
}
}
