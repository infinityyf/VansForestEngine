#include "VansGameplayAssetPackageCooker.h"

#include "../GameplayActionSchema/VansGameplayAssetCompiler.h"
#include "../GameplayActionSchema/VansGameplayAssetStorage.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <unordered_set>

namespace Vans
{
namespace
{
std::optional<VansAssetRecord> FindAsset(
	VansAssetDatabase& projectDatabase,
	VansAssetDatabase* builtInDatabase,
	const std::string& reference)
{
	VansAssetGuid guid;
	if (VansAssetGuid::TryParse(reference, guid))
	{
		if (auto record = projectDatabase.Find(guid)) return record;
		return builtInDatabase ? builtInDatabase->Find(guid) : std::nullopt;
	}
	if (auto record = projectDatabase.Find(reference)) return record;
	return builtInDatabase ? builtInDatabase->Find(reference) : std::nullopt;
}

std::filesystem::path ArtifactPathFor(
	const std::filesystem::path& projectRoot,
	const VansAssetRecord& record)
{
	return projectRoot / "Library/Artifacts/GAF" / record.guid.ToString() /
		(record.sourcePath.filename().string() + ".gafcooked");
}
}

VansGameplayPackageCookResult VansGameplayAssetPackageCooker::CookClosure(
	const std::filesystem::path& projectRoot,
	VansAssetDatabase& projectDatabase,
	VansAssetDatabase* builtInDatabase,
	const std::vector<std::string>& seedAssetGuids,
	const VansGAFProjectConfiguration* configuration)
{
	VansGameplayPackageCookResult result;
	std::deque<std::string> pending(seedAssetGuids.begin(), seedAssetGuids.end());
	std::unordered_set<std::string> visitedReferences;
	std::unordered_set<std::string> emittedGuids;
	while (!pending.empty())
	{
		std::string reference = std::move(pending.front());
		pending.pop_front();
		if (reference.empty() || !visitedReferences.insert(reference).second) continue;
		const std::optional<VansAssetRecord> record =
			FindAsset(projectDatabase, builtInDatabase, reference);
		if (!record || record->state == VansAssetState::Missing)
		{
			VansAssetGuid guid;
			if (VansAssetGuid::TryParse(reference, guid))
				result.errors.push_back("Missing GAF dependency: " + reference);
			continue;
		}
		if (!VansGameplayAssetSchemaRegistry::IsGameplayAssetType(record->type)) continue;
		if (record->type == VansAssetType::GAFEditorLayout)
		{
			result.errors.push_back("Editor-only GAF asset cannot be packaged: " +
				record->sourcePath.string());
			continue;
		}
		const std::string guid = record->guid.ToString();
		if (!emittedGuids.insert(guid).second) continue;
		VansSerializedValue source;
		std::string error;
		if (!VansGameplayAssetStorage::LoadSource(record->sourcePath, source, error))
		{
			result.errors.push_back(record->sourcePath.string() + ": " + error);
			continue;
		}
		const VansGameplayCookResult cooked = VansGameplayAssetStorage::Cook(
			record->type, source, VansGameplayAssetSchemaRegistry::BuiltIns(), configuration);
		if (!cooked)
		{
			result.errors.push_back(record->sourcePath.string() + ": " + cooked.error);
			continue;
		}
		const VansGameplayCompileResult compiled = VansGameplayAssetCompiler::Compile(cooked.asset);
		if (!compiled)
		{
			result.errors.push_back(record->sourcePath.string() + ": " + compiled.error);
			continue;
		}
		const std::filesystem::path artifactPath = ArtifactPathFor(projectRoot, *record);
		std::error_code directoryError;
		std::filesystem::create_directories(artifactPath.parent_path(), directoryError);
		if (directoryError || !VansGameplayAssetStorage::SaveCookedAtomic(
			artifactPath, cooked.asset, error))
		{
			result.errors.push_back(artifactPath.string() + ": " +
				(directoryError ? directoryError.message() : error));
			continue;
		}
		result.assets.push_back({ guid, record->type, record->sourcePath, artifactPath,
			cooked.asset.contentHash, cooked.asset.dependencies });
		result.requiredAssetGuids.push_back(guid);
		for (const std::string& dependency : cooked.asset.dependencies)
		{
			const std::optional<VansAssetRecord> dependencyRecord =
				FindAsset(projectDatabase, builtInDatabase, dependency);
			if (!dependencyRecord || dependencyRecord->state == VansAssetState::Missing)
			{
				result.errors.push_back(record->sourcePath.string() +
					": unresolved GAF dependency " + dependency);
				continue;
			}
			pending.push_back(dependencyRecord->guid.ToString());
			result.requiredAssetGuids.push_back(dependencyRecord->guid.ToString());
		}
	}
	std::sort(result.requiredAssetGuids.begin(), result.requiredAssetGuids.end());
	result.requiredAssetGuids.erase(std::unique(result.requiredAssetGuids.begin(),
		result.requiredAssetGuids.end()), result.requiredAssetGuids.end());
	std::sort(result.assets.begin(), result.assets.end(), [](const auto& left, const auto& right)
	{
		return left.guid < right.guid;
	});
	result.success = result.errors.empty();
	return result;
}
}
