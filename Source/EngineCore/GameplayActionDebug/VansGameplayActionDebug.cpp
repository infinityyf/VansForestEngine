#include "VansGameplayActionDebug.h"

#include "../AssetCore/Storage/VansFileStorage.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace Vans
{
namespace
{
using Json = nlohmann::ordered_json;

Json SerializedJson(const VansSerializedValue& value)
{
	switch (value.kind)
	{
	case VansSerializedValue::Kind::Null: return nullptr;
	case VansSerializedValue::Kind::Bool: return value.boolValue;
	case VansSerializedValue::Kind::Int: return value.intValue;
	case VansSerializedValue::Kind::Float: return value.floatValue;
	case VansSerializedValue::Kind::String: return value.stringValue;
	case VansSerializedValue::Kind::Array:
	{
		Json result = Json::array();
		for (const auto& item : value.arrayItems) result.push_back(SerializedJson(item));
		return result;
	}
	case VansSerializedValue::Kind::Object:
	{
		Json result = Json::object();
		for (const auto& [name, item] : value.objectFields) result[name] = SerializedJson(item);
		return result;
	}
	}
	return nullptr;
}

bool DecodeSerialized(const Json& json, VansSerializedValue& value, std::size_t depth)
{
	if (depth > 32) return false;
	if (json.is_null()) value = VansSerializedValue::Null();
	else if (json.is_boolean()) value = VansSerializedValue::Bool(json.get<bool>());
	else if (json.is_number_integer()) value = VansSerializedValue::Int(json.get<std::int64_t>());
	else if (json.is_number_float())
	{
		const double number = json.get<double>();
		if (!std::isfinite(number)) return false;
		value = VansSerializedValue::Float(number);
	}
	else if (json.is_string()) value = VansSerializedValue::String(json.get<std::string>());
	else if (json.is_array())
	{
		if (json.size() > 4096) return false;
		std::vector<VansSerializedValue> items;
		for (const auto& source : json)
		{
			VansSerializedValue item;
			if (!DecodeSerialized(source, item, depth + 1)) return false;
			items.push_back(std::move(item));
		}
		value = VansSerializedValue::Array(std::move(items));
	}
	else if (json.is_object())
	{
		if (json.size() > 4096) return false;
		std::vector<std::pair<std::string, VansSerializedValue>> fields;
		for (auto iterator = json.begin(); iterator != json.end(); ++iterator)
		{
			VansSerializedValue item;
			if (!DecodeSerialized(iterator.value(), item, depth + 1)) return false;
			fields.emplace_back(iterator.key(), std::move(item));
		}
		value = VansSerializedValue::Object(std::move(fields));
	}
	else return false;
	return true;
}

Json HandleJson(VansGenerationHandle handle)
{
	return { { "index", handle.index }, { "generation", handle.generation } };
}

bool DecodeHandle(const Json& json, VansGenerationHandle& handle, bool requireValid = true)
{
	if (!json.is_object()) return false;
	handle.index = json.value("index", UINT32_MAX);
	handle.generation = json.value("generation", 0u);
	return !requireValid || handle.IsValid();
}

std::uint64_t HandleKey(VansGenerationHandle handle)
{
	return (static_cast<std::uint64_t>(handle.generation) << 32u) |
		static_cast<std::uint64_t>(handle.index);
}

struct DebugActionKey
{
	std::uint64_t owner = 0;
	std::uint64_t action = 0;
	bool operator==(const DebugActionKey& other) const
		{ return owner == other.owner && action == other.action; }
};

struct DebugActionKeyHash
{
	std::size_t operator()(const DebugActionKey& key) const
	{
		const std::uint64_t mixed = key.owner ^
			(key.action + 0x9e3779b97f4a7c15ull + (key.owner << 6u) + (key.owner >> 2u));
		return static_cast<std::size_t>(mixed ^ (mixed >> 32u));
	}
};

Json ActionJson(const VansActionInstanceSnapshot& action)
{
	Json targetDataValue = nullptr;
	if (action.hasTargetData)
		targetDataValue = SerializedJson(VansEncodeTargetData(action.targetData));
	Json variables = Json::array();
	for (const auto& variable : action.variables)
		variables.push_back({ { "field", std::to_string(variable.field.value) },
			{ "value", SerializedJson(variable.value) } });
	Json tasks = Json::array();
	for (const auto& task : action.tasks)
		tasks.push_back({ { "handle", HandleJson(task.handle.value) },
			{ "type", std::to_string(task.type.value) }, { "name", task.debugName },
			{ "state", static_cast<int>(task.state) }, { "elapsed", task.elapsedSeconds },
			{ "timeout", task.timeoutSeconds } });
	Json resources = Json::array();
	for (const auto& resource : action.resources)
		resources.push_back({ { "handle", HandleJson(resource.handle.value) },
			{ "type", resource.type }, { "name", resource.debugName },
			{ "dependsOn", HandleJson(resource.dependsOn.value) } });
	Json trace = Json::array();
	for (const auto& entry : action.trace)
		trace.push_back({ { "time", entry.elapsedSeconds },
			{ "state", static_cast<int>(entry.state) }, { "message", entry.message } });
	Json events = Json::array();
	for (const auto& event : action.recentEvents)
		events.push_back({ { "sequence", std::to_string(event.sequence) },
			{ "type", std::to_string(event.type.value) }, { "name", event.stableName } });
	Json contextSlots = Json::array();
	for (const VansActionContextSlot& slot : action.context.Slots())
	{
		Json value;
		switch (slot.value.kind)
		{
		case VansActionValueKind::Serialized:
			value = SerializedJson(slot.value.serialized);
			break;
		case VansActionValueKind::Entity:
			value = HandleJson({ slot.value.entity.index, slot.value.entity.generation });
			break;
		case VansActionValueKind::TargetData:
			value = HandleJson(slot.value.targetData.value);
			break;
		case VansActionValueKind::Resource:
			value = HandleJson(slot.value.resource);
			break;
		}
		contextSlots.push_back({ { "name", slot.name },
			{ "kind", static_cast<int>(slot.value.kind) }, { "value", std::move(value) } });
	}
	return {
		{ "handle", HandleJson(action.handle.value) }, { "action", std::to_string(action.action.value) },
		{ "sourceSpec", HandleJson(action.sourceSpec.value) },
		{ "state", static_cast<int>(action.state) }, { "endReason", static_cast<int>(action.endReason) },
		{ "error", static_cast<int>(action.error) }, { "elapsed", action.elapsedSeconds },
		{ "correlationId", std::to_string(action.correlationId) },
		{ "context", { { "slots", std::move(contextSlots) },
			{ "hasTargetData", action.hasTargetData },
			{ "targetDataValue", std::move(targetDataValue) },
			{ "randomSeed", std::to_string(action.context.randomSeed) } } },
		{ "variables", std::move(variables) }, { "tasks", std::move(tasks) },
		{ "resources", std::move(resources) }, { "executor", action.executor.executor },
		{ "activeNodes", action.executor.activeNodes }, { "waitingNodes", action.executor.waitingNodes },
		{ "trace", std::move(trace) }, { "events", std::move(events) }
	};
}

bool ParseUnsigned(const Json& json, std::uint64_t& value)
{
	if (!json.is_string()) return false;
	const std::string text = json.get<std::string>();
	if (text.empty()) return false;
	const char* begin = text.data();
	const char* end = begin + text.size();
	const auto parsed = std::from_chars(begin, end, value);
	return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool DecodeAction(const Json& json, VansActionInstanceSnapshot& action)
{
	VansGenerationHandle handle;
	VansGenerationHandle spec;
	std::uint64_t actionId = 0;
	if (!DecodeHandle(json.value("handle", Json{}), handle) ||
		!DecodeHandle(json.value("sourceSpec", Json{}), spec) ||
		!ParseUnsigned(json.value("action", Json{}), actionId)) return false;
	action.handle = { handle };
	action.sourceSpec = { spec };
	action.action = { actionId };
	action.state = static_cast<VansActionInstanceState>(json.value("state", 0));
	action.endReason = static_cast<VansActionEndReason>(json.value("endReason", 0));
	action.error = static_cast<VansActionError>(json.value("error", 0));
	action.elapsedSeconds = json.value("elapsed", 0.0);
	if (!ParseUnsigned(json.value("correlationId", Json{}), action.correlationId)) return false;
	const Json context = json.value("context", Json::object());
	if (!ParseUnsigned(context.value("randomSeed", Json{}), action.context.randomSeed)) return false;
	for (const Json& slotJson : context.value("slots", Json::array()))
	{
		const std::string name = slotJson.value("name", std::string{});
		const int kindValue = slotJson.value("kind", -1);
		if (name.empty() || kindValue < 0 || kindValue > static_cast<int>(VansActionValueKind::Resource))
			return false;
		const VansActionValueKind kind = static_cast<VansActionValueKind>(kindValue);
		if (kind == VansActionValueKind::Serialized)
		{
			VansSerializedValue value;
			if (!DecodeSerialized(slotJson.value("value", Json{}), value, 0)) return false;
			action.context.SetSerialized(name, std::move(value));
		}
		else
		{
			VansGenerationHandle value;
			if (!DecodeHandle(slotJson.value("value", Json{}), value, false)) return false;
			if (kind == VansActionValueKind::Entity)
				action.context.SetEntity(name, { value.index, value.generation });
			else if (kind == VansActionValueKind::TargetData)
				action.context.SetTargetData(name, { value });
			else action.context.Set(name, VansActionValue::Resource(value));
		}
	}
	if (!action.context.Entity(VansActionContextSlots::Owner).IsValid()) return false;
	const bool hasTargetData = context.value("hasTargetData", false) ||
		(context.contains("targetDataValue") && !context["targetDataValue"].is_null());
	if (hasTargetData)
	{
		VansSerializedValue encodedTargetData;
		std::string targetError;
		if (!DecodeSerialized(context.value("targetDataValue", Json{}), encodedTargetData, 0) ||
			!VansDecodeTargetData(encodedTargetData, action.targetData, targetError)) return false;
		action.hasTargetData = true;
	}
	for (const auto& variable : json.value("variables", Json::array()))
	{
		std::uint64_t field = 0;
		VansSerializedValue value;
		if (!ParseUnsigned(variable.value("field", Json{}), field) ||
			!DecodeSerialized(variable.value("value", Json{}), value, 0)) return false;
		action.variables.push_back({ { field }, std::move(value) });
	}
	for (const auto& taskJson : json.value("tasks", Json::array()))
	{
		VansGenerationHandle taskHandle;
		std::uint64_t type = 0;
		if (!DecodeHandle(taskJson.value("handle", Json{}), taskHandle) ||
			!ParseUnsigned(taskJson.value("type", Json{}), type)) return false;
		VansActionTaskSnapshot task;
		task.handle = { taskHandle };
		task.type = { type };
		task.debugName = taskJson.value("name", "");
		task.state = static_cast<VansActionTaskState>(taskJson.value("state", 0));
		task.elapsedSeconds = taskJson.value("elapsed", 0.0);
		task.timeoutSeconds = taskJson.value("timeout", 0.0);
		if (!std::isfinite(task.elapsedSeconds) || !std::isfinite(task.timeoutSeconds)) return false;
		action.tasks.push_back(std::move(task));
	}
	for (const auto& resourceJson : json.value("resources", Json::array()))
	{
		VansGenerationHandle resourceHandle;
		VansGenerationHandle dependency;
		if (!DecodeHandle(resourceJson.value("handle", Json{}), resourceHandle) ||
			!DecodeHandle(resourceJson.value("dependsOn", Json{}), dependency, false)) return false;
		VansActionResourceSnapshot resource;
		resource.handle = { resourceHandle };
		resource.type = resourceJson.value("type", "");
		resource.debugName = resourceJson.value("name", "");
		resource.dependsOn = { dependency };
		action.resources.push_back(std::move(resource));
	}
	action.executor.executor = json.value("executor", "");
	action.executor.activeNodes = json.value("activeNodes", std::vector<std::string>{});
	action.executor.waitingNodes = json.value("waitingNodes", std::vector<std::string>{});
	for (const auto& entry : json.value("trace", Json::array()))
	{
		const double time = entry.value("time", 0.0);
		if (!std::isfinite(time)) return false;
		action.trace.push_back({ time, static_cast<VansActionInstanceState>(entry.value("state", 0)),
			entry.value("message", "") });
	}
	for (const auto& eventJson : json.value("events", Json::array()))
	{
		std::uint64_t sequence = 0;
		std::uint64_t type = 0;
		if (!ParseUnsigned(eventJson.value("sequence", Json{}), sequence) ||
			!ParseUnsigned(eventJson.value("type", Json{}), type)) return false;
		action.recentEvents.push_back({ sequence, { type }, eventJson.value("name", "") });
	}
	action.taskCount = action.tasks.size();
	action.resourceCount = action.resources.size();
	return true;
}

Json SnapshotJson(const VansGameplayDebugSnapshot& snapshot)
{
	Json hosts = Json::array();
	for (const auto& host : snapshot.hosts)
	{
		Json tags = Json::array();
		for (const auto& [tag, count] : host.tags)
			tags.push_back({ { "tag", std::to_string(tag.value) }, { "count", count } });
		Json attributes = Json::array();
		for (const auto& attribute : host.attributes)
			attributes.push_back({ { "attribute", std::to_string(attribute.attribute.value) },
				{ "base", attribute.baseValue }, { "current", attribute.currentValue } });
		Json effects = Json::array();
		for (const auto& effect : host.effects)
			effects.push_back({ { "handle", HandleJson(effect.handle.value) },
				{ "effect", std::to_string(effect.effect.value) }, { "source", std::to_string(effect.source) },
				{ "remaining", effect.remainingSeconds }, { "period", effect.periodRemainingSeconds },
				{ "stacks", effect.stacks }, { "correlationId", std::to_string(effect.correlationId) } });
		Json grants = Json::array();
		for (const auto& grant : host.grants)
		{
			Json extensions = Json::array();
			for (const VansCompiledActionRecord& extension : grant.extensions)
				extensions.push_back({ { "type", extension.type },
					{ "inputs", SerializedJson(extension.inputs) } });
			grants.push_back({ { "handle", HandleJson(grant.handle.value) },
				{ "action", std::to_string(grant.action.value) },
				{ "extensions", std::move(extensions) },
				{ "source", std::to_string(grant.source) },
				{ "pendingRemoval", grant.pendingRemoval } });
		}
		Json actions = Json::array();
		for (const auto& action : host.actions) actions.push_back(ActionJson(action));
		hosts.push_back({ { "owner", HandleJson({ host.owner.index, host.owner.generation }) },
			{ "enabled", host.enabled },
			{ "activeCues", host.activeCueCount }, { "tags", std::move(tags) },
			{ "attributes", std::move(attributes) }, { "effects", std::move(effects) },
			{ "grants", std::move(grants) }, { "actions", std::move(actions) } });
	}
	return { { "frame", snapshot.frame }, { "time", snapshot.timeSeconds },
		{ "contentManifestHash", std::to_string(snapshot.contentManifestHash) },
		{ "hosts", std::move(hosts) } };
}

bool DecodeSnapshot(const Json& json, VansGameplayDebugSnapshot& snapshot)
{
	snapshot.frame = json.value("frame", 0ull);
	snapshot.timeSeconds = json.value("time", 0.0);
	if (!std::isfinite(snapshot.timeSeconds)) return false;
	if (!ParseUnsigned(json.value("contentManifestHash", Json{}), snapshot.contentManifestHash)) return false;
	for (const auto& hostJson : json.value("hosts", Json::array()))
	{
		VansGenerationHandle owner;
		if (!DecodeHandle(hostJson.value("owner", Json{}), owner)) return false;
		VansActionHostDebugSnapshot host;
		host.owner = { owner.index, owner.generation };
		host.enabled = hostJson.value("enabled", false);
		host.activeCueCount = hostJson.value("activeCues", 0u);
		for (const auto& tagJson : hostJson.value("tags", Json::array()))
		{
			std::uint64_t tag = 0;
			if (!ParseUnsigned(tagJson.value("tag", Json{}), tag)) return false;
			host.tags.push_back({ { tag }, tagJson.value("count", 0u) });
		}
		for (const auto& attributeJson : hostJson.value("attributes", Json::array()))
		{
			std::uint64_t attributeId = 0;
			if (!ParseUnsigned(attributeJson.value("attribute", Json{}), attributeId)) return false;
			VansAttributeSnapshot attribute;
			attribute.attribute = { attributeId };
			attribute.baseValue = attributeJson.value("base", 0.0);
			attribute.currentValue = attributeJson.value("current", 0.0);
			if (!std::isfinite(attribute.baseValue) || !std::isfinite(attribute.currentValue)) return false;
			host.attributes.push_back(attribute);
		}
		for (const auto& effectJson : hostJson.value("effects", Json::array()))
		{
			VansGenerationHandle effectHandle;
			std::uint64_t effectId = 0;
			std::uint64_t source = 0;
			if (!DecodeHandle(effectJson.value("handle", Json{}), effectHandle) ||
				!ParseUnsigned(effectJson.value("effect", Json{}), effectId) ||
				!ParseUnsigned(effectJson.value("source", Json{}), source)) return false;
			VansActiveEffectSnapshot effect;
			effect.handle = { effectHandle };
			effect.effect = { effectId };
			effect.source = source;
			effect.remainingSeconds = effectJson.value("remaining", 0.0);
			effect.periodRemainingSeconds = effectJson.value("period", 0.0);
			effect.stacks = effectJson.value("stacks", 1u);
			if (!ParseUnsigned(effectJson.value("correlationId", Json{}),
				effect.correlationId)) return false;
			if (!std::isfinite(effect.remainingSeconds) ||
				!std::isfinite(effect.periodRemainingSeconds)) return false;
			host.effects.push_back(effect);
		}
		for (const auto& grantJson : hostJson.value("grants", Json::array()))
		{
			VansGenerationHandle grantHandle;
			std::uint64_t actionId = 0;
			std::uint64_t source = 0;
			if (!DecodeHandle(grantJson.value("handle", Json{}), grantHandle) ||
				!ParseUnsigned(grantJson.value("action", Json{}), actionId) ||
				!ParseUnsigned(grantJson.value("source", Json{}), source)) return false;
			VansGrantedActionSpecSnapshot grant;
			grant.handle = { grantHandle };
			grant.action = { actionId };
			grant.source = source;
			grant.pendingRemoval = grantJson.value("pendingRemoval", false);
			const Json extensions = grantJson.value("extensions", Json::array());
			if (!extensions.is_array()) return false;
			for (const auto& extensionJson : extensions)
			{
				if (!extensionJson.is_object()) return false;
				VansCompiledActionRecord extension;
				extension.type = extensionJson.value("type", "");
				if (extension.type.empty() || !extensionJson.contains("inputs") ||
					!DecodeSerialized(extensionJson["inputs"], extension.inputs, 0) ||
					extension.inputs.kind != VansSerializedValue::Kind::Object) return false;
				grant.extensions.push_back(std::move(extension));
			}
			host.grants.push_back(std::move(grant));
		}
		for (const auto& actionJson : hostJson.value("actions", Json::array()))
		{
			VansActionInstanceSnapshot action;
			if (!DecodeAction(actionJson, action)) return false;
			host.actions.push_back(std::move(action));
		}
		snapshot.hosts.push_back(std::move(host));
	}
	return true;
}

std::size_t ApproximateSize(const VansGameplayDebugSnapshot& snapshot)
{
	std::size_t size = sizeof(snapshot);
	for (const auto& host : snapshot.hosts)
	{
		size += sizeof(host) + host.tags.size() * sizeof(std::pair<VansGameplayTagId, std::uint32_t>) +
			host.attributes.size() * sizeof(VansAttributeSnapshot) +
			host.effects.size() * sizeof(VansActiveEffectSnapshot) +
			host.grants.size() * sizeof(VansGrantedActionSpecSnapshot);
		for (const auto& action : host.actions)
		{
			size += sizeof(action) + action.variables.size() * sizeof(VansActionVariableSnapshot) +
				action.tasks.size() * sizeof(VansActionTaskSnapshot) +
				action.resources.size() * sizeof(VansActionResourceSnapshot);
			for (const auto& trace : action.trace) size += trace.message.size();
			for (const auto& event : action.recentEvents) size += event.stableName.size();
		}
	}
	return size;
}
}

VansGameplayDebugSnapshot VansGameplayActionDebugService::Capture(
	const VansGameplayRuntime& runtime,
	std::uint64_t frame,
	double timeSeconds,
	std::uint64_t contentManifestHash)
{
	VansGameplayDebugSnapshot result;
	result.frame = frame;
	result.timeSeconds = timeSeconds;
	result.contentManifestHash = contentManifestHash;
	for (const auto& host : runtime.Hosts())
	{
		VansActionHostDebugSnapshot snapshot;
		snapshot.owner = host->Owner();
		snapshot.enabled = host->IsEnabled();
		snapshot.tags = host->Tags().Snapshot();
		snapshot.attributes = host->Attributes().Capture();
		snapshot.effects = host->Effects().Snapshot();
		snapshot.activeCueCount = host->Cues().ActiveCount();
		snapshot.grants = host->GrantedActions();
		snapshot.actions = host->ActiveActions();
		result.hosts.push_back(std::move(snapshot));
	}
	std::sort(result.hosts.begin(), result.hosts.end(), [](const auto& left, const auto& right)
	{
		if (left.owner.index != right.owner.index) return left.owner.index < right.owner.index;
		return left.owner.generation < right.owner.generation;
	});
	return result;
}

std::uint64_t VansGameplayActionBreakpointSet::Add(VansActionBreakpoint breakpoint)
{
	if (breakpoint.id == 0) breakpoint.id = m_NextId++;
	else m_NextId = (std::max)(m_NextId, breakpoint.id + 1);
	m_Breakpoints.push_back(std::move(breakpoint));
	return m_Breakpoints.back().id;
}

bool VansGameplayActionBreakpointSet::Remove(std::uint64_t breakpoint)
{
	const auto found = std::find_if(m_Breakpoints.begin(), m_Breakpoints.end(),
		[breakpoint](const auto& value) { return value.id == breakpoint; });
	if (found == m_Breakpoints.end()) return false;
	m_Breakpoints.erase(found);
	return true;
}

bool VansGameplayActionBreakpointSet::SetEnabled(std::uint64_t breakpoint, bool enabled)
{
	const auto found = std::find_if(m_Breakpoints.begin(), m_Breakpoints.end(),
		[breakpoint](const auto& value) { return value.id == breakpoint; });
	if (found == m_Breakpoints.end()) return false;
	found->enabled = enabled;
	return true;
}

void VansGameplayActionBreakpointSet::Clear()
{
	m_Breakpoints.clear();
}

std::vector<VansActionBreakpointHit> VansGameplayActionBreakpointSet::Evaluate(
	const VansGameplayDebugSnapshot& previous,
	const VansGameplayDebugSnapshot& current) const
{
	std::unordered_map<DebugActionKey, const VansActionInstanceSnapshot*, DebugActionKeyHash> oldActions;
	for (const auto& host : previous.hosts)
		for (const auto& action : host.actions)
			oldActions[{ HandleKey({ host.owner.index, host.owner.generation }),
				HandleKey(action.handle.value) }] = &action;
	std::unordered_map<std::uint64_t, const VansActionHostDebugSnapshot*> oldHosts;
	for (const auto& host : previous.hosts)
		oldHosts[HandleKey({ host.owner.index, host.owner.generation })] = &host;
	std::vector<VansActionBreakpointHit> result;
	for (const auto& host : current.hosts)
	{
		const auto oldHost = oldHosts.find(HandleKey({ host.owner.index, host.owner.generation }));
		for (const auto& breakpoint : m_Breakpoints)
		{
			if (!breakpoint.enabled || breakpoint.kind != VansActionBreakpointKind::Attribute ||
				!breakpoint.attribute) continue;
			const auto findAttribute = [&](const VansActionHostDebugSnapshot& source)
			{
				return std::find_if(source.attributes.begin(), source.attributes.end(),
					[&](const auto& value) { return value.attribute == breakpoint.attribute; });
			};
			const auto currentAttribute = findAttribute(host);
			if (currentAttribute == host.attributes.end()) continue;
			const bool hadPrevious = oldHost != oldHosts.end() &&
				findAttribute(*oldHost->second) != oldHost->second->attributes.end();
			double previousValue = currentAttribute->currentValue;
			if (hadPrevious) previousValue = findAttribute(*oldHost->second)->currentValue;
			const double currentValue = currentAttribute->currentValue;
			const double epsilon = (std::max)(0.0, breakpoint.epsilon);
			bool hit = false;
			switch (breakpoint.comparison)
			{
			case VansActionBreakpointComparison::Changed:
				hit = hadPrevious && std::abs(currentValue - previousValue) > epsilon; break;
			case VansActionBreakpointComparison::Equal:
				hit = std::abs(currentValue - breakpoint.value) <= epsilon &&
					(!hadPrevious || std::abs(previousValue - breakpoint.value) > epsilon); break;
			case VansActionBreakpointComparison::Less:
				hit = currentValue < breakpoint.value && (!hadPrevious || previousValue >= breakpoint.value); break;
			case VansActionBreakpointComparison::LessOrEqual:
				hit = currentValue <= breakpoint.value && (!hadPrevious || previousValue > breakpoint.value); break;
			case VansActionBreakpointComparison::Greater:
				hit = currentValue > breakpoint.value && (!hadPrevious || previousValue <= breakpoint.value); break;
			case VansActionBreakpointComparison::GreaterOrEqual:
				hit = currentValue >= breakpoint.value && (!hadPrevious || previousValue < breakpoint.value); break;
			}
			if (hit) result.push_back({ breakpoint.id, host.owner, {}, "Attribute condition crossed" });
		}
		for (const auto& action : host.actions)
		{
			const auto old = oldActions.find({ HandleKey({ host.owner.index, host.owner.generation }),
				HandleKey(action.handle.value) });
			for (const auto& breakpoint : m_Breakpoints)
			{
				if (!breakpoint.enabled || breakpoint.kind == VansActionBreakpointKind::Attribute ||
					(breakpoint.action && breakpoint.action != action.action)) continue;
				bool hit = false;
				std::string reason;
				switch (breakpoint.kind)
				{
				case VansActionBreakpointKind::Action:
					hit = old == oldActions.end(); reason = "Action started"; break;
				case VansActionBreakpointKind::State:
					hit = action.state == breakpoint.state &&
						(old == oldActions.end() || old->second->state != action.state);
					reason = "Action state changed"; break;
				case VansActionBreakpointKind::Node:
				{
					const auto containsNode = [&](const VansActionInstanceSnapshot& value)
					{
						return std::find(value.executor.activeNodes.begin(), value.executor.activeNodes.end(),
							breakpoint.node) != value.executor.activeNodes.end() ||
							std::find(value.executor.waitingNodes.begin(), value.executor.waitingNodes.end(),
								breakpoint.node) != value.executor.waitingNodes.end();
					};
					hit = containsNode(action) && (old == oldActions.end() || !containsNode(*old->second));
					reason = "Graph node reached"; break;
				}
				case VansActionBreakpointKind::Event:
				case VansActionBreakpointKind::Window:
				{
					std::uint64_t previousSequence = 0;
					if (old != oldActions.end() && !old->second->recentEvents.empty())
						previousSequence = old->second->recentEvents.back().sequence;
					const std::string windowPrefix = breakpoint.window.empty()
						? "Action.Window." : "Action.Window." + breakpoint.window + ".";
					for (const auto& event : action.recentEvents)
					{
						if (event.sequence <= previousSequence) continue;
						if (breakpoint.kind == VansActionBreakpointKind::Event)
							hit = breakpoint.event.empty() || event.stableName == breakpoint.event;
						else hit = event.stableName.rfind(windowPrefix, 0) == 0;
						if (hit) break;
					}
					reason = breakpoint.kind == VansActionBreakpointKind::Event
						? "Action event received" : "Action window edge reached";
					break;
				}
				case VansActionBreakpointKind::Error:
					hit = action.error == breakpoint.error && action.error != VansActionError::None &&
						(old == oldActions.end() || old->second->error != action.error);
					reason = "Action error reached"; break;
				case VansActionBreakpointKind::Attribute:
					break;
				}
				if (hit) result.push_back({ breakpoint.id, host.owner, action.handle, std::move(reason) });
			}
		}
	}
	return result;
}

bool VansGameplayTraceRecorder::Begin(
	std::uint64_t contentManifestHash,
	std::size_t maximumFrames,
	std::size_t maximumApproximateBytes,
	std::string& error)
{
	if (m_Recording || maximumFrames == 0 || maximumApproximateBytes == 0)
		{ error = "Gameplay trace recorder limits are invalid or it is already recording"; return false; }
	m_Archive = {};
	m_Archive.contentManifestHash = contentManifestHash;
	m_MaximumFrames = maximumFrames;
	m_MaximumApproximateBytes = maximumApproximateBytes;
	m_ApproximateBytes = 0;
	m_Recording = true;
	return true;
}

bool VansGameplayTraceRecorder::Record(VansGameplayDebugSnapshot snapshot, std::string& error)
{
	if (!m_Recording) { error = "Gameplay trace recorder is not active"; return false; }
	if (!std::isfinite(snapshot.timeSeconds) ||
		(!m_Archive.frames.empty() && (snapshot.frame <= m_Archive.frames.back().frame ||
			snapshot.timeSeconds < m_Archive.frames.back().timeSeconds)))
		{ error = "Gameplay trace frames must have finite, increasing frame/time values"; return false; }
	if (snapshot.contentManifestHash != m_Archive.contentManifestHash)
		{ error = "Gameplay trace content manifest changed during recording"; return false; }
	const std::size_t bytes = ApproximateSize(snapshot);
	if (m_Archive.frames.size() >= m_MaximumFrames ||
		m_ApproximateBytes + bytes > m_MaximumApproximateBytes)
		{ error = "Gameplay trace recorder budget exceeded"; return false; }
	m_ApproximateBytes += bytes;
	m_Archive.frames.push_back(std::move(snapshot));
	return true;
}

VansGameplayTraceArchive VansGameplayTraceRecorder::End()
{
	m_Recording = false;
	m_ApproximateBytes = 0;
	return std::move(m_Archive);
}

bool VansGameplayTraceRecorder::Save(
	const std::filesystem::path& path,
	const VansGameplayTraceArchive& archive,
	std::string& error)
{
	Json frames = Json::array();
	for (const auto& frame : archive.frames) frames.push_back(SnapshotJson(frame));
	const Json root = { { "assetKind", "GAFTrace" },
		{ "contentManifestHash", std::to_string(archive.contentManifestHash) },
		{ "frames", std::move(frames) } };
	return VansFileStorage::WriteAtomicBytes(path, root.dump(2), error);
}

bool VansGameplayTraceRecorder::Load(
	const std::filesystem::path& path,
	VansGameplayTraceArchive& archive,
	std::string& error)
{
	std::string bytes;
	if (!VansFileStorage::ReadAllBytes(path, bytes, error)) return false;
	constexpr std::size_t MaximumTraceBytes = 64u * 1024u * 1024u;
	if (bytes.size() > MaximumTraceBytes)
		{ error = "Gameplay trace archive exceeds the read budget"; return false; }
	try
	{
		const Json root = Json::parse(bytes);
		if (root.value("assetKind", "") != "GAFTrace" || !root.contains("frames") ||
			!root["frames"].is_array() || root["frames"].size() > 100000)
			{ error = "Gameplay trace archive header is invalid"; return false; }
		VansGameplayTraceArchive decoded;
		if (!ParseUnsigned(root.value("contentManifestHash", Json{}), decoded.contentManifestHash))
			{ error = "Gameplay trace manifest hash is invalid"; return false; }
		for (const auto& frameJson : root["frames"])
		{
			VansGameplayDebugSnapshot frame;
			if (!DecodeSnapshot(frameJson, frame))
				{ error = "Gameplay trace frame is invalid"; return false; }
			decoded.frames.push_back(std::move(frame));
		}
		archive = std::move(decoded);
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}
}

bool VansGameplayReplaySession::Load(VansGameplayTraceArchive archive, std::string& error)
{
	if (archive.frames.empty())
		{ error = "Gameplay replay archive is empty or unsupported"; return false; }
	for (const auto& frame : archive.frames)
		if (!std::isfinite(frame.timeSeconds) ||
			frame.contentManifestHash != archive.contentManifestHash)
			{ error = "Gameplay replay frame manifest hash mismatch"; return false; }
	for (std::size_t index = 1; index < archive.frames.size(); ++index)
		if (archive.frames[index].frame <= archive.frames[index - 1].frame ||
			archive.frames[index].timeSeconds < archive.frames[index - 1].timeSeconds)
			{ error = "Gameplay replay frames are not ordered"; return false; }
	m_Archive = std::move(archive);
	m_Frame = 0;
	return true;
}

bool VansGameplayReplaySession::SeekFrame(std::size_t frame)
{
	if (frame >= m_Archive.frames.size()) return false;
	m_Frame = frame;
	return true;
}

bool VansGameplayReplaySession::Step(std::int32_t direction)
{
	if (m_Archive.frames.empty() || direction == 0) return false;
	const std::int64_t target = static_cast<std::int64_t>(m_Frame) + direction;
	if (target < 0 || target >= static_cast<std::int64_t>(m_Archive.frames.size())) return false;
	m_Frame = static_cast<std::size_t>(target);
	return true;
}

const VansGameplayDebugSnapshot* VansGameplayReplaySession::Current() const
{
	return m_Frame < m_Archive.frames.size() ? &m_Archive.frames[m_Frame] : nullptr;
}
}
