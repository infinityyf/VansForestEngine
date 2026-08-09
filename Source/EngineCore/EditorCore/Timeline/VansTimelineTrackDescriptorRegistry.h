#pragma once

#include "../../EngineAPILayer/Public/EngineDTOs.h"
#include "../../TimelineCore/VansTimelineTypes.h"

#include <string>
#include <vector>

namespace Vans
{
enum class VansTimelineEditorCapabilityLevel
{
	Full,
	DataAndEditor,
	RegisteredOnly
};

struct VansTimelineTrackDescriptor
{
	VansTimelineTrackType type = VansTimelineTrackType::Transform;
	std::string stableTypeId;
	std::string displayName;
	std::string category;
	bool bindingRequired = true;
	bool supportsSections = true;
	bool supportsChannels = true;
	EditorAPI::AssetType sectionAssetType = EditorAPI::AssetType::Unknown;
	VansTimelineEditorCapabilityLevel capability = VansTimelineEditorCapabilityLevel::Full;
	VansTimelineCapability runtimeGate = VansTimelineCapability::Runtime;
};

class VansTimelineTrackDescriptorRegistry
{
public:
	static const VansTimelineTrackDescriptor* Find(VansTimelineTrackType type);
	static const std::vector<VansTimelineTrackDescriptor>& All();
};
}
