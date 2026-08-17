#pragma once

#include "VansGameplayAssetCompiler.h"

#include "../AssetCore/VansAssetDatabase.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansGameplayAssetSourceOverride
{
	std::filesystem::path sourcePath;
	VansSerializedValue document;
};

class VansGameplayAssetLibrary
{
public:
	bool Load(const std::vector<VansAssetRecord>& records, std::string& error);
	bool Load(
		const std::vector<VansAssetRecord>& records,
		const std::vector<VansGameplayAssetSourceOverride>& sourceOverrides,
		std::string& error);
	void Clear();

	bool IsLoaded() const { return m_Loaded; }
	bool ValidatePredictionRollbackPolicy(std::string& error) const;
	std::size_t AssetCount() const { return m_Entries.size(); }
	std::uint64_t ContentManifestHash() const { return m_ContentManifestHash; }
	const VansCompiledGameplayAsset* ResolveAsset(std::string_view reference) const;
	std::shared_ptr<const VansCompiledActionDefinition> ResolveAction(std::string_view reference) const;
	const VansActionSetDefinition* ResolveActionSet(std::string_view reference) const;
	const VansCameraRigDefinition* ResolveCameraRig(std::string_view reference) const;
	const VansCameraShakeDefinition* ResolveCameraShake(std::string_view reference) const;

	const VansActionDefinitionRegistry& Actions() const { return m_Actions; }
	const VansGameplayTagDictionary& Tags() const { return m_Tags; }
	const VansAttributeRegistry& Attributes() const { return m_Attributes; }
	const VansEffectRegistry* Effects() const { return m_Effects.IsSealed() ? &m_Effects : nullptr; }
	const VansTargetingPolicyRegistry* TargetingPolicies() const
		{ return m_TargetingPolicies.IsSealed() ? &m_TargetingPolicies : nullptr; }
	const VansPayloadSchemaRegistry& PayloadSchemas() const { return m_PayloadSchemas; }
	const std::vector<VansCompiledGameplayCueDefinition>& Cues() const { return m_Cues; }
	const std::vector<VansCameraRigDefinition>& CameraRigs() const { return m_CameraRigs; }
	const std::vector<VansCameraShakeDefinition>& CameraShakes() const { return m_CameraShakes; }

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
	std::unordered_map<VansCameraRigId, std::size_t> m_CameraRigIds;
	std::unordered_map<VansCameraShakeId, std::size_t> m_CameraShakeIds;
	std::vector<VansCameraRigDefinition> m_CameraRigs;
	std::vector<VansCameraShakeDefinition> m_CameraShakes;
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
