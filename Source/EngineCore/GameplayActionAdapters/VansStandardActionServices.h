#pragma once

#include "../GameplayActionCore/VansActionServices.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
const std::vector<VansActionServiceCapability>& VansStandardActionServiceCapabilities();
const VansActionServiceCapability* VansFindStandardActionServiceCapability(
	VansActionServiceId service);
VansSerializedValue VansBuildActionCommandSamplePayload(
	const VansActionCommandSchema& schema);

using VansActionServiceCommandHandler =
	std::function<VansActionCommandResult(const VansActionCommand&)>;
using VansActionServiceReleaseHandler =
	std::function<bool(VansGenerationHandle, std::string&)>;

class VansActionServiceAdapter final : public IVansActionService
{
public:
	explicit VansActionServiceAdapter(VansActionServiceCapability capability);

	bool Bind(std::string_view command, VansActionServiceCommandHandler handler, std::string& error);
	void SetReleaseHandler(VansActionServiceReleaseHandler handler);
	bool ValidateBindings(std::string& error) const;

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

private:
	VansActionServiceCapability m_Capability;
	std::unordered_map<VansActionFieldId, VansActionServiceCommandHandler> m_Handlers;
	VansActionServiceReleaseHandler m_Release;
};

class VansFakeActionService final : public IVansActionService
{
public:
	explicit VansFakeActionService(VansActionServiceCapability capability);

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

	std::size_t ActiveResourceCount() const { return m_Resources.ActiveCount(); }
	std::size_t ExecutedCommandCount() const { return m_ExecutedCommandCount; }

private:
	struct ResourceState
	{
		VansActionFieldId command;
		std::uint64_t sequence = 0;
	};

	VansActionServiceCapability m_Capability;
	VansGenerationPool<ResourceState> m_Resources;
	std::size_t m_ExecutedCommandCount = 0;
	std::uint64_t m_NextSequence = 1;
};

std::vector<std::shared_ptr<VansFakeActionService>> VansCreateFakeStandardActionServices();
bool VansRunActionServiceConformance(
	VansActionServiceRegistry& registry,
	const std::vector<std::shared_ptr<VansFakeActionService>>& services,
	std::string& error);
}
