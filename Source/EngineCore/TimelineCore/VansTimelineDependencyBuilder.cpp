#include "VansTimelineDependencyBuilder.h"

#include "VansTimelineValidator.h"

#include <algorithm>
#include <unordered_set>

namespace Vans
{
namespace
{
std::string ReferenceIdentity(const VansTimelineAssetReference& reference)
{
	if (!reference.assetGuid.empty()) return "guid:" + reference.assetGuid;
	return "path:" + reference.assetPath;
}

void AddReference(
	std::vector<VansTimelineAssetReference>& references,
	std::unordered_set<std::string>& identities,
	const std::string& guid,
	const std::string& path,
	VansTimelineAssetReferenceKind kind,
	const VansTimelineId& sourceObjectId)
{
	if (guid.empty() && path.empty()) return;
	VansTimelineAssetReference reference{ guid, path, kind, sourceObjectId };
	const std::string identity = ReferenceIdentity(reference);
	if (identities.insert(identity).second)
		references.push_back(std::move(reference));
}

VansTimelineAssetReferenceKind SectionReferenceKind(VansTimelineTrackType type)
{
	switch (type)
	{
	case VansTimelineTrackType::AnimationClip: return VansTimelineAssetReferenceKind::AnimationClip;
	case VansTimelineTrackType::Audio: return VansTimelineAssetReferenceKind::Audio;
	case VansTimelineTrackType::Media: return VansTimelineAssetReferenceKind::Video;
	case VansTimelineTrackType::MaterialSwitch: return VansTimelineAssetReferenceKind::Material;
	case VansTimelineTrackType::SubTimeline: return VansTimelineAssetReferenceKind::Timeline;
	default: return VansTimelineAssetReferenceKind::Unknown;
	}
}

void CollectKeyReferences(
	const VansTimelineSection& section,
	std::vector<VansTimelineAssetReference>& references,
	std::unordered_set<std::string>& identities)
{
	for (const auto& channel : section.channels)
	{
		for (const auto& key : channel.keys)
		{
			if (const auto* object = std::get_if<VansTimelineObjectReference>(&key.value))
				AddReference(references, identities, object->guid, object->path,
					VansTimelineAssetReferenceKind::ObjectReference, key.id);
		}
	}
}

void CollectConfigReferences(
	const VansTimelineTrackConfig& config,
	const VansTimelineId& sourceObjectId,
	std::vector<VansTimelineAssetReference>& references,
	std::unordered_set<std::string>& identities)
{
	if (const auto* animation = std::get_if<VansTimelineAnimationTrackConfig>(&config))
		AddReference(references, identities, animation->avatarMaskGuid, animation->avatarMaskPath,
			VansTimelineAssetReferenceKind::BoneMask, sourceObjectId);
	else if (const auto* postProcess = std::get_if<VansTimelineFadePostProcessTrackConfig>(&config))
		AddReference(references, identities, postProcess->profileGuid, postProcess->profilePath,
			VansTimelineAssetReferenceKind::PostProcessProfile, sourceObjectId);
	else if (const auto* spawnable = std::get_if<VansTimelineSpawnableTrackConfig>(&config))
		AddReference(references, identities, spawnable->spawnTemplateGuid, spawnable->spawnTemplatePath,
			VansTimelineAssetReferenceKind::SpawnTemplate, sourceObjectId);
	else if (const auto* sceneState = std::get_if<VansTimelineSceneStateTrackConfig>(&config))
		AddReference(references, identities, sceneState->sceneGuid, sceneState->scenePath,
			VansTimelineAssetReferenceKind::Scene, sourceObjectId);
}

bool ExpandTimeline(
	const VansTimelineAsset& timeline,
	const VansTimelineDependencyAssetLoader& loader,
	std::unordered_set<std::string>& visited,
	std::unordered_set<std::string>& active,
	VansTimelineDependencyClosure& closure,
	VansTimelineDiagnostics& diagnostics)
{
	const auto direct = VansTimelineDependencyBuilder::CollectDirect(timeline);
	for (const auto& reference : direct)
	{
		const std::string referenceKey = ReferenceIdentity(reference);
		if (std::none_of(closure.transitive.begin(), closure.transitive.end(), [&](const auto& existing)
			{ return ReferenceIdentity(existing) == referenceKey; }))
			closure.transitive.push_back(reference);
		if (reference.kind != VansTimelineAssetReferenceKind::Timeline)
			continue;
		if (!loader)
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, reference.sourceObjectId,
				"assetGuid", "SubTimeline dependency resolution requires a Timeline asset loader" });
			return false;
		}
		VansTimelineAsset child;
		std::string identity;
		std::string error;
		if (!loader(reference, child, identity, error))
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, reference.sourceObjectId,
				"assetGuid", "Failed to load SubTimeline: " + error });
			return false;
		}
		if (identity.empty()) identity = referenceKey;
		if (active.find(identity) != active.end())
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, reference.sourceObjectId,
				"assetGuid", "SubTimeline dependency cycle detected at " + identity });
			return false;
		}
		if (!visited.insert(identity).second)
			continue;
		const auto childDiagnostics = VansTimelineValidator::Validate(child);
		diagnostics.insert(diagnostics.end(), childDiagnostics.begin(), childDiagnostics.end());
		if (VansTimelineValidator::HasErrors(childDiagnostics))
			return false;
		active.insert(identity);
		if (!ExpandTimeline(child, loader, visited, active, closure, diagnostics))
			return false;
		active.erase(identity);
	}
	return true;
}
}

std::vector<VansTimelineAssetReference> VansTimelineDependencyBuilder::CollectDirect(const VansTimelineAsset& asset)
{
	std::vector<VansTimelineAssetReference> references;
	std::unordered_set<std::string> identities;
	for (const auto& binding : asset.bindings)
	{
		if (binding.kind == VansTimelineBindingKind::Asset)
			AddReference(references, identities, binding.assetGuid, binding.assetPath,
				VansTimelineAssetReferenceKind::ObjectReference, binding.id);
	}
	for (const auto& track : asset.tracks)
	{
		CollectConfigReferences(track.config, track.id, references, identities);
		for (const auto& section : track.sections)
		{
			AddReference(references, identities, section.assetGuid, section.assetPath,
				SectionReferenceKind(track.type), section.id);
			CollectConfigReferences(section.config, section.id, references, identities);
			CollectKeyReferences(section, references, identities);
		}
	}
	std::stable_sort(references.begin(), references.end(), [](const auto& left, const auto& right)
	{
		const std::string leftKey = ReferenceIdentity(left);
		const std::string rightKey = ReferenceIdentity(right);
		if (leftKey != rightKey) return leftKey < rightKey;
		return static_cast<int>(left.kind) < static_cast<int>(right.kind);
	});
	return references;
}

bool VansTimelineDependencyBuilder::BuildClosure(
	const VansTimelineAsset& root,
	const VansTimelineDependencyAssetLoader& loader,
	VansTimelineDependencyClosure& closure,
	VansTimelineDiagnostics& diagnostics)
{
	closure = {};
	diagnostics.clear();
	closure.direct = CollectDirect(root);
	std::unordered_set<std::string> visited;
	std::unordered_set<std::string> active{ "root" };
	const bool result = ExpandTimeline(root, loader, visited, active, closure, diagnostics);
	std::stable_sort(closure.transitive.begin(), closure.transitive.end(), [](const auto& left, const auto& right)
	{
		return ReferenceIdentity(left) < ReferenceIdentity(right);
	});
	return result && !VansTimelineValidator::HasErrors(diagnostics);
}
}
