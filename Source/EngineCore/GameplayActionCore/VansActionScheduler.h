#pragma once

#include "VansActionHost.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <memory>
#include <vector>

namespace Vans
{
class VansActionScheduler
{
public:
	VansActionSchedulerHandle Register(std::shared_ptr<VansActionHost> host, std::string& error);
	bool Unregister(VansActionSchedulerHandle handle);
	std::shared_ptr<VansActionHost> FindByOwner(VansEntityHandle owner) const;
	std::vector<std::shared_ptr<VansActionHost>> Hosts() const;
	void TickEarly(double deltaSeconds);
	bool RunLateContinuation();
	void Clear();
	std::size_t HostCount() const { return m_Hosts.ActiveCount(); }

private:
	struct HostEntry { std::weak_ptr<VansActionHost> host; };
	void RemoveExpired();

	VansGenerationPool<HostEntry> m_Hosts;
	bool m_LateContinuationRan = false;
};
}
