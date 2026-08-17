#include "VansGameplayAttributes.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
bool VansAttributeRegistry::Register(VansAttributeDefinition definition, std::string& error)
{
	if (m_Sealed)
	{
		error = "Attribute registry is sealed";
		return false;
	}
	if (definition.name.empty())
	{
		error = "Attribute name is empty";
		return false;
	}
	if (!definition.id)
		definition.id = VansMakeStableId<VansAttributeIdTag>(definition.name);
	if (definition.fieldId == 0)
		definition.fieldId = VansStableHash64(definition.name);
	if (m_ById.find(definition.id) != m_ById.end() ||
		m_ByFieldId.find(definition.fieldId) != m_ByFieldId.end())
	{
		error = "duplicate Attribute id or FieldId: " + definition.name;
		return false;
	}
	if ((definition.hasMinimum && !std::isfinite(definition.minimum)) ||
		(definition.hasMaximum && !std::isfinite(definition.maximum)) ||
		!std::isfinite(definition.defaultValue) ||
		(definition.hasMinimum && definition.hasMaximum && definition.minimum > definition.maximum))
	{
		error = "invalid Attribute range: " + definition.name;
		return false;
	}
	const std::size_t index = m_Definitions.size();
	m_ById.emplace(definition.id, index);
	m_ByFieldId.emplace(definition.fieldId, index);
	m_Definitions.push_back(std::move(definition));
	return true;
}

bool VansAttributeRegistry::Seal(std::string& error)
{
	error.clear();
	m_Sealed = true;
	return true;
}

const VansAttributeDefinition* VansAttributeRegistry::Resolve(VansAttributeId id) const
{
	const auto found = m_ById.find(id);
	return found == m_ById.end() ? nullptr : &m_Definitions[found->second];
}

void VansAttributeService::SetRegistry(const VansAttributeRegistry* registry)
{
	m_Registry = registry;
	m_States.clear();
	m_Modifiers.Clear();
	m_Dirty.clear();
}

bool VansAttributeService::InitializeDefaults(std::string& error)
{
	if (!m_Registry || !m_Registry->IsSealed())
	{
		error = "Attribute registry is missing or not sealed";
		return false;
	}
	BeginBatch();
	for (const VansAttributeDefinition& definition : m_Registry->Definitions())
	{
		m_States[definition.id] = { definition.defaultValue, definition.defaultValue };
		MarkDirty(definition.id);
	}
	EndBatch();
	return true;
}

bool VansAttributeService::SetBase(VansAttributeId attribute, double value)
{
	if (!HasAttribute(attribute) || !std::isfinite(value)) return false;
	m_States[attribute].baseValue = value;
	MarkDirty(attribute);
	return true;
}

bool VansAttributeService::AddBase(VansAttributeId attribute, double delta)
{
	if (!HasAttribute(attribute) || !std::isfinite(delta)) return false;
	m_States[attribute].baseValue += delta;
	MarkDirty(attribute);
	return true;
}

double VansAttributeService::Base(VansAttributeId attribute) const
{
	const auto found = m_States.find(attribute);
	return found == m_States.end() ? 0.0 : found->second.baseValue;
}

double VansAttributeService::Current(VansAttributeId attribute) const
{
	const auto found = m_States.find(attribute);
	return found == m_States.end() ? 0.0 : found->second.currentValue;
}

VansAttributeModifierHandle VansAttributeService::AddModifier(const VansAttributeModifierDesc& desc)
{
	if (!HasAttribute(desc.attribute) || !std::isfinite(desc.magnitude)) return {};
	const VansGenerationHandle handle = m_Modifiers.Emplace(ModifierState{ desc });
	MarkDirty(desc.attribute);
	return { handle };
}

bool VansAttributeService::UpdateModifier(
	VansAttributeModifierHandle handle,
	const VansAttributeModifierDesc& desc)
{
	ModifierState* state = m_Modifiers.Resolve(handle.value);
	if (!state || !HasAttribute(desc.attribute) || !std::isfinite(desc.magnitude)) return false;
	const VansAttributeId previousAttribute = state->desc.attribute;
	state->desc = desc;
	BeginBatch();
	MarkDirty(previousAttribute);
	MarkDirty(desc.attribute);
	EndBatch();
	return true;
}

bool VansAttributeService::RemoveModifier(VansAttributeModifierHandle handle)
{
	ModifierState* state = m_Modifiers.Resolve(handle.value);
	if (!state) return false;
	const VansAttributeId attribute = state->desc.attribute;
	if (!m_Modifiers.Release(handle.value)) return false;
	MarkDirty(attribute);
	return true;
}

std::size_t VansAttributeService::RemoveModifiersFromSource(std::uint64_t source)
{
	std::vector<VansAttributeModifierHandle> removals;
	m_Modifiers.ForEach([&](VansGenerationHandle handle, const ModifierState& state)
	{
		if (state.desc.source == source) removals.push_back({ handle });
	});
	BeginBatch();
	for (VansAttributeModifierHandle handle : removals) RemoveModifier(handle);
	EndBatch();
	return removals.size();
}

std::vector<VansAttributeSnapshot> VansAttributeService::Capture() const
{
	std::vector<VansAttributeSnapshot> result;
	result.reserve(m_States.size());
	for (const auto& entry : m_States)
		result.push_back({ entry.first, entry.second.baseValue, entry.second.currentValue });
	std::sort(result.begin(), result.end(), [](const VansAttributeSnapshot& left, const VansAttributeSnapshot& right)
	{
		return left.attribute < right.attribute;
	});
	return result;
}

void VansAttributeService::Restore(const std::vector<VansAttributeSnapshot>& snapshot)
{
	BeginBatch();
	for (const VansAttributeSnapshot& item : snapshot)
	{
		const auto found = m_States.find(item.attribute);
		if (found == m_States.end()) continue;
		found->second.baseValue = item.baseValue;
		MarkDirty(item.attribute);
	}
	EndBatch();
}

void VansAttributeService::BeginBatch()
{
	++m_BatchDepth;
}

void VansAttributeService::EndBatch()
{
	if (m_BatchDepth == 0) return;
	--m_BatchDepth;
	if (m_BatchDepth == 0) FlushDirty();
}

bool VansAttributeService::HasAttribute(VansAttributeId attribute) const
{
	return m_States.find(attribute) != m_States.end();
}

double VansAttributeService::Evaluate(VansAttributeId attribute) const
{
	const auto state = m_States.find(attribute);
	if (state == m_States.end()) return 0.0;
	std::vector<VansAttributeModifierDesc> modifiers;
	m_Modifiers.ForEach([&](VansGenerationHandle, const ModifierState& modifier)
	{
		if (modifier.desc.attribute == attribute) modifiers.push_back(modifier.desc);
	});
	std::sort(modifiers.begin(), modifiers.end(), [](const auto& left, const auto& right)
	{
		if (left.priority != right.priority) return left.priority < right.priority;
		if (left.sourceOrder != right.sourceOrder) return left.sourceOrder < right.sourceOrder;
		return left.source < right.source;
	});
	double value = state->second.baseValue;
	for (const auto& modifier : modifiers)
		if (modifier.operation == VansAttributeModifierOperation::Additive) value += modifier.magnitude;
	for (const auto& modifier : modifiers)
		if (modifier.operation == VansAttributeModifierOperation::Multiplicative) value *= modifier.magnitude;
	for (const auto& modifier : modifiers)
		if (modifier.operation == VansAttributeModifierOperation::Override) value = modifier.magnitude;
	if (const VansAttributeDefinition* definition = m_Registry ? m_Registry->Resolve(attribute) : nullptr)
	{
		if (definition->hasMinimum) value = std::max(value, definition->minimum);
		if (definition->hasMaximum) value = std::min(value, definition->maximum);
	}
	return value;
}

void VansAttributeService::MarkDirty(VansAttributeId attribute)
{
	m_Dirty.insert(attribute);
	if (m_BatchDepth == 0) FlushDirty();
}

void VansAttributeService::FlushDirty()
{
	if (m_Dirty.empty()) return;
	std::vector<VansAttributeId> dirty(m_Dirty.begin(), m_Dirty.end());
	std::sort(dirty.begin(), dirty.end());
	m_Dirty.clear();
	for (VansAttributeId attribute : dirty)
	{
		const auto found = m_States.find(attribute);
		if (found == m_States.end()) continue;
		const double previous = found->second.currentValue;
		const double current = Evaluate(attribute);
		found->second.currentValue = current;
		if (m_Changed && previous != current) m_Changed(attribute, previous, current);
	}
}
}
