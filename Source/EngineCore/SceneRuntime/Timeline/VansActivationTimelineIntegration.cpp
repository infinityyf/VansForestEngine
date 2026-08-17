#include "VansActivationTimelineIntegration.h"

#include "../VansRuntimeWorld.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"

namespace Vans
{
namespace
{
struct ActivationRestoreState
{
	VansTimelineWriterHandle writer;
	VansEntityHandle entity;
	VansComponentHandle component;
	bool componentScope = false;
	bool previous = true;
};

class ActivationTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	explicit ActivationTimelineApplier(VansRuntimeWorld& world) : m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{
		return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Activation) + ".Output");
	}
	std::string_view StableName() const override { return "Scene.ActivationTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !context.section) return { VansTimelineApplyStatus::Failed, {}, "Activation output is invalid" };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const auto* scopeValue = reader.ValueAt(context.section->extensionData, 0);
		const auto* insideValue = reader.ValueAt(context.section->extensionData, 1);
		const auto* beforeValue = reader.ValueAt(context.section->extensionData, 2);
		const auto* afterValue = reader.ValueAt(context.section->extensionData, 3);
		const auto* scope = scopeValue ? std::get_if<std::string>(scopeValue) : nullptr;
		const auto* stateInside = insideValue ? std::get_if<bool>(insideValue) : nullptr;
		const auto* before = beforeValue ? std::get_if<std::string>(beforeValue) : nullptr;
		const auto* after = afterValue ? std::get_if<std::string>(afterValue) : nullptr;
		bool shouldApply = sample->active;
		bool active = stateInside ? *stateInside : true;
		if (!sample->active && sample->entered && before && *before != "Restore")
		{ shouldApply = true; active = *before == "Active"; }
		if (!sample->active && sample->exited && after && *after != "Restore")
		{ shouldApply = true; active = *after == "Active"; }
		if (!shouldApply) return { VansTimelineApplyStatus::Ignored };
		const bool componentScope = scope && *scope == "ComponentEnabled";
		if (componentScope && (!target.component.IsValid() || !m_World.GetComponentHeader(target.component)))
			return { VansTimelineApplyStatus::Failed, {}, "Component activation requires a live component binding" };
		const VansEntityRecord* entity = m_World.Entities().Get(target.entity);
		if (!componentScope && !entity)
			return { VansTimelineApplyStatus::Failed, {}, "Entity activation requires a live entity binding" };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			return ActivationRestoreState{ context.writer, target.entity, target.component,
				componentScope, componentScope ? m_World.IsComponentSelfEnabled(target.component) : entity->selfActive };
		});
		(void)state;
		if (componentScope) m_World.Commands().SetComponentEnabled(target.component, active);
		else m_World.Commands().SetEntityActive(target.entity, active);
		const VansTimelineResourceId resource{ VansStableHash64(componentScope ? "Scene.ComponentEnabled" : "Scene.EntityActive"),
			componentScope ? ((static_cast<std::uint64_t>(target.component.generation) << 32) |
				(static_cast<std::uint64_t>(target.component.typeId) << 16) | target.component.index + 1ull)
			: ((static_cast<std::uint64_t>(target.entity.generation) << 32) | target.entity.index + 1ull) };
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, resource } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		ActivationRestoreState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		if (state->componentScope)
		{
			if (m_World.GetComponentHeader(state->component))
				m_World.Commands().SetComponentEnabled(state->component, state->previous);
		}
		else if (m_World.IsAlive(state->entity)) m_World.Commands().SetEntityActive(state->entity, state->previous);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<ActivationRestoreState> m_State;
};
}

bool VansRegisterActivationTimelineExtension(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = VansTimelineValueType;
	return registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::Activation, "Activation", "Object",
		VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required,
		VansTimelineContinuousTrackFlags(false),
		{ { VansMakeTimelineSourceField("scope", F::Enum, std::string("EntityActive"), false,
				{ "EntityActive", "ComponentEnabled" }),
			VansMakeTimelineSourceField("stateWhenInside", F::Bool, true),
			VansMakeTimelineSourceField("stateBefore", F::Enum, std::string("Restore"), false,
				{ "Restore", "Active", "Inactive" }),
			VansMakeTimelineSourceField("stateAfter", F::Enum, std::string("Restore"), false,
				{ "Restore", "Active", "Inactive" }),
			VansMakeTimelineSourceField("useCommandBuffer", F::Bool, true) }, {}, false, false }), error);
}

bool VansRegisterActivationTimelineIntegration(
	VansRuntimeWorld& world,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return registry.Register(std::make_shared<ActivationTimelineApplier>(world), error);
}
}
