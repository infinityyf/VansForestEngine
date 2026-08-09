#pragma once

#include "VansTimelineAsset.h"

#include <functional>
#include <string>
#include <vector>

namespace Vans
{
enum class VansTimelineAssetReferenceKind
{
	Unknown,
	Timeline,
	AnimationClip,
	BoneMask,
	Audio,
	Video,
	Material,
	PostProcessProfile,
	Scene,
	SpawnTemplate,
	ObjectReference
};

struct VansTimelineAssetReference
{
	std::string assetGuid;
	std::string assetPath;
	VansTimelineAssetReferenceKind kind = VansTimelineAssetReferenceKind::Unknown;
	VansTimelineId sourceObjectId;
};

struct VansTimelineDependencyClosure
{
	std::vector<VansTimelineAssetReference> direct;
	std::vector<VansTimelineAssetReference> transitive;
};

using VansTimelineDependencyAssetLoader = std::function<bool(
	const VansTimelineAssetReference& reference,
	VansTimelineAsset& asset,
	std::string& identity,
	std::string& error)>;

class VansTimelineDependencyBuilder
{
public:
	static std::vector<VansTimelineAssetReference> CollectDirect(const VansTimelineAsset& asset);
	static bool BuildClosure(
		const VansTimelineAsset& root,
		const VansTimelineDependencyAssetLoader& loader,
		VansTimelineDependencyClosure& closure,
		VansTimelineDiagnostics& diagnostics);
};
}
