#include "VansGameplayActionTimelineIntegration.h"

#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../TimelineCore/VansTimelineDependencyBuilder.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../TimelineRuntime/VansTimelineEvaluator.h"
#include "../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../TimelineRuntime/VansTimelineSampleExtension.h"
#include "../TimelineRuntime/VansTimelineRuntimeSystem.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace Vans
{
VansTimelineSessionScope VansMakeExactActionTimelineScope(VansActionHandle action)
{
	return { "Gameplay.Action.Exact", action.value };
}

bool VansRegisterTimelineGAFTypes(VansGAFTypeRegistry& registry, std::string& error)
{
	VansGAFTypeDescriptor descriptor;
	descriptor.typeId = "Timeline.Driver.Session";
	descriptor.displayName = descriptor.typeId;
	descriptor.kind = VansGAFExtensionKind::Driver;
	return registry.RegisterType(std::move(descriptor), error);
}

bool VansRegisterTimelineGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error)
{
	VansGAFInputSchemaDescriptor descriptor;
	descriptor.typeId = "Timeline.Driver.Session";
	descriptor.fields = {
		{ "timeline", "Core.Value.Reference", false, VansSerializedValue::Null() },
		{ "timelines", "Core.Value.Array", false, VansSerializedValue::Array({}) }
	};
	return registry.Register(std::move(descriptor), error);
}

namespace
{
constexpr const char* EventTrack = "Action.Event";
constexpr const char* WindowTrack = "Action.Window";
constexpr const char* CueTrack = "Action.Cue";
constexpr const char* ParameterTrack = "Action.Parameter";
constexpr const char* SubActionTrack = "Action.SubAction";
constexpr const char* MarkerTrack = "Action.Marker";

std::string AssetReference(const VansSerializedValue* value)
{
	if (!value) return {};
	if (value->kind == VansSerializedValue::Kind::String) return value->stringValue;
	if (value->kind != VansSerializedValue::Kind::Object) return {};
	for (const char* field : { "assetGuid", "stableId", "pathHint" })
	{
		const std::string reference = ReadSerializedStringField(*value, field);
		if (!reference.empty()) return reference;
	}
	return {};
}

class TimelineSessionActionDriver final : public IVansActionSidecarDriver
{
public:
	TimelineSessionActionDriver(
		VansTimelineRuntimeSystem& runtime,
		const VansCompiledActionRecord& record)
		: m_Runtime(runtime)
	{
		if (const std::string primary = AssetReference(
			FindObjectField(record.inputs, "timeline")); !primary.empty())
			m_Assets.push_back(primary);
		if (const VansSerializedValue* values = FindObjectField(record.inputs, "timelines");
			values && values->kind == VansSerializedValue::Kind::Array)
			for (const VansSerializedValue& value : values->arrayItems)
				if (const std::string reference = AssetReference(&value); !reference.empty())
					m_Assets.push_back(reference);
	}

	bool Start(VansActionExecutionContext& context, std::string& error) override
	{
		if (m_Assets.empty())
		{
			error = "Timeline.Driver.Session requires at least one Timeline asset";
			return false;
		}
		m_Owner = context.owner;
		m_Action = context.action;
		if (!m_Runtime.IsReadyForActionSessions()) return true;
		return StartSessions(error);
	}

	bool Tick(VansActionExecutionContext& context, std::string& error) override
	{
		if (!m_Started)
		{
			if (!m_Runtime.IsReadyForActionSessions()) return true;
			if (!StartSessions(error)) return false;
		}
		for (VansTimelineSessionHandle session : m_Sessions)
		{
			m_Runtime.Sessions().Advance(session, context.deltaSeconds);
			m_Runtime.Sessions().Evaluate(session, VansTimelineEvaluationPhase::PostScript);
			const auto view = m_Runtime.Sessions().Query(session);
			if (!view)
			{
				error = "Timeline Action Session became stale";
				return false;
			}
			if (view->state == VansTimelinePlayerState::Error)
			{
				error = "Timeline Action Session failed";
				return false;
			}
		}
		return true;
	}

	void Finish(VansActionExecutionContext&, VansActionEndReason reason) override
	{
		ReleaseAll(reason == VansActionEndReason::Completed
			? VansTimelineEndReason::Completed : VansTimelineEndReason::Stopped);
	}

	std::string_view StableName() const override { return "Timeline.Driver.Session"; }

private:
	bool StartSessions(std::string& error)
	{
		for (const std::string& asset : m_Assets)
		{
			VansTimelineSessionResult result = m_Runtime.CreateActionSession(
				asset, m_Owner, VansMakeExactActionTimelineScope(m_Action));
			if (!result || !m_Runtime.Sessions().Play(result.handle))
			{
				error = result.error.empty()
					? "Timeline Action Session could not start" : std::move(result.error);
				ReleaseAll(VansTimelineEndReason::Failed);
				return false;
			}
			m_Sessions.push_back(result.handle);
		}
		m_Started = true;
		return true;
	}
	void ReleaseAll(VansTimelineEndReason reason)
	{
		for (VansTimelineSessionHandle session : m_Sessions)
		{
			m_Runtime.Sessions().Stop(session, reason);
			m_Runtime.Sessions().Release(session);
		}
		m_Sessions.clear();
	}

	VansTimelineRuntimeSystem& m_Runtime;
	std::vector<std::string> m_Assets;
	std::vector<VansTimelineSessionHandle> m_Sessions;
	VansEntityHandle m_Owner;
	VansActionHandle m_Action;
	bool m_Started = false;
};

VansTimelineOutputTypeId OutputType(std::string_view track)
{
	return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(track) + ".Output");
}

const VansTimelineValue* ValueAt(
	const VansTimelineApplyContext& context,
	std::size_t slot)
{
	if (!context.section) return nullptr;
	const VansTimelineCompiledDataReader reader(
		context.timeline.CompiledBytes(), context.timeline.CompiledValues());
	return reader.ValueAt(context.section->extensionData, slot);
}

std::string StringAt(const VansTimelineApplyContext& context, std::size_t slot)
{
	const VansTimelineValue* value = ValueAt(context, slot);
	const auto* text = value ? std::get_if<std::string>(value) : nullptr;
	return text ? *text : std::string{};
}

double NumberAt(const VansTimelineApplyContext& context, std::size_t slot, double fallback)
{
	const VansTimelineValue* value = ValueAt(context, slot);
	if (!value) return fallback;
	return std::visit([fallback](const auto& item) -> double
	{
		using T = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, float> || std::is_same_v<T, double>) return static_cast<double>(item);
		return fallback;
	}, *value);
}

VansSerializedValue PayloadAt(const VansTimelineApplyContext& context, std::size_t slot)
{
	const VansTimelineValue* value = ValueAt(context, slot);
	const auto* payload = value ? std::get_if<VansTimelineStructValue>(value) : nullptr;
	return payload ? payload->value : VansSerializedValue::Object({});
}

std::shared_ptr<VansActionHost> ResolveHost(
	VansGameplayRuntime& gameplay,
	const VansResolvedTimelineTarget& target)
{
	if (target.entity.IsValid())
		if (auto host = gameplay.FindHost(target.entity)) return host;
	return target.rootOwner.IsValid() ? gameplay.FindHost(target.rootOwner) : nullptr;
}

std::vector<VansActionHandle> MatchingActions(
	const VansActionHost& host,
	const VansTimelineApplyContext& context,
	std::string_view actionName,
	std::string_view actionScope)
{
	const VansActionId filter = actionName.empty()
		? VansActionId{} : VansMakeStableId<VansActionIdTag>(actionName);
	std::vector<VansActionHandle> result;
	if (context.scope && context.scope->type == "Gameplay.Action.Exact")
	{
		const VansActionHandle exact{ context.scope->handle };
		const auto instance = host.Query(exact);
		if (instance && (!filter || instance->action == filter)) result.push_back(exact);
		return result;
	}
	if (actionScope != "HostQuery") return result;
	for (const VansActionInstanceSnapshot& instance : host.ActiveActions())
		if (!filter || instance.action == filter) result.push_back(instance.handle);
	return result;
}

std::uint64_t ActionCorrelation(VansActionHandle action)
{
	return (static_cast<std::uint64_t>(action.value.generation) << 32u) |
		(static_cast<std::uint64_t>(action.value.index) + 1ull);
}

bool SendEvent(
	VansActionHost& host,
	const std::vector<VansActionHandle>& actions,
	std::string name,
	VansSerializedValue payload,
	std::string& error)
{
	if (name.empty()) { error = "Timeline Action event name is empty"; return false; }
	for (VansActionHandle action : actions)
	{
		VansActionEvent event;
		event.type = VansMakeStableId<VansActionFieldIdTag>(name);
		event.stableName = name;
		event.source = host.Owner();
		event.payload = payload;
		if (!host.EnqueueEvent(action, std::move(event), error)) return false;
	}
	return true;
}

VansTimelineApplyResult Failed(std::string error)
{
	return { VansTimelineApplyStatus::Failed, {}, std::move(error) };
}

VansTimelineResourceId Resource(std::string_view type, VansEntityHandle owner,
	std::uint64_t detail = 0)
{
	std::uint64_t instance = (static_cast<std::uint64_t>(owner.generation) << 32u) |
		(static_cast<std::uint64_t>(owner.index) + 1ull);
	instance ^= detail + 0x9e3779b97f4a7c15ull + (instance << 6u) + (instance >> 2u);
	return { VansStableHash64(type), instance == 0 ? 1 : instance };
}

class EventApplier final : public IVansTimelineOutputApplier
{
public:
	explicit EventApplier(VansGameplayRuntime& gameplay) : m_Gameplay(gameplay) {}
	VansTimelineOutputTypeId OutputType() const override { return ::Vans::OutputType(EventTrack); }
	std::string_view StableName() const override { return "GAF.EventTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<VansTimelineSampleOutput>();
		if (!sample || !sample->entered || context.sessionKind == VansTimelineSessionKind::Preview)
			return { VansTimelineApplyStatus::Ignored };
		auto host = ResolveHost(m_Gameplay, target);
		if (!host) return Failed("Timeline Action.Event requires a live ActionHost binding");
		std::string error;
		if (!SendEvent(*host, MatchingActions(*host, context,
			StringAt(context, 0), StringAt(context, 3)),
			StringAt(context, 1), PayloadAt(context, 2), error)) return Failed(std::move(error));
		return { VansTimelineApplyStatus::Applied };
	}
	bool Restore(VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(VansTimelineWriterHandle) override {}
	void ReleaseAll() override {}
private:
	VansGameplayRuntime& m_Gameplay;
};

struct WindowState
{
	VansTimelineWriterHandle writer;
	std::weak_ptr<VansActionHost> host;
	std::vector<VansActionHandle> actions;
	std::string closeEvent;
	VansSerializedValue payload;
	bool closed = false;
};

class WindowApplier final : public IVansTimelineOutputApplier
{
public:
	explicit WindowApplier(VansGameplayRuntime& gameplay) : m_Gameplay(gameplay) {}
	VansTimelineOutputTypeId OutputType() const override { return ::Vans::OutputType(WindowTrack); }
	std::string_view StableName() const override { return "GAF.WindowTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<VansTimelineSampleOutput>();
		if (!sample || context.sessionKind == VansTimelineSessionKind::Preview)
			return { VansTimelineApplyStatus::Ignored };
		auto host = ResolveHost(m_Gameplay, target);
		if (!host) return Failed("Timeline Action.Window requires a live ActionHost binding");
		const std::string window = StringAt(context, 1);
		const std::string prefix = "Action.Window." + window;
		const VansSerializedValue eventPayload = PayloadAt(context, 2);
		const auto actions = MatchingActions(
			*host, context, StringAt(context, 0), StringAt(context, 3));
		std::string error;
		if (sample->entered && sample->exited && !sample->active)
		{
			if (!SendEvent(*host, actions, prefix + ".Open", eventPayload, error) ||
				!SendEvent(*host, actions, prefix + ".Close", eventPayload, error))
				return Failed(std::move(error));
			return { VansTimelineApplyStatus::Applied };
		}
		if (!sample->active) return { VansTimelineApplyStatus::Ignored };
		const bool created = m_State.ResolveWriter(context.writer) == nullptr;
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			return WindowState{ context.writer, host, actions, prefix + ".Close", eventPayload, false };
		});
		if ((sample->entered || created) &&
			!SendEvent(*host, actions, prefix + ".Open", eventPayload, error))
			return Failed(std::move(error));
		if (!SendEvent(*host, actions, prefix + ".Update", eventPayload, error))
			return Failed(std::move(error));
		return { VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, Resource("GAF.Window", host->Owner(), VansStableHash64(window)) } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		WindowState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		Close(*state);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override
	{
		if (WindowState* state = m_State.ResolveWriter(writer)) Close(*state);
		m_State.ReleaseWriter(writer);
	}
	void ReleaseAll() override
	{
		m_State.ForEach([&](WindowState& state) { Close(state); });
		m_State.Clear();
	}
private:
	static void Close(WindowState& state)
	{
		if (state.closed) return;
		if (auto host = state.host.lock())
		{
			std::string ignored;
			SendEvent(*host, state.actions, state.closeEvent, state.payload, ignored);
		}
		state.closed = true;
	}
	VansGameplayRuntime& m_Gameplay;
	VansTimelineModuleApplierState<WindowState> m_State;
};

VansGameplayCueScope CueScope(std::string_view value)
{
	if (value == "Target") return VansGameplayCueScope::Target;
	if (value == "Observers") return VansGameplayCueScope::Observers;
	if (value == "World") return VansGameplayCueScope::World;
	if (value == "LocalOnly") return VansGameplayCueScope::LocalOnly;
	return VansGameplayCueScope::Owner;
}

struct CueState
{
	VansTimelineWriterHandle writer;
	std::weak_ptr<VansActionHost> host;
	std::vector<VansCueHandle> cues;
};

class CueApplier final : public IVansTimelineOutputApplier
{
public:
	explicit CueApplier(VansGameplayRuntime& gameplay) : m_Gameplay(gameplay) {}
	VansTimelineOutputTypeId OutputType() const override { return ::Vans::OutputType(CueTrack); }
	std::string_view StableName() const override { return "GAF.CueTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<VansTimelineSampleOutput>();
		if (!sample || context.sessionKind == VansTimelineSessionKind::Preview)
			return { VansTimelineApplyStatus::Ignored };
		auto host = ResolveHost(m_Gameplay, target);
		if (!host) return Failed("Timeline Action.Cue requires a live ActionHost binding");
		const VansCueId cue = VansMakeStableId<VansCueIdTag>(StringAt(context, 1));
		const std::string mode = StringAt(context, 2);
		VansGameplayCueParameters parameters;
		parameters.context.SetEntity(VansActionContextSlots::Owner, host->Owner());
		parameters.target = target.entity;
		parameters.payload = PayloadAt(context, 4);
		parameters.intensity = NumberAt(context, 5, 1.0);
		const std::vector<VansActionHandle> actions =
			MatchingActions(*host, context, StringAt(context, 0), StringAt(context, 6));
		std::string error;
		if (mode == "Execute")
		{
			if (!sample->entered) return { VansTimelineApplyStatus::Ignored };
			for (std::size_t index = 0; index < actions.size(); ++index)
			{
				const VansGameplayCueKey key{ ActionCorrelation(actions[index]), cue,
					static_cast<std::uint32_t>(((context.order.sequence + index) % UINT32_MAX) + 1u) };
				if (!host->Cues().Execute(key, CueScope(StringAt(context, 3)), parameters, error))
					return Failed(std::move(error));
			}
			return { VansTimelineApplyStatus::Applied };
		}
		if (!sample->active) return { VansTimelineApplyStatus::Ignored };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			CueState created{ context.writer, host, {} };
			for (std::size_t index = 0; index < actions.size(); ++index)
			{
				const VansGameplayCueKey key{ ActionCorrelation(actions[index]), cue,
					static_cast<std::uint32_t>(((context.order.sequence + index) % UINT32_MAX) + 1u) };
				const VansCueHandle handle = host->Cues().Add(key,
					CueScope(StringAt(context, 3)), parameters,
					VansTimelineHandleKey(context.writer), error);
				if (handle) created.cues.push_back(handle);
				if (!error.empty()) break;
			}
			return created;
		});
		if (!error.empty()) { m_State.Release(restore); return Failed(std::move(error)); }
		for (VansCueHandle handle : state->cues)
			if (!host->Cues().Update(handle, parameters, error)) return Failed(std::move(error));
		return { VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, Resource("GAF.Cue", host->Owner(), cue.value) } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		CueState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		Remove(*state);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override
	{
		if (CueState* state = m_State.ResolveWriter(writer)) Remove(*state);
		m_State.ReleaseWriter(writer);
	}
	void ReleaseAll() override
	{
		m_State.ForEach([&](CueState& state) { Remove(state); });
		m_State.Clear();
	}
private:
	static void Remove(CueState& state)
	{
		if (auto host = state.host.lock())
		{
			std::string ignored;
			for (VansCueHandle handle : state.cues) host->Cues().Remove(handle, ignored);
		}
		state.cues.clear();
	}
	VansGameplayRuntime& m_Gameplay;
	VansTimelineModuleApplierState<CueState> m_State;
};

struct ParameterPrevious
{
	VansActionHandle action;
	VansSerializedValue value;
};

struct ParameterState
{
	VansTimelineWriterHandle writer;
	std::weak_ptr<VansActionHost> host;
	VansActionFieldId variable;
	std::vector<ParameterPrevious> previous;
};

class ParameterApplier final : public IVansTimelineOutputApplier
{
public:
	explicit ParameterApplier(VansGameplayRuntime& gameplay) : m_Gameplay(gameplay) {}
	VansTimelineOutputTypeId OutputType() const override { return ::Vans::OutputType(ParameterTrack); }
	std::string_view StableName() const override { return "GAF.ParameterTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section || context.section->channels.empty() ||
			context.sessionKind == VansTimelineSessionKind::Preview)
			return { VansTimelineApplyStatus::Ignored };
		auto host = ResolveHost(m_Gameplay, target);
		if (!host) return Failed("Timeline Action.Parameter requires a live ActionHost binding");
		const auto sampled = VansTimelineEvaluator::SampleChannel(
			context.section->channels.front(), sample->localTick);
		if (!sampled) return { VansTimelineApplyStatus::Ignored };
		const VansActionFieldId variable =
			VansMakeStableId<VansActionFieldIdTag>(StringAt(context, 1));
		const std::vector<VansActionHandle> actions =
			MatchingActions(*host, context, StringAt(context, 0), StringAt(context, 3));
		std::string error;
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			ParameterState created{ context.writer, host, variable, {} };
			for (VansActionHandle action : actions)
			{
				VansSerializedValue previous;
				if (host->ReadVariable(action, variable, previous, error))
					created.previous.push_back({ action, std::move(previous) });
				else break;
			}
			return created;
		});
		if (!error.empty()) { m_State.Release(restore); return Failed(std::move(error)); }
		const VansSerializedValue value = VansTimelineEncodeSourceValue(*sampled);
		for (VansActionHandle action : actions)
			if (!host->WriteVariable(action, variable, value, error)) return Failed(std::move(error));
		return { VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, Resource("GAF.Parameter", host->Owner(), variable.value) } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		ParameterState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		RestoreState(*state);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override
	{
		if (ParameterState* state = m_State.ResolveWriter(writer)) RestoreState(*state);
		m_State.ReleaseWriter(writer);
	}
	void ReleaseAll() override
	{
		m_State.ForEach([&](ParameterState& state) { RestoreState(state); });
		m_State.Clear();
	}
private:
	static void RestoreState(ParameterState& state)
	{
		if (auto host = state.host.lock())
		{
			std::string ignored;
			for (const ParameterPrevious& item : state.previous)
				host->WriteVariable(item.action, state.variable, item.value, ignored);
		}
		state.previous.clear();
	}
	VansGameplayRuntime& m_Gameplay;
	VansTimelineModuleApplierState<ParameterState> m_State;
};

class SubActionApplier final : public IVansTimelineOutputApplier
{
public:
	explicit SubActionApplier(VansGameplayRuntime& gameplay) : m_Gameplay(gameplay) {}
	VansTimelineOutputTypeId OutputType() const override { return ::Vans::OutputType(SubActionTrack); }
	std::string_view StableName() const override { return "GAF.SubActionTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<VansTimelineSampleOutput>();
		if (!sample || !sample->entered || context.sessionKind == VansTimelineSessionKind::Preview)
			return { VansTimelineApplyStatus::Ignored };
		auto host = ResolveHost(m_Gameplay, target);
		if (!host) return Failed("Timeline Action.SubAction requires a live ActionHost binding");
		VansActionContext actionContext;
		actionContext.SetEntity(VansActionContextSlots::Owner, host->Owner());
		actionContext.SetEntity(VansActionContextSlots::Instigator, host->Owner());
		actionContext.SetEntity(VansActionContextSlots::Source, host->Owner());
		actionContext.SetSerialized(VansActionContextSlots::Payload, PayloadAt(context, 1));
		const VansActionResult result = host->ActivateAction(
			VansMakeStableId<VansActionIdTag>(StringAt(context, 0)), std::move(actionContext));
		if (!result && StringAt(context, 2) != "Ignore")
			return Failed(result.message.empty() ? "Timeline SubAction activation failed" : result.message);
		return result ? VansTimelineApplyResult{ VansTimelineApplyStatus::Applied }
			: VansTimelineApplyResult{ VansTimelineApplyStatus::Ignored };
	}
	bool Restore(VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(VansTimelineWriterHandle) override {}
	void ReleaseAll() override {}
private:
	VansGameplayRuntime& m_Gameplay;
};

class MarkerApplier final : public IVansTimelineOutputApplier
{
public:
	explicit MarkerApplier(VansGameplayRuntime& gameplay) : m_Gameplay(gameplay) {}
	VansTimelineOutputTypeId OutputType() const override { return ::Vans::OutputType(MarkerTrack); }
	std::string_view StableName() const override { return "GAF.MarkerTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView payload) override
	{
		const auto* sample = payload.As<VansTimelineSampleOutput>();
		if (!sample || !sample->entered || context.sessionKind == VansTimelineSessionKind::Preview)
			return { VansTimelineApplyStatus::Ignored };
		auto host = ResolveHost(m_Gameplay, target);
		if (!host) return Failed("Timeline Action.Marker requires a live ActionHost binding");
		std::string error;
		if (!SendEvent(*host, MatchingActions(*host, context,
			StringAt(context, 0), StringAt(context, 3)),
			"Action.Marker." + StringAt(context, 1), PayloadAt(context, 2), error))
			return Failed(std::move(error));
		return { VansTimelineApplyStatus::Applied };
	}
	bool Restore(VansTimelineRestoreToken) override { return true; }
	void ReleaseWriter(VansTimelineWriterHandle) override {}
	void ReleaseAll() override {}
private:
	VansGameplayRuntime& m_Gameplay;
};

const VansSerializedValue* SectionField(
	const VansTimelineTrack& track,
	const VansTimelineSection& section,
	const std::string& name)
{
	if (section.extensionData)
		if (const VansSerializedValue* value =
			VansTimelineFindSourceField(*section.extensionData, name)) return value;
	return VansTimelineFindSourceField(track.extensionData, name);
}

void ValidateIdentity(const VansTimelineTrack& track,
	const VansTimelineSourceSchema& schema,
	const VansTimelineValidationContext&,
	VansTimelineDiagnostics& diagnostics,
	const char* field)
{
	VansValidateTimelineExtensionSchema(track, schema, diagnostics);
	for (const VansTimelineSection& section : track.sections)
	{
		const VansSerializedValue* value = SectionField(track, section, field);
		if (!value || value->kind != VansSerializedValue::Kind::String || value->stringValue.empty())
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"GAF.TimelineIdentityMissing", {}, section.id, field,
				std::string("GAF Timeline section requires a non-empty ") + field });
	}
}

#define VANS_GAF_VALIDATE(name, field) \
	void name(const VansTimelineTrack& track, const VansTimelineSourceSchema& schema, \
		const VansTimelineValidationContext& context, VansTimelineDiagnostics& diagnostics) \
	{ ValidateIdentity(track, schema, context, diagnostics, field); }
VANS_GAF_VALIDATE(ValidateEvent, "event")
VANS_GAF_VALIDATE(ValidateWindow, "window")
VANS_GAF_VALIDATE(ValidateCue, "cue")
VANS_GAF_VALIDATE(ValidateParameter, "variable")
VANS_GAF_VALIDATE(ValidateSubAction, "action")
VANS_GAF_VALIDATE(ValidateMarker, "marker")
#undef VANS_GAF_VALIDATE

void CollectGameplayDependency(const VansTimelineTrack& track,
	std::vector<VansTimelineDependency>& dependencies)
{
	dependencies.push_back({ VansTimelineDependencyKind::ServiceCapability,
		"GameplayAction", {}, {}, track.id });
}
}

std::shared_ptr<const IVansGameplayModuleContributor> VansMakeTimelineGAFContributor(
	VansTimelineRuntimeSystem& timelineRuntime)
{
	return VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Timeline", "Timeline GAF Driver", { "Core" }),
		VansRegisterTimelineGAFTypes,
		VansRegisterTimelineGAFSchemas,
		[&timelineRuntime](VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.RegisterSidecarDriver("Timeline.Driver.Session",
				[&timelineRuntime](const VansCompiledActionRecord& record)
				{
					return std::make_unique<TimelineSessionActionDriver>(timelineRuntime, record);
				}, error);
		});
}

bool VansRegisterGameplayActionTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = VansTimelineValueType;
	using B = VansTimelineBindingRequirement;
	using P = VansTimelineEvaluationPhase;
	const auto common = []
	{
		return VansMakeTimelineSourceField(
			"action", VansTimelineValueType::String, std::string(""));
	};
	const auto payload = []
	{
		return VansMakeTimelineSourceField("payload", VansTimelineValueType::Struct,
			VansTimelineStructValue{ {}, VansSerializedValue::Object({}) });
	};
	const auto actionScope = []
	{
		return VansMakeTimelineSourceField("actionScope", VansTimelineValueType::Enum,
			std::string("HostQuery"), true, { "HostQuery" });
	};
	auto event = VansMakeTimelinePointExtension(EventTrack, "Action Event", "Gameplay Action",
		P::PostScript, B::Required, { { common(),
			VansMakeTimelineSourceField("event", F::String, std::string(""), true),
			payload(), actionScope() }, {}, false, false },
		CollectGameplayDependency);
	event.validate = ValidateEvent;
	if (!registry.Register(std::move(event), error)) return false;
	auto window = VansMakeTimelineSampleExtension(WindowTrack, "Action Window", "Gameplay Action",
		P::PostScript, B::Required, VansTimelineContinuousTrackFlags(false),
		{ { common(), VansMakeTimelineSourceField("window", F::String, std::string(""), true),
			payload(), actionScope() }, {}, false, false }, CollectGameplayDependency);
	window.validate = ValidateWindow;
	if (!registry.Register(std::move(window), error)) return false;
	auto cue = VansMakeTimelineSampleExtension(CueTrack, "Gameplay Cue", "Gameplay Action",
		P::PostScript, B::Required, VansTimelineContinuousTrackFlags(false),
		{ { common(), VansMakeTimelineSourceField("cue", F::String, std::string(""), true),
			VansMakeTimelineSourceField("mode", F::Enum, std::string("Execute"), false,
				{ "Execute", "Sustained" }),
			VansMakeTimelineSourceField("scope", F::Enum, std::string("Owner"), false,
				{ "Owner", "Target", "Observers", "World", "LocalOnly" }),
			payload(), VansMakeTimelineSourceField("intensity", F::Double, 1.0),
			actionScope() }, {}, false, false },
		CollectGameplayDependency);
	cue.validate = ValidateCue;
	if (!registry.Register(std::move(cue), error)) return false;
	auto parameter = VansMakeTimelineSampleExtension(ParameterTrack, "Action Parameter", "Gameplay Action",
		P::PostScript, B::Required, VansTimelineContinuousTrackFlags(),
		{ { common(), VansMakeTimelineSourceField("variable", F::String, std::string(""), true),
			VansMakeTimelineSourceField("valueType", F::Enum, std::string("Float"), false,
				{ "Bool", "Int32", "Int64", "Float", "Double", "String", "Vec2", "Vec3", "Vec4" }),
			actionScope() },
			{ VansMakeTimelineChannelSchema("value", F::Float, true, "valueType") }, false, false },
		CollectGameplayDependency);
	parameter.validate = ValidateParameter;
	if (!registry.Register(std::move(parameter), error)) return false;
	auto subAction = VansMakeTimelinePointExtension(SubActionTrack, "Sub Action", "Gameplay Action",
		P::PostScript, B::Required, { {
			VansMakeTimelineSourceField("action", F::String, std::string(""), true), payload(),
			VansMakeTimelineSourceField("failurePolicy", F::Enum, std::string("FailTimeline"), false,
				{ "FailTimeline", "Ignore" }) }, {}, false, false }, CollectGameplayDependency);
	subAction.flags = subAction.flags | VansTimelineTrackFlags::Destructive;
	subAction.validate = ValidateSubAction;
	if (!registry.Register(std::move(subAction), error)) return false;
	auto marker = VansMakeTimelinePointExtension(MarkerTrack, "Action Marker", "Gameplay Action",
		P::PostScript, B::Required, { { common(),
			VansMakeTimelineSourceField("marker", F::String, std::string(""), true),
			payload(), actionScope() }, {}, false, false },
		CollectGameplayDependency);
	marker.validate = ValidateMarker;
	return registry.Register(std::move(marker), error);
}

bool VansRegisterGameplayActionTimelineIntegration(
	VansGameplayRuntime& gameplay,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	return registry.Register(std::make_shared<EventApplier>(gameplay), error) &&
		registry.Register(std::make_shared<WindowApplier>(gameplay), error) &&
		registry.Register(std::make_shared<CueApplier>(gameplay), error) &&
		registry.Register(std::make_shared<ParameterApplier>(gameplay), error) &&
		registry.Register(std::make_shared<SubActionApplier>(gameplay), error) &&
		registry.Register(std::make_shared<MarkerApplier>(gameplay), error);
}
}
