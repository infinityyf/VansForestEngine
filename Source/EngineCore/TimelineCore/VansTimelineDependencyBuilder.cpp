#include "VansTimelineDependencyBuilder.h"
#include "VansTimelineValidator.h"

#include <algorithm>
#include <unordered_set>

namespace Vans
{
namespace
{
std::string DependencyKey(const VansTimelineDependency& dependency)
{
	return std::to_string(static_cast<int>(dependency.kind)) + "|" + dependency.stableType + "|" +
		dependency.guid + "|" + dependency.path;
}

void SortUnique(std::vector<VansTimelineDependency>& dependencies)
{
	std::stable_sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right)
	{
		return DependencyKey(left) < DependencyKey(right);
	});
	dependencies.erase(std::unique(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right)
	{
		return DependencyKey(left) == DependencyKey(right);
	}), dependencies.end());
}
}

std::vector<VansTimelineDependency> VansTimelineDependencyBuilder::CollectDirect(
	const VansTimelineAsset& asset,
	const VansTimelineTrackExtensionRegistry& extensions,
	VansTimelineDiagnostics& diagnostics)
{
	std::vector<VansTimelineDependency> dependencies;
	for (const VansTimelineTrack& track : asset.tracks)
	{
		const VansTimelineTrackExtensionDescriptor* descriptor = extensions.Resolve(track.type.typeId);
		if (!descriptor)
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.TrackExtensionMissing", {}, track.id, "type",
				"Timeline dependency collection requires a registered extension" });
			continue;
		}
		if (!descriptor->sectionAssetKind.empty())
			for (const VansTimelineSection& section : track.sections)
				if (!section.assetGuid.empty() || !section.assetPath.empty())
					dependencies.push_back({ VansTimelineDependencyKind::Asset,
						descriptor->sectionAssetKind, section.assetGuid,
						section.assetPath, section.id });
		if (descriptor->collectDependencies) descriptor->collectDependencies(track, dependencies);
	}
	SortUnique(dependencies);
	return dependencies;
}

bool VansTimelineDependencyBuilder::BuildClosure(
	const VansTimelineAsset& root,
	const VansTimelineTrackExtensionRegistry& extensions,
	const VansTimelineDependencyAssetLoader& loader,
	VansTimelineDependencyClosure& closure,
	VansTimelineDiagnostics& diagnostics)
{
	closure = {};
	closure.direct = CollectDirect(root, extensions, diagnostics);
	if (VansTimelineValidator::HasErrors(diagnostics)) return false;
	std::vector<VansTimelineDependency> pending;
	for (const auto& dependency : closure.direct)
		if (dependency.kind == VansTimelineDependencyKind::Asset && dependency.stableType == "Timeline")
			pending.push_back(dependency);
	std::unordered_set<std::string> visited;
	while (!pending.empty())
	{
		VansTimelineDependency dependency = std::move(pending.back());
		pending.pop_back();
		const std::string fallbackIdentity = !dependency.guid.empty() ? dependency.guid : dependency.path;
		if (fallbackIdentity.empty() || !visited.insert(fallbackIdentity).second) continue;
		if (!loader)
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.DependencyLoaderMissing", {}, dependency.sourceObjectId, "dependency",
				"Nested Timeline dependency loader is not configured" });
			return false;
		}
		VansTimelineAsset nested; std::string identity; std::string error;
		if (!loader(dependency, nested, identity, error))
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.DependencyLoadFailed", {}, dependency.sourceObjectId, "dependency",
				error.empty() ? "Timeline dependency failed to load" : error });
			return false;
		}
		if (!identity.empty() && identity != fallbackIdentity)
		{
			if (!visited.insert(identity).second) continue;
		}
		auto nestedDependencies = CollectDirect(nested, extensions, diagnostics);
		for (auto& child : nestedDependencies)
		{
			closure.transitive.push_back(child);
			if (child.kind == VansTimelineDependencyKind::Asset && child.stableType == "Timeline")
				pending.push_back(child);
		}
	}
	SortUnique(closure.transitive);
	return !VansTimelineValidator::HasErrors(diagnostics);
}
}
