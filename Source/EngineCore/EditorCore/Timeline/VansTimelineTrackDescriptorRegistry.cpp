#include "VansTimelineTrackDescriptorRegistry.h"

namespace Vans
{
namespace
{
EditorAPI::AssetType AssetPicker(std::string_view kind)
{
	if (kind == "Audio") return EditorAPI::AssetType::Audio;
	if (kind == "Video") return EditorAPI::AssetType::Video;
	if (kind == "AnimationClip") return EditorAPI::AssetType::AnimationClip;
	if (kind == "Timeline") return EditorAPI::AssetType::Timeline;
	if (kind == "PostProcessProfile") return EditorAPI::AssetType::PostProcessProfile;
	return EditorAPI::AssetType::Unknown;
}
}

const std::vector<VansTimelineTrackDescriptor>& VansTimelineTrackDescriptorRegistry::All()
{
	static const std::vector<VansTimelineTrackDescriptor> descriptors =
		Build(VansTimelineTrackExtensionRegistry::BuiltIns());
	return descriptors;
}

std::vector<VansTimelineTrackDescriptor> VansTimelineTrackDescriptorRegistry::Build(
	const VansTimelineTrackExtensionRegistry& extensions)
{
	std::vector<VansTimelineTrackDescriptor> descriptors;
	for (const VansTimelineTrackExtensionDescriptor& extension : extensions.All())
	{
		VansTimelineTrackDescriptor descriptor;
		descriptor.typeId = extension.typeId;
		descriptor.stableName = extension.stableName;
		descriptor.displayName = extension.displayName;
		descriptor.category = extension.category;
		descriptor.bindingRequired = extension.binding == VansTimelineBindingRequirement::Required;
		descriptor.supportsSections = VansHasTimelineFlag(extension.flags, VansTimelineTrackFlags::SupportsSections);
		descriptor.supportsChannels = VansHasTimelineFlag(extension.flags, VansTimelineTrackFlags::SupportsChannels);
		descriptor.supportsRanges = VansHasTimelineFlag(extension.flags, VansTimelineTrackFlags::RangeEdge);
		descriptor.sectionAssetType = AssetPicker(extension.sectionAssetKind);
		descriptor.schema = &extension.sourceSchema;
		descriptors.push_back(std::move(descriptor));
	}
	return descriptors;
}

const VansTimelineTrackDescriptor* VansTimelineTrackDescriptorRegistry::Find(VansTimelineTrackTypeId typeId)
{
	for (const auto& descriptor : All()) if (descriptor.typeId == typeId) return &descriptor;
	return nullptr;
}

const VansTimelineTrackDescriptor* VansTimelineTrackDescriptorRegistry::Find(const VansTimelineTrackTypeRef& type)
{
	return Find(type.typeId);
}
}
