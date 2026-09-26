#pragma once

#include "../../EngineAPILayer/Public/EngineDTOs.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"

#include <vector>

namespace Vans
{
struct VansTimelineTrackDescriptor
{
	VansTimelineTrackTypeId typeId;
	std::string stableName;
	std::string displayName;
	std::string category;
	bool bindingRequired = false;
	bool supportsSections = false;
	bool supportsChannels = false;
	bool supportsRanges = false;
	EditorAPI::AssetType sectionAssetType = EditorAPI::AssetType::Unknown;
	const VansTimelineSourceSchema* schema = nullptr;
};

class VansTimelineTrackDescriptorRegistry
{
public:
	static const VansTimelineTrackDescriptor* Find(
		const std::vector<VansTimelineTrackDescriptor>& descriptors,
		VansTimelineTrackTypeId typeId);
	static const VansTimelineTrackDescriptor* Find(
		const std::vector<VansTimelineTrackDescriptor>& descriptors,
		const VansTimelineTrackTypeRef& type);
	static std::vector<VansTimelineTrackDescriptor> Build(
		const VansTimelineTrackExtensionRegistry& extensions);
};
}
