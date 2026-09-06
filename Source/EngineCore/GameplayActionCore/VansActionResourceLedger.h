#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <functional>
#include <string>
#include <vector>

namespace Vans
{
struct VansActionResourceEntry
{
	std::string type;
	std::string debugName;
	VansActionServiceId service;
	VansGenerationHandle externalResource;
	VansActionResourceHandle dependsOn;
	std::function<bool()> release;
};

struct VansActionResourceSnapshot
{
	VansActionResourceHandle handle;
	std::string type;
	std::string debugName;
	VansActionResourceHandle dependsOn;
};

class VansActionResourceLedger
{
public:
	VansActionResourceHandle Register(VansActionResourceEntry entry, std::string& error);
	bool Release(VansActionResourceHandle handle, std::string& error);
	bool Transfer(
		VansActionResourceHandle handle,
		VansActionResourceLedger& destination,
		VansActionResourceHandle& destinationHandle,
		std::string& error);
	bool ResolveExternal(
		VansActionResourceHandle handle,
		VansActionServiceId& service,
		VansGenerationHandle& externalResource) const;
	bool ForgetExternalResource(VansActionServiceId service, VansGenerationHandle resource);
	bool ReleaseAll(std::vector<std::string>& errors);
	std::vector<VansActionResourceSnapshot> Snapshot() const;
	std::size_t ActiveCount() const { return m_Entries.ActiveCount(); }
	bool IsReleased() const { return m_Released; }
	bool CanAccept() const { return !m_Released; }

private:
	struct State
	{
		VansActionResourceEntry entry;
	};

	VansGenerationPool<State> m_Entries;
	std::vector<VansActionResourceHandle> m_Order;
	bool m_Released = false;
};
}
