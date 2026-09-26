#include "VansGameplayEffects.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace Vans
{
bool VansDecodeEffectSetByCaller(
	const VansSerializedValue* value,
	std::unordered_map<VansActionFieldId, double>& result,
	std::string& error)
{
	result.clear();
	if (!value) return true;
	if (value->kind != VansSerializedValue::Kind::Object)
	{
		error = "Effect setByCaller must be an object";
		return false;
	}
	for (const auto& [name, magnitude] : value->objectFields)
	{
		if (name.empty() || (magnitude.kind != VansSerializedValue::Kind::Int &&
			magnitude.kind != VansSerializedValue::Kind::Float))
		{
			error = "Effect setByCaller entries need a name and numeric value";
			return false;
		}
		const double number = ReadSerializedNumber(magnitude);
		const VansActionFieldId field = VansMakeStableId<VansActionFieldIdTag>(name);
		if (!std::isfinite(number) || !result.emplace(field, number).second)
		{
			error = "Effect setByCaller contains a duplicate or non-finite value: " + name;
			return false;
		}
	}
	return true;
}

VansEffectPolicyValidation VansValidateEffectPolicy(
	const VansEffectDefinition& definition)
{
	VansEffectPolicyValidation validation;
	const auto addIssue = [&validation](VansEffectPolicyField field, std::string_view message)
	{
		if (validation.count < validation.issues.size())
			validation.issues[validation.count++] = { field, message };
	};
	const bool instant = definition.durationPolicy == VansEffectDurationPolicy::Instant;
	const bool duration = definition.durationPolicy == VansEffectDurationPolicy::Duration;
	const bool stacking = definition.stackingPolicy != VansEffectStackingPolicy::None;
	const bool periodic = definition.periodSeconds > 0.0;
	if (!duration && definition.durationSeconds != 0.0)
		addIssue(VansEffectPolicyField::DurationSeconds,
			"Only Duration Effect can declare duration seconds");
	if (instant && periodic)
		addIssue(VansEffectPolicyField::PeriodSeconds,
			"Instant Effect cannot declare a period");
	if (definition.executePeriodicOnApply && (instant || !periodic))
		addIssue(VansEffectPolicyField::ExecutePeriodicOnApply,
			"Execute periodic on apply requires a non-Instant Effect with a positive period");
	if (instant && stacking)
		addIssue(VansEffectPolicyField::StackingPolicy,
			"Instant Effect cannot use a stacking policy");
	if (!stacking)
	{
		if (definition.overflowPolicy != VansEffectOverflowPolicy::Reject)
			addIssue(VansEffectPolicyField::OverflowPolicy,
				"Effect without stacking must use Reject overflow");
		if (definition.maximumStacks != 1)
			addIssue(VansEffectPolicyField::MaximumStacks,
				"Effect without stacking must use one maximum stack");
		if (definition.refreshDurationOnStack)
			addIssue(VansEffectPolicyField::RefreshDuration,
				"Effect without stacking cannot refresh duration");
		if (definition.resetPeriodOnStack)
			addIssue(VansEffectPolicyField::ResetPeriod,
				"Effect without stacking cannot reset its period");
	}
	else
	{
		if (definition.refreshDurationOnStack && !duration)
			addIssue(VansEffectPolicyField::RefreshDuration,
				"Only Duration Effect stacking can refresh duration");
		if (definition.resetPeriodOnStack && !periodic)
			addIssue(VansEffectPolicyField::ResetPeriod,
				"Stacking period reset requires a positive period");
	}
	if (instant && !definition.grantedTags.empty())
		addIssue(VansEffectPolicyField::GrantedTags,
			"Instant Effect cannot grant persistent Tags");
	if (instant && !definition.persistentCues.empty())
		addIssue(VansEffectPolicyField::PersistentCues,
			"Instant Effect cannot own persistent Cues");
	if (!definition.periodicCues.empty() && (instant || !periodic))
		addIssue(VansEffectPolicyField::PeriodicCues,
			"Periodic Cues require a non-Instant Effect with a positive period");
	if (instant && !definition.removeCues.empty())
		addIssue(VansEffectPolicyField::RemoveCues,
			"Instant Effect cannot own remove Cues");
	return validation;
}

std::string_view VansEffectPolicyFieldPath(VansEffectPolicyField field)
{
	switch (field)
	{
	case VansEffectPolicyField::DurationSeconds: return "/duration/seconds";
	case VansEffectPolicyField::PeriodSeconds: return "/duration/period";
	case VansEffectPolicyField::ExecutePeriodicOnApply:
		return "/duration/executePeriodicOnApply";
	case VansEffectPolicyField::StackingPolicy: return "/stacking/policy";
	case VansEffectPolicyField::OverflowPolicy: return "/stacking/overflow";
	case VansEffectPolicyField::MaximumStacks: return "/stacking/maximumStacks";
	case VansEffectPolicyField::RefreshDuration: return "/stacking/refreshDuration";
	case VansEffectPolicyField::ResetPeriod: return "/stacking/resetPeriod";
	case VansEffectPolicyField::GrantedTags: return "/grantedTags";
	case VansEffectPolicyField::PersistentCues:
	case VansEffectPolicyField::PeriodicCues:
	case VansEffectPolicyField::RemoveCues:
		return "/extensions";
	}
	return {};
}

namespace
{
VansAttributeModifierOperation AttributeOperation(VansEffectModifierOperation operation)
{
	switch (operation)
	{
	case VansEffectModifierOperation::Add:
		return VansAttributeModifierOperation::Additive;
	case VansEffectModifierOperation::Multiply:
		return VansAttributeModifierOperation::Multiplicative;
	case VansEffectModifierOperation::Set:
		return VansAttributeModifierOperation::Override;
	}
	return VansAttributeModifierOperation::Additive;
}

bool ValidateEffectDefinition(const VansEffectDefinition& definition, std::string& error)
{
	if (!definition.id || definition.name.empty())
	{
		error = "Effect identity is invalid";
		return false;
	}
	if (!std::isfinite(definition.durationSeconds) || definition.durationSeconds < 0.0)
	{
		error = "Effect duration is invalid";
		return false;
	}
	if (definition.durationPolicy == VansEffectDurationPolicy::Duration &&
		definition.durationSeconds <= 0.0)
	{
		error = "Duration Effect must have a positive duration";
		return false;
	}
	if (!std::isfinite(definition.periodSeconds) || definition.periodSeconds < 0.0)
	{
		error = "Effect period is invalid";
		return false;
	}
	if (definition.maximumStacks == 0)
	{
		error = "Effect maximumStacks must be positive";
		return false;
	}
	const VansEffectPolicyValidation policyIssues =
		VansValidateEffectPolicy(definition);
	if (!policyIssues.IsValid())
	{
		error = std::string(policyIssues.issues.front().message);
		return false;
	}
	for (const VansEffectModifier& modifier : definition.modifiers)
	{
		if (!modifier.attribute || !std::isfinite(modifier.magnitude) ||
			!std::isfinite(modifier.randomMinimum) || !std::isfinite(modifier.randomMaximum) ||
			!std::isfinite(modifier.coefficient) || !std::isfinite(modifier.preAdd) ||
			!std::isfinite(modifier.postAdd) || modifier.randomMinimum > modifier.randomMaximum)
		{
			error = "Effect contains an invalid Attribute modifier";
			return false;
		}
		if (modifier.application == VansEffectModifierApplication::Persistent &&
			definition.durationPolicy == VansEffectDurationPolicy::Instant)
		{
			error = "Instant Effect cannot own a persistent Attribute modifier";
			return false;
		}
		if (modifier.application == VansEffectModifierApplication::Base &&
			definition.durationPolicy != VansEffectDurationPolicy::Instant &&
			definition.periodSeconds <= 0.0)
		{
			error = "Non-Instant base modifier requires a positive period";
			return false;
		}
		if (modifier.application == VansEffectModifierApplication::Base &&
			modifier.priority != 0)
		{
			error = "Base Attribute modifier cannot declare a priority";
			return false;
		}
		switch (modifier.magnitudeSource)
		{
		case VansEffectMagnitudeSource::SetByCaller:
			if (!modifier.setByCallerField)
			{
				error = "Effect SetByCaller modifier is missing a field";
				return false;
			}
			break;
		case VansEffectMagnitudeSource::CapturedAttribute:
			if (!modifier.capturedAttribute ||
				(modifier.capturePolicy == VansEffectCapturePolicy::Dynamic &&
					modifier.capturedAttribute == modifier.attribute))
			{
				error = "Effect Attribute capture is invalid or self-referential";
				return false;
			}
			break;
		case VansEffectMagnitudeSource::ContextPayload:
			if (modifier.contextPayloadPath.empty() || modifier.contextPayloadPath.front() != '/')
			{
				error = "Effect Context payload source needs an absolute JSON pointer";
				return false;
			}
			break;
		default:
			break;
		}
	}
	return true;
}

bool ValidateEffectPerformanceBudget(
	const VansEffectDefinition& definition,
	const VansGAFPerformanceBudget& performance,
	std::string& error)
{
	if (performance.maximumEffectsPerHost == 0 ||
		!std::isfinite(performance.minimumEffectPeriodSeconds) ||
		performance.minimumEffectPeriodSeconds <= 0.0 ||
		performance.maximumEffectPulsesPerTick == 0)
	{
		error = "Effect performance budget is invalid";
		return false;
	}
	if (definition.periodSeconds > 0.0 &&
		definition.periodSeconds < performance.minimumEffectPeriodSeconds)
	{
		error = "Effect period is below the project minimum: " + definition.name;
		return false;
	}
	return true;
}

bool UsesPersistentModifiers(const VansEffectDefinition& definition)
{
	return std::any_of(definition.modifiers.begin(), definition.modifiers.end(),
		[](const VansEffectModifier& modifier)
		{ return modifier.application == VansEffectModifierApplication::Persistent; });
}

std::size_t PersistentModifierCount(const VansEffectDefinition& definition)
{
	return static_cast<std::size_t>(std::count_if(
		definition.modifiers.begin(), definition.modifiers.end(),
		[](const VansEffectModifier& modifier)
		{ return modifier.application == VansEffectModifierApplication::Persistent; }));
}

bool UsesDynamicPersistentModifier(const VansEffectDefinition& definition)
{
	return std::any_of(definition.modifiers.begin(), definition.modifiers.end(),
		[](const VansEffectModifier& modifier)
		{
			return modifier.application == VansEffectModifierApplication::Persistent &&
				modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
				modifier.capturePolicy == VansEffectCapturePolicy::Dynamic;
		});
}

bool UsesSnapshotMagnitude(const VansEffectModifier& modifier)
{
	return modifier.magnitudeSource == VansEffectMagnitudeSource::RandomRange ||
		(modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
			modifier.capturePolicy == VansEffectCapturePolicy::Snapshot);
}

std::uint64_t SplitMix64(std::uint64_t value)
{
	value += 0x9e3779b97f4a7c15ull;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
	return value ^ (value >> 31);
}

double SampleRandomRange(
	const VansEffectModifier& modifier,
	std::size_t modifierIndex,
	VansEffectId effect,
	const VansActionContext& context,
	std::uint64_t source,
	std::uint64_t applicationSequence)
{
	std::uint64_t seed = SplitMix64(context.randomSeed);
	seed = SplitMix64(seed ^ effect.value);
	seed = SplitMix64(seed ^ source);
	seed = SplitMix64(seed ^ context.correlationId);
	seed = SplitMix64(seed ^ applicationSequence);
	seed = SplitMix64(seed ^ (static_cast<std::uint64_t>(modifierIndex) + 1ull));
	const double unit = static_cast<double>(seed >> 11) *
		(1.0 / 9007199254740992.0);
	return modifier.randomMinimum +
		(modifier.randomMaximum - modifier.randomMinimum) * unit;
}

bool ResolveTargetDataMetric(const VansTargetData& data,
	VansEffectTargetDataMetric metric,
	double& value)
{
	if (metric == VansEffectTargetDataMetric::Count)
	{
		value = static_cast<double>(data.values.size());
		return true;
	}
	for (const VansTargetDataValue& target : data.values)
	{
		if (metric == VansEffectTargetDataMetric::HitDistance)
			if (const auto* hit = std::get_if<VansTargetHitResult>(&target))
			{
				value = hit->distance;
				return true;
			}
		if (metric == VansEffectTargetDataMetric::RayLength)
			if (const auto* ray = std::get_if<VansTargetRay>(&target))
			{
				value = ray->length;
				return true;
			}
	}
	return false;
}

bool ValidatePersistentTargetData(const VansTargetData& source, std::string& error)
{
	VansTargetDataValidationPolicy policy;
	policy.maximumTargets = source.values.size();
	policy.maximumCoordinateMagnitude = std::numeric_limits<double>::max();
	policy.maximumDistance = std::numeric_limits<double>::max();
	VansTargetData decoded;
	return VansDecodeTargetData(VansEncodeTargetData(source), decoded, error, policy);
}

const VansPersistentEffectContextSlot* FindPersistentContextSlot(
	const VansPersistentEffectContext& context,
	std::string_view name)
{
	const VansActionFieldId id = VansMakeStableId<VansActionFieldIdTag>(name);
	for (const VansPersistentEffectContextSlot& slot : context.slots)
		if (VansMakeStableId<VansActionFieldIdTag>(slot.name) == id) return &slot;
	return nullptr;
}

bool ValidatePersistentContext(
	const VansPersistentEffectContext& context,
	std::string& error)
{
	std::vector<VansActionFieldId> fields;
	fields.reserve(context.slots.size());
	for (const VansPersistentEffectContextSlot& slot : context.slots)
	{
		if (slot.name.empty())
		{
			error = "Persistent Effect Context contains an empty slot name";
			return false;
		}
		const VansActionFieldId field =
			VansMakeStableId<VansActionFieldIdTag>(slot.name);
		if (std::find(fields.begin(), fields.end(), field) != fields.end())
		{
			error = "Persistent Effect Context contains a duplicate slot";
			return false;
		}
		fields.push_back(field);
		if (slot.kind == VansActionValueKind::Resource)
		{
			error = "Persistent Effect Context cannot contain a runtime Resource handle";
			return false;
		}
		if (slot.kind == VansActionValueKind::TargetData &&
			!ValidatePersistentTargetData(slot.targetData, error))
			return false;
	}
	return true;
}

bool CapturePersistentContext(
	const VansActionContext& source,
	const VansTargetDataStore* targetData,
	VansPersistentEffectContext& result,
	std::string& error)
{
	result = {};
	result.correlationId = source.correlationId;
	result.randomSeed = source.randomSeed;
	result.slots.reserve(source.Slots().size());
	for (const VansActionContextSlot& sourceSlot : source.Slots())
	{
		VansPersistentEffectContextSlot slot;
		slot.name = sourceSlot.name;
		slot.kind = sourceSlot.value.kind;
		switch (sourceSlot.value.kind)
		{
		case VansActionValueKind::Serialized:
			slot.serialized = sourceSlot.value.serialized;
			break;
		case VansActionValueKind::Entity:
			slot.entity = sourceSlot.value.entity;
			break;
		case VansActionValueKind::TargetData:
		{
			const VansTargetData* value = targetData ?
				targetData->Resolve(sourceSlot.value.targetData) : nullptr;
			if (!value)
			{
				error = "Persistent Effect Context references stale TargetData";
				return false;
			}
			slot.targetData = *value;
			break;
		}
		case VansActionValueKind::Resource:
			error = "Persistent Effect Context contains a runtime Resource handle";
			return false;
		}
		result.slots.push_back(std::move(slot));
	}
	return true;
}

bool RestorePersistentContext(
	const VansPersistentEffectContext& source,
	VansTargetDataStore* targetData,
	VansActionContext& result,
	std::vector<VansTargetDataHandle>& ownedTargetData,
	std::string& error)
{
	result = {};
	result.correlationId = source.correlationId;
	result.randomSeed = source.randomSeed;
	for (const VansPersistentEffectContextSlot& slot : source.slots)
	{
		bool stored = false;
		switch (slot.kind)
		{
		case VansActionValueKind::Serialized:
			stored = result.SetSerialized(slot.name, slot.serialized);
			break;
		case VansActionValueKind::Entity:
			stored = result.SetEntity(slot.name, slot.entity);
			break;
		case VansActionValueKind::TargetData:
			if (!targetData)
			{
				error = "Persistent Effect TargetData store is unavailable";
				return false;
			}
			else
			{
				const VansTargetDataHandle handle = targetData->Store(slot.targetData);
				ownedTargetData.push_back(handle);
				stored = result.SetTargetData(slot.name, handle);
			}
			break;
		case VansActionValueKind::Resource:
			error = "Persistent Effect Context contains a runtime Resource handle";
			return false;
		}
		if (!stored)
		{
			error = "Persistent Effect Context slot could not be restored";
			return false;
		}
	}
	return true;
}

const VansTargetData* PersistentTargetData(
	const VansPersistentEffectStackState& stack)
{
	if (stack.hasTargetData) return &stack.targetData;
	const VansPersistentEffectContextSlot* slot = FindPersistentContextSlot(
		stack.context, VansActionContextSlots::TargetData);
	return slot && slot->kind == VansActionValueKind::TargetData ? &slot->targetData : nullptr;
}

bool ResolvePersistentMagnitude(
	const VansEffectModifier& modifier,
	std::size_t modifierIndex,
	const VansPersistentEffectStackState& stack,
	const VansAttributeService& attributes,
	double& magnitude,
	std::string& error)
{
	double value = 0.0;
	switch (modifier.magnitudeSource)
	{
	case VansEffectMagnitudeSource::Fixed:
		value = modifier.magnitude;
		break;
	case VansEffectMagnitudeSource::SetByCaller:
	{
		const auto found = std::find_if(stack.setByCaller.begin(), stack.setByCaller.end(),
			[&](const VansPersistentEffectSetByCallerState& entry)
			{ return entry.field == modifier.setByCallerField; });
		if (found == stack.setByCaller.end())
		{
			error = "Persistent Effect SetByCaller magnitude is missing";
			return false;
		}
		value = found->value;
		break;
	}
	case VansEffectMagnitudeSource::CapturedAttribute:
		if (modifier.capturePolicy == VansEffectCapturePolicy::Snapshot)
		{
			const auto found = std::find_if(stack.snapshots.begin(), stack.snapshots.end(),
				[modifierIndex](const VansPersistentEffectSnapshotState& snapshot)
				{ return snapshot.modifierIndex == modifierIndex; });
			if (found == stack.snapshots.end())
			{
				error = "Persistent Effect magnitude snapshot is missing";
				return false;
			}
			value = found->value;
		}
		else value = attributes.Current(modifier.capturedAttribute);
		break;
	case VansEffectMagnitudeSource::ContextPayload:
	{
		const VansPersistentEffectContextSlot* slot = FindPersistentContextSlot(
			stack.context, VansActionContextSlots::Payload);
		const VansSerializedValue* payload = slot &&
			slot->kind == VansActionValueKind::Serialized
			? FindSerializedPointer(slot->serialized, modifier.contextPayloadPath) : nullptr;
		if (!payload || (payload->kind != VansSerializedValue::Kind::Int &&
			payload->kind != VansSerializedValue::Kind::Float))
		{
			error = "Persistent Effect Context payload magnitude is missing or not numeric";
			return false;
		}
		value = ReadSerializedNumber(*payload);
		break;
	}
	case VansEffectMagnitudeSource::TargetData:
	{
		const VansTargetData* data = PersistentTargetData(stack);
		if (!data)
		{
			error = "Persistent Effect TargetData magnitude is missing";
			return false;
		}
		if (!ResolveTargetDataMetric(*data, modifier.targetDataMetric, value))
		{
			error = "Persistent Effect TargetData metric is unavailable";
			return false;
		}
		break;
	}
	case VansEffectMagnitudeSource::RandomRange:
	{
		const auto found = std::find_if(stack.snapshots.begin(), stack.snapshots.end(),
			[modifierIndex](const VansPersistentEffectSnapshotState& snapshot)
			{ return snapshot.modifierIndex == modifierIndex; });
		if (found == stack.snapshots.end())
		{
			error = "Persistent Effect RandomRange snapshot is missing";
			return false;
		}
		value = found->value;
		break;
	}
	}
	magnitude = ((value + modifier.preAdd) * modifier.coefficient) + modifier.postAdd;
	if (!std::isfinite(magnitude))
	{
		error = "Persistent Effect magnitude is non-finite";
		return false;
	}
	return true;
}
}

bool VansEffectRegistry::Register(
	std::shared_ptr<const VansEffectDefinition> definition,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Effect registry is sealed";
		return false;
	}
	if (!definition || !ValidateEffectDefinition(*definition, error)) return false;
	if (!m_Definitions.emplace(definition->id, std::move(definition)).second)
	{
		error = "duplicate Effect id";
		return false;
	}
	return true;
}

bool VansEffectRegistry::Seal(std::string& error)
{
	if (m_Definitions.empty())
	{
		error = "Effect registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

bool VansEffectRegistry::ValidatePerformanceBudget(
	const VansGAFPerformanceBudget& performance,
	std::string& error) const
{
	for (const auto& [id, definition] : m_Definitions)
	{
		(void)id;
		if (!definition || !ValidateEffectPerformanceBudget(*definition, performance, error))
			return false;
	}
	return true;
}

std::shared_ptr<const VansEffectDefinition> VansEffectRegistry::Resolve(VansEffectId id) const
{
	const auto found = m_Definitions.find(id);
	return found == m_Definitions.end() ? nullptr : found->second;
}

bool VansGameplayEffectService::PrepareSpec(
	const VansEffectSpec& source,
	VansEffectSpec& prepared,
	std::uint64_t applicationSequence,
	std::string& error) const
{
	prepared = source;
	if (!prepared.definition) return false;
	prepared.snapshotModifierValues.assign(prepared.definition->modifiers.size(),
		std::numeric_limits<double>::quiet_NaN());
	for (std::size_t index = 0; index < prepared.definition->modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = prepared.definition->modifiers[index];
		if (modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
			modifier.capturePolicy == VansEffectCapturePolicy::Snapshot)
			prepared.snapshotModifierValues[index] = m_Attributes->Current(modifier.capturedAttribute);
		else if (modifier.magnitudeSource == VansEffectMagnitudeSource::RandomRange)
			prepared.snapshotModifierValues[index] = SampleRandomRange(
				modifier, index, prepared.definition->id, prepared.context,
				prepared.source, applicationSequence);
		double ignored = 0.0;
		if (!ResolveMagnitude(modifier, index, prepared, ignored, error)) return false;
	}
	return true;
}

bool VansGameplayEffectService::ResolveMagnitude(
	const VansEffectModifier& modifier,
	std::size_t modifierIndex,
	const VansEffectSpec& spec,
	double& magnitude,
	std::string& error) const
{
	double value = 0.0;
	switch (modifier.magnitudeSource)
	{
	case VansEffectMagnitudeSource::Fixed:
		value = modifier.magnitude;
		break;
	case VansEffectMagnitudeSource::SetByCaller:
	{
		const auto found = spec.setByCaller.find(modifier.setByCallerField);
		if (found == spec.setByCaller.end())
		{
			error = "Effect SetByCaller magnitude is missing";
			return false;
		}
		value = found->second;
		break;
	}
	case VansEffectMagnitudeSource::CapturedAttribute:
		if (modifier.capturePolicy == VansEffectCapturePolicy::Snapshot)
		{
			if (modifierIndex >= spec.snapshotModifierValues.size() ||
				!std::isfinite(spec.snapshotModifierValues[modifierIndex]))
			{
				error = "Effect magnitude snapshot is unavailable";
				return false;
			}
			value = spec.snapshotModifierValues[modifierIndex];
		}
		else value = m_Attributes->Current(modifier.capturedAttribute);
		break;
	case VansEffectMagnitudeSource::ContextPayload:
	{
		const VansSerializedValue* contextPayload =
			spec.context.Serialized(VansActionContextSlots::Payload);
		const VansSerializedValue* payload = contextPayload
			? FindSerializedPointer(*contextPayload, modifier.contextPayloadPath) : nullptr;
		if (!payload || (payload->kind != VansSerializedValue::Kind::Int &&
			payload->kind != VansSerializedValue::Kind::Float))
		{
			error = "Effect Context payload magnitude is missing or not numeric";
			return false;
		}
		value = ReadSerializedNumber(*payload);
		break;
	}
	case VansEffectMagnitudeSource::TargetData:
	{
		const VansTargetDataHandle handle = spec.targetData ? spec.targetData :
			spec.context.TargetData(VansActionContextSlots::TargetData);
		const VansTargetData* data = m_TargetData ? m_TargetData->Resolve(handle) : nullptr;
		if (!data)
		{
			error = "Effect TargetData magnitude requires a live TargetData handle";
			return false;
		}
		if (!ResolveTargetDataMetric(*data, modifier.targetDataMetric, value))
		{
			error = "Effect TargetData does not contain the requested metric";
			return false;
		}
		break;
	}
	case VansEffectMagnitudeSource::RandomRange:
	{
		if (modifierIndex >= spec.snapshotModifierValues.size() ||
			!std::isfinite(spec.snapshotModifierValues[modifierIndex]))
		{
			error = "Effect RandomRange snapshot is unavailable";
			return false;
		}
		value = spec.snapshotModifierValues[modifierIndex];
		break;
	}
	}
	magnitude = ((value + modifier.preAdd) * modifier.coefficient) + modifier.postAdd;
	if (!std::isfinite(magnitude))
	{
		error = "Effect magnitude calculation produced a non-finite value";
		return false;
	}
	return true;
}

bool VansGameplayEffectService::AggregateMagnitude(
	const VansEffectModifier& modifier,
	std::size_t modifierIndex,
	const std::vector<VansActiveEffectStack>& stackStates,
	double& magnitude,
	std::string& error) const
{
	if (stackStates.empty())
	{
		magnitude = 0.0;
		return true;
	}
	if (modifier.operation == VansEffectModifierOperation::Multiply)
		magnitude = 1.0;
	else magnitude = 0.0;
	for (const VansActiveEffectStack& stack : stackStates)
	{
		const VansEffectSpec& spec = stack.spec;
		double resolved = 0.0;
		if (!ResolveMagnitude(modifier, modifierIndex, spec, resolved, error)) return false;
		resolved *= spec.level;
		if (modifier.operation == VansEffectModifierOperation::Add) magnitude += resolved;
		else if (modifier.operation == VansEffectModifierOperation::Multiply)
			magnitude *= resolved;
		else magnitude = resolved;
	}
	if (!std::isfinite(magnitude))
	{
		error = "Effect stack magnitude produced a non-finite value";
		return false;
	}
	return true;
}

VansEffectValidationResult VansGameplayEffectService::ValidateApplication(
	const VansEffectSpec& spec) const
{
	VansEffectSpec prepared;
	VansEffectValidationResult result = ValidateSpec(
		spec, prepared, m_NextApplicationSequence, nullptr);
	return result ? ValidatePrepared(prepared) : result;
}

VansEffectValidationResult VansGameplayEffectService::ValidateApplications(
	const std::vector<VansEffectSpec>& specs) const
{
	struct VansPendingEffectStack
	{
		VansEffectId effect;
		std::uint64_t source = 0;
		std::uint32_t stacks = 0;
	};
	std::vector<VansPendingEffectStack> pending;
	std::vector<VansGameplayTagId> addedTags;
	std::size_t reservedActive = 0;
	std::uint64_t applicationSequence = m_NextApplicationSequence;
	for (const VansEffectSpec& spec : specs)
	{
		VansEffectSpec prepared;
		VansEffectValidationResult result = ValidateSpec(
			spec, prepared, applicationSequence, &addedTags);
		if (!result) return result;
		++applicationSequence;
		if (prepared.definition->durationPolicy == VansEffectDurationPolicy::Instant) continue;
		addedTags.insert(addedTags.end(), prepared.definition->grantedTags.begin(),
			prepared.definition->grantedTags.end());
		if (prepared.definition->stackingPolicy == VansEffectStackingPolicy::None)
		{
			if (m_Performance.maximumEffectsPerHost == 0 ||
				m_Active.ActiveCount() + reservedActive >=
					m_Performance.maximumEffectsPerHost)
			{
				result.error = VansActionError::Budget;
				result.message = "Active Effect budget exceeded";
				return result;
			}
			++reservedActive;
			continue;
		}
		const std::uint64_t stackSource =
			prepared.definition->stackingPolicy == VansEffectStackingPolicy::AggregateByTarget
			? 0 : prepared.source;
		auto planned = std::find_if(pending.begin(), pending.end(), [&](const VansPendingEffectStack& stack)
		{
			return stack.effect == prepared.definition->id && stack.source == stackSource;
		});
		if (planned == pending.end())
		{
			std::uint32_t stacks = 0;
			if (const VansActiveEffectHandle existing = FindStack(prepared); existing)
			{
				const VansActiveEffect* active = m_Active.Resolve(existing.value);
				if (!active)
				{
					result.error = VansActionError::Internal;
					result.message = "Effect stack handle became stale";
					return result;
				}
				stacks = active->StackCount();
			}
			else
			{
				if (m_Performance.maximumEffectsPerHost == 0 ||
					m_Active.ActiveCount() + reservedActive >=
						m_Performance.maximumEffectsPerHost)
				{
					result.error = VansActionError::Budget;
					result.message = "Active Effect budget exceeded";
					return result;
				}
				++reservedActive;
			}
			pending.push_back({ prepared.definition->id, stackSource, stacks });
			planned = std::prev(pending.end());
		}
		if (planned->stacks >= prepared.definition->maximumStacks)
		{
			if (prepared.definition->overflowPolicy == VansEffectOverflowPolicy::Reject)
			{
				result.error = VansActionError::Rejected;
				result.message = "Effect stack is full";
				return result;
			}
			continue;
		}
		++planned->stacks;
	}
	return {};
}

VansEffectValidationResult VansGameplayEffectService::ValidateSpec(
	const VansEffectSpec& spec,
	VansEffectSpec& prepared,
	std::uint64_t applicationSequence,
	const std::vector<VansGameplayTagId>* addedTags) const
{
	VansEffectValidationResult result;
	std::string error;
	if (applicationSequence == 0 ||
		applicationSequence == std::numeric_limits<std::uint64_t>::max())
	{
		result.error = VansActionError::Budget;
		result.message = "Effect application sequence is exhausted";
		return result;
	}
	if (!spec.definition || !ValidateEffectDefinition(*spec.definition, error) ||
		!ValidateEffectPerformanceBudget(*spec.definition, m_Performance, error) ||
		!m_Attributes || !m_Tags || spec.source == 0 || !std::isfinite(spec.level))
	{
		result.error = VansActionError::InvalidDefinition;
		result.message = error.empty() ? "Effect service or spec is invalid" : std::move(error);
		return result;
	}
	const auto matches = [this, addedTags](const VansGameplayTagQuery& query)
	{
		return addedTags ? m_Tags->MatchesWithTags(query, *addedTags) : m_Tags->Matches(query);
	};
	if (!matches(spec.definition->requirements))
	{
		result.error = VansActionError::Rejected;
		result.message = "Effect requirements failed";
		return result;
	}
	const bool hasImmunityQuery = !spec.definition->immunity.all.empty() ||
		!spec.definition->immunity.any.empty() || !spec.definition->immunity.none.empty();
	if (hasImmunityQuery && matches(spec.definition->immunity))
	{
		result.error = VansActionError::Rejected;
		result.message = "Effect was blocked by immunity";
		return result;
	}
	const auto containsTags = [this](const std::vector<VansGameplayTagId>& tags)
	{
		return std::all_of(tags.begin(), tags.end(),
			[this](VansGameplayTagId tag) { return m_Tags->Contains(tag); });
	};
	const bool persistent =
		spec.definition->durationPolicy != VansEffectDurationPolicy::Instant;
	if (!containsTags(spec.definition->requirements.all) ||
		!containsTags(spec.definition->requirements.any) ||
		!containsTags(spec.definition->requirements.none) ||
		!containsTags(spec.definition->immunity.all) ||
		!containsTags(spec.definition->immunity.any) ||
		!containsTags(spec.definition->immunity.none) ||
		(persistent && !containsTags(spec.definition->grantedTags)))
	{
		result.error = VansActionError::InvalidDefinition;
		result.message = "Effect references an unavailable Gameplay Tag";
		return result;
	}
	const auto containsCues = [this](const std::vector<VansCueId>& cues)
	{
		return cues.empty() || (m_Cues && std::all_of(cues.begin(), cues.end(),
			[this](VansCueId cue) { return m_Cues->Contains(cue); }));
	};
	if (!containsCues(spec.definition->executeCues) ||
		(persistent && (!containsCues(spec.definition->persistentCues) ||
			!containsCues(spec.definition->periodicCues) ||
			!containsCues(spec.definition->removeCues))))
	{
		result.error = VansActionError::Dependency;
		result.message = "Effect references an unavailable Gameplay Cue";
		return result;
	}
	for (const VansEffectModifier& modifier : spec.definition->modifiers)
	{
		if (!m_Attributes->Contains(modifier.attribute) ||
			(modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
				!m_Attributes->Contains(modifier.capturedAttribute)))
		{
			result.error = VansActionError::InvalidDefinition;
			result.message = "Effect modifier references an unavailable Attribute";
			return result;
		}
	}
	if (!PrepareSpec(spec, prepared, applicationSequence, result.message))
	{
		result.error = VansActionError::InvalidDefinition;
		return result;
	}
	return result;
}

VansEffectValidationResult VansGameplayEffectService::ValidatePrepared(
	const VansEffectSpec& prepared) const
{
	VansEffectValidationResult result;
	if (prepared.definition->durationPolicy == VansEffectDurationPolicy::Instant) return result;
	if (const VansActiveEffectHandle existing = FindStack(prepared); existing)
	{
		const VansActiveEffect* active = m_Active.Resolve(existing.value);
		if (!active)
		{
			result.error = VansActionError::Internal;
			result.message = "Effect stack handle became stale";
			return result;
		}
		if (active->StackCount() >= prepared.definition->maximumStacks &&
			prepared.definition->overflowPolicy == VansEffectOverflowPolicy::Reject)
		{
			result.error = VansActionError::Rejected;
			result.message = "Effect stack is full";
		}
		return result;
	}
	if (m_Performance.maximumEffectsPerHost == 0 ||
		m_Active.ActiveCount() >= m_Performance.maximumEffectsPerHost)
	{
		result.error = VansActionError::Budget;
		result.message = "Active Effect budget exceeded";
	}
	return result;
}

VansEffectApplicationResult VansGameplayEffectService::Apply(const VansEffectSpec& spec)
{
	VansEffectApplicationResult result;
	const std::uint64_t applicationSequence = m_NextApplicationSequence;
	VansEffectSpec prepared;
	VansEffectValidationResult validation = ValidateSpec(
		spec, prepared, applicationSequence, nullptr);
	if (validation) validation = ValidatePrepared(prepared);
	if (!validation)
	{
		result.error = validation.error;
		result.message = validation.message;
		return result;
	}
	if (prepared.definition->durationPolicy == VansEffectDurationPolicy::Instant)
	{
		const std::vector<VansAttributeBaseState> snapshot = m_Attributes->CaptureBases();
		if (!ApplyBaseModifiers(prepared, result.message))
		{
			result.error = VansActionError::Execution;
			return result;
		}
		VansActiveEffect transient;
		transient.stackStates.push_back({ prepared, applicationSequence, {} });
		if (!EmitExecuteCues(prepared.definition->executeCues, transient, result.message))
		{
			if (!m_Attributes->RestoreBases(snapshot))
				result.message = "Instant Effect Attribute rollback failed";
			result.error = VansActionError::Dependency;
			return result;
		}
		++m_NextApplicationSequence;
		return result;
	}

	if (const VansActiveEffectHandle existing = FindStack(prepared); existing)
	{
		VansActiveEffect* active = m_Active.Resolve(existing.value);
		if (!active)
		{
			result.error = VansActionError::Internal;
			result.message = "Effect stack handle became stale";
			return result;
		}
		const double previousRemaining = active->remainingSeconds;
		const double previousPeriod = active->periodRemainingSeconds;
		VansActiveEffectStack retiredStack;
		bool addedStack = false;
		bool replacedStack = false;
		if (active->StackCount() >= prepared.definition->maximumStacks)
		{
			if (prepared.definition->overflowPolicy == VansEffectOverflowPolicy::ReplaceOldest)
			{
				retiredStack = std::move(active->stackStates.front());
				active->stackStates.erase(active->stackStates.begin());
				active->stackStates.push_back({ prepared, applicationSequence, {} });
				addedStack = true;
				replacedStack = true;
			}
		}
		else
		{
			active->stackStates.push_back({ prepared, applicationSequence, {} });
			addedStack = true;
		}
		if (prepared.definition->refreshDurationOnStack)
			active->remainingSeconds = prepared.definition->durationSeconds;
		if (prepared.definition->resetPeriodOnStack)
			active->periodRemainingSeconds = prepared.definition->periodSeconds;
		if (!RebuildStackResources(existing, *active, result.message))
		{
			if (addedStack)
				active->stackStates.pop_back();
			if (replacedStack)
				active->stackStates.insert(active->stackStates.begin(), std::move(retiredStack));
			active->remainingSeconds = previousRemaining;
			active->periodRemainingSeconds = previousPeriod;
			std::string rollbackError;
			if (!RebuildStackResources(existing, *active, rollbackError))
				VANS_LOG_ERROR("[GAF] Effect stack rollback rebuild failed: " << rollbackError);
			result.error = VansActionError::Execution;
			return result;
		}
		if (replacedStack) ReleaseTargetData(retiredStack.ownedTargetData);
		result.active = existing;
		result.stacked = true;
		++m_NextApplicationSequence;
		return result;
	}
	VansActiveEffect active;
	active.stackStates.push_back({ prepared, applicationSequence, {} });
	active.remainingSeconds = prepared.definition->durationPolicy == VansEffectDurationPolicy::Duration ?
		prepared.definition->durationSeconds : -1.0;
	active.periodRemainingSeconds = prepared.definition->periodSeconds;
	const VansActiveEffectHandle handle{ m_Active.Emplace(std::move(active)) };
	VansActiveEffect* stored = m_Active.Resolve(handle.value);
	stored->modifierOrder = ModifierOrderFor(handle);
	stored->tagSource = SourceForHandle(handle);
	if (!ApplyPersistentResources(handle, *stored, true, result.message))
	{
		ReleaseResources(*stored);
		m_Active.Release(handle.value);
		result.error = VansActionError::Execution;
		return result;
	}
	if (prepared.definition->executePeriodicOnApply && prepared.definition->periodSeconds > 0.0 &&
		!ApplyBaseModifiers(*stored, result.message))
	{
		ReleaseResources(*stored);
		m_Active.Release(handle.value);
		result.error = VansActionError::Execution;
		return result;
	}
	result.active = handle;
	++m_NextApplicationSequence;
	return result;
}

VansEffectTickResult VansGameplayEffectService::Tick(double deltaSeconds)
{
	VansEffectTickResult result;
	if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return result;
	std::vector<VansActiveEffectHandle> handles;
	m_Active.ForEach([&](VansGenerationHandle handle, const VansActiveEffect&) { handles.push_back({ handle }); });
	std::vector<VansActiveEffectHandle> expired;
	const auto fail = [&result](
		VansActionError error,
		VansActiveEffectHandle effect,
		std::string message)
	{
		if (result.error != VansActionError::None) return;
		result.error = error;
		result.effect = effect;
		result.message = std::move(message);
	};
	for (VansActiveEffectHandle handle : handles)
	{
		VansActiveEffect* effect = m_Active.Resolve(handle.value);
		if (!effect) continue;
		const auto& definition = *effect->CurrentSpec().definition;
		double activeDeltaSeconds = deltaSeconds;
		bool shouldExpire = false;
		bool refreshFailed = false;
		if (UsesDynamicPersistentModifier(definition))
		{
			std::string refreshError;
			if (!RefreshDynamicModifiers(handle, *effect, refreshError))
			{
				shouldExpire = true;
				refreshFailed = true;
				fail(VansActionError::Execution, handle, std::move(refreshError));
			}
		}
		if (!refreshFailed && definition.durationPolicy == VansEffectDurationPolicy::Duration)
		{
			activeDeltaSeconds = std::min(deltaSeconds, std::max(0.0, effect->remainingSeconds));
			effect->remainingSeconds = std::max(0.0, effect->remainingSeconds - deltaSeconds);
			shouldExpire = effect->remainingSeconds <= 0.0;
		}
		if (!refreshFailed && definition.periodSeconds > 0.0 && activeDeltaSeconds > 0.0)
		{
			effect->periodRemainingSeconds -= activeDeltaSeconds;
			std::uint32_t executedPulses = 0;
			while (effect->periodRemainingSeconds <= 1e-12)
			{
				if (executedPulses >= m_Performance.maximumEffectPulsesPerTick)
				{
					shouldExpire = true;
					fail(VansActionError::Budget, handle,
						"Effect periodic pulse budget exceeded: " + definition.name);
					break;
				}
				std::string pulseError;
				if (!ApplyBaseModifiers(*effect, pulseError) ||
					!EmitExecuteCues(definition.periodicCues, *effect, pulseError))
				{
					shouldExpire = true;
					fail(VansActionError::Execution, handle, std::move(pulseError));
					break;
				}
				++executedPulses;
				++result.executedPulses;
				effect->periodRemainingSeconds += definition.periodSeconds;
			}
		}
		if (shouldExpire) expired.push_back(handle);
	}
	for (VansActiveEffectHandle handle : expired)
	{
		std::string removeError;
		if (Remove(handle, removeError)) ++result.removedEffects;
		else fail(VansActionError::Internal, handle, std::move(removeError));
	}
	return result;
}

bool VansGameplayEffectService::Remove(VansActiveEffectHandle handle, std::string& error)
{
	VansActiveEffect* effect = m_Active.Resolve(handle.value);
	if (!effect)
	{
		error = "Active Effect handle is stale";
		return false;
	}
	const bool cuesRemoved = EmitExecuteCues(
		effect->CurrentSpec().definition->removeCues, *effect, error);
	ReleaseResources(*effect);
	const bool released = m_Active.Release(handle.value);
	if (!released)
	{
		if (!error.empty()) error += "; ";
		error += "Active Effect storage release failed";
	}
	return cuesRemoved && released;
}

std::vector<VansActiveEffectSnapshot> VansGameplayEffectService::Snapshot() const
{
	std::vector<VansActiveEffectSnapshot> result;
	m_Active.ForEach([&](VansGenerationHandle handle, const VansActiveEffect& active)
	{
		const VansEffectSpec& spec = active.CurrentSpec();
		result.push_back({ { handle }, spec.definition->id, spec.source,
			active.remainingSeconds, active.periodRemainingSeconds, active.StackCount(),
			spec.context.correlationId });
	});
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
	{
		if (left.effect != right.effect) return left.effect < right.effect;
		return left.source < right.source;
	});
	return result;
}

bool VansGameplayEffectService::CapturePersistentState(
	VansPersistentEffectServiceState& state,
	std::string& error) const
{
	error.clear();
	state = {};
	state.nextApplicationSequence = m_NextApplicationSequence;
	bool succeeded = true;
	m_Active.ForEach([&](VansGenerationHandle, const VansActiveEffect& active)
	{
		if (!succeeded) return;
		VansPersistentEffectState saved;
		saved.effect = active.CurrentSpec().definition->id;
		saved.modifierOrder = active.modifierOrder;
		saved.remainingSeconds = active.remainingSeconds;
		saved.periodRemainingSeconds = active.periodRemainingSeconds;
		saved.stacks.reserve(active.stackStates.size());
		for (const VansActiveEffectStack& activeStack : active.stackStates)
		{
			const VansEffectSpec& spec = activeStack.spec;
			VansPersistentEffectStackState stack;
			stack.applicationSequence = activeStack.applicationSequence;
			if (!CapturePersistentContext(spec.context, m_TargetData, stack.context, error))
			{
				succeeded = false;
				break;
			}
			if (spec.targetData)
			{
				const VansTargetData* targetData = m_TargetData ?
					m_TargetData->Resolve(spec.targetData) : nullptr;
				if (!targetData)
				{
					error = "Persistent Effect references stale TargetData";
					succeeded = false;
					break;
				}
				stack.targetData = *targetData;
				stack.hasTargetData = true;
			}
			stack.source = spec.source;
			stack.level = spec.level;
			stack.setByCaller.reserve(spec.setByCaller.size());
			for (const auto& [field, value] : spec.setByCaller)
				stack.setByCaller.push_back({ field, value });
			std::sort(stack.setByCaller.begin(), stack.setByCaller.end(),
				[](const VansPersistentEffectSetByCallerState& left,
					const VansPersistentEffectSetByCallerState& right)
				{ return left.field < right.field; });
			for (std::size_t index = 0;
				index < spec.definition->modifiers.size(); ++index)
			{
				const VansEffectModifier& modifier = spec.definition->modifiers[index];
				if (!UsesSnapshotMagnitude(modifier)) continue;
				if (index >= spec.snapshotModifierValues.size() ||
					!std::isfinite(spec.snapshotModifierValues[index]))
				{
					error = "Persistent Effect magnitude snapshot is unavailable";
					succeeded = false;
					break;
				}
				stack.snapshots.push_back({ static_cast<std::uint32_t>(index),
					spec.snapshotModifierValues[index] });
			}
			if (!succeeded) break;
			saved.stacks.push_back(std::move(stack));
		}
		if (succeeded) state.activeEffects.push_back(std::move(saved));
	});
	if (!succeeded) state = {};
	return succeeded;
}

bool VansGameplayEffectService::ValidatePersistentState(
	const VansPersistentEffectServiceState& state,
	const VansEffectRegistry& registry,
	std::string& error) const
{
	error.clear();
	if (!registry.IsSealed() || !m_Attributes || !m_Tags ||
		state.nextApplicationSequence == 0 ||
		state.activeEffects.size() > m_Performance.maximumEffectsPerHost)
	{
		error = "Persistent Effect dependencies or active budget are invalid";
		return false;
	}
	struct VansEffectStackGroup
	{
		VansEffectId effect;
		std::uint64_t source = 0;
	};
	std::vector<VansEffectStackGroup> groups;
	std::vector<std::uint64_t> modifierOrders;
	std::vector<std::uint64_t> applicationSequences;
	for (const VansPersistentEffectState& saved : state.activeEffects)
	{
		if (std::find(modifierOrders.begin(), modifierOrders.end(), saved.modifierOrder) !=
			modifierOrders.end())
		{
			error = "Persistent Effect modifier order is duplicated";
			return false;
		}
		modifierOrders.push_back(saved.modifierOrder);
		const std::shared_ptr<const VansEffectDefinition> definition =
			registry.Resolve(saved.effect);
		if (!definition || definition->durationPolicy == VansEffectDurationPolicy::Instant ||
			!ValidateEffectDefinition(*definition, error) ||
			!ValidateEffectPerformanceBudget(*definition, m_Performance, error))
		{
			if (error.empty()) error = "Persistent Effect definition is invalid or unresolved";
			return false;
		}
		if (saved.stacks.empty() || saved.stacks.size() > definition->maximumStacks ||
			(definition->stackingPolicy == VansEffectStackingPolicy::None &&
				saved.stacks.size() != 1))
		{
			error = "Persistent Effect stack count is invalid";
			return false;
		}
		if (definition->durationPolicy == VansEffectDurationPolicy::Duration)
		{
			if (!std::isfinite(saved.remainingSeconds) || saved.remainingSeconds <= 0.0 ||
				saved.remainingSeconds > definition->durationSeconds + 1e-9)
			{
				error = "Persistent Duration Effect remaining time is invalid";
				return false;
			}
		}
		else if (saved.remainingSeconds != -1.0)
		{
			error = "Persistent Infinite Effect remaining time is invalid";
			return false;
		}
		if (definition->periodSeconds > 0.0)
		{
			if (!std::isfinite(saved.periodRemainingSeconds) ||
				saved.periodRemainingSeconds <= 0.0 ||
				saved.periodRemainingSeconds > definition->periodSeconds + 1e-9)
			{
				error = "Persistent Effect period time is invalid";
				return false;
			}
		}
		else if (saved.periodRemainingSeconds != 0.0)
		{
			error = "Non-periodic persistent Effect has period time";
			return false;
		}
		if (definition->stackingPolicy != VansEffectStackingPolicy::None)
		{
			const std::uint64_t groupSource =
				definition->stackingPolicy == VansEffectStackingPolicy::AggregateByTarget
				? 0 : saved.stacks.front().source;
			if (std::any_of(groups.begin(), groups.end(), [&](const VansEffectStackGroup& group)
				{ return group.effect == saved.effect && group.source == groupSource; }))
			{
				error = "Persistent Effect contains duplicate stack groups";
				return false;
			}
			groups.push_back({ saved.effect, groupSource });
		}
		const auto containsTags = [this](const std::vector<VansGameplayTagId>& tags)
		{
			return std::all_of(tags.begin(), tags.end(),
				[this](VansGameplayTagId tag) { return m_Tags->Contains(tag); });
		};
		if (!containsTags(definition->requirements.all) ||
			!containsTags(definition->requirements.any) ||
			!containsTags(definition->requirements.none) ||
			!containsTags(definition->immunity.all) ||
			!containsTags(definition->immunity.any) ||
			!containsTags(definition->immunity.none) ||
			!containsTags(definition->grantedTags))
		{
			error = "Persistent Effect references an unavailable Gameplay Tag";
			return false;
		}
		const auto containsCues = [this](const std::vector<VansCueId>& cues)
		{
			return cues.empty() || (m_Cues && std::all_of(cues.begin(), cues.end(),
				[this](VansCueId cue) { return m_Cues->Contains(cue); }));
		};
		if (!containsCues(definition->executeCues) ||
			!containsCues(definition->persistentCues) ||
			!containsCues(definition->periodicCues) ||
			!containsCues(definition->removeCues))
		{
			error = "Persistent Effect references an unavailable Gameplay Cue";
			return false;
		}
		for (const VansEffectModifier& modifier : definition->modifiers)
			if (!m_Attributes->Contains(modifier.attribute) ||
				(modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
					!m_Attributes->Contains(modifier.capturedAttribute)))
			{
				error = "Persistent Effect references an unavailable Attribute";
				return false;
			}

		for (const VansPersistentEffectStackState& stack : saved.stacks)
		{
			if (stack.applicationSequence == 0 ||
				stack.applicationSequence >= state.nextApplicationSequence ||
				std::find(applicationSequences.begin(), applicationSequences.end(),
					stack.applicationSequence) != applicationSequences.end() ||
				stack.source == 0 || !std::isfinite(stack.level) ||
				!ValidatePersistentContext(stack.context, error) ||
				(stack.hasTargetData &&
					!ValidatePersistentTargetData(stack.targetData, error)))
			{
				if (error.empty()) error = "Persistent Effect stack is invalid";
				return false;
			}
			applicationSequences.push_back(stack.applicationSequence);
			if (definition->stackingPolicy == VansEffectStackingPolicy::AggregateBySource &&
				stack.source != saved.stacks.front().source)
			{
				error = "Persistent Effect source stack is inconsistent";
				return false;
			}
			std::vector<VansActionFieldId> fields;
			for (const VansPersistentEffectSetByCallerState& value : stack.setByCaller)
			{
				if (!value.field || !std::isfinite(value.value) ||
					std::find(fields.begin(), fields.end(), value.field) != fields.end())
				{
					error = "Persistent Effect SetByCaller state is invalid";
					return false;
				}
				fields.push_back(value.field);
			}
			std::vector<std::uint32_t> snapshotIndices;
			for (const VansPersistentEffectSnapshotState& snapshot : stack.snapshots)
			{
				if (snapshot.modifierIndex >= definition->modifiers.size() ||
					!std::isfinite(snapshot.value) ||
					std::find(snapshotIndices.begin(), snapshotIndices.end(),
						snapshot.modifierIndex) != snapshotIndices.end())
				{
					error = "Persistent Effect snapshot state is invalid";
					return false;
				}
				const VansEffectModifier& modifier =
					definition->modifiers[snapshot.modifierIndex];
				if (!UsesSnapshotMagnitude(modifier))
				{
					error = "Persistent Effect snapshot targets a dynamic modifier";
					return false;
				}
				if (modifier.magnitudeSource == VansEffectMagnitudeSource::RandomRange &&
					(snapshot.value < modifier.randomMinimum ||
						snapshot.value > modifier.randomMaximum))
				{
					error = "Persistent Effect RandomRange snapshot is outside its range";
					return false;
				}
				snapshotIndices.push_back(snapshot.modifierIndex);
			}
			for (std::size_t index = 0; index < definition->modifiers.size(); ++index)
			{
				const VansEffectModifier& modifier = definition->modifiers[index];
				if (UsesSnapshotMagnitude(modifier) &&
					std::find(snapshotIndices.begin(), snapshotIndices.end(), index) ==
						snapshotIndices.end())
				{
					error = "Persistent Effect magnitude snapshot is missing";
					return false;
				}
				double magnitude = 0.0;
				if (!ResolvePersistentMagnitude(modifier, index,
					stack, *m_Attributes, magnitude, error) ||
					!std::isfinite(magnitude * stack.level))
				{
					if (error.empty()) error = "Persistent Effect stack magnitude is invalid";
					return false;
				}
			}
			const bool needsTargetData = std::any_of(stack.context.slots.begin(),
				stack.context.slots.end(), [](const VansPersistentEffectContextSlot& slot)
				{ return slot.kind == VansActionValueKind::TargetData; });
			if ((stack.hasTargetData || needsTargetData) && !m_TargetData)
			{
				error = "Persistent Effect TargetData store is unavailable";
				return false;
			}
		}
		for (std::size_t index = 0; index < definition->modifiers.size(); ++index)
		{
			const VansEffectModifier& modifier = definition->modifiers[index];
			double aggregate = modifier.operation ==
				VansEffectModifierOperation::Multiply ? 1.0 : 0.0;
			for (const VansPersistentEffectStackState& stack : saved.stacks)
			{
				double magnitude = 0.0;
				if (!ResolvePersistentMagnitude(modifier, index,
					stack, *m_Attributes, magnitude, error)) return false;
				magnitude *= stack.level;
				if (modifier.operation == VansEffectModifierOperation::Add)
					aggregate += magnitude;
				else if (modifier.operation == VansEffectModifierOperation::Multiply)
					aggregate *= magnitude;
				else aggregate = magnitude;
			}
			if (!std::isfinite(aggregate))
			{
				error = "Persistent Effect aggregate magnitude is non-finite";
				return false;
			}
		}
	}
	return true;
}

bool VansGameplayEffectService::RestorePersistentState(
	const VansPersistentEffectServiceState& state,
	const VansEffectRegistry& registry,
	std::string& error)
{
	if (!ValidatePersistentState(state, registry, error)) return false;
	ClearForRestore();
	for (const VansPersistentEffectState& saved : state.activeEffects)
	{
		VansActiveEffect active;
		const std::shared_ptr<const VansEffectDefinition> definition =
			registry.Resolve(saved.effect);
		active.remainingSeconds = saved.remainingSeconds;
		active.periodRemainingSeconds = saved.periodRemainingSeconds;
		active.modifierOrder = saved.modifierOrder;
		active.stackStates.reserve(saved.stacks.size());
		for (const VansPersistentEffectStackState& savedStack : saved.stacks)
		{
			VansEffectSpec spec;
			spec.definition = definition;
			std::vector<VansTargetDataHandle> ownedTargetData;
			if (!RestorePersistentContext(savedStack.context, m_TargetData,
				spec.context, ownedTargetData, error))
			{
				ReleaseTargetData(ownedTargetData);
				for (const VansActiveEffectStack& stack : active.stackStates)
					ReleaseTargetData(stack.ownedTargetData);
				ClearForRestore();
				return false;
			}
			if (savedStack.hasTargetData)
			{
				const VansTargetDataHandle handle = m_TargetData->Store(savedStack.targetData);
				ownedTargetData.push_back(handle);
				spec.targetData = handle;
			}
			spec.source = savedStack.source;
			spec.level = savedStack.level;
			for (const VansPersistentEffectSetByCallerState& value : savedStack.setByCaller)
				spec.setByCaller.emplace(value.field, value.value);
			spec.snapshotModifierValues.assign(definition->modifiers.size(),
				std::numeric_limits<double>::quiet_NaN());
			for (const VansPersistentEffectSnapshotState& snapshot : savedStack.snapshots)
				spec.snapshotModifierValues[snapshot.modifierIndex] = snapshot.value;
			active.stackStates.push_back({ std::move(spec),
				savedStack.applicationSequence, std::move(ownedTargetData) });
		}
		const VansActiveEffectHandle handle{ m_Active.Emplace(std::move(active)) };
		VansActiveEffect* stored = m_Active.Resolve(handle.value);
		stored->tagSource = SourceForHandle(handle);
		if (!ApplyPersistentResources(handle, *stored, false, error))
		{
			ReleaseResources(*stored);
			m_Active.Release(handle.value);
			ClearForRestore();
			return false;
		}
	}
	m_NextApplicationSequence = state.nextApplicationSequence;
	return true;
}

void VansGameplayEffectService::Clear()
{
	std::vector<VansActiveEffectHandle> handles;
	m_Active.ForEach([&](VansGenerationHandle handle, const VansActiveEffect&) { handles.push_back({ handle }); });
	for (const auto handle : handles)
	{
		std::string removeError;
		if (!Remove(handle, removeError))
			VANS_LOG_ERROR("[GAF] Effect service clear failed: " << removeError);
	}
}

void VansGameplayEffectService::ClearForRestore()
{
	std::vector<VansActiveEffectHandle> handles;
	m_Active.ForEach([&](VansGenerationHandle handle, const VansActiveEffect&)
		{ handles.push_back({ handle }); });
	for (VansActiveEffectHandle handle : handles)
	{
		VansActiveEffect* effect = m_Active.Resolve(handle.value);
		if (!effect) continue;
		ReleaseResources(*effect);
		if (!m_Active.Release(handle.value))
			VANS_LOG_ERROR("[GAF] Persistent Effect storage cleanup failed");
	}
}

VansActiveEffectHandle VansGameplayEffectService::FindStack(const VansEffectSpec& spec) const
{
	VansActiveEffectHandle result;
	if (spec.definition->stackingPolicy == VansEffectStackingPolicy::None) return result;
	m_Active.ForEach([&](VansGenerationHandle handle, const VansActiveEffect& active)
	{
		const VansEffectSpec& current = active.CurrentSpec();
		if (result || current.definition->id != spec.definition->id) return;
		if (spec.definition->stackingPolicy == VansEffectStackingPolicy::AggregateByTarget ||
			current.source == spec.source)
			result = { handle };
	});
	return result;
}

bool VansGameplayEffectService::ApplyBaseModifiers(
	const VansEffectSpec& spec,
	std::string& error)
{
	const std::vector<VansAttributeBaseState> snapshot = m_Attributes->CaptureBases();
	m_Attributes->BeginBatch();
	const auto rollback = [&]()
	{
		m_Attributes->EndBatch();
		if (!m_Attributes->RestoreBases(snapshot))
			error = "Effect Attribute rollback failed";
		return false;
	};
	for (std::size_t index = 0; index < spec.definition->modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = spec.definition->modifiers[index];
		if (modifier.application != VansEffectModifierApplication::Base) continue;
		double magnitude = 0.0;
		if (!ResolveMagnitude(modifier, index, spec, magnitude, error))
			return rollback();
		magnitude *= spec.level;
		VansAttributeBaseResult applied;
		switch (modifier.operation)
		{
		case VansEffectModifierOperation::Add:
			applied = m_Attributes->ApplyBase(modifier.attribute,
				VansAttributeBaseOperation::Add, magnitude);
			break;
		case VansEffectModifierOperation::Multiply:
			applied = m_Attributes->ApplyBase(modifier.attribute,
				VansAttributeBaseOperation::Multiply, magnitude);
			break;
		case VansEffectModifierOperation::Set:
			applied = m_Attributes->ApplyBase(modifier.attribute,
				VansAttributeBaseOperation::Set, magnitude);
			break;
		}
		if (!applied)
		{
			error = "Effect modifier references an unavailable Attribute";
			return rollback();
		}
	}
	m_Attributes->EndBatch();
	return true;
}

bool VansGameplayEffectService::ApplyBaseModifiers(
	const VansActiveEffect& effect,
	std::string& error)
{
	const VansEffectSpec& spec = effect.CurrentSpec();
	const std::vector<VansAttributeBaseState> snapshot = m_Attributes->CaptureBases();
	m_Attributes->BeginBatch();
	const auto rollback = [&]()
	{
		m_Attributes->EndBatch();
		if (!m_Attributes->RestoreBases(snapshot))
			error = "Effect stack Attribute rollback failed";
		return false;
	};
	for (std::size_t index = 0; index < spec.definition->modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = spec.definition->modifiers[index];
		if (modifier.application != VansEffectModifierApplication::Base) continue;
		double magnitude = 0.0;
		if (!AggregateMagnitude(modifier, index, effect.stackStates, magnitude, error))
			return rollback();
		VansAttributeBaseResult applied;
		switch (modifier.operation)
		{
		case VansEffectModifierOperation::Add:
			applied = m_Attributes->ApplyBase(modifier.attribute,
				VansAttributeBaseOperation::Add, magnitude);
			break;
		case VansEffectModifierOperation::Multiply:
			applied = m_Attributes->ApplyBase(modifier.attribute,
				VansAttributeBaseOperation::Multiply, magnitude);
			break;
		case VansEffectModifierOperation::Set:
			applied = m_Attributes->ApplyBase(modifier.attribute,
				VansAttributeBaseOperation::Set, magnitude);
			break;
		}
		if (!applied)
		{
			error = "Effect stack modifier references an unavailable Attribute";
			return rollback();
		}
	}
	m_Attributes->EndBatch();
	return true;
}

bool VansGameplayEffectService::ApplyPersistentResources(
	VansActiveEffectHandle handle,
	VansActiveEffect& effect,
	bool emitExecuteCues,
	std::string& error)
{
	const VansEffectSpec& spec = effect.CurrentSpec();
	const auto& definition = *spec.definition;
	for (VansGameplayTagId tag : definition.grantedTags)
	{
		if (!m_Tags->Add(tag, effect.tagSource, effect.StackCount()))
		{
			error = "Effect granted an unavailable Gameplay Tag";
			return false;
		}
	}
	if (UsesPersistentModifiers(definition))
	{
		for (std::size_t index = 0; index < definition.modifiers.size(); ++index)
		{
			const VansEffectModifier& modifier = definition.modifiers[index];
			if (modifier.application != VansEffectModifierApplication::Persistent) continue;
			VansAttributeModifierDesc desc;
			desc.attribute = modifier.attribute;
			desc.operation = AttributeOperation(modifier.operation);
			if (!AggregateMagnitude(modifier, index, effect.stackStates, desc.magnitude, error))
				return false;
			desc.priority = modifier.priority;
			desc.sourceOrder = effect.modifierOrder;
			desc.source = effect.tagSource;
			const auto modifierHandle = m_Attributes->AddModifier(desc);
			if (!modifierHandle)
			{
				error = "Effect modifier references an unavailable Attribute";
				return false;
			}
			effect.modifiers.push_back(modifierHandle);
		}
	}
	if (emitExecuteCues &&
		!EmitExecuteCues(definition.executeCues, effect, error)) return false;
	if (!definition.persistentCues.empty() && !m_Cues)
	{
		error = "Effect requires Gameplay Cue service";
		return false;
	}
	for (VansCueId cue : definition.persistentCues)
	{
		VansGameplayCueKey key{ spec.context.correlationId, cue,
			effect.tagSource != 0 ? effect.tagSource : spec.source, m_CueSequence++ };
		const VansCueHandle cueHandle = m_Cues->Add(key, std::nullopt,
			BuildCueParameters(effect), error);
		if (!cueHandle) return false;
		effect.cues.push_back(cueHandle);
	}
	return true;
}

bool VansGameplayEffectService::RebuildStackResources(
	VansActiveEffectHandle handle,
	VansActiveEffect& effect,
	std::string& error)
{
	const auto& definition = *effect.CurrentSpec().definition;
	m_Tags->RemoveSource(effect.tagSource);
	for (VansGameplayTagId tag : definition.grantedTags)
	{
		if (!m_Tags->Add(tag, effect.tagSource, effect.StackCount()))
		{
			error = "Effect stack granted an unavailable Gameplay Tag";
			return false;
		}
	}
	const std::size_t expectedModifierCount = PersistentModifierCount(definition);
	if (effect.modifiers.size() != expectedModifierCount)
	{
		error = "Effect modifier resource count changed";
		return false;
	}
	std::size_t persistentIndex = 0;
	for (std::size_t index = 0; index < definition.modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = definition.modifiers[index];
		if (modifier.application != VansEffectModifierApplication::Persistent) continue;
		VansAttributeModifierDesc desc;
		desc.attribute = modifier.attribute;
		desc.operation = AttributeOperation(modifier.operation);
		if (!AggregateMagnitude(modifier, index, effect.stackStates, desc.magnitude, error))
			return false;
		desc.priority = modifier.priority;
		desc.sourceOrder = effect.modifierOrder;
		desc.source = effect.tagSource;
		if (!m_Attributes->UpdateModifier(effect.modifiers[persistentIndex], desc))
		{
			error = "Effect stack modifier handle is stale";
			return false;
		}
		++persistentIndex;
	}
	if (!effect.cues.empty() && !m_Cues)
	{
		error = "Effect stack requires Gameplay Cue service";
		return false;
	}
	for (VansCueHandle cue : effect.cues)
		if (!m_Cues->Update(cue, BuildCueParameters(effect), error)) return false;
	return true;
}

bool VansGameplayEffectService::RefreshDynamicModifiers(
	VansActiveEffectHandle handle,
	VansActiveEffect& effect,
	std::string& error)
{
	const auto& modifiers = effect.CurrentSpec().definition->modifiers;
	if (effect.modifiers.size() != PersistentModifierCount(
		*effect.CurrentSpec().definition))
	{
		error = "Effect dynamic modifier resource count changed";
		return false;
	}
	std::size_t persistentIndex = 0;
	for (std::size_t index = 0; index < modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = modifiers[index];
		if (modifier.application != VansEffectModifierApplication::Persistent) continue;
		if (modifier.magnitudeSource != VansEffectMagnitudeSource::CapturedAttribute ||
			modifier.capturePolicy != VansEffectCapturePolicy::Dynamic)
		{
			++persistentIndex;
			continue;
		}
		VansAttributeModifierDesc desc;
		desc.attribute = modifier.attribute;
		desc.operation = AttributeOperation(modifier.operation);
		if (!AggregateMagnitude(modifier, index, effect.stackStates, desc.magnitude, error))
			return false;
		desc.priority = modifier.priority;
		desc.sourceOrder = effect.modifierOrder;
		desc.source = effect.tagSource;
		if (!m_Attributes->UpdateModifier(effect.modifiers[persistentIndex], desc))
		{
			error = "Effect dynamic modifier handle is stale";
			return false;
		}
		++persistentIndex;
	}
	return true;
}

void VansGameplayEffectService::ReleaseResources(VansActiveEffect& effect)
{
	for (auto it = effect.cues.rbegin(); it != effect.cues.rend(); ++it)
	{
		std::string removeError;
		if (m_Cues && !m_Cues->Remove(*it, removeError))
			VANS_LOG_ERROR("[GAF] Effect Cue cleanup failed: " << removeError);
	}
	effect.cues.clear();
	for (auto it = effect.modifiers.rbegin(); it != effect.modifiers.rend(); ++it)
		if (!m_Attributes->RemoveModifier(*it))
			VANS_LOG_ERROR("[GAF] Effect Attribute modifier cleanup failed");
	effect.modifiers.clear();
	m_Tags->RemoveSource(effect.tagSource);
	for (VansActiveEffectStack& stack : effect.stackStates)
	{
		ReleaseTargetData(stack.ownedTargetData);
		stack.ownedTargetData.clear();
	}
}

void VansGameplayEffectService::ReleaseTargetData(
	const std::vector<VansTargetDataHandle>& handles)
{
	for (auto iterator = handles.rbegin(); iterator != handles.rend(); ++iterator)
		if (m_TargetData && !m_TargetData->Release(*iterator))
			VANS_LOG_ERROR("[GAF] Effect TargetData cleanup failed");
}

std::uint64_t VansGameplayEffectService::ModifierOrderFor(
	VansActiveEffectHandle handle) const
{
	std::uint64_t order = handle.value.index;
	for (;; ++order)
	{
		bool used = false;
		m_Active.ForEach([&](VansGenerationHandle current, const VansActiveEffect& effect)
		{
			if (current.index != handle.value.index ||
				current.generation != handle.value.generation)
				used = used || effect.modifierOrder == order;
		});
		if (!used) return order;
	}
}

bool VansGameplayEffectService::EmitExecuteCues(
	const std::vector<VansCueId>& cues,
	const VansActiveEffect& effect,
	std::string& error)
{
	if (cues.empty()) return true;
	if (!m_Cues)
	{
		error = "Effect requires Gameplay Cue service";
		return false;
	}
	const VansEffectSpec& spec = effect.CurrentSpec();
	for (VansCueId cue : cues)
	{
		const VansGameplayCueKey key{ spec.context.correlationId, cue,
			effect.tagSource != 0 ? effect.tagSource : spec.source, m_CueSequence++ };
		if (m_Cues->Execute(key, std::nullopt, BuildCueParameters(effect), error) ==
			VansGameplayCueExecuteStatus::Failed)
			return false;
	}
	return true;
}

VansGameplayCueParameters VansGameplayEffectService::BuildCueParameters(const VansActiveEffect& effect) const
{
	const VansEffectSpec& spec = effect.CurrentSpec();
	VansGameplayCueParameters parameters;
	parameters.context = spec.context;
	parameters.target = spec.context.Entity(VansActionContextSlots::PrimaryTarget);
	if (const VansSerializedValue* payload =
		spec.context.Serialized(VansActionContextSlots::Payload))
		parameters.payload = *payload;
	const VansTargetDataHandle targetDataHandle = spec.targetData ? spec.targetData :
		spec.context.TargetData(VansActionContextSlots::TargetData);
	if (const VansTargetData* targetData = m_TargetData ?
		m_TargetData->Resolve(targetDataHandle) : nullptr)
		VansApplyGameplayCueTargetData(parameters, *targetData);
	parameters.intensity = 0.0;
	for (const VansActiveEffectStack& stack : effect.stackStates)
		parameters.intensity += stack.spec.level;
	return parameters;
}

std::uint64_t VansGameplayEffectService::SourceForHandle(VansActiveEffectHandle handle)
{
	return (static_cast<std::uint64_t>(handle.value.generation) << 32) |
		(static_cast<std::uint64_t>(handle.value.index) + 1ull);
}
}
