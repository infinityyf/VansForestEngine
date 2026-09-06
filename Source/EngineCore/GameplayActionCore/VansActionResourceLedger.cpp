#include "VansActionResourceLedger.h"

namespace Vans
{
VansActionResourceHandle VansActionResourceLedger::Register(
	VansActionResourceEntry entry,
	std::string& error)
{
	if (m_Released)
	{
		error = "Action ResourceLedger already released";
		return {};
	}
	if (entry.type.empty() || !entry.release)
	{
		error = "Action resource needs a type and release callback";
		return {};
	}
	if (entry.dependsOn && !m_Entries.Contains(entry.dependsOn.value))
	{
		error = "Action resource dependency is stale or not registered earlier";
		return {};
	}
	const VansActionResourceHandle handle{ m_Entries.Emplace(State{ std::move(entry) }) };
	m_Order.push_back(handle);
	return handle;
}

bool VansActionResourceLedger::Release(VansActionResourceHandle handle, std::string& error)
{
	State* state = m_Entries.Resolve(handle.value);
	if (!state)
	{
		error = "Action resource handle is stale";
		return false;
	}
	if (!state->entry.release())
	{
		error = "failed to release Action resource: " + state->entry.debugName;
		return false;
	}
	return m_Entries.Release(handle.value);
}

bool VansActionResourceLedger::Transfer(
	VansActionResourceHandle handle,
	VansActionResourceLedger& destination,
	VansActionResourceHandle& destinationHandle,
	std::string& error)
{
	destinationHandle = {};
	if (&destination == this || !destination.CanAccept())
	{
		error = "Action resource transfer destination is invalid or released";
		return false;
	}
	State* state = m_Entries.Resolve(handle.value);
	if (!state)
	{
		error = "Action resource handle is stale";
		return false;
	}
	bool hasDependent = false;
	m_Entries.ForEach([&](VansGenerationHandle, const State& candidate)
	{
		hasDependent = hasDependent || candidate.entry.dependsOn == handle;
	});
	if (hasDependent)
	{
		error = "Action resource cannot transfer while Action-owned dependents remain";
		return false;
	}
	VansActionResourceEntry transferred = state->entry;
	transferred.dependsOn = {};
	destinationHandle = destination.Register(std::move(transferred), error);
	if (!destinationHandle) return false;
	return m_Entries.Release(handle.value);
}

bool VansActionResourceLedger::ResolveExternal(
	VansActionResourceHandle handle,
	VansActionServiceId& service,
	VansGenerationHandle& externalResource) const
{
	const State* state = m_Entries.Resolve(handle.value);
	if (!state) return false;
	service = state->entry.service;
	externalResource = state->entry.externalResource;
	return static_cast<bool>(service) && static_cast<bool>(externalResource);
}

bool VansActionResourceLedger::ForgetExternalResource(
	VansActionServiceId service,
	VansGenerationHandle resource)
{
	if (!service || !resource) return false;
	for (auto it = m_Order.rbegin(); it != m_Order.rend(); ++it)
	{
		State* state = m_Entries.Resolve(it->value);
		if (!state || state->entry.service != service ||
			state->entry.externalResource != resource) continue;
		const auto handle = it->value;
		m_Order.erase(std::next(it).base());
		return m_Entries.Release(handle);
	}
	return false;
}

bool VansActionResourceLedger::ReleaseAll(std::vector<std::string>& errors)
{
	if (m_Released) return true;
	bool succeeded = true;
	for (auto it = m_Order.rbegin(); it != m_Order.rend(); ++it)
	{
		State* state = m_Entries.Resolve(it->value);
		if (!state) continue;
		std::string error;
		if (!Release(*it, error))
		{
			succeeded = false;
			errors.push_back(std::move(error));
		}
	}
	m_Order.clear();
	m_Released = true;
	return succeeded;
}

std::vector<VansActionResourceSnapshot> VansActionResourceLedger::Snapshot() const
{
	std::vector<VansActionResourceSnapshot> result;
	result.reserve(m_Entries.ActiveCount());
	m_Entries.ForEach([&](VansGenerationHandle handle, const State& state)
	{
		result.push_back({ { handle }, state.entry.type, state.entry.debugName,
			state.entry.dependsOn });
	});
	return result;
}
}
