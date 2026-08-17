#pragma once

#include "VansActionServices.h"

namespace Vans
{
class VansActionScheduler;

class VansActionRoutingService final : public IVansActionService
{
public:
	explicit VansActionRoutingService(VansActionScheduler& scheduler);

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

private:
	VansActionScheduler* m_Scheduler = nullptr;
	VansActionServiceCapability m_Capability;
};
}
