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
	static const VansTimelineTrackDescriptor* Find(VansTimelineTrackTypeId typeId);
	static const VansTimelineTrackDescriptor* Find(const VansTimelineTrackTypeRef& type);
	static const std::vector<VansTimelineTrackDescriptor>& All();
	static std::vector<VansTimelineTrackDescriptor> Build(
		const VansTimelineTrackExtensionRegistry& extensions);
};
}
