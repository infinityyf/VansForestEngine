#include "VansTimelineTrackDescriptorRegistry.h"

#include <algorithm>

namespace Vans
{
const std::vector<VansTimelineTrackDescriptor>& VansTimelineTrackDescriptorRegistry::All()
{
	static const std::vector<VansTimelineTrackDescriptor> descriptors{
		{ VansTimelineTrackType::Transform, "Transform", "Transform", "Object", true, true, true },
		{ VansTimelineTrackType::Property, "Property", "Property", "Object", true, true, true },
		{ VansTimelineTrackType::Activation, "Activation", "Activation / Visibility", "Object", true, true, false },
		{ VansTimelineTrackType::Constraint, "Constraint", "Constraint", "Object", true, true, true },
		{ VansTimelineTrackType::AnimationClip, "AnimationClip", "Animation Clip", "Animation", true, true, true,
			EditorAPI::AssetType::AnimationClip },
		{ VansTimelineTrackType::AnimatorParameter, "AnimatorParameter", "Animator Parameter", "Animation", true, true, true },
		{ VansTimelineTrackType::BoneOverride, "BoneOverride", "Bone Override / IK", "Animation", true, true, true },
		{ VansTimelineTrackType::Audio, "Audio", "Audio", "Media", true, true, true, EditorAPI::AssetType::Audio },
		{ VansTimelineTrackType::Media, "Media", "Video / Media", "Media", true, true, true, EditorAPI::AssetType::Video },
		{ VansTimelineTrackType::Particle, "Particle", "Particle / VFX", "Media", true, true, true,
			EditorAPI::AssetType::Particle },
		{ VansTimelineTrackType::CameraCut, "CameraCut", "Camera Cut", "Cinematic", true, true, false },
		{ VansTimelineTrackType::CameraProperty, "CameraProperty", "Camera Property", "Cinematic", true, true, true },
		{ VansTimelineTrackType::CameraShake, "CameraShake", "Camera Shake", "Cinematic", true, true, true },
		{ VansTimelineTrackType::FadePostProcess, "FadePostProcess", "Fade / Post Process", "Cinematic", false, true, true,
			EditorAPI::AssetType::PostProcessProfile },
		{ VansTimelineTrackType::Light, "Light", "Light", "Rendering", true, true, true },
		{ VansTimelineTrackType::MaterialParameter, "MaterialParameter", "Material Parameter", "Rendering", true, true, true },
		{ VansTimelineTrackType::MaterialSwitch, "MaterialSwitch", "Material Switch", "Rendering", true, true, false,
			EditorAPI::AssetType::Material },
		{ VansTimelineTrackType::UIState, "UIState", "UI State", "UI", true, true, true },
		{ VansTimelineTrackType::EventSignal, "EventSignal", "Event / Signal", "Logic", false, true, true },
		{ VansTimelineTrackType::SubTimeline, "SubTimeline", "SubTimeline / Shot", "Cinematic", false, true, false,
			EditorAPI::AssetType::Timeline },
		{ VansTimelineTrackType::Spawnable, "Spawnable", "Spawnable / Lifetime", "World", false, true, false,
			EditorAPI::AssetType::Unknown, VansTimelineEditorCapabilityLevel::DataAndEditor, VansTimelineCapability::SpawnTemplate },
		{ VansTimelineTrackType::TimeScale, "TimeScale", "Time Scale", "Logic", false, true, true },
		{ VansTimelineTrackType::SceneState, "SceneState", "Scene State", "World", false, true, false,
			EditorAPI::AssetType::Scene, VansTimelineEditorCapabilityLevel::DataAndEditor, VansTimelineCapability::AdditiveScene },
		{ VansTimelineTrackType::Custom, "Custom", "Custom", "Extension", false, true, true,
			EditorAPI::AssetType::Unknown, VansTimelineEditorCapabilityLevel::RegisteredOnly }
	};
	return descriptors;
}

const VansTimelineTrackDescriptor* VansTimelineTrackDescriptorRegistry::Find(VansTimelineTrackType type)
{
	const auto& descriptors = All();
	const auto found = std::find_if(descriptors.begin(), descriptors.end(),
		[&](const auto& descriptor) { return descriptor.type == type; });
	return found == descriptors.end() ? nullptr : &*found;
}
}
