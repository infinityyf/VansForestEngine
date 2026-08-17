#include "VansActionScheduler.h"

#include <cmath>

namespace Vans
{
VansActionSchedulerHandle VansActionScheduler::Register(
	std::shared_ptr<VansActionHost> host,
	std::string& error)
{
	if (!host || !host->IsInitialized())
	{
		error = "ActionScheduler requires an initialized Host";
		return {};
	}
	if (FindByOwner(host->Owner()))
	{
		error = "ActionScheduler already contains a Host for this owner";
		return {};
	}
	return { m_Hosts.Emplace(HostEntry{ host }) };
}

bool VansActionScheduler::Unregister(VansActionSchedulerHandle handle)
{
	return m_Hosts.Release(handle.value);
}

std::shared_ptr<VansActionHost> VansActionScheduler::FindByOwner(VansEntityHandle owner) const
{
	std::shared_ptr<VansActionHost> result;
	m_Hosts.ForEach([&](VansGenerationHandle, const HostEntry& entry)
	{
		if (result) return;
		if (auto host = entry.host.lock(); host && host->Owner() == owner)
			result = std::move(host);
	});
	return result;
}

std::vector<std::shared_ptr<VansActionHost>> VansActionScheduler::Hosts() const
{
	std::vector<std::shared_ptr<VansActionHost>> result;
	result.reserve(m_Hosts.ActiveCount());
	m_Hosts.ForEach([&](VansGenerationHandle, const HostEntry& entry)
	{
		if (auto host = entry.host.lock()) result.push_back(std::move(host));
	});
	return result;
}

void VansActionScheduler::TickEarly(double deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
	m_LateContinuationRan = false;
	std::vector<std::shared_ptr<VansActionHost>> hosts;
	m_Hosts.ForEach([&](VansGenerationHandle, const HostEntry& entry)
	{
		if (auto host = entry.host.lock()) hosts.push_back(std::move(host));
	});
	for (const auto& host : hosts) host->Tick(deltaSeconds);
	RemoveExpired();
}

bool VansActionScheduler::RunLateContinuation()
{
	if (m_LateContinuationRan) return false;
	m_LateContinuationRan = true;
	bool ran = false;
	std::vector<std::shared_ptr<VansActionHost>> hosts;
	m_Hosts.ForEach([&](VansGenerationHandle, const HostEntry& entry)
	{
		if (auto host = entry.host.lock()) hosts.push_back(std::move(host));
	});
	for (const auto& host : hosts) ran = host->RunLateContinuation() || ran;
	RemoveExpired();
	return ran;
}

void VansActionScheduler::Clear()
{
	m_Hosts.Clear();
	m_LateContinuationRan = false;
}

void VansActionScheduler::RemoveExpired()
{
	std::vector<VansActionSchedulerHandle> expired;
	m_Hosts.ForEach([&](VansGenerationHandle handle, const HostEntry& entry)
	{
		if (entry.host.expired()) expired.push_back({ handle });
	});
	for (VansActionSchedulerHandle handle : expired) m_Hosts.Release(handle.value);
}
}
