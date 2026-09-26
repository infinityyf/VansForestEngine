#include "VansGameplayTargeting.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace Vans
{
namespace
{
VansSerializedValue HandleValue(VansEntityHandle handle)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(handle.index) },
		{ "generation", VansSerializedValue::Int(handle.generation) }
	});
}

bool ReadHandle(const VansSerializedValue* value, VansEntityHandle& handle)
{
	if (!value || value->kind != VansSerializedValue::Kind::Object) return false;
	const auto* index = FindObjectField(*value, "index");
	const auto* generation = FindObjectField(*value, "generation");
	if (!index || !generation || index->kind != VansSerializedValue::Kind::Int ||
		generation->kind != VansSerializedValue::Kind::Int || index->intValue < 0 ||
		generation->intValue < 0 || index->intValue > UINT32_MAX ||
		generation->intValue > UINT32_MAX)
		return false;
	handle = { static_cast<std::uint32_t>(index->intValue),
		static_cast<std::uint32_t>(generation->intValue) };
	return true;
}

template <std::size_t Size>
VansSerializedValue VectorValue(const std::array<double, Size>& value)
{
	static constexpr const char* Names[] = { "x", "y", "z", "w" };
	std::vector<std::pair<std::string, VansSerializedValue>> fields;
	fields.reserve(Size);
	for (std::size_t index = 0; index < Size; ++index)
		fields.emplace_back(Names[index], VansSerializedValue::Float(value[index]));
	return VansSerializedValue::Object(std::move(fields));
}

template <std::size_t Size>
bool ReadVector(const VansSerializedValue* value, std::array<double, Size>& result,
	double maximumMagnitude)
{
	static constexpr const char* Names[] = { "x", "y", "z", "w" };
	if (!value || value->kind != VansSerializedValue::Kind::Object ||
		!std::isfinite(maximumMagnitude) || maximumMagnitude < 0.0) return false;
	for (std::size_t index = 0; index < Size; ++index)
	{
		const VansSerializedValue* component = FindObjectField(*value, Names[index]);
		if (!component || (component->kind != VansSerializedValue::Kind::Float &&
			component->kind != VansSerializedValue::Kind::Int)) return false;
		result[index] = ReadSerializedNumber(*component);
		if (!std::isfinite(result[index]) || std::abs(result[index]) > maximumMagnitude)
			return false;
	}
	return true;
}

}

VansSerializedValue VansEncodeTargetData(const VansTargetData& data)
{
	std::vector<VansSerializedValue> values;
	values.reserve(data.values.size());
	for (const VansTargetDataValue& source : data.values)
	{
		values.push_back(std::visit([](const auto& target)
		{
			using Target = std::decay_t<decltype(target)>;
			if constexpr (std::is_same_v<Target, VansEntityHandle>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Entity") },
					{ "entity", HandleValue(target) } });
			else if constexpr (std::is_same_v<Target, VansTargetLocation>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Location") },
					{ "value", VectorValue(target.value) } });
			else if constexpr (std::is_same_v<Target, VansTargetRay>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Ray") },
					{ "origin", VectorValue(target.origin) },
					{ "direction", VectorValue(target.direction) },
					{ "length", VansSerializedValue::Float(target.length) } });
			else
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Hit") },
					{ "entity", HandleValue(target.entity) },
					{ "position", VectorValue(target.position) },
					{ "normal", VectorValue(target.normal) },
					{ "distance", VansSerializedValue::Float(target.distance) },
					{ "hitEntity", HandleValue(target.hitEntity) },
					{ "componentGuid", VansSerializedValue::String(target.componentGuid) },
					{ "region", VansSerializedValue::String(target.region) },
					{ "surface", VansSerializedValue::String(std::to_string(target.surface.value)) } });
		}, source));
	}
	return VansSerializedValue::Object({
		{ "values", VansSerializedValue::Array(std::move(values)) }
	});
}

bool VansDecodeTargetData(const VansSerializedValue& value,
	VansTargetData& data,
	std::string& error,
	const VansTargetDataValidationPolicy& policy)
{
	if (value.kind != VansSerializedValue::Kind::Object)
	{
		error = "TargetData root or policy is invalid";
		return false;
	}
	const VansSerializedValue* values = FindObjectField(value, "values");
	if (!values ||
		values->kind != VansSerializedValue::Kind::Array ||
		values->arrayItems.size() > policy.maximumTargets ||
		!std::isfinite(policy.maximumCoordinateMagnitude) ||
		!std::isfinite(policy.maximumDistance) || policy.maximumCoordinateMagnitude < 0.0 ||
		policy.maximumDistance < 0.0)
	{
		error = "TargetData root or policy is invalid";
		return false;
	}
	VansTargetData decoded;
	const auto allowEntity = [&](VansEntityHandle entity)
	{
		return entity.IsValid() && (!policy.entityAllowed || policy.entityAllowed(entity));
	};
	for (const VansSerializedValue& item : values->arrayItems)
	{
		if (item.kind != VansSerializedValue::Kind::Object)
			{ error = "TargetData item is malformed"; return false; }
		const std::string kind = ReadSerializedStringField(item, "kind");
		if (kind.empty()) { error = "TargetData item is malformed"; return false; }
		if (kind == "Entity")
		{
			VansEntityHandle entity;
			if (!ReadHandle(FindObjectField(item, "entity"), entity) || !allowEntity(entity))
				{ error = "TargetData Entity is invalid or unauthorized"; return false; }
			decoded.values.push_back(entity);
		}
		else if (kind == "Location")
		{
			std::array<double, 3> vector{};
			if (!ReadVector(FindObjectField(item, "value"), vector,
				policy.maximumCoordinateMagnitude))
				{ error = "TargetData vector is invalid"; return false; }
			decoded.values.push_back(VansTargetLocation{ vector });
		}
		else if (kind == "Ray")
		{
			VansTargetRay ray;
			const VansSerializedValue* length = FindObjectField(item, "length");
			if (!ReadVector(FindObjectField(item, "origin"), ray.origin,
				policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "direction"), ray.direction,
					policy.maximumCoordinateMagnitude) || !length ||
				(length->kind != VansSerializedValue::Kind::Float &&
					length->kind != VansSerializedValue::Kind::Int) ||
				!std::isfinite(ray.length = ReadSerializedNumber(*length)) ||
				ray.length < 0.0 || ray.length > policy.maximumDistance)
				{ error = "TargetData Ray is invalid"; return false; }
			decoded.values.push_back(ray);
		}
		else if (kind == "Hit")
		{
			VansTargetHitResult hit;
			const VansSerializedValue* distance = FindObjectField(item, "distance");
			const VansSerializedValue* surfaceValue = FindObjectField(item, "surface");
			std::uint64_t surface = 0;
			if (!ReadHandle(FindObjectField(item, "entity"), hit.entity) ||
				!allowEntity(hit.entity) ||
				!ReadVector(FindObjectField(item, "position"), hit.position,
					policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "normal"), hit.normal,
					policy.maximumCoordinateMagnitude) || !distance ||
				(distance->kind != VansSerializedValue::Kind::Float &&
					distance->kind != VansSerializedValue::Kind::Int) ||
				!std::isfinite(hit.distance = ReadSerializedNumber(*distance)) ||
				hit.distance < 0.0 || hit.distance > policy.maximumDistance ||
				!surfaceValue || !ReadSerializedUnsigned(*surfaceValue, surface))
				{ error = "TargetData Hit is invalid or unauthorized"; return false; }
			hit.surface = VansGameplayTagId{ surface };
			if (const auto* body = FindObjectField(item, "hitEntity"))
			{
				if (!ReadHandle(body, hit.hitEntity) ||
					(hit.hitEntity.IsValid() && !allowEntity(hit.hitEntity)))
				{ error = "TargetData hit entity is invalid or unauthorized"; return false; }
			}
			hit.componentGuid = ReadSerializedStringField(item, "componentGuid");
			hit.region = ReadSerializedStringField(item, "region");
			decoded.values.push_back(hit);
		}
		else { error = "TargetData item kind is unsupported"; return false; }
	}
	data = std::move(decoded);
	return true;
}

bool VansTargetingPolicyRegistry::Register(VansTargetingPolicy policy, std::string& error)
{
	if (m_Sealed || !policy.id || policy.name.empty() || policy.steps.empty())
	{
		error = "Targeting policy is invalid or the registry is sealed";
		return false;
	}
	if (!m_Policies.emplace(policy.id, std::move(policy)).second)
	{
		error = "duplicate Targeting policy";
		return false;
	}
	return true;
}

bool VansTargetingPolicyRegistry::Seal(std::string& error)
{
	if (m_Policies.empty())
	{
		error = "Targeting policy registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

const VansTargetingPolicy* VansTargetingPolicyRegistry::Resolve(VansTargetingPolicyId id) const
{
	const auto found = m_Policies.find(id);
	return found == m_Policies.end() ? nullptr : &found->second;
}

bool VansTargetingHandlerRegistry::Register(
	std::shared_ptr<const IVansTargetingStepHandler> handler,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Targeting handler registry is sealed";
		return false;
	}
	if (!handler || !handler->TypeId() || handler->StableName().empty())
	{
		error = "Targeting handler is invalid";
		return false;
	}
	const std::string stableName(handler->StableName());
	if (handler->TypeId() !=
		VansMakeStableId<VansActionGraphNodeTypeIdTag>(stableName))
	{
		error = "Targeting handler stable name does not match its TypeId: " + stableName;
		return false;
	}
	std::unordered_set<std::string> inputNames;
	for (const VansTargetingInputField& input : handler->InputFields())
		if (input.name.empty() || input.valueType.empty() || !inputNames.insert(input.name).second)
		{
			error = "Targeting handler input schema is invalid: " + stableName;
			return false;
		}
	if (m_Handlers.find(handler->TypeId()) != m_Handlers.end() ||
		m_ByName.find(stableName) != m_ByName.end())
	{
		error = "duplicate Targeting handler";
		return false;
	}
	m_ByName.emplace(stableName, handler->TypeId());
	m_Handlers.emplace(handler->TypeId(), std::move(handler));
	return true;
}

bool VansTargetingHandlerRegistry::Seal(std::string& error)
{
	error.clear();
	m_Sealed = true;
	return true;
}

std::shared_ptr<const IVansTargetingStepHandler> VansTargetingHandlerRegistry::Resolve(
	VansActionGraphNodeTypeId type) const
{
	const auto found = m_Handlers.find(type);
	return found == m_Handlers.end() ? nullptr : found->second;
}

std::shared_ptr<const IVansTargetingStepHandler> VansTargetingHandlerRegistry::Find(
	std::string_view stableName) const
{
	const auto found = m_ByName.find(std::string(stableName));
	return found == m_ByName.end() ? nullptr : Resolve(found->second);
}

std::vector<VansTargetingStepDescriptor> VansTargetingHandlerRegistry::Snapshot() const
{
	std::vector<VansTargetingStepDescriptor> result;
	result.reserve(m_Handlers.size());
	for (const auto& [type, handler] : m_Handlers)
		result.push_back({ type, std::string(handler->StableName()),
			handler->BeginsPipeline(), handler->InputFields() });
	std::sort(result.begin(), result.end(),
		[](const VansTargetingStepDescriptor& left,
			const VansTargetingStepDescriptor& right)
		{
			return left.stableName < right.stableName;
		});
	return result;
}

VansTargetingResult VansTargetingPipeline::Execute(
	const VansTargetingPolicy& policy,
	const VansActionContext& context,
	const VansTargetingHandlerRegistry& handlers,
	VansTargetData initial)
{
	VansTargetingResult result;
	result.data = std::move(initial);
	if (!policy.id || policy.steps.empty() || !handlers.IsSealed())
	{
		result.error = VansActionError::InvalidDefinition;
		result.message = "Targeting policy or handler registry is invalid";
		return result;
	}
	bool acquired = !result.data.values.empty();
	for (const VansTargetingStep& step : policy.steps)
	{
		VansTargetingTraceEntry trace;
		trace.inputCount = result.data.values.size();
		const auto handler = handlers.Resolve(step.handler);
		trace.step = handler ? std::string(handler->StableName()) :
			std::to_string(step.handler.value);
		if (!handler)
		{
			trace.message = "handler is missing";
			result.message = "Targeting handler is missing: " + trace.step;
			result.trace.push_back(std::move(trace));
			result.error = VansActionError::Dependency;
			return result;
		}
		if (handler->BeginsPipeline())
		{
			if (acquired)
			{
				trace.succeeded = true;
				trace.outputCount = result.data.values.size();
				trace.message = "supplied TargetData retained";
				result.trace.push_back(std::move(trace));
				continue;
			}
			acquired = true;
		}
		else if (!acquired)
		{
			trace.message = "Acquire must run before this step";
			result.trace.push_back(std::move(trace));
			result.error = VansActionError::InvalidDefinition;
			result.message = "Targeting policy does not begin with Acquire";
			return result;
		}
		trace.succeeded = handler->Execute(step, context, result.data.values, trace.message);
		trace.outputCount = result.data.values.size();
		result.trace.push_back(trace);
		if (!trace.succeeded)
		{
			result.error = VansActionError::Rejected;
			result.message = trace.message;
			return result;
		}
	}
	if (!acquired)
	{
		result.error = VansActionError::InvalidDefinition;
		result.message = "Targeting policy has no Acquire step";
	}
	return result;
}

namespace
{
class AcquireOwnerTarget final : public IVansTargetingStepHandler
{
public:
	static constexpr std::string_view kStableName = "Targeting.Acquire.Owner";
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>(kStableName); }
	std::string_view StableName() const override { return kStableName; }
	bool BeginsPipeline() const override { return true; }
	bool Execute(const VansTargetingStep&, const VansActionContext& context,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		const VansEntityHandle owner = context.Entity(VansActionContextSlots::Owner);
		if (!owner.IsValid()) { message = "Targeting owner is invalid"; return false; }
		values.push_back(owner);
		return true;
	}
};

class AcquirePrimaryTarget final : public IVansTargetingStepHandler
{
public:
	static constexpr std::string_view kStableName = "Targeting.Acquire.PrimaryTarget";
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>(kStableName); }
	std::string_view StableName() const override { return kStableName; }
	bool BeginsPipeline() const override { return true; }
	bool Execute(const VansTargetingStep&, const VansActionContext& context,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		const VansEntityHandle primaryTarget =
			context.Entity(VansActionContextSlots::PrimaryTarget);
		if (!primaryTarget.IsValid())
			{ message = "Targeting primary target is invalid"; return false; }
		values.push_back(primaryTarget);
		return true;
	}
};

class FilterValidEntityTarget final : public IVansTargetingStepHandler
{
public:
	static constexpr std::string_view kStableName = "Targeting.Filter.ValidEntity";
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>(kStableName); }
	std::string_view StableName() const override { return kStableName; }
	bool Execute(const VansTargetingStep&, const VansActionContext&,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		values.erase(std::remove_if(values.begin(), values.end(), [](const auto& value)
		{
			const auto* entity = std::get_if<VansEntityHandle>(&value);
			return entity && !entity->IsValid();
		}), values.end());
		if (values.empty()) { message = "Targeting removed every invalid entity"; return false; }
		return true;
	}
};

class LimitTargetCount final : public IVansTargetingStepHandler
{
public:
	static constexpr std::string_view kStableName = "Targeting.Limit.Count";
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>(kStableName); }
	std::string_view StableName() const override { return kStableName; }
	std::vector<VansTargetingInputField> InputFields() const override
	{
		return { { "count", "Core.Value.Int", false, VansSerializedValue::Int(1) } };
	}
	bool Execute(const VansTargetingStep& step, const VansActionContext&,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		const std::int64_t count = ReadSerializedIntField(step.inputs, "count", 1);
		if (count <= 0) { message = "Targeting limit count must be positive"; return false; }
		if (values.size() > static_cast<std::size_t>(count))
			values.resize(static_cast<std::size_t>(count));
		if (values.empty()) { message = "Targeting limit received no candidates"; return false; }
		return true;
	}
};

class LockEntityTarget final : public IVansTargetingStepHandler
{
public:
	static constexpr std::string_view kStableName = "Targeting.Lock.Entity";
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>(kStableName); }
	std::string_view StableName() const override { return kStableName; }
	bool Execute(const VansTargetingStep&, const VansActionContext&,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		const bool valid = std::any_of(values.begin(), values.end(), [](const auto& value)
		{
			const auto* entity = std::get_if<VansEntityHandle>(&value);
			return entity && entity->IsValid();
		});
		if (!valid) message = "Targeting lock requires a valid entity";
		return valid;
	}
};
}

bool VansRegisterBuiltInTargetingHandlers(
	VansTargetingHandlerRegistry& registry,
	std::string& error)
{
	return registry.Register(std::make_shared<AcquireOwnerTarget>(), error) &&
		registry.Register(std::make_shared<AcquirePrimaryTarget>(), error) &&
		registry.Register(std::make_shared<FilterValidEntityTarget>(), error) &&
		registry.Register(std::make_shared<LimitTargetCount>(), error) &&
		registry.Register(std::make_shared<LockEntityTarget>(), error);
}

bool VansBuildBuiltInTargetingHandlerRegistry(
	VansTargetingHandlerRegistry& registry,
	std::string& error)
{
	return VansRegisterBuiltInTargetingHandlers(registry, error) && registry.Seal(error);
}
}
