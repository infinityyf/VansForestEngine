#pragma once

#include "VansTimelineTrackExtensionRegistry.h"

#include <functional>
#include <string>
#include <vector>

namespace Vans
{
enum class VansTimelineDependencyKind : std::uint8_t
{
	Asset,
	ServiceCapability,
	PayloadSchema,
	BindingType
};

struct VansTimelineDependency
{
	VansTimelineDependencyKind kind = VansTimelineDependencyKind::Asset;
	std::string stableType;
	std::string guid;
	std::string path;
	VansTimelineId sourceObjectId;
};

struct VansTimelineDependencyClosure
{
	std::vector<VansTimelineDependency> direct;
	std::vector<VansTimelineDependency> transitive;
};

using VansTimelineDependencyAssetLoader = std::function<bool(
	const VansTimelineDependency& dependency,
	VansTimelineAsset& asset,
	std::string& identity,
	std::string& error)>;

class VansTimelineDependencyBuilder
{
public:
	static std::vector<VansTimelineDependency> CollectDirect(
		const VansTimelineAsset& asset,
		const VansTimelineTrackExtensionRegistry& extensions,
		VansTimelineDiagnostics& diagnostics);
	static bool BuildClosure(
		const VansTimelineAsset& root,
		const VansTimelineTrackExtensionRegistry& extensions,
		const VansTimelineDependencyAssetLoader& loader,
		VansTimelineDependencyClosure& closure,
		VansTimelineDiagnostics& diagnostics);
};
}
