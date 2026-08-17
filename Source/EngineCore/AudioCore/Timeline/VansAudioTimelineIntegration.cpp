#include "VansAudioTimelineIntegration.h"

#include "../VansAudioSourceBinding.h"
#include "../VansAudioManager.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelinePropertyAccessRegistry.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"

#include <algorithm>

namespace Vans
{
namespace
{
VansEngine::VansAudioSourceBinding* ResolveAudio(
	VansRuntimeWorld& world,
	const VansResolvedTimelineTarget& target)
{
	auto* storage = static_cast<VansComponentStorage<VansRuntimeAudioComponent>*>(
		world.FindStorage(VansRuntimeComponentType_Audio));
	if (!storage || !world.IsAlive(target.entity)) return nullptr;
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
		if (component.typeId == VansRuntimeComponentType_Audio)
			if (const auto* audio = storage->Get(component)) return audio->sourceBinding;
	return nullptr;
}

VansEngine::VansAudioSourceBinding* ResolveAudio(const VansTimelinePropertyAccessContext& context)
{
	return context.world ? ResolveAudio(*context.world, context.target) : nullptr;
}

#define VANS_AUDIO_FLOAT_PROPERTY(Name, Getter, Setter) \
	bool Read##Name(const VansTimelinePropertyAccessContext& context, VansTimelineValue& value, std::string& error) \
	{ auto* audio = ResolveAudio(context); if (!audio) { error = "Audio." #Name " requires a bound source"; return false; } value = static_cast<float>(audio->Getter()); return true; } \
	bool Write##Name(const VansTimelinePropertyAccessContext& context, const VansTimelineValue& value, std::string& error) \
	{ auto* audio = ResolveAudio(context); const auto* typed = std::get_if<float>(&value); if (!audio || !typed) { error = "Audio." #Name " target or value is invalid"; return false; } audio->Setter(*typed); return true; }

VANS_AUDIO_FLOAT_PROPERTY(Volume, GetVolume, SetVolume)
VANS_AUDIO_FLOAT_PROPERTY(Pitch, GetPitch, SetPitch)
VANS_AUDIO_FLOAT_PROPERTY(ReferenceDistance, GetRefDistance, SetRefDistance)
VANS_AUDIO_FLOAT_PROPERTY(MaxDistance, GetMaxDistance, SetMaxDistance)
VANS_AUDIO_FLOAT_PROPERTY(Rolloff, GetRolloff, SetRolloff)
VANS_AUDIO_FLOAT_PROPERTY(ReverbSend, GetReverbSend, SetReverbSend)
#undef VANS_AUDIO_FLOAT_PROPERTY

bool ReadAudioLoop(const VansTimelinePropertyAccessContext& context, VansTimelineValue& value, std::string& error)
{ auto* audio = ResolveAudio(context); if (!audio) { error = "Audio.Loop requires a bound source"; return false; } value = audio->GetLoop(); return true; }
bool WriteAudioLoop(const VansTimelinePropertyAccessContext& context, const VansTimelineValue& value, std::string& error)
{ auto* audio = ResolveAudio(context); const auto* typed = std::get_if<bool>(&value); if (!audio || !typed) { error = "Audio.Loop target or value is invalid"; return false; } audio->SetLoop(*typed); return true; }
bool ReadAudioSpatial(const VansTimelinePropertyAccessContext& context, VansTimelineValue& value, std::string& error)
{ auto* audio = ResolveAudio(context); if (!audio) { error = "Audio.Spatial requires a bound source"; return false; } value = audio->GetSpatial(); return true; }
bool WriteAudioSpatial(const VansTimelinePropertyAccessContext& context, const VansTimelineValue& value, std::string& error)
{ auto* audio = ResolveAudio(context); const auto* typed = std::get_if<bool>(&value); if (!audio || !typed) { error = "Audio.Spatial target or value is invalid"; return false; } audio->SetSpatial(*typed); return true; }
bool ReadAudioBus(const VansTimelinePropertyAccessContext& context, VansTimelineValue& value, std::string& error)
{ auto* audio = ResolveAudio(context); if (!audio) { error = "Audio.Bus requires a bound source"; return false; } value = audio->GetBusName(); return true; }
bool WriteAudioBus(const VansTimelinePropertyAccessContext& context, const VansTimelineValue& value, std::string& error)
{ auto* audio = ResolveAudio(context); const auto* typed = std::get_if<std::string>(&value); if (!audio || !typed) { error = "Audio.Bus target or value is invalid"; return false; } audio->SetBusName(*typed); return true; }

float Number(const VansTimelineValue* value, float fallback)
{
	if (!value) return fallback;
	if (const auto* number = std::get_if<float>(value)) return *number;
	if (const auto* number = std::get_if<double>(value)) return static_cast<float>(*number);
	if (const auto* number = std::get_if<std::int32_t>(value)) return static_cast<float>(*number);
	if (const auto* number = std::get_if<std::int64_t>(value)) return static_cast<float>(*number);
	return fallback;
}

class AudioTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	AudioTimelineApplier(VansRuntimeWorld& world, VansEngine::VansAudioManager& audioManager)
		: m_World(world), m_AudioManager(audioManager) {}
	VansTimelineOutputTypeId OutputType() const override
	{
		return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Audio) + ".Output");
	}
	std::string_view StableName() const override { return "Audio.AudioTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const VansTimelineSampleOutput* sample = view.As<VansTimelineSampleOutput>();
		if (!sample) return { VansTimelineApplyStatus::Failed, {}, "Audio output is invalid" };
		if (!context.section) return { VansTimelineApplyStatus::Failed, {}, "Audio section is unavailable" };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const auto stringAt = [&](std::size_t slot, std::string fallback = {})
		{
			const VansTimelineValue* value = reader.ValueAt(context.section->extensionData, slot);
			const auto* text = value ? std::get_if<std::string>(value) : nullptr;
			return text ? *text : std::move(fallback);
		};
		VansEngine::VansAudioSourceBinding* boundSource = ResolveAudio(m_World, target);
		const std::string sourceMode = stringAt(0, "SectionAsset");
		const std::string requestedSource = sourceMode == "SectionAsset" ? context.section->assetGuid
			: (boundSource ? boundSource->GetSourceName() : std::string{});
		if (requestedSource.empty() || !m_AudioManager.Get(requestedSource))
			return { VansTimelineApplyStatus::Failed, {}, "Audio section asset is not loaded" };
		const auto boolAt = [&](std::size_t slot, bool fallback)
		{
			const VansTimelineValue* value = reader.ValueAt(context.section->extensionData, slot);
			const auto* typed = value ? std::get_if<bool>(value) : nullptr;
			return typed ? *typed : fallback;
		};
		VansEngine::VansAudioOneShotRequest request;
		request.sourceName = requestedSource;
		request.volume = Number(reader.ValueAt(context.section->extensionData, 1), 1.0f);
		request.pitch = Number(reader.ValueAt(context.section->extensionData, 2), 1.0f);
		request.stereoPan = Number(reader.ValueAt(context.section->extensionData, 3), 0.0f);
		request.bus = stringAt(4, "SFX");
		request.spatial = boolAt(5, false);
		request.loop = boolAt(6, false);
		request.referenceDistance = Number(reader.ValueAt(context.section->extensionData, 7), 1.0f);
		request.maxDistance = Number(reader.ValueAt(context.section->extensionData, 8), 100.0f);
		request.rolloff = Number(reader.ValueAt(context.section->extensionData, 9), 1.0f);
		request.reverbSend = Number(reader.ValueAt(context.section->extensionData, 10), 0.0f);
		request.startSeconds = VansTimelineTime::TickToSeconds(sample->localTick, context.timeline.Timebase());
		if (request.spatial)
		{
			const auto* transformStorage = static_cast<VansComponentStorage<VansRuntimeTransformComponent>*>(
				m_World.FindStorage(VansRuntimeComponentType_Transform));
			if (transformStorage)
				for (VansComponentHandle component : m_World.CollectComponentsOwnedBy(target.entity))
					if (component.typeId == VansRuntimeComponentType_Transform)
						if (const auto* transform = transformStorage->Get(component))
							if (transform->transformStoreId < VansGraphics::VansTransformStore::GlobalTransforms.size())
							{
								const auto& position = VansGraphics::VansTransformStore::GetTransform(transform->transformStoreId).m_Position;
								request.positionX = position.x; request.positionY = position.y; request.positionZ = position.z;
							}
		}
		return m_AudioManager.PlayOneShot(request).IsValid()
			? VansTimelineApplyResult{ VansTimelineApplyStatus::Applied }
			: VansTimelineApplyResult{ VansTimelineApplyStatus::Failed, {}, "Audio one-shot playback could not start" };
	}
	bool Restore(VansTimelineRestoreToken) override { return false; }
	void ReleaseWriter(VansTimelineWriterHandle) override {}
	void ReleaseAll() override {}
private:
	VansRuntimeWorld& m_World;
	VansEngine::VansAudioManager& m_AudioManager;
};

void EvaluateAudioOneShot(VansTimelineExtensionEvaluationContext& context)
{
	if (!context.section) return;
	const bool discontinuous = context.traversal.reason == VansTimelineEvaluationReason::Jump ||
		context.traversal.reason == VansTimelineEvaluationReason::Scrub ||
		context.traversal.reason == VansTimelineEvaluationReason::Step || context.traversal.discontinuity;
	if (discontinuous || !VansTimelineEvaluator::Crossed(
		context.traversal, context.section->startTick)) return;
	VansTimelineSampleOutput payload;
	payload.timelineTick = context.traversal.currentTick;
	payload.localTick = context.section->sourceInTick;
	payload.loopIteration = context.traversal.loopIteration;
	payload.direction = static_cast<std::int8_t>(context.traversal.playbackDirection);
	payload.active = true;
	payload.entered = true;
	context.Emit(context.track.outputTypeId, VansInvalidTimelineRegistrySlot,
		payload, context.section->id, context.section->completionMode);
	context.outputs.back().retainsPreAnimatedState = false;
}
}

bool VansRegisterAudioTimelinePropertyAccessors(
	VansTimelinePropertyAccessRegistry& registry,
	std::string& error)
{
	auto add = [&](std::string name, VansTimelineValueType type,
		VansTimelinePropertyReadFn read, VansTimelinePropertyWriteFn write)
	{
		VansTimelinePropertyAccessDescriptor descriptor;
		descriptor.id = VansMakeStableId<VansTimelinePropertyAccessTag>(name);
		descriptor.stableName = std::move(name); descriptor.componentTypeId = VansRuntimeComponentType_Audio;
		descriptor.valueType = type; descriptor.read = read; descriptor.write = write;
		return registry.Register(std::move(descriptor), error);
	};
	return add("Audio.Volume", VansTimelineValueType::Float, ReadVolume, WriteVolume) &&
		add("Audio.Pitch", VansTimelineValueType::Float, ReadPitch, WritePitch) &&
		add("Audio.ReferenceDistance", VansTimelineValueType::Float, ReadReferenceDistance, WriteReferenceDistance) &&
		add("Audio.MaxDistance", VansTimelineValueType::Float, ReadMaxDistance, WriteMaxDistance) &&
		add("Audio.Rolloff", VansTimelineValueType::Float, ReadRolloff, WriteRolloff) &&
		add("Audio.ReverbSend", VansTimelineValueType::Float, ReadReverbSend, WriteReverbSend) &&
		add("Audio.Loop", VansTimelineValueType::Bool, ReadAudioLoop, WriteAudioLoop) &&
		add("Audio.Spatial", VansTimelineValueType::Bool, ReadAudioSpatial, WriteAudioSpatial) &&
		add("Audio.Bus", VansTimelineValueType::String, ReadAudioBus, WriteAudioBus);
}

bool VansRegisterAudioTimelineIntegration(
	VansRuntimeWorld& world,
	VansEngine::VansAudioManager& audioManager,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return registry.Register(std::make_shared<AudioTimelineApplier>(world, audioManager), error);
}

bool VansRegisterAudioTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = VansTimelineValueType;
	auto descriptor = VansMakeTimelinePointExtension(
		TimelineNames::Audio, "Audio", "Media", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required,
		{ { VansMakeTimelineSourceField("sourceMode", F::Enum, std::string("SectionAsset"), false,
				{ "SectionAsset", "BoundSource" }),
			VansMakeTimelineSourceField("volume", F::Float, 1.0f),
			VansMakeTimelineSourceField("pitch", F::Float, 1.0f),
			VansMakeTimelineSourceField("stereoPan", F::Float, 0.0f),
			VansMakeTimelineSourceField("bus", F::String, std::string("SFX")),
			VansMakeTimelineSourceField("spatial", F::Bool, false),
			VansMakeTimelineSourceField("loop", F::Bool, false),
			VansMakeTimelineSourceField("referenceDistance", F::Float, 1.0f),
			VansMakeTimelineSourceField("maxDistance", F::Float, 100.0f),
			VansMakeTimelineSourceField("rolloff", F::Float, 1.0f),
			VansMakeTimelineSourceField("reverbSend", F::Float, 0.0f) },
			{}, false, false });
	descriptor.evaluate = EvaluateAudioOneShot;
	descriptor.sectionAssetKind = "Audio";
	return registry.Register(std::move(descriptor), error);
}
}
