#include "VansGameplayTargeting.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

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

bool ReadUnsigned(const VansSerializedValue* value, std::uint64_t& result)
{
	if (!value || value->kind != VansSerializedValue::Kind::String || value->stringValue.empty())
		return false;
	const char* begin = value->stringValue.data();
	const char* end = begin + value->stringValue.size();
	const auto parsed = std::from_chars(begin, end, result);
	return parsed.ec == std::errc{} && parsed.ptr == end;
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

const char* ShapeKindName(VansTargetShapeKind kind)
{
	switch (kind)
	{
	case VansTargetShapeKind::Sphere: return "Sphere";
	case VansTargetShapeKind::Box: return "Box";
	case VansTargetShapeKind::Capsule: return "Capsule";
	}
	return "Sphere";
}

bool ReadShapeKind(std::string_view name, VansTargetShapeKind& kind)
{
	if (name == "Sphere") kind = VansTargetShapeKind::Sphere;
	else if (name == "Box") kind = VansTargetShapeKind::Box;
	else if (name == "Capsule") kind = VansTargetShapeKind::Capsule;
	else return false;
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
			else if constexpr (std::is_same_v<Target, VansTargetDirection>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Direction") },
					{ "value", VectorValue(target.value) } });
			else if constexpr (std::is_same_v<Target, VansTargetTransform>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Transform") },
					{ "position", VectorValue(target.position) },
					{ "rotation", VectorValue(target.rotation) },
					{ "scale", VectorValue(target.scale) } });
			else if constexpr (std::is_same_v<Target, VansTargetArea>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Area") },
					{ "center", VectorValue(target.center) },
					{ "radius", VansSerializedValue::Float(target.radius) } });
			else if constexpr (std::is_same_v<Target, VansTargetShape>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Shape") },
					{ "shape", VansSerializedValue::String(ShapeKindName(target.kind)) },
					{ "position", VectorValue(target.transform.position) },
					{ "rotation", VectorValue(target.transform.rotation) },
					{ "scale", VectorValue(target.transform.scale) },
					{ "extents", VectorValue(target.extents) },
					{ "radius", VansSerializedValue::Float(target.radius) },
					{ "halfHeight", VansSerializedValue::Float(target.halfHeight) } });
			else if constexpr (std::is_same_v<Target, VansTargetRay>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Ray") },
					{ "origin", VectorValue(target.origin) },
					{ "direction", VectorValue(target.direction) },
					{ "length", VansSerializedValue::Float(target.length) } });
			else if constexpr (std::is_same_v<Target, VansTargetHitResult>)
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Hit") },
					{ "entity", HandleValue(target.entity) },
					{ "position", VectorValue(target.position) },
					{ "normal", VectorValue(target.normal) },
					{ "distance", VansSerializedValue::Float(target.distance) },
					{ "surface", VansSerializedValue::String(std::to_string(target.surface.value)) } });
			else
				return VansSerializedValue::Object({
					{ "kind", VansSerializedValue::String("Deferred") },
					{ "service", VansSerializedValue::String(std::to_string(target.service.value)) },
					{ "descriptor", target.descriptor } });
		}, source));
	}
	return VansSerializedValue::Object({
		{ "version", VansSerializedValue::Int(data.version) },
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
	const VansSerializedValue* version = FindObjectField(value, "version");
	const VansSerializedValue* values = FindObjectField(value, "values");
	if (!version || !values || version->kind != VansSerializedValue::Kind::Int ||
		version->intValue <= 0 || version->intValue > UINT32_MAX ||
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
	decoded.version = static_cast<std::uint32_t>(version->intValue);
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
		else if (kind == "Location" || kind == "Direction")
		{
			std::array<double, 3> vector{};
			if (!ReadVector(FindObjectField(item, "value"), vector,
				policy.maximumCoordinateMagnitude))
				{ error = "TargetData vector is invalid"; return false; }
			if (kind == "Location") decoded.values.push_back(VansTargetLocation{ vector });
			else decoded.values.push_back(VansTargetDirection{ vector });
		}
		else if (kind == "Transform")
		{
			VansTargetTransform transform;
			if (!ReadVector(FindObjectField(item, "position"), transform.position,
				policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "rotation"), transform.rotation,
					policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "scale"), transform.scale,
					policy.maximumCoordinateMagnitude))
				{ error = "TargetData Transform is invalid"; return false; }
			decoded.values.push_back(transform);
		}
		else if (kind == "Area")
		{
			VansTargetArea area;
			const VansSerializedValue* radius = FindObjectField(item, "radius");
			if (!ReadVector(FindObjectField(item, "center"), area.center,
				policy.maximumCoordinateMagnitude) || !radius ||
				(radius->kind != VansSerializedValue::Kind::Float &&
					radius->kind != VansSerializedValue::Kind::Int) ||
				!std::isfinite(area.radius = ReadSerializedNumber(*radius)) ||
				area.radius < 0.0 || area.radius > policy.maximumDistance)
				{ error = "TargetData Area is invalid"; return false; }
			decoded.values.push_back(area);
		}
		else if (kind == "Shape")
		{
			VansTargetShape shape;
			const VansSerializedValue* radius = FindObjectField(item, "radius");
			const VansSerializedValue* halfHeight = FindObjectField(item, "halfHeight");
			if (!ReadShapeKind(ReadSerializedStringField(item, "shape"), shape.kind) ||
				!ReadVector(FindObjectField(item, "position"), shape.transform.position,
					policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "rotation"), shape.transform.rotation,
					policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "scale"), shape.transform.scale,
					policy.maximumCoordinateMagnitude) ||
				!ReadVector(FindObjectField(item, "extents"), shape.extents,
					policy.maximumDistance) || !radius || !halfHeight ||
				(radius->kind != VansSerializedValue::Kind::Float &&
					radius->kind != VansSerializedValue::Kind::Int) ||
				(halfHeight->kind != VansSerializedValue::Kind::Float &&
					halfHeight->kind != VansSerializedValue::Kind::Int) ||
				!std::isfinite(shape.radius = ReadSerializedNumber(*radius)) ||
				!std::isfinite(shape.halfHeight = ReadSerializedNumber(*halfHeight)) ||
				shape.radius < 0.0 || shape.radius > policy.maximumDistance ||
				shape.halfHeight < 0.0 || shape.halfHeight > policy.maximumDistance ||
				std::any_of(shape.extents.begin(), shape.extents.end(),
					[](double extent) { return extent < 0.0; }))
				{ error = "TargetData Shape is invalid"; return false; }
			decoded.values.push_back(shape);
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
				!ReadUnsigned(FindObjectField(item, "surface"), surface))
				{ error = "TargetData Hit is invalid or unauthorized"; return false; }
			hit.surface = VansGameplayTagId{ surface };
			decoded.values.push_back(hit);
		}
		else if (kind == "Deferred")
		{
			std::uint64_t service = 0;
			const VansSerializedValue* descriptor = FindObjectField(item, "descriptor");
			if (!ReadUnsigned(FindObjectField(item, "service"), service) || service == 0 ||
				!descriptor || !SerializedValueFitsBudget(
					*descriptor, policy.maximumDeferredDescriptorBytes))
				{ error = "TargetData Deferred query is invalid or exceeds its budget"; return false; }
			VansDeferredTargetQuery deferred;
			deferred.service = VansActionServiceId{ service };
			if (policy.deferredServiceAllowed &&
				!policy.deferredServiceAllowed(deferred.service))
				{ error = "TargetData Deferred query service is unauthorized"; return false; }
			deferred.descriptor = *descriptor;
			decoded.values.push_back(std::move(deferred));
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
	if (!m_Handlers.emplace(handler->TypeId(), std::move(handler)).second)
	{
		error = "duplicate Targeting handler";
		return false;
	}
	return true;
}

bool VansTargetingHandlerRegistry::Seal(std::string& error)
{
	if (m_Handlers.empty())
	{
		error = "Targeting handler registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

std::shared_ptr<const IVansTargetingStepHandler> VansTargetingHandlerRegistry::Resolve(
	VansActionGraphNodeTypeId type) const
{
	const auto found = m_Handlers.find(type);
	return found == m_Handlers.end() ? nullptr : found->second;
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
		result.error = VansActionError::DefinitionInvalid;
		result.message = "Targeting policy or handler registry is invalid";
		return result;
	}
	bool acquired = !result.data.values.empty();
	for (const VansTargetingStep& step : policy.steps)
	{
		VansTargetingTraceEntry trace;
		trace.step = step.stableName;
		trace.inputCount = result.data.values.size();
		if (step.kind == VansTargetingStepKind::Acquire)
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
			result.error = VansActionError::DefinitionInvalid;
			result.message = "Targeting policy does not begin with Acquire";
			return result;
		}
		const auto handler = handlers.Resolve(step.handler);
		if (!handler)
		{
			trace.message = "handler is missing";
			result.trace.push_back(std::move(trace));
			result.error = VansActionError::ServiceMissing;
			result.message = "Targeting handler is missing: " + step.stableName;
			return result;
		}
		trace.succeeded = handler->Execute(step, context, result.data.values, trace.message);
		trace.outputCount = result.data.values.size();
		result.trace.push_back(trace);
		if (!trace.succeeded)
		{
			result.error = VansActionError::TargetInvalid;
			result.message = trace.message;
			return result;
		}
	}
	if (!acquired)
	{
		result.error = VansActionError::DefinitionInvalid;
		result.message = "Targeting policy has no Acquire step";
	}
	return result;
}

namespace
{
class AcquireOwnerTarget final : public IVansTargetingStepHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Targeting.Acquire.Owner"); }
	std::string_view StableName() const override { return "Targeting.Acquire.Owner"; }
	bool Execute(const VansTargetingStep&, const VansActionContext& context,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		if (!context.owner.IsValid()) { message = "Targeting owner is invalid"; return false; }
		values.push_back(context.owner);
		return true;
	}
};

class AcquirePrimaryTarget final : public IVansTargetingStepHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Targeting.Acquire.PrimaryTarget"); }
	std::string_view StableName() const override { return "Targeting.Acquire.PrimaryTarget"; }
	bool Execute(const VansTargetingStep&, const VansActionContext& context,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		if (!context.primaryTarget.IsValid())
			{ message = "Targeting primary target is invalid"; return false; }
		values.push_back(context.primaryTarget);
		return true;
	}
};

class FilterValidEntityTarget final : public IVansTargetingStepHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Targeting.Filter.ValidEntity"); }
	std::string_view StableName() const override { return "Targeting.Filter.ValidEntity"; }
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
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Targeting.Limit.Count"); }
	std::string_view StableName() const override { return "Targeting.Limit.Count"; }
	bool Execute(const VansTargetingStep& step, const VansActionContext&,
		std::vector<VansTargetDataValue>& values, std::string& message) const override
	{
		const std::int64_t count = ReadSerializedIntField(step.parameters, "count", 1);
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
	VansActionGraphNodeTypeId TypeId() const override
		{ return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Targeting.Lock.Entity"); }
	std::string_view StableName() const override { return "Targeting.Lock.Entity"; }
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
}
