#include "VansParticleTimelineIntegration.h"

#include "../VansParticleRuntime.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
VansGraphics::VansParticleRuntime* ResolveParticle(
	VansRuntimeWorld& world, const VansResolvedTimelineTarget& target)
{
	auto* storage = static_cast<VansComponentStorage<VansRuntimeParticleComponent>*>(
		world.FindStorage(VansRuntimeComponentType_Particle));
	if (!storage || !world.IsAlive(target.entity)) return nullptr;
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
		if (component.typeId == VansRuntimeComponentType_Particle)
			if (const auto* runtime = storage->Get(component)) return runtime->runtime;
	return nullptr;
}

double Number(const VansTimelineValue* value, double fallback)
{
	if (const auto* typed = value ? std::get_if<float>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<double>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<std::int32_t>(value) : nullptr) return *typed;
	if (const auto* typed = value ? std::get_if<std::int64_t>(value) : nullptr) return static_cast<double>(*typed);
	return fallback;
}

struct ParticleRestoreState
{
	VansTimelineWriterHandle writer;
	VansGraphics::VansParticleRuntime* particle = nullptr;
	bool playing = false;
	float time = 0.0f;
	std::uint32_t seed = 0;
	float rate = 1.0f;
};

class ParticleTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	explicit ParticleTimelineApplier(VansRuntimeWorld& world) : m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Particle) + ".Output"); }
	std::string_view StableName() const override { return "Particle.ParticleTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		VansGraphics::VansParticleRuntime* particle = ResolveParticle(m_World, target);
		if (!sample || !context.section || !particle)
			return { VansTimelineApplyStatus::Failed, {}, "Particle binding is unavailable" };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			return ParticleRestoreState{ context.writer, particle, particle->IsPlaying(), particle->GetPlayTime(),
				particle->GetRandomSeed(), particle->GetSimulationRate() };
		});
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const auto* actionValue = reader.ValueAt(context.section->extensionData, 0);
		const auto* action = actionValue ? std::get_if<std::string>(actionValue) : nullptr;
		const auto* resetValue = reader.ValueAt(context.section->extensionData, 4);
		const auto* clearValue = reader.ValueAt(context.section->extensionData, 5);
		const auto* seekValue = reader.ValueAt(context.section->extensionData, 7);
		const auto* seekPolicy = seekValue ? std::get_if<std::string>(seekValue) : nullptr;
		if (sample->active)
		{
			if (sample->entered || sample->rebuild)
			{
				particle->SetRandomSeed(static_cast<std::uint32_t>(std::max(0.0,
					Number(reader.ValueAt(context.section->extensionData, 3), 0.0))));
				particle->SetSimulationRate(static_cast<float>(std::max(0.0,
					Number(reader.ValueAt(context.section->extensionData, 2), 1.0))));
				const auto* reset = resetValue ? std::get_if<bool>(resetValue) : nullptr;
				if (!reset || *reset) particle->Restart();
			}
			if (action && *action == "Stop") particle->Stop();
			else if (action && *action == "Pause") particle->Pause();
			else if (action && *action == "Burst")
			{
				if (sample->entered) particle->Burst();
				particle->Pause();
			}
			else
			{
				if (action && *action == "Restart" && sample->entered) particle->Restart();
				const double prewarm = VansTimelineTime::TickToSeconds(
					static_cast<VansTimelineTick>(Number(reader.ValueAt(context.section->extensionData, 1), 0.0)),
					context.timeline.Timebase());
				const float targetTime = static_cast<float>(std::max(0.0,
					VansTimelineTime::TickToSeconds(sample->localTick, context.timeline.Timebase()) + prewarm));
				if (seekPolicy && *seekPolicy == "DeterministicResimulate" &&
					(sample->entered || sample->rebuild || std::abs(particle->GetPlayTime() - targetTime) > 0.05f))
					particle->Seek(targetTime);
				particle->Play();
			}
		}
		else if (sample->exited)
		{
			const auto* clear = clearValue ? std::get_if<bool>(clearValue) : nullptr;
			(clear && *clear) ? particle->Stop() : particle->Pause();
		}
		const VansTimelineResourceId resource{ VansStableHash64("Particle.Runtime"),
			static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(particle)) };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		ParticleRestoreState* state = m_State.Resolve(token.handle);
		if (!state || !state->particle) return false;
		state->particle->SetRandomSeed(state->seed);
		state->particle->SetSimulationRate(state->rate);
		state->particle->Seek(state->time);
		state->playing ? state->particle->Play() : state->particle->Pause();
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<ParticleRestoreState> m_State;
};
}

bool VansRegisterParticleTimelineExtensions(VansTimelineTrackExtensionRegistry& registry, std::string& error)
{
	using F = VansTimelineValueType;
	return registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::Particle, "Particle", "FX", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required, VansTimelineContinuousTrackFlags(false),
		{ { VansMakeTimelineSourceField("action", F::Enum, std::string("Play"), false,
				{ "Play", "Pause", "Stop", "Restart", "Burst" }),
			VansMakeTimelineSourceField("prewarmTicks", F::Int64, std::int64_t{}),
			VansMakeTimelineSourceField("simulationRate", F::Double, 1.0),
			VansMakeTimelineSourceField("randomSeed", F::Int64, std::int64_t{}),
			VansMakeTimelineSourceField("resetOnEnter", F::Bool, true),
			VansMakeTimelineSourceField("clearOnExit", F::Bool, true),
			VansMakeTimelineSourceField("loop", F::Bool, false),
			VansMakeTimelineSourceField("seekPolicy", F::Enum, std::string("DeterministicResimulate"), false,
				{ "DeterministicResimulate", "Disabled" }) }, {}, false, false }), error);
}

bool VansRegisterParticleTimelineIntegration(VansRuntimeWorld& world,
	VansTimelineApplierRegistry& registry, std::string& error)
{
	return registry.Register(std::make_shared<ParticleTimelineApplier>(world), error);
}
}
