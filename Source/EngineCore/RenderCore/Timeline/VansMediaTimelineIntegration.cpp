#include "VansMediaTimelineIntegration.h"

#include "../VansVideoManager.h"
#include "../VulkanCore/VansVideoTexture.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"

#include <cmath>

namespace VansGraphics
{
namespace
{
VansVideoTexture* ResolveVideo(Vans::VansRuntimeWorld& world, VansVideoManager& manager,
	const Vans::VansResolvedTimelineTarget& target, const std::string& assetGuid)
{
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeVideoComponent>*>(
		world.FindStorage(Vans::VansRuntimeComponentType_Video));
	if (storage && world.IsAlive(target.entity))
		for (Vans::VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
			if (component.typeId == Vans::VansRuntimeComponentType_Video)
				if (const auto* runtime = storage->Get(component))
					if (runtime->videoTexture) return runtime->videoTexture;
	return assetGuid.empty() ? nullptr : manager.GetByAssetGuid(assetGuid);
}

std::string String(const Vans::VansTimelineCompiledDataReader& reader,
	const Vans::VansTimelineCompiledDataView& data, std::size_t slot)
{
	const auto* value = reader.ValueAt(data, slot);
	const auto* typed = value ? std::get_if<std::string>(value) : nullptr;
	return typed ? *typed : std::string{};
}

struct MediaRestoreState
{
	Vans::VansTimelineWriterHandle writer;
	VansVideoTexture* video = nullptr;
	bool playing = false;
	double time = 0.0;
	double rate = 1.0;
};

class MediaTimelineApplier final : public Vans::IVansTimelineOutputApplier
{
public:
	MediaTimelineApplier(Vans::VansRuntimeWorld& world, VansVideoManager& manager)
		: m_World(world), m_Manager(manager) {}
	Vans::VansTimelineOutputTypeId OutputType() const override
	{ return Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(std::string(Vans::TimelineNames::Media) + ".Output"); }
	std::string_view StableName() const override { return "Render.MediaTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(Vans::VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(Vans::VansTimelineSampleOutput); }
	Vans::VansTimelineApplyResult Apply(const Vans::VansTimelineApplyContext& context,
		const Vans::VansResolvedTimelineTarget& target, Vans::VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<Vans::VansTimelineSampleOutput>();
		if (!sample || !context.section) return { Vans::VansTimelineApplyStatus::Failed, {}, "Media output is invalid" };
		VansVideoTexture* video = ResolveVideo(m_World, m_Manager, target, context.section->assetGuid);
		if (!video) return { Vans::VansTimelineApplyStatus::Failed, {}, "Media video is unavailable" };
		const Vans::VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string syncMode = String(reader, context.section->extensionData, 0);
		if (!sample->active) return { Vans::VansTimelineApplyStatus::Ignored };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{ return MediaRestoreState{ context.writer, video, video->IsPlaying(),
			video->GetPlayTime(), video->GetPlaybackRate() }; });
		(void)state;
		const double seconds = Vans::VansTimelineTime::TickToSeconds(sample->localTick, context.timeline.Timebase());
		if (context.section->reverse)
			return { Vans::VansTimelineApplyStatus::Failed, {}, "Media reverse playback is unavailable" };
		if (syncMode == "TimelineClock")
		{
			video->SetPlaybackRate(0.0);
			if (!video->Seek(seconds)) return { Vans::VansTimelineApplyStatus::Failed, {}, "Media seek failed" };
			video->Pause();
		}
		else
		{
			video->SetPlaybackRate(std::abs(context.section->playRate));
			if ((sample->entered || sample->rebuild) && !video->Seek(seconds))
				return { Vans::VansTimelineApplyStatus::Failed, {}, "Media section start seek failed" };
			video->Play();
		}
		return { Vans::VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, { Vans::VansStableHash64("Media.Video"),
				static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(video)) } } };
	}
	bool Restore(Vans::VansTimelineRestoreToken token) override
	{
		MediaRestoreState* state = m_State.Resolve(token.handle);
		if (!state || !state->video) return false;
		state->video->SetPlaybackRate(state->rate);
		state->video->Seek(state->time);
		if (state->playing) state->video->Play();
		else state->video->Pause();
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(Vans::VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	Vans::VansRuntimeWorld& m_World;
	VansVideoManager& m_Manager;
	Vans::VansTimelineModuleApplierState<MediaRestoreState> m_State;
};
}

bool VansRegisterMediaTimelineExtension(Vans::VansTimelineTrackExtensionRegistry& registry, std::string& error)
{
	using F = Vans::VansTimelineValueType;
	auto descriptor = Vans::VansMakeTimelineSampleExtension(
		Vans::TimelineNames::Media, "Media", "Media", Vans::VansTimelineEvaluationPhase::PostScript,
		Vans::VansTimelineBindingRequirement::Required, Vans::VansTimelineContinuousTrackFlags(false),
		{ { Vans::VansMakeTimelineSourceField("syncMode", F::Enum, std::string("TimelineClock"), false,
				{ "TimelineClock", "FreeRun" }) }, {}, false, false });
	descriptor.flags = Vans::VansTimelineTrackFlags::Continuous |
		Vans::VansTimelineTrackFlags::SupportsSections |
		Vans::VansTimelineTrackFlags::SeekRebuildable |
		Vans::VansTimelineTrackFlags::Reversible |
		Vans::VansTimelineTrackFlags::Deterministic;
	descriptor.sectionAssetKind = "Video";
	return registry.Register(std::move(descriptor), error);
}

bool VansRegisterMediaTimelineIntegration(Vans::VansRuntimeWorld& world, VansVideoManager& manager,
	Vans::VansTimelineApplierRegistry& registry, std::string& error)
{
	return registry.Register(std::make_shared<MediaTimelineApplier>(world, manager), error);
}
}
