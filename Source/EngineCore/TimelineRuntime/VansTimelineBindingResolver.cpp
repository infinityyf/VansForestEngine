#include "VansTimelineBindingResolver.h"

#include "../SceneRuntime/VansRuntimeWorld.h"

#include <algorithm>

namespace Vans
{
void VansTimelineBindingResolver::BindWorld(VansRuntimeWorld* world, VansEntityHandle owner)
{
	if (m_World != world || m_Owner != owner)
	{
		m_World = world;
		m_Owner = owner;
		Invalidate();
	}
}

void VansTimelineBindingResolver::SetOverrides(
	const std::vector<VansTimelineBindingOverride>& overrides)
{
	m_Overrides = overrides;
	Invalidate();
}

void VansTimelineBindingResolver::SetRuntimeBindings(
	const std::vector<VansTimelineRuntimeBinding>& bindings)
{
	m_RuntimeBindings = bindings;
	Invalidate();
}

const VansTimelineBindingOverride* VansTimelineBindingResolver::FindOverride(
	VansTimelineBindingId bindingId) const
{
	const auto found = std::find_if(m_Overrides.begin(), m_Overrides.end(), [&](const auto& item)
	{
		return item.bindingId == bindingId;
	});
	return found == m_Overrides.end() ? nullptr : &*found;
}

const VansTimelineRuntimeBinding* VansTimelineBindingResolver::FindRuntimeBinding(
	VansTimelineBindingId bindingId) const
{
	const auto found = std::find_if(m_RuntimeBindings.begin(), m_RuntimeBindings.end(), [&](const auto& item)
	{
		return item.bindingId == bindingId;
	});
	return found == m_RuntimeBindings.end() ? nullptr : &*found;
}

bool VansTimelineBindingResolver::IsCachedTargetValid(
	const VansResolvedTimelineTarget& target) const
{
	if (!target.valid) return false;
	if (target.object.IsValid()) return true;
	if (!m_World) return false;
	if (target.entity.IsValid() && !m_World->IsAlive(target.entity)) return false;
	if (target.component.IsValid() && !m_World->GetComponentHeader(target.component)) return false;
	return true;
}

VansResolvedTimelineTarget VansTimelineBindingResolver::ResolveOne(
	const VansCompiledTimelineBinding& compiled,
	VansTimelineDiagnostics& diagnostics)
{
	const VansTimelineBinding& binding = compiled.authoring;
	VansResolvedTimelineTarget target;
	target.bindingId = compiled.id;
	target.kind = binding.kind;
	target.rootOwner = m_Owner;
	target.required = binding.required;
	target.assetGuid = binding.assetGuid;
	target.assetPath = binding.assetPath;
	if (const VansTimelineRuntimeBinding* runtime = FindRuntimeBinding(compiled.id))
	{
		target.objectType = runtime->objectType;
		target.object = runtime->object;
		target.changeSerial = runtime->changeSerial;
		target.valid = runtime->object.IsValid();
		return target;
	}
	if (!m_World)
	{
		diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
			"Timeline.BindingWorldMissing", {}, binding.id, "binding",
			"Runtime world is not registered" });
		return target;
	}

	std::string entityGuid = binding.targetGuid;
	std::string componentGuid = binding.componentGuid;
	std::uint16_t componentTypeId = binding.componentTypeId;
	if (const VansTimelineBindingOverride* overrideValue = FindOverride(compiled.id))
	{
		if (overrideValue->useOwner) target.entity = m_Owner;
		else if (!overrideValue->targetEntityGuid.empty()) entityGuid = overrideValue->targetEntityGuid;
		if (!overrideValue->targetComponentGuid.empty()) componentGuid = overrideValue->targetComponentGuid;
		if (overrideValue->targetComponentTypeId != 0) componentTypeId = overrideValue->targetComponentTypeId;
	}
	if (!target.entity.IsValid() && (entityGuid == "owner" || entityGuid.empty()) &&
		binding.kind != VansTimelineBindingKind::Asset && binding.kind != VansTimelineBindingKind::External)
		target.entity = m_Owner;
	if (!target.entity.IsValid() && !entityGuid.empty())
		target.entity = m_World->Entities().FindByGuid(entityGuid);

	if (binding.kind == VansTimelineBindingKind::SceneComponent ||
		binding.kind == VansTimelineBindingKind::UIComponent)
	{
		target.component = m_World->FindComponentByGuid(componentGuid, componentTypeId);
		if (const auto* header = m_World->GetComponentHeader(target.component)) target.entity = header->owner;
		target.valid = target.component.IsValid() && m_World->IsAlive(target.entity);
	}
	else if (binding.kind == VansTimelineBindingKind::Asset || binding.kind == VansTimelineBindingKind::External)
		target.valid = !target.assetGuid.empty() || !target.assetPath.empty();
	else target.valid = target.entity.IsValid() && m_World->IsAlive(target.entity);

	if (!target.valid)
		diagnostics.push_back({ binding.required ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
			"Timeline.BindingMissing", {}, binding.id, "binding",
			std::string(binding.required ? "Required" : "Optional") + " Timeline binding could not be resolved" });
	return target;
}

void VansTimelineBindingResolver::Resolve(
	const VansCompiledTimeline& timeline,
	VansTimelineDiagnostics& diagnostics)
{
	if (m_Cache.size() != timeline.Bindings().size()) m_Cache.resize(timeline.Bindings().size());
	for (const VansCompiledTimelineBinding& binding : timeline.Bindings())
	{
		if (binding.slot >= m_Cache.size()) continue;
		if (!IsCachedTargetValid(m_Cache[binding.slot])) m_Cache[binding.slot] = ResolveOne(binding, diagnostics);
	}
}

const VansResolvedTimelineTarget* VansTimelineBindingResolver::Find(std::uint32_t bindingSlot)
{
	if (bindingSlot >= m_Cache.size() || !IsCachedTargetValid(m_Cache[bindingSlot])) return nullptr;
	return &m_Cache[bindingSlot];
}

const VansResolvedTimelineTarget* VansTimelineBindingResolver::Find(
	VansTimelineBindingId bindingId,
	const VansCompiledTimeline& timeline,
	VansTimelineDiagnostics& diagnostics)
{
	Resolve(timeline, diagnostics);
	return Find(timeline.BindingSlot(bindingId));
}

void VansTimelineBindingResolver::Invalidate() { m_Cache.clear(); }
}
