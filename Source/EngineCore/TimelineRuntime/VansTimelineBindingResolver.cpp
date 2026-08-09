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

void VansTimelineBindingResolver::SetOverrides(const std::vector<VansTimelineBindingOverride>& overrides)
{
	m_Overrides = overrides;
	Invalidate();
}

const VansTimelineBindingOverride* VansTimelineBindingResolver::FindOverride(const VansTimelineId& bindingId) const
{
	const auto found = std::find_if(m_Overrides.begin(), m_Overrides.end(), [&](const auto& item)
	{
		return item.bindingId == bindingId;
	});
	return found == m_Overrides.end() ? nullptr : &*found;
}

bool VansTimelineBindingResolver::IsCachedTargetValid(const VansResolvedTimelineTarget& target) const
{
	if (!m_World || !target.valid) return false;
	if (target.entity.IsValid() && !m_World->IsAlive(target.entity)) return false;
	if (target.component.IsValid() && !m_World->GetComponentHeader(target.component)) return false;
	return true;
}

VansResolvedTimelineTarget VansTimelineBindingResolver::ResolveOne(
	const VansTimelineBinding& binding,
	VansTimelineDiagnostics* diagnostics)
{
	const auto cached = m_Cache.find(binding.id);
	if (cached != m_Cache.end() && IsCachedTargetValid(cached->second))
		return cached->second;

	VansResolvedTimelineTarget target;
	target.bindingId = binding.id;
	target.kind = binding.kind;
	target.rootOwner = m_Owner;
	target.required = binding.required;
	target.assetGuid = binding.assetGuid;
	target.assetPath = binding.assetPath;
	if (!m_World)
	{
		if (diagnostics)
			diagnostics->push_back({ VansTimelineDiagnosticSeverity::Error, binding.id, "binding", "Runtime world is not registered" });
		return target;
	}

	std::string entityGuid = binding.targetGuid;
	std::string componentGuid = binding.componentGuid;
	std::uint16_t componentTypeId = binding.componentTypeId;
	if (const auto* overrideValue = FindOverride(binding.id))
	{
		if (overrideValue->useOwner)
			target.entity = m_Owner;
		else if (!overrideValue->targetEntityGuid.empty())
			entityGuid = overrideValue->targetEntityGuid;
		if (!overrideValue->targetComponentGuid.empty())
			componentGuid = overrideValue->targetComponentGuid;
		if (overrideValue->targetComponentTypeId != 0)
			componentTypeId = overrideValue->targetComponentTypeId;
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
		if (const auto* header = m_World->GetComponentHeader(target.component))
			target.entity = header->owner;
		target.valid = target.component.IsValid() && m_World->IsAlive(target.entity);
	}
	else if (binding.kind == VansTimelineBindingKind::Asset || binding.kind == VansTimelineBindingKind::External)
		target.valid = !target.assetGuid.empty() || !target.assetPath.empty();
	else
		target.valid = target.entity.IsValid() && m_World->IsAlive(target.entity);

	if (!target.valid && diagnostics)
	{
		diagnostics->push_back({
			binding.required ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
			binding.id,
			"binding",
			std::string(binding.required ? "Required" : "Optional") + " Timeline binding could not be resolved"
		});
	}
	m_Cache[binding.id] = target;
	return target;
}

void VansTimelineBindingResolver::Resolve(
	const std::vector<VansTimelineBinding>& bindings,
	VansTimelineDiagnostics& diagnostics)
{
	for (const auto& binding : bindings)
		ResolveOne(binding, &diagnostics);
}

const VansResolvedTimelineTarget* VansTimelineBindingResolver::Find(const VansTimelineId& bindingId)
{
	const auto found = m_Cache.find(bindingId);
	if (found == m_Cache.end() || !IsCachedTargetValid(found->second))
		return nullptr;
	return &found->second;
}

void VansTimelineBindingResolver::Invalidate()
{
	m_Cache.clear();
}
}
