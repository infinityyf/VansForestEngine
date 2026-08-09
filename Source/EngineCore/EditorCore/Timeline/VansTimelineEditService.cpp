#include "VansTimelineEditService.h"

#include "../../AssetCore/VansAssetGuid.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../TimelineCore/VansTimelineSerialization.h"
#include "../../TimelineCore/VansTimelineValidator.h"
#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansEditorAssetSaveService.h"
#include "../VansEditorPropertyDescriptorRegistry.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace Vans
{
namespace
{
TimelineEditResult Failure(std::string message)
{
	return { false, std::move(message), {} };
}

VansTimelineChannel DefaultPropertyChannel(const VansTimelinePropertyTrackConfig& property)
{
	VansTimelineChannel channel;
	channel.id = VansTimelineEditService::NewStableId();
	channel.name = property.descriptorId.empty() ? "Value" : property.descriptorId;
	channel.type = property.valueType;
	return channel;
}

void RegenerateSectionIds(VansTimelineSection& section)
{
	section.id = VansTimelineEditService::NewStableId();
	for (VansTimelineChannel& channel : section.channels)
	{
		channel.id = VansTimelineEditService::NewStableId();
		for (VansTimelineKey& key : channel.keys)
			key.id = VansTimelineEditService::NewStableId();
	}
}

bool OffsetNumericKeyValue(VansTimelineKeyValue& value, double delta)
{
	if (auto* typed = std::get_if<float>(&value)) { *typed += static_cast<float>(delta); return true; }
	if (auto* typed = std::get_if<double>(&value)) { *typed += delta; return true; }
	if (auto* typed = std::get_if<std::int32_t>(&value))
	{
		const double next = std::clamp(static_cast<double>(*typed) + delta,
			static_cast<double>(std::numeric_limits<std::int32_t>::min()),
			static_cast<double>(std::numeric_limits<std::int32_t>::max()));
		*typed = static_cast<std::int32_t>(std::llround(next));
		return true;
	}
	if (auto* typed = std::get_if<std::int64_t>(&value))
	{
		const long double next = std::clamp(static_cast<long double>(*typed) + delta,
			static_cast<long double>(std::numeric_limits<std::int64_t>::min()),
			static_cast<long double>(std::numeric_limits<std::int64_t>::max()));
		*typed = static_cast<std::int64_t>(std::llround(next));
		return true;
	}
	return false;
}

bool NumericKeyValue(const VansTimelineKeyValue& value, double& number)
{
	if (const auto* typed = std::get_if<float>(&value)) { number = *typed; return true; }
	if (const auto* typed = std::get_if<double>(&value)) { number = *typed; return true; }
	if (const auto* typed = std::get_if<std::int32_t>(&value)) { number = *typed; return true; }
	if (const auto* typed = std::get_if<std::int64_t>(&value))
	{
		number = static_cast<double>(*typed);
		return true;
	}
	return false;
}

bool ScaleNumericKeyValue(VansTimelineKeyValue& value, double pivot, double scale)
{
	if (auto* typed = std::get_if<float>(&value))
	{
		*typed = static_cast<float>(pivot + (static_cast<double>(*typed) - pivot) * scale);
		return true;
	}
	if (auto* typed = std::get_if<double>(&value))
	{
		*typed = pivot + (*typed - pivot) * scale;
		return true;
	}
	if (auto* typed = std::get_if<std::int32_t>(&value))
	{
		const double scaled = pivot + (static_cast<double>(*typed) - pivot) * scale;
		*typed = static_cast<std::int32_t>(std::llround(std::clamp(scaled,
			static_cast<double>(std::numeric_limits<std::int32_t>::min()),
			static_cast<double>(std::numeric_limits<std::int32_t>::max()))));
		return true;
	}
	if (auto* typed = std::get_if<std::int64_t>(&value))
	{
		const long double scaled = pivot + (static_cast<long double>(*typed) - pivot) * scale;
		*typed = static_cast<std::int64_t>(std::llround(std::clamp(scaled,
			static_cast<long double>(std::numeric_limits<std::int64_t>::min()),
			static_cast<long double>(std::numeric_limits<std::int64_t>::max()))));
		return true;
	}
	return false;
}
}

VansTimelineId VansTimelineEditService::NewStableId()
{
	return VansAssetGuid::New().ToString();
}

VansTimelineTrackConfig VansTimelineEditService::DefaultTrackConfig(VansTimelineTrackType type)
{
	switch (type)
	{
	case VansTimelineTrackType::Transform: return VansTimelineTransformTrackConfig{};
	case VansTimelineTrackType::Property:
	{
		VansTimelinePropertyTrackConfig config;
		config.componentTypeId = VansRuntimeComponentType_Transform;
		config.descriptorId = "Transform.Position";
		config.propertyPath = "position";
		config.valueType = VansTimelineChannelType::Vec3;
		config.unit = "m";
		config.minimum = -100000.0;
		config.maximum = 100000.0;
		return config;
	}
	case VansTimelineTrackType::Activation: return VansTimelineActivationTrackConfig{};
	case VansTimelineTrackType::Constraint: return VansTimelineConstraintTrackConfig{};
	case VansTimelineTrackType::AnimationClip:
	{
		VansTimelineAnimationTrackConfig config; config.slot = "Timeline"; return config;
	}
	case VansTimelineTrackType::AnimatorParameter:
	{
		VansTimelineAnimatorParameterTrackConfig config; config.parameterName = "TimelineParameter"; return config;
	}
	case VansTimelineTrackType::BoneOverride:
	{
		VansTimelineBoneOverrideTrackConfig config; config.bone = "Root"; return config;
	}
	case VansTimelineTrackType::Audio: return VansTimelineAudioTrackConfig{};
	case VansTimelineTrackType::Media: return VansTimelineMediaTrackConfig{};
	case VansTimelineTrackType::Particle: return VansTimelineParticleTrackConfig{};
	case VansTimelineTrackType::CameraCut: return VansTimelineCameraCutTrackConfig{};
	case VansTimelineTrackType::CameraProperty: return VansTimelineCameraPropertyTrackConfig{};
	case VansTimelineTrackType::CameraShake: return VansTimelineCameraShakeTrackConfig{};
	case VansTimelineTrackType::FadePostProcess: return VansTimelineFadePostProcessTrackConfig{};
	case VansTimelineTrackType::Light: return VansTimelineLightTrackConfig{};
	case VansTimelineTrackType::MaterialParameter:
	{
		VansTimelineMaterialParameterTrackConfig config; config.materialSlotId = "0"; config.parameterName = "Value"; return config;
	}
	case VansTimelineTrackType::MaterialSwitch:
	{
		VansTimelineMaterialSwitchTrackConfig config; config.materialSlotId = "0"; return config;
	}
	case VansTimelineTrackType::UIState:
	{
		VansTimelineUIStateTrackConfig config;
		config.screen = "Main";
		config.targetKind = "Screen";
		config.descriptorId = "Screen.Visible";
		config.setterId = 300;
		return config;
	}
	case VansTimelineTrackType::EventSignal:
	{
		VansTimelineEventTrackConfig config; config.signalId = NewStableId(); config.displayName = "Timeline Signal"; return config;
	}
	case VansTimelineTrackType::SubTimeline: return VansTimelineSubTimelineTrackConfig{};
	case VansTimelineTrackType::Spawnable: return VansTimelineSpawnableTrackConfig{};
	case VansTimelineTrackType::TimeScale: return VansTimelineTimeScaleTrackConfig{};
	case VansTimelineTrackType::SceneState: return VansTimelineSceneStateTrackConfig{};
	case VansTimelineTrackType::Custom: return VansTimelineCustomTrackConfig{};
	}
	return std::monostate{};
}

TimelineEditResult VansTimelineEditService::Open(const std::filesystem::path& sourcePath)
{
	m_SourcePath = sourcePath;
	m_Document = VansAssetDocumentRegistry::Get().GetOrOpen(sourcePath);
	if (!m_Document || !m_Document->sourceDocument.IsLoaded())
		return Failure(m_Document ? m_Document->lastError : "Timeline document could not be opened");
	return Reload();
}

TimelineEditResult VansTimelineEditService::Reload()
{
	if (!m_Document || !m_Document->sourceDocument.IsLoaded())
		return Failure("Timeline document is not open");
	std::string error;
	if (!VansTimelineSerialization::DecodeSerialized(
		m_Document->sourceDocument.SerializedRootSnapshot(), m_Asset, error))
		return Failure(std::move(error));
	m_InteractionSnapshot.reset();
	return ValidateWorkingCopy();
}

TimelineEditResult VansTimelineEditService::ValidateWorkingCopy()
{
	VansTimelineValidationContext context;
	context.runtimeValidation = false;
	context.supportsPropertyDescriptor = [](std::uint16_t componentTypeId,
		const std::string& descriptorId, VansTimelineChannelType valueType)
	{
		const auto* descriptor = VansEditorPropertyDescriptorRegistry::FindAnimatable(descriptorId);
		return descriptor && descriptor->componentTypeId == componentTypeId && descriptor->valueType == valueType;
	};
	m_Diagnostics = VansTimelineValidator::Validate(m_Asset, context);
	if (VansTimelineValidator::HasErrors(m_Diagnostics))
	{
		const auto found = std::find_if(m_Diagnostics.begin(), m_Diagnostics.end(), [](const auto& item)
		{
			return item.severity == VansTimelineDiagnosticSeverity::Error;
		});
		return Failure(found == m_Diagnostics.end() ? "Timeline validation failed" : found->message);
	}
	return { true, {}, {} };
}

TimelineEditResult VansTimelineEditService::CommitWorkingCopy()
{
	if (!m_Document) return Failure("Timeline document is not open");
	VansTimelineSerialization::Normalize(m_Asset);
	if (TimelineEditResult validation = ValidateWorkingCopy(); !validation)
		return validation;
	const AssetDocumentEditResult result = VansAssetDocumentEditService::ReplaceRoot(
		m_Document->sourceDocument, VansTimelineSerialization::EncodeSerialized(m_Asset));
	if (!result && result.message != "Asset property is unchanged")
		return Failure(result.message);
	return { true, {}, {} };
}

TimelineEditResult VansTimelineEditService::FinishMutation(VansTimelineId objectId)
{
	if (IsInteracting())
	{
		TimelineEditResult validation = ValidateWorkingCopy();
		validation.objectId = std::move(objectId);
		return validation;
	}
	TimelineEditResult result = CommitWorkingCopy();
	result.objectId = std::move(objectId);
	if (!result)
	{
		const std::string message = result.message;
		Reload();
		result.message = message;
	}
	return result;
}

TimelineEditResult VansTimelineEditService::SetPlaybackRange(
	VansTimelineTick startTick,
	VansTimelineTick endTick)
{
	if (startTick < 0 || endTick <= startTick || endTick > m_Asset.durationTicks)
		return Failure("Playback range must be non-empty and contained by the Timeline duration");
	m_Asset.playbackRange = { startTick, endTick };
	return FinishMutation();
}

TimelineEditResult VansTimelineEditService::RenameObject(
	const VansTimelineId& objectId,
	std::string name)
{
	if (name.empty()) return Failure("Timeline names cannot be empty");
	if (objectId.empty())
	{
		m_Asset.metadata.displayName = std::move(name);
		return FinishMutation();
	}
	for (auto& binding : m_Asset.bindings)
		if (binding.id == objectId) { binding.displayName = std::move(name); return FinishMutation(objectId); }
	for (auto& group : m_Asset.groups)
		if (group.id == objectId) { group.name = std::move(name); return FinishMutation(objectId); }
	for (auto& track : m_Asset.tracks)
	{
		if (track.id == objectId) { track.name = std::move(name); return FinishMutation(objectId); }
		for (auto& section : track.sections)
		{
			if (section.id == objectId) { section.name = std::move(name); return FinishMutation(objectId); }
			for (auto& channel : section.channels)
				if (channel.id == objectId) { channel.name = std::move(name); return FinishMutation(objectId); }
		}
	}
	for (auto& marker : m_Asset.markers)
		if (marker.id == objectId) { marker.label = std::move(name); return FinishMutation(objectId); }
	return Failure("Timeline object does not support renaming or no longer exists");
}

TimelineEditResult VansTimelineEditService::Save(EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (IsInteracting())
	{
		TimelineEditResult committed = CommitInteraction();
		if (!committed) return committed;
	}
	if (!m_Document) return Failure("Timeline document is not open");
	const VansAssetSaveResult result = VansEditorAssetSaveService::Get().SaveAsset(editorAPI, m_Document);
	return result ? TimelineEditResult{ true, {}, {} }
		: Failure(result.message.empty() ? "Timeline save failed" : result.message);
}

TimelineEditResult VansTimelineEditService::RevertToSaved()
{
	if (!m_Document) return Failure("Timeline document is not open");
	if (IsInteracting()) CancelInteraction();
	const AssetDocumentEditResult result = VansAssetDocumentEditService::RevertToSaved(m_Document->sourceDocument);
	if (!result) return Failure(result.message);
	return Reload();
}

TimelineEditResult VansTimelineEditService::Undo()
{
	if (!m_Document) return Failure("Timeline document is not open");
	if (IsInteracting()) CancelInteraction();
	const AssetDocumentEditResult result = VansAssetDocumentEditService::Undo(m_Document->sourceDocument);
	if (!result) return Failure(result.message);
	return Reload();
}

TimelineEditResult VansTimelineEditService::Redo()
{
	if (!m_Document) return Failure("Timeline document is not open");
	if (IsInteracting()) CancelInteraction();
	const AssetDocumentEditResult result = VansAssetDocumentEditService::Redo(m_Document->sourceDocument);
	if (!result) return Failure(result.message);
	return Reload();
}

TimelineEditResult VansTimelineEditService::BeginInteraction()
{
	if (IsInteracting()) return Failure("A Timeline interaction is already active");
	m_InteractionSnapshot = m_Asset;
	return { true, {}, {} };
}

TimelineEditResult VansTimelineEditService::CommitInteraction()
{
	if (!IsInteracting()) return Failure("No Timeline interaction is active");
	TimelineEditResult result = CommitWorkingCopy();
	if (result) m_InteractionSnapshot.reset();
	return result;
}

TimelineEditResult VansTimelineEditService::CancelInteraction()
{
	if (!IsInteracting()) return Failure("No Timeline interaction is active");
	m_Asset = std::move(*m_InteractionSnapshot);
	m_InteractionSnapshot.reset();
	return ValidateWorkingCopy();
}

TimelineEditResult VansTimelineEditService::AddBinding(VansTimelineBinding binding)
{
	if (binding.id.empty()) binding.id = NewStableId();
	const VansTimelineId id = binding.id;
	if (binding.displayName.empty()) binding.displayName = "Binding";
	m_Asset.bindings.push_back(std::move(binding));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::AddGroup(VansTimelineGroup group)
{
	if (group.id.empty()) group.id = NewStableId();
	const VansTimelineId id = group.id;
	if (group.name.empty()) group.name = "Folder";
	group.order = static_cast<std::int32_t>(m_Asset.groups.size());
	m_Asset.groups.push_back(std::move(group));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::AddMarker(VansTimelineMarker marker)
{
	if (marker.id.empty()) marker.id = NewStableId();
	const VansTimelineId id = marker.id;
	if (marker.label.empty()) marker.label = "Marker";
	marker.tick = std::clamp(marker.tick, VansTimelineTick{ 0 }, m_Asset.durationTicks);
	m_Asset.markers.push_back(std::move(marker));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::MoveMarker(VansTimelineId markerId, VansTimelineTick tick)
{
	const auto marker = std::find_if(m_Asset.markers.begin(), m_Asset.markers.end(),
		[&](const auto& value) { return value.id == markerId; });
	if (marker == m_Asset.markers.end()) return Failure("Timeline marker does not exist");
	marker->tick = std::clamp(tick, VansTimelineTick{ 0 }, m_Asset.durationTicks);
	return FinishMutation(std::move(markerId));
}

TimelineEditResult VansTimelineEditService::AddTrack(
	VansTimelineTrackType type,
	VansTimelineId bindingId,
	VansTimelineId groupId,
	VansTimelineTrackConfig config)
{
	VansTimelineTrack track;
	track.id = NewStableId();
	track.type = type;
	track.name = VansTimelineSerialization::TrackTypeName(type);
	track.bindingId = std::move(bindingId);
	track.groupId = std::move(groupId);
	track.order = static_cast<std::int32_t>(m_Asset.tracks.size());
	track.config = std::holds_alternative<std::monostate>(config)
		? DefaultTrackConfig(type) : std::move(config);
	if (auto* constraint = std::get_if<VansTimelineConstraintTrackConfig>(&track.config))
	{
		constraint->sourceBindingId = track.bindingId;
		const auto target = std::find_if(m_Asset.bindings.begin(), m_Asset.bindings.end(), [&](const auto& binding)
		{
			return binding.id != track.bindingId &&
				(binding.kind == VansTimelineBindingKind::SceneEntity || binding.kind == VansTimelineBindingKind::SceneComponent);
		});
		if (target == m_Asset.bindings.end())
			return Failure("Constraint tracks require a second compatible binding target");
		constraint->targetBindingId = target->id;
	}
	if (auto* cameraCut = std::get_if<VansTimelineCameraCutTrackConfig>(&track.config))
		cameraCut->cameraBindingId = track.bindingId;
	const VansTimelineId id = track.id;
	m_Asset.tracks.push_back(std::move(track));
	return FinishMutation(id);
}

VansTimelineTrack* VansTimelineEditService::FindTrack(const VansTimelineId& id)
{
	const auto found = std::find_if(m_Asset.tracks.begin(), m_Asset.tracks.end(),
		[&](const auto& item) { return item.id == id; });
	return found == m_Asset.tracks.end() ? nullptr : &*found;
}

VansTimelineSection* VansTimelineEditService::FindSection(VansTimelineTrack& track, const VansTimelineId& id)
{
	const auto found = std::find_if(track.sections.begin(), track.sections.end(),
		[&](const auto& item) { return item.id == id; });
	return found == track.sections.end() ? nullptr : &*found;
}

TimelineEditResult VansTimelineEditService::AddSection(VansTimelineId trackId, VansTimelineSection section)
{
	VansTimelineTrack* track = FindTrack(trackId);
	if (!track) return Failure("Timeline track does not exist");
	if (section.id.empty()) section.id = NewStableId();
	if (section.name.empty()) section.name = track->name;
	if (section.durationTicks <= 0) section.durationTicks = std::max<VansTimelineTick>(1, m_Asset.durationTicks / 10);
	if (track->type == VansTimelineTrackType::Property && section.channels.empty())
		section.channels.push_back(DefaultPropertyChannel(std::get<VansTimelinePropertyTrackConfig>(track->config)));
	if (section.channels.empty())
	{
		auto addChannel = [&](const char* name, VansTimelineChannelType type)
		{
			VansTimelineChannel channel;
			channel.id = NewStableId();
			channel.name = name;
			channel.type = type;
			section.channels.push_back(std::move(channel));
		};
		switch (track->type)
		{
		case VansTimelineTrackType::Transform:
			addChannel("Position", VansTimelineChannelType::Vec3);
			addChannel("Rotation", VansTimelineChannelType::Quaternion);
			addChannel("Scale", VansTimelineChannelType::Vec3);
			break;
		case VansTimelineTrackType::Constraint: addChannel("Weight", VansTimelineChannelType::Float); break;
		case VansTimelineTrackType::AnimatorParameter: addChannel("Value", VansTimelineChannelType::Float); break;
		case VansTimelineTrackType::BoneOverride:
			addChannel("Position", VansTimelineChannelType::Vec3);
			addChannel("Rotation", VansTimelineChannelType::Quaternion);
			break;
		case VansTimelineTrackType::CameraProperty:
		{
			const auto& camera = std::get<VansTimelineCameraPropertyTrackConfig>(track->config);
			if (camera.fieldOfView) addChannel("FieldOfView", VansTimelineChannelType::Float);
			if (camera.nearClip) addChannel("NearClip", VansTimelineChannelType::Float);
			if (camera.farClip) addChannel("FarClip", VansTimelineChannelType::Float);
			if (camera.transform)
			{
				addChannel("Position", VansTimelineChannelType::Vec3);
				addChannel("Rotation", VansTimelineChannelType::Quaternion);
				addChannel("Scale", VansTimelineChannelType::Vec3);
			}
			break;
		}
		case VansTimelineTrackType::CameraShake:
		{
			const auto& shake = std::get<VansTimelineCameraShakeTrackConfig>(track->config);
			if (shake.position) addChannel("PositionOffset", VansTimelineChannelType::Vec3);
			if (shake.rotation) addChannel("RotationOffset", VansTimelineChannelType::Vec3);
			break;
		}
		case VansTimelineTrackType::FadePostProcess: addChannel("Opacity", VansTimelineChannelType::Float); break;
		case VansTimelineTrackType::Light: addChannel("Intensity", VansTimelineChannelType::Float); break;
		case VansTimelineTrackType::MaterialParameter:
			addChannel("Value", std::get<VansTimelineMaterialParameterTrackConfig>(track->config).parameterType); break;
		case VansTimelineTrackType::UIState: addChannel("Value", VansTimelineChannelType::Bool); break;
		case VansTimelineTrackType::EventSignal: addChannel("Event", VansTimelineChannelType::EventPayload); break;
		case VansTimelineTrackType::TimeScale: addChannel("Scale", VansTimelineChannelType::Float); break;
		default: break;
		}
	}
	const VansTimelineId id = section.id;
	track->sections.push_back(std::move(section));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::AddKey(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	std::size_t channelIndex,
	VansTimelineKey key)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || channelIndex >= section->channels.size()) return Failure("Timeline channel does not exist");
	if (key.id.empty()) key.id = NewStableId();
	const VansTimelineId id = key.id;
	section->channels[channelIndex].keys.push_back(std::move(key));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::MoveKey(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	std::size_t channelIndex,
	VansTimelineId keyId,
	VansTimelineTick tick)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || channelIndex >= section->channels.size()) return Failure("Timeline channel does not exist");
	auto& keys = section->channels[channelIndex].keys;
	const auto key = std::find_if(keys.begin(), keys.end(), [&](const auto& item) { return item.id == keyId; });
	if (key == keys.end()) return Failure("Timeline key does not exist");
	key->tick = std::clamp(tick, VansTimelineTick{ 0 }, std::max<VansTimelineTick>(0, section->durationTicks - 1));
	return FinishMutation(std::move(keyId));
}

TimelineEditResult VansTimelineEditService::MoveKeysBy(
	const std::unordered_set<VansTimelineId>& keyIds,
	VansTimelineTick deltaTicks)
{
	if (keyIds.empty()) return Failure("Timeline key selection is empty");
	VansTimelineTick minimumDelta = std::numeric_limits<VansTimelineTick>::min();
	VansTimelineTick maximumDelta = std::numeric_limits<VansTimelineTick>::max();
	bool found = false;
	for (const auto& track : m_Asset.tracks)
		for (const auto& section : track.sections)
			for (const auto& channel : section.channels)
				for (const auto& key : channel.keys)
					if (keyIds.find(key.id) != keyIds.end())
					{
						minimumDelta = std::max(minimumDelta, -key.tick);
						maximumDelta = std::min(maximumDelta,
							std::max<VansTimelineTick>(0, section.durationTicks - 1) - key.tick);
						found = true;
					}
	if (!found) return Failure("Timeline key selection no longer exists");
	deltaTicks = std::clamp(deltaTicks, minimumDelta, maximumDelta);
	for (auto& track : m_Asset.tracks)
		for (auto& section : track.sections)
			for (auto& channel : section.channels)
				for (auto& key : channel.keys)
					if (keyIds.find(key.id) != keyIds.end()) key.tick += deltaTicks;
	return FinishMutation(*keyIds.begin());
}

TimelineEditResult VansTimelineEditService::DuplicateKeys(
	const std::unordered_set<VansTimelineId>& keyIds,
	VansTimelineTick offsetTicks)
{
	if (keyIds.empty() || offsetTicks == 0) return Failure("Timeline key duplication requires a non-zero offset");
	const VansTimelineAsset original = m_Asset;
	VansTimelineId lastId;
	bool found = false;
	for (auto& track : m_Asset.tracks)
		for (auto& section : track.sections)
			for (auto& channel : section.channels)
			{
				std::vector<VansTimelineKey> copies;
				for (const auto& key : channel.keys)
				{
					if (keyIds.find(key.id) == keyIds.end()) continue;
					VansTimelineKey copy = key;
					copy.id = NewStableId();
					copy.tick += offsetTicks;
					if (copy.tick < 0 || copy.tick >= section.durationTicks)
					{
						m_Asset = original;
						return Failure("Duplicated Timeline keys would leave their section range");
					}
					const bool collision = std::any_of(channel.keys.begin(), channel.keys.end(), [&](const auto& item)
					{
						return item.tick == copy.tick;
					}) || std::any_of(copies.begin(), copies.end(), [&](const auto& item)
					{
						return item.tick == copy.tick;
					});
					if (collision)
					{
						m_Asset = original;
						return Failure("Duplicated Timeline keys would collide at the same tick");
					}
					lastId = copy.id;
					copies.push_back(std::move(copy));
					found = true;
				}
				channel.keys.insert(channel.keys.end(),
					std::make_move_iterator(copies.begin()), std::make_move_iterator(copies.end()));
				std::stable_sort(channel.keys.begin(), channel.keys.end(), [](const auto& left, const auto& right)
				{
					return left.tick < right.tick;
				});
			}
	if (!found) return Failure("Timeline key selection no longer exists");
	return FinishMutation(lastId);
}

TimelineEditResult VansTimelineEditService::ScaleKeys(
	const std::unordered_set<VansTimelineId>& keyIds,
	double timeScale,
	double valueScale)
{
	if (keyIds.empty() || !std::isfinite(timeScale) || timeScale <= 0.0 ||
		!std::isfinite(valueScale))
		return Failure("Timeline key scale factors are invalid");
	VansTimelineTick minimumTick = std::numeric_limits<VansTimelineTick>::max();
	VansTimelineTick maximumTick = std::numeric_limits<VansTimelineTick>::min();
	double minimumValue = std::numeric_limits<double>::max();
	double maximumValue = std::numeric_limits<double>::lowest();
	bool found = false;
	for (const auto& track : m_Asset.tracks)
		for (const auto& section : track.sections)
			for (const auto& channel : section.channels)
				for (const auto& key : channel.keys)
					if (keyIds.find(key.id) != keyIds.end())
					{
						const VansTimelineTick globalTick = section.startTick + key.tick;
						minimumTick = std::min(minimumTick, globalTick);
						maximumTick = std::max(maximumTick, globalTick);
						double value = 0.0;
						if (NumericKeyValue(key.value, value))
						{
							minimumValue = std::min(minimumValue, value);
							maximumValue = std::max(maximumValue, value);
						}
						found = true;
					}
	if (!found) return Failure("Timeline key selection no longer exists");
	const VansTimelineAsset original = m_Asset;
	const double pivotTick = (static_cast<double>(minimumTick) + maximumTick) * 0.5;
	const double pivotValue = minimumValue == std::numeric_limits<double>::max()
		? 0.0 : (minimumValue + maximumValue) * 0.5;
	for (auto& track : m_Asset.tracks)
		for (auto& section : track.sections)
			for (auto& channel : section.channels)
			{
				for (auto& key : channel.keys)
					if (keyIds.find(key.id) != keyIds.end())
					{
						const double globalTick = section.startTick + key.tick;
						const auto scaledGlobal = static_cast<VansTimelineTick>(std::llround(
							pivotTick + (globalTick - pivotTick) * timeScale));
						key.tick = scaledGlobal - section.startTick;
						if (key.tick < 0 || key.tick >= section.durationTicks)
						{
							m_Asset = original;
							return Failure("Scaled Timeline keys would leave their section range");
						}
						ScaleNumericKeyValue(key.value, pivotValue, valueScale);
					}
				std::unordered_set<VansTimelineTick> ticks;
				for (const auto& key : channel.keys)
					if (!ticks.insert(key.tick).second)
					{
						m_Asset = original;
						return Failure("Scaled Timeline keys would collide at the same tick");
					}
				std::stable_sort(channel.keys.begin(), channel.keys.end(), [](const auto& left, const auto& right)
				{
					return left.tick < right.tick;
				});
			}
	return FinishMutation(*keyIds.begin());
}

TimelineEditResult VansTimelineEditService::SetKeysCurveMode(
	const std::unordered_set<VansTimelineId>& keyIds,
	VansTimelineInterpolation interpolation,
	VansTimelineTangentMode tangentMode)
{
	if (keyIds.empty()) return Failure("Timeline key selection is empty");
	bool found = false;
	for (auto& track : m_Asset.tracks)
		for (auto& section : track.sections)
			for (auto& channel : section.channels)
				for (auto& key : channel.keys)
					if (keyIds.find(key.id) != keyIds.end())
					{
						key.interpolation = interpolation;
						key.tangentMode = tangentMode;
						found = true;
					}
	return found ? FinishMutation(*keyIds.begin())
		: Failure("Timeline key selection no longer exists");
}

TimelineEditResult VansTimelineEditService::OffsetKeyValuesBy(
	const std::unordered_set<VansTimelineId>& keyIds,
	double deltaValue)
{
	if (keyIds.empty() || !std::isfinite(deltaValue))
		return Failure("Timeline numeric key selection is invalid");
	bool changed = false;
	for (auto& track : m_Asset.tracks)
		for (auto& section : track.sections)
			for (auto& channel : section.channels)
				for (auto& key : channel.keys)
					if (keyIds.find(key.id) != keyIds.end())
						changed |= OffsetNumericKeyValue(key.value, deltaValue);
	return changed ? FinishMutation(*keyIds.begin())
		: Failure("Timeline key selection has no numeric values");
}

TimelineEditResult VansTimelineEditService::SetKeyValue(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	std::size_t channelIndex,
	VansTimelineId keyId,
	VansTimelineKeyValue value)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || channelIndex >= section->channels.size()) return Failure("Timeline channel does not exist");
	auto& keys = section->channels[channelIndex].keys;
	const auto key = std::find_if(keys.begin(), keys.end(), [&](const auto& item) { return item.id == keyId; });
	if (key == keys.end()) return Failure("Timeline key does not exist");
	key->value = std::move(value);
	return FinishMutation(std::move(keyId));
}

TimelineEditResult VansTimelineEditService::SetKeyCurve(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	std::size_t channelIndex,
	VansTimelineId keyId,
	VansTimelineInterpolation interpolation,
	VansTimelineTangentMode tangentMode,
	double arriveTangent,
	double leaveTangent,
	double arriveWeight,
	double leaveWeight)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || channelIndex >= section->channels.size()) return Failure("Timeline channel does not exist");
	auto& keys = section->channels[channelIndex].keys;
	const auto key = std::find_if(keys.begin(), keys.end(), [&](const auto& item) { return item.id == keyId; });
	if (key == keys.end()) return Failure("Timeline key does not exist");
	key->interpolation = interpolation;
	key->tangentMode = tangentMode;
	key->arriveTangent = arriveTangent;
	key->leaveTangent = leaveTangent;
	key->arriveWeight = std::max(0.0, arriveWeight);
	key->leaveWeight = std::max(0.0, leaveWeight);
	return FinishMutation(std::move(keyId));
}

TimelineEditResult VansTimelineEditService::ReplaceChannelKeys(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	std::size_t channelIndex,
	std::vector<VansTimelineKey> keys)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || channelIndex >= section->channels.size())
		return Failure("Timeline channel does not exist");
	section->channels[channelIndex].keys = std::move(keys);
	return FinishMutation(section->channels[channelIndex].id);
}

TimelineEditResult VansTimelineEditService::MoveSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick startTick)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section) return Failure("Timeline section does not exist");
	section->startTick = std::clamp(startTick, VansTimelineTick{ 0 }, m_Asset.durationTicks - section->durationTicks);
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::MoveSectionsBy(
	const std::unordered_set<VansTimelineId>& sectionIds,
	VansTimelineTick deltaTicks)
{
	if (sectionIds.empty()) return Failure("Timeline multi-section move has no selection");
	std::vector<VansTimelineSection*> sections;
	for (VansTimelineTrack& track : m_Asset.tracks)
		for (VansTimelineSection& section : track.sections)
			if (sectionIds.find(section.id) != sectionIds.end()) sections.push_back(&section);
	if (sections.empty()) return Failure("Timeline multi-section selection does not exist");
	for (const VansTimelineSection* section : sections)
		if (section->startTick + deltaTicks < 0 ||
			section->startTick + section->durationTicks + deltaTicks > m_Asset.durationTicks)
			return Failure("Multi-section move would leave the Timeline duration");
	for (VansTimelineSection* section : sections) section->startTick += deltaTicks;
	return FinishMutation();
}

TimelineEditResult VansTimelineEditService::SlipSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick sourceDeltaTicks)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section) return Failure("Timeline section does not exist");
	const VansTimelineTick nextSourceIn = std::max<VansTimelineTick>(0, section->sourceInTick + sourceDeltaTicks);
	const VansTimelineTick appliedDelta = nextSourceIn - section->sourceInTick;
	section->sourceInTick = nextSourceIn;
	if (section->sourceOutTick >= 0)
		section->sourceOutTick += appliedDelta;
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::RippleMoveSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick startTick)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section) return Failure("Timeline section does not exist");
	const VansTimelineTick oldStart = section->startTick;
	const VansTimelineTick oldEnd = oldStart + section->durationTicks;
	const VansTimelineTick nextStart = std::clamp(startTick, VansTimelineTick{ 0 },
		m_Asset.durationTicks - section->durationTicks);
	const VansTimelineTick delta = nextStart - oldStart;
	for (const VansTimelineSection& candidate : track->sections)
	{
		if (candidate.id == sectionId || candidate.startTick < oldEnd) continue;
		if (candidate.startTick + delta < 0 || candidate.startTick + candidate.durationTicks + delta > m_Asset.durationTicks)
			return Failure("Ripple move would place a section outside the Timeline duration");
	}
	for (VansTimelineSection& candidate : track->sections)
		if (candidate.id == sectionId || candidate.startTick >= oldEnd)
			candidate.startTick += delta;
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::TrimSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick startTick,
	VansTimelineTick durationTicks)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || durationTicks <= 0) return Failure("Timeline section trim is invalid");
	const VansTimelineTick oldStart = section->startTick;
	section->startTick = std::clamp(startTick, VansTimelineTick{ 0 }, m_Asset.durationTicks - 1);
	section->durationTicks = std::min(durationTicks, m_Asset.durationTicks - section->startTick);
	section->sourceInTick = std::max<VansTimelineTick>(0,
		section->sourceInTick + section->startTick - oldStart);
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::RippleTrimSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick startTick,
	VansTimelineTick durationTicks)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || durationTicks <= 0) return Failure("Timeline ripple trim is invalid");
	const VansTimelineTick oldEnd = section->startTick + section->durationTicks;
	const VansTimelineTick oldStart = section->startTick;
	const VansTimelineTick nextStart = std::clamp(startTick, VansTimelineTick{ 0 }, m_Asset.durationTicks - 1);
	const VansTimelineTick nextDuration = std::min(durationTicks, m_Asset.durationTicks - nextStart);
	const VansTimelineTick nextEnd = nextStart + nextDuration;
	const VansTimelineTick rippleDelta = nextEnd - oldEnd;
	for (const VansTimelineSection& candidate : track->sections)
	{
		if (candidate.id == sectionId || candidate.startTick < oldEnd) continue;
		if (candidate.startTick + rippleDelta < 0 ||
			candidate.startTick + candidate.durationTicks + rippleDelta > m_Asset.durationTicks)
			return Failure("Ripple trim would place a section outside the Timeline duration");
	}
	section->startTick = nextStart;
	section->durationTicks = nextDuration;
	section->sourceInTick = std::max<VansTimelineTick>(0,
		section->sourceInTick + nextStart - oldStart);
	for (VansTimelineSection& candidate : track->sections)
		if (candidate.id != sectionId && candidate.startTick >= oldEnd)
			candidate.startTick += rippleDelta;
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::ScaleSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick durationTicks)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section) return Failure("Timeline section does not exist");
	if (m_InteractionSnapshot)
	{
		const auto baselineTrack = std::find_if(m_InteractionSnapshot->tracks.begin(), m_InteractionSnapshot->tracks.end(),
			[&](const auto& item) { return item.id == trackId; });
		if (baselineTrack != m_InteractionSnapshot->tracks.end())
		{
			const auto baselineSection = std::find_if(baselineTrack->sections.begin(), baselineTrack->sections.end(),
				[&](const auto& item) { return item.id == sectionId; });
			if (baselineSection != baselineTrack->sections.end()) *section = *baselineSection;
		}
	}
	const VansTimelineTick oldDuration = std::max<VansTimelineTick>(1, section->durationTicks);
	const VansTimelineTick nextDuration = std::clamp(durationTicks, VansTimelineTick{ 1 },
		m_Asset.durationTicks - section->startTick);
	const double scale = static_cast<double>(nextDuration) / static_cast<double>(oldDuration);
	for (VansTimelineChannel& channel : section->channels)
		for (VansTimelineKey& key : channel.keys)
			key.tick = std::clamp<VansTimelineTick>(
				static_cast<VansTimelineTick>(std::llround(key.tick * scale)), 0, nextDuration - 1);
	section->easeInTicks = std::clamp<VansTimelineTick>(
		static_cast<VansTimelineTick>(std::llround(section->easeInTicks * scale)), 0, nextDuration);
	section->easeOutTicks = std::clamp<VansTimelineTick>(
		static_cast<VansTimelineTick>(std::llround(section->easeOutTicks * scale)), 0, nextDuration);
	section->playRate *= static_cast<double>(oldDuration) / static_cast<double>(nextDuration);
	section->durationTicks = nextDuration;
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::LoopExtendSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick durationTicks)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section) return Failure("Timeline section does not exist");
	if (m_InteractionSnapshot)
	{
		const auto baselineTrack = std::find_if(m_InteractionSnapshot->tracks.begin(), m_InteractionSnapshot->tracks.end(),
			[&](const auto& item) { return item.id == trackId; });
		if (baselineTrack != m_InteractionSnapshot->tracks.end())
		{
			const auto baselineSection = std::find_if(baselineTrack->sections.begin(), baselineTrack->sections.end(),
				[&](const auto& item) { return item.id == sectionId; });
			if (baselineSection != baselineTrack->sections.end()) *section = *baselineSection;
		}
	}
	const VansTimelineTick oldDuration = std::max<VansTimelineTick>(1, section->durationTicks);
	section->durationTicks = std::clamp(durationTicks, VansTimelineTick{ 1 },
		m_Asset.durationTicks - section->startTick);
	section->loopMode = VansTimelineLoopMode::Loop;
	section->loopCount = std::max<std::int32_t>(1, static_cast<std::int32_t>(
		(section->durationTicks + oldDuration - 1) / oldDuration));
	return FinishMutation(sectionId);
}

TimelineEditResult VansTimelineEditService::SplitSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick splitTick)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section || splitTick <= section->startTick || splitTick >= section->startTick + section->durationTicks)
		return Failure("Timeline split tick must be inside the section");
	VansTimelineSection right = *section;
	right.id = NewStableId();
	right.name += " Split";
	right.durationTicks = section->startTick + section->durationTicks - splitTick;
	right.sourceInTick += splitTick - section->startTick;
	right.startTick = splitTick;
	section->durationTicks = splitTick - section->startTick;
	const VansTimelineTick splitLocalTick = splitTick - section->startTick;
	for (VansTimelineChannel& channel : section->channels)
		channel.keys.erase(std::remove_if(channel.keys.begin(), channel.keys.end(),
			[&](const VansTimelineKey& key) { return key.tick >= splitLocalTick; }), channel.keys.end());
	for (VansTimelineChannel& channel : right.channels)
	{
		channel.id = NewStableId();
		channel.keys.erase(std::remove_if(channel.keys.begin(), channel.keys.end(),
			[&](const VansTimelineKey& key) { return key.tick < splitLocalTick; }), channel.keys.end());
		for (VansTimelineKey& key : channel.keys)
		{
			key.id = NewStableId();
			key.tick -= splitLocalTick;
		}
	}
	const VansTimelineId id = right.id;
	track->sections.push_back(std::move(right));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::DuplicateSection(
	VansTimelineId trackId,
	VansTimelineId sectionId,
	VansTimelineTick offsetTicks)
{
	VansTimelineTrack* track = FindTrack(trackId);
	VansTimelineSection* section = track ? FindSection(*track, sectionId) : nullptr;
	if (!section) return Failure("Timeline section does not exist");
	return PasteSection(std::move(trackId), *section, section->startTick + offsetTicks);
}

TimelineEditResult VansTimelineEditService::PasteSection(
	VansTimelineId trackId,
	VansTimelineSection section,
	VansTimelineTick startTick)
{
	VansTimelineTrack* track = FindTrack(trackId);
	if (!track) return Failure("Timeline track does not exist");
	if (section.durationTicks <= 0 || section.durationTicks > m_Asset.durationTicks)
		return Failure("Timeline pasted section duration is invalid");
	RegenerateSectionIds(section);
	section.name = section.name.empty() ? track->name + " Copy" : section.name + " Copy";
	section.startTick = std::clamp(startTick, VansTimelineTick{ 0 }, m_Asset.durationTicks - section.durationTicks);
	const VansTimelineId id = section.id;
	track->sections.push_back(std::move(section));
	return FinishMutation(id);
}

TimelineEditResult VansTimelineEditService::MoveTrack(
	VansTimelineId trackId,
	VansTimelineId groupId,
	VansTimelineId beforeTrackId)
{
	if (!groupId.empty() && std::none_of(m_Asset.groups.begin(), m_Asset.groups.end(),
		[&](const auto& group) { return group.id == groupId; }))
		return Failure("Timeline target group does not exist");
	const auto source = std::find_if(m_Asset.tracks.begin(), m_Asset.tracks.end(),
		[&](const auto& track) { return track.id == trackId; });
	if (source == m_Asset.tracks.end()) return Failure("Timeline track does not exist");
	if (!beforeTrackId.empty())
	{
		const auto before = std::find_if(m_Asset.tracks.begin(), m_Asset.tracks.end(),
			[&](const auto& track) { return track.id == beforeTrackId; });
		if (before == m_Asset.tracks.end()) return Failure("Timeline reorder target does not exist");
		groupId = before->groupId;
	}
	VansTimelineTrack moved = std::move(*source);
	m_Asset.tracks.erase(source);
	moved.groupId = std::move(groupId);
	const auto target = beforeTrackId.empty() ? m_Asset.tracks.end() :
		std::find_if(m_Asset.tracks.begin(), m_Asset.tracks.end(),
			[&](const auto& track) { return track.id == beforeTrackId; });
	m_Asset.tracks.insert(target, std::move(moved));
	for (std::size_t index = 0; index < m_Asset.tracks.size(); ++index)
		m_Asset.tracks[index].order = static_cast<std::int32_t>(index);
	return FinishMutation(trackId);
}

TimelineEditResult VansTimelineEditService::SetTrackLocked(VansTimelineId trackId, bool locked)
{
	VansTimelineTrack* track = FindTrack(trackId);
	if (!track) return Failure("Timeline track does not exist");
	track->locked = locked;
	return FinishMutation(std::move(trackId));
}

TimelineEditResult VansTimelineEditService::RemoveObject(const VansTimelineId& objectId)
{
	for (VansTimelineTrack& track : m_Asset.tracks)
	{
		for (VansTimelineSection& section : track.sections)
		{
			for (VansTimelineChannel& channel : section.channels)
			{
				const auto key = std::remove_if(channel.keys.begin(), channel.keys.end(),
					[&](const auto& item) { return item.id == objectId; });
				if (key != channel.keys.end()) { channel.keys.erase(key, channel.keys.end()); return FinishMutation(); }
			}
			const auto channel = std::remove_if(section.channels.begin(), section.channels.end(),
				[&](const auto& item) { return item.id == objectId; });
			if (channel != section.channels.end()) { section.channels.erase(channel, section.channels.end()); return FinishMutation(); }
		}
		const auto section = std::remove_if(track.sections.begin(), track.sections.end(),
			[&](const auto& item) { return item.id == objectId; });
		if (section != track.sections.end()) { track.sections.erase(section, track.sections.end()); return FinishMutation(); }
	}
	const auto track = std::remove_if(m_Asset.tracks.begin(), m_Asset.tracks.end(),
		[&](const auto& item) { return item.id == objectId; });
	if (track != m_Asset.tracks.end()) { m_Asset.tracks.erase(track, m_Asset.tracks.end()); return FinishMutation(); }
	const auto group = std::remove_if(m_Asset.groups.begin(), m_Asset.groups.end(),
		[&](const auto& item) { return item.id == objectId; });
	if (group != m_Asset.groups.end()) { m_Asset.groups.erase(group, m_Asset.groups.end()); return FinishMutation(); }
	const auto binding = std::remove_if(m_Asset.bindings.begin(), m_Asset.bindings.end(),
		[&](const auto& item) { return item.id == objectId; });
	if (binding != m_Asset.bindings.end()) { m_Asset.bindings.erase(binding, m_Asset.bindings.end()); return FinishMutation(); }
	const auto marker = std::remove_if(m_Asset.markers.begin(), m_Asset.markers.end(),
		[&](const auto& item) { return item.id == objectId; });
	if (marker != m_Asset.markers.end()) { m_Asset.markers.erase(marker, m_Asset.markers.end()); return FinishMutation(); }
	return Failure("Timeline object does not exist");
}

TimelineEditResult VansTimelineEditService::ReplaceAsset(VansTimelineAsset asset)
{
	m_Asset = std::move(asset);
	return FinishMutation();
}

bool VansTimelineEditService::IsDirty() const
{
	return m_Document && (m_Document->IsDirty() || IsInteracting());
}

bool VansTimelineEditService::CanUndo() const
{
	return m_Document && VansAssetDocumentEditService::CanUndo(m_Document->sourceDocument);
}

bool VansTimelineEditService::CanRedo() const
{
	return m_Document && VansAssetDocumentEditService::CanRedo(m_Document->sourceDocument);
}
}
