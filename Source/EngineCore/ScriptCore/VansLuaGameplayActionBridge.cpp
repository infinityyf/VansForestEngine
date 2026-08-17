#include "VansLuaGameplayActionBridge.h"

#include "VansScriptContext.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EventCore/VansEventBus.h"
#include "../GameplayActionCore/VansActionSystem.h"
#include "../RenderCore/VansScene.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../Util/VansLog.h"

#include <cstdint>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C"
{
#include <lauxlib.h>
}

namespace VansRuntime
{
namespace
{
struct LuaActionSubscription
{
	lua_State* state = nullptr;
	Vans::VansEntityHandle owner;
	std::string event;
	int callback = LUA_NOREF;
};

std::unordered_map<std::uint64_t, LuaActionSubscription> g_Subscriptions;
std::uint64_t g_NextSubscription = 1;
Vans::VansScopedEventConnections g_EventConnections;
bool g_EventConnectionsInitialized = false;

int Fail(lua_State* state, const std::string& message)
{
	lua_pushnil(state);
	lua_pushlstring(state, message.data(), message.size());
	return 2;
}

void PushHandle(lua_State* state, Vans::VansGenerationHandle handle)
{
	lua_createtable(state, 0, 2);
	lua_pushinteger(state, handle.index); lua_setfield(state, -2, "index");
	lua_pushinteger(state, handle.generation); lua_setfield(state, -2, "generation");
}

bool ReadHandle(lua_State* state, int index, Vans::VansGenerationHandle& handle)
{
	if (!lua_istable(state, index)) return false;
	index = lua_absindex(state, index);
	lua_getfield(state, index, "index");
	lua_getfield(state, index, "generation");
	const bool valid = lua_isinteger(state, -2) && lua_isinteger(state, -1);
	if (valid)
	{
		const lua_Integer rawIndex = lua_tointeger(state, -2);
		const lua_Integer rawGeneration = lua_tointeger(state, -1);
		if (rawIndex >= 0 && rawGeneration > 0 && rawIndex <= UINT32_MAX && rawGeneration <= UINT32_MAX)
			handle = { static_cast<std::uint32_t>(rawIndex),
				static_cast<std::uint32_t>(rawGeneration) };
	}
	lua_pop(state, 2);
	return valid && handle.IsValid();
}

bool LuaToSerialized(lua_State* state, int index, Vans::VansSerializedValue& value,
	std::size_t depth, std::string& error)
{
	if (depth > 24) { error = "Lua payload nesting budget exceeded"; return false; }
	index = lua_absindex(state, index);
	switch (lua_type(state, index))
	{
	case LUA_TNIL: value = Vans::VansSerializedValue::Null(); return true;
	case LUA_TBOOLEAN: value = Vans::VansSerializedValue::Bool(lua_toboolean(state, index) != 0); return true;
	case LUA_TNUMBER:
		if (lua_isinteger(state, index))
			value = Vans::VansSerializedValue::Int(lua_tointeger(state, index));
		else
		{
			const double number = lua_tonumber(state, index);
			if (!std::isfinite(number)) { error = "Lua payload number must be finite"; return false; }
			value = Vans::VansSerializedValue::Float(number);
		}
		return true;
	case LUA_TSTRING:
	{
		std::size_t size = 0;
		const char* text = lua_tolstring(state, index, &size);
		if (size > 65536) { error = "Lua payload string budget exceeded"; return false; }
		value = Vans::VansSerializedValue::String(std::string(text, size));
		return true;
	}
	case LUA_TTABLE:
	{
		const std::size_t length = lua_rawlen(state, index);
		if (length > 4096) { error = "Lua payload array budget exceeded"; return false; }
		if (length > 0)
		{
			std::vector<Vans::VansSerializedValue> items;
			items.reserve(length);
			for (std::size_t item = 1; item <= length; ++item)
			{
				lua_rawgeti(state, index, static_cast<lua_Integer>(item));
				Vans::VansSerializedValue decoded;
				const bool converted = LuaToSerialized(state, -1, decoded, depth + 1, error);
				lua_pop(state, 1);
				if (!converted) return false;
				items.push_back(std::move(decoded));
			}
			value = Vans::VansSerializedValue::Array(std::move(items));
			return true;
		}
		std::vector<std::pair<std::string, Vans::VansSerializedValue>> fields;
		lua_pushnil(state);
		while (lua_next(state, index) != 0)
		{
			if (!lua_isstring(state, -2) || fields.size() >= 4096)
			{
				lua_pop(state, 2);
				error = "Lua payload objects require bounded string keys";
				return false;
			}
			std::size_t size = 0;
			const char* name = lua_tolstring(state, -2, &size);
			Vans::VansSerializedValue decoded;
			if (!LuaToSerialized(state, -1, decoded, depth + 1, error))
				{ lua_pop(state, 2); return false; }
			fields.emplace_back(std::string(name, size), std::move(decoded));
			lua_pop(state, 1);
		}
		value = Vans::VansSerializedValue::Object(std::move(fields));
		return true;
	}
	default:
		error = "Lua payload contains an unsupported value";
		return false;
	}
}

void PushSerialized(lua_State* state, const Vans::VansSerializedValue& value)
{
	switch (value.kind)
	{
	case Vans::VansSerializedValue::Kind::Null: lua_pushnil(state); break;
	case Vans::VansSerializedValue::Kind::Bool: lua_pushboolean(state, value.boolValue); break;
	case Vans::VansSerializedValue::Kind::Int: lua_pushinteger(state, value.intValue); break;
	case Vans::VansSerializedValue::Kind::Float: lua_pushnumber(state, value.floatValue); break;
	case Vans::VansSerializedValue::Kind::String:
		lua_pushlstring(state, value.stringValue.data(), value.stringValue.size()); break;
	case Vans::VansSerializedValue::Kind::Array:
		lua_createtable(state, static_cast<int>(value.arrayItems.size()), 0);
		for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
		{
			PushSerialized(state, value.arrayItems[index]);
			lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
		}
		break;
	case Vans::VansSerializedValue::Kind::Object:
		lua_createtable(state, 0, static_cast<int>(value.objectFields.size()));
		for (const auto& [name, field] : value.objectFields)
		{
			PushSerialized(state, field);
			lua_setfield(state, -2, name.c_str());
		}
		break;
	}
}

bool ResolveHost(lua_State* state, int index,
	Vans::VansGameplayRuntime*& runtime,
	std::shared_ptr<Vans::VansActionHost>& host,
	std::string& error)
{
	const char* ownerGuid = luaL_checkstring(state, index);
	auto* context = ::VansScriptContext::GetInstance();
	auto* scene = context ? context->GetScene() : nullptr;
	auto* world = scene ? scene->GetRuntimeWorld() : nullptr;
	runtime = scene ? scene->GetGameplayRuntime() : nullptr;
	if (!world || !runtime || !runtime->IsInitialized())
		{ error = "Gameplay Runtime is unavailable"; return false; }
	const Vans::VansEntityHandle owner = world->Entities().FindByGuid(ownerGuid ? ownerGuid : "");
	host = owner.IsValid() ? runtime->FindHost(owner) : nullptr;
	if (!host) { error = "ActionHost owner GUID was not found"; return false; }
	return true;
}

bool ReadContext(lua_State* state, int index,
	const std::shared_ptr<Vans::VansActionHost>& host,
	Vans::VansActionContext& context,
	std::string& error)
{
	context = {};
	context.owner = host->Owner();
	context.instigator = host->Owner();
	context.source = host->Owner();
	if (!lua_istable(state, index)) return true;
	index = lua_absindex(state, index);
	const auto readInteger = [&](const char* name, std::uint64_t& target)
	{
		lua_getfield(state, index, name);
		if (lua_isinteger(state, -1) && lua_tointeger(state, -1) >= 0)
			target = static_cast<std::uint64_t>(lua_tointeger(state, -1));
		else if (lua_isstring(state, -1))
		{
			std::size_t size = 0;
			const char* text = lua_tolstring(state, -1, &size);
			std::uint64_t value = 0;
			const auto parsed = std::from_chars(text, text + size, value);
			if (parsed.ec == std::errc{} && parsed.ptr == text + size) target = value;
		}
		lua_pop(state, 1);
	};
	const auto resolveGuid = [&](const char* field, Vans::VansEntityHandle& target)
	{
		lua_getfield(state, index, field);
		const char* guid = lua_isstring(state, -1) ? lua_tostring(state, -1) : nullptr;
		const bool requested = guid && *guid;
		if (requested)
		{
			auto* scriptContext = ::VansScriptContext::GetInstance();
			auto* scene = scriptContext ? scriptContext->GetScene() : nullptr;
			auto* world = scene ? scene->GetRuntimeWorld() : nullptr;
			target = world ? world->Entities().FindByGuid(guid) : Vans::VansEntityHandle{};
		}
		lua_pop(state, 1);
		return !requested || target.IsValid();
	};
	readInteger("random_seed", context.randomSeed);
	lua_getfield(state, index, "prediction_connection");
	if (lua_isinteger(state, -1))
	{
		const lua_Integer value = lua_tointeger(state, -1);
		if (value >= 0 && value <= UINT32_MAX)
			context.predictionKey.connection = static_cast<std::uint32_t>(value);
	}
	lua_pop(state, 1);
	lua_getfield(state, index, "prediction_sequence");
	if (lua_isinteger(state, -1))
	{
		const lua_Integer value = lua_tointeger(state, -1);
		if (value >= 0 && value <= UINT32_MAX)
			context.predictionKey.sequence = static_cast<std::uint32_t>(value);
	}
	lua_pop(state, 1);
	if (!resolveGuid("instigator_guid", context.instigator) ||
		!resolveGuid("source_guid", context.source) ||
		!resolveGuid("target_guid", context.primaryTarget))
	{
		error = "Action Context entity GUID could not be resolved";
		return false;
	}
	Vans::VansSerializedValue targetPayload;
	bool hasTargetPayload = false;
	lua_getfield(state, index, "target");
	if (lua_istable(state, -1))
	{
		lua_getfield(state, -1, "kind");
		const char* kind = lua_isstring(state, -1) ? lua_tostring(state, -1) : nullptr;
		lua_pop(state, 1);
		if (kind && std::string_view(kind) == "Entity")
		{
			lua_getfield(state, -1, "guid");
			const char* guid = lua_isstring(state, -1) ? lua_tostring(state, -1) : nullptr;
			auto* scriptContext = ::VansScriptContext::GetInstance();
			auto* scene = scriptContext ? scriptContext->GetScene() : nullptr;
			auto* world = scene ? scene->GetRuntimeWorld() : nullptr;
			context.primaryTarget = guid && world
				? world->Entities().FindByGuid(guid) : Vans::VansEntityHandle{};
			lua_pop(state, 1);
			if (!context.primaryTarget.IsValid())
			{
				lua_pop(state, 1);
				error = "Action Context target entity could not be resolved";
				return false;
			}
		}
		else
		{
			if (!kind || !LuaToSerialized(state, -1, targetPayload, 0, error))
			{
				lua_pop(state, 1);
				if (error.empty()) error = "Action Context target builder is invalid";
				return false;
			}
			hasTargetPayload = true;
		}
	}
	lua_pop(state, 1);
	lua_getfield(state, index, "payload");
	if (!lua_isnil(state, -1) && !LuaToSerialized(state, -1, context.payload, 0, error))
	{
		lua_pop(state, 1);
		return false;
	}
	lua_pop(state, 1);
	if (hasTargetPayload)
	{
		if (context.payload.kind != Vans::VansSerializedValue::Kind::Object)
		{
			error = "Action Context payload must be an object when using target builders";
			return false;
		}
		Vans::SetSerializedObjectField(context.payload, "target", std::move(targetPayload));
	}
	return true;
}

Vans::VansActionId ResolveActionId(
	Vans::VansGameplayRuntime& runtime,
	const char* reference)
{
	if (!reference || !*reference) return {};
	if (const auto definition = runtime.Assets().ResolveAction(reference)) return definition->id;
	return Vans::VansMakeStableId<Vans::VansActionIdTag>(reference);
}

void PushResult(lua_State* state, const Vans::VansActionResult& result)
{
	lua_createtable(state, 0, 5);
	lua_pushboolean(state, static_cast<bool>(result)); lua_setfield(state, -2, "success");
	lua_pushinteger(state, static_cast<lua_Integer>(result.error)); lua_setfield(state, -2, "error_code");
	lua_pushlstring(state, result.message.data(), result.message.size()); lua_setfield(state, -2, "message");
	PushHandle(state, result.action.value); lua_setfield(state, -2, "handle");
	lua_pushboolean(state, result.disposition == Vans::VansActionActivationDisposition::Queued);
	lua_setfield(state, -2, "queued");
}

const char* StateName(Vans::VansActionInstanceState state)
{
	static constexpr const char* names[] = { "Created", "Queued", "Resolving", "BuildingContext",
		"Validating", "Preparing", "Committing", "Committed", "Running", "Waiting",
		"Transitioning", "Ending", "Ended" };
	return names[static_cast<std::size_t>(state)];
}

void PushActionView(lua_State* state, const Vans::VansActionInstanceSnapshot& view)
{
	lua_createtable(state, 0, 11);
	PushHandle(state, view.handle.value); lua_setfield(state, -2, "handle");
	const std::string actionId = std::to_string(view.action.value);
	lua_pushlstring(state, actionId.data(), actionId.size()); lua_setfield(state, -2, "action_id");
	lua_pushstring(state, StateName(view.state)); lua_setfield(state, -2, "state");
	lua_pushinteger(state, static_cast<lua_Integer>(view.endReason)); lua_setfield(state, -2, "end_reason");
	lua_pushinteger(state, static_cast<lua_Integer>(view.error)); lua_setfield(state, -2, "error_code");
	lua_pushnumber(state, view.elapsedSeconds); lua_setfield(state, -2, "elapsed_seconds");
	lua_pushinteger(state, static_cast<lua_Integer>(view.taskCount)); lua_setfield(state, -2, "task_count");
	lua_pushinteger(state, static_cast<lua_Integer>(view.resourceCount)); lua_setfield(state, -2, "resource_count");
	lua_pushinteger(state, view.prediction.connection); lua_setfield(state, -2, "prediction_connection");
	lua_pushinteger(state, view.prediction.sequence); lua_setfield(state, -2, "prediction_sequence");
}

int GiveAction(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const char* reference = luaL_checkstring(state, 2);
	const auto definition = runtime->Assets().ResolveAction(reference ? reference : "");
	if (!definition) return Fail(state, "Action asset reference could not be resolved");
	Vans::VansActionGrantDesc grant;
	grant.action = definition->id;
	grant.actionReference = reference;
	if (lua_istable(state, 3))
	{
		lua_getfield(state, 3, "level");
		if (lua_isnumber(state, -1)) grant.level = lua_tonumber(state, -1);
		lua_pop(state, 1);
		lua_getfield(state, 3, "input_binding");
		if (lua_isstring(state, -1)) grant.inputBinding = lua_tostring(state, -1);
		lua_pop(state, 1);
		lua_getfield(state, 3, "charges");
		if (lua_isinteger(state, -1)) grant.charges = static_cast<std::int32_t>(lua_tointeger(state, -1));
		lua_pop(state, 1);
	}
	const auto handle = host->Grant(grant, error);
	if (!handle) return Fail(state, error);
	PushHandle(state, handle.value);
	return 1;
}

int RevokeAction(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	Vans::VansGenerationHandle raw;
	if (!ReadHandle(state, 2, raw)) return Fail(state, "Action Spec handle is invalid");
	const char* policy = luaL_optstring(state, 3, "CancelRunning");
	const auto revokePolicy = std::string_view(policy) == "KeepRunning"
		? Vans::VansActionRevokePolicy::KeepRunning
		: std::string_view(policy) == "DeferUntilIdle"
		? Vans::VansActionRevokePolicy::DeferUntilIdle
		: Vans::VansActionRevokePolicy::CancelRunning;
	const bool success = host->Revoke({ raw }, revokePolicy, error);
	lua_pushboolean(state, success);
	if (!success) { lua_pushlstring(state, error.data(), error.size()); return 2; }
	return 1;
}

int ApplyActionSet(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const char* reference = luaL_checkstring(state, 2);
	const auto* set = runtime->Assets().ResolveActionSet(reference ? reference : "");
	if (!set) return Fail(state, "Action Set asset reference could not be resolved");
	const auto handle = host->ApplyActionSet(*set, error);
	if (!handle) return Fail(state, error);
	PushHandle(state, handle.value);
	return 1;
}

int CanActivate(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const Vans::VansActionId action = ResolveActionId(*runtime, luaL_checkstring(state, 2));
	Vans::VansActionContext context;
	if (!ReadContext(state, 3, host, context, error)) return Fail(state, error);
	const bool hasAuthority = lua_isnoneornil(state, 4) || lua_toboolean(state, 4) != 0;
	const bool predicted = lua_toboolean(state, 5) != 0;
	const auto result = host->CanActivateAction(action, context, hasAuthority, predicted);
	PushResult(state, result);
	return 1;
}

int TryActivate(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const Vans::VansActionId action = ResolveActionId(*runtime, luaL_checkstring(state, 2));
	Vans::VansActionContext context;
	if (!ReadContext(state, 3, host, context, error)) return Fail(state, error);
	const auto grants = host->GrantedActions();
	const auto found = std::find_if(grants.begin(), grants.end(),
		[action](const auto& grant) { return grant.action == action; });
	if (found == grants.end()) return Fail(state, "Action is not granted");
	Vans::VansActionActivationRequest request;
	request.spec = found->handle;
	request.context = std::move(context);
	request.hasAuthority = lua_isnoneornil(state, 4) || lua_toboolean(state, 4) != 0;
	request.predicted = lua_toboolean(state, 5) != 0;
	PushResult(state, host->Activate(request));
	return 1;
}

int RequestCancel(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	Vans::VansGenerationHandle raw;
	if (!ReadHandle(state, 2, raw)) return Fail(state, "Action handle is invalid");
	const bool success = host->Cancel({ raw }, Vans::VansActionCancelReason::User, error);
	lua_pushboolean(state, success);
	if (!success) { lua_pushlstring(state, error.data(), error.size()); return 2; }
	return 1;
}

int Interrupt(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const Vans::VansActionId action = ResolveActionId(*runtime, luaL_optstring(state, 2, ""));
	std::size_t count = 0;
	for (const auto& view : host->ActiveActions())
		if ((!action || view.action == action) && host->Interrupt(view.handle, error)) ++count;
	lua_pushinteger(state, static_cast<lua_Integer>(count));
	if (!error.empty()) { lua_pushlstring(state, error.data(), error.size()); return 2; }
	return 1;
}

int QueryActions(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const Vans::VansActionId action = ResolveActionId(*runtime, luaL_optstring(state, 2, ""));
	const auto actions = host->ActiveActions();
	lua_createtable(state, static_cast<int>(actions.size()), 0);
	int output = 1;
	for (const auto& view : actions)
	{
		if (action && view.action != action) continue;
		PushActionView(state, view);
		lua_rawseti(state, -2, output++);
	}
	return 1;
}

int InspectAction(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	Vans::VansGenerationHandle raw;
	if (!ReadHandle(state, 2, raw)) return Fail(state, "Action handle is invalid");
	const auto view = host->Query({ raw });
	if (!view) return Fail(state, "Action handle is stale");
	PushActionView(state, *view);
	return 1;
}

template <typename Event>
void DispatchLifecycle(const char* name, const Event& event)
{
	std::vector<std::uint64_t> tokens;
	tokens.reserve(g_Subscriptions.size());
	for (const auto& [token, subscription] : g_Subscriptions)
		if (subscription.owner == event.owner &&
			(subscription.event == "*" || subscription.event == name)) tokens.push_back(token);
	for (std::uint64_t token : tokens)
	{
		const auto found = g_Subscriptions.find(token);
		if (found == g_Subscriptions.end()) continue;
		lua_State* state = found->second.state;
		const int callback = found->second.callback;
		lua_rawgeti(state, LUA_REGISTRYINDEX, callback);
		lua_pushstring(state, name);
		lua_createtable(state, 0, 5);
		PushHandle(state, event.action.value); lua_setfield(state, -2, "handle");
		const std::string actionId = std::to_string(event.definition.value);
		lua_pushlstring(state, actionId.data(), actionId.size()); lua_setfield(state, -2, "action_id");
		lua_pushinteger(state, event.prediction.connection); lua_setfield(state, -2, "prediction_connection");
		lua_pushinteger(state, event.prediction.sequence); lua_setfield(state, -2, "prediction_sequence");
		if constexpr (std::is_same_v<Event, Vans::VansActionEndedEvent>)
		{
			lua_pushinteger(state, static_cast<lua_Integer>(event.reason)); lua_setfield(state, -2, "end_reason");
			lua_pushinteger(state, static_cast<lua_Integer>(event.error)); lua_setfield(state, -2, "error_code");
		}
		if (lua_pcall(state, 2, 0, 0) != LUA_OK)
		{
			VANS_LOG_ERROR("[GAF Lua] action event callback failed: " << lua_tostring(state, -1));
			lua_pop(state, 1);
		}
	}
}

void EnsureEventConnections()
{
	if (g_EventConnectionsInitialized) return;
	auto& events = Vans::VansEventBus::Get();
	g_EventConnections.Add(events.Subscribe<Vans::VansActionStartedEvent>(
		[](const auto& event) { DispatchLifecycle("started", event); },
		Vans::VansEventLane::GameLogic, 0, "Lua.GAF.Started"));
	g_EventConnections.Add(events.Subscribe<Vans::VansActionQueuedEvent>(
		[](const auto& event) { DispatchLifecycle("queued", event); },
		Vans::VansEventLane::GameLogic, 0, "Lua.GAF.Queued"));
	g_EventConnections.Add(events.Subscribe<Vans::VansActionEndedEvent>(
		[](const auto& event) { DispatchLifecycle("ended", event); },
		Vans::VansEventLane::GameLogic, 0, "Lua.GAF.Ended"));
	g_EventConnectionsInitialized = true;
}

int SubscribeActionEvent(lua_State* state)
{
	Vans::VansGameplayRuntime* runtime = nullptr;
	std::shared_ptr<Vans::VansActionHost> host;
	std::string error;
	if (!ResolveHost(state, 1, runtime, host, error)) return Fail(state, error);
	const char* event = luaL_optstring(state, 2, "*");
	luaL_checktype(state, 3, LUA_TFUNCTION);
	lua_pushvalue(state, 3);
	const int callback = luaL_ref(state, LUA_REGISTRYINDEX);
	const std::uint64_t token = g_NextSubscription++;
	g_Subscriptions.emplace(token, LuaActionSubscription{ state, host->Owner(), event, callback });
	EnsureEventConnections();
	lua_pushinteger(state, static_cast<lua_Integer>(token));
	return 1;
}

int UnsubscribeActionEvent(lua_State* state)
{
	const std::uint64_t token = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
	const auto found = g_Subscriptions.find(token);
	if (found == g_Subscriptions.end() || found->second.state != state)
		{ lua_pushboolean(state, false); return 1; }
	luaL_unref(state, LUA_REGISTRYINDEX, found->second.callback);
	g_Subscriptions.erase(found);
	lua_pushboolean(state, true);
	return 1;
}

int TargetEntity(lua_State* state)
{
	const char* guid = luaL_checkstring(state, 1);
	lua_createtable(state, 0, 2);
	lua_pushstring(state, "Entity"); lua_setfield(state, -2, "kind");
	lua_pushstring(state, guid); lua_setfield(state, -2, "guid");
	return 1;
}

int TargetLocation(lua_State* state)
{
	lua_createtable(state, 0, 4);
	lua_pushstring(state, "Location"); lua_setfield(state, -2, "kind");
	lua_pushnumber(state, luaL_checknumber(state, 1)); lua_setfield(state, -2, "x");
	lua_pushnumber(state, luaL_checknumber(state, 2)); lua_setfield(state, -2, "y");
	lua_pushnumber(state, luaL_checknumber(state, 3)); lua_setfield(state, -2, "z");
	return 1;
}

int TargetRay(lua_State* state)
{
	lua_createtable(state, 0, 8);
	lua_pushstring(state, "Ray"); lua_setfield(state, -2, "kind");
	static constexpr const char* fields[] = { "ox", "oy", "oz", "dx", "dy", "dz" };
	for (int index = 0; index < 6; ++index)
	{
		lua_pushnumber(state, luaL_checknumber(state, index + 1));
		lua_setfield(state, -2, fields[index]);
	}
	lua_pushnumber(state, luaL_optnumber(state, 7, 0.0));
	lua_setfield(state, -2, "length");
	return 1;
}

int TargetSet(lua_State* state)
{
	luaL_checktype(state, 1, LUA_TTABLE);
	lua_createtable(state, 0, 2);
	lua_pushstring(state, "Set"); lua_setfield(state, -2, "kind");
	lua_pushvalue(state, 1); lua_setfield(state, -2, "targets");
	return 1;
}
}

void VansLuaGameplayActionBridge::Register(lua_State* state)
{
	lua_newtable(state);
	lua_pushcfunction(state, GiveAction); lua_setfield(state, -2, "give_action");
	lua_pushcfunction(state, RevokeAction); lua_setfield(state, -2, "revoke_action");
	lua_pushcfunction(state, ApplyActionSet); lua_setfield(state, -2, "apply_action_set");
	lua_pushcfunction(state, CanActivate); lua_setfield(state, -2, "can_activate");
	lua_pushcfunction(state, TryActivate); lua_setfield(state, -2, "try_activate");
	lua_pushcfunction(state, RequestCancel); lua_setfield(state, -2, "request_cancel");
	lua_pushcfunction(state, Interrupt); lua_setfield(state, -2, "interrupt");
	lua_pushcfunction(state, QueryActions); lua_setfield(state, -2, "query_actions");
	lua_pushcfunction(state, InspectAction); lua_setfield(state, -2, "inspect_action");
	lua_pushcfunction(state, SubscribeActionEvent); lua_setfield(state, -2, "subscribe_action_event");
	lua_pushcfunction(state, UnsubscribeActionEvent); lua_setfield(state, -2, "unsubscribe_action_event");
	lua_setfield(state, -2, "action");

	lua_newtable(state);
	lua_pushcfunction(state, TargetEntity); lua_setfield(state, -2, "entity");
	lua_pushcfunction(state, TargetLocation); lua_setfield(state, -2, "location");
	lua_pushcfunction(state, TargetRay); lua_setfield(state, -2, "ray");
	lua_pushcfunction(state, TargetSet); lua_setfield(state, -2, "set");
	lua_setfield(state, -2, "target");
}

void VansLuaGameplayActionBridge::Shutdown(lua_State* state)
{
	for (auto iterator = g_Subscriptions.begin(); iterator != g_Subscriptions.end();)
	{
		if (iterator->second.state != state) { ++iterator; continue; }
		luaL_unref(state, LUA_REGISTRYINDEX, iterator->second.callback);
		iterator = g_Subscriptions.erase(iterator);
	}
	if (g_Subscriptions.empty())
	{
		g_EventConnections.DisconnectAll();
		g_EventConnectionsInitialized = false;
	}
}
}
