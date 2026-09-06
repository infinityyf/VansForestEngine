#pragma once

#include "VansGameplayAssetCompiler.h"

#include "../AssetCore/VansAssetDatabase.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansAssetObjectRepository;
class VansGAFSchemaRegistry;

struct VansGameplayAssetSourceOverride
{
	std::filesystem::path sourcePath;
	VansSerializedValue document;
};

class VansGameplayAssetLibrary
{
public:
	bool Load(const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects, std::string& error);
	bool Load(
		const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects,
		const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
		std::string& error);
	bool Load(
		const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects,
		const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
		const VansGAFSchemaRegistry& extensionSchemas,
		std::string& error);
	bool Load(
		const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects,
		const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
		const VansGAFSchemaRegistry& extensionSchemas,
		const VansGameplayAssetCompilerRegistry& compilers,
		std::string& error);
	bool Load(
		const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects,
		const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
		const VansGameplayAssetSchemaRegistry& assetSchemas,
		const VansGAFSchemaRegistry& extensionSchemas,
		const VansGameplayAssetCompilerRegistry& compilers,
		std::string& error);
	void Clear();

	bool IsLoaded() const { return m_Loaded; }
	std::size_t AssetCount() const { return m_Entries.size(); }
	std::uint64_t ContentManifestHash() const { return m_ContentManifestHash; }
	const VansCompiledGameplayAsset* ResolveAsset(std::string_view reference) const;
	std::shared_ptr<const VansCompiledActionDefinition> ResolveAction(std::string_view reference) const;
	const VansActionSetDefinition* ResolveActionSet(std::string_view reference) const;
	const VansCompiledGameplayExtensionAsset* ResolveExtensionAsset(
		std::string_view reference,
		std::string_view typeId) const;
	std::vector<const VansCompiledGameplayExtensionAsset*> ExtensionAssets(
		std::string_view typeId) const;
	template <typename T>
	const T* ResolveExtensionAssetAs(
		std::string_view reference,
		std::string_view typeId) const
	{
		return VansResolveCompiledGameplayExtension<T>(
			ResolveExtensionAsset(reference, typeId), typeId);
	}

	const VansActionDefinitionRegistry& Actions() const { return m_Actions; }
	const VansGameplayTagDictionary& Tags() const { return m_Tags; }
	const VansAttributeRegistry& Attributes() const { return m_Attributes; }
	const VansEffectRegistry* Effects() const { return m_Effects.IsSealed() ? &m_Effects : nullptr; }
	const VansTargetingPolicyRegistry* TargetingPolicies() const
		{ return m_TargetingPolicies.IsSealed() ? &m_TargetingPolicies : nullptr; }
	const VansPayloadSchemaRegistry& PayloadSchemas() const { return m_PayloadSchemas; }
	const std::vector<VansCompiledGameplayCueDefinition>& Cues() const { return m_Cues; }

private:
	struct Entry
	{
		std::string guid;
		std::filesystem::path sourcePath;
		VansCompiledGameplayAsset asset;
	};

	const Entry* ResolveEntry(std::string_view reference) const;
	bool BuildRegistries(std::string& error);
	bool LinkReferences(std::string& error);

	std::vector<Entry> m_Entries;
	std::unordered_map<std::string, std::size_t> m_ByGuid;
	std::unordered_map<std::string, std::size_t> m_ByPath;
	std::unordered_map<VansActionSetId, std::size_t> m_ActionSets;
	std::vector<VansCompiledGameplayCueDefinition> m_Cues;
	std::unordered_map<VansCueId, std::size_t> m_CueIds;
	VansActionDefinitionRegistry m_Actions;
	VansGameplayTagDictionary m_Tags;
	VansAttributeRegistry m_Attributes;
	VansEffectRegistry m_Effects;
	VansTargetingPolicyRegistry m_TargetingPolicies;
	VansPayloadSchemaRegistry m_PayloadSchemas;
	std::uint64_t m_ContentManifestHash = 0;
	bool m_Loaded = false;
};
}
