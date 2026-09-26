#include "VansParticleTimelineIntegration.h"

#include "../VansParticleManager.h"
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
VansGenerationHandle ResolveParticle(
	VansRuntimeWorld& world, const VansResolvedTimelineTarget& target)
{
	auto* storage = world.FindStorage<VansRuntimeParticleComponent>(
		VansRuntimeComponentType_Particle);
	if (!storage || !world.IsAlive(target.entity)) return {};
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
		if (component.typeId == VansRuntimeComponentType_Particle)
			if (const auto* runtime = storage->Get(component)) return runtime->instance;
	return {};
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
	VansGenerationHandle particle;
	bool playing = false;
	float time = 0.0f;
	std::uint32_t seed = 0;
	float rate = 1.0f;
};

class ParticleTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	ParticleTimelineApplier(VansRuntimeWorld& world, VansGraphics::VansParticleManager& particles) : m_World(world), m_Particles(particles) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Particle) + ".Output"); }
	std::string_view StableName() const override { return "Particle.ParticleTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		const auto handle = ResolveParticle(m_World, target);
        auto* particle = m_Particles.Resolve(handle);
		if (!sample || !context.section || !particle)
			return { VansTimelineApplyStatus::Failed, {}, "Particle binding is unavailable" };
        // 移动烟带没有历史锚点轨迹，无法承诺时间跳转或退出恢复；在写命令前明确拒绝。
		if (!particle->CanSeek())
			return { VansTimelineApplyStatus::Failed, {}, "Moving Ribbon requires recorded source poses for Timeline resimulation" };
		const ParticleRestoreState restoreState{ context.writer, handle, particle->IsPlaying(), particle->GetPlayTime(),
			particle->GetRandomSeed(), particle->GetSimulationRate() };
		std::vector<VansGraphics::VansParticleCommand> commands;
		const auto queue = [&](VansGraphics::VansParticleControl control, float value = 0.0f,
			std::uint32_t index = 0)
		{
			commands.push_back({ handle, control, value, index });
		};
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const auto* actionValue = reader.ValueAt(context.section->extensionData, 0);
		const auto* action = actionValue ? std::get_if<std::string>(actionValue) : nullptr;
		const auto* resetValue = reader.ValueAt(context.section->extensionData, 4);
		const auto* clearValue = reader.ValueAt(context.section->extensionData, 5);
		const auto* seekValue = reader.ValueAt(context.section->extensionData, 6);
		const auto* seekPolicy = seekValue ? std::get_if<std::string>(seekValue) : nullptr;
		if (sample->active)
		{
			if (sample->entered || sample->rebuild)
			{
				queue(VansGraphics::VansParticleControl::Seed, 0, static_cast<std::uint32_t>(std::max(0.0,
					Number(reader.ValueAt(context.section->extensionData, 3), 0.0))));
				queue(VansGraphics::VansParticleControl::SimulationRate, static_cast<float>(std::max(0.0,
					Number(reader.ValueAt(context.section->extensionData, 2), 1.0))));
				const auto* reset = resetValue ? std::get_if<bool>(resetValue) : nullptr;
				if (!reset || *reset) queue(VansGraphics::VansParticleControl::Restart);
			}
			if (action && *action == "Stop") queue(VansGraphics::VansParticleControl::Stop);
			else if (action && *action == "Pause") queue(VansGraphics::VansParticleControl::Pause);
			else if (action && *action == "Burst")
			{
				if (sample->entered) queue(VansGraphics::VansParticleControl::Burst, 0, 1);
				queue(VansGraphics::VansParticleControl::Pause);
			}
			else
			{
				if (action && *action == "Restart" && sample->entered) queue(VansGraphics::VansParticleControl::Restart);
				const double prewarm = VansTimelineTime::TickToSeconds(
					static_cast<VansTimelineTick>(Number(reader.ValueAt(context.section->extensionData, 1), 0.0)),
					context.timeline.Timebase());
				const float targetTime = static_cast<float>(std::max(0.0,
					VansTimelineTime::TickToSeconds(sample->localTick, context.timeline.Timebase()) + prewarm));
				if (seekPolicy && *seekPolicy == "DeterministicResimulate" &&
					(sample->entered || sample->rebuild || std::abs(particle->GetPlayTime() - targetTime) > 0.05f))
					queue(VansGraphics::VansParticleControl::Seek, targetTime);
				queue(VansGraphics::VansParticleControl::Play);
			}
		}
		else if (sample->exited)
		{
			const auto* clear = clearValue ? std::get_if<bool>(clearValue) : nullptr;
			queue(clear && *clear ? VansGraphics::VansParticleControl::Stop : VansGraphics::VansParticleControl::Pause);
		}
		if (!m_Particles.QueueBatch(commands))
			return { VansTimelineApplyStatus::Failed, {}, "Particle command batch was rejected" };
		const VansTimelineRestoreHandle restore =
			m_State.Acquire(context.writer, [&] { return restoreState; }).first;
		const VansTimelineResourceId resource{ VansStableHash64("Particle.Runtime"),
			(static_cast<std::uint64_t>(handle.generation) << 32) | handle.index };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		ParticleRestoreState* state = m_State.Resolve(token.handle);
        if (!state) return false;
        const auto* particle = m_Particles.Resolve(state->particle);
        if (!particle) return m_State.Release(token.handle);
        if (!particle->CanSeek()) { m_State.Release(token.handle); return false; }
        const bool queued = m_Particles.QueueBatch({
            { state->particle, VansGraphics::VansParticleControl::Seed, 0.0f, state->seed },
            { state->particle, VansGraphics::VansParticleControl::SimulationRate, state->rate, 0 },
            { state->particle, VansGraphics::VansParticleControl::Seek, state->time, 0 },
            { state->particle, state->playing ? VansGraphics::VansParticleControl::Play : VansGraphics::VansParticleControl::Pause, 0.0f, 0 }
        });
		const bool released = m_State.Release(token.handle);
		return queued && released;
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
    VansGraphics::VansParticleManager& m_Particles;
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
			VansMakeTimelineSourceField("seekPolicy", F::Enum, std::string("DeterministicResimulate"), false,
				{ "DeterministicResimulate", "Disabled" }) }, {}, false, false }), error);
}

bool VansRegisterParticleTimelineIntegration(VansRuntimeWorld& world, VansGraphics::VansParticleManager& particles,
	VansTimelineApplierRegistry& registry, std::string& error)
{
	return registry.Register(std::make_shared<ParticleTimelineApplier>(world, particles), error);
}
}
