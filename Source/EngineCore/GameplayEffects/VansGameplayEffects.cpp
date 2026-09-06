#include "VansGameplayEffects.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans
{
namespace
{
bool ValidateEffectDefinition(const VansEffectDefinition& definition, std::string& error)
{
	if (!definition.id || definition.name.empty())
	{
		error = "Effect identity is invalid";
		return false;
	}
	if (definition.durationPolicy == VansEffectDurationPolicy::Duration &&
		(!std::isfinite(definition.durationSeconds) || definition.durationSeconds <= 0.0))
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

bool UsesPersistentModifiers(const VansEffectDefinition& definition)
{
	return definition.periodSeconds <= 0.0;
}

bool UsesDynamicCapture(const VansEffectDefinition& definition)
{
	return std::any_of(definition.modifiers.begin(), definition.modifiers.end(),
		[](const VansEffectModifier& modifier)
		{
			return modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
				modifier.capturePolicy == VansEffectCapturePolicy::Dynamic;
		});
}

std::uint64_t SplitMix64(std::uint64_t value)
{
	value += 0x9e3779b97f4a7c15ull;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
	return value ^ (value >> 31);
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

std::shared_ptr<const VansEffectDefinition> VansEffectRegistry::Resolve(VansEffectId id) const
{
	const auto found = m_Definitions.find(id);
	return found == m_Definitions.end() ? nullptr : found->second;
}

bool VansGameplayEffectService::PrepareSpec(
	const VansEffectSpec& source,
	VansEffectSpec& prepared,
	std::string& error) const
{
	prepared = source;
	if (!prepared.definition) return false;
	prepared.capturedModifierValues.assign(prepared.definition->modifiers.size(),
		std::numeric_limits<double>::quiet_NaN());
	for (std::size_t index = 0; index < prepared.definition->modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = prepared.definition->modifiers[index];
		if (modifier.magnitudeSource == VansEffectMagnitudeSource::CapturedAttribute &&
			modifier.capturePolicy == VansEffectCapturePolicy::Snapshot)
			prepared.capturedModifierValues[index] = m_Attributes->Current(modifier.capturedAttribute);
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
			if (modifierIndex >= spec.capturedModifierValues.size() ||
				!std::isfinite(spec.capturedModifierValues[modifierIndex]))
			{
				error = "Effect Snapshot capture is unavailable";
				return false;
			}
			value = spec.capturedModifierValues[modifierIndex];
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
		if (modifier.targetDataMetric == VansEffectTargetDataMetric::Count)
			value = static_cast<double>(data->values.size());
		else
		{
			bool found = false;
			for (const VansTargetDataValue& target : data->values)
			{
				if (modifier.targetDataMetric == VansEffectTargetDataMetric::HitDistance)
					if (const auto* hit = std::get_if<VansTargetHitResult>(&target))
					{
						value = hit->distance;
						found = true;
					}
				if (modifier.targetDataMetric == VansEffectTargetDataMetric::AreaRadius)
					if (const auto* area = std::get_if<VansTargetArea>(&target))
					{
						value = area->radius;
						found = true;
					}
				if (modifier.targetDataMetric == VansEffectTargetDataMetric::RayLength)
					if (const auto* ray = std::get_if<VansTargetRay>(&target))
					{
						value = ray->length;
						found = true;
					}
				if (found) break;
			}
			if (!found)
			{
				error = "Effect TargetData does not contain the requested metric";
				return false;
			}
		}
		break;
	}
	case VansEffectMagnitudeSource::RandomRange:
	{
		const std::uint64_t seed = SplitMix64(spec.context.randomSeed ^
			spec.definition->id.value ^ (static_cast<std::uint64_t>(modifierIndex) << 32));
		const double unit = static_cast<double>(seed >> 11) * (1.0 / 9007199254740992.0);
		value = modifier.randomMinimum +
			(modifier.randomMaximum - modifier.randomMinimum) * unit;
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
	const std::vector<VansEffectSpec>& stackSpecs,
	double& magnitude,
	std::string& error) const
{
	if (stackSpecs.empty())
	{
		magnitude = 0.0;
		return true;
	}
	if (modifier.operation == VansAttributeModifierOperation::Multiplicative)
		magnitude = 1.0;
	else magnitude = 0.0;
	for (const VansEffectSpec& spec : stackSpecs)
	{
		double resolved = 0.0;
		if (!ResolveMagnitude(modifier, modifierIndex, spec, resolved, error)) return false;
		resolved *= spec.level;
		if (modifier.operation == VansAttributeModifierOperation::Additive) magnitude += resolved;
		else if (modifier.operation == VansAttributeModifierOperation::Multiplicative)
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

VansEffectApplicationResult VansGameplayEffectService::Apply(const VansEffectSpec& spec)
{
	VansEffectApplicationResult result;
	std::string error;
	if (!spec.definition || !ValidateEffectDefinition(*spec.definition, error) ||
		!m_Attributes || !m_Tags || spec.source == 0 || !std::isfinite(spec.level))
	{
		result.error = VansActionError::InvalidDefinition;
		result.message = error.empty() ? "Effect service or spec is invalid" : std::move(error);
		return result;
	}
	if (!m_Tags->Matches(spec.definition->requirements))
	{
		result.error = VansActionError::Rejected;
		result.message = "Effect requirements failed";
		return result;
	}
	const bool hasImmunityQuery = !spec.definition->immunity.all.empty() ||
		!spec.definition->immunity.any.empty() || !spec.definition->immunity.none.empty();
	if (hasImmunityQuery && m_Tags->Matches(spec.definition->immunity))
	{
		result.error = VansActionError::Rejected;
		result.message = "Effect was blocked by immunity";
		return result;
	}
	VansEffectSpec prepared;
	if (!PrepareSpec(spec, prepared, result.message))
	{
		result.error = VansActionError::InvalidDefinition;
		return result;
	}
	if (prepared.definition->durationPolicy == VansEffectDurationPolicy::Instant)
	{
		const std::vector<VansAttributeSnapshot> snapshot = m_Attributes->Capture();
		if (!ApplyInstantModifiers(prepared, 1, result.message))
		{
			result.error = VansActionError::Execution;
			return result;
		}
		ActiveEffect transient;
		transient.spec = prepared;
		transient.stackSpecs.push_back(prepared);
		transient.stacks = 1;
		if (!EmitExecuteCues(prepared.definition->executeCues, transient, result.message))
		{
			m_Attributes->Restore(snapshot);
			result.error = VansActionError::Dependency;
			return result;
		}
		return result;
	}

	if (const VansActiveEffectHandle existing = FindStack(prepared); existing)
	{
		ActiveEffect* active = m_Active.Resolve(existing.value);
		if (!active)
		{
			result.error = VansActionError::Internal;
			result.message = "Effect stack handle became stale";
			return result;
		}
		const VansEffectSpec previousSpec = active->spec;
		const std::vector<VansEffectSpec> previousStackSpecs = active->stackSpecs;
		const double previousRemaining = active->remainingSeconds;
		const double previousPeriod = active->periodRemainingSeconds;
		if (active->stacks >= prepared.definition->maximumStacks)
		{
			if (prepared.definition->overflowPolicy == VansEffectOverflowPolicy::Reject)
			{
				result.error = VansActionError::Rejected;
				result.message = "Effect stack is full";
				return result;
			}
			if (prepared.definition->overflowPolicy == VansEffectOverflowPolicy::ReplaceOldest)
			{
				if (!active->stackSpecs.empty()) active->stackSpecs.erase(active->stackSpecs.begin());
				active->stackSpecs.push_back(prepared);
			}
		}
		else
		{
			active->stackSpecs.push_back(prepared);
		}
		active->stacks = static_cast<std::uint32_t>(active->stackSpecs.size());
		if (!active->stackSpecs.empty()) active->spec = active->stackSpecs.back();
		if (prepared.definition->refreshDurationOnStack)
			active->remainingSeconds = prepared.definition->durationSeconds;
		if (prepared.definition->resetPeriodOnStack)
			active->periodRemainingSeconds = prepared.definition->periodSeconds;
		if (!RebuildStackResources(existing, *active, result.message))
		{
			active->spec = previousSpec;
			active->stackSpecs = previousStackSpecs;
			active->stacks = static_cast<std::uint32_t>(active->stackSpecs.size());
			active->remainingSeconds = previousRemaining;
			active->periodRemainingSeconds = previousPeriod;
			std::string ignored;
			RebuildStackResources(existing, *active, ignored);
			result.error = VansActionError::Execution;
			return result;
		}
		result.active = existing;
		result.stacked = true;
		return result;
	}
	if (m_MaximumActiveEffects == 0 || m_Active.ActiveCount() >= m_MaximumActiveEffects)
	{
		result.error = VansActionError::Budget;
		result.message = "Active Effect budget exceeded";
		return result;
	}

	ActiveEffect active;
	active.spec = prepared;
	active.stackSpecs.push_back(prepared);
	active.remainingSeconds = prepared.definition->durationPolicy == VansEffectDurationPolicy::Duration ?
		prepared.definition->durationSeconds : -1.0;
	active.periodRemainingSeconds = prepared.definition->periodSeconds;
	const VansActiveEffectHandle handle{ m_Active.Emplace(std::move(active)) };
	ActiveEffect* stored = m_Active.Resolve(handle.value);
	stored->tagSource = SourceForHandle(handle);
	if (!ApplyPersistentResources(handle, *stored, result.message))
	{
		ReleaseResources(*stored);
		m_Active.Release(handle.value);
		result.error = VansActionError::Execution;
		return result;
	}
	if (prepared.definition->executePeriodicOnApply && prepared.definition->periodSeconds > 0.0 &&
		!ApplyStackModifiers(*stored, result.message))
	{
		ReleaseResources(*stored);
		m_Active.Release(handle.value);
		result.error = VansActionError::Execution;
		return result;
	}
	result.active = handle;
	return result;
}

void VansGameplayEffectService::Tick(double deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) return;
	std::vector<VansActiveEffectHandle> handles;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect&) { handles.push_back({ handle }); });
	std::vector<VansActiveEffectHandle> expired;
	for (VansActiveEffectHandle handle : handles)
	{
		ActiveEffect* effect = m_Active.Resolve(handle.value);
		if (!effect) continue;
		const auto& definition = *effect->spec.definition;
		double activeDeltaSeconds = deltaSeconds;
		bool shouldExpire = false;
		bool refreshFailed = false;
		if (UsesPersistentModifiers(definition) && UsesDynamicCapture(definition))
		{
			std::string refreshError;
			if (!RefreshDynamicModifiers(handle, *effect, refreshError))
			{
				shouldExpire = true;
				refreshFailed = true;
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
			std::uint32_t iterationGuard = 0;
			while (effect->periodRemainingSeconds <= 1e-12 && iterationGuard++ < 1024)
			{
				std::string pulseError;
				if (!ApplyStackModifiers(*effect, pulseError) ||
					!EmitExecuteCues(definition.periodicCues, *effect, pulseError))
				{
					shouldExpire = true;
					break;
				}
				effect->periodRemainingSeconds += definition.periodSeconds;
			}
		}
		if (shouldExpire) expired.push_back(handle);
	}
	for (VansActiveEffectHandle handle : expired)
	{
		std::string ignored;
		Remove(handle, ignored);
	}
}

bool VansGameplayEffectService::Remove(VansActiveEffectHandle handle, std::string& error)
{
	ActiveEffect* effect = m_Active.Resolve(handle.value);
	if (!effect)
	{
		error = "Active Effect handle is stale";
		return false;
	}
	std::string cueError;
	EmitExecuteCues(effect->spec.definition->removeCues, *effect, cueError);
	ReleaseResources(*effect);
	if (!cueError.empty()) error = std::move(cueError);
	return m_Active.Release(handle.value);
}

std::size_t VansGameplayEffectService::RemoveByEffect(VansEffectId effect)
{
	std::vector<VansActiveEffectHandle> removals;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect& active)
	{
		if (active.spec.definition->id == effect) removals.push_back({ handle });
	});
	for (const auto handle : removals) { std::string ignored; Remove(handle, ignored); }
	return removals.size();
}

std::size_t VansGameplayEffectService::RemoveBySource(std::uint64_t source)
{
	std::vector<VansActiveEffectHandle> affected;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect& active)
	{
		if (std::any_of(active.stackSpecs.begin(), active.stackSpecs.end(),
			[source](const VansEffectSpec& spec) { return spec.source == source; }))
			affected.push_back({ handle });
	});
	for (VansActiveEffectHandle handle : affected)
	{
		ActiveEffect* active = m_Active.Resolve(handle.value);
		if (!active) continue;
		active->stackSpecs.erase(std::remove_if(active->stackSpecs.begin(), active->stackSpecs.end(),
			[source](const VansEffectSpec& spec) { return spec.source == source; }),
			active->stackSpecs.end());
		if (active->stackSpecs.empty())
		{
			std::string ignored;
			Remove(handle, ignored);
			continue;
		}
		active->stacks = static_cast<std::uint32_t>(active->stackSpecs.size());
		active->spec = active->stackSpecs.back();
		std::string rebuildError;
		if (!RebuildStackResources(handle, *active, rebuildError))
		{
			std::string ignored;
			Remove(handle, ignored);
		}
	}
	return affected.size();
}

std::size_t VansGameplayEffectService::RemoveMatchingTags(const VansGameplayTagQuery& query)
{
	std::vector<VansActiveEffectHandle> removals;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect& active)
	{
		VansGameplayTagContainer tags;
		for (VansGameplayTagId tag : active.spec.definition->effectTags) tags.Add(tag, 1);
		VansGameplayTagQuery exactQuery = query;
		exactQuery.exact = true;
		if (tags.Matches(exactQuery)) removals.push_back({ handle });
	});
	for (const auto handle : removals) { std::string ignored; Remove(handle, ignored); }
	return removals.size();
}

std::vector<VansActiveEffectSnapshot> VansGameplayEffectService::Snapshot() const
{
	std::vector<VansActiveEffectSnapshot> result;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect& active)
	{
		result.push_back({ { handle }, active.spec.definition->id, active.spec.source,
			active.remainingSeconds, active.periodRemainingSeconds, active.stacks,
			active.spec.context.correlationId });
	});
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
	{
		if (left.effect != right.effect) return left.effect < right.effect;
		return left.source < right.source;
	});
	return result;
}

void VansGameplayEffectService::Clear()
{
	std::vector<VansActiveEffectHandle> handles;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect&) { handles.push_back({ handle }); });
	for (const auto handle : handles) { std::string ignored; Remove(handle, ignored); }
}

VansActiveEffectHandle VansGameplayEffectService::FindStack(const VansEffectSpec& spec) const
{
	VansActiveEffectHandle result;
	if (spec.definition->stackingPolicy == VansEffectStackingPolicy::None) return result;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveEffect& active)
	{
		if (result || active.spec.definition->id != spec.definition->id) return;
		if (spec.definition->stackingPolicy == VansEffectStackingPolicy::AggregateByTarget ||
			active.spec.source == spec.source)
			result = { handle };
	});
	return result;
}

bool VansGameplayEffectService::ApplyInstantModifiers(
	const VansEffectSpec& spec,
	std::uint32_t stacks,
	std::string& error)
{
	const std::vector<VansAttributeSnapshot> snapshot = m_Attributes->Capture();
	m_Attributes->BeginBatch();
	for (std::size_t index = 0; index < spec.definition->modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = spec.definition->modifiers[index];
		double magnitude = 0.0;
		if (!ResolveMagnitude(modifier, index, spec, magnitude, error))
		{
			m_Attributes->EndBatch();
			m_Attributes->Restore(snapshot);
			return false;
		}
		magnitude *= spec.level;
		if (modifier.operation == VansAttributeModifierOperation::Multiplicative)
			magnitude = std::pow(magnitude, static_cast<double>(stacks));
		else if (modifier.operation == VansAttributeModifierOperation::Additive)
			magnitude *= static_cast<double>(stacks);
		bool applied = false;
		switch (modifier.operation)
		{
		case VansAttributeModifierOperation::Additive:
			applied = m_Attributes->AddBase(modifier.attribute, magnitude);
			break;
		case VansAttributeModifierOperation::Multiplicative:
			applied = m_Attributes->SetBase(modifier.attribute,
				m_Attributes->Base(modifier.attribute) * magnitude);
			break;
		case VansAttributeModifierOperation::Override:
			applied = m_Attributes->SetBase(modifier.attribute, magnitude);
			break;
		}
		if (!applied)
		{
			m_Attributes->EndBatch();
			m_Attributes->Restore(snapshot);
			error = "Effect modifier references an unavailable Attribute";
			return false;
		}
	}
	m_Attributes->EndBatch();
	return true;
}

bool VansGameplayEffectService::ApplyStackModifiers(
	const ActiveEffect& effect,
	std::string& error)
{
	const std::vector<VansAttributeSnapshot> snapshot = m_Attributes->Capture();
	m_Attributes->BeginBatch();
	for (std::size_t index = 0; index < effect.spec.definition->modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = effect.spec.definition->modifiers[index];
		double magnitude = 0.0;
		if (!AggregateMagnitude(modifier, index, effect.stackSpecs, magnitude, error))
		{
			m_Attributes->EndBatch();
			m_Attributes->Restore(snapshot);
			return false;
		}
		bool applied = false;
		switch (modifier.operation)
		{
		case VansAttributeModifierOperation::Additive:
			applied = m_Attributes->AddBase(modifier.attribute, magnitude);
			break;
		case VansAttributeModifierOperation::Multiplicative:
			applied = m_Attributes->SetBase(modifier.attribute,
				m_Attributes->Base(modifier.attribute) * magnitude);
			break;
		case VansAttributeModifierOperation::Override:
			applied = m_Attributes->SetBase(modifier.attribute, magnitude);
			break;
		}
		if (!applied)
		{
			m_Attributes->EndBatch();
			m_Attributes->Restore(snapshot);
			error = "Effect stack modifier references an unavailable Attribute";
			return false;
		}
	}
	m_Attributes->EndBatch();
	return true;
}

bool VansGameplayEffectService::ApplyPersistentResources(
	VansActiveEffectHandle handle,
	ActiveEffect& effect,
	std::string& error)
{
	const auto& definition = *effect.spec.definition;
	m_Tags->BeginBatch();
	for (VansGameplayTagId tag : definition.grantedTags)
	{
		if (!m_Tags->Add(tag, effect.tagSource, effect.stacks))
		{
			m_Tags->EndBatch();
			error = "Effect granted an unavailable Gameplay Tag";
			return false;
		}
	}
	m_Tags->EndBatch();
	if (UsesPersistentModifiers(definition))
	{
		for (std::size_t index = 0; index < definition.modifiers.size(); ++index)
		{
			const VansEffectModifier& modifier = definition.modifiers[index];
			VansAttributeModifierDesc desc;
			desc.attribute = modifier.attribute;
			desc.operation = modifier.operation;
			if (!AggregateMagnitude(modifier, index, effect.stackSpecs, desc.magnitude, error))
				return false;
			desc.priority = modifier.priority;
			desc.sourceOrder = handle.value.index;
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
	if (!EmitExecuteCues(definition.executeCues, effect, error)) return false;
	if (!definition.persistentCues.empty() && !m_Cues)
	{
		error = "Effect requires Gameplay Cue service";
		return false;
	}
	for (VansCueId cue : definition.persistentCues)
	{
		VansGameplayCueKey key{ effect.spec.context.correlationId, cue, m_CueSequence++ };
		const VansCueHandle cueHandle = m_Cues->Add(key, m_Cues->DefaultScope(cue),
			BuildCueParameters(effect), effect.tagSource, error);
		if (!cueHandle) return false;
		effect.cues.push_back(cueHandle);
	}
	return true;
}

bool VansGameplayEffectService::RebuildStackResources(
	VansActiveEffectHandle handle,
	ActiveEffect& effect,
	std::string& error)
{
	const auto& definition = *effect.spec.definition;
	m_Tags->RemoveSource(effect.tagSource);
	m_Tags->BeginBatch();
	for (VansGameplayTagId tag : definition.grantedTags)
	{
		if (!m_Tags->Add(tag, effect.tagSource, effect.stacks))
		{
			m_Tags->EndBatch();
			error = "Effect stack granted an unavailable Gameplay Tag";
			return false;
		}
	}
	m_Tags->EndBatch();
	const std::size_t expectedModifierCount =
		UsesPersistentModifiers(definition) ? definition.modifiers.size() : 0;
	if (effect.modifiers.size() != expectedModifierCount)
	{
		error = "Effect modifier resource count changed";
		return false;
	}
	for (std::size_t index = 0; index < expectedModifierCount; ++index)
	{
		const VansEffectModifier& modifier = definition.modifiers[index];
		VansAttributeModifierDesc desc;
		desc.attribute = modifier.attribute;
		desc.operation = modifier.operation;
		if (!AggregateMagnitude(modifier, index, effect.stackSpecs, desc.magnitude, error))
			return false;
		desc.priority = modifier.priority;
		desc.sourceOrder = handle.value.index;
		desc.source = effect.tagSource;
		if (!m_Attributes->UpdateModifier(effect.modifiers[index], desc))
		{
			error = "Effect stack modifier handle is stale";
			return false;
		}
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
	ActiveEffect& effect,
	std::string& error)
{
	const auto& modifiers = effect.spec.definition->modifiers;
	if (effect.modifiers.size() != modifiers.size())
	{
		error = "Effect dynamic modifier resource count changed";
		return false;
	}
	for (std::size_t index = 0; index < modifiers.size(); ++index)
	{
		const VansEffectModifier& modifier = modifiers[index];
		if (modifier.magnitudeSource != VansEffectMagnitudeSource::CapturedAttribute ||
			modifier.capturePolicy != VansEffectCapturePolicy::Dynamic) continue;
		VansAttributeModifierDesc desc;
		desc.attribute = modifier.attribute;
		desc.operation = modifier.operation;
		if (!AggregateMagnitude(modifier, index, effect.stackSpecs, desc.magnitude, error))
			return false;
		desc.priority = modifier.priority;
		desc.sourceOrder = handle.value.index;
		desc.source = effect.tagSource;
		if (!m_Attributes->UpdateModifier(effect.modifiers[index], desc))
		{
			error = "Effect dynamic modifier handle is stale";
			return false;
		}
	}
	return true;
}

void VansGameplayEffectService::ReleaseResources(ActiveEffect& effect)
{
	for (auto it = effect.cues.rbegin(); it != effect.cues.rend(); ++it)
	{
		std::string ignored;
		if (m_Cues) m_Cues->Remove(*it, ignored);
	}
	effect.cues.clear();
	for (auto it = effect.modifiers.rbegin(); it != effect.modifiers.rend(); ++it)
		m_Attributes->RemoveModifier(*it);
	effect.modifiers.clear();
	m_Tags->RemoveSource(effect.tagSource);
}

bool VansGameplayEffectService::EmitExecuteCues(
	const std::vector<VansCueId>& cues,
	const ActiveEffect& effect,
	std::string& error)
{
	if (cues.empty()) return true;
	if (!m_Cues)
	{
		error = "Effect requires Gameplay Cue service";
		return false;
	}
	for (VansCueId cue : cues)
	{
		const VansGameplayCueKey key{ effect.spec.context.correlationId, cue, m_CueSequence++ };
		if (!m_Cues->Execute(key, m_Cues->DefaultScope(cue), BuildCueParameters(effect), error))
			return false;
	}
	return true;
}

VansGameplayCueParameters VansGameplayEffectService::BuildCueParameters(const ActiveEffect& effect) const
{
	VansGameplayCueParameters parameters;
	parameters.context = effect.spec.context;
	parameters.target = effect.spec.context.Entity(VansActionContextSlots::PrimaryTarget);
	parameters.intensity = 0.0;
	for (const VansEffectSpec& stack : effect.stackSpecs) parameters.intensity += stack.level;
	return parameters;
}

std::uint64_t VansGameplayEffectService::SourceForHandle(VansActiveEffectHandle handle)
{
	return (static_cast<std::uint64_t>(handle.value.generation) << 32) |
		(static_cast<std::uint64_t>(handle.value.index) + 1ull);
}
}
